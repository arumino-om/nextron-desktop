/*
 * main.c — UEFI ブートローダ
 *
 * docs/design-issues.md 詳細 D8「本OS での方針」:
 *   ブートローダ       UEFI (x86_64 / arm64 共通)
 *   区画               GPT
 *   ハードウェア列挙   ACPI (x86_64) / Device Tree (arm64)
 *   フレームバッファ   UEFI GOP
 *
 * やること:
 *   1. 自分が読まれたボリュームから \BTRON\KERNEL.ELF を読む
 *   2. PT_LOAD セグメントを p_paddr へ置く（空いていなければ任意位置＋差分）
 *   3. ACPI RSDP / DTB をコンフィグレーションテーブルから探す
 *   4. GOP からフレームバッファの情報を取る
 *   5. メモリマップを取り、BootInfo に畳む
 *   6. ExitBootServices
 *   7. MMU を切ってカーネルへ飛ぶ (arch_jump_to_kernel)
 *
 * 【規律】ExitBootServices 後は AllocatePool も AllocatePages も呼べない。
 * カーネルへ渡すものは**すべてその前に確保しておく**。
 */

#include <btron/bootinfo.h>

#include "efi.h"
#include "loader.h"

/* カーネルイメージのパス。ESP 上に置く。 */
#define KERNEL_PATH L"\\BTRON\\KERNEL.ELF"

/* 緊急画面の検査用の印。あれば BootInfo.flags に立てる（T4-02）。 */
#define PANIC_TEST_PATH L"\\BTRON\\PANICTEST"

/* 検査のあとも電源を落とさず画面を残す印。手で画面を見るためのもの。 */
#define HOLD_PATH L"\\BTRON\\HOLD"

#define EFI_PAGE_SIZE 4096ULL
#define PAGES_OF(n) (((n) + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE)

LOCAL EFI_SYSTEM_TABLE *ST;
LOCAL EFI_BOOT_SERVICES *BS;

/* ------------------------------------------------------------------ */
/* ELF64 (D1)                                                          */
/* ------------------------------------------------------------------ */

#define PT_LOAD 1

typedef struct Elf64Hdr {
	UB e_ident[16];
	UH e_type;
	UH e_machine;
	UW e_version;
	UD e_entry;
	UD e_phoff;
	UD e_shoff;
	UW e_flags;
	UH e_ehsize;
	UH e_phentsize;
	UH e_phnum;
	UH e_shentsize;
	UH e_shnum;
	UH e_shstrndx;
} Elf64Hdr;

typedef struct Elf64Phdr {
	UW p_type;
	UW p_flags;
	UD p_offset;
	UD p_vaddr;
	UD p_paddr;
	UD p_filesz;
	UD p_memsz;
	UD p_align;
} Elf64Phdr;

#define EM_AARCH64 183
#define EM_X86_64 62

#if defined(__aarch64__)
#define EM_EXPECTED EM_AARCH64
#elif defined(__x86_64__)
#define EM_EXPECTED EM_X86_64
#else
#error "未対応のアーキテクチャ"
#endif

/* ------------------------------------------------------------------ */
/* 補助                                                                */
/* ------------------------------------------------------------------ */

LOCAL void die(CONST B *msg, EFI_STATUS st)
{
	lcon_puts("\n*** ローダ異常終了: ");
	lcon_puts(msg);
	lcon_puts(" (status ");
	lcon_hex(st);
	lcon_puts(")\n");
	for (;;) {
		if (BS != 0) {
			BS->Stall(1000000);
		}
	}
}

LOCAL void zero(VP p, UD n)
{
	UB *b = (UB *)p;
	UD i;

	for (i = 0; i < n; i++) {
		b[i] = 0;
	}
}

LOCAL Bool guid_eq(CONST EFI_GUID *a, CONST EFI_GUID *b)
{
	W i;

	if (a->d1 != b->d1 || a->d2 != b->d2 || a->d3 != b->d3) {
		return False;
	}
	for (i = 0; i < 8; i++) {
		if (a->d4[i] != b->d4[i]) {
			return False;
		}
	}
	return True;
}

LOCAL VP alloc_pool(UD size)
{
	VP p = 0;
	EFI_STATUS st = BS->AllocatePool(EfiLoaderData, size, &p);

	if (EFI_ERROR(st)) {
		die("AllocatePool", st);
	}
	zero(p, size);
	return p;
}

/* ------------------------------------------------------------------ */
/* カーネルの読み込み                                                  */
/* ------------------------------------------------------------------ */

typedef struct KernelImage {
	UD entry; /* 物理アドレス */
	UD base;  /* 置いた物理先頭 */
	UD size;  /* バイト数（.bss と起動スタックを含む） */
} KernelImage;

/* 起動したボリュームの根。open_kernel() と印の確認で共用する。 */
LOCAL EFI_FILE_PROTOCOL *volume_root(EFI_HANDLE image)
{
	CONST EFI_GUID li_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
	CONST EFI_GUID fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
	EFI_LOADED_IMAGE_PROTOCOL *li = 0;
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
	EFI_FILE_PROTOCOL *root = 0;
	EFI_STATUS st;

	st = BS->HandleProtocol(image, &li_guid, (VP *)&li);
	if (EFI_ERROR(st)) {
		die("LoadedImage が取れない", st);
	}
	st = BS->HandleProtocol(li->DeviceHandle, &fs_guid, (VP *)&fs);
	if (EFI_ERROR(st)) {
		die("起動したボリュームにファイルシステムが無い", st);
	}
	st = fs->OpenVolume(fs, &root);
	if (EFI_ERROR(st)) {
		die("OpenVolume", st);
	}
	return root;
}

/* 検査用の印があるか見る。無いのが普通なのでエラーにしない。 */
/* ESP に印のファイルが置かれているか見るだけ。
 * **カーネルを作り直さずに振る舞いを切り替える**ための仕掛け（Phase 1 ㉙）。 */
LOCAL Bool has_marker(EFI_HANDLE image, CONST EFI_CHAR16 *path)
{
	EFI_FILE_PROTOCOL *root = volume_root(image);
	EFI_FILE_PROTOCOL *f = 0;
	EFI_STATUS st;

	st = root->Open(root, &f, path, EFI_FILE_MODE_READ, 0);
	if (EFI_ERROR(st)) {
		return False;
	}
	f->Close(f);
	return True;
}

LOCAL EFI_FILE_PROTOCOL *open_kernel(EFI_HANDLE image)
{
	EFI_FILE_PROTOCOL *root = volume_root(image);
	EFI_FILE_PROTOCOL *f = 0;
	EFI_STATUS st;

	st = root->Open(root, &f, (CONST EFI_CHAR16 *)KERNEL_PATH, EFI_FILE_MODE_READ, 0);
	if (EFI_ERROR(st)) {
		lcon_puts("  探したパス: \\BTRON\\KERNEL.ELF\n");
		die("カーネルイメージが開けない", st);
	}
	return f;
}

LOCAL void read_at(EFI_FILE_PROTOCOL *f, UD off, VP buf, UD len)
{
	EFI_UINTN n = len;
	EFI_STATUS st = f->SetPosition(f, off);

	if (EFI_ERROR(st)) {
		die("SetPosition", st);
	}
	st = f->Read(f, &n, buf);
	if (EFI_ERROR(st)) {
		die("Read", st);
	}
	if (n != len) {
		die("Read が短い", (EFI_STATUS)n);
	}
}

LOCAL void load_kernel(EFI_HANDLE image, KernelImage *out)
{
	EFI_FILE_PROTOCOL *f = open_kernel(image);
	Elf64Hdr eh;
	Elf64Phdr *ph;
	UD lo = ~0ULL;
	UD hi = 0;
	UD span;
	EFI_PHYSICAL_ADDRESS base;
	UD delta;
	EFI_STATUS st;
	UH i;

	read_at(f, 0, &eh, sizeof(eh));
	if (eh.e_ident[0] != 0x7F || eh.e_ident[1] != 'E' || eh.e_ident[2] != 'L' ||
	    eh.e_ident[3] != 'F') {
		die("ELF ではない", 0);
	}
	if (eh.e_ident[4] != 2 || eh.e_ident[5] != 1) {
		die("ELF64 リトルエンディアンではない (A4)", 0);
	}
	if (eh.e_machine != EM_EXPECTED) {
		die("アーキテクチャが違う", eh.e_machine);
	}
	if (eh.e_phnum == 0) {
		die("PT_LOAD が無い", 0);
	}

	ph = (Elf64Phdr *)alloc_pool((UD)eh.e_phnum * eh.e_phentsize);
	read_at(f, eh.e_phoff, ph, (UD)eh.e_phnum * eh.e_phentsize);

	/* 物理的に占める範囲を求める */
	for (i = 0; i < eh.e_phnum; i++) {
		if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0) {
			continue;
		}
		if (ph[i].p_paddr < lo) {
			lo = ph[i].p_paddr;
		}
		if (ph[i].p_paddr + ph[i].p_memsz > hi) {
			hi = ph[i].p_paddr + ph[i].p_memsz;
		}
	}
	if (lo > hi) {
		die("PT_LOAD が無い", 0);
	}

	/* 起動スタックは .bss の中（__bss_end の後ろ）に置いてあるので、
	 * p_memsz に含まれている。ここで余白を足す必要はない。
	 * リンカスクリプト側の注記も参照。 */
	lo &= ~(EFI_PAGE_SIZE - 1);
	hi = (hi + EFI_PAGE_SIZE - 1) & ~(EFI_PAGE_SIZE - 1);
	span = hi - lo;

	/* まずはリンク時の物理アドレスをそのまま狙う。QEMU の -kernel 直接起動と
	 * 同じ配置になり、デバッグが楽になる。空いていなければ任意位置へ。 */
	base = lo;
	st = BS->AllocatePages(AllocateAddress, EfiLoaderData, PAGES_OF(span), &base);
	if (EFI_ERROR(st)) {
		base = 0;
		st = BS->AllocatePages(AllocateAnyPages, EfiLoaderData, PAGES_OF(span), &base);
		if (EFI_ERROR(st)) {
			die("カーネル用のメモリが確保できない", st);
		}
	}
	delta = base - lo;

	zero((VP)base, span);
	for (i = 0; i < eh.e_phnum; i++) {
		if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0) {
			continue;
		}
		if (ph[i].p_filesz != 0) {
			read_at(f, ph[i].p_offset, (VP)(ph[i].p_paddr + delta), ph[i].p_filesz);
		}
		/* p_memsz > p_filesz の分（.bss）は zero() 済み */
	}
	f->Close(f);
	BS->FreePool(ph);

	out->entry = eh.e_entry + delta;
	out->base = base;
	out->size = span;

	lcon_puts("  カーネル  : ");
	lcon_hex(base);
	lcon_puts(" .. ");
	lcon_hex(base + span);
	if (delta != 0) {
		/* 差分は負になりうるので、移動前のアドレスを出す */
		lcon_puts("  (リンク時は ");
		lcon_hex(lo);
		lcon_puts(")");
	}
	lcon_puts("\n  エントリ  : ");
	lcon_hex(out->entry);
	lcon_puts("\n");
}

/* ------------------------------------------------------------------ */
/* ハードウェア列挙の入口                                              */
/* ------------------------------------------------------------------ */

LOCAL void find_tables(BootInfo *bi)
{
	CONST EFI_GUID acpi20 = EFI_ACPI_20_TABLE_GUID;
	CONST EFI_GUID acpi10 = EFI_ACPI_10_TABLE_GUID;
	CONST EFI_GUID dtb = EFI_DTB_TABLE_GUID;
	EFI_UINTN i;

	for (i = 0; i < ST->NumberOfTableEntries; i++) {
		CONST EFI_CONFIGURATION_TABLE *t = &ST->ConfigurationTable[i];

		if (guid_eq(&t->VendorGuid, &acpi20)) {
			bi->acpi_rsdp = (UD)t->VendorTable;
		} else if (guid_eq(&t->VendorGuid, &acpi10) && bi->acpi_rsdp == 0) {
			/* 2.0 が無ければ 1.0 で代用する。RSDT しか引けないが無いよりよい。 */
			bi->acpi_rsdp = (UD)t->VendorTable;
		} else if (guid_eq(&t->VendorGuid, &dtb)) {
			bi->dtb = (UD)t->VendorTable;
		}
	}

	lcon_puts("  ACPI RSDP : ");
	if (bi->acpi_rsdp != 0) {
		lcon_hex(bi->acpi_rsdp);
	} else {
		lcon_puts("なし");
	}
	lcon_puts("\n  DTB       : ");
	if (bi->dtb != 0) {
		lcon_hex(bi->dtb);
	} else {
		lcon_puts("なし");
	}
	lcon_puts("\n");
}

LOCAL void find_framebuffer(BootInfo *bi)
{
	CONST EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
	EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
	CONST EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi;
	EFI_STATUS st = BS->LocateProtocol(&gop_guid, 0, (VP *)&gop);

	if (EFI_ERROR(st) || gop == 0 || gop->Mode == 0 || gop->Mode->Info == 0) {
		lcon_puts("  画面      : GOP なし（シリアルのみ）\n");
		bi->fb.format = BFB_NONE;
		return;
	}
	mi = gop->Mode->Info;

	bi->fb.base = gop->Mode->FrameBufferBase;
	bi->fb.size = gop->Mode->FrameBufferSize;
	bi->fb.width = mi->HorizontalResolution;
	bi->fb.height = mi->VerticalResolution;
	/* GOP が返すのは「1 走査線の画素数」。バイト数に直す。 */
	bi->fb.pitch = mi->PixelsPerScanLine * 4U;
	switch (mi->PixelFormat) {
	case PixelBlueGreenRedReserved8BitPerColor:
		bi->fb.format = BFB_BGRA8;
		break;
	case PixelRedGreenBlueReserved8BitPerColor:
		bi->fb.format = BFB_RGBA8;
		break;
	default:
		/* ビットマスク指定や BltOnly は扱わない。 */
		bi->fb.base = 0;
		bi->fb.format = BFB_NONE;
		break;
	}

	/* FrameBufferBase が 0 のドライバがある（EDK2 の VirtioGpuDxe が該当。
	 * Blt 呼び出ししか提供せず、リニアなフレームバッファを見せない）。
	 * ExitBootServices 後は Blt も呼べないので、これは使えない。 */
	if (bi->fb.base == 0) {
		bi->fb.format = BFB_NONE;
		bi->fb.size = 0;
		lcon_puts("  画面      : GOP はあるが直接アクセス不可 (Blt のみ)\n");
		return;
	}

	lcon_puts("  画面      : ");
	lcon_dec(bi->fb.width);
	lcon_puts("x");
	lcon_dec(bi->fb.height);
	lcon_puts(" pitch ");
	lcon_dec(bi->fb.pitch);
	lcon_puts(" @ ");
	lcon_hex(bi->fb.base);
	lcon_puts("\n");
}

/* ------------------------------------------------------------------ */
/* メモリマップ                                                        */
/* ------------------------------------------------------------------ */

LOCAL UW efi_type_to_bmem(UW t)
{
	switch (t) {
	case EfiConventionalMemory:
	case EfiBootServicesCode:
	case EfiBootServicesData:
		/* ExitBootServices の後はブートサービスの領域も自由に使える。 */
		return BMEM_FREE;
	case EfiLoaderCode:
	case EfiLoaderData:
		/* カーネル本体と BootInfo がここにある。踏ませない。 */
		return BMEM_LOADER;
	case EfiRuntimeServicesCode:
	case EfiRuntimeServicesData:
	case EfiACPIReclaimMemory:
	case EfiACPIMemoryNVS:
		/* ACPI テーブルとランタイムサービスはこの中にある。残す。 */
		return BMEM_FIRMWARE;
	default:
		return BMEM_UNUSABLE;
	}
}

/* UEFI のメモリマップは物理アドレス順に並んでいるとは限らない
 * （実測: QEMU + OVMF では MMIO の 0x0400_0000 が最後に来た）。
 * カーネル側は昇順を前提にしたいので、ここで並べ替える。
 * 要素長が desc_size で可変なのでバイト単位で入れ替える。 */
LOCAL void sort_memory_map(EFI_MEMORY_DESCRIPTOR *map, UD count, UD desc_size)
{
	UB *base = (UB *)map;
	UD i;
	UD j;

	if (desc_size > 256) {
		return; /* 想定外に大きい。並べ替えを諦める */
	}
	for (i = 1; i < count; i++) {
		UB tmp[256];
		UB *dst;
		UD key;
		UD at;

		for (j = 0; j < desc_size; j++) {
			tmp[j] = base[i * desc_size + j];
		}
		key = ((CONST EFI_MEMORY_DESCRIPTOR *)tmp)->PhysicalStart;

		/* key より大きい要素を 1 つ分ずつ後ろへずらす */
		for (at = i; at > 0; at--) {
			UB *prev = base + (at - 1) * desc_size;

			if (((CONST EFI_MEMORY_DESCRIPTOR *)prev)->PhysicalStart <= key) {
				break;
			}
			for (j = 0; j < desc_size; j++) {
				prev[desc_size + j] = prev[j];
			}
		}
		dst = base + at * desc_size;
		for (j = 0; j < desc_size; j++) {
			dst[j] = tmp[j];
		}
	}
}

/* 隣接する同種の領域を 1 本に畳む。UEFI のマップは細切れで、
 * BOOTINFO_MAX_REGIONS に収めるには畳まないと溢れる。
 * 呼ぶ前に sort_memory_map() で昇順にしておくこと。 */
LOCAL UD fold_memory_map(CONST EFI_MEMORY_DESCRIPTOR *map, UD count, UD desc_size,
                         BootMemRegion *out, UD max)
{
	UD n = 0;
	UD i;

	for (i = 0; i < count; i++) {
		CONST EFI_MEMORY_DESCRIPTOR *d =
		    (CONST EFI_MEMORY_DESCRIPTOR *)((CONST UB *)map + i * desc_size);
		UW type = efi_type_to_bmem(d->Type);
		UD base = d->PhysicalStart;
		UD size = d->NumberOfPages * EFI_PAGE_SIZE;

		if (size == 0) {
			continue;
		}
		if (n > 0 && out[n - 1].type == type && out[n - 1].base + out[n - 1].size == base) {
			out[n - 1].size += size;
			continue;
		}
		if (n >= max) {
			/* 溢れた分は切り捨てるのではなく、最後の領域を伸ばして
			 * 「使ってはならない」側に寄せる。安全側に倒す。 */
			if (n > 0) {
				out[n - 1].type = BMEM_UNUSABLE;
				out[n - 1].size = base + size - out[n - 1].base;
			}
			continue;
		}
		out[n].base = base;
		out[n].size = size;
		out[n].type = type;
		out[n].resv = 0;
		n++;
	}
	return n;
}

/* ------------------------------------------------------------------ */
/* 入口                                                                */
/* ------------------------------------------------------------------ */

EXPORT EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *systab);

EXPORT EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *systab)
{
	KernelImage ki;
	BootInfo *bi;
	BootMemRegion *regions;
	EFI_PHYSICAL_ADDRESS handoff = 0;
	UD handoff_size;
	EFI_MEMORY_DESCRIPTOR *map;
	EFI_UINTN map_size;
	EFI_UINTN map_key;
	EFI_UINTN desc_size;
	UW desc_ver;
	EFI_STATUS st;
	W tries;

	ST = systab;
	BS = systab->BootServices;
	lcon_init(systab);

	lcon_puts("\nBTRON3 準拠 OS — UEFI ローダ\n");

	load_kernel(image, &ki);

	/* BootInfo と領域配列はカーネルへ渡す。ExitBootServices 後も生きていて、
	 * かつ MMU を切った後に物理アドレスで読める必要がある。
	 * EfiLoaderData で確保すれば BMEM_LOADER に分類され、カーネルが
	 * 自分のページテーブル用に踏むこともない。 */
	handoff_size = sizeof(BootInfo) + sizeof(BootMemRegion) * BOOTINFO_MAX_REGIONS;
	st = BS->AllocatePages(AllocateAnyPages, EfiLoaderData, PAGES_OF(handoff_size),
	                       &handoff);
	if (EFI_ERROR(st)) {
		die("引き継ぎ情報のメモリが確保できない", st);
	}
	zero((VP)handoff, PAGES_OF(handoff_size) * EFI_PAGE_SIZE);

	bi = (BootInfo *)handoff;
	regions = (BootMemRegion *)(handoff + sizeof(BootInfo));

	bi->magic = BTRON_BOOTINFO_MAGIC;
	bi->version = BTRON_BOOTINFO_VERSION;
	bi->size = (UW)sizeof(BootInfo);
	bi->kernel_pa = ki.base;
	bi->kernel_size = ki.size;
	bi->mem_pa = (UD)(VP)regions;
	bi->efi_system_table = (UD)(VP)systab;
	bi->flags = 0;
	if (has_marker(image, (CONST EFI_CHAR16 *)PANIC_TEST_PATH)) {
		bi->flags |= BI_F_PANIC_TEST;
		lcon_puts("  印        : PANICTEST（緊急画面の検査用）\n");
	}
	if (has_marker(image, (CONST EFI_CHAR16 *)HOLD_PATH)) {
		bi->flags |= BI_F_HOLD;
		lcon_puts("  印        : HOLD（検査後も画面を残す）\n");
	}

	find_tables(bi);
	find_framebuffer(bi);

	/* メモリマップ取得用のバッファ。GetMemoryMap は必要量を返すので
	 * 2 回呼ぶ。確保そのものがマップを変えるので余裕を足す。 */
	map_size = 0;
	map = 0;
	st = BS->GetMemoryMap(&map_size, 0, &map_key, &desc_size, &desc_ver);
	if (st != EFI_BUFFER_TOO_SMALL) {
		die("GetMemoryMap(サイズ問い合わせ)", st);
	}
	map_size += desc_size * 16;
	map = (EFI_MEMORY_DESCRIPTOR *)alloc_pool(map_size);

	/* ExitBootServices は MapKey が古いと EFI_INVALID_PARAMETER を返す。
	 * そのときはマップを取り直して再試行する（仕様が要求する手順）。 */
	for (tries = 0; tries < 8; tries++) {
		EFI_UINTN sz = map_size;

		st = BS->GetMemoryMap(&sz, map, &map_key, &desc_size, &desc_ver);
		if (EFI_ERROR(st)) {
			die("GetMemoryMap", st);
		}
		sort_memory_map(map, sz / desc_size, desc_size);
		bi->mem_count =
		    fold_memory_map(map, sz / desc_size, desc_size, regions, BOOTINFO_MAX_REGIONS);

		st = BS->ExitBootServices(image, map_key);
		if (!EFI_ERROR(st)) {
			break;
		}
	}
	if (EFI_ERROR(st)) {
		die("ExitBootServices", st);
	}

	/* ---- ここから先はファームウェアのサービスを呼べない ----
	 * ConOut も Serial I/O もブートサービスのドライバなので使えない。
	 * 以後の出力はカーネルの UART ドライバが行う。
	 * lcon_* を無効化しておき、万一 die() に落ちても暴走させない。 */
	lcon_shutdown();
	BS = 0;

	/* MMU とキャッシュを切ってカーネルへ。カーネルイメージと引き継ぎ情報を
	 * PoC まで書き出す必要があるので、範囲を渡す。
	 * 両者は離れていることがあるので、カーネル側だけを渡し、引き継ぎ情報は
	 * arch 側で別に処理させる…のではなく、ここでは連続した 2 範囲として
	 * 扱えるよう、低い方から高い方までを 1 範囲にして渡す。 */
	{
		UD lo = ki.base < handoff ? ki.base : handoff;
		UD hi_k = ki.base + ki.size;
		UD hi_h = handoff + PAGES_OF(handoff_size) * EFI_PAGE_SIZE;
		UD hi = hi_k > hi_h ? hi_k : hi_h;

		arch_jump_to_kernel(ki.entry, (UD)(VP)bi, lo, hi - lo);
	}
}

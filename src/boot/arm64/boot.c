/*
 * boot.c — arm64 の C 側入口
 *
 * 起動は 2 段構えになっている。
 *
 *   boot_early()  物理アドレスで走る。コンソールとページテーブルを用意し
 *                 MMU を有効にして戻る。恒等写像が残っているので戻れる。
 *   boot_main()   start.S が上位半分 (BTRON_KERNEL_BASE) へジャンプした
 *                 後に走る。ここで恒等写像を捨て、TTBR0 をプロセスに明け渡す。
 */

#include <btron/bootinfo.h>
#include <btron/hal.h>
#include <btron/machine.h>
#include <btron/test.h>
#include <btron/vmem.h>

EXPORT void boot_early(UD arg);
EXPORT void boot_main(UD arg);

IMPORT B __kernel_start[];
IMPORT B __kernel_end[];

/* ページテーブル用の早期プール。規律 R4 で上位半分の最上段 256 エントリを
 * 全数確保するだけで 1MB 強を使うので、余裕を見て 16MB 取る。 */
#define EARLY_POOL_SIZE (16UL * 1024 * 1024)

/* QEMU virt の RAM は 0x4000_0000 から 512MB。
 * UEFI 経由ならメモリマップが来るので使わない。-kernel 直接起動のときだけ、
 * 「カーネルの直後」を自分で決めるための上限として使う。 */
#define QEMU_VIRT_RAM_BASE 0x40000000ULL
#define QEMU_VIRT_RAM_SIZE (512ULL * 1024 * 1024)

LOCAL void put_dec(UD v)
{
	B buf[24];
	W i = 0;

	do {
		buf[i++] = (B)('0' + (v % 10));
		v /= 10;
	} while (v > 0 && i < (W)sizeof(buf));
	while (i > 0) {
		hal_console_putc(buf[--i]);
	}
}

LOCAL void put_hex(UD v)
{
	CONST B *digits = "0123456789abcdef";
	W i;

	hal_console_puts("0x");
	for (i = 60; i >= 0; i -= 4) {
		hal_console_putc(digits[(v >> (UW)i) & 0xF]);
	}
}

/* ------------------------------------------------------------------ */
/* 第 1 段: 物理アドレスで走る                                         */
/* ------------------------------------------------------------------ */

EXPORT void boot_early(UD arg)
{
	Bool via_uefi;
	UD ps;
	UD pool;

	hal_console_init();
	hal_exception_init(); /* これより後のフォルトは無言のハングではなく報告になる */

	hal_console_puts("\n");
	hal_console_puts("BTRON3 準拠 OS — arm64\n");

	/* 引き継ぎ情報を取り込む。ローダが置いた領域はこの先写像されないので、
	 * MMU を有効にする前にカーネル内へ複製しておく。 */
	via_uefi = bootinfo_take(arg);

	/* カーネルが実際に置かれた物理先頭を HAL に教える。UEFI ローダは
	 * リンク時の LMA が空いていなければ別の場所へ置く（規律 R17）。 */
	hal_kernel_set_pa_base(via_uefi ? bootinfo()->kernel_pa : 0);

	hal_console_puts("  起動経路  : ");
	hal_console_puts(via_uefi ? "UEFI\n" : "QEMU -kernel（直接起動）\n");

	/* ページテーブルの置き場。
	 *
	 * UEFI 経由ならメモリマップから空き領域を選ぶ。直接起動のときは
	 * メモリマップが無いので、カーネルの直後を決め打ちで使う。
	 *
	 * いずれにしても __kernel_end のアドレスを直接使ってはいけない。物理で
	 * 走っている間、adrp が返すのは物理アドレスで、上位半分へ移った後は
	 * VMA になる（規律 R17）。段によらず同じ値を返す HAL 経由で取る。 */
	ps = (UD)hal_page_size();
	pool = (hal_kernel_pa_end() + ps - 1) & ~(ps - 1);
	if (via_uefi) {
		UD found = bootinfo_find_free(EARLY_POOL_SIZE, pool);

		if (found == 0) {
			/* カーネルより後ろに空きが無い。前から探し直す。 */
			found = bootinfo_find_free(EARLY_POOL_SIZE, 0);
		}
		if (found == 0) {
			hal_console_puts("  空き物理メモリが見つからない\n");
			hal_exit(1);
		}
		pool = found;
	} else if (pool + EARLY_POOL_SIZE > QEMU_VIRT_RAM_BASE + QEMU_VIRT_RAM_SIZE) {
		hal_console_puts("  RAM が足りない\n");
		hal_exit(1);
	}
	pmem_early_init(pool, EARLY_POOL_SIZE);

	if (hal_vm_init() != 0) {
		hal_console_puts("  hal_vm_init に失敗\n");
		hal_exit(1);
	}
	/* ここに到達した時点で MMU は有効。まだ恒等写像の上で走っている。 */
}

/* ------------------------------------------------------------------ */
/* 第 2 段: 上位半分で走る                                             */
/* ------------------------------------------------------------------ */

LOCAL CONST B *mem_type_name(UW t)
{
	switch (t) {
	case BMEM_FREE:
		return "空き    ";
	case BMEM_LOADER:
		return "ローダ  ";
	case BMEM_FIRMWARE:
		return "FW 予約 ";
	default:
		return "使用不可";
	}
}

LOCAL void show_bootinfo(void)
{
	CONST BootInfo *bi = bootinfo();
	UD total = 0;
	UW i;

	hal_console_puts("  ACPI RSDP : ");
	if (bi->acpi_rsdp != 0) {
		put_hex(bi->acpi_rsdp);
	} else {
		hal_console_puts("なし");
	}
	hal_console_puts("\n  DTB       : ");
	if (bi->dtb != 0) {
		put_hex(bi->dtb);
	} else {
		hal_console_puts("なし");
	}
	hal_console_puts("\n  画面      : ");
	if (bi->fb.format != BFB_NONE && bi->fb.base != 0) {
		put_dec(bi->fb.width);
		hal_console_puts("x");
		put_dec(bi->fb.height);
		hal_console_puts(" pitch ");
		put_dec(bi->fb.pitch);
		hal_console_puts(" @ ");
		put_hex(bi->fb.base);
	} else {
		hal_console_puts("なし（シリアルのみ）");
	}
	hal_console_puts("\n");

	if (bootinfo_mem_count() == 0) {
		hal_console_puts("  メモリマップ: なし\n");
		return;
	}
	hal_console_puts("  メモリマップ ");
	put_dec(bootinfo_mem_count());
	hal_console_puts(" 領域\n");
	for (i = 0; i < bootinfo_mem_count(); i++) {
		CONST BootMemRegion *r = bootinfo_mem(i);

		if (r->type == BMEM_FREE) {
			total += r->size;
		}
		/* 全部出すと長いので、1MB 以上の領域だけ出す */
		if (r->size < 1024 * 1024) {
			continue;
		}
		hal_console_puts("    ");
		put_hex(r->base);
		hal_console_puts(" + ");
		put_dec(r->size / 1024 / 1024);
		hal_console_puts("MB  ");
		hal_console_puts(mem_type_name(r->type));
		hal_console_puts("\n");
	}
	hal_console_puts("  空き合計  : ");
	put_dec(total / 1024 / 1024);
	hal_console_puts("MB\n");
}

EXPORT void boot_main(UD arg)
{
	(void)arg;

	/* UART を MMIO 区画の別名に切り替える。これで恒等写像を捨てられる。 */
	hal_console_remap();

	/* 例外ベクタを張り直す。第 1 段で入れた VBAR_EL1 は btron_vectors の
	 * 物理アドレスを指しており、恒等写像を捨てると届かなくなる。
	 * ここは上位半分なので adrp は VMA を返す。 */
	hal_exception_init();

	hal_console_puts("  上位半分へ移行しました\n");
	hal_console_puts("  カーネル VMA : ");
	put_hex((UD)(VP)__kernel_start);
	hal_console_puts("\n  カーネル PA  : ");
	put_hex(hal_kva_to_pa((UD)(VP)__kernel_start));
	hal_console_puts("\n  ページテーブル: ");
	put_dec(pmem_early_used() / 1024);
	hal_console_puts(" KB\n");

	show_bootinfo();

	BTRON_TEST("基本データタイプの幅", {
		BTRON_CHECK("幅", sizeof(B) == 1);
		BTRON_CHECK("幅", sizeof(H) == 2);
		BTRON_CHECK("幅", sizeof(W) == 4);
		BTRON_CHECK("幅", sizeof(D) == 8);
		BTRON_CHECK("幅", sizeof(UD) == 8);
		BTRON_CHECK("幅", sizeof(VP) == 8);
	});

	BTRON_TEST("文字コード (A3 / B1)", {
		BTRON_CHECK("TC", sizeof(TC) == 2);
		BTRON_CHECK("WTC", sizeof(WTC) == 4);
		BTRON_CHECK("pack", wtclang(towtc(TSC_SYS, (TC)0x2121)) == TSC_SYS);
		BTRON_CHECK("pack", wtcchar(towtc(TSC_SYS, (TC)0x2121)) == 0x2121);
	});

	BTRON_TEST("STIME の 64bit 化 (A5)", {
		BTRON_CHECK("幅", sizeof(STIME) == 8);
		/* 2053 年を越えても溢れないこと。1985 起点で 100 年分の秒数。 */
		STIME far_future = (STIME)100 * 365 * 24 * 60 * 60;
		BTRON_CHECK("範囲", far_future > 0);
	});

	BTRON_TEST("LINK = 56 byte (A2)", {
		BTRON_CHECK("サイズ", sizeof(LINK) == 56);
		BTRON_CHECK("f_id の幅", sizeof(((LINK *)0)->f_id) == 4);
	});

	BTRON_TEST("エンディアン (A4)", {
		UW v = 0x01020304U;
		UB *p = (UB *)&v;
		BTRON_CHECK("リトル", p[0] == 0x04);
		BTRON_CHECK("BIGENDIAN", BIGENDIAN == 0);
	});

	BTRON_TEST("ページサイズは HAL 経由 (A13 / R2)", {
		BTRON_CHECK("4KB", hal_page_size() == 4096);
		BTRON_CHECK("shift", hal_page_shift() == 12);
		BTRON_CHECK("整合", (1U << hal_page_shift()) == hal_page_size());
	});

	BTRON_TEST("プラットフォーム ID (H2)", {
		BTRON_CHECK("CPU", CPU_ID == CPU_ARM64);
		BTRON_CHECK("機種", MACHINE_ID == MACH_ARM64);
		BTRON_CHECK("仕様", BTRON_SPEC == (SPEC_BTRON | 0x320));
	});

	BTRON_TEST("アドレス空間レイアウト (§3.2-1 / BTRON3 §1.5)", {
		/* BTRON3 §1.5「共有・システムメモリは負、ローカルは正」 */
		BTRON_CHECK("共有は負", (D)BTRON_SHARED_BASE < 0);
		BTRON_CHECK("固有は正", (D)(BTRON_USER_LIMIT - 1) > 0);
		BTRON_CHECK("各半分 128TB", BTRON_USER_LIMIT == (1ULL << BTRON_VA_BITS));
	});

	BTRON_TEST("上位半分の区画は 512GB 境界 (§3.2-3)", {
		CONST UD g = 512ULL * 1024 * 1024 * 1024;
		BTRON_CHECK("directmap", (BTRON_DIRECTMAP_BASE % g) == 0);
		BTRON_CHECK("common", (BTRON_COMMON_BASE % g) == 0);
		BTRON_CHECK("system", (BTRON_SYSTEM_BASE % g) == 0);
		BTRON_CHECK("maprec", (BTRON_MAPREC_BASE % g) == 0);
		BTRON_CHECK("mmio", (BTRON_MMIO_BASE % g) == 0);
		BTRON_CHECK("kernel", (BTRON_KERNEL_BASE % g) == 0);
	});

	BTRON_TEST("カーネルは上位半分で走っている", {
		UD pc;
		__asm__ volatile("adr %0, ." : "=r"(pc));
		BTRON_CHECK("PC が上位半分", pc >= BTRON_SHARED_BASE);
		BTRON_CHECK("PC がカーネル区画", pc >= BTRON_KERNEL_BASE);
		BTRON_CHECK("リンク先も上位半分",
		            (UD)(VP)__kernel_start >= BTRON_KERNEL_BASE);
		/* スタックも上位半分にある */
		BTRON_CHECK("SP が上位半分", (UD)(VP)&pc >= BTRON_SHARED_BASE);
	});

	BTRON_TEST("VMA <-> LMA の換算 (R4 の前提)", {
		BTRON_CHECK("基準", hal_kva_to_pa(BTRON_KERNEL_BASE) == hal_kernel_pa_base());
		BTRON_CHECK("整合",
		            hal_kva_to_pa((UD)(VP)__kernel_start) == hal_kernel_pa_base());
		BTRON_CHECK("終端",
		            hal_kva_to_pa((UD)(VP)__kernel_end) == hal_kernel_pa_end());
		BTRON_CHECK("物理は低位", hal_kernel_pa_base() < BTRON_USER_LIMIT);
		BTRON_CHECK("大きさ",
		            hal_kernel_pa_end() - hal_kernel_pa_base()
		                == (UD)(VP)__kernel_end - (UD)(VP)__kernel_start);
	});

	BTRON_TEST("引き継ぎ情報 (D8)", {
		CONST BootInfo *bi = bootinfo();

		BTRON_CHECK("magic", bi->magic == BTRON_BOOTINFO_MAGIC);
		BTRON_CHECK("version", bi->version == BTRON_BOOTINFO_VERSION);
		BTRON_CHECK("size", bi->size == sizeof(BootInfo));
		/* 範囲外アクセスは 0 を返す */
		BTRON_CHECK("範囲外", bootinfo_mem(bootinfo_mem_count()) == 0);

		if (bi->efi_system_table != 0) {
			/* UEFI 経由なら、ハードウェア列挙の入口とメモリマップが
			 * 必ず来ているはず。arm64 の QEMU virt は DTB、
			 * x86_64 は ACPI (詳細 D8)。 */
			BTRON_CHECK("列挙の入口", bi->dtb != 0 || bi->acpi_rsdp != 0);
			BTRON_CHECK("メモリマップ", bootinfo_mem_count() > 0);
			BTRON_CHECK("カーネル位置", bi->kernel_pa == hal_kernel_pa_base());
			BTRON_CHECK("カーネル大きさ",
			            bi->kernel_size >= hal_kernel_pa_end() - hal_kernel_pa_base());
		} else {
			/* QEMU -kernel の直接起動。ELF 直接起動では x0 に何も入らない
			 * ので引き継ぎ情報は空。カーネルが自分で決め打ちする。 */
			BTRON_CHECK("マップなし", bootinfo_mem_count() == 0);
		}
	});

	BTRON_TEST("メモリマップの整合", {
		UW n = bootinfo_mem_count();
		UD ps = (UD)hal_page_size();
		UD prev_end = 0;
		Bool sorted = True;
		Bool aligned = True;
		Bool kernel_covered = (n == 0);
		UD kb = hal_kernel_pa_base();
		UW i;

		for (i = 0; i < n; i++) {
			CONST BootMemRegion *r = bootinfo_mem(i);

			if (r->base < prev_end) {
				sorted = False;
			}
			if ((r->base & (ps - 1)) != 0 || (r->size & (ps - 1)) != 0) {
				aligned = False;
			}
			/* カーネルを置いた領域が「空き」に見えていてはならない。
			 * 空きに見えると周辺核のメモリ管理が自分を踏む。 */
			if (kb >= r->base && kb < r->base + r->size) {
				kernel_covered = (r->type != BMEM_FREE);
			}
			prev_end = r->base + r->size;
		}
		BTRON_CHECK("昇順で重なりなし", sorted == True);
		BTRON_CHECK("ページ境界", aligned == True);
		BTRON_CHECK("カーネル領域は空きでない", kernel_covered == True);
	});

	BTRON_TEST("フレームバッファの整合 (F6 / Phase 4 の前提)", {
		CONST BootFrameBuffer *fb = &bootinfo()->fb;

		if (fb->format == BFB_NONE || fb->base == 0) {
			/* GOP が無い、またはリニアなフレームバッファを見せない
			 * ドライバだった。シリアルだけで進む。 */
			BTRON_CHECK("無効なら base も 0", fb->base == 0);
		} else {
			UW i;
			Bool protected_region = (bootinfo_mem_count() == 0);

			BTRON_CHECK("pitch", fb->pitch >= fb->width * 4);
			BTRON_CHECK("大きさ", fb->size >= (UD)fb->pitch * fb->height);
			/* フレームバッファの物理領域が「空き」に見えてはならない。
			 * 空きに見えると周辺核のメモリ管理が画面を踏む。 */
			for (i = 0; i < bootinfo_mem_count(); i++) {
				CONST BootMemRegion *r = bootinfo_mem(i);

				if (fb->base >= r->base && fb->base < r->base + r->size) {
					protected_region = (r->type != BMEM_FREE);
				}
			}
			BTRON_CHECK("空きでない", protected_region == True);
		}
	});

	BTRON_TEST("早期プールの位置 (R4 の前提)", {
		UD pb = pmem_early_base();
		UD kb = hal_kernel_pa_base();
		UD ke = hal_kernel_pa_end();

		/* ページテーブルの置き場がカーネル本体と重なっていないこと */
		BTRON_CHECK("重なりなし", pb >= ke || pmem_early_limit() <= kb);
		BTRON_CHECK("確保済みがある", pmem_early_used() > 0);
		BTRON_CHECK("上限内", pmem_early_base() + pmem_early_used()
		                          <= pmem_early_limit());
	});

	BTRON_TEST("直接マップが引ける", {
		CONST AddrSpace *ks = hal_vm_kernel_space();
		UD kpa = hal_kva_to_pa((UD)(VP)__kernel_start);
		BTRON_CHECK("往復", hal_vm_translate(ks, BTRON_PA_TO_VA(kpa)) == kpa);
		BTRON_CHECK("読める", *(volatile UW *)(VP)BTRON_PA_TO_VA(kpa) != 0);
	});

	/* ---- 恒等写像を捨てて、下位半分をプロセスに明け渡す ---- */
	BTRON_TEST("恒等写像を捨てる", {
		UD kpa = hal_kva_to_pa((UD)(VP)__kernel_start);
		hal_vm_drop_identity();
		/* ここに到達できれば、上位半分だけで実行が続いている */
		BTRON_CHECK("生存", hal_vm_enabled() == True);
		BTRON_CHECK("直接マップは健在",
		            *(volatile UW *)(VP)BTRON_PA_TO_VA(kpa) != 0);
	});

	BTRON_TEST("アドレス空間の生成と切替 (規律 R3)", {
		AddrSpace as;
		UD pa = pmem_early_alloc_page();
		CONST UD va = 0x0000400000000000ULL; /* 下位半分のどこか */

		BTRON_CHECK("ページ確保", pa != 0);
		BTRON_CHECK("生成", hal_vm_create(&as) == 0);
		BTRON_CHECK("ASID", as.asid != 0);
		BTRON_CHECK("マップ", hal_vm_map(&as, va, pa, (UD)hal_page_size(),
		                                 BTRON_PG_READ | BTRON_PG_WRITE) == 0);
		BTRON_CHECK("変換", hal_vm_translate(&as, va) == pa);
		BTRON_CHECK("未マップは 0", hal_vm_translate(&as, va + 0x10000000ULL) == 0);

		/* 足場なしで切り替えられる。カーネルは上位半分で走り続ける。 */
		hal_vm_switch(&as);
		*(volatile UW *)(VP)va = 0xB7120000U;
		BTRON_CHECK("書いて読める", *(volatile UW *)(VP)va == 0xB7120000U);
		BTRON_CHECK("共有側から同一視",
		            *(volatile UW *)(VP)BTRON_PA_TO_VA(pa) == 0xB7120000U);

		/* 別の空間へ切り替えると、さっきのページは見えなくなる */
		{
			AddrSpace other;
			BTRON_CHECK("2 つめ", hal_vm_create(&other) == 0);
			hal_vm_switch(&other);
			BTRON_CHECK("分離", hal_vm_translate(&other, va) == 0);
		}
	});

	hal_exit(btron_test_report());
}

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
#include <btron/font.h>
#include <btron/hal.h>
#include <btron/intc.h>
#include <btron/kernel.h>
#include <btron/machdesc.h>
#include <btron/machine.h>
#include <btron/screen.h>
#include <btron/test.h>
#include <btron/vmem.h>

EXPORT void boot_early(UD arg);
EXPORT void boot_main(UD arg);

/* src/kernel/core/selftest.c。検査タスクを作って中心核を起動する。戻らない。 */
IMPORT void knl_selftest_start(void) __attribute__((noreturn));

LOCAL void draw_splash(void);

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

	hal_cpu_init();       /* SIMD/FP のトラップを外す。コンテキスト切替が要求する */
	hal_console_init();
	hal_exception_init(); /* これより後のフォルトは無言のハングではなく報告になる */

	hal_console_puts("\n");
	hal_console_puts("BTRON3 準拠 OS — arm64\n");

	/* 引き継ぎ情報を取り込む。ローダが置いた領域はこの先写像されないので、
	 * MMU を有効にする前にカーネル内へ複製しておく。 */
	via_uefi = bootinfo_take(arg);

	/* ハードウェア構成を読む（詳細 D8）。DT / ACPI のテーブルも
	 * ファームウェアの領域にあり、この先写像されない。いまは MMU が
	 * 無効で全物理メモリが見えているので、ここで読んで複製する。 */
	if (machdesc_probe()) {
		CONST MachDesc *md = machdesc();

		/* シリアルを記述どおりに差し替える。既定値は記述が読めない
		 * ときの保険であって、本来のアドレスはここから来る。 */
		if (hal_console_set(md->uart_type, md->uart_pa)) {
			hal_console_puts("  シリアル  : ");
			hal_console_puts(md_uart_name(md->uart_type));
			hal_console_puts(" @ ");
			put_hex(md->uart_pa);
			hal_console_puts("\n");
		}
	}

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

/* 起動画面。画面が立ったことを目で見て分かるようにする。
 *
 * 文字が出れば B8（グリフデータ）と UTF-8 → グリフの経路まで一度に
 * 確かめられる。色は BGRA / RGBA の差を screen_rgb() が吸収している。 */
LOCAL void draw_splash(void)
{
	CONST Screen *s = screen();
	UW w = s->width;
	UW i;

	CONST UW band = screen_rgb(0x1E, 0x3A, 0x5F);

	/* 上端に帯を引き、その中に表題を置く。
	 * この 3 行は固定領域にするので、以後の出力でスクロールしない。 */
	screen_fill(0, 0, (W)w, 3 * 16 + 4, band);
	screen_text_at(1, 0, "BTRON3 準拠 OS — 画面コンソール", screen_rgb(0xF0, 0xF4, 0xFF),
	               band);
	screen_text_at(1, 1, "かな・漢字も出ます（GNU Unifont / B8）",
	               screen_rgb(0xA8, 0xC0, 0xE0), band);

	/* 色の帯。画素形式 (BGRA / RGBA) を取り違えていれば色が入れ替わるので、
	 * 目で見て分かる。赤・緑・青の順に明るくなっていれば正しい。 */
	for (i = 0; i < 8; i++) {
		UW r = ((i & 1U) != 0) ? 0xFFU : 0x20U;
		UW g = ((i & 2U) != 0) ? 0xFFU : 0x20U;
		UW b = ((i & 4U) != 0) ? 0xFFU : 0x20U;

		screen_fill((W)(i * (w / 8U)), 2 * 16 + 2, (W)(w / 8U), 14,
		            screen_rgb(r, g, b));
	}

	screen_console_reserve(3);
	screen_console_color(screen_rgb(0xD0, 0xD8, 0xE0), screen_rgb(0x10, 0x14, 0x20));
	hal_console_puts("  画面コンソール: ");
	put_dec(screen_console_cols());
	hal_console_puts(" 桁 x ");
	put_dec(screen_console_rows());
	hal_console_puts(" 行\n");
}

LOCAL void show_machdesc(void)
{
	CONST MachDesc *md = machdesc();
	UW i;

	hal_console_puts("  ハード記述: ");
	hal_console_puts(md_source_name(md->source));
	if (md->source == MD_SRC_NONE) {
		hal_console_puts("（ハードウェアは既定値で動かしている）\n");
		return;
	}
	hal_console_puts("\n    シリアル  : ");
	hal_console_puts(md_uart_name(md->uart_type));
	hal_console_puts(" @ ");
	put_hex(md->uart_pa);
	hal_console_puts("\n    割込み    : ");
	hal_console_puts(md_intc_name(md->intc_type));
	hal_console_puts(" dist ");
	put_hex(md->intc_dist_pa);
	hal_console_puts(" cpu ");
	put_hex(md->intc_cpu_pa);
	hal_console_puts("\n    タイマ    : INTID");
	for (i = 0; i < MD_TIMER_COUNT; i++) {
		hal_console_puts(" ");
		put_dec(md->timer_intid[i]);
	}
	hal_console_puts(" (EL1 物理 = ");
	put_dec(md->timer_intid[MD_TIMER_NONSECURE]);
	hal_console_puts(", ");
	put_dec(hal_timer_freq() / 1000000);
	hal_console_puts("MHz)\n    CPU       : ");
	put_dec(md->cpu_count);
	hal_console_puts(" 個  id");
	for (i = 0; i < md->cpu_count && i < MD_MAX_CPUS; i++) {
		hal_console_puts(" ");
		put_hex(md->cpu_id[i]);
	}
	hal_console_puts("\n    CPU 起動  : ");
	hal_console_puts(md_boot_name(md->boot_method));
	hal_console_puts("\n");
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

	/* 物理ページアロケータ。直接マップが張られた後でないと立てられない
	 * （確保したページをゼロクリアするのに直接マップ経由で触る）。
	 * メモリマップが無い直接起動では立たない。黙って先へ進む。 */
	if (pmem_init() == E_OK) {
		hal_console_puts("  物理メモリ  : ");
		put_dec(pmem_total_pages() * hal_page_size() / 1024 / 1024);
		hal_console_puts("MB (");
		put_dec(pmem_total_pages());
		hal_console_puts(" ページ) 管理下\n");
	}

	show_bootinfo();
	show_machdesc();

	/* 画面を立てる。これ以降 hal_console_puts() の出力が画面にも出る
	 * （E24: 画面はカーネルが保有する / 規律 R18）。
	 * 画面が無い構成では黙ってシリアルだけで進む。 */
	if (screen_init() == E_OK && screen_console_init() == E_OK) {
		draw_splash();
	}

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

	BTRON_TEST("ハードウェア記述 (D8)", {
		CONST MachDesc *md = machdesc();

		if (bootinfo()->dtb == 0 && bootinfo()->acpi_rsdp == 0) {
			/* QEMU -kernel 直接起動。記述が無いので既定値で動いている。 */
			BTRON_CHECK("記述なし", md->source == MD_SRC_NONE);
			BTRON_CHECK("既定値で動作", hal_console_pa() != 0);
		} else {
			BTRON_CHECK("出どころ", md->source != MD_SRC_NONE);
			/* シリアル。これが読めないと実機で何も出せない。 */
			BTRON_CHECK("シリアル種別", md->uart_type == MD_UART_PL011);
			BTRON_CHECK("シリアル位置", md->uart_pa != 0);
			BTRON_CHECK("ドライバに反映", hal_console_pa() == md->uart_pa);
			/* 割込みコントローラ。中心核の前提。 */
			BTRON_CHECK("GIC", md->intc_type == MD_INTC_GICV2
			                       || md->intc_type == MD_INTC_GICV3);
			BTRON_CHECK("GICD", md->intc_dist_pa != 0);
			BTRON_CHECK("GICR/GICC", md->intc_cpu_pa != 0);
			/* タイマ。EL1 物理タイマは PPI なので INTID 16..31。 */
			BTRON_CHECK("タイマ INTID",
			            md->timer_intid[MD_TIMER_NONSECURE] >= 16
			                && md->timer_intid[MD_TIMER_NONSECURE] < 32);
			BTRON_CHECK("CPU", md->cpu_count >= 1);
			BTRON_CHECK("CPU 起動方法", md->boot_method != MD_BOOT_NONE);
		}
		/* 周波数はレジスタから読むので記述の有無に依らない */
		BTRON_CHECK("タイマ周波数", hal_timer_freq() > 0);
	});

	BTRON_TEST("MMIO 区画への写像 (§3.2-3 / R18 の機構)", {
		CONST MachDesc *md = machdesc();
		UD pa = (md->intc_dist_pa != 0) ? md->intc_dist_pa : hal_console_pa();
		UD va = hal_vm_map_device(pa, 0x10000);

		BTRON_CHECK("張れた", va != 0);
		BTRON_CHECK("別名は基底+物理", va == BTRON_MMIO_BASE + pa);
		BTRON_CHECK("MMIO 区画の中", va >= BTRON_MMIO_BASE
		                                 && va < BTRON_RESERVED_BASE);
		BTRON_CHECK("往復", hal_vm_translate(hal_vm_kernel_space(), va) == pa);
		/* 二度呼んでも同じ値。別名は物理アドレスで一意に決まる。 */
		BTRON_CHECK("冪等", hal_vm_map_device(pa, 0x10000) == va);
		BTRON_CHECK("0 は拒否", hal_vm_map_device(pa, 0) == 0);
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

	BTRON_TEST("物理ページアロケータ (A13 / A14 / §1.5 の土台)", {
		UD ps = (UD)hal_page_size();

		if (bootinfo_mem_count() == 0) {
			/* メモリマップが無い構成。アロケータは立たない。 */
			BTRON_CHECK("立っていない", pmem_total_pages() == 0);
			BTRON_CHECK("確保できない", pmem_alloc(1) == 0);
		} else {
			UD a;
			UD b;
			UD c;
			UD free0 = pmem_free_pages();

			BTRON_CHECK("管理下にある", pmem_total_pages() > 1000);
			BTRON_CHECK("空きがある", free0 > 1000);

			a = pmem_alloc(1);
			BTRON_CHECK("1 ページ確保", a != 0);
			BTRON_CHECK("ページ境界", (a & (ps - 1)) == 0);
			BTRON_CHECK("減った", pmem_free_pages() == free0 - 1);
			/* ゼロクリアされていること。前の中身が漏れると情報漏洩になる。 */
			BTRON_CHECK("ゼロクリア", *(volatile UD *)(VP)BTRON_PA_TO_VA(a) == 0);
			/* 直接マップ経由で書いて読めること */
			*(volatile UD *)(VP)BTRON_PA_TO_VA(a) = 0xB7120001ULL;
			BTRON_CHECK("書いて読める",
			            *(volatile UD *)(VP)BTRON_PA_TO_VA(a) == 0xB7120001ULL);

			b = pmem_alloc(1);
			BTRON_CHECK("2 つめ", b != 0 && b != a);

			/* **連続確保。** §1.5 の get_mbk が「nblk 個の連続した
			 * メモリブロック」を求めているので、ここが要件。 */
			c = pmem_alloc(4);
			BTRON_CHECK("4 ページ確保", c != 0);
			BTRON_CHECK("連続している",
			            *(volatile UD *)(VP)BTRON_PA_TO_VA(c + 3 * ps) == 0);
			BTRON_CHECK("減った", pmem_free_pages() == free0 - 6);

			pmem_free(c, 4);
			pmem_free(b, 1);
			pmem_free(a, 1);
			BTRON_CHECK("全部戻った", pmem_free_pages() == free0);
			/* 戻したページは再び配られる */
			{
				UD d = pmem_alloc(1);

				BTRON_CHECK("再確保", d != 0);
				pmem_free(d, 1);
			}

			/* カーネル本体と早期プールは絶対に配らない。
			 * 配ると自分のコードやページテーブルを上書きする。 */
			BTRON_CHECK("カーネルは管理外扱い",
			            pmem_owns(hal_kernel_pa_base()) == False
			                || pmem_free_pages() < pmem_total_pages() + 1);
			/* 変な引数で壊れない */
			BTRON_CHECK("0 ページ", pmem_alloc(0) == 0);
			pmem_free(0, 1);
			pmem_free(1, 1); /* ページ境界でない */
			BTRON_CHECK("壊れていない", pmem_free_pages() == free0);
		}
	});

	BTRON_TEST("カーネルヒープ", {
		VP a;
		VP b;
		VP c;

		if (bootinfo_mem_count() == 0) {
			BTRON_CHECK("ページが無いので取れない", kmalloc(16) == 0);
		} else {
			UD used0 = kheap_used();

			a = kmalloc(16);
			b = kmalloc(1000);
			BTRON_CHECK("確保", a != 0 && b != 0);
			BTRON_CHECK("重ならない", a != b);
			BTRON_CHECK("8 バイト整列", ((UD)a & 7U) == 0 && ((UD)b & 7U) == 0);
			BTRON_CHECK("使用量が増えた", kheap_used() > used0);
			*(volatile UD *)a = 0xAAAAAAAAULL;
			*(volatile UD *)b = 0xBBBBBBBBULL;
			BTRON_CHECK("独立している",
			            *(volatile UD *)a == 0xAAAAAAAAULL
			                && *(volatile UD *)b == 0xBBBBBBBBULL);

			kfree(a);
			kfree(b);
			BTRON_CHECK("戻った", kheap_used() == used0);
			kfree(0);     /* NULL は無視 */
			BTRON_CHECK("0 バイト", kmalloc(0) == 0);

			/* ページより大きい要求も通る */
			c = kmalloc(100000);
			BTRON_CHECK("大きい確保", c != 0);
			*(volatile UD *)c = 1;
			kfree(c);
			BTRON_CHECK("戻った", kheap_used() == used0);
			BTRON_CHECK("ヒープは成長した", kheap_total() > 0);
		}
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

	BTRON_TEST("画面 (E24 / 規律 R18 / F6)", {
		CONST Screen *s = screen();

		if (s == 0) {
			/* 画面が無い構成。異常ではない。 */
			BTRON_CHECK("記述も無い", bootinfo()->fb.base == 0
			                              || bootinfo()->fb.format == BFB_NONE);
		} else {
			UW red = screen_rgb(0xFF, 0, 0);
			UW blue = screen_rgb(0, 0, 0xFF);

			BTRON_CHECK("大きさ", s->width > 0 && s->height > 0);
			BTRON_CHECK("pitch", s->pitch >= s->width * 4);
			/* **写像は上位半分の MMIO 区画にある**（規律 R18）。
			 * ここが下位半分だと、プロセスへ貸し出した瞬間に
			 * 緊急画面が描けなくなる。 */
			BTRON_CHECK("上位半分", s->base >= BTRON_MMIO_BASE
			                            && s->base < BTRON_RESERVED_BASE);
			BTRON_CHECK("別名", s->base == BTRON_MMIO_BASE + s->pa);

			/* **書いた画素を読み返して確かめる。**
			 * 画面出力の自動テストはスクリーンショット比較になりがちで
			 * CI に乗らない。読み返せば普通のテストとして扱える。 */
			screen_pixel(1, 1, red);
			screen_pixel(2, 1, blue);
			BTRON_CHECK("書いて読める", screen_get(1, 1) == red);
			BTRON_CHECK("隣と混ざらない", screen_get(2, 1) == blue);
			BTRON_CHECK("赤と青は別の値", red != blue);
			/* 画面外は黙って無視し、読むと 0 */
			screen_pixel((W)s->width, 0, red);
			BTRON_CHECK("画面外は 0", screen_get((W)s->width, 0) == 0);

			screen_fill(4, 4, 8, 8, red);
			BTRON_CHECK("矩形の中", screen_get(5, 5) == red);
			BTRON_CHECK("矩形の外", screen_get(12, 5) != red);
			screen_fill(0, 0, 16, 16, screen_rgb(0x10, 0x14, 0x20));
		}
	});

	BTRON_TEST("グリフデータ (B8)", {
		CONST FontSet *f = font_default();
		UW width = 0;
		UW len = 0;

		BTRON_CHECK("ある", f != 0);
		BTRON_CHECK("16 ドット", f->height == 16);
		BTRON_CHECK("半角がある", f->narrow_count > 90);
		BTRON_CHECK("全角がある", f->wide_count > 10000);
		/* 半角は 8 ドット、全角は 16 ドット */
		BTRON_CHECK("'A' は半角", font_glyph(f, 'A', &width) != 0 && width == 8);
		BTRON_CHECK("'あ' は全角", font_glyph(f, 0x3042U, &width) != 0 && width == 16);
		BTRON_CHECK("'準' がある", font_glyph(f, 0x6E96U, &width) != 0 && width == 16);
		BTRON_CHECK("'—' がある", font_glyph(f, 0x2014U, &width) != 0);
		/* 範囲外は 0 を返す（落ちない） */
		BTRON_CHECK("無い字", font_glyph(f, 0xE000U, &width) == 0 && width == 0);
		BTRON_CHECK("面外", font_glyph(f, 0x10000U, &width) == 0);

		/* UTF-8 の切り出し。規律 R7 の境界。 */
		BTRON_CHECK("ASCII", font_utf8_next("A", &len) == 'A' && len == 1);
		BTRON_CHECK("3 バイト", font_utf8_next("あ", &len) == 0x3042U && len == 3);
		BTRON_CHECK("終端", font_utf8_next("", &len) == 0 && len == 0);
	});

	/* ハードウェア記述が無いと中心核は起動できない。
	 *
	 * 割込みコントローラとタイマの位置が分からないので、タイマ割込みも
	 * ディスパッチも仕掛けられない。規律 R19 により決め打ちはしない。
	 * QEMU の -kernel 直接起動がこの経路で、ここまでの検査だけで終わる。 */
	if (machdesc()->source == MD_SRC_NONE) {
		hal_console_puts("\n  ハードウェア記述が無いので中心核は起動しない\n");
		hal_console_puts("  （割込みコントローラとタイマの位置が不明。規律 R19）\n");
		hal_console_puts("  UEFI 経由なら中心核の検査まで走る\n");
		hal_exit(btron_test_report());
	}

	BTRON_TEST("中心核の初期化 (D10 / T1-01 / T1-02)", {
		BTRON_CHECK("knl_init", knl_init() == E_OK);
		BTRON_CHECK("GIC の版",
		            hal_intc_version() == 2 || hal_intc_version() == 3);
		BTRON_CHECK("まだ未起動", knl_running() == False);
		BTRON_CHECK("タイマ周波数", hal_timer_freq() > 0);
	});

	/* ここから先は中心核のタスクの中で検査する。
	 *
	 * タスクを待ち状態にするシステムコールは**タスク独立部から呼べない**
	 * （仕様は E_CTX とする）。boot_main はまだどのタスクでもないので、
	 * 検査タスクを作って起動し、そこで残りを走らせる。
	 * knl_selftest_start() は戻らず、最後に hal_exit() する。 */
	knl_selftest_start();
}

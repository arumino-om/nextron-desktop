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
			boot_put_hex(md->uart_pa);
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
	boot_put_hex((UD)(VP)__kernel_start);
	hal_console_puts("\n  カーネル PA  : ");
	boot_put_hex(hal_kva_to_pa((UD)(VP)__kernel_start));
	hal_console_puts("\n  ページテーブル: ");
	boot_put_dec(pmem_early_used() / 1024);
	hal_console_puts(" KB\n");

	/* 物理ページアロケータ。直接マップが張られた後でないと立てられない
	 * （確保したページをゼロクリアするのに直接マップ経由で触る）。
	 * メモリマップが無い直接起動では立たない。黙って先へ進む。 */
	if (pmem_init() == E_OK) {
		hal_console_puts("  物理メモリ  : ");
		boot_put_dec(pmem_total_pages() * hal_page_size() / 1024 / 1024);
		hal_console_puts("MB (");
		boot_put_dec(pmem_total_pages());
		hal_console_puts(" ページ) 管理下\n");
	}

	boot_show_bootinfo();
	boot_show_machdesc();

	/* 画面を立てる。これ以降 hal_console_puts() の出力が画面にも出る
	 * （E24: 画面はカーネルが保有する / 規律 R18）。
	 * 画面が無い構成では黙ってシリアルだけで進む。 */
	if (screen_init() == E_OK && screen_console_init() == E_OK) {
			boot_draw_splash();
	}

	boot_early_tests();

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

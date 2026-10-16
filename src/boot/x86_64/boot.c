/*
 * boot.c — x86_64 の C 側入口
 *
 * arm64 と同じ 2 段構え（src/boot/arm64/boot.c を参照）。表示と起動時の
 * 不変条件検査は**アーキ非依存の共有**を使う（`src/kernel/boot/bootlog.c` /
 * `earlytest.c`）ので、ここに残るのはアーキ固有の段取りだけである。
 *
 *   boot_early()  恒等写像の上（物理アドレス）で走る。コンソールと
 *                 ページテーブルを用意し、**CR3 を差し替えて**戻る
 *   boot_main()   start.S が上位半分へジャンプした後に走る
 *
 * arm64 との違いは 2 つだけ。
 *
 *   1. **ページングは最初から有効**（ロングモードの要求）。第 1 段の
 *      仕事は「MMU を有効にする」ではなく「CR3 を自前の表に差し替える」
 *   2. コンソールが**ポート I/O の 16550**（COM1）なので、上位半分へ
 *      移っても写像を張り替える必要がない。arm64 は MMIO なので
 *      hal_console_remap() が要る
 */

#include <btron/bootinfo.h>
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
 * 全数確保するだけで 1MB 強を使うので、余裕を見て 16MB 取る（arm64 と同じ）。 */
#define EARLY_POOL_SIZE (16UL * 1024 * 1024)

/* ------------------------------------------------------------------ */
/* 第 1 段: 恒等写像の上（物理アドレス）で走る                          */
/* ------------------------------------------------------------------ */

EXPORT void boot_early(UD arg)
{
	Bool via_uefi;
	UD ps;
	UD pool;
	UD found;

	hal_cpu_init();
	hal_console_init();
	hal_exception_init(); /* これより後のフォルトは無言のハングではなく報告になる */

	hal_console_puts("\n");
	hal_console_puts("BTRON3 準拠 OS — x86_64\n");

	/* 引き継ぎ情報を取り込む。ローダが置いた領域はこの先写像されないので、
	 * CR3 を差し替える前にカーネル内へ複製しておく。 */
	via_uefi = bootinfo_take(arg);
	if (!via_uefi) {
		/* **x86_64 には直接起動の経路が無い。** arm64 は QEMU の -kernel で
		 * ELF をそのまま置けるが、x86_64 で同じことをすると QEMU は
		 * Linux boot protocol を期待する。UEFI 経由だけを支える。 */
		hal_console_puts("  引き継ぎ情報がありません（UEFI 経由で起動すること）\n");
		hal_exit(1);
	}

	/* ハードウェア構成を読む（詳細 D8）。ACPI のテーブルはファームウェアの
	 * 領域にあり、この先写像されない。いまは恒等写像で全物理メモリが
	 * 見えているので、ここで読んで複製する。 */
	if (machdesc_probe()) {
		CONST MachDesc *md = machdesc();

		/* シリアルを記述どおりに差し替える。x86 では ACPI の SPCR に
		 * ポート番号が入る。読めなければ COM1 の既定値のまま（R19 の
		 * 保険）。 */
		if (hal_console_set(md->uart_type, md->uart_pa)) {
			hal_console_puts("  シリアル  : ");
			hal_console_puts(md_uart_name(md->uart_type));
			hal_console_puts(" @ ");
			boot_put_hex(md->uart_pa);
			hal_console_puts("\n");
		}
	}

	/* カーネルが実際に置かれた物理先頭を HAL に教える。ローダはリンク時の
	 * LMA が空いていなければ別の場所へ置く（規律 R17）。 */
	hal_kernel_set_pa_base(bootinfo()->kernel_pa);

	hal_console_puts("  起動経路  : UEFI\n");

	ps = (UD)hal_page_size();
	pool = (hal_kernel_pa_end() + ps - 1) & ~(ps - 1);
	found = bootinfo_find_free(EARLY_POOL_SIZE, pool);
	if (found == 0) {
		found = bootinfo_find_free(EARLY_POOL_SIZE, 0);
	}
	if (found == 0) {
		hal_console_puts("  空き物理メモリが見つからない\n");
		hal_exit(1);
	}
	pmem_early_init(found, EARLY_POOL_SIZE);

	if (hal_vm_init() != 0) {
		hal_console_puts("  hal_vm_init に失敗\n");
		hal_exit(1);
	}
	/* ここに到達した時点で CR3 は自前の表。まだ恒等写像の上で走っている。 */
}

/* ------------------------------------------------------------------ */
/* 第 2 段: 上位半分で走る                                             */
/* ------------------------------------------------------------------ */

EXPORT void boot_main(UD arg)
{
	(void)arg;

	/* **arm64 と違い、コンソールの張り替えは要らない。** COM1 はポート
	 * I/O なので写像に依存しない。呼んでも無害なので形を揃えておく。 */
	hal_console_remap();

	/* **GDT と IDT を張り直す。** 第 1 段で入れたものは物理アドレスを
	 * 指しており、恒等写像を捨てると届かなくなる。GDT が引けないと
	 * 例外の受付そのものが壊れてトリプルフォルトになる（cpu.c の
	 * 冒頭に経緯）。 */
	hal_cpu_init();
	hal_exception_init();

	hal_console_puts("  上位半分へ移行しました\n");
	hal_console_puts("  カーネル VMA : ");
	boot_put_hex((UD)(VP)__kernel_start);
	hal_console_puts("\n  カーネル PA  : ");
	boot_put_hex(hal_kva_to_pa((UD)(VP)__kernel_start));
	hal_console_puts("\n  ページテーブル: ");
	boot_put_dec(pmem_early_used() / 1024);
	hal_console_puts(" KB\n");

	if (pmem_init() == E_OK) {
		hal_console_puts("  物理メモリ  : ");
		boot_put_dec(pmem_total_pages() * hal_page_size() / 1024 / 1024);
		hal_console_puts("MB (");
		boot_put_dec(pmem_total_pages());
		hal_console_puts(" ページ) 管理下\n");
	}

	boot_show_bootinfo();
	boot_show_machdesc();

	if (screen_init() == E_OK && screen_console_init() == E_OK) {
		boot_draw_splash();
	}

	boot_early_tests();

	/* **ここで止まる。** 中心核を動かすには割込みコントローラ (APIC)・
	 * タイマ割込み・コンテキスト切替が要る。x86_64 側はまだ入っていない
	 * （T1-11 の残り。arm64 の GIC / CNTP / context.S に相当するもの）。
	 *
	 * 嘘をつかずに止めるのが要点である。スタブで「動いているように
	 * 見せる」と、後から入れるときに何が動いていて何が動いていないのか
	 * 分からなくなる。 */
	hal_console_puts("\n  x86_64 は起動とページングまで。中心核は未接続\n");
	hal_console_puts("  （APIC・タイマ割込み・コンテキスト切替が残り。T1-11）\n");
	hal_exit(btron_test_report());
}

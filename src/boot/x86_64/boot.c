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

/* シリアルを開く。**開ける確証が取れたときだけ**（規律 R31）。
 *
 * x86 はポート番号が ACPI の SPCR に入る。読めなければ COM1 の既定値を
 * 試すが、**スクラッチレジスタで本当に居ることを確かめてから開く** ——
 * ノート PC には 16550 が無く、居ない装置の THRE を待つと永久に回る
 * （詳細 F9 / 判明 (62)）。黙ったときの出力は画面が引き受ける。 */
LOCAL void open_console(void)
{
	CONST MachDesc *md = machdesc();

	if (hal_console_set(md->uart_type, md->uart_pa)) {
		hal_console_puts("  シリアル  : ");
		hal_console_puts(md_uart_name(md->uart_type));
		hal_console_puts(" @ ");
		boot_put_hex(md->uart_pa);
		hal_console_puts("\n");
		return;
	}
	/* 記述に無い。**確かめの取れる既定値なら開く**（q35 は SPCR を
	 * 出さないが COM1 は居る）。確かめが取れなければ黙る。
	 *
	 * NOUART の印が立っていたら試さない — 「シリアルが無い機械」の
	 * ふりをさせる逆テストなので、既定値で開いては意味が無い（R31）。 */
	if ((bootinfo()->flags & BI_F_NO_UART) == 0) {
		(void)hal_console_default();
	}
}

EXPORT void boot_early(UD arg)
{
	Bool via_uefi;
	UD ps;
	UD pool;
	UD found;

	hal_cpu_init();
	hal_exception_init(); /* これより後のフォルトは無言のハングではなく報告になる */

	/* **記述を読む前に喋らない**（規律 R31）。順番は
	 * 「引き継ぎ情報 → 記述 → シリアル」で、開くときには必ず裏付け
	 * （SPCR か スクラッチレジスタ）がある。
	 *
	 * 引き継ぎ情報と ACPI のテーブルはローダの領域にあり、この先
	 * 写像されない。いまは恒等写像で全物理メモリが見えているので、
	 * ここで読んでカーネル内へ複製する。 */
	via_uefi = bootinfo_take(arg);
	(void)machdesc_probe();
	open_console();

	hal_console_puts("\n");
	hal_console_puts("BTRON3 準拠 OS — x86_64\n");

	if (!via_uefi) {
		/* **x86_64 には直接起動の経路が無い。** arm64 は QEMU の -kernel で
		 * ELF をそのまま置けるが、x86_64 で同じことをすると QEMU は
		 * Linux boot protocol を期待する。UEFI 経由だけを支える。 */
		hal_console_puts("  引き継ぎ情報がありません（UEFI 経由で起動すること）\n");
		hal_exit(1);
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

	/* 中心核を起こす。**arm64 と同じ順序・同じ検査名で通す**のが T1-11 の
	 * 受入条件なので、ここも boot.c (arm64) と同じ形にしてある。 */
	BTRON_TEST("中心核の初期化 (D10 / T1-01 / T1-02)", {
		BTRON_CHECK("knl_init", knl_init() == E_OK);
		/* APIC の版レジスタ。xAPIC は 0x10 以上を返す
		 * （arm64 側はここで GIC の版 2 / 3 を見ている）。 */
		BTRON_CHECK("APIC の版", hal_intc_version() >= 0x10U);
		BTRON_CHECK("まだ未起動", knl_running() == False);
		BTRON_CHECK("タイマ周波数", hal_timer_freq() > 0);
	});

	/* 中心核のシステムコールはタスクの中でしか呼べない（仕様は E_CTX と
	 * する）。boot_main はまだどのタスクでもないので、検査タスクを作って
	 * 起動し、そこで残りを走らせる。
	 * knl_selftest_start() は戻らず、最後に hal_exit() する。 */
	knl_selftest_start();
}

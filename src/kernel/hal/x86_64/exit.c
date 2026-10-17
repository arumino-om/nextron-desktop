/*
 * exit.c — 実行の終了 (x86_64)
 *
 * 試す順番は 3 段（詳細 F9「報告と終了」/ F7）。arm64 側と同じ形。
 *
 *   1. QEMU の isa-debug-exit   終了コード付きで終わらせる
 *   2. ACPI の PM1 制御レジスタ  S5（ソフト電源断）
 *   3. 停止ループ
 *
 * **終了コードを持ち帰れるのは 1 だけ**なので先に試す。arm64 の
 * セミホスティングと違い、割り当てられていない ISA ポートへの書き込みは
 * 実機でも無視されるだけなので、試す代償が無い（arm64 側は命令自体が
 * 例外になるため、例外ハンドラから 2 段目へ回している）。
 *
 * 【状態】ブート経路が無いため未検証。2 段目の SLP_TYP だけは、合成した
 * AML を `acpi_s5_parse()` に食わせる形で検査している。
 */

#include <btron/hal.h>
#include <btron/machdesc.h>
#include <btron/screen.h>
#include <btron/vmem.h>

#define DEBUG_EXIT_PORT 0xF4U

/* PM1 制御レジスタのビット配置 (ACPI 4.8.3.2.1)。
 * SLP_TYP は bit[12:10]、SLP_EN は bit13。 */
#define PM1_SLP_TYP_SHIFT 10
#define PM1_SLP_EN 0x2000U

LOCAL void poweroff_acpi(void)
{
	CONST MachDesc *md = machdesc();

	/* **SLP_TYP が分からなければ書いてはならない。** 値は DSDT の
	 * `\_S5` にしか無く機種ごとに違う。当てずっぽうで書くと、S5 ではなく
	 * S1〜S4（サスペンド）に入って**復帰できないまま止まる**。 */
	if (md->pm1_kind == MD_PM1_NONE || !md->s5_known) {
		return;
	}
	{
		UH a = (UH)((md->slp_typ_a << PM1_SLP_TYP_SHIFT) | PM1_SLP_EN);
		UH b = (UH)((md->slp_typ_b << PM1_SLP_TYP_SHIFT) | PM1_SLP_EN);

		if (md->pm1_kind == MD_PM1_PIO) {
			if (md->pm1a_cnt != 0) {
				__asm__ volatile("outw %0, %1"
				                 :
				                 : "a"(a), "Nd"((UH)md->pm1a_cnt));
			}
			if (md->pm1b_cnt != 0) {
				__asm__ volatile("outw %0, %1"
				                 :
				                 : "a"(b), "Nd"((UH)md->pm1b_cnt));
			}
		} else {
			/* MMIO 側。直接マップ経由で触る。 */
			if (md->pm1a_cnt != 0) {
				*(volatile UH *)(VP)BTRON_PA_TO_VA(md->pm1a_cnt) = a;
			}
			if (md->pm1b_cnt != 0) {
				*(volatile UH *)(VP)BTRON_PA_TO_VA(md->pm1b_cnt) = b;
			}
		}
	}
}

EXPORT void hal_exit(W code)
{
	/* 終了コードは (値 << 1) | 1 になるので、tools/run_qemu.py 側で
	 * 0 と 1 を正常として扱っている。 */
	__asm__ volatile("outl %0, %1" : : "a"((UW)code), "Nd"((UH)DEBUG_EXIT_PORT));

	/* 戻ってきた = QEMU の isa-debug-exit が無い（実機）。 */

	/* 結果を読む手段が画面しか無ければ、残して止まる。
	 * 電源を落とすと消えるので、それでは実機で何も分からない。 */
	screen_hold_result();

	hal_console_puts("\n  電源を落とします (ACPI S5)\n");
	poweroff_acpi();

	for (;;) {
		__asm__ volatile("cli; hlt");
	}
}

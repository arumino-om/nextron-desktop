/*
 * exception.c — 例外の報告 (arm64)
 *
 * いまは全例外を捕まえて情報を出し、終了する。
 * 中心核の割込み管理ができたら IRQ を分離する。
 */

#include <btron/hal.h>
#include <btron/screen.h>

IMPORT void btron_vectors(void);
EXPORT void hal_exception_report(UD esr, UD far, UD elr) __attribute__((noreturn));

LOCAL void put_hex(UD v)
{
	CONST B *digits = "0123456789abcdef";
	W i;

	hal_console_puts("0x");
	for (i = 60; i >= 0; i -= 4) {
		hal_console_putc(digits[(v >> (UW)i) & 0xF]);
	}
}

/* アボートの原因 (ESR_EL1.ISS の DFSC/IFSC = bits[5:0]) を出す。
 *
 * 「ページが無い」のか「物理メモリが無い」のか「権限が無い」のかで
 * 原因が全く違う。無言のハングに戻らないよう、ここを細かく出す。 */
LOCAL void put_fault(UD esr)
{
	UD fsc = esr & 0x3FULL;

	if ((fsc & 0x3CULL) == 0x04ULL) {
		hal_console_puts("（変換フォルト = 写像が無い, level ");
	} else if ((fsc & 0x3CULL) == 0x08ULL) {
		hal_console_puts("（アクセスフラグ = AF 未設定, level ");
	} else if ((fsc & 0x3CULL) == 0x0CULL) {
		hal_console_puts("（権限フォルト, level ");
	} else if (fsc == 0x10ULL) {
		hal_console_puts("（外部アボート = その物理アドレスに何も無い");
	} else if ((fsc & 0x3CULL) == 0x00ULL) {
		hal_console_puts("（アドレスサイズフォルト, level ");
	} else {
		hal_console_puts("（FSC = ");
		put_hex(fsc);
	}
	if ((fsc & 0x30ULL) == 0x00ULL && fsc != 0x10ULL) {
		hal_console_putc((B)('0' + (W)(fsc & 3ULL)));
	}
	hal_console_puts("）\n");
}

EXPORT void hal_exception_init(void)
{
	UD vbar = (UD)(VP)btron_vectors;
	__asm__ volatile(
		"msr vbar_el1, %0\n"
		"isb\n"
		:
		: "r"(vbar)
		: "memory");
}

/* 例外報告の入れ子の深さ。
 *
 * **報告の途中でもう 1 度例外が起きると無限に回る。** 実際に起きた:
 * `hal_exit()` は ARM セミホスティングの `hlt #0xF000` で QEMU を
 * 終わらせるが、`-semihosting` が無い環境（実機や、付け忘れた QEMU）では
 * この命令自体が「分類なし」の例外になる。すると
 *
 *   例外 → 報告 → hal_exit → hlt が例外 → 報告 → hal_exit → …
 *
 * と回り続け、**最初の本物の例外の情報が画面から流れて消える**。
 * 緊急画面 (E24) は「落ちた理由を人に見せる」ためにあるので、
 * これでは目的を果たせない。
 *
 * 2 度目からは何も出さずに止める。最初の報告が画面に残る。 */
LOCAL W report_depth;

EXPORT void hal_exception_report(UD esr, UD far, UD elr)
{
	UD ec = (esr >> 26) & 0x3F;

	report_depth++;
	if (report_depth > 1) {
		/* 報告の途中で落ちた。**画面はそのままにして止まる。**
		 * ここで hal_exit() を呼んではならない — それがこの状況を
		 * 作った張本人かもしれない。 */
		for (;;) {
			__asm__ volatile("wfe");
		}
	}

	/* 画面があれば占有して緊急画面に切り替える（E24 / T4-02）。
	 * 以後の hal_console_puts() がそのままそこへ流れるので、
	 * 報告の本体は 1 行も変えなくてよい。画面が無ければ何も起きない。 */
	(void)screen_panic_begin("*** 例外 ***");

	hal_console_puts("\n*** 例外 ***\n");
	hal_console_puts("  ESR_EL1 = ");
	put_hex(esr);
	hal_console_puts("  (EC = ");
	put_hex(ec);
	hal_console_puts(")\n");
	hal_console_puts("  FAR_EL1 = ");
	put_hex(far);
	hal_console_puts("\n  ELR_EL1 = ");
	put_hex(elr);
	hal_console_puts("\n");

	switch (ec) {
	case 0x20:
	case 0x21:
		hal_console_puts("  命令アボート");
		put_fault(esr);
		break;
	case 0x24:
	case 0x25:
		hal_console_puts("  データアボート");
		put_fault(esr);
		break;
	case 0x07:
		/* これで CPACR_EL1.FPEN の設定漏れを見つけた。
		 * カーネルが明示的に FP を使わなくても、コンテキスト切替が
		 * d8-d15 を退避するので許可が必要になる (hal_cpu_init)。 */
		hal_console_puts("  SIMD / 浮動小数点のトラップ（CPACR_EL1.FPEN 未設定）\n");
		break;
	case 0x15:
		hal_console_puts("  SVC（システムコール）\n");
		break;
	case 0x16:
		hal_console_puts("  HVC / SMC\n");
		break;
	default:
		hal_console_puts("  （分類なし）\n");
		break;
	}

	hal_exit(2);
}

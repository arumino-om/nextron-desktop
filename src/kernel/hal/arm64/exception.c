/*
 * exception.c — 例外の報告 (arm64)
 *
 * いまは全例外を捕まえて情報を出し、終了する。
 * 中心核の割込み管理ができたら IRQ を分離する。
 */

#include <btron/hal.h>

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

EXPORT void hal_exception_report(UD esr, UD far, UD elr)
{
	UD ec = (esr >> 26) & 0x3F;

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
		hal_console_puts("  命令アボート（実行しようとしたページが無い）\n");
		break;
	case 0x24:
	case 0x25:
		hal_console_puts("  データアボート（アクセスしたページが無い）\n");
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

/*
 * test.c — 極小のテストハーネスの実装
 *
 * 標準ライブラリに依存しない。出力は HAL のコンソールだけを使う。
 */

#include <btron/test.h>

EXPORT W btron_test_total = 0;
EXPORT W btron_test_failed = 0;

LOCAL W current_failed = 0;

LOCAL void put_dec(W v)
{
	B buf[12];
	W i = 0;

	if (v < 0) {
		hal_console_putc('-');
		v = -v;
	}
	do {
		buf[i++] = (B)('0' + (v % 10));
		v /= 10;
	} while (v > 0 && i < (W)sizeof(buf));

	while (i > 0) {
		hal_console_putc(buf[--i]);
	}
}

EXPORT void btron_test_begin(CONST B *name)
{
	btron_test_total++;
	current_failed = 0;
	hal_console_puts("[ RUN  ] ");
	hal_console_puts(name);
	hal_console_puts("\n");
}

EXPORT void btron_test_fail(CONST B *name, CONST B *expr, CONST B *file, W line)
{
	if (current_failed == 0) {
		btron_test_failed++;
	}
	current_failed++;
	hal_console_puts("[ FAIL ] ");
	hal_console_puts(name);
	hal_console_puts(": ");
	hal_console_puts(expr);
	hal_console_puts("  (");
	hal_console_puts(file);
	hal_console_puts(":");
	put_dec(line);
	hal_console_puts(")\n");
}

EXPORT void btron_test_end(CONST B *name)
{
	if (current_failed == 0) {
		hal_console_puts("[ PASS ] ");
		hal_console_puts(name);
		hal_console_puts("\n");
	}
}

EXPORT W btron_test_report(void)
{
	hal_console_puts("[ DONE ] ");
	put_dec(btron_test_total - btron_test_failed);
	hal_console_puts("/");
	put_dec(btron_test_total);
	hal_console_puts("\n");
	return btron_test_failed == 0 ? 0 : 1;
}

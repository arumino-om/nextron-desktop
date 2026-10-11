/*
 * uart.c — 16550 UART (x86_64)
 *
 * COM1 (I/O 0x3F8)。B-right/V のデバッグコンソールも COM1 だった（詳細 D8）。
 *
 * 【状態】ブート経路が無いため未検証。
 */

#include <btron/hal.h>
#include <btron/machdesc.h>

/* 起動初期の既定値。本来は ACPI の SPCR から取る (hal_console_set)。 */
#define COM1 0x3F8U

#define UART_THR 0U /* 送信保持レジスタ */
#define UART_IER 1U
#define UART_FCR 2U
#define UART_LCR 3U
#define UART_LSR 5U

#define LSR_THRE 0x20U /* 送信保持レジスタが空 */

LOCAL UH port_base = COM1;

LOCAL inline void outb(UH port, UB v)
{
	__asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

LOCAL inline UB inb(UH port)
{
	UB v;
	__asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
	return v;
}

EXPORT void hal_console_init(void)
{
	outb((UH)(port_base + UART_IER), 0x00); /* 割り込み禁止 */
	outb((UH)(port_base + UART_LCR), 0x80); /* DLAB on */
	outb((UH)(port_base + 0), 0x01);        /* 分周比 1 = 115200 bps */
	outb((UH)(port_base + 1), 0x00);
	outb((UH)(port_base + UART_LCR), 0x03); /* 8N1、DLAB off */
	outb((UH)(port_base + UART_FCR), 0xC7); /* FIFO 有効・クリア */
}

EXPORT Bool hal_console_set(UW type, UD base)
{
	/* ポート I/O の 16550 だけを受け付ける。MMIO の 16550 と PL011 は
	 * このドライバでは扱わない。 */
	if (type != MD_UART_8250_PIO || base == 0 || base > 0xFFFFULL) {
		return False;
	}
	port_base = (UH)base;
	hal_console_init();
	return True;
}

EXPORT UD hal_console_pa(void)
{
	/* ポート I/O はアドレス空間に現れない。MMIO 区画に張るものが無い。 */
	return 0;
}

EXPORT void hal_console_remap(void)
{
	/* ポート I/O なのでアドレス空間に現れない。移す先が無い。
	 * arm64 の PL011 (MMIO) との差を吸収するために空実装を置く。 */
}

EXPORT void hal_console_putc(B c)
{
	while (!(inb((UH)(port_base + UART_LSR)) & LSR_THRE)) {
		/* 送信可能になるまで待つ */
	}
	outb((UH)(port_base + UART_THR), (UB)c);
}

LOCAL void (*tee)(CONST B *s);

EXPORT void hal_console_tee(void (*fn)(CONST B *s))
{
	tee = fn;
}

EXPORT void hal_console_puts(CONST B *s)
{
	CONST B *p = s;

	for (; *p != '\0'; p++) {
		if (*p == '\n') {
			hal_console_putc('\r');
		}
		hal_console_putc(*p);
	}
	if (tee != 0) {
		void (*fn)(CONST B *) = tee;

		tee = 0;
		fn(s);
		tee = fn;
	}
}

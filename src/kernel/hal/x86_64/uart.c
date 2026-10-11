/*
 * uart.c — 16550 UART (x86_64)
 *
 * COM1 (I/O 0x3F8)。B-right/V のデバッグコンソールも COM1 だった（詳細 D8）。
 *
 * 【状態】ブート経路が無いため未検証。
 */

#include <btron/hal.h>

#define COM1 0x3F8U

#define UART_THR 0U /* 送信保持レジスタ */
#define UART_IER 1U
#define UART_FCR 2U
#define UART_LCR 3U
#define UART_LSR 5U

#define LSR_THRE 0x20U /* 送信保持レジスタが空 */

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
	outb((UH)(COM1 + UART_IER), 0x00); /* 割り込み禁止 */
	outb((UH)(COM1 + UART_LCR), 0x80); /* DLAB on */
	outb((UH)(COM1 + 0), 0x01);        /* 分周比 1 = 115200 bps */
	outb((UH)(COM1 + 1), 0x00);
	outb((UH)(COM1 + UART_LCR), 0x03); /* 8N1、DLAB off */
	outb((UH)(COM1 + UART_FCR), 0xC7); /* FIFO 有効・クリア */
}

EXPORT void hal_console_putc(B c)
{
	while (!(inb((UH)(COM1 + UART_LSR)) & LSR_THRE)) {
		/* 送信可能になるまで待つ */
	}
	outb((UH)(COM1 + UART_THR), (UB)c);
}

EXPORT void hal_console_puts(CONST B *s)
{
	for (; *s != '\0'; s++) {
		if (*s == '\n') {
			hal_console_putc('\r');
		}
		hal_console_putc(*s);
	}
}

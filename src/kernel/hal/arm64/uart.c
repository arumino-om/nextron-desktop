/*
 * uart.c — PL011 UART (arm64)
 *
 * QEMU の virt マシンは PL011 を 0x0900_0000 に置く。
 * 起動初期のコンソール出力に使う (D8: デバッグコンソールは Phase 1 から)。
 *
 * ここはカーネルドライバである。F1 の原則はユーザランドドライバだが、
 * 詳細 F1 の例外基準「ブート時、ユーザランドが立ち上がる前に必要」に該当する。
 */

#include <btron/hal.h>

/* QEMU virt の PL011 UART0 */
#define PL011_BASE 0x09000000UL

#define PL011_DR 0x000   /* Data Register */
#define PL011_FR 0x018   /* Flag Register */

#define FR_TXFF (1U << 5) /* 送信 FIFO が満杯 */

LOCAL inline UW mmio_read(UW off)
{
	return *(volatile UW *)(PL011_BASE + off);
}

LOCAL inline void mmio_write(UW off, UW v)
{
	*(volatile UW *)(PL011_BASE + off) = v;
}

EXPORT void hal_console_init(void)
{
	/* QEMU のファームウェアが初期化済みなので何もしない。
	 * 実機では分周比とライン制御の設定が要る。 */
}

EXPORT void hal_console_putc(B c)
{
	while (mmio_read(PL011_FR) & FR_TXFF) {
		/* 送信 FIFO が空くまで待つ */
	}
	mmio_write(PL011_DR, (UW)(UB)c);
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

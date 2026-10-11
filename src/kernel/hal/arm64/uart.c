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
#include <btron/machdesc.h>
#include <btron/vmem.h>

/* 起動初期の既定値。QEMU virt の PL011 UART0 の物理アドレス。
 *
 * **これは「ファームウェアの記述を読む前に何か出すため」の保険**であって、
 * 本来のアドレスは DT / ACPI から取る (hal_console_set)。記述が読めない
 * 構成（QEMU の -kernel 直接起動）ではこの値で動き続ける。 */
#define PL011_PA_DEFAULT 0x09000000UL

LOCAL UD pl011_pa = PL011_PA_DEFAULT;   /* 物理アドレス */
LOCAL UD pl011_base = PL011_PA_DEFAULT; /* いま触っているアドレス */

#define PL011_DR 0x000   /* Data Register */
#define PL011_FR 0x018   /* Flag Register */

#define FR_TXFF (1U << 5) /* 送信 FIFO が満杯 */

LOCAL inline UW mmio_read(UW off)
{
	return *(volatile UW *)(VP)(pl011_base + off);
}

LOCAL inline void mmio_write(UW off, UW v)
{
	*(volatile UW *)(VP)(pl011_base + off) = v;
}

EXPORT Bool hal_console_set(UW type, UD base)
{
	/* このドライバは PL011 だけ。16550 が来たら受け付けない。
	 * （x86_64 側のドライバが 16550 を見る） */
	if (type != MD_UART_PL011 || base == 0) {
		return False;
	}
	pl011_pa = base;
	pl011_base = base; /* まだ MMU 無効。物理アドレスで触る */
	return True;
}

EXPORT UD hal_console_pa(void)
{
	return pl011_pa;
}

EXPORT void hal_console_remap(void)
{
	pl011_base = BTRON_MMIO_BASE + pl011_pa;
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

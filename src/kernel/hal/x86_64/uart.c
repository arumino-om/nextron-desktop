/*
 * uart.c — 16550 UART (x86_64)
 *
 * COM1 (I/O 0x3F8)。B-right/V のデバッグコンソールも COM1 だった（詳細 D8）。
 *
 * ------------------------------------------------------------------------
 * 【規律 R31】確かめていない番地へは 1 バイトも書かない
 * ------------------------------------------------------------------------
 *
 * arm64 側 (src/kernel/hal/arm64/uart.c) と同じ形にしてある。**ドライバは
 * 黙った状態で始まり**、記述が指した番地か、確かめの取れた既定値だけを開く。
 *
 * ポート I/O なので書き込みが外部アボートになることは無いが、**代わりに
 * もっと静かな形で死ぬ** — 居ない 16550 の LSR は 0xFF か 0x00 を返し、
 * `LSR_THRE` が立たないまま `hal_console_putc()` が永久に回る。
 * 実機のノート PC には COM ポートが無いことが普通なので（詳細 F9）、
 * これは起きる。確かめにはスクラッチレジスタを使う。
 *
 * 【状態】ブート経路が無いため未検証。
 */

#include <btron/hal.h>
#include <btron/machdesc.h>

/* 組み込みの既定値。記述が無い経路のためだけにある（本来は ACPI の SPCR）。 */
#define COM1 0x3F8U

#define UART_THR 0U /* 送信保持レジスタ */
#define UART_IER 1U
#define UART_FCR 2U
#define UART_LCR 3U
#define UART_LSR 5U
#define UART_SCR 7U /* スクラッチレジスタ。16550 の存在確認に使う */

#define LSR_THRE 0x20U /* 送信保持レジスタが空 */

/* 0 = まだどこも開いていない（黙っている） */
LOCAL UH port_base;

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

/* 分周比とライン制御を設定する。開ける番地が決まってから呼ぶ。 */
LOCAL void program_16550(UH base)
{
	outb((UH)(base + UART_IER), 0x00); /* 割り込み禁止 */
	outb((UH)(base + UART_LCR), 0x80); /* DLAB on */
	outb((UH)(base + 0), 0x01);        /* 分周比 1 = 115200 bps */
	outb((UH)(base + 1), 0x00);
	outb((UH)(base + UART_LCR), 0x03); /* 8N1、DLAB off */
	outb((UH)(base + UART_FCR), 0xC7); /* FIFO 有効・クリア */
}

EXPORT Bool hal_console_set(UW type, UD base)
{
	/* ポート I/O の 16550 だけを受け付ける。MMIO の 16550 と PL011 は
	 * このドライバでは扱わない。
	 * type が MD_UART_NONE のときもここで False になる — それが
	 * 「記述に UART が無い機械では黙る」の入口である（R31）。 */
	if (type != MD_UART_8250_PIO || base == 0 || base > 0xFFFFULL) {
		return False;
	}
	port_base = (UH)base;
	program_16550(port_base);
	return True;
}

EXPORT Bool hal_console_default(void)
{
	CONST UH base = (UH)COM1;

	/* **読んで確かめる**（R31）。スクラッチレジスタに書いて読み返す。
	 * 16550 が居なければ 0xFF か 0x00 が返り、一致しない。
	 * ポート I/O なので、居ない番地を突いても止まらない。 */
	outb((UH)(base + UART_SCR), 0x5A);
	if (inb((UH)(base + UART_SCR)) != 0x5A) {
		return False;
	}
	outb((UH)(base + UART_SCR), 0xA5);
	if (inb((UH)(base + UART_SCR)) != 0xA5) {
		return False;
	}
	port_base = base;
	program_16550(port_base);
	return True;
}

EXPORT UD hal_console_pa(void)
{
	/* **ポート I/O はアドレス空間に現れない。** MMIO 区画に張るものが
	 * 無いという意味で 0 を返す（出力できないという意味ではない）。
	 * 開いているかどうかは `hal_console_opened()` の方で答える。 */
	return 0;
}

EXPORT void hal_console_remap(void)
{
	/* ポート I/O なのでアドレス空間に現れない。移す先が無い。
	 * arm64 の PL011 (MMIO) との差を吸収するために空実装を置く。 */
}

/* 出力の複製先。画面コンソールが登録する。 */
LOCAL void (*tee)(CONST B *s);

/* hal_console_puts() の中にいる間は putc 側で複製しない。
 * 二重に出るのを防ぐためと、**UTF-8 の途中で切らない**ため。
 * 画面コンソールは NUL 終端の文字列から UTF-8 を切り出すので、
 * 1 バイトずつ渡すとマルチバイト文字が壊れる。 */
LOCAL Bool in_puts = False;

LOCAL void tee_char(B c);

EXPORT void hal_console_putc(B c)
{
	/* **黙っているときは複製先だけに出す。** 居ない 16550 を待つと
	 * THRE が永久に立たず、ここで止まる（R31）。 */
	if (port_base != 0) {
		while (!(inb((UH)(port_base + UART_LSR)) & LSR_THRE)) {
			/* 送信可能になるまで待つ */
		}
		outb((UH)(port_base + UART_THR), (UB)c);
	}
	tee_char(c);
}

EXPORT void hal_console_tee(void (*fn)(CONST B *s))
{
	tee = fn;
}

/* putc 単体で出された文字も複製する。
 *
 * **これが無いと数値が画面に出ない。** 例外の報告やテストの出力は
 * 「ラベルを puts、数字を putc」という形で書かれていて、putc の分だけ
 * 落ちる。実際に緊急画面が `ESR_EL1 = 0x` で止まって気づいた。 */
LOCAL void tee_char(B c)
{
	B buf[2];

	if (tee == 0 || in_puts) {
		return;
	}
	buf[0] = c;
	buf[1] = '\0';
	{
		void (*fn)(CONST B *) = tee;

		tee = 0;
		fn(buf);
		tee = fn;
	}
}

EXPORT void hal_console_puts(CONST B *s)
{
	CONST B *p = s;

	in_puts = True;
	for (; *p != '\0'; p++) {
		if (*p == '\n') {
			hal_console_putc('\r');
		}
		hal_console_putc(*p);
	}
	in_puts = False;
	if (tee != 0) {
		void (*fn)(CONST B *) = tee;

		tee = 0;
		fn(s);
		tee = fn;
	}
}

EXPORT Bool hal_console_opened(void)
{
	return (port_base != 0) ? True : False;
}

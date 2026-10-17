/*
 * uart.c — PL011 UART (arm64)
 *
 * 起動初期のコンソール出力に使う (D8: デバッグコンソールは Phase 1 から)。
 *
 * ここはカーネルドライバである。F1 の原則はユーザランドドライバだが、
 * 詳細 F1 の例外基準「ブート時、ユーザランドが立ち上がる前に必要」に該当する。
 *
 * ------------------------------------------------------------------------
 * 【規律 R31】確かめていない番地へは 1 バイトも書かない
 * ------------------------------------------------------------------------
 *
 * **ドライバは黙った状態で始まる。** 以前はここに QEMU virt の PL011 の
 * 番地 (0x0900_0000) を既定値として持ち、記述が読めるまでそこへ書いていた。
 * QEMU では当たりだが、**実機ではその番地に何も無い** — 書けば外部
 * アボートになり、しかも症状は無言の停止として出る（詳細 F9）。
 *
 * 開ける道は 2 つだけにした。
 *
 *   hal_console_set()      ファームウェアの記述が指した番地（確かめた値）
 *   hal_console_default()  組み込みの既定値。**ID レジスタを読んで
 *                          PL011 が本当に居ることを確かめてから**開く
 *
 * どちらも通らなければ黙ったままで、出力は画面が引き受ける
 * （`hal_console_tee()` と緊急画面。詳細 F9「報告と終了」）。
 */

#include <btron/hal.h>
#include <btron/machdesc.h>
#include <btron/vmem.h>

/* 組み込みの既定値。QEMU virt の PL011 UART0 の物理アドレス。
 *
 * **記述が 1 つも無い経路（QEMU の -kernel 直接起動）のためだけにある。**
 * 実機は必ず UEFI 経由で来て DT か ACPI を置くので、この経路に実機は来ない。
 * それでも使う前に ID レジスタで確かめる（R31）。 */
#define PL011_PA_DEFAULT 0x09000000UL

/* 0 = まだどこも開いていない（黙っている） */
LOCAL UD pl011_pa;   /* 物理アドレス */
LOCAL UD pl011_base; /* いま触っているアドレス */

#define PL011_DR 0x000   /* Data Register */
#define PL011_FR 0x018   /* Flag Register */

/* PrimeCell 識別レジスタ。PL011 であることの確認に使う。
 * ARM の PrimeCell 共通の署名 0xB105F00D が 4 レジスタに 1 バイトずつ入る。 */
#define PL011_PCELLID0 0xFF0
#define PL011_PCELL_SIG 0xB105F00DUL

#define FR_TXFF (1U << 5) /* 送信 FIFO が満杯 */

LOCAL inline UW mmio_read_at(UD base, UW off)
{
	return *(volatile UW *)(VP)(base + off);
}

LOCAL inline UW mmio_read(UW off)
{
	return mmio_read_at(pl011_base, off);
}

LOCAL inline void mmio_write(UW off, UW v)
{
	*(volatile UW *)(VP)(pl011_base + off) = v;
}

EXPORT Bool hal_console_set(UW type, UD base)
{
	/* このドライバは PL011 だけ。16550 が来たら受け付けない。
	 * （x86_64 側のドライバが 16550 を見る）
	 * type が MD_UART_NONE のときもここで False になる — それが
	 * 「記述に UART が無い機械では黙る」の入口である（R31）。 */
	if (type != MD_UART_PL011 || base == 0) {
		return False;
	}
	pl011_pa = base;
	pl011_base = base; /* まだ MMU 無効。物理アドレスで触る */
	return True;
}

EXPORT Bool hal_console_default(void)
{
	UD base = PL011_PA_DEFAULT;
	UD sig = 0;
	UW i;

	/* **読んで確かめる**（R31）。PrimeCell の署名が出なければ、
	 * そこに PL011 は無いので開かない。
	 *
	 * 読み出しもアボートしうるが、この関数を呼ぶのは記述が 1 つも
	 * 無い経路に限る（= QEMU の -kernel 直接起動。実機は来ない）。 */
	for (i = 0; i < 4; i++) {
		sig |= (UD)(mmio_read_at(base, (UW)(PL011_PCELLID0 + i * 4)) & 0xFFU)
		       << (i * 8);
	}
	if (sig != PL011_PCELL_SIG) {
		return False;
	}
	pl011_pa = base;
	pl011_base = base;
	return True;
}

EXPORT UD hal_console_pa(void)
{
	return pl011_pa;
}

EXPORT Bool hal_console_opened(void)
{
	/* MMIO なので「写像すべき番地がある」= 「開いている」。
	 * x86 のポート I/O では一致しないので、口を分けてある。 */
	return (pl011_pa != 0) ? True : False;
}

EXPORT void hal_console_remap(void)
{
	if (pl011_pa == 0) {
		return; /* 黙っている。移す先も無い */
	}
	pl011_base = BTRON_MMIO_BASE + pl011_pa;
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
	/* **黙っているときは複製先だけに出す。** ここで既定値へ書くと
	 * 実機で外部アボートになる（R31）。画面が立っていれば読める。 */
	if (pl011_base != 0) {
		while (mmio_read(PL011_FR) & FR_TXFF) {
			/* 送信 FIFO が空くまで待つ */
		}
		mmio_write(PL011_DR, (UW)(UB)c);
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
		/* 複製先の中で hal_console_puts() を呼ばれると無限に潜る。
		 * 画面コンソールは呼ばないが、念のため呼び出し中は外す。 */
		void (*fn)(CONST B *) = tee;

		tee = 0;
		fn(s);
		tee = fn;
	}
}

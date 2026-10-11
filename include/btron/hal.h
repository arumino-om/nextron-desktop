/*
 * hal.h — HAL (ハードウェア抽象層) の最小インタフェース
 *
 * アーキ依存の処理はここを通す。docs/design-issues.md の規律:
 *   R2  ページサイズを定数でハードコードせず、HAL が公開する値を参照する
 *   R3  アドレス空間の生成・切替は 2 関数に閉じる
 */

#ifndef __BTRON_HAL_H__
#define __BTRON_HAL_H__

#include <btron/typedef.h>

/* --- コンソール（起動初期のシリアル出力） ------------------------- */

IMPORT void hal_console_init(void);
IMPORT void hal_console_putc(B c);
IMPORT void hal_console_puts(CONST B *s);

/* 起動直後は物理アドレス（恒等写像）で UART を触る。カーネルが上位半分へ
 * 移った後、MMIO 区画の別名 (BTRON_MMIO_BASE + 物理) に切り替える。
 * これ以降は恒等写像を捨てられる。宛先はドライバ自身が知っている。 */
IMPORT void hal_console_remap(void);

/* ファームウェアの記述（DT / ACPI）から読んだシリアルをドライバに教える。
 *
 * type は machdesc.h の MD_UART_*。ドライバが扱える種類でなければ False を
 * 返し、既定値のまま動き続ける。起動第 1 段で、MMU を有効にする前に呼ぶ。
 *
 * これにより規律 R2 と同じ形（アドレスを直書きせず読み取った値を使う）が
 * シリアルについても成立する。既定値は「記述が読めないときの保険」。 */
IMPORT Bool hal_console_set(UW type, UD base);

/* ドライバが使っている物理アドレス。hal_vm_init() が MMIO 区画に
 * 張るべきページを知るために使う。 */
IMPORT UD hal_console_pa(void);

/* --- ページサイズ (R2) --------------------------------------------
 * A13 で 4KB と決めたが、定数として配らず HAL 経由で参照する。
 * 将来 16KB granule へ移行する余地を残すため。            */

IMPORT UW hal_page_size(void);
IMPORT UW hal_page_shift(void);

/* --- 実行の終了 ----------------------------------------------------
 * テスト実行時に QEMU を終了させるために使う。
 * 実機では停止ループになる。                              */

IMPORT void hal_exit(W code) __attribute__((noreturn));

/* --- 例外 ----------------------------------------------------------
 * ベクタが無いとフォルトが無言のハングになる。起動直後に入れる。 */

IMPORT void hal_exception_init(void);

/* --- 時間の読み出し ------------------------------------------------
 * 中心核の時間管理（BTRON3 §1.3）の土台。いまは単調カウンタの読み出しだけで、
 * 割込みは GIC ができてから (D8 / machdesc の timer_intid)。
 *
 * 周波数はレジスタから直接読めるので、ファームウェアの記述に依らない。 */

IMPORT UD hal_timer_freq(void);  /* Hz。0 なら読めなかった */
IMPORT UD hal_timer_count(void); /* 単調増加のカウンタ値 */

/* usec マイクロ秒だけ待つ。カウンタを回して待つだけで、割込みは使わない。 */
IMPORT void hal_timer_wait_us(UD usec);

#endif /* __BTRON_HAL_H__ */

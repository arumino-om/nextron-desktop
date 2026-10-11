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

/* --- ページサイズ (R2) --------------------------------------------
 * A13 で 4KB と決めたが、定数として配らず HAL 経由で参照する。
 * 将来 16KB granule へ移行する余地を残すため。            */

IMPORT UW hal_page_size(void);
IMPORT UW hal_page_shift(void);

/* --- 実行の終了 ----------------------------------------------------
 * テスト実行時に QEMU を終了させるために使う。
 * 実機では停止ループになる。                              */

IMPORT void hal_exit(W code) __attribute__((noreturn));

#endif /* __BTRON_HAL_H__ */

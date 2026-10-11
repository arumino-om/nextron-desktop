/*
 * screen.h — 画面（フレームバッファと画面コンソール）
 *
 * docs/design-issues.md E24 / 詳細 E24 / 規律 R18:
 *
 *   画面はカーネルが保有し、gopn_dev("SCREEN") で貸し出す。
 *   カーネルの上位半分の写像は貸出と独立に残る。
 *
 * **したがってここはカーネル常駐 (GPL) であって、DP (src/lib/dp, LGPL) とは
 * 別物。** Phase 4 のディスプレイプリミティブはこの上ではなく、リースを
 * 受け取って自分で描く。ここにあるのは次の 2 つのためだけ。
 *
 *   - 画面コンソール（外殻が立ち上がる前・落ちた後の出力先）
 *   - 緊急画面（WM の協力なしに動かなければならない）
 *
 * 【未実装】リース (gopn_dev) 本体。Phase 4 で E24 のとおりに作る。
 * いまはカーネル側の写像を用意して描けることまで。
 */

#ifndef __BTRON_SCREEN_H__
#define __BTRON_SCREEN_H__

#include <btron/errcode.h>
#include <btron/typedef.h>

typedef struct Screen {
	UD base;   /* 上位半分の MMIO 別名（規律 R18。貸出とは独立に残る） */
	UD pa;     /* 物理アドレス */
	UW width;  /* 有効画素数 */
	UW height;
	UW pitch;  /* 1 走査線のバイト数。width * 4 とは限らない */
	UW format; /* BFB_* (bootinfo.h) */
} Screen;

/* BootInfo.fb から画面を立てる。起動第 2 段（上位半分）で呼ぶ。
 * 画面が無い構成では E_NOEXS を返す（異常ではない）。 */
IMPORT ERR screen_init(void);

/* 画面。無ければ 0。 */
IMPORT CONST Screen *screen(void);

/* 色を作る。画素形式（BGRA / RGBA）の差をここで吸収する。 */
IMPORT UW screen_rgb(UW r, UW g, UW b);

IMPORT void screen_clear(UW color);
IMPORT void screen_fill(W x, W y, W w, W h, UW color);
IMPORT void screen_pixel(W x, W y, UW color);

/* 画素を読み返す。**検証用。**
 *
 * 画面出力の自動テストはスクリーンショット比較になりがちで CI に乗らない。
 * カーネル自身が書いた画素を読み返して確かめれば、シリアル経由の
 * 普通のテストとして扱える。 */
IMPORT UW screen_get(W x, W y);

/* 画面内で矩形を縦にずらす（スクロール）。
 *
 * **画面はデバイスメモリでキャッシュが効かない。** 1 画素ずつ読み書きすると
 * 800x600 で 50 万回の非キャッシュアクセスになり、目に見えて遅い。
 * 行単位でまとめて動かす。
 *
 * 本来の解はバックバッファ (E2) を RAM に持って差分だけ転送することで、
 * それは Phase 4 の DP の仕事。ここは外殻が無いときの代替なので、
 * 行単位のコピーで十分とする。 */
IMPORT void screen_scroll_up(W y, W h, W dy, UW fill);

/* ------------------------------------------------------------------ */
/* 画面コンソール                                                      */
/* ------------------------------------------------------------------ */

/* 画面コンソールを有効にし、`hal_console_puts()` の出力を画面にも出す。
 * 以後シリアルと画面の両方に出る。 */
IMPORT ERR screen_console_init(void);

IMPORT void screen_console_puts(CONST B *utf8);
IMPORT void screen_console_color(UW fg, UW bg);

/* 上から n 行を固定領域として確保する（表題帯・状態表示用）。
 * スクロールはその下だけで起きる。 */
IMPORT void screen_console_reserve(UW n);

/* 画面上の文字数。桁 × 行（固定領域を除いたスクロール領域の行数）。 */
IMPORT UW screen_console_cols(void);
IMPORT UW screen_console_rows(void);

/* 指定位置に 1 行書く（カーソルを動かさない）。緊急画面が使う。 */
IMPORT void screen_text_at(W col, W row, CONST B *utf8, UW fg, UW bg);

#endif /* __BTRON_SCREEN_H__ */

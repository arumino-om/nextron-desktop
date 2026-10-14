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

/* 画面をプロセスへ貸し出す。下位半分へ写して、その番地を返す。
 *
 * **BTRON3 の API ではない。** 仕様 §2.3.1 の `gopn_dev("SCREEN")` が
 * 「画面の貸し出し」で、その写像を実際に行うのがこれ。外殻をユーザ特権で
 * 動かすのに要る。上位半分の別名は残す（規律 R18。緊急画面のため）。 */
struct AddrSpace;
IMPORT ERR screen_lend(struct AddrSpace *as, UD *user_base);

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

/* ------------------------------------------------------------------ */
/* 緊急画面 (E24 / T4-02)                                              */
/*                                                                     */
/* **外殻の協力を一切必要としない。** 画面はカーネルが保有していて、     */
/* 上位半分の写像は貸出と独立に残る（規律 R18）。だから WM が落ちて      */
/* いようと、例外の中からでも描ける。                                   */
/* ------------------------------------------------------------------ */

/* 画面を占有して黒い面に切り替え、以後の `hal_console_puts()` の出力を
 * そこへ流す。画面が無ければ False を返し、何もしない。
 * カーソル状態を使わないので、画面コンソールの途中で呼ばれても壊れない。
 *
 * **赤一色にはしない。** 画面全体が赤いと、復旧できる不具合でも
 * 「壊れた」という印象を与えて必要以上に不安にさせる。落ち着いた黒地に
 * して、見出しだけ琥珀色の細い罫で示す。 */
IMPORT Bool screen_panic_begin(CONST B *title);

/* 緊急画面を畳む。**本物のパニックでは戻らないので呼ばれない。**
 * 自己検査がわざと例外を起こさずに描画を確かめるためにある。 */
IMPORT void screen_panic_end(void);

IMPORT Bool screen_panic_active(void);

/* 緊急画面の配色。検査が色を決め打ちしないで済むように公開する。 */
IMPORT UW screen_panic_bg(void);
IMPORT UW screen_panic_fg(void);

#endif /* __BTRON_SCREEN_H__ */

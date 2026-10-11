/*
 * dpi.h — ディスプレイプリミティブの内部定義（公開しない）
 *
 * 公開 API は include/btron/dp.h。
 *
 * 【層の約束】**ここは src/lib 側（LGPL 予定。J2 / 規律 R14）。**
 * カーネルや外殻サーバのソースから include してはならない。
 * 使えるのは include/btron の公開ヘッダと、周辺核のシステムコール、
 * それにフォントマネージャの利用側 (src/lib/appl/font) だけである。
 */

#ifndef __BTRON_LIB_DPI_H__
#define __BTRON_LIB_DPI_H__

#include <btron/dp.h>
#include <btron/fontman.h>

/* 描画環境。仕様 §2.2.4 が列挙しているデータを持つ。 */
typedef struct DEnv {
	Bool used;
	W owner; /* 生成したプロセス。gcls_env の権限確認に使う */

	TC dev[DP_DEVNAME_LEN + 1]; /* 対象デバイス名。先頭が TNULL なら汎用 */
	DEVSPEC spec;
	Bool has_spec;
	CSPEC cspec;
	Bool has_cspec; /* カラー情報を持つか。無ければ絶対 RGB 指定は無視 */

	BMP bmp;   /* 対象ビットマップ。baseaddr の指す領域は共有 */
	UB *base;  /* bmp.baseaddr[0]。プレーン 1 枚だけ扱う */
	UW bound;  /* 境界ビット数 */
	UW valid;  /* 有効ビット数 */
	UW vmask;  /* 有効ビットのマスク */

	PIXVAL maskpix; /* マスクピクセル値 */

	RECT frame;   /* フレーム長方形 */
	RECT visible; /* 表示長方形 */
	RECT clip;    /* クリッピング領域（矩形のみ） */
	Bool has_clip;

	/* 文字属性 */
	W chx;
	W chy;
	COLOR chfgc;
	COLOR chbgc;
	W chdir;
	CGAP chgap;

	/* 線マスクパターン。既定は実線。 */
	UB lmask[8];
	UW lmask_bytes;
} DEnv;

IMPORT DEnv dp_env[DP_MAX_GID + 1];

/* gid の検査。不正なら 0 を返す（呼び出し側が EG_GID を返す）。 */
IMPORT DEnv *dp_get(GID gid);

/* 実際に描画される領域。bounds ∩ frame ∩ visible ∩ clip。 */
IMPORT RECT dp_cliprect(CONST DEnv *e);

/* 長方形が空か。half-open property。 */
IMPORT Bool dp_rect_empty(CONST RECT *r);

/* 長方形の共通部分。 */
IMPORT RECT dp_rect_isect(CONST RECT *a, CONST RECT *b);

/* 点が長方形に含まれるか。right / bottom は含まない。 */
IMPORT Bool dp_rect_in(CONST RECT *r, W x, W y);

/* ピクセルの読み書き。**絶対座標**（ビットマップ左上を (0,0) とする）。 */
IMPORT UW dp_pix_get(CONST DEnv *e, W ax, W ay);
IMPORT void dp_pix_put(CONST DEnv *e, W ax, W ay, UW val);

/* 描画演算モードの適用。 */
IMPORT UW dp_dcm(W op, UW src, UW dest, UW vmask);

/* マスクピクセル指定の判定。True なら描画してよい。 */
IMPORT Bool dp_maskok(CONST DEnv *e, DCM mode, UW dest);

/* 描画モードの妥当性。 */
IMPORT Bool dp_mode_ok(DCM mode);

/* COLOR → ピクセル値。透明なら False。 */
IMPORT Bool dp_color(CONST DEnv *e, COLOR c, UW *pix);

/* 描画パターンの解決。相対座標 (rx, ry) に置くべき値。透明なら False。
 * 標準パターン（WHITE0 等の小さな整数）もここで扱う。 */
IMPORT Bool dp_pat(CONST DEnv *e, CONST PAT *pat, W rx, W ry, UW *pix);

/* パターンが使えるか（kind と中身の検査）。 */
IMPORT ERR dp_pat_check(CONST PAT *pat);

/* 1 ピクセル描く。相対座標。クリップとマスクを見る。 */
IMPORT void dp_plot(CONST DEnv *e, CONST RECT *clip, W rx, W ry, UW src,
                    DCM mode);

/* 相対 → 絶対 / 絶対 → 相対。 */
#define DP_ABS_X(e, x) ((x) - (e)->bmp.bounds.c.left)
#define DP_ABS_Y(e, y) ((y) - (e)->bmp.bounds.c.top)

/* 文字描画の内部。text.c が使う。 */
IMPORT CONST FontShared *dp_font(void);

#endif /* __BTRON_LIB_DPI_H__ */

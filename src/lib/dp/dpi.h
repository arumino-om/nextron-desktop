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

	/* 前置長方形リスト。ここに含まれる領域には描画しない。
	 * ウィンドウの重なりを表すのに使う。 */
	RECT fore[DP_MAX_FORE];
	W nfore;

	/* 文字属性 */
	W chx;
	W chy;
	COLOR chfgc;
	COLOR chbgc;
	W chdir;
	CGAP chgap;

	/* 損傷の記録先（本OS の拡張。dp.h の DMGREC）。
	 * 0 なら記録しない。窓の裏画面に描くときだけ使う。 */
	DMGREC *dmg;

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

/* 点が前置長方形リストに入っているか。入っていれば描画しない。 */
IMPORT Bool dp_in_fore(CONST DEnv *e, W rx, W ry);

/* 損傷を記録する。**絶対座標**で、[ax0, ax1) × [ay0, ay1)。
 * 記録先が無ければ何もしない。**区間ごとに 1 回**呼ぶ（画素ごとに
 * 呼ぶと走査線の速い経路が台無しになる）。 */
IMPORT void dp_mark(CONST DEnv *e, W ax0, W ay0, W ax1, W ay1);

/* 1 ピクセル描く。相対座標。クリップと前置長方形とマスクを見る。 */
IMPORT void dp_plot(CONST DEnv *e, CONST RECT *clip, W rx, W ry, UW src,
                    DCM mode);

/* ------------------------------------------------------------------ */
/* 走査線単位の高速経路                                                */
/* ------------------------------------------------------------------ */

/* 面を塗るときに 1 ピクセルずつ dp_plot を呼ぶと、同じクリップ判定と
 * 行頭アドレス計算を全ピクセルで繰り返すことになる。行単位にまとめる
 * ための道具をここに置く。詳しい理由は pixel.c の同名の節。 */

/* パターン 1 行を先に解いておく器。横方向の周期の分だけ持つ。 */
#define DP_PAT_PERIOD 64

typedef struct PatRow {
	W period;              /* 横方向の周期。1 以上 */
	UW pix[DP_PAT_PERIOD]; /* 相対 x = 0, 1, ... の値 */
	UB opaque[DP_PAT_PERIOD]; /* 0 なら透明（描かない） */
	Bool solid;               /* 全て同じ値で不透明。塗りを単純化できる */
} PatRow;

/* 高速経路に乗せられるモード・環境か。乗らなければ dp_plot を使う。 */
IMPORT Bool dp_fast_ok(CONST DEnv *e, DCM mode);

/* 走査線 ry のパターンを 1 周期だけ解く。周期が大きすぎる等で
 * 行単位に解けないパターンなら False。 */
IMPORT Bool dp_pat_row(CONST DEnv *e, CONST PAT *pat, W ry, PatRow *pr);

/* 走査線 ry の [*x, x1) から前置長方形を除いた次の区間 [*rs, *re) を返す。
 * *x は呼ぶたびに進む。区間が尽きたら False。 */
IMPORT Bool dp_next_run(CONST DEnv *e, W ry, W x1, W *x, W *rs, W *re);

/* 走査線の区間をパターン行で塗る。**判定は一切しない** —— クリップも
 * 前置長方形も呼び出し側で外してあることが前提。 */
IMPORT void dp_run(CONST DEnv *e, CONST PatRow *pr, W rx0, W rx1, W ry);

/* 走査線の区間を単色で塗る。クリップと前置長方形はここで見る。
 * 高速経路に乗らないときは dp_plot に落ちるので、呼ぶ側は気にしなくてよい。 */
IMPORT void dp_hline(CONST DEnv *e, CONST RECT *clip, W rx0, W rx1, W ry,
                     UW src, DCM mode);

/* 走査線の区間を s から d へ転送する。**判定は一切しない**（dp_run と同じ
 * 約束）。sx0 が dx0 に対応する。境界ビット数と有効ビット数が同じときだけ
 * 使える。back は同じビットマップで重なるときの走査の向き。 */
IMPORT void dp_copy_run(CONST DEnv *s, W sx0, W sy, CONST DEnv *d, W dx0,
                        W dx1, W dy, Bool back);

/* 相対 → 絶対 / 絶対 → 相対。 */
#define DP_ABS_X(e, x) ((x) - (e)->bmp.bounds.c.left)
#define DP_ABS_Y(e, y) ((y) - (e)->bmp.bounds.c.top)

/* 画面全体を指す描画環境を GID を消費せずに組む。pointer.c が使う。
 * クリッピングも前置長方形も掛けない。 */
IMPORT Bool dp_screen_env(DEnv *e);

/* 文字描画の内部。text.c が使う。 */
IMPORT CONST FontShared *dp_font(void);

#endif /* __BTRON_LIB_DPI_H__ */

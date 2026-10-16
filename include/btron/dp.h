/*
 * dp.h — ディスプレイプリミティブ (BTRON3 第2章)
 *
 * ビットマップ上に図形と文字を描く基本関数群。仕様の言葉で言えば
 * 「マルチウィンドウ機能やビジネスアプリケーションで要求される機能を
 * ビットマップ上で効率良くサポートするための基本的な関数群」であり、
 * **CAD やイメージ処理のための描画関数群とは目的が違う**（仕様 §2.1.1）。
 *
 * **第 3 のエラー族 `EG_*` を返す。** `E_*`（中心核）や `ER_*`（周辺核）と
 * 混ぜてはならない（規律 R20）。番号空間も重なっている。
 *
 * ------------------------------------------------------------------------
 * 層の位置
 * ------------------------------------------------------------------------
 *
 * 詳細 E1 の分類では DP は**呼び出し粒度が極細**（図形 1 個・文字 1 個
 * ごと）で**グローバル可変状態を持たない**（描画環境はプロセス固有）。
 * したがってサーバにせず、**プロセス内のライブラリ**として実装する
 * (`src/lib/dp/`)。描画は対象ビットマップへ直接行う。
 *
 * ------------------------------------------------------------------------
 * いま実装していないもの
 * ------------------------------------------------------------------------
 *
 * 宣言はするが `EG_NOSPT` を返す、または宣言していないものがある。
 * 素直に書ける図形から順に実装しており、残りは外殻が要求した時点で足す。
 *
 *   角丸長方形 / 楕円 / 弧 / 扇形 / 弓形 / 多角形 / 曲線   未実装
 *   図形の回転 (angle != 0)                                EG_NOSPT
 *   圧縮ビットマップ / ピッキング                          未実装
 *   カラーマップ (gset_col / gget_col)                     EG_ENV
 *   内部パターンイメージ (gsiz_pat / gcre_pat)              EG_NOSPT
 *   実線以外の線種                                          EG_NOSPT
 *   任意領域クリッピング (GRGN の ns > 0)                   EG_NOSPT
 *   ポインタ制御 (§2.6)                      マウスドライバ (§1.7/§1.8) 待ち
 *   フォント切替 (gset_fon / gset_scr)        フォントマネージャの本体待ち
 */

#ifndef __BTRON_DP_H__
#define __BTRON_DP_H__

#include <btron/errcode.h>
#include <btron/typedef.h>

/* ------------------------------------------------------------------ */
/* 座標平面 / 点 / 長方形 (§2.2.1)                                     */
/*                                                                     */
/* PNT と RECT は typedef.h にある（基本データタイプ）。                */
/*                                                                     */
/* **長方形は右と下の辺を含まない** (half-open property)。             */
/*   点 (x,y) が含まれる条件                                            */
/*     (left <= x) && (x < right) && (top <= y) && (y < bottom)         */
/*   top >= bottom または left >= right なら領域は空。                  */
/* ------------------------------------------------------------------ */

typedef struct RectangleList {
	struct RectangleList *next; /* 次の要素。最後は NULL */
	RECT comp;                  /* 要素としての長方形 */
} RLIST;

/* 座標系は 16 ビット整数 (-32768〜32767)。水平は右、垂直は下が正。 */
#define DP_COORD_MIN (-32768)
#define DP_COORD_MAX 32767

/* ------------------------------------------------------------------ */
/* ビットマップ (§2.2.2)                                               */
/*                                                                     */
/* 連続した 1 次元メモリ配列を長方形領域の 2 次元配列として解釈する。   */
/*                                                                     */
/* 【本OS の解釈】仕様は「低位アドレスのバイトの MSB が左上のピクセル」  */
/* と書いているが、これは**ピクセルの並び順**の規定である。1 ピクセルが */
/* 複数バイトを占める場合の値のバイト順までは規定していない。          */
/* 本OS は A4 の決定どおり**内部をリトルエンディアン**とし、            */
/* 8/16/32 ビット境界のピクセル値はネイティブ整数として置く。           */
/* 1/2/4 ビット境界のピクセルはバイト内で MSB 側から詰める（仕様どおり）。*/
/* UEFI GOP のフレームバッファがリトルエンディアンの 32 ビット語なので、 */
/* これ以外だと画面に出した瞬間に色が入れ替わる。                       */
/* ------------------------------------------------------------------ */

#define DP_MAX_PLANES 28 /* ビットマップの最大の深さ (§2.2.2) */

typedef struct Bitmap {
	UW planes;    /* プレーンの数 (1〜28) */
	UH pixbits;   /* 上位バイト = 境界ビット数、下位バイト = 有効ビット数 */
	UH rowbytes;  /* 1 プレーンの横幅のバイト数（偶数） */
	RECT bounds;  /* 境界長方形（座標定義） */
	UB *baseaddr[1]; /* 開始アドレス（planes 個の配列）。0 は不在 */
} BMP;

/* pixbits の分解。「単にピクセルビット数と言った場合は有効ビット数」（仕様） */
#define BMP_BOUNDBITS(pb) ((UW)((pb) >> 8) & 0xFFU)
#define BMP_VALIDBITS(pb) ((UW)(pb) & 0xFFU)
#define BMP_PIXBITS(bound, valid) ((UH)(((bound) << 8) | (valid)))

typedef W PIXVAL; /* ピクセル値。下位 depth(最大 28) ビットが有効 */

/* イメージ圧縮方法 (§2.2.2) */
#define NOCOMPAC 0  /* 非圧縮 */
#define MHCOMPAC 1  /* MH 符号 */
#define MR2COMPAC 2 /* MR 符号 (K = 2) */
#define MR4COMPAC 3 /* MR 符号 (K = 4) */

typedef struct CompactedBitmap {
	W compac; /* イメージ圧縮方法 */
	BMP bmp;
} CBMP;

/* ------------------------------------------------------------------ */
/* カラー表現 (§2.2.3)                                                 */
/*                                                                     */
/*   bit31      透明 (trans)。描画カラー指定のみで有効                  */
/*   bit30-28   カラー表現 (crep)                                       */
/*   bit27-0    カラー値 (cval)                                         */
/*                                                                     */
/*   crep = 0   直接ピクセル値（下位 depth ビットが有効）                */
/*   crep = 1   RGB 絶対指定。各色 0〜255 に正規化                       */
/*                XXXX RRRRRRRR GGGGGGGG BBBBBBBB                      */
/* ------------------------------------------------------------------ */

typedef UW COLOR;

#define COL_TRANS 0x80000000U /* 透明 */
#define COL_CREP_SHIFT 28
#define COL_CREP_MASK 0x70000000U
#define COL_CVAL_MASK 0x0FFFFFFFU

#define COL_PIXVAL 0 /* crep = 0 */
#define COL_RGB 1    /* crep = 1 */

#define COLOR_CREP(c) (((UW)(c) & COL_CREP_MASK) >> COL_CREP_SHIFT)
#define COLOR_CVAL(c) ((UW)(c) & COL_CVAL_MASK)
#define COLOR_IS_TRANS(c) ((((UW)(c)) & COL_TRANS) != 0)

/* RGB 絶対指定を組み立てる。各要素 0〜255。 */
#define COLOR_RGB(r, g, b)                                                 \
	((COLOR)(((UW)COL_RGB << COL_CREP_SHIFT) | (((UW)(r) & 0xFFU) << 16) | \
	         (((UW)(g) & 0xFFU) << 8) | ((UW)(b) & 0xFFU)))

/* 仕様が `gopn_dev` の初期値として挙げている文字カラー。
 * 「前景：黒(0x10000000)、背景：白(0x10FFFFFF)」= crep 1 の RGB 絶対指定。 */
#define COL_BLACK 0x10000000U
#define COL_WHITE 0x10FFFFFFU

/* ------------------------------------------------------------------ */
/* デバイス仕様 / カラー指定 (§2.2.4)                                  */
/* ------------------------------------------------------------------ */

typedef struct DevSpec {
	H attr;     /* デバイスの属性。0LMX XXXX XXXX PRRR */
	H planes;   /* プレーン数 */
	H pixbits;  /* ピクセルビット数（境界／有効） */
	H hpixels;  /* 横のピクセル数。規定が無ければ 0 */
	H vpixels;  /* 縦のピクセル数 */
	H hres;     /* 横の解像度。1cm 当たり。インチなら負 */
	H vres;     /* 縦の解像度 */
	H color[4]; /* カラー情報。下位バイト = 階調ビット数、上位 = ビット位置 */
	H resv[6];
} DEVSPEC;

/* DEVSPEC.attr のビット */
#define DEV_LOCK 0x4000U /* L: デバイス描画環境レコードがロック対象 */
#define DEV_MEM 0x2000U  /* M: 固有イメージ領域（ビットマップ）を持つ */
#define DEV_CMAP 0x0008U /* P: カラーマップ有 */
#define DEV_CTYPE 0x0007U /* R: カラータイプ */

#define DEV_MONO 0 /* R = 0: 白黒 */
#define DEV_RGB 1  /* R = 1: RGB カラー */
#define DEV_CMY 2  /* R = 2: CMY カラー（プリンタ） */

/* color[] の分解。下位バイトが階調ビット数、上位バイトがビット位置。 */
#define DEV_CBITS(c) ((UW)(UH)(c) & 0xFFU)
#define DEV_CPOS(c) (((UW)(UH)(c) >> 8) & 0xFFU)
#define DEV_COLINFO(pos, bits) ((H)(((pos) << 8) | (bits)))

typedef struct ColorSpec {
	W attr;        /* DEVSPEC.attr の下位 4 ビットと同じ意味 */
	H info[4];     /* カラーマップ形式なら info[0] = エントリ数 */
	COLOR *colmap; /* カラーマップ。カラーマップ形式でないときは無意味 */
} CSPEC;

/* ------------------------------------------------------------------ */
/* 描画モード (§2.2.4)                                                 */
/*                                                                     */
/*   0000 ... 0000 PPCF MMMM MMMM                                      */
/*     M: 描画演算モード  F: 形式変換  C: カラー変換  P: マスクピクセル  */
/* ------------------------------------------------------------------ */

typedef W DCM;

/* 描画演算モード。src = 書き込む値、dest = 現在の値 */
#define G_STORE 0 /* src            -> result */
#define G_XOR 1   /* src XOR dest   -> result */
#define G_OR 2    /* src OR  dest   -> result */
#define G_AND 3   /* src AND dest   -> result */
#define G_CPYN 4  /* NOT src        -> result */
#define G_XORN 5  /* NOT src XOR dest -> result */
#define G_ORN 6   /* NOT src OR  dest -> result */
#define G_ANDN 7  /* NOT src AND dest -> result */
#define G_NOP 8   /* dest           -> result */

#define G_OPMASK 0x00FF

#define G_CVFORM 0x0100  /* ビットマップ形式変換 */
#define G_CVCOLOR 0x0200 /* カラー変換 */
#define G_MASK 0x0400    /* マスクピクセル値と同一の dest のみ描画 */
#define G_MASKN 0x0800   /* マスクピクセル値と異なる dest のみ描画 */

/* ------------------------------------------------------------------ */
/* 描画パターン (§2.2.4)                                               */
/* ------------------------------------------------------------------ */

typedef W PatKind;

#define PAT_SPAT 0 /* マスク + 前景色 / 背景色 */
#define PAT_MPAT 1 /* ビットマップ + マスク */
#define PAT_IPAT 2 /* 内部パターンイメージ */

typedef union Pattern {
	struct {
		PatKind kind; /* = PAT_SPAT */
		UH hsize;     /* 横サイズ（ピクセル数） */
		UH vsize;     /* 縦サイズ（ピクセル数） */
		COLOR fgcol;  /* マスクの "1" の色。負（透明ビット）で透明 */
		COLOR bgcol;  /* マスクの "0" の色 */
		UB *mask;     /* パターンマスクデータ。spat では NULL 不可 */
	} spat;
	struct {
		PatKind kind; /* = PAT_MPAT */
		UH hsize;
		UH vsize;
		UB *mask; /* "0" の部分は透明。NULL なら透明部分なし */
		BMP *bmap;
	} mpat;
	struct {
		PatKind kind; /* = PAT_IPAT */
		UH hsize;
		UH vsize;
		UB *pat;
	} ipat;
} PAT;

/* 標準パターンマスク。**パターンのサイズに無関係に使用できる**（仕様）。 */
#define _FILL0 1
#define _FILL12 2
#define _FILL25 3
#define _FILL50 4
#define _FILL75 5
#define _FILL87 6
#define _FILL100 7

#define FILL0 ((UB *)(VP)(UD)_FILL0)
#define FILL12 ((UB *)(VP)(UD)_FILL12)
#define FILL25 ((UB *)(VP)(UD)_FILL25)
#define FILL50 ((UB *)(VP)(UD)_FILL50)
#define FILL75 ((UB *)(VP)(UD)_FILL75)
#define FILL87 ((UB *)(VP)(UD)_FILL87)
#define FILL100 ((UB *)(VP)(UD)_FILL100)

/* 標準パターン。すべて前景色：黒、背景色：白。 */
#define _WHITE0 1
#define _BLACK12 2
#define _BLACK25 3
#define _BLACK50 4
#define _BLACK75 5
#define _BLACK87 6
#define _BLACK100 7

#define WHITE0 ((PAT *)(VP)(UD)_WHITE0)
#define BLACK12 ((PAT *)(VP)(UD)_BLACK12)
#define BLACK25 ((PAT *)(VP)(UD)_BLACK25)
#define BLACK50 ((PAT *)(VP)(UD)_BLACK50)
#define BLACK75 ((PAT *)(VP)(UD)_BLACK75)
#define BLACK87 ((PAT *)(VP)(UD)_BLACK87)
#define BLACK100 ((PAT *)(VP)(UD)_BLACK100)

/* ------------------------------------------------------------------ */
/* 線属性 (§2.2.4)                                                     */
/*                                                                     */
/*   0000 ... TTTT TTTT WWWW WWWW                                      */
/*     W: 線幅 (0〜255)   T: 線種 (0〜5, 255)                           */
/* ------------------------------------------------------------------ */

typedef W LATTR;

#define LN_SOLID 0  /* 実線 */
#define LN_DASH 1   /* 破線 */
#define LN_DOT 2    /* 点線 */
#define LN_DDASH 3  /* 一点鎖線 */
#define LN_DDDASH 4 /* 二点鎖線 */
#define LN_LDASH 5  /* 長破線 */
#define LN_MASK 255 /* 描画環境に登録した線マスクパターンを使う */

#define LATTR_WIDTH(a) ((UW)(a) & 0xFFU)
#define LATTR_KIND(a) (((UW)(a) >> 8) & 0xFFU)
#define LATTR_MAKE(kind, width) ((LATTR)(((UW)(kind) << 8) | (UW)(width)))

/* ------------------------------------------------------------------ */
/* 任意領域 (§2.2.4)                                                   */
/*                                                                     */
/* ns = 0 のとき「任意領域は長方形 r に等しい」（仕様）。本OS はいまこの */
/* 場合だけを扱い、ns > 0 は EG_NOSPT を返す。                          */
/* ------------------------------------------------------------------ */

typedef struct HorizontalRegion {
	UH nx;    /* 水平座標の数（偶数） */
	UH x[1];  /* 値の小さい順。r.c.left を 0 とする非負の値 */
} HRGN;

typedef struct GenericRegion {
	RECT r; /* 任意領域全体を囲む最小の長方形（相対座標） */
	UW ns;  /* 垂直区間数。0 なら領域は r に等しい */
	struct {
		UH y;      /* 区間の開始垂直座標値 */
		HRGN *hp;  /* 区間の水平領域。NULL なら空 */
	} s[1];
} GRGN;

/* ------------------------------------------------------------------ */
/* 多角形 / 回転角 (§2.4)                                              */
/* ------------------------------------------------------------------ */

typedef struct Polygon {
	UH round; /* 角を丸める場合の円の直径 */
	UH size;  /* 頂点の数 */
	PNT pt[1];
} POLY;

typedef UW DEG; /* 回転角度 */

/* ------------------------------------------------------------------ */
/* 文字描画 (§2.2.5)                                                   */
/* ------------------------------------------------------------------ */

/* 文字描画方向 */
#define TORIGHT 0 /* 右向き（文字枠のベースラインの左端が描画位置） */
#define TOLEFT 1  /* 左向き（ベースラインの右端） */
#define TOUP 2    /* 上向き（文字枠の左上） */
#define TODOWN 3  /* 下向き（文字枠の左下） */
#define TOARB 4   /* 任意の向き。未サポート */

typedef struct SignedGap {
	H dir;  /* 0: 文字描画方向にとる / != 0: 反対方向（重なる） */
	UH gap; /* 文字間隔 */
} SGAP;

typedef struct CharacterGap {
	SGAP hgap;   /* 水平文字間隔 */
	SGAP vgap;   /* 垂直文字間隔 */
	SGAP hspgap; /* 空白文字の直後に追加される水平間隔 */
	SGAP vspgap; /* 空白文字の垂直間隔 */
} CGAP;

typedef W GID; /* 描画環境 ID */

/* 実装の制限値 (D9。暫定) */
#define DP_MAX_GID 32     /* 同時に開ける描画環境の数 */
#define DP_MAX_FORE 16    /* 前置長方形リストの要素数 */
#define DP_DEVNAME_LEN 10 /* デバイス名の長さ（gget_dev は 11 要素を要求） */

/* 必ずサポートするデバイス（仕様 §2.2.4） */
#define DP_DEV_SCREEN "SCREEN"
#define DP_DEV_MSGPNL "MSGPNL"

/* ------------------------------------------------------------------ */
/* 基本関数 (§2.3)                                                     */
/* ------------------------------------------------------------------ */

/* 初期化 / 終了。**通常アプリケーションは使用しない**（仕様）。 */
IMPORT ERR gini_dsp(W mode);
IMPORT ERR gfin_dsp(void);

/* デバイス描画環境の生成。dev は TC の文字列。 */
/* ------------------------------------------------------------------ */
/* 損傷の記録（**本OS の拡張。仕様には無い**）                         */
/*                                                                     */
/* 詳細 E2 で窓ごとの裏画面を入れたとき、「利用側がどこを描いたかを    */
/* 知る手立てが無い」ことが宿題として残った。知る手立てが無いと、      */
/* マネージャは要求のたびに窓を丸ごと画面へ貼り直すしかなく、          */
/* **走査が追い越してちらつきになる**（実際にそうなっていた）。        */
/*                                                                     */
/* そこで、描画環境に「変わった範囲」の置き場を登録できるようにする。  */
/* 置き場が共有メモリなら、別プロセスのマネージャがそのまま読める。    */
/* 仕様の API は 1 つも変わらない。                                    */
/* ------------------------------------------------------------------ */

typedef struct DMGREC {
	RECT r;  /* 変わった範囲。ビットマップの絶対座標 */
	W any;   /* 0 なら変わっていない */
	UW seq;  /* 変わるたびに進む。読み落としの検出に使う */
} DMGREC;

/* 記録先を登録する。`rec` が 0 なら記録をやめる。
 * **記録は積むだけで、消すのは読む側の仕事**（マネージャが合成した
 * あとに any を 0 に戻す）。 */
IMPORT ERR gset_dmg(GID gid, DMGREC *rec);

IMPORT GID gopn_dev(CONST TC *dev, CONST B *par);

/* メモリ描画環境の生成。dev = NULL で汎用メモリ描画環境。
 * bmap の内容は生成後に直接参照されるので**変更してはいけない**（仕様）。 */
IMPORT GID gopn_mem(CONST TC *dev, BMP *bmap, CONST B *par);

/* 描画環境の複製。デバイス情報・カラー情報・ビットマップは共有される。 */
IMPORT GID gnew_env(GID gid);

/* 描画環境の削除 / 初期化。 */
IMPORT ERR gcls_env(GID gid);
IMPORT ERR gini_env(GID gid);

/* デバイス仕様 / 描画環境のデバイス情報。 */
IMPORT ERR gget_spc(CONST TC *dev, DEVSPEC *buf);
IMPORT ERR gget_dev(GID gid, TC *dev);
IMPORT ERR gget_dsp(GID gid, DEVSPEC *buf);
IMPORT ERR gget_csp(GID gid, CSPEC *buf);

/* カラー指定 → ピクセル値。 */
IMPORT W gcnv_col(GID gid, COLOR cv, PIXVAL *pixv);

/* マスクピクセル値。 */
IMPORT ERR gset_msk(GID gid, PIXVAL pixv);
IMPORT ERR gget_msk(GID gid, PIXVAL *pixv);

/* クリッピング領域 / フレーム長方形 / 表示長方形。 */
IMPORT ERR gset_clp(GID gid, CONST GRGN *clip);
IMPORT ERR gget_clp(GID gid, GRGN *clip, W csize, W *a_size);
IMPORT ERR gset_fra(GID gid, RECT r);
IMPORT ERR gget_fra(GID gid, RECT *rp);
IMPORT ERR gset_vis(GID gid, RECT r);
IMPORT ERR gget_vis(GID gid, RECT *rp);

/* 前置長方形リスト。**ここに含まれる領域には描画しない。**
 *
 * 仕様 §2.2.4「実際に…描画が行なわれる領域は、対象ビットマップの境界
 * 長方形、フレーム長方形、表示長方形、設定されたクリッピング領域に含まれ、
 * かつ、**前置長方形リストに含まれない**領域となる」。
 *
 * ウィンドウマネージャがこれを使う — 手前にあるウィンドウの矩形を並べて
 * おけば、奥のウィンドウが手前を上書きしなくなる。これが無いと
 * 重なり合うウィンドウを正しく描けない。
 *
 * `rlp = NULL` でリストを空にする。 */
IMPORT ERR gset_for(GID gid, CONST RLIST *rlp);

/* **`rlp` は呼び出し側が鎖を張った配列でなければならない。**
 * 仕様「リストの各要素の r_next は正しく設定されているものとする」。
 * 未初期化の `RLIST` を 1 つ渡すと、2 要素目で野良ポインタを踏む。
 * 要素数が足りなければそこで打ち切られる（個数は返らない）。 */
IMPORT ERR gget_for(GID gid, RLIST *rlp);

/* 対象ビットマップと座標系。 */
IMPORT ERR gget_bmp(GID gid, BMP *bmap);
IMPORT ERR gget_bnd(GID gid, RECT *rp);
IMPORT ERR gmov_bnd(GID gid, W dh, W dv);
IMPORT ERR gmov_cor(GID gid, W dh, W dv);

/* 相対座標 ←→ 絶対座標。絶対座標は左上を常に (0,0) とした座標。 */
IMPORT ERR gcnv_abs(GID gid, PNT *pp);
IMPORT ERR gcnv_rel(GID gid, PNT *pp);

/* ------------------------------------------------------------------ */
/* 図形描画関数 (§2.4 の実装済み分)                                    */
/* ------------------------------------------------------------------ */

/* 点。gtst_pnt は 0 = 描画される / 1 = クリップされる。
 *
 * **仕様は戻り型を `Bool` と書いているが `W` にしてある。** 仕様自身が
 * 「<0 : エラー」と規定していて、本OS の `Bool` は `{False, True}` の
 * 列挙型（typedef.h）なので負の値を持てない。仕様の `Bool` は C の int
 * 相当のつもりで書かれている。 */
IMPORT W gtst_pnt(GID gid, PNT p, PIXVAL *val);
IMPORT ERR gdra_pnt(GID gid, PNT p, PIXVAL val, DCM mode);
IMPORT ERR gfil_pnt(GID gid, PNT p, CONST PAT *pat, DCM mode);

/* 直線。 */
IMPORT ERR gdra_lin(GID gid, PNT p1, PNT p2, LATTR attr, CONST PAT *pat,
                    DCM mode);

/* 長方形。**線幅 > 1 の枠は内側に太る**（指定した枠の外へはみ出さない）。 */
IMPORT ERR gfra_rec(GID gid, RECT r, LATTR attr, CONST PAT *pat, DEG angle,
                    DCM mode);
IMPORT ERR gfil_rec(GID gid, RECT r, CONST PAT *pat, DEG angle, DCM mode);

/* ビットマップの転送。 */
IMPORT ERR gcop_bmp(GID srcid, CONST RECT *sr, GID dstid, CONST RECT *dr,
                    CONST GRGN *mask, DCM mode);

/* 長方形領域のスクロール。戻り値は塗り潰した矩形の数。 */
IMPORT W gscr_rec(GID gid, RECT r, W dh, W dv, RLIST *rlp, CONST PAT *pat);

/* ------------------------------------------------------------------ */
/* 文字(列)描画関数 (§2.5 の実装済み分)                                */
/* ------------------------------------------------------------------ */

IMPORT ERR gset_chp(GID gid, W x, W y, Bool abs);
IMPORT ERR gget_chp(GID gid, W *x, W *y);
IMPORT ERR gset_chc(GID gid, COLOR fgc, COLOR bgc);
IMPORT ERR gget_chc(GID gid, COLOR *fgc, COLOR *bgc);
IMPORT ERR gset_chd(GID gid, W dir, CONST CGAP *gap);
IMPORT W gget_chd(GID gid, CGAP *gap);

/* 文字 / 文字列の寸法。戻り値が幅・高さ。 */
IMPORT WERR gget_chw(GID gid, TC ch);
IMPORT WERR gget_chh(GID gid, TC ch);
IMPORT WERR gget_stw(GID gid, CONST TC *str, W len, CONST CGAP *gap, W *pos);
IMPORT WERR gget_sth(GID gid, CONST TC *str, W len, CONST CGAP *gap, W *pos);

/* 描画。描画後、文字描画位置は文字描画方向／文字間隔に従って更新される。 */
IMPORT WERR gdra_chr(GID gid, TC ch, DCM mode);
IMPORT WERR gdra_chp(GID gid, W x, W y, TC ch, DCM mode);
IMPORT WERR gdra_str(GID gid, CONST TC *str, W len, DCM mode);
IMPORT WERR gdra_stp(GID gid, W x, W y, CONST TC *str, W len, DCM mode);

/* ------------------------------------------------------------------ */
/* ポインタ制御 (§2.6)                                                 */
/*                                                                     */
/* ポインタは「スクリーン上の表示にオーバーラップする形で表示される」  */
/* 特殊な図形で、描画環境 (GID) に属さない**画面全体で 1 つ**の資源。   */
/* だから他の関数と違って gid を取らない。                             */
/* ------------------------------------------------------------------ */

typedef W PTRSTL; /* ポインタの形状 */

/* 標準ポインタ形状。≧0 がシステム定義、<0 がアプリ定義。 */
#define PS_SELECT 0 /* 選択指 */
#define PS_MODIFY 1 /* 修正選択指 */
#define PS_MOVE 2   /* 移動手 */
#define PS_VMOVE 3  /* 移動手（縦方向） */
#define PS_HMOVE 4  /* 移動手（横方向） */
#define PS_GRIP 5   /* 握り（移動ドラッグ中） */
#define PS_VGRIP 6
#define PS_HGRIP 7
#define PS_RSIZ 8 /* 変形手 */
#define PS_VRSIZ 9
#define PS_HRSIZ 10
#define PS_PICK 11 /* つまみ（変形ドラッグ中） */
#define PS_VPICK 12
#define PS_HPICK 13
#define PS_BUSY 14 /* 湯のみ（待ち状態） */
#define PS_MENU 15 /* プリメニュー */
#define PS_STD_COUNT 16

typedef struct PointerStatus {
	PNT pos;      /* 現在のポインタの座標位置（スクリーン絶対座標） */
	PTRSTL style; /* ポインタの形状 */
	COLOR fgcol;  /* 前景色 */
	COLOR bgcol;  /* 背景色 */
	W motion;     /* 追従状態 (0:非追従、1:追従) */
	W hidden;     /* 消去要求数。0 なら表示されている */
	W size;       /* ポインタサイズ（16, 24, 32 等） */
} PTRSTS;

#define MAX_PTR_SIZE 64 /* ポインタイメージデータの一辺の上限 */
#define P_SIZE ((MAX_PTR_SIZE * MAX_PTR_SIZE) / 8)

/* ポインタイメージ。
 *
 * data と mask の組で表示が決まる（仕様 §2.6.2）。
 *
 *   data mask  表示
 *    0    1    背景色
 *    1    1    前景色
 *    0    0    元のイメージのまま（透明）
 *    1    0    元のイメージの反転 (XOR)
 *
 * ビットマップとしての形は planes=1 / rowbytes=((n+15)÷16)×2 /
 * bounds=(0,0,n,n)。n はポインタサイズ。 */
typedef struct PointerImage {
	PNT hotpt; /* ホットポイント。イメージ長方形の左上を (0,0) とする */
	SIZE size; /* ポインタサイズ */
	UB data[P_SIZE];
	UB mask[P_SIZE];
} PTRIMG;

/* ------------------------------------------------------------------ */
/* ポインタの共有状態（BTRON3 の API ではない。本OS 内部の約束）        */
/* ------------------------------------------------------------------ */

/*
 * **ポインタは画面に 1 つしか無い。**
 *
 * ところがディスプレイプリミティブはプロセスごとのライブラリである
 * （詳細 E1）。素直に静的変数で持つと、プロセスごとに別のポインタ状態を
 * 持つことになり、次の形で壊れる。
 *
 *   - アプリが描く前に `gdsp_ptr(0)` で消しても、画面に出ている本物は
 *     消えない。描いた絵の上にポインタの残骸が出る
 *   - その `gdsp_ptr(1)` が**自分のプロセスが思っている位置**に
 *     ポインタを描く。動かしているのは別のプロセスなので、そこに
 *     描かれたポインタは誰も消さない。**これが画面の隅に残る
 *     「書き残し」の正体だった**（ウィンドウマネージャがドラッグの
 *     影を描くたびに、画面の左上にポインタを置いていた）
 *   - 退避した下地が別プロセスの描画で古くなり、動かしたときに
 *     古い絵が書き戻される
 *
 * だから状態は 1 か所に置き、全プロセスの DP が同じものを見る。置き場は
 * 画面と一緒にカーネルが用意して貸し出す（`Screen.ptr`）。
 *
 * **プロセスごとに違う仮想アドレスで見える。** 中に生ポインタを
 * 置いてはならない。
 */
#define PTR_SHARED_MAGIC 0x50545231U /* 'PTR1' */

typedef struct PointerShared {
	UW magic;
	W inited;   /* 初期値を入れたか */
	W seq;      /* 変わるたびに進む。合成する側が見る */
	PTRSTS sts; /* §2.6.3 の状態 */
	PTRIMG img; /* いまの形 */
	UW fgpix;   /* 前景色のピクセル値 */
	UW bgpix;   /* 背景色のピクセル値 */
} PtrShared;

/* カーネルが用意する置き場の大きさ。**カーネルは中身を知らなくてよい。**
 * 大きさだけを約束にして、解釈は DP に閉じる。
 * 中身が入り切らなくなったらビルドで落とす（器からはみ出した分を
 * 別プロセスが踏むと、原因の見えない壊れ方をする）。 */
#define PTR_SHARED_SIZE 32768

_Static_assert(sizeof(PtrShared) <= PTR_SHARED_SIZE,
               "PtrShared が貸し出し枠に入らない");

/* 初期状態（表示・style=0・既定色・追従）にしてPDの位置に表示する。 */
IMPORT ERR gini_ptr(void);

/* 消去 (req=0) / 表示 (req>0) / 強制表示 (req<0)。
 * 戻り値は結果としての消去要求数。0 なら表示されている。 */
IMPORT ERR gdsp_ptr(W req);

/* 形状と色。style<0 なら img が有効、style≧0 なら img は無視。
 * fgcol<0 / bgcol<0 はそれぞれ既定色の指定。 */
IMPORT ERR gset_ptr(PTRSTL style, PTRIMG *img, COLOR fgcol, COLOR bgcol);

/* 非追従状態のときだけ位置を動かす。戻り値は追従状態 (0:非追従/1:追従)。 */
IMPORT ERR gmov_ptr(PNT pos);

/* 追従 (sts≠0) / 非追従 (sts=0) の設定。 */
IMPORT ERR gmot_ptr(W sts);

/* 状態と形状の取り出し。戻り値はポインタのサイズ。 */
IMPORT W gget_ptr(PTRSTS *sts, PTRIMG *img);

/* ------------------------------------------------------------------ */
/* 以下は BTRON3 の API ではない。本OS 内部の口                         */
/* ------------------------------------------------------------------ */

/* 追従状態なら、ポインティングデバイスの現在位置へポインタを動かす。
 *
 * **誰かが周期的に呼ぶ必要がある。** 割込みハンドラから画面を書くのは
 * 避けたいので、ドライバではなくウィンドウマネージャの主ループが呼ぶ。
 * BTRON の「システムがポインタを動かす」をそこで実現している。
 *
 * **ここは位置を更新するだけで、画面には描かない**（詳細 E2）。
 * 描くのは合成する側（`gptr_paint`）である。 */
IMPORT void gptr_follow(void);

/* ポインタが占めている画面上の矩形。出ていなければ 0 を返す。
 * 合成する側が「動いたからそこを作り直す」判断に使う。 */
IMPORT W gptr_rect(RECT *r);

/* 変化の通し番号。位置・形・消去要求数が変わると進む。 */
IMPORT W gptr_seq(void);

/* **ポインタを画面に重ねる。合成する側が最後に呼ぶ。**
 *
 * 直描きの頃は下地を退避して、動くときに書き戻していた。合成なら下地は
 * いつでも作り直せるので、退避も書き戻しも要らない。§2.6.2 の 4 通り
 * （前景色 / 背景色 / 透明 / 反転）はここで解釈する。 */
IMPORT void gptr_paint(CONST RECT *area);

/* ポインタを**任意の描画環境へ**重ねる。合成を一時面で組むために要る
 * （画面へ直に書くと、下地を貼ってからポインタを載せるまでの間が
 * 見えてちらつく）。gid が 0 なら画面。 */
IMPORT void gptr_paint_to(GID gid, CONST RECT *area);

#endif /* __BTRON_DP_H__ */

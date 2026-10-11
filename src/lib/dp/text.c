/*
 * text.c — 文字(列)描画 (仕様 §2.5 の実装済み分)
 *
 * ------------------------------------------------------------------------
 * 字形はどこから来るか
 * ------------------------------------------------------------------------
 *
 * フォントマネージャが共有メモリに置いたグリフデータを**直接読む**。
 * 詳細 E1 が「フォントマネージャのグリフデータは読み出し専用共有メモリ」
 * と整理したとおりで、文字 1 個ごとに IPC を往復させない。
 * 問い合わせは `fontman_shared()` が 1 プロセス 1 回だけ行う。
 *
 * ------------------------------------------------------------------------
 * 文字描画カラー (§2.2.5)
 * ------------------------------------------------------------------------
 *
 * 文字イメージは単一プレーンのビットマップで、"1" が文字部分、"0" が
 * 背景部分。それぞれに色を与える。**どちらも透明にできる**ので、
 * 背景を透明にすれば下の絵の上に字を重ねられる。
 *
 * ------------------------------------------------------------------------
 * いま扱っていないもの
 * ------------------------------------------------------------------------
 *
 * TC は TRON 文字コードだが、変換表 (T2-01) がまだ無いので
 * **Unicode のコードポイントとして扱う**（font.c と同じ扱い。規律 R7/R8）。
 * 言語指定コードの処理（`gdra_chr` が言語切り替えで正の値を返す）も
 * 多国語ライブラリができてから。いまは常に 0 を返す。
 * フォント切替 (`gset_fon` / `gset_scr`) はフォントマネージャの本体待ち。
 */

#include "dpi.h"

EXPORT CONST FontShared *dp_font(void)
{
	return fontman_shared();
}

/* 文字の幅。半角 8 / 全角 16。無い文字は半角扱い（描かないが幅は進む）。 */
LOCAL UW char_width(CONST FontShared *fs, TC ch, CONST UB **glyph)
{
	UW w = 0;
	CONST UB *g;

	if (fs == 0) {
		if (glyph != 0) {
			*glyph = 0;
		}
		return 8;
	}
	g = fontman_glyph(fs, (UW)ch, &w);
	if (glyph != 0) {
		*glyph = g;
	}
	return (w != 0) ? w : 8;
}

/* ------------------------------------------------------------------ */
/* 描画環境レコードのアクセス (§2.5.1)                                 */
/* ------------------------------------------------------------------ */

EXPORT ERR gset_chp(GID gid, W x, W y, Bool abs)
{
	DEnv *e = dp_get(gid);
	W nx;
	W ny;

	if (e == 0) {
		return EG_GID;
	}
	/* 仕様: abs = 0 は相対指定（現在位置に加算）、abs != 0 は絶対指定。 */
	nx = (abs != 0) ? x : (e->chx + x);
	ny = (abs != 0) ? y : (e->chy + y);
	if (nx < DP_COORD_MIN || nx > DP_COORD_MAX || ny < DP_COORD_MIN ||
	    ny > DP_COORD_MAX) {
		return EG_PAR;
	}
	e->chx = nx;
	e->chy = ny;
	return EG_OK;
}

EXPORT ERR gget_chp(GID gid, W *x, W *y)
{
	DEnv *e = dp_get(gid);

	if (e == 0) {
		return EG_GID;
	}
	if (x == 0 || y == 0) {
		return EG_ADR;
	}
	*x = e->chx;
	*y = e->chy;
	return EG_OK;
}

EXPORT ERR gset_chc(GID gid, COLOR fgc, COLOR bgc)
{
	DEnv *e = dp_get(gid);

	if (e == 0) {
		return EG_GID;
	}
	e->chfgc = fgc;
	e->chbgc = bgc;
	return EG_OK;
}

EXPORT ERR gget_chc(GID gid, COLOR *fgc, COLOR *bgc)
{
	DEnv *e = dp_get(gid);

	if (e == 0) {
		return EG_GID;
	}
	if (fgc == 0 || bgc == 0) {
		return EG_ADR;
	}
	*fgc = e->chfgc;
	*bgc = e->chbgc;
	return EG_OK;
}

EXPORT ERR gset_chd(GID gid, W dir, CONST CGAP *gap)
{
	DEnv *e = dp_get(gid);

	if (e == 0) {
		return EG_GID;
	}
	if (dir < TORIGHT || dir > TOARB) {
		return EG_PAR;
	}
	if (dir == TOARB) {
		return EG_NOSPT; /* 任意の向きは未実装 */
	}
	e->chdir = dir;
	if (gap != 0) {
		e->chgap = *gap;
	}
	return EG_OK;
}

EXPORT W gget_chd(GID gid, CGAP *gap)
{
	DEnv *e = dp_get(gid);

	if (e == 0) {
		return EG_GID;
	}
	if (gap != 0) {
		*gap = e->chgap;
	}
	return e->chdir;
}

/* ------------------------------------------------------------------ */
/* 文字(列)チェック関数 (§2.5.2)                                       */
/* ------------------------------------------------------------------ */

EXPORT WERR gget_chw(GID gid, TC ch)
{
	DEnv *e = dp_get(gid);

	if (e == 0) {
		return EG_GID;
	}
	return (W)char_width(dp_font(), ch, 0);
}

EXPORT WERR gget_chh(GID gid, TC ch)
{
	DEnv *e = dp_get(gid);
	CONST FontShared *fs = dp_font();

	(void)ch;
	if (e == 0) {
		return EG_GID;
	}
	return (fs != 0) ? (W)fs->height : 16;
}

/* 文字間隔の符号付き値。dir != 0 なら描画方向の逆（重なる）。 */
LOCAL W gap_of(CONST SGAP *g)
{
	return (g->dir != 0) ? -(W)g->gap : (W)g->gap;
}

/* 文字列を 1 文字ずつ送ったときの累積量。pos には文字数が入る。 */
LOCAL W advance_str(CONST DEnv *e, CONST FontShared *fs, CONST TC *str, W len,
                    CONST CGAP *gap, Bool horiz, W *pos)
{
	W total = 0;
	W n = 0;
	W i;

	for (i = 0; i < len && str[i] != TNULL; i++) {
		if (horiz) {
			total += (W)char_width(fs, str[i], 0);
			total += gap_of(&gap->hgap);
			if (str[i] == (TC)' ') {
				total += gap_of(&gap->hspgap);
			}
		} else {
			total += (fs != 0) ? (W)fs->height : 16;
			total += gap_of(&gap->vgap);
			if (str[i] == (TC)' ') {
				total += gap_of(&gap->vspgap);
			}
		}
		n++;
	}
	(void)e;
	if (pos != 0) {
		*pos = n;
	}
	return total;
}

EXPORT WERR gget_stw(GID gid, CONST TC *str, W len, CONST CGAP *gap, W *pos)
{
	DEnv *e = dp_get(gid);

	if (e == 0) {
		return EG_GID;
	}
	if (str == 0) {
		return EG_ADR;
	}
	return advance_str(e, dp_font(), str, len,
	                   (gap != 0) ? gap : &e->chgap, True, pos);
}

EXPORT WERR gget_sth(GID gid, CONST TC *str, W len, CONST CGAP *gap, W *pos)
{
	DEnv *e = dp_get(gid);

	if (e == 0) {
		return EG_GID;
	}
	if (str == 0) {
		return EG_ADR;
	}
	return advance_str(e, dp_font(), str, len,
	                   (gap != 0) ? gap : &e->chgap, False, pos);
}

/* ------------------------------------------------------------------ */
/* 文字(列)の描画 (§2.5.3)                                             */
/* ------------------------------------------------------------------ */

/* 文字枠の左上を求める。
 *
 * 仕様「文字描画方向により、文字イメージ矩形(文字枠)のどの位置を
 * 文字描画位置とするかが一意的に決定される」。
 *
 *   TORIGHT  文字枠のベースラインの左端
 *   TOLEFT   文字枠のベースラインの右端
 *   TOUP     文字枠の左上
 *   TODOWN   文字枠の左下
 *
 * ベースラインは字形の下端から 1 行上に置く（16 ドットフォントの慣例で、
 * 下 1 行はディセンダのために空いている）。 */
LOCAL void frame_origin(CONST DEnv *e, UW w, UW h, W *x, W *y)
{
	W baseline = (W)h - 1;

	switch (e->chdir) {
	case TOLEFT:
		*x = e->chx - (W)w;
		*y = e->chy - baseline;
		return;
	case TOUP:
		*x = e->chx;
		*y = e->chy;
		return;
	case TODOWN:
		*x = e->chx;
		*y = e->chy - (W)h;
		return;
	default: /* TORIGHT */
		*x = e->chx;
		*y = e->chy - baseline;
		return;
	}
}

/* 1 文字描く。文字描画位置は更新しない。 */
LOCAL void draw_glyph(CONST DEnv *e, CONST RECT *clip, CONST UB *g, UW w,
                      UW h, W ox, W oy, DCM mode)
{
	UW bytes = (w + 7U) / 8U; /* 1 行のバイト数。8 なら 1、16 なら 2 */
	UW fg = 0;
	UW bg = 0;
	Bool has_fg = dp_color(e, e->chfgc, &fg);
	Bool has_bg = dp_color(e, e->chbgc, &bg);
	UW row;
	UW col;

	if (!has_fg && !has_bg) {
		return; /* 両方透明なら何も起きない */
	}
	for (row = 0; row < h; row++) {
		for (col = 0; col < w; col++) {
			Bool ink;

			if (g != 0) {
				UW b = g[row * bytes + (col >> 3)];

				ink = ((b >> (7U - (col & 7U))) & 1U) != 0 ? True : False;
			} else {
				ink = False; /* 字形が無い文字は背景だけ描く */
			}
			if (ink) {
				if (has_fg) {
					dp_plot(e, clip, ox + (W)col, oy + (W)row, fg, mode);
				}
			} else {
				if (has_bg) {
					dp_plot(e, clip, ox + (W)col, oy + (W)row, bg, mode);
				}
			}
		}
	}
}

/* 文字描画位置を 1 文字分進める。 */
LOCAL void advance_one(DEnv *e, TC ch, UW w, UW h)
{
	switch (e->chdir) {
	case TOLEFT:
		e->chx -= (W)w + gap_of(&e->chgap.hgap);
		if (ch == (TC)' ') {
			e->chx -= gap_of(&e->chgap.hspgap);
		}
		return;
	case TOUP:
		e->chy -= (W)h + gap_of(&e->chgap.vgap);
		if (ch == (TC)' ') {
			e->chy -= gap_of(&e->chgap.vspgap);
		}
		return;
	case TODOWN:
		e->chy += (W)h + gap_of(&e->chgap.vgap);
		if (ch == (TC)' ') {
			e->chy += gap_of(&e->chgap.vspgap);
		}
		return;
	default: /* TORIGHT */
		e->chx += (W)w + gap_of(&e->chgap.hgap);
		if (ch == (TC)' ') {
			e->chx += gap_of(&e->chgap.hspgap);
		}
		return;
	}
}

EXPORT WERR gdra_chr(GID gid, TC ch, DCM mode)
{
	DEnv *e = dp_get(gid);
	CONST FontShared *fs;
	CONST UB *g = 0;
	UW w;
	UW h;
	RECT clip;
	W ox;
	W oy;

	if (e == 0) {
		return EG_GID;
	}
	if (!dp_mode_ok(mode)) {
		return EG_PAR;
	}
	fs = dp_font();
	w = char_width(fs, ch, &g);
	h = (fs != 0) ? fs->height : 16;
	clip = dp_cliprect(e);
	frame_origin(e, w, h, &ox, &oy);
	draw_glyph(e, &clip, g, w, h, ox, oy, mode);
	advance_one(e, ch, w, h);
	/* 言語切り替えは未実装なので常に 0（切り替えは発生しなかった）。 */
	return 0;
}

EXPORT WERR gdra_chp(GID gid, W x, W y, TC ch, DCM mode)
{
	ERR er = gset_chp(gid, x, y, True);

	if (er != EG_OK) {
		return er;
	}
	return gdra_chr(gid, ch, mode);
}

EXPORT WERR gdra_str(GID gid, CONST TC *str, W len, DCM mode)
{
	DEnv *e = dp_get(gid);
	W i;

	if (e == 0) {
		return EG_GID;
	}
	if (str == 0) {
		return EG_ADR;
	}
	if (!dp_mode_ok(mode)) {
		return EG_PAR;
	}
	/* 仕様「str で指定した文字列が len で指定した値より短い場合は、
	 * そこまでの文字列が描画される」 */
	for (i = 0; i < len && str[i] != TNULL; i++) {
		WERR r = gdra_chr(gid, str[i], mode);

		if (r < 0) {
			return r;
		}
	}
	return 0;
}

EXPORT WERR gdra_stp(GID gid, W x, W y, CONST TC *str, W len, DCM mode)
{
	ERR er = gset_chp(gid, x, y, True);

	if (er != EG_OK) {
		return er;
	}
	return gdra_str(gid, str, len, mode);
}

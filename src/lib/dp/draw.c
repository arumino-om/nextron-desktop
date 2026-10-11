/*
 * draw.c — 図形描画 (仕様 §2.4 の実装済み分)
 *
 * 点・直線・長方形。外殻のウィンドウ枠とコンソールに必要な分である。
 *
 * ------------------------------------------------------------------------
 * 回転は未実装
 * ------------------------------------------------------------------------
 *
 * `gfil_rec` / `gfra_rec` は `DEG angle` を取り、長方形の左上を中心に
 * 反時計方向へ回転できる。回転した図形は走査線ごとの区間計算が必要で、
 * 楕円・弧と同じ仕掛けを共有する。そちらを実装するときに一緒に入れる。
 * いまは angle != 0 を `EG_NOSPT` で断る — 黙って回転しない方が危ない。
 */

#include "dpi.h"

/* ------------------------------------------------------------------ */
/* 点                                                                  */
/* ------------------------------------------------------------------ */

EXPORT W gtst_pnt(GID gid, PNT p, PIXVAL *val)
{
	DEnv *e = dp_get(gid);
	RECT clip;

	if (e == 0) {
		return EG_GID;
	}
	clip = dp_cliprect(e);
	if (!dp_rect_in(&clip, p.x, p.y)) {
		/* 仕様「点 p がクリッピング領域の外部にあり描画されない場合は
		 * 関数値 "1" を戻し、val には何も設定されない」 */
		return 1;
	}
	if (val == 0) {
		return EG_ADR;
	}
	*val = (PIXVAL)dp_pix_get(e, DP_ABS_X(e, p.x), DP_ABS_Y(e, p.y));
	return 0;
}

EXPORT ERR gdra_pnt(GID gid, PNT p, PIXVAL val, DCM mode)
{
	DEnv *e = dp_get(gid);
	RECT clip;

	if (e == 0) {
		return EG_GID;
	}
	if (!dp_mode_ok(mode)) {
		return EG_PAR;
	}
	clip = dp_cliprect(e);
	dp_plot(e, &clip, p.x, p.y, (UW)val, mode);
	return EG_OK;
}

EXPORT ERR gfil_pnt(GID gid, PNT p, CONST PAT *pat, DCM mode)
{
	DEnv *e = dp_get(gid);
	RECT clip;
	UW pix;
	ERR er;

	if (e == 0) {
		return EG_GID;
	}
	if (!dp_mode_ok(mode)) {
		return EG_PAR;
	}
	er = dp_pat_check(pat);
	if (er != EG_OK) {
		return er;
	}
	clip = dp_cliprect(e);
	if (!dp_rect_in(&clip, p.x, p.y)) {
		return EG_OK;
	}
	if (!dp_pat(e, pat, p.x, p.y, &pix)) {
		return EG_OK; /* 透明 */
	}
	dp_plot(e, &clip, p.x, p.y, pix, mode);
	return EG_OK;
}

/* ------------------------------------------------------------------ */
/* 長方形                                                              */
/* ------------------------------------------------------------------ */

/* 長方形をパターンで塗る。クリップは dp_plot が見る。 */
LOCAL void fill_rect(CONST DEnv *e, CONST RECT *clip, CONST RECT *r,
                     CONST PAT *pat, DCM mode)
{
	RECT a = dp_rect_isect(clip, r);
	W x;
	W y;

	if (dp_rect_empty(&a)) {
		return;
	}
	for (y = a.c.top; y < a.c.bottom; y++) {
		for (x = a.c.left; x < a.c.right; x++) {
			UW pix;

			if (!dp_pat(e, pat, x, y, &pix)) {
				continue; /* 透明 */
			}
			dp_plot(e, clip, x, y, pix, mode);
		}
	}
}

EXPORT ERR gfil_rec(GID gid, RECT r, CONST PAT *pat, DEG angle, DCM mode)
{
	DEnv *e = dp_get(gid);
	RECT clip;
	ERR er;

	if (e == 0) {
		return EG_GID;
	}
	if (angle != 0) {
		return EG_NOSPT; /* 回転は未実装 */
	}
	if (!dp_mode_ok(mode)) {
		return EG_PAR;
	}
	if (dp_rect_empty(&r)) {
		return EG_PAR; /* 仕様「r が空」は EG_PAR */
	}
	er = dp_pat_check(pat);
	if (er != EG_OK) {
		return er;
	}
	clip = dp_cliprect(e);
	fill_rect(e, &clip, &r, pat, mode);
	return EG_OK;
}

EXPORT ERR gfra_rec(GID gid, RECT r, LATTR attr, CONST PAT *pat, DEG angle,
                    DCM mode)
{
	DEnv *e = dp_get(gid);
	RECT clip;
	W w = (W)LATTR_WIDTH(attr);
	ERR er;

	if (e == 0) {
		return EG_GID;
	}
	if (angle != 0) {
		return EG_NOSPT;
	}
	if (LATTR_KIND(attr) != LN_SOLID) {
		return EG_NOSPT; /* 実線以外の線種は未実装 */
	}
	if (!dp_mode_ok(mode)) {
		return EG_PAR;
	}
	if (dp_rect_empty(&r)) {
		return EG_PAR;
	}
	er = dp_pat_check(pat);
	if (er != EG_OK) {
		return er;
	}
	if (w == 0) {
		w = 1; /* 線幅 0 は 1 として扱う */
	}
	clip = dp_cliprect(e);

	/* 仕様「線幅＞１の場合には枠は内側に線幅だけ太くなり、指定した枠の
	 * 外側にはみでることはない。線幅が大き過ぎて枠をはみでるような場合は、
	 * 枠の中を指定したパターンで塗り潰す。(エラーとはならない)」 */
	if (w * 2 >= (r.c.right - r.c.left) || w * 2 >= (r.c.bottom - r.c.top)) {
		fill_rect(e, &clip, &r, pat, mode);
		return EG_OK;
	}
	{
		RECT s;

		s = r;
		s.c.bottom = (H)(r.c.top + w); /* 上辺 */
		fill_rect(e, &clip, &s, pat, mode);
		s = r;
		s.c.top = (H)(r.c.bottom - w); /* 下辺 */
		fill_rect(e, &clip, &s, pat, mode);
		s = r;
		s.c.top = (H)(r.c.top + w);
		s.c.bottom = (H)(r.c.bottom - w);
		s.c.right = (H)(r.c.left + w); /* 左辺 */
		fill_rect(e, &clip, &s, pat, mode);
		s.c.left = (H)(r.c.right - w);
		s.c.right = r.c.right; /* 右辺 */
		fill_rect(e, &clip, &s, pat, mode);
	}
	return EG_OK;
}

/* ------------------------------------------------------------------ */
/* 直線                                                                */
/* ------------------------------------------------------------------ */

/* 線マスクパターンの位相。線幅 1 のときに描画対象となるピクセルを
 * "1" で示した周期的なマスク（仕様 §2.2.4）。既定は実線。 */
LOCAL Bool line_on(CONST DEnv *e, UW step)
{
	UW bits = e->lmask_bytes * 8U;
	UW i = step % bits;

	return ((e->lmask[i >> 3] >> (7U - (i & 7U))) & 1U) != 0 ? True : False;
}

/* 太い線は、各点を中心とした w×w の正方形で塗る。
 * 仕様は太線の端点形状を規定していないので、素直な方法を選ぶ。 */
LOCAL void line_dot(CONST DEnv *e, CONST RECT *clip, W x, W y, W w,
                    CONST PAT *pat, DCM mode)
{
	W half = w / 2;
	W dx;
	W dy;

	for (dy = 0; dy < w; dy++) {
		for (dx = 0; dx < w; dx++) {
			UW pix;
			W px = x + dx - half;
			W py = y + dy - half;

			if (!dp_rect_in(clip, px, py)) {
				continue;
			}
			if (!dp_pat(e, pat, px, py, &pix)) {
				continue;
			}
			dp_plot(e, clip, px, py, pix, mode);
		}
	}
}

EXPORT ERR gdra_lin(GID gid, PNT p1, PNT p2, LATTR attr, CONST PAT *pat,
                    DCM mode)
{
	DEnv *e = dp_get(gid);
	RECT clip;
	W w = (W)LATTR_WIDTH(attr);
	ERR er;
	W x0;
	W y0;
	W x1;
	W y1;
	W dx;
	W dy;
	W sx;
	W sy;
	W err;
	UW step = 0;

	if (e == 0) {
		return EG_GID;
	}
	if (LATTR_KIND(attr) != LN_SOLID && LATTR_KIND(attr) != LN_MASK) {
		return EG_NOSPT;
	}
	if (!dp_mode_ok(mode)) {
		return EG_PAR;
	}
	er = dp_pat_check(pat);
	if (er != EG_OK) {
		return er;
	}
	if (w == 0) {
		w = 1;
	}
	clip = dp_cliprect(e);

	/* Bresenham。整数座標系なので誤差項も整数で足りる。 */
	x0 = p1.x;
	y0 = p1.y;
	x1 = p2.x;
	y1 = p2.y;
	dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
	dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
	sx = (x0 < x1) ? 1 : -1;
	sy = (y0 < y1) ? 1 : -1;
	err = dx - dy;

	for (;;) {
		if (line_on(e, step)) {
			if (w == 1) {
				UW pix;

				if (dp_rect_in(&clip, x0, y0) &&
				    dp_pat(e, pat, x0, y0, &pix)) {
					dp_plot(e, &clip, x0, y0, pix, mode);
				}
			} else {
				line_dot(e, &clip, x0, y0, w, pat, mode);
			}
		}
		step++;
		if (x0 == x1 && y0 == y1) {
			break;
		}
		{
			W e2 = err * 2;

			if (e2 > -dy) {
				err -= dy;
				x0 += sx;
			}
			if (e2 < dx) {
				err += dx;
				y0 += sy;
			}
		}
	}
	return EG_OK;
}

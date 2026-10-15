/*
 * bitmap.c — ビットマップの転送とスクロール (仕様 §2.4.9)
 *
 * `gcop_bmp` と `gscr_rec`。ウィンドウの移動と、コンソールの行送りに要る。
 *
 * ------------------------------------------------------------------------
 * 重なりの扱い
 * ------------------------------------------------------------------------
 *
 * スクロールは**同じビットマップの中で領域が重なる**転送である。
 * 走査線の向きを間違えると、コピー元を上書きしながら読むことになって
 * 画像が引き伸ばされる。移動方向から走査の向きを決めるのが要点で、
 * `src/kernel/screen/fb.c` の `screen_scroll_up` が行単位で同じことを
 * している（あちらはデバイスメモリが非キャッシュなので行単位）。
 */

#include "dpi.h"

/* 1 ピクセル転送する。形式変換の有無で値の扱いが変わる。 */
LOCAL void copy_pixel(CONST DEnv *s, W sx, W sy, CONST DEnv *d,
                      CONST RECT *clip, W dx, W dy, DCM mode)
{
	UW v = dp_pix_get(s, DP_ABS_X(s, sx), DP_ABS_Y(s, sy));

	if (s->valid != d->valid) {
		/* 仕様「変換後の双方の下位 N ビットのピクセル値が等しくなるように
		 * 変換する。ここで N は、ソースとディスティネーションの depth の
		 * 最小値」。下位 N ビットを残すだけでよい。 */
		UW n = (s->valid < d->valid) ? s->valid : d->valid;

		v &= (n >= 32) ? 0xFFFFFFFFU : ((1U << n) - 1U);
	}
	dp_plot(d, clip, dx, dy, v, mode);
}

EXPORT ERR gcop_bmp(GID srcid, CONST RECT *sr, GID dstid, CONST RECT *dr,
                    CONST GRGN *mask, DCM mode)
{
	DEnv *s = dp_get(srcid);
	DEnv *d = dp_get(dstid);
	RECT sarea;
	RECT darea;
	RECT clip;
	W w;
	W h;
	W y;

	if (s == 0 || d == 0) {
		return EG_GID;
	}
	if (!dp_mode_ok(mode)) {
		return EG_PAR;
	}
	if (mask != 0 && mask->ns != 0) {
		return EG_NOSPT; /* 任意領域のマスクは未実装 */
	}
	sarea = (sr != 0) ? *sr : s->bmp.bounds;
	darea = (dr != 0) ? *dr : d->bmp.bounds;
	if (dp_rect_empty(&sarea) || dp_rect_empty(&darea)) {
		return EG_PAR;
	}
	/* 形式が違うとき、変換指定が無ければエラー（仕様 §2.4 の前書き）。 */
	if (s->valid != d->valid && (mode & G_CVFORM) == 0) {
		return EG_FORM;
	}
	w = sarea.c.right - sarea.c.left;
	h = sarea.c.bottom - sarea.c.top;
	if (darea.c.right - darea.c.left < w) {
		w = darea.c.right - darea.c.left;
	}
	if (darea.c.bottom - darea.c.top < h) {
		h = darea.c.bottom - darea.c.top;
	}
	clip = dp_cliprect(d);
	if (mask != 0) {
		clip = dp_rect_isect(&clip, &mask->r);
	}

	/* 同じビットマップで領域が重なるときは走査の向きを選ぶ。 */
	{
		Bool same = (s->base == d->base) ? True : False;
		Bool back = False;
		Bool fast;

		if (same && (darea.c.top > sarea.c.top ||
		             (darea.c.top == sarea.c.top &&
		              darea.c.left > sarea.c.left))) {
			back = True;
		}
		/* 形式が同じで上書きなら、走査線ごとにまとめて運べる。
		 * 二重緩衝（裏画面を作って一度に送る）はこの経路の速さが
		 * そのまま効くので、ここを 1 画素ずつにはできない。 */
		fast = (dp_fast_ok(d, mode) && s->valid == d->valid &&
		        s->bound == d->bound)
		           ? True
		           : False;
		for (y = 0; y < h; y++) {
			W sy = back ? (h - 1 - y) : y;
			W x;

			if (fast) {
				W x0 = darea.c.left;
				W x1 = darea.c.left + w;
				W dy = darea.c.top + sy;
				W rs;
				W re;

				if (dy < clip.c.top || dy >= clip.c.bottom) {
					continue;
				}
				if (x0 < clip.c.left) {
					x0 = clip.c.left;
				}
				if (x1 > clip.c.right) {
					x1 = clip.c.right;
				}
				x = x0;
				while (dp_next_run(d, dy, x1, &x, &rs, &re)) {
					dp_copy_run(s,
					            sarea.c.left + (rs - darea.c.left),
					            sarea.c.top + sy, d, rs, re, dy, back);
				}
				continue;
			}
			for (x = 0; x < w; x++) {
				W sxx = back ? (w - 1 - x) : x;

				copy_pixel(s, sarea.c.left + sxx, sarea.c.top + sy, d,
				           &clip, darea.c.left + sxx, darea.c.top + sy,
				           mode);
			}
		}
	}
	return EG_OK;
}

/* ------------------------------------------------------------------ */
/* スクロール                                                          */
/* ------------------------------------------------------------------ */

EXPORT W gscr_rec(GID gid, RECT r, W dh, W dv, RLIST *rlp, CONST PAT *pat)
{
	DEnv *e = dp_get(gid);
	RECT clip;
	RECT area;
	RECT src;
	RECT dst;
	RECT gap[2]; /* 隙間は上下の帯 1 本 + 左右の帯 1 本の最大 2 つ */
	W ng = 0;
	RLIST *p;
	W i;
	ERR er;

	if (e == 0) {
		return EG_GID;
	}
	if (dp_rect_empty(&r)) {
		return EG_PAR;
	}
	if (pat != 0) {
		er = dp_pat_check(pat);
		if (er != EG_OK) {
			return er;
		}
	}
	clip = dp_cliprect(e);
	area = dp_rect_isect(&clip, &r);
	if (dp_rect_empty(&area)) {
		return 0;
	}

	/* 移動元 = area を (-dh,-dv) した部分のうち area に収まるところ。
	 * 移動先 = それを (dh,dv) した位置。 */
	src = area;
	src.c.left = (H)(area.c.left - dh);
	src.c.right = (H)(area.c.right - dh);
	src.c.top = (H)(area.c.top - dv);
	src.c.bottom = (H)(area.c.bottom - dv);
	src = dp_rect_isect(&src, &area);
	if (!dp_rect_empty(&src)) {
		dst = src;
		dst.c.left = (H)(src.c.left + dh);
		dst.c.right = (H)(src.c.right + dh);
		dst.c.top = (H)(src.c.top + dv);
		dst.c.bottom = (H)(src.c.bottom + dv);
		(void)gcop_bmp(gid, &src, gid, &dst, 0, G_STORE);
	}

	/* 隙間を求める。仕様は「水平線分解した矩形」で、垂直座標の小さい順に
	 * 並べることを要求しているので、**上下の帯を先に全幅で取り、
	 * 左右の帯は残った高さだけ**にする。重複しない分解になる。 */
	{
		W ytop = area.c.top;
		W ybot = area.c.bottom;
		RECT g;

		if (dv > 0) {
			g = area;
			g.c.bottom = (H)((area.c.top + dv < ybot) ? (area.c.top + dv)
			                                          : ybot);
			ytop = g.c.bottom;
			if (!dp_rect_empty(&g)) {
				gap[ng++] = g;
			}
		} else if (dv < 0) {
			g = area;
			g.c.top = (H)((area.c.bottom + dv > ytop) ? (area.c.bottom + dv)
			                                          : ytop);
			ybot = g.c.top;
			if (!dp_rect_empty(&g)) {
				gap[ng++] = g;
			}
		}
		if (dh != 0) {
			g = area;
			g.c.top = (H)ytop;
			g.c.bottom = (H)ybot;
			if (dh > 0) {
				g.c.right = (H)((area.c.left + dh < area.c.right)
				                    ? (area.c.left + dh)
				                    : area.c.right);
			} else {
				g.c.left = (H)((area.c.right + dh > area.c.left)
				                   ? (area.c.right + dh)
				                   : area.c.left);
			}
			if (!dp_rect_empty(&g)) {
				gap[ng++] = g;
			}
		}
		/* dv < 0 のときは下の帯が後ろに来るので、垂直座標の小さい順に直す。 */
		if (ng == 2 && gap[0].c.top > gap[1].c.top) {
			g = gap[0];
			gap[0] = gap[1];
			gap[1] = g;
		}
	}

	/* 仕様「rlp が NULL でない場合は、その矩形領域のリストが設定される。
	 * rlp の要素が不足する場合は、rlp の要素分のみ設定される」 */
	p = rlp;
	for (i = 0; i < ng && p != 0; i++) {
		p->comp = gap[i];
		p = p->next;
	}
	/* 仕様「pat=NULL の場合は塗りつぶしは行わないが、矩形リストは
	 * rlp に設定される」 */
	if (pat != 0) {
		for (i = 0; i < ng; i++) {
			(void)gfil_rec(gid, gap[i], pat, 0, G_STORE);
		}
	}
	return ng;
}

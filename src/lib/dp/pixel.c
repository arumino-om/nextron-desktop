/*
 * pixel.c — ピクセルの読み書き・描画演算・カラー変換・描画パターン
 *
 * ディスプレイプリミティブの最下層。ここだけがビットマップのメモリ配置を
 * 知っている。
 *
 * ------------------------------------------------------------------------
 * ビットの並び（仕様 §2.2.2 の解釈）
 * ------------------------------------------------------------------------
 *
 * 仕様は「メモリ上の低位アドレスのバイトの MSB が左上のピクセルに対応する。
 * アドレス順に右側に対応し…」と書いている。これは**ピクセルの並び順**の
 * 規定であって、1 ピクセルが複数バイトを占める場合の**値のバイト順**までは
 * 規定していない。
 *
 * 本OS は A4 の決定どおり内部をリトルエンディアンとし、
 *
 *   境界ビット数 8 / 16 / 32  ネイティブ整数として置く
 *   境界ビット数 1 / 2 / 4    バイト内で MSB 側から詰める（仕様どおり）
 *
 * とする。UEFI GOP のフレームバッファがリトルエンディアンの 32 ビット語
 * なので、16/32 ビットをビッグエンディアンで置くと画面に出した瞬間に
 * 色が入れ替わる。サブバイトのピクセルには並び順しか無いので仕様どおり。
 */

#include "dpi.h"

EXPORT DEnv dp_env[DP_MAX_GID + 1];

EXPORT DEnv *dp_get(GID gid)
{
	if (gid < 1 || gid > DP_MAX_GID || !dp_env[gid].used) {
		return 0;
	}
	return &dp_env[gid];
}

/* ------------------------------------------------------------------ */
/* 長方形 (§2.2.1 half-open property)                                  */
/* ------------------------------------------------------------------ */

EXPORT Bool dp_rect_empty(CONST RECT *r)
{
	/* 仕様「top >= bottom または left >= right の場合は、その長方形の
	 * 領域は空である」 */
	if (r->c.top >= r->c.bottom || r->c.left >= r->c.right) {
		return True;
	}
	return False;
}

EXPORT RECT dp_rect_isect(CONST RECT *a, CONST RECT *b)
{
	RECT r;

	r.c.left = (a->c.left > b->c.left) ? a->c.left : b->c.left;
	r.c.top = (a->c.top > b->c.top) ? a->c.top : b->c.top;
	r.c.right = (a->c.right < b->c.right) ? a->c.right : b->c.right;
	r.c.bottom = (a->c.bottom < b->c.bottom) ? a->c.bottom : b->c.bottom;
	return r;
}

EXPORT Bool dp_rect_in(CONST RECT *r, W x, W y)
{
	/* 仕様「点(lefttop)は長方形に含まれるが、点(rightbot)は含まれない」 */
	if (x < r->c.left || x >= r->c.right) {
		return False;
	}
	if (y < r->c.top || y >= r->c.bottom) {
		return False;
	}
	return True;
}

EXPORT RECT dp_cliprect(CONST DEnv *e)
{
	/* 仕様「実際に対象ビットマップ上に描画が行なわれる領域は、対象
	 * ビットマップの境界長方形、フレーム長方形、表示長方形、設定された
	 * クリッピング領域に含まれ、かつ、前置長方形リストに含まれない領域」。
	 * 前置長方形リストは未実装。 */
	RECT r = dp_rect_isect(&e->bmp.bounds, &e->frame);

	r = dp_rect_isect(&r, &e->visible);
	if (e->has_clip) {
		r = dp_rect_isect(&r, &e->clip);
	}
	return r;
}

/* ------------------------------------------------------------------ */
/* ピクセルの読み書き                                                  */
/* ------------------------------------------------------------------ */

LOCAL UB *row_of(CONST DEnv *e, W ay)
{
	return e->base + (UD)(UW)ay * e->bmp.rowbytes;
}

EXPORT UW dp_pix_get(CONST DEnv *e, W ax, W ay)
{
	UB *p = row_of(e, ay);

	switch (e->bound) {
	case 32:
		return ((CONST UW *)(CONST VP)p)[ax] & e->vmask;
	case 16:
		return (UW)((CONST UH *)(CONST VP)p)[ax] & e->vmask;
	case 8:
		return (UW)p[ax] & e->vmask;
	default: {
		/* サブバイト。バイト内は MSB 側が左（仕様）。 */
		UW per = 8U / e->bound;
		UW idx = (UW)ax / per;
		UW slot = (UW)ax % per;
		UW shift = 8U - e->bound * (slot + 1U);

		return ((UW)p[idx] >> shift) & e->vmask;
	}
	}
}

EXPORT void dp_pix_put(CONST DEnv *e, W ax, W ay, UW val)
{
	UB *p = row_of(e, ay);

	val &= e->vmask;
	switch (e->bound) {
	case 32:
		((UW *)(VP)p)[ax] = val;
		return;
	case 16:
		((UH *)(VP)p)[ax] = (UH)val;
		return;
	case 8:
		p[ax] = (UB)val;
		return;
	default: {
		UW per = 8U / e->bound;
		UW idx = (UW)ax / per;
		UW slot = (UW)ax % per;
		UW shift = 8U - e->bound * (slot + 1U);
		UW m = e->vmask << shift;

		p[idx] = (UB)(((UW)p[idx] & ~m) | ((val << shift) & m));
		return;
	}
	}
}

/* ------------------------------------------------------------------ */
/* 描画演算モード (§2.2.4)                                             */
/* ------------------------------------------------------------------ */

EXPORT UW dp_dcm(W op, UW src, UW dest, UW vmask)
{
	UW n = (~src) & vmask;

	switch (op) {
	case G_STORE:
		return src & vmask;
	case G_XOR:
		return (src ^ dest) & vmask;
	case G_OR:
		return (src | dest) & vmask;
	case G_AND:
		return (src & dest) & vmask;
	case G_CPYN:
		return n;
	case G_XORN:
		return (n ^ dest) & vmask;
	case G_ORN:
		return (n | dest) & vmask;
	case G_ANDN:
		return (n & dest) & vmask;
	default: /* G_NOP */
		return dest & vmask;
	}
}

EXPORT Bool dp_maskok(CONST DEnv *e, DCM mode, UW dest)
{
	/* 仕様: G_MASK はマスクピクセル値と同一の dest のみ、
	 * G_MASKN は異なる dest のみを描画対象とする。 */
	if ((mode & G_MASK) != 0) {
		return (dest == ((UW)e->maskpix & e->vmask)) ? True : False;
	}
	if ((mode & G_MASKN) != 0) {
		return (dest != ((UW)e->maskpix & e->vmask)) ? True : False;
	}
	return True;
}

EXPORT Bool dp_mode_ok(DCM mode)
{
	W op = mode & G_OPMASK;

	if (op < G_STORE || op > G_NOP) {
		return False;
	}
	if ((mode & ~(DCM)(G_OPMASK | G_CVFORM | G_CVCOLOR | G_MASK | G_MASKN)) !=
	    0) {
		return False;
	}
	/* G_MASK と G_MASKN の同時指定は意味を持たない */
	if ((mode & G_MASK) != 0 && (mode & G_MASKN) != 0) {
		return False;
	}
	return True;
}

EXPORT Bool dp_in_fore(CONST DEnv *e, W rx, W ry)
{
	W i;

	for (i = 0; i < e->nfore; i++) {
		if (dp_rect_in(&e->fore[i], rx, ry)) {
			return True;
		}
	}
	return False;
}

EXPORT void dp_plot(CONST DEnv *e, CONST RECT *clip, W rx, W ry, UW src,
                    DCM mode)
{
	W ax;
	W ay;
	UW dest;

	if (!dp_rect_in(clip, rx, ry)) {
		return;
	}
	/* 仕様 §2.2.4: 前置長方形リストに含まれる領域は描画対象から外れる。 */
	if (e->nfore > 0 && dp_in_fore(e, rx, ry)) {
		return;
	}
	ax = DP_ABS_X(e, rx);
	ay = DP_ABS_Y(e, ry);
	dest = dp_pix_get(e, ax, ay);
	if (!dp_maskok(e, mode, dest)) {
		return;
	}
	dp_pix_put(e, ax, ay, dp_dcm(mode & G_OPMASK, src, dest, e->vmask));
}

/* ------------------------------------------------------------------ */
/* カラー変換 (§2.2.3)                                                 */
/* ------------------------------------------------------------------ */

/* 絶対 RGB をデバイスのビット配置に当てはめる。
 * color[] は「下位バイトが階調ビット数、上位バイトがビット位置」。 */
LOCAL UW rgb_to_pix(CONST H *info, UW r, UW g, UW b)
{
	CONST UW v[3] = {r, g, b};
	UW pix = 0;
	W i;

	for (i = 0; i < 3; i++) {
		UW bits = DEV_CBITS(info[i]);
		UW pos = DEV_CPOS(info[i]);

		if (bits == 0 || bits > 8) {
			continue;
		}
		/* 0〜255 の正規化値を bits ビットへ落とす。 */
		pix |= ((v[i] >> (8U - bits)) & ((1U << bits) - 1U)) << pos;
	}
	return pix;
}

EXPORT Bool dp_color(CONST DEnv *e, COLOR c, UW *pix)
{
	UW crep;

	/* 仕様「透明を指定した場合は実際には描画を行なわないことを意味し、
	 * 下位 31 ビットの値は無視される」 */
	if (COLOR_IS_TRANS(c)) {
		return False;
	}
	crep = COLOR_CREP(c);
	if (crep == COL_PIXVAL) {
		/* 仕様「COLOR の下位 N ビットをそのままピクセル値とし、上位
		 * ビットは無視する」 */
		*pix = COLOR_CVAL(c) & e->vmask;
		return True;
	}
	if (crep != COL_RGB) {
		/* crep = 2〜7 は予約。ピクセル値として扱う方が安全。 */
		*pix = COLOR_CVAL(c) & e->vmask;
		return True;
	}
	if (!e->has_cspec || ((UW)e->cspec.attr & DEV_CMAP) != 0) {
		/* 仕様「カラー情報を持たない汎用メモリ描画環境の場合には…
		 * 単にピクセルの深さでマスクを取った値をピクセル値として処理される」。
		 * カラーマップ方式は未実装なので同じ扱いにする。 */
		*pix = COLOR_CVAL(c) & e->vmask;
		return True;
	}
	*pix = rgb_to_pix(e->cspec.info, (COLOR_CVAL(c) >> 16) & 0xFFU,
	                  (COLOR_CVAL(c) >> 8) & 0xFFU, COLOR_CVAL(c) & 0xFFU) &
	       e->vmask;
	return True;
}

/* ------------------------------------------------------------------ */
/* 描画パターン (§2.2.4)                                               */
/* ------------------------------------------------------------------ */

/* 標準パターンマスク / 標準パターンは 1〜7 の小さな整数で表される。
 * ポインタとして渡されるので、デリファレンスの前に判別する。 */
LOCAL Bool is_std(CONST VP p, UW *n)
{
	UD v = (UD)(CONST VP)p;

	if (v >= 1 && v <= 7) {
		*n = (UW)v;
		return True;
	}
	return False;
}

/* 8x8 の順序ディザ閾値。標準パターンマスクを「パターンのサイズに
 * 無関係に」作るために使う（仕様がサイズ非依存と規定している）。
 * 値が小さいほど薄い濃度で点が立つ。 */
LOCAL CONST UB bayer8[64] = {
	0,  32, 8,  40, 2,  34, 10, 42, 48, 16, 56, 24, 50, 18, 58, 26,
	12, 44, 4,  36, 14, 46, 6,  38, 60, 28, 52, 20, 62, 30, 54, 22,
	3,  35, 11, 43, 1,  33, 9,  41, 51, 19, 59, 27, 49, 17, 57, 25,
	15, 47, 7,  39, 13, 45, 5,  37, 63, 31, 55, 23, 61, 29, 53, 21};

/* 標準マスクの濃度。_FILL0..._FILL100 に対応（0 / 12.5 / 25 / 50 / 75 /
 * 87.5 / 100 %）。64 分率で持つ。 */
LOCAL CONST UB std_level[8] = {0, 0, 8, 16, 32, 48, 56, 64};

/* 正の剰余。負の座標でもパターンの位相が揃うようにする。 */
LOCAL W pmod(W v, W m)
{
	W r;

	if (m <= 0) {
		return 0;
	}
	r = v % m;
	return (r < 0) ? (r + m) : r;
}

LOCAL Bool std_mask_bit(UW n, W x, W y)
{
	UW lv = std_level[n & 7U];

	if (lv == 0) {
		return False;
	}
	if (lv >= 64) {
		return True;
	}
	return (bayer8[((UW)(y & 7) * 8U) + (UW)(x & 7)] < lv) ? True : False;
}

/* 利用者が用意したマスクデータのビット。
 *
 * 仕様「パターンサイズで示される長方形が入る最小のビットマップデータ
 * (ピクセルビット数＝1)」。**行はバイト境界で始まる**ものとして扱う
 * （「最小」の素直な読み方。BMP の rowbytes 偶数規則はここには掛からない）。
 * バイト内は MSB が左。 */
LOCAL Bool mask_bit(CONST UB *mask, UW hsize, W x, W y)
{
	UW stride = (hsize + 7U) / 8U;
	UW idx = (UW)y * stride + ((UW)x >> 3);

	return ((mask[idx] >> (7U - ((UW)x & 7U))) & 1U) != 0 ? True : False;
}

EXPORT ERR dp_pat_check(CONST PAT *pat)
{
	UW n;

	if (pat == 0) {
		return EG_PAR;
	}
	if (is_std((CONST VP)pat, &n)) {
		return EG_OK; /* 標準パターン */
	}
	switch (pat->spat.kind) {
	case PAT_SPAT:
		if (pat->spat.hsize == 0 || pat->spat.vsize == 0) {
			return EG_PAR;
		}
		/* 仕様「spat 形式の場合は、NULL であってはいけない」 */
		if (pat->spat.mask == 0) {
			return EG_PAR;
		}
		return EG_OK;
	case PAT_MPAT:
		if (pat->mpat.hsize == 0 || pat->mpat.vsize == 0 ||
		    pat->mpat.bmap == 0) {
			return EG_PAR;
		}
		return EG_OK;
	case PAT_IPAT:
		/* 内部パターンイメージは未実装 */
		return EG_NOSPT;
	default:
		/* 仕様「=3〜: 予約 (指定した場合は、EG_NOSPT のエラーとなる)」 */
		return EG_NOSPT;
	}
}

EXPORT Bool dp_pat(CONST DEnv *e, CONST PAT *pat, W rx, W ry, UW *pix)
{
	UW n;
	W px;
	W py;

	/* 仕様「描画対象のビットマップの境界長方形(bounds)の原点(0,0) に
	 * 描画パターンデータの左上の点を揃えて、規則正しくタイルを詰める」。
	 * 原点は**相対座標の (0,0)** なので相対座標で位相を取る。 */
	if (is_std((CONST VP)pat, &n)) {
		/* 標準パターン: 前景は黒、背景は白。 */
		Bool ink = std_mask_bit(n, rx, ry);

		return dp_color(e, ink ? COL_BLACK : COL_WHITE, pix);
	}
	switch (pat->spat.kind) {
	case PAT_SPAT: {
		Bool ink;

		px = pmod(rx, (W)pat->spat.hsize);
		py = pmod(ry, (W)pat->spat.vsize);
		if (is_std((CONST VP)pat->spat.mask, &n)) {
			ink = std_mask_bit(n, px, py);
		} else {
			ink = mask_bit(pat->spat.mask, pat->spat.hsize, px, py);
		}
		return dp_color(e, ink ? pat->spat.fgcol : pat->spat.bgcol, pix);
	}
	case PAT_MPAT: {
		CONST BMP *b = pat->mpat.bmap;
		DEnv src;
		UW bound = BMP_BOUNDBITS(b->pixbits);

		px = pmod(rx, (W)pat->mpat.hsize);
		py = pmod(ry, (W)pat->mpat.vsize);
		if (pat->mpat.mask != 0) {
			/* 仕様「"0" の部分は描画を行なわず、透明とする」 */
			if (!mask_bit(pat->mpat.mask, pat->mpat.hsize, px, py)) {
				return False;
			}
		}
		/* ビットマップから 1 ピクセル読むために最小限の環境を組む。 */
		src.base = b->baseaddr[0];
		src.bmp = *b;
		src.bound = bound;
		src.valid = BMP_VALIDBITS(b->pixbits);
		src.vmask = (src.valid >= 32) ? 0xFFFFFFFFU
		                              : ((1U << src.valid) - 1U);
		if (src.base == 0) {
			return False;
		}
		*pix = dp_pix_get(&src, px, py) & e->vmask;
		return True;
	}
	default:
		return False;
	}
}

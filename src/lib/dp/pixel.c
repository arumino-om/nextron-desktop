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

EXPORT void dp_mark(CONST DEnv *e, W ax0, W ay0, W ax1, W ay1)
{
	DMGREC *d = e->dmg;

	if (d == 0 || ax1 <= ax0 || ay1 <= ay0) {
		return;
	}
	if (d->any == 0) {
		d->r.c.left = (H)ax0;
		d->r.c.top = (H)ay0;
		d->r.c.right = (H)ax1;
		d->r.c.bottom = (H)ay1;
		d->any = 1;
	} else {
		if (ax0 < d->r.c.left) {
			d->r.c.left = (H)ax0;
		}
		if (ay0 < d->r.c.top) {
			d->r.c.top = (H)ay0;
		}
		if (ax1 > d->r.c.right) {
			d->r.c.right = (H)ax1;
		}
		if (ay1 > d->r.c.bottom) {
			d->r.c.bottom = (H)ay1;
		}
	}
	d->seq++;
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
	dp_mark(e, ax, ay, ax + 1, ay + 1);
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

/* ------------------------------------------------------------------ */
/* 走査線単位の高速経路                                                */
/* ------------------------------------------------------------------ */

/*
 * dp_plot は 1 ピクセルごとにクリップ判定・前置長方形の走査・行頭
 * アドレスの計算・読み出し・マスク判定をやり直す。1 点を打つには
 * これでよいが、面を塗ると同じ判定を全ピクセルで繰り返すことになる。
 * 800x600 を 1 回塗ると 48 万回で、実測 238ms かかっていた。画面の
 * 塗り替えがこの速さだと、走査が追い越してちらつきとして見える。
 *
 * そこで**行単位で判定をまとめる**。1 走査線について
 *
 *   - パターンは横方向に周期 period で繰り返すので 1 周期だけ解く
 *   - 前置長方形で切った区間を求める
 *   - 区間の中はもう判定が要らないので、素直な代入で書く
 *
 * とすれば、内側のループは添字の増加と代入だけになる。
 *
 * この経路に乗せられるのは「上書き (G_STORE) で、マスクピクセル指定が
 * 無く、境界ビット数がバイトの整数倍」のときだけである。それ以外は
 * 読み出しや部分書き換えが必要なので dp_plot に落ちる。塗り潰しの
 * ほとんどは G_STORE なので、これで足りる。
 */

EXPORT Bool dp_fast_ok(CONST DEnv *e, DCM mode)
{
	if ((mode & G_OPMASK) != G_STORE) {
		return False; /* dest を読む必要がある */
	}
	if ((mode & (G_MASK | G_MASKN)) != 0) {
		return False; /* dest ごとに描くか決める必要がある */
	}
	if (e->bound != 8 && e->bound != 16 && e->bound != 32) {
		return False; /* サブバイトは読んで部分書き換えになる */
	}
	return True;
}

/* パターンの横方向の周期。0 なら行単位に解けない。 */
LOCAL W pat_period(CONST PAT *pat)
{
	UW n;
	UW h;

	if (is_std((CONST VP)pat, &n)) {
		return 8; /* 標準パターンは 8x8 のディザ */
	}
	switch (pat->spat.kind) {
	case PAT_SPAT:
		h = pat->spat.hsize;
		break;
	case PAT_MPAT:
		h = pat->mpat.hsize;
		break;
	default:
		return 0;
	}
	if (h < 1 || h > DP_PAT_PERIOD) {
		return 0;
	}
	return (W)h;
}

EXPORT Bool dp_pat_row(CONST DEnv *e, CONST PAT *pat, W ry, PatRow *pr)
{
	W p = pat_period(pat);
	W i;

	if (p <= 0) {
		return False;
	}
	pr->period = p;
	pr->solid = True;
	for (i = 0; i < p; i++) {
		UW pix = 0;
		Bool op = dp_pat(e, pat, i, ry, &pix);

		pr->pix[i] = pix;
		pr->opaque[i] = (UB)(op ? 1 : 0);
		if (!op || pix != pr->pix[0] || pr->opaque[0] == 0) {
			pr->solid = False;
		}
	}
	return True;
}

EXPORT Bool dp_next_run(CONST DEnv *e, W ry, W x1, W *x, W *rs, W *re)
{
	W s = *x;
	W t = x1;
	W i;

	/* s が前置長方形の内側なら、その右端まで飛ぶ。飛んだ先がまた
	 * 別の長方形の内側かもしれないので、動かなくなるまで繰り返す。 */
	for (;;) {
		Bool hit = False;

		if (s >= x1) {
			return False;
		}
		for (i = 0; i < e->nfore; i++) {
			CONST RECT *f = &e->fore[i];

			if (ry >= f->c.top && ry < f->c.bottom && s >= f->c.left &&
			    s < f->c.right) {
				s = f->c.right;
				hit = True;
				break;
			}
		}
		if (!hit) {
			break;
		}
	}
	/* s から次の前置長方形の左端までが塗れる。 */
	for (i = 0; i < e->nfore; i++) {
		CONST RECT *f = &e->fore[i];

		if (ry >= f->c.top && ry < f->c.bottom && f->c.left > s &&
		    f->c.left < t) {
			t = f->c.left;
		}
	}
	*rs = s;
	*re = t;
	*x = t;
	return True;
}

EXPORT void dp_run(CONST DEnv *e, CONST PatRow *pr, W rx0, W rx1, W ry)
{
	W ay = DP_ABS_Y(e, ry);
	UB *row = row_of(e, ay);
	W a0 = DP_ABS_X(e, rx0);
	W a1 = DP_ABS_X(e, rx1);
	W i = pmod(rx0, pr->period);
	W a;

	dp_mark(e, a0, ay, a1, ay + 1);

	if (pr->solid) {
		UW v = pr->pix[0];

		switch (e->bound) {
		case 32: {
			UW *p = (UW *)(VP)row;

			for (a = a0; a < a1; a++) {
				p[a] = v;
			}
			return;
		}
		case 16: {
			UH *p = (UH *)(VP)row;

			for (a = a0; a < a1; a++) {
				p[a] = (UH)v;
			}
			return;
		}
		default:
			for (a = a0; a < a1; a++) {
				row[a] = (UB)v;
			}
			return;
		}
	}
	switch (e->bound) {
	case 32: {
		UW *p = (UW *)(VP)row;

		for (a = a0; a < a1; a++) {
			if (pr->opaque[i] != 0) {
				p[a] = pr->pix[i];
			}
			i = (i + 1 < pr->period) ? (i + 1) : 0;
		}
		return;
	}
	case 16: {
		UH *p = (UH *)(VP)row;

		for (a = a0; a < a1; a++) {
			if (pr->opaque[i] != 0) {
				p[a] = (UH)pr->pix[i];
			}
			i = (i + 1 < pr->period) ? (i + 1) : 0;
		}
		return;
	}
	default:
		for (a = a0; a < a1; a++) {
			if (pr->opaque[i] != 0) {
				row[a] = (UB)pr->pix[i];
			}
			i = (i + 1 < pr->period) ? (i + 1) : 0;
		}
		return;
	}
}

EXPORT void dp_hline(CONST DEnv *e, CONST RECT *clip, W rx0, W rx1, W ry,
                     UW src, DCM mode)
{
	PatRow pr;
	W x;
	W rs;
	W re;

	if (ry < clip->c.top || ry >= clip->c.bottom) {
		return;
	}
	if (rx0 < clip->c.left) {
		rx0 = clip->c.left;
	}
	if (rx1 > clip->c.right) {
		rx1 = clip->c.right;
	}
	if (rx0 >= rx1) {
		return;
	}
	if (!dp_fast_ok(e, mode)) {
		for (x = rx0; x < rx1; x++) {
			dp_plot(e, clip, x, ry, src, mode);
		}
		return;
	}
	/* 単色は「周期 1 の不透明なパターン」として同じ経路に乗せる。 */
	pr.period = 1;
	pr.solid = True;
	pr.pix[0] = src & e->vmask;
	pr.opaque[0] = 1;
	x = rx0;
	while (dp_next_run(e, ry, rx1, &x, &rs, &re)) {
		dp_run(e, &pr, rs, re, ry);
	}
}

EXPORT void dp_copy_run(CONST DEnv *s, W sx0, W sy, CONST DEnv *d, W dx0,
                        W dx1, W dy, Bool back)
{
	W ady = DP_ABS_Y(d, dy);
	CONST UB *sr = row_of(s, DP_ABS_Y(s, sy));
	UB *dr = row_of(d, ady);
	W sa = DP_ABS_X(s, sx0);
	W da = DP_ABS_X(d, dx0);
	W n = dx1 - dx0;
	W i;

	dp_mark(d, da, ady, da + n, ady + 1);

	/* 同じビットマップで区間が重なるときは、読む前に書いてしまわない
	 * 向きで回す（bitmap.c の前書き）。 */
	switch (d->bound) {
	case 32: {
		CONST UW *p = (CONST UW *)(CONST VP)sr;
		UW *q = (UW *)(VP)dr;

		if (back) {
			for (i = n - 1; i >= 0; i--) {
				q[da + i] = p[sa + i] & d->vmask;
			}
		} else {
			for (i = 0; i < n; i++) {
				q[da + i] = p[sa + i] & d->vmask;
			}
		}
		return;
	}
	case 16: {
		CONST UH *p = (CONST UH *)(CONST VP)sr;
		UH *q = (UH *)(VP)dr;

		if (back) {
			for (i = n - 1; i >= 0; i--) {
				q[da + i] = (UH)(p[sa + i] & d->vmask);
			}
		} else {
			for (i = 0; i < n; i++) {
				q[da + i] = (UH)(p[sa + i] & d->vmask);
			}
		}
		return;
	}
	default:
		if (back) {
			for (i = n - 1; i >= 0; i--) {
				dr[da + i] = (UB)(sr[sa + i] & d->vmask);
			}
		} else {
			for (i = 0; i < n; i++) {
				dr[da + i] = (UB)(sr[sa + i] & d->vmask);
			}
		}
		return;
	}
}

/*
 * env.c — 描画環境の生成・削除とレコードアクセス (仕様 §2.3)
 *
 * ------------------------------------------------------------------------
 * デバイスをどう持つか
 * ------------------------------------------------------------------------
 *
 * 仕様は「描画環境の生成時には、描画対象とするデバイスを示す文字列で
 * 指定する。あらかじめディスプレイプリミティブに登録されているデバイスの
 * みが指定可能」として、必ずサポートするデバイスに "SCREEN" と "MSGPNL" を
 * 挙げている。
 *
 * "SCREEN" の固有ビットマップは**カーネルが保有している画面**で、
 * `screen()` が返す上位半分の別名（規律 R18）を使う。これが
 * `gopn_dev("SCREEN")` による「画面の貸し出し」である。
 *
 * **EL0 ができたら貸出は下位半分への写像になる**（規律 R18）。いまは
 * DP もプロセスもカーネルと同じ特権で走っているので、上位半分の別名を
 * そのまま渡している。`screen()` はそのときシステムコールに変わる。
 *
 * "MSGPNL"（システムメッセージパネル）は専用のビットマップを持つ
 * デバイスで、パネルマネージャが使う。まだ画面の外に面を持てないので
 * 固有ビットマップ無し（EG_DEV）として登録してある。
 */

#include <btron/bootinfo.h>
#include <btron/memory.h>
#include <btron/proc.h>
#include <btron/screen.h>

#include "dpi.h"

LOCAL Bool dp_ready;

/* ------------------------------------------------------------------ */
/* デバイスの登録                                                      */
/* ------------------------------------------------------------------ */

LOCAL Bool name_eq(CONST TC *name, CONST B *ascii)
{
	W i;

	for (i = 0; i <= DP_DEVNAME_LEN; i++) {
		TC a = (TC)(UB)ascii[i];

		if (name[i] != a) {
			return False;
		}
		if (a == TNULL) {
			return True;
		}
	}
	return False;
}

LOCAL void name_set(TC *dst, CONST B *ascii)
{
	W i = 0;

	while (i < DP_DEVNAME_LEN && ascii[i] != 0) {
		dst[i] = (TC)(UB)ascii[i];
		i++;
	}
	while (i <= DP_DEVNAME_LEN) {
		dst[i] = TNULL;
		i++;
	}
}

/* 画面のデバイス仕様を組み立てる。
 *
 * フレームバッファは 32 ビット語の中に 8 ビットずつ R/G/B が入っている。
 * `color[]` は「下位バイトが階調ビット数、上位バイトがビット位置」なので、
 * BGRA なら赤がビット 16、RGBA なら赤がビット 0 になる。
 * **ここを取り違えると赤と青が入れ替わる。** fb.c の screen_rgb() が
 * 同じ分岐を持っているのと同じ理由。 */
LOCAL Bool screen_spec(DEVSPEC *sp)
{
	CONST Screen *s = screen();
	UW rpos;
	UW bpos;

	if (s == 0 || s->base == 0) {
		return False;
	}
	rpos = (s->format == BFB_RGBA8) ? 0U : 16U;
	bpos = (s->format == BFB_RGBA8) ? 16U : 0U;

	sp->attr = (H)(DEV_MEM | DEV_RGB); /* 固有ビットマップ有、RGB、マップ無 */
	sp->planes = 1;
	/* 境界 32 ビットの器に有効 24 ビット。仕様の上限は 28 なので収まる。 */
	sp->pixbits = (H)BMP_PIXBITS(32, 24);
	sp->hpixels = (H)s->width;
	sp->vpixels = (H)s->height;
	sp->hres = 0; /* 解像度は分からない。仕様は「規定されていない場合は 0」 */
	sp->vres = 0;
	sp->color[0] = DEV_COLINFO(rpos, 8); /* 赤 */
	sp->color[1] = DEV_COLINFO(8, 8);    /* 緑 */
	sp->color[2] = DEV_COLINFO(bpos, 8); /* 青 */
	sp->color[3] = 0;
	{
		W i;

		for (i = 0; i < 6; i++) {
			sp->resv[i] = 0;
		}
	}
	return True;
}

/* MSGPNL の仕様。固有ビットマップは持たない（まだ面を作れない）。 */
LOCAL void msgpnl_spec(DEVSPEC *sp)
{
	W i;

	sp->attr = (H)DEV_RGB; /* DEV_MEM を立てない = デバイス描画環境は作れない */
	sp->planes = 1;
	sp->pixbits = (H)BMP_PIXBITS(32, 24);
	sp->hpixels = 0;
	sp->vpixels = 0;
	sp->hres = 0;
	sp->vres = 0;
	for (i = 0; i < 4; i++) {
		sp->color[i] = 0;
	}
	for (i = 0; i < 6; i++) {
		sp->resv[i] = 0;
	}
}

/* 登録されているデバイスを引く。無ければ False。 */
LOCAL Bool dev_lookup(CONST TC *dev, DEVSPEC *sp, Bool *has_bmp)
{
	if (name_eq(dev, DP_DEV_SCREEN)) {
		*has_bmp = screen_spec(sp);
		if (!*has_bmp) {
			/* 画面が無い構成。デバイスとしては登録されているが
			 * 固有ビットマップを持たない。 */
			msgpnl_spec(sp);
		}
		return True;
	}
	if (name_eq(dev, DP_DEV_MSGPNL)) {
		msgpnl_spec(sp);
		*has_bmp = False;
		return True;
	}
	return False;
}

/* ------------------------------------------------------------------ */
/* 初期化                                                              */
/* ------------------------------------------------------------------ */

EXPORT ERR gini_dsp(W mode)
{
	W i;

	(void)mode;
	for (i = 0; i <= DP_MAX_GID; i++) {
		dp_env[i].used = False;
	}
	dp_ready = True;
	return EG_OK;
}

EXPORT ERR gfin_dsp(void)
{
	W i;

	for (i = 1; i <= DP_MAX_GID; i++) {
		dp_env[i].used = False;
	}
	dp_ready = False;
	return EG_OK;
}

/* ------------------------------------------------------------------ */
/* 描画環境の生成                                                      */
/* ------------------------------------------------------------------ */

LOCAL DEnv *alloc_env(GID *gid)
{
	W i;

	for (i = 1; i <= DP_MAX_GID; i++) {
		if (!dp_env[i].used) {
			*gid = i;
			return &dp_env[i];
		}
	}
	return 0;
}

/* 仕様 §2.3.1 が列挙している初期値を入れる。 */
LOCAL void init_fields(DEnv *e)
{
	W i;

	e->maskpix = 0;            /* 白(0) */
	e->frame = e->bmp.bounds;  /* ビットマップの bounds */
	e->visible = e->bmp.bounds;
	e->has_clip = False;       /* 未定義 */
	e->chfgc = COL_BLACK;      /* 前景：黒(0x10000000) */
	e->chbgc = COL_WHITE;      /* 背景：白(0x10FFFFFF) */
	e->chdir = TORIGHT;
	e->chx = 0;
	e->chy = 0;
	e->chgap.hgap.dir = 0;
	e->chgap.hgap.gap = 0;
	e->chgap.vgap.dir = 0;
	e->chgap.vgap.gap = 0;
	e->chgap.hspgap.dir = 0;
	e->chgap.hspgap.gap = 0;
	e->chgap.vspgap.dir = 0;
	e->chgap.vspgap.gap = 0;
	/* 線マスクパターンは既定で実線 */
	for (i = 0; i < 8; i++) {
		e->lmask[i] = 0xFF;
	}
	e->lmask_bytes = 1;
}

/* ビットマップから派生する欄を埋める。形式が扱えなければ False。 */
LOCAL Bool set_bitmap(DEnv *e, CONST BMP *b)
{
	UW bound = BMP_BOUNDBITS(b->pixbits);
	UW valid = BMP_VALIDBITS(b->pixbits);

	if (b->planes != 1) {
		return False; /* 複数プレーンは未実装 */
	}
	if (valid == 0 || valid > DP_MAX_PLANES || valid > bound) {
		return False;
	}
	if (bound != 1 && bound != 2 && bound != 4 && bound != 8 &&
	    bound != 16 && bound != 32) {
		return False;
	}
	if ((b->rowbytes & 1) != 0) {
		return False; /* 仕様「必ず偶数バイト」 */
	}
	if (b->baseaddr[0] == 0) {
		return False;
	}
	e->bmp = *b;
	e->base = b->baseaddr[0];
	e->bound = bound;
	e->valid = valid;
	e->vmask = (valid >= 32) ? 0xFFFFFFFFU : ((1U << valid) - 1U);
	return True;
}

LOCAL void spec_to_cspec(CONST DEVSPEC *sp, CSPEC *cs)
{
	W i;

	cs->attr = sp->attr & (W)(DEV_CMAP | DEV_CTYPE);
	for (i = 0; i < 4; i++) {
		cs->info[i] = sp->color[i];
	}
	cs->colmap = 0;
}

EXPORT GID gopn_dev(CONST TC *dev, CONST B *par)
{
	DEVSPEC sp;
	Bool has_bmp = False;
	CONST Screen *s;
	DEnv *e;
	GID gid = 0;
	BMP b;

	(void)par;
	if (dev == 0) {
		return EG_ADR;
	}
	if (!dp_ready) {
		(void)gini_dsp(0);
	}
	if (!dev_lookup(dev, &sp, &has_bmp)) {
		return EG_NOEXS; /* 登録されていないデバイス */
	}
	/* 仕様「固有ビットマップを持たないデバイスの場合は、EG_DEV のエラー」 */
	if (!has_bmp) {
		return EG_DEV;
	}
	s = screen();
	if (s == 0) {
		return EG_DEV;
	}
	e = alloc_env(&gid);
	if (e == 0) {
		return EG_LIMIT;
	}

	b.planes = 1;
	b.pixbits = (UH)sp.pixbits;
	b.rowbytes = (UH)s->pitch;
	b.bounds.c.left = 0;
	b.bounds.c.top = 0;
	b.bounds.c.right = (H)s->width;
	b.bounds.c.bottom = (H)s->height;
	/* **カーネルが保有する画面の貸し出し。** 上位半分の MMIO 別名
	 * （規律 R18）をそのまま使う。EL0 を作ったらここが下位半分への
	 * 写像に変わる。 */
	b.baseaddr[0] = (UB *)(VP)s->base;

	if (!set_bitmap(e, &b)) {
		return EG_FORM;
	}
	name_set(e->dev, DP_DEV_SCREEN);
	e->spec = sp;
	e->has_spec = True;
	spec_to_cspec(&sp, &e->cspec);
	e->has_cspec = True;
	init_fields(e);
	e->owner = prc_self();
	e->used = True;
	return gid;
}

EXPORT GID gopn_mem(CONST TC *dev, BMP *bmap, CONST B *par)
{
	DEVSPEC sp;
	Bool has_bmp = False;
	DEnv *e;
	GID gid = 0;

	if (bmap == 0) {
		return EG_ADR;
	}
	if (!dp_ready) {
		(void)gini_dsp(0);
	}
	if (dev != 0) {
		if (!dev_lookup(dev, &sp, &has_bmp)) {
			return EG_NOEXS;
		}
		/* 仕様「bmap で指定したビットマップのプレーン数とピクセルビット数が、
		 * dev で指定したデバイスで定義されるものと一致していない場合は
		 * EG_DEV のエラー」 */
		if ((H)bmap->planes != sp.planes ||
		    (H)bmap->pixbits != sp.pixbits) {
			return EG_DEV;
		}
	}
	e = alloc_env(&gid);
	if (e == 0) {
		return EG_LIMIT;
	}
	if (!set_bitmap(e, bmap)) {
		return EG_PAR; /* 仕様「bmap の内容が不正」 */
	}
	if (dev != 0) {
		W i;

		for (i = 0; i <= DP_DEVNAME_LEN; i++) {
			e->dev[i] = (i < DP_DEVNAME_LEN) ? dev[i] : TNULL;
			if (e->dev[i] == TNULL) {
				break;
			}
		}
		e->dev[DP_DEVNAME_LEN] = TNULL;
		e->spec = sp;
		e->has_spec = True;
		spec_to_cspec(&sp, &e->cspec);
		e->has_cspec = True;
	} else {
		/* 汎用メモリ描画環境。dev = NULL。 */
		e->dev[0] = TNULL;
		e->has_spec = False;
		if (par != 0) {
			/* 仕様「B *par != NULL の場合、CSPEC *par とみなされ、
			 * カラー情報付きの汎用メモリ描画環境を生成する」 */
			e->cspec = *(CONST CSPEC *)(CONST VP)par;
			e->has_cspec = True;
		} else {
			/* 仕様「カラー情報を持たない汎用メモリ描画環境の場合には、
			 * ピクセル値とカラーとの対応は定義されない」 */
			e->has_cspec = False;
		}
	}
	init_fields(e);
	e->owner = prc_self();
	e->used = True;
	return gid;
}

EXPORT GID gnew_env(GID gid)
{
	DEnv *src = dp_get(gid);
	DEnv *e;
	GID ngid = 0;

	if (src == 0) {
		return EG_GID;
	}
	e = alloc_env(&ngid);
	if (e == 0) {
		return EG_LIMIT;
	}
	/* 仕様「全く同じ内容を持つ描画環境を新たに生成(複製)」。
	 * 「デバイス情報、カラー情報、カラーマップ、ビットマップ
	 * (BMP.baseaddr のデータ領域)は生成元と共有される」 */
	*e = *src;
	e->owner = prc_self();
	e->used = True;
	return ngid;
}

EXPORT ERR gcls_env(GID gid)
{
	DEnv *e = dp_get(gid);

	if (e == 0) {
		return EG_GID;
	}
	/* 仕様「描画環境を生成したプロセスのみがその描画環境を削除可能であり、
	 * そうでない場合には EG_GID のエラーとなる」 */
	if (e->owner != prc_self()) {
		return EG_GID;
	}
	e->used = False;
	return EG_OK;
}

EXPORT ERR gini_env(GID gid)
{
	DEnv *e = dp_get(gid);

	if (e == 0) {
		return EG_GID;
	}
	init_fields(e);
	return EG_OK;
}

/* ------------------------------------------------------------------ */
/* デバイス情報の取り出し                                              */
/* ------------------------------------------------------------------ */

EXPORT ERR gget_spc(CONST TC *dev, DEVSPEC *buf)
{
	DEVSPEC sp;
	Bool has_bmp = False;

	if (dev == 0 || buf == 0) {
		return EG_ADR;
	}
	if (!dev_lookup(dev, &sp, &has_bmp)) {
		return EG_NOEXS;
	}
	*buf = sp;
	return EG_OK;
}

EXPORT ERR gget_dev(GID gid, TC *dev)
{
	DEnv *e = dp_get(gid);
	W i;

	if (e == 0) {
		return EG_GID;
	}
	if (dev == 0) {
		return EG_ADR;
	}
	for (i = 0; i <= DP_DEVNAME_LEN; i++) {
		dev[i] = e->dev[i];
	}
	return EG_OK;
}

EXPORT ERR gget_dsp(GID gid, DEVSPEC *buf)
{
	DEnv *e = dp_get(gid);

	if (e == 0) {
		return EG_GID;
	}
	if (buf == 0) {
		return EG_ADR;
	}
	if (!e->has_spec) {
		return EG_DEV; /* 汎用メモリ描画環境はデバイス情報を持たない */
	}
	*buf = e->spec;
	return EG_OK;
}

EXPORT ERR gget_csp(GID gid, CSPEC *buf)
{
	DEnv *e = dp_get(gid);

	if (e == 0) {
		return EG_GID;
	}
	if (buf == 0) {
		return EG_ADR;
	}
	if (!e->has_cspec) {
		return EG_ENV; /* カラー情報が無い */
	}
	*buf = e->cspec;
	return EG_OK;
}

EXPORT W gcnv_col(GID gid, COLOR cv, PIXVAL *pixv)
{
	DEnv *e = dp_get(gid);
	UW pix = 0;

	if (e == 0) {
		return EG_GID;
	}
	if (pixv == 0) {
		return EG_ADR;
	}
	if (!dp_color(e, cv, &pix)) {
		return EG_PAR; /* 透明は変換できない */
	}
	*pixv = (PIXVAL)pix;
	return EG_OK;
}

/* ------------------------------------------------------------------ */
/* 描画環境レコードのアクセス                                          */
/* ------------------------------------------------------------------ */

EXPORT ERR gset_msk(GID gid, PIXVAL pixv)
{
	DEnv *e = dp_get(gid);

	if (e == 0) {
		return EG_GID;
	}
	e->maskpix = pixv;
	return EG_OK;
}

EXPORT ERR gget_msk(GID gid, PIXVAL *pixv)
{
	DEnv *e = dp_get(gid);

	if (e == 0) {
		return EG_GID;
	}
	if (pixv == 0) {
		return EG_ADR;
	}
	*pixv = e->maskpix;
	return EG_OK;
}

EXPORT ERR gset_clp(GID gid, CONST GRGN *clip)
{
	DEnv *e = dp_get(gid);

	if (e == 0) {
		return EG_GID;
	}
	if (clip == 0) {
		e->has_clip = False; /* 未定義に戻す */
		return EG_OK;
	}
	/* 仕様「ns = 0 の場合は、任意領域は長方形 r に等しい」。
	 * ns > 0 の任意領域は未実装。 */
	if (clip->ns != 0) {
		return EG_NOSPT;
	}
	e->clip = clip->r;
	e->has_clip = True;
	return EG_OK;
}

EXPORT ERR gget_clp(GID gid, GRGN *clip, W csize, W *a_size)
{
	DEnv *e = dp_get(gid);
	W need = (W)sizeof(GRGN);

	if (e == 0) {
		return EG_GID;
	}
	if (a_size != 0) {
		*a_size = need;
	}
	if (clip == 0) {
		return EG_ADR;
	}
	if (csize < need) {
		return EG_PAR;
	}
	clip->r = e->has_clip ? e->clip : e->bmp.bounds;
	clip->ns = 0;
	return EG_OK;
}

EXPORT ERR gset_fra(GID gid, RECT r)
{
	DEnv *e = dp_get(gid);

	if (e == 0) {
		return EG_GID;
	}
	e->frame = r;
	return EG_OK;
}

EXPORT ERR gget_fra(GID gid, RECT *rp)
{
	DEnv *e = dp_get(gid);

	if (e == 0) {
		return EG_GID;
	}
	if (rp == 0) {
		return EG_ADR;
	}
	*rp = e->frame;
	return EG_OK;
}

EXPORT ERR gset_vis(GID gid, RECT r)
{
	DEnv *e = dp_get(gid);

	if (e == 0) {
		return EG_GID;
	}
	e->visible = r;
	return EG_OK;
}

EXPORT ERR gget_vis(GID gid, RECT *rp)
{
	DEnv *e = dp_get(gid);

	if (e == 0) {
		return EG_GID;
	}
	if (rp == 0) {
		return EG_ADR;
	}
	*rp = e->visible;
	return EG_OK;
}

EXPORT ERR gget_bmp(GID gid, BMP *bmap)
{
	DEnv *e = dp_get(gid);

	if (e == 0) {
		return EG_GID;
	}
	if (bmap == 0) {
		return EG_ADR;
	}
	*bmap = e->bmp;
	return EG_OK;
}

EXPORT ERR gget_bnd(GID gid, RECT *rp)
{
	DEnv *e = dp_get(gid);

	if (e == 0) {
		return EG_GID;
	}
	if (rp == 0) {
		return EG_ADR;
	}
	*rp = e->bmp.bounds;
	return EG_OK;
}

/* 座標値が 16 ビットに収まるか。仕様が範囲外を EG_PAR としている。 */
LOCAL Bool fits(W v)
{
	return (v >= DP_COORD_MIN && v <= DP_COORD_MAX) ? True : False;
}

LOCAL Bool move_rect(RECT *r, W dh, W dv)
{
	if (!fits(r->c.left + dh) || !fits(r->c.right + dh) ||
	    !fits(r->c.top + dv) || !fits(r->c.bottom + dv)) {
		return False;
	}
	r->c.left = (H)(r->c.left + dh);
	r->c.right = (H)(r->c.right + dh);
	r->c.top = (H)(r->c.top + dv);
	r->c.bottom = (H)(r->c.bottom + dv);
	return True;
}

EXPORT ERR gmov_bnd(GID gid, W dh, W dv)
{
	DEnv *e = dp_get(gid);
	RECT b;

	if (e == 0) {
		return EG_GID;
	}
	b = e->bmp.bounds;
	if (!move_rect(&b, dh, dv)) {
		return EG_PAR;
	}
	/* 仕様「クリッピング領域、文字描画位置等の相対座標は変更されない」。
	 * **bounds だけを動かす。** これにより同じ相対座標が別のピクセルを
	 * 指すようになる（座標系の原点が動く）。 */
	e->bmp.bounds = b;
	return EG_OK;
}

EXPORT ERR gmov_cor(GID gid, W dh, W dv)
{
	DEnv *e = dp_get(gid);
	RECT b;
	RECT f;
	RECT v;
	RECT c;

	if (e == 0) {
		return EG_GID;
	}
	b = e->bmp.bounds;
	f = e->frame;
	v = e->visible;
	c = e->clip;
	/* 仕様「ビットマップ境界(bounds)、フレーム長方形、表示長方形、
	 * 文字描画位置の全てを(dh, dv)だけ平行移動する」。
	 * 全部が動くので、同じ相対座標が同じピクセルを指し続ける。 */
	if (!move_rect(&b, dh, dv) || !move_rect(&f, dh, dv) ||
	    !move_rect(&v, dh, dv)) {
		return EG_PAR;
	}
	if (e->has_clip && !move_rect(&c, dh, dv)) {
		return EG_PAR;
	}
	if (!fits(e->chx + dh) || !fits(e->chy + dv)) {
		return EG_PAR;
	}
	e->bmp.bounds = b;
	e->frame = f;
	e->visible = v;
	e->clip = c;
	e->chx += dh;
	e->chy += dv;
	return EG_OK;
}

EXPORT ERR gcnv_abs(GID gid, PNT *pp)
{
	DEnv *e = dp_get(gid);

	if (e == 0) {
		return EG_GID;
	}
	if (pp == 0) {
		return EG_ADR;
	}
	/* 相対座標 → 絶対座標（ビットマップ左上を常に (0,0) とする座標）。 */
	pp->x = (H)DP_ABS_X(e, pp->x);
	pp->y = (H)DP_ABS_Y(e, pp->y);
	return EG_OK;
}

EXPORT ERR gcnv_rel(GID gid, PNT *pp)
{
	DEnv *e = dp_get(gid);

	if (e == 0) {
		return EG_GID;
	}
	if (pp == 0) {
		return EG_ADR;
	}
	pp->x = (H)(pp->x + e->bmp.bounds.c.left);
	pp->y = (H)(pp->y + e->bmp.bounds.c.top);
	return EG_OK;
}

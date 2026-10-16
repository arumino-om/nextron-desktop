/*
 * pointer.c — ポインタ制御関数 (§2.6)
 *
 * ポインタは「スクリーン上に表示されているオブジェクトを指し示すために
 * 使用される特殊な図形であり、スクリーン上の表示にオーバーラップする形で
 * 表示される」。したがって
 *
 *   - **描画環境 (GID) に属さない。**画面全体で 1 つの資源なので、
 *     §2.6 の関数だけが gid を取らない
 *   - **クリッピングも前置長方形も掛けない。**手前の窓の上にも出る
 *
 * 【消したものをどう戻すか】
 * ポインタは下にあった絵を壊す。だから描く前に n×n 画素を退避し、
 * 消すときに書き戻す（いわゆる save-under）。ハードウェアカーソルがあれば
 * そちらに逃がせるが、ramfb にはない。
 *
 * **退避した後に誰かがその場所へ描くと、書き戻しで古い絵が戻ってしまう。**
 * 仕様が `gdsp_ptr` に消去要求数（カウンタ）を持たせているのはこのためで、
 * 描画する側が消去 → 描画 → 表示で囲む約束になっている。
 *
 * 【ポインタサイズは 16】
 * 仕様は「標準的には 24 であるが、インプリメントによっては、16、32 等の
 * 場合もある」としている。本OS は 16 を選んだ。内蔵フォントが 16 ドット
 * 固定で、画面も QEMU の既定では広くないため、24 だと大きすぎる。
 * アプリ定義イメージ (style < 0) は仕様どおり 64 まで受ける。
 */

#include "dpi.h"

#include <btron/event.h>
#include <btron/screen.h>

/* ------------------------------------------------------------------ */
/* 標準形状                                                            */
/*                                                                     */
/* 絵は 16 行の文字列で書く。16 進のビットパターンで書くと、**1 ビット  */
/* ずれても気づけない。** 文字で書けば読んで直せる。                    */
/*                                                                     */
/*   'X'  前景色（地の色）                                             */
/*   '.'  何も無い                                                     */
/*                                                                     */
/* 縁取り（背景色）は書かない。'X' に 8 近傍で接する '.' を自動的に     */
/* 背景色にする。こうすると、どの絵も必ず縁が付いて、濃い背景の上でも   */
/* 見える。                                                            */
/* ------------------------------------------------------------------ */

#define PTR_N 16 /* 本OS のポインタサイズ */

typedef struct StdShape {
	CONST B *row[PTR_N];
	H hotx;
	H hoty;
} StdShape;

/* 選択指。矢印。ホットポイントは先端 (0,0)。 */
LOCAL CONST StdShape sh_select = {
    {"X...............",
     "XX..............",
     "XXX.............",
     "XXXX............",
     "XXXXX...........",
     "XXXXXX..........",
     "XXXXXXX.........",
     "XXXXXXXX........",
     "XXXXXXXXX.......",
     "XXXXXXXXXX......",
     "XXXXXX..........",
     "XXX.XXX.........",
     "XX..XXX.........",
     "X....XXX........",
     "......XXX.......",
     ".......XX......."},
    0, 0};

/* 修正選択指。矢印に「＋」を添える。 */
LOCAL CONST StdShape sh_modify = {
    {"X.........XX....",
     "XX........XX....",
     "XXX.....XXXXXX..",
     "XXXX....XXXXXX..",
     "XXXXX.....XX....",
     "XXXXXX....XX....",
     "XXXXXXX.........",
     "XXXXXXXX........",
     "XXXXXXXXX.......",
     "XXXXXXXXXX......",
     "XXXXXX..........",
     "XXX.XXX.........",
     "XX..XXX.........",
     "X....XXX........",
     "......XXX.......",
     ".......XX......."},
    0, 0};

/* 移動手。四方向の矢印。ホットポイントは中心。 */
LOCAL CONST StdShape sh_move = {
    {".......XX.......",
     "......XXXX......",
     ".....XXXXXX.....",
     ".......XX.......",
     ".......XX.......",
     "..X....XX....X..",
     ".XX....XX....XX.",
     "XXXXXXXXXXXXXXXX",
     "XXXXXXXXXXXXXXXX",
     ".XX....XX....XX.",
     "..X....XX....X..",
     ".......XX.......",
     ".......XX.......",
     ".....XXXXXX.....",
     "......XXXX......",
     ".......XX......."},
    8, 8};

LOCAL CONST StdShape sh_vmove = {
    {".......XX.......",
     "......XXXX......",
     ".....XXXXXX.....",
     "....XXXXXXXX....",
     ".......XX.......",
     ".......XX.......",
     ".......XX.......",
     ".......XX.......",
     ".......XX.......",
     ".......XX.......",
     ".......XX.......",
     "....XXXXXXXX....",
     ".....XXXXXX.....",
     "......XXXX......",
     ".......XX.......",
     "................"},
    8, 8};

LOCAL CONST StdShape sh_hmove = {
    {"................",
     "................",
     "................",
     "...X.........X..",
     "..XX.........XX.",
     ".XXX.........XXX",
     "XXXXXXXXXXXXXXXX",
     "XXXXXXXXXXXXXXXX",
     ".XXX.........XXX",
     "..XX.........XX.",
     "...X.........X..",
     "................",
     "................",
     "................",
     "................",
     "................"},
    8, 8};

/* 握り。移動ドラッグ中。枠の中を掴んでいる形。 */
LOCAL CONST StdShape sh_grip = {
    {"................",
     "................",
     "...XXXXXXXXXX...",
     "...X........X...",
     "...X..XXXX..X...",
     "...X.XXXXXX.X...",
     "...X.XXXXXX.X...",
     "...X.XXXXXX.X...",
     "...X.XXXXXX.X...",
     "...X..XXXX..X...",
     "...X........X...",
     "...XXXXXXXXXX...",
     "................",
     "................",
     "................",
     "................"},
    8, 8};

/* 変形手。斜めの双方向矢印。 */
LOCAL CONST StdShape sh_rsiz = {
    {"XXXXXXX.........",
     "XXXX............",
     "XX.XX...........",
     "X...XX..........",
     ".....XX.........",
     "......XX........",
     ".......XX.......",
     "........XX......",
     ".........XX.....",
     "..........XX....",
     "...........XX..X",
     "............XX.X",
     ".............XXX",
     "..........XXXXXX",
     ".........XXXXXXX",
     "................"},
    8, 8};

LOCAL CONST StdShape sh_vrsiz = {
    {".......XX.......",
     "......XXXX......",
     ".....XXXXXX.....",
     ".......XX.......",
     ".......XX.......",
     ".......XX.......",
     "XXXXXXXXXXXXXXXX",
     "XXXXXXXXXXXXXXXX",
     ".......XX.......",
     ".......XX.......",
     ".......XX.......",
     ".....XXXXXX.....",
     "......XXXX......",
     ".......XX.......",
     "................",
     "................"},
    8, 8};

LOCAL CONST StdShape sh_hrsiz = {
    {"................",
     "................",
     "...X.....X...X..",
     "..XX.....X...XX.",
     ".XXX.....X...XXX",
     "XXXXXXXXXXXXXXXX",
     "XXXXXXXXXXXXXXXX",
     "XXXXXXXXXXXXXXXX",
     ".XXX.....X...XXX",
     "..XX.....X...XX.",
     "...X.....X...X..",
     "................",
     "................",
     "................",
     "................",
     "................"},
    8, 8};

/* つまみ。変形ドラッグ中。 */
LOCAL CONST StdShape sh_pick = {
    {"................",
     "................",
     "................",
     "................",
     "................",
     ".....XXXXXX.....",
     ".....XXXXXX.....",
     ".....XXXXXX.....",
     ".....XXXXXX.....",
     ".....XXXXXX.....",
     ".....XXXXXX.....",
     "................",
     "................",
     "................",
     "................",
     "................"},
    8, 8};

/* 湯のみ。待ち状態。BTRON の待ちカーソルは砂時計ではなく湯のみである。 */
LOCAL CONST StdShape sh_busy = {
    {"................",
     "..XXXXXXXXXX....",
     "..X........X....",
     "..X........X....",
     "..X........XXX..",
     "..X........X.X..",
     "..X........X.X..",
     "..X........XXX..",
     "..X........X....",
     "...X......X.....",
     "....X....X......",
     ".....XXXX.......",
     "................",
     ".XXXXXXXXXXXX...",
     "................",
     "................"},
    8, 8};

/* プリメニュー。矢印にメニューを添える。 */
LOCAL CONST StdShape sh_menu = {
    {"X...............",
     "XX..............",
     "XXX.............",
     "XXXX....XXXXXXX.",
     "XXXXX...X.....X.",
     "XXXXXX..XXXXXXX.",
     "XXXXXXX.X.....X.",
     "XXXXXXXXXXXXXXX.",
     "XXXXXXXXX.....X.",
     "XXXXXXXXXXXXXXX.",
     "XXXXXX..........",
     "XXX.XXX.........",
     "XX..XXX.........",
     "X....XXX........",
     "......XXX.......",
     ".......XX......."},
    0, 0};

/* 標準形状の表。
 *
 * **縦横の別がある形状は、いまは同じ絵を使っている。** 仕様が区別して
 * いるのは「どの方向に動かせるか」であって、絵を必ず変えろとは言って
 * いない。方向つきの絵は、ドラッグ操作を入れるときに見直す。 */
LOCAL CONST StdShape *CONST std[PS_STD_COUNT] = {
    &sh_select, /* PS_SELECT */
    &sh_modify, /* PS_MODIFY */
    &sh_move,   /* PS_MOVE   */
    &sh_vmove,  /* PS_VMOVE  */
    &sh_hmove,  /* PS_HMOVE  */
    &sh_grip,   /* PS_GRIP   */
    &sh_grip,   /* PS_VGRIP  暫定 */
    &sh_grip,   /* PS_HGRIP  暫定 */
    &sh_rsiz,   /* PS_RSIZ   */
    &sh_vrsiz,  /* PS_VRSIZ  */
    &sh_hrsiz,  /* PS_HRSIZ  */
    &sh_pick,   /* PS_PICK   */
    &sh_pick,   /* PS_VPICK  暫定 */
    &sh_pick,   /* PS_HPICK  暫定 */
    &sh_busy,   /* PS_BUSY   */
    &sh_menu,   /* PS_MENU   */
};

/* ------------------------------------------------------------------ */
/* 状態                                                                */
/* ------------------------------------------------------------------ */

/* --- プロセスごとに持つもの --- */

LOCAL DEnv scr;      /* 画面の描画環境。GID は消費しない */
LOCAL Bool scr_ok;   /* 画面があるか */
LOCAL Bool env_done; /* scr を組んだか */

/* --- 全プロセスで 1 つのもの --- */

/* 画面を借りられないとき（検査用の構成など）の器。
 * **画面が無ければポインタも 1 つに揃える相手が居ない**ので、
 * プロセスごとに持っていても害が無い。 */
LOCAL PtrShared solo;

/* いま見ている置き場。ensure() が決める。 */
LOCAL PtrShared *pst = &solo;

/* ------------------------------------------------------------------ */
/* ビットマップの出し入れ                                              */
/* ------------------------------------------------------------------ */

/* 1 行のバイト数。仕様 §2.6.2「rowbytes : ((n + 15)÷16)×2 (偶数)」。 */
LOCAL W row_bytes(W n)
{
	return ((n + 15) / 16) * 2;
}

LOCAL Bool bit_get(CONST UB *bits, W n, W x, W y)
{
	W rb = row_bytes(n);

	/* 上位ビットが左。A4 の決めに合わせる（pixel.c と同じ向き）。 */
	return (bits[y * rb + (x / 8)] & (UB)(0x80U >> (x % 8))) != 0;
}

LOCAL void bit_set(UB *bits, W n, W x, W y, Bool on)
{
	W rb = row_bytes(n);
	UB m = (UB)(0x80U >> (x % 8));

	if (on) {
		bits[y * rb + (x / 8)] |= m;
	} else {
		bits[y * rb + (x / 8)] &= (UB)~m;
	}
}

/* 文字で書いた絵を data / mask に直す。
 *
 * data = 'X' のところ。mask = 'X' とその 8 近傍。こうすると
 * 「data 1 mask 1 = 前景色」「data 0 mask 1 = 背景色（縁取り）」
 * 「data 0 mask 0 = 透明」になり、縁が自動的に付く。 */
LOCAL void load_std(CONST StdShape *sh)
{
	W x;
	W y;
	W i;

	for (i = 0; i < P_SIZE; i++) {
		pst->img.data[i] = 0;
		pst->img.mask[i] = 0;
	}
	pst->img.size.h = PTR_N;
	pst->img.size.v = PTR_N;
	pst->img.hotpt.x = sh->hotx;
	pst->img.hotpt.y = sh->hoty;

	for (y = 0; y < PTR_N; y++) {
		for (x = 0; x < PTR_N; x++) {
			if (sh->row[y][x] == 'X') {
				bit_set(pst->img.data, PTR_N, x, y, True);
			}
		}
	}
	for (y = 0; y < PTR_N; y++) {
		for (x = 0; x < PTR_N; x++) {
			W dy;

			for (dy = -1; dy <= 1; dy++) {
				W dx;

				for (dx = -1; dx <= 1; dx++) {
					W sx = x + dx;
					W sy = y + dy;

					if (sx < 0 || sy < 0 || sx >= PTR_N ||
					    sy >= PTR_N) {
						continue;
					}
					if (sh->row[sy][sx] == 'X') {
						bit_set(pst->img.mask, PTR_N, x, y, True);
					}
				}
			}
		}
	}
}

/* ------------------------------------------------------------------ */
/* 画面への出し入れ                                                    */
/* ------------------------------------------------------------------ */

/* 共有の置き場を決める。
 *
 * **ポインタは画面に 1 つしか無い**ので、画面を借りられたときは
 * カーネルが貸してくれた置き場を使う（dp.h の `PtrShared` の前書き）。
 * 画面が無い構成では揃える相手が居ないので自前の器で動く。 */
LOCAL void bind_shared(void)
{
	CONST Screen *s = screen();
	PtrShared *q;

	if (s == 0 || s->ptr == 0) {
		return;
	}
	q = (PtrShared *)(VP)s->ptr;
	/* カーネルが渡してくる領域はゼロで始まる。**最初に来たプロセスが
	 * 印を打つ**ので、印が合わなければまだ誰も使っていない。 */
	if (q->magic != PTR_SHARED_MAGIC) {
		q->magic = PTR_SHARED_MAGIC;
		q->inited = 0;
	}
	pst = q;
}

LOCAL Bool ensure(void)
{
	if (!env_done) {
		env_done = True;
		scr_ok = dp_screen_env(&scr);
		bind_shared();
	}
	if (!pst->inited) {
		pst->inited = 1;
		pst->sts.style = PS_SELECT;
		pst->sts.fgcol = COL_BLACK;
		pst->sts.bgcol = COL_WHITE;
		pst->sts.motion = 1;
		pst->sts.hidden = 0;
		pst->sts.size = PTR_N;
		pst->sts.pos.x = 0;
		pst->sts.pos.y = 0;
		load_std(std[PS_SELECT]);
		if (scr_ok) {
			if (!dp_color(&scr, pst->sts.fgcol, &pst->fgpix)) {
				pst->fgpix = 0;
			}
			if (!dp_color(&scr, pst->sts.bgcol, &pst->bgpix)) {
				pst->bgpix = scr.vmask;
			}
		}
	}
	return scr_ok;
}

LOCAL Bool on_screen(W ax, W ay)
{
	return ax >= 0 && ay >= 0 && ax < (W)scr.bmp.bounds.c.right &&
	       ay < (W)scr.bmp.bounds.c.bottom;
}

/* ホットポイントを pos に合わせたときのイメージ左上。 */
LOCAL PNT topleft(PNT pos)
{
	PNT p;

	p.x = (H)(pos.x - pst->img.hotpt.x);
	p.y = (H)(pos.y - pst->img.hotpt.y);
	return p;
}

/* 変わったことを合成する側へ知らせる。
 *
 * **ここでは画面に描かない**（詳細 E2）。下地は合成でいつでも作り直せる
 * ので、退避も書き戻しも要らない。描くのは `gptr_paint`。 */
LOCAL void refresh(void)
{
	pst->seq++;
}

/* ------------------------------------------------------------------ */
/* §2.6.3 の関数                                                       */
/* ------------------------------------------------------------------ */

EXPORT ERR gini_ptr(void)
{
	PNT p;

	(void)ensure();
	pst->sts.style = PS_SELECT;
	pst->sts.fgcol = COL_BLACK;
	pst->sts.bgcol = COL_WHITE;
	pst->sts.motion = 1; /* 追従状態 */
	pst->sts.hidden = 0; /* 表示状態 */
	pst->sts.size = PTR_N;
	load_std(std[PS_SELECT]);
	if (scr_ok) {
		if (!dp_color(&scr, pst->sts.fgcol, &pst->fgpix)) {
			pst->fgpix = 0;
		}
		if (!dp_color(&scr, pst->sts.bgcol, &pst->bgpix)) {
			pst->bgpix = scr.vmask;
		}
	}
	/* 「現在のポインティングデバイスの位置に表示する」 */
	if (get_pdp(&p) == ER_OK) {
		pst->sts.pos = p;
	}
	refresh();
	return EG_OK;
}

EXPORT ERR gdsp_ptr(W req)
{
	(void)ensure();
	if (req == 0) {
		/* 消去要求。要求数を +1 して消す。 */
		pst->sts.hidden++;
	} else if (req > 0) {
		/* 表示要求。0 でなければ -1 し、0 になったら表示する。 */
		if (pst->sts.hidden > 0) {
			pst->sts.hidden--;
		}
	} else {
		/* 強制表示要求。要求数を 0 にして表示する。 */
		pst->sts.hidden = 0;
	}
	refresh();
	return pst->sts.hidden;
}

EXPORT ERR gset_ptr(PTRSTL style, PTRIMG *im, COLOR fgcol, COLOR bgcol)
{
	UW fp;
	UW bp;
	COLOR fc = fgcol;
	COLOR bc = bgcol;

	(void)ensure();
	if (style >= 0 && style >= PS_STD_COUNT) {
		return EG_PAR; /* 予約されている標準形状 */
	}
	if (style < 0) {
		W n;

		if (im == 0) {
			return EG_ADR;
		}
		n = im->size.v;
		/* 仕様はポインタ形状を正方形として定義している。 */
		if (n <= 0 || n > MAX_PTR_SIZE || im->size.h != im->size.v) {
			return EG_PAR;
		}
		if (im->hotpt.x < 0 || im->hotpt.y < 0 || im->hotpt.x >= n ||
		    im->hotpt.y >= n) {
			return EG_PAR;
		}
	}

	/* 仕様「fgcol<0, bgcol<0 の場合は、それぞれデフォールト色の指定」。
	 * COLOR は符号なしで持っているが、最上位ビットが立つのは透明指定
	 * (COL_TRANS) だけなので、「負」と「透明」はここでは同じ値になる。
	 * 既定色として扱う。 */
	if (COLOR_IS_TRANS(fc)) {
		fc = COL_BLACK;
	}
	if (COLOR_IS_TRANS(bc)) {
		bc = COL_WHITE;
	}
	if (scr_ok) {
		if (!dp_color(&scr, fc, &fp) || !dp_color(&scr, bc, &bp)) {
			return EG_PAR;
		}
	} else {
		fp = 0;
		bp = 0;
	}

	/* ここまで検査が通ってから状態を書き換える。
	 * 途中で失敗して半端な形状が残ると、画面が壊れたままになる。 */
	pst->sts.style = style;
	pst->sts.fgcol = fc;
	pst->sts.bgcol = bc;
	pst->fgpix = fp;
	pst->bgpix = bp;
	if (style >= 0) {
		load_std(std[style]);
	} else {
		pst->img = *im;
	}
	pst->sts.size = pst->img.size.v;
	refresh();
	return EG_OK;
}

EXPORT ERR gmov_ptr(PNT pos)
{
	(void)ensure();
	/* 仕様「ポインタが追従状態の場合は、何もしない」。戻り値は 1。 */
	if (pst->sts.motion != 0) {
		return 1;
	}
	if (scr_ok) {
		if (pos.x < 0 || pos.y < 0 || pos.x >= scr.bmp.bounds.c.right ||
		    pos.y >= scr.bmp.bounds.c.bottom) {
			return EG_PAR;
		}
	}
	pst->sts.pos = pos;
	refresh();
	return 0;
}

EXPORT ERR gmot_ptr(W s)
{
	Bool want = (s != 0);

	(void)ensure();
	if (want == (pst->sts.motion != 0)) {
		return EG_OK; /* 仕様「既に…の場合は何もしない」 */
	}
	pst->sts.motion = want ? 1 : 0;
	if (want) {
		/* 仕様「追従指定を行なった場合は、その時点から追従を始める。
		 * 従って、表示位置が PD の位置と異なっていた場合は、
		 * 追従指定によりポインタ表示が移動する」 */
		gptr_follow();
	}
	return EG_OK;
}

EXPORT W gget_ptr(PTRSTS *s, PTRIMG *im)
{
	(void)ensure();
	if (s != 0) {
		*s = pst->sts;
	}
	if (im != 0) {
		*im = pst->img;
	}
	return pst->sts.size;
}

/* ------------------------------------------------------------------ */
/* 本OS 内部の口                                                       */
/* ------------------------------------------------------------------ */

EXPORT void gptr_follow(void)
{
	PNT p;

	if (!ensure()) {
		return;
	}
	if (pst->sts.motion == 0) {
		return;
	}
	if (get_pdp(&p) != ER_OK) {
		return;
	}
	if (p.x == pst->sts.pos.x && p.y == pst->sts.pos.y) {
		return;
	}
	pst->sts.pos = p;
	refresh();
}

EXPORT W gptr_seq(void)
{
	(void)ensure();
	return pst->seq;
}

EXPORT W gptr_rect(RECT *r)
{
	PNT at;
	W n;

	if (!ensure() || pst->sts.hidden > 0 || r == 0) {
		return 0;
	}
	at = topleft(pst->sts.pos);
	n = pst->img.size.v;
	r->c.left = at.x;
	r->c.top = at.y;
	r->c.right = (H)(at.x + n);
	r->c.bottom = (H)(at.y + n);
	return 1;
}

EXPORT void gptr_paint_to(GID gid, CONST RECT *area)
{
	DEnv *dst;
	PNT at;
	W n;
	W x;
	W y;

	if (!ensure() || pst->sts.hidden > 0) {
		return;
	}
	/* **描き先を選べるようにした理由。**
	 *
	 * 合成を画面へ直に書くと、下地を貼ってからポインタを載せるまでの
	 * 間が見える（走査が追い越す）。一時面へ組んでから 1 回で貼れば
	 * 画面の各画素は 1 度しか書かれないので、その隙間が無くなる。
	 * gid が 0 なら従来どおり画面へ描く。 */
	dst = (gid >= 1) ? dp_get(gid) : &scr;
	if (dst == 0) {
		return;
	}
	at = topleft(pst->sts.pos);
	n = pst->img.size.v;
	for (y = 0; y < n; y++) {
		for (x = 0; x < n; x++) {
			W ax = at.x + x;
			W ay = at.y + y;
			Bool d;
			Bool m;
			UW v;

			if (!on_screen(ax, ay)) {
				continue;
			}
			if (area != 0 &&
			    (ax < area->c.left || ax >= area->c.right ||
			     ay < area->c.top || ay >= area->c.bottom)) {
				continue;
			}
			d = bit_get(pst->img.data, n, x, y);
			m = bit_get(pst->img.mask, n, x, y);
			if (!d && !m) {
				continue; /* 透明。下地のまま */
			}
			if (m) {
				v = d ? pst->fgpix : pst->bgpix;
			} else {
				/* data=1 mask=0 は反転。合成し直した下地を読む。 */
				v = dp_pix_get(dst, ax, ay) ^ dst->vmask;
			}
			dp_pix_put(dst, ax, ay, v);
		}
	}
}

EXPORT void gptr_paint(CONST RECT *area)
{
	gptr_paint_to(0, area);
}

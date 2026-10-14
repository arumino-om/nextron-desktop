/*
 * winman.c — ウィンドウマネージャ (BTRON3 §3.1)
 *
 * ------------------------------------------------------------------------
 * 前後関係は「クラス」で決まり、クラスの中だけが可変である
 * ------------------------------------------------------------------------
 *
 * 仕様 §3.1.1.2 の前後関係は固定の 5 段で、
 *
 *   (1) 前面ウィンドウ  (2) パネル  (3) 入力受付の従属
 *   (4) 入力受付の主    (5) 入力不可の主
 *
 * 「ウィンドウを前に出す」操作はクラスを跨がない。主ウィンドウを前に
 * 出すのは「入力受付状態にする」ことと同じで、クラス (5) から (4) へ
 * 移ることを意味する。だから**順序の管理は 1 本の配列 + クラスによる
 * 安定並べ替え**で済み、Z 値を持ち回る必要がない。
 *
 * ------------------------------------------------------------------------
 * 重なりは DP の前置長方形リストで表す
 * ------------------------------------------------------------------------
 *
 * 各ウィンドウの描画環境に「自分より手前にあるウィンドウの矩形」を
 * `gset_for()` で入れておくと、奥のウィンドウが手前を上書きしなくなる
 * （仕様 §2.2.4）。前後関係が変わるたびに入れ直す。
 *
 * これが無いと、奥のウィンドウが描画するたびに手前が消える。仕様が
 * ディスプレイプリミティブ側に前置長方形リストを用意しているのは、
 * まさにウィンドウマネージャのためである。
 *
 * ------------------------------------------------------------------------
 * イベントの配送
 * ------------------------------------------------------------------------
 *
 * `wget_evt` が核のイベント管理 (§1.7) からイベントを取り、
 *
 *   - PD のボタンが入力受付状態でないウィンドウの上で押された
 *       → そのウィンドウへ入力受付状態を移し、呼び出し側へは EV_SWITCH
 *   - 作業領域の中で押された   → 生イベントをそのまま渡す
 *   - 枠・タイトル・バーの上   → 位置コードを入れた EV_SWITCH
 *
 * と振り分ける。入力受付状態の保証は §1.7 が「外殻レベルで提供する」と
 * 明記している部分で、ここがその実体である。
 */

#include <btron/message.h>
#include <btron/proc.h>
#include <btron/window.h>

/* 枠の寸法。仕様は WDDISP.frame で指定できるとしているが、既定値は
 * 「ウィンドウマネージャにより適当な値が規定されている」。 */
#define WIN_BORDER 2  /* 境界線の幅 */
#define WIN_TITLE_H 18 /* タイトルバーの高さ */
#define WIN_BAR_W 12   /* スクロールバーの幅 */
#define WIN_HANDLE 10  /* ハンドルの一辺 */

/* 変形の最小サイズ。仕様は「システムの規定値」とだけ言っている。
 * 枠とタイトルとバーが収まり、作業領域が残る大きさにした。 */
#define W_MIN_WIDTH 80
#define W_MIN_HEIGHT 60

/* ウィンドウのクラス。小さいほど手前。 */
#define WC_FRONT 0    /* 前面ウィンドウ */
#define WC_PANEL 1    /* パネルウィンドウ */
#define WC_ACTSUB 2   /* 入力受付状態の従属ウィンドウ */
#define WC_ACTMAIN 3  /* 入力受付状態の主ウィンドウ */
#define WC_MAIN 4     /* 入力不可状態の主ウィンドウ */
#define WC_HIDDEN 5   /* 不可視（入力不可の主に従属するもの） */

typedef struct Window {
	Bool used;
	UW attr;
	W parent; /* 主ウィンドウ ID / 親ウィンドウ ID。0 なら無し */
	W pid;    /* 管理プロセス */
	RECT frame; /* 外枠（絶対座標） */
	RECT work;  /* 作業領域（絶対座標） */
	RECT org;   /* 生成元の矩形領域 */
	W pict;
	TC title[W_MAX_TITLE + 1];
	WDDISP disp;
	PAT bgpat;
	Bool has_bgpat;
	GID gid; /* 作業領域の描画環境 */
	W dat;
	RECT wrk;
	Bool redisp;    /* 再表示要求 */
	RECT redisp_r;  /* 再表示すべき領域（相対座標） */
	Bool in_disp;   /* wsta_dsp 〜 wend_dsp の間 */
	Bool panel;     /* パネルウィンドウか */
} Window;

LOCAL Window wnd[W_MAX_WND + 1];

/* 前後関係。同一クラス内の順序を表す。先頭が手前。 */
LOCAL W order[W_MAX_WND];
LOCAL W norder;

/* 画面デバイス名。win_init と wsta_drg が使う。 */
LOCAL CONST TC nm_screen[] = {(TC)'S', (TC)'C', (TC)'R', (TC)'E', (TC)'E',
                              (TC)'N', TNULL};

/* ドラッグ (§3.1.2.5)。システムに 1 つだけ。 */
LOCAL WID drag_wid;  /* 0 ならドラッグしていない */
LOCAL GID drag_gid;
LOCAL W drag_pid;
LOCAL Bool drag_lock;
LOCAL PNT drag_org;  /* ドラッグ描画環境の相対座標の原点（絶対座標） */

LOCAL W active;    /* 入力受付状態の主ウィンドウ。0 なら無し */
LOCAL W act_saved; /* パネルに横取りされる前の主ウィンドウ */
LOCAL GID screen_gid;
LOCAL Bool win_on;

/* ------------------------------------------------------------------ */
/* 補助                                                                */
/* ------------------------------------------------------------------ */

LOCAL Window *get(WID wid)
{
	if (wid < 1 || wid > W_MAX_WND || !wnd[wid].used) {
		return 0;
	}
	return &wnd[wid];
}

LOCAL Bool rect_empty(CONST RECT *r)
{
	return (r->c.left >= r->c.right || r->c.top >= r->c.bottom) ? True
	                                                            : False;
}

LOCAL Bool rect_in(CONST RECT *r, W x, W y)
{
	if (x < r->c.left || x >= r->c.right) {
		return False;
	}
	return (y >= r->c.top && y < r->c.bottom) ? True : False;
}

LOCAL RECT rect_isect(CONST RECT *a, CONST RECT *b)
{
	RECT r;

	r.c.left = (a->c.left > b->c.left) ? a->c.left : b->c.left;
	r.c.top = (a->c.top > b->c.top) ? a->c.top : b->c.top;
	r.c.right = (a->c.right < b->c.right) ? a->c.right : b->c.right;
	r.c.bottom = (a->c.bottom < b->c.bottom) ? a->c.bottom : b->c.bottom;
	return r;
}

/* ウィンドウのクラス。 */
LOCAL W class_of(WID wid)
{
	CONST Window *w = &wnd[wid];

	if ((w->attr & WA_FRONT) != 0) {
		return WC_FRONT;
	}
	if (w->panel) {
		return WC_PANEL;
	}
	if ((w->attr & WA_SUBW) != 0) {
		/* 仕様「従属ウィンドウは、対応する主ウィンドウが入力受付状態の
		 * 場合のみ表示されて『見える』存在となる」 */
		return (w->parent == active) ? WC_ACTSUB : WC_HIDDEN;
	}
	return (wid == active) ? WC_ACTMAIN : WC_MAIN;
}

LOCAL Bool visible(WID wid)
{
	return (class_of(wid) != WC_HIDDEN) ? True : False;
}

/* 手前から順に並べた一覧を作る。戻り値は個数。 */
LOCAL W zsort(W *out)
{
	W n = 0;
	W c;
	W i;

	for (c = WC_FRONT; c <= WC_MAIN; c++) {
		for (i = 0; i < norder; i++) {
			if (wnd[order[i]].used && class_of(order[i]) == c) {
				out[n++] = order[i];
			}
		}
	}
	return n;
}

/* 作業領域の描画環境の座標系と前置長方形を設定し直す。 */
LOCAL void setup_gid(WID wid)
{
	Window *w = &wnd[wid];
	RECT bnd;
	RECT rel;
	RLIST rl[DP_MAX_FORE];
	W z[W_MAX_WND];
	W nz;
	W nfore = 0;
	W me = -1;
	W i;

	if (w->gid < 1) {
		return;
	}
	/* 相対座標の原点を作業領域の左上に合わせる。
	 * DP は「絶対 = 相対 - bounds.left」なので、相対 0 を絶対 work.left に
	 * するには bounds.left = -work.left にする。 */
	(void)gget_bnd(w->gid, &bnd);
	(void)gmov_bnd(w->gid, -w->work.c.left - bnd.c.left,
	               -w->work.c.top - bnd.c.top);
	rel.c.left = 0;
	rel.c.top = 0;
	rel.c.right = (H)(w->work.c.right - w->work.c.left);
	rel.c.bottom = (H)(w->work.c.bottom - w->work.c.top);
	(void)gset_fra(w->gid, rel);
	(void)gset_vis(w->gid, rel);

	/* 自分より手前のウィンドウの矩形を前置長方形リストに入れる。
	 * 座標は**この描画環境の相対座標**に直す。 */
	nz = zsort(z);
	for (i = 0; i < nz; i++) {
		if (z[i] == wid) {
			me = i;
			break;
		}
	}
	if (me > 0) {
		for (i = 0; i < me && nfore < DP_MAX_FORE; i++) {
			RECT f = wnd[z[i]].frame;

			f.c.left = (H)(f.c.left - w->work.c.left);
			f.c.right = (H)(f.c.right - w->work.c.left);
			f.c.top = (H)(f.c.top - w->work.c.top);
			f.c.bottom = (H)(f.c.bottom - w->work.c.top);
			rl[nfore].comp = f;
			rl[nfore].next = 0;
			if (nfore > 0) {
				rl[nfore - 1].next = &rl[nfore];
			}
			nfore++;
		}
	}
	(void)gset_for(w->gid, (nfore > 0) ? &rl[0] : 0);
}

/* 全ウィンドウの前置長方形を入れ直す。前後関係が変わったら呼ぶ。 */
LOCAL void refresh_fore(void)
{
	W i;

	for (i = 1; i <= W_MAX_WND; i++) {
		if (wnd[i].used) {
			setup_gid(i);
		}
	}
}

/* ------------------------------------------------------------------ */
/* 枠の計算 (§3.1.3 / wcnv_rec)                                        */
/* ------------------------------------------------------------------ */

/* 外枠の各辺の厚み。属性で変わる。 */
LOCAL void insets(UW attr, W *l, W *t, W *r, W *b)
{
	W bd = WIN_BORDER;

	*l = bd;
	*t = bd;
	*r = bd;
	*b = bd;
	/* 仕様「WA_SIZE を指定していない場合は、WA_HHDL で左右のウィンドウ枠が
	 * 広くなり、WA_VHDL で上下のウィンドウ枠が広くなる」 */
	if ((attr & WA_SIZE) == 0) {
		if ((attr & WA_HHDL) != 0) {
			*l += 2;
			*r += 2;
		}
		if ((attr & WA_VHDL) != 0) {
			*t += 2;
			*b += 2;
		}
	}
	/* **WA_TITL は「タイトルバー無」。** 0 でバーが有る。 */
	if ((attr & WA_TITL) == 0) {
		*t += WIN_TITLE_H;
	}
	if ((attr & WA_RBAR) != 0) {
		*r += WIN_BAR_W;
	}
	if ((attr & WA_LBAR) != 0) {
		*l += WIN_BAR_W;
	}
	if ((attr & WA_BBAR) != 0) {
		*b += WIN_BAR_W;
	}
}

EXPORT ERR wcnv_rec(UW attr, RECT *r, UW opt)
{
	W l;
	W t;
	W rr;
	W b;

	if (r == 0) {
		return EX_ADR;
	}
	insets(attr, &l, &t, &rr, &b);
	if ((opt & WA_WORK) != 0) {
		/* 作業領域 → 外枠 */
		r->c.left = (H)(r->c.left - l);
		r->c.top = (H)(r->c.top - t);
		r->c.right = (H)(r->c.right + rr);
		r->c.bottom = (H)(r->c.bottom + b);
		return EX_OK;
	}
	if ((opt & WA_FRAME) != 0) {
		/* 外枠 → 作業領域 */
		r->c.left = (H)(r->c.left + l);
		r->c.top = (H)(r->c.top + t);
		r->c.right = (H)(r->c.right - rr);
		r->c.bottom = (H)(r->c.bottom - b);
		return EX_OK;
	}
	return EX_PAR;
}

/* 枠から作業領域を求める。 */
LOCAL void work_of(UW attr, CONST RECT *frame, RECT *work)
{
	W l;
	W t;
	W r;
	W b;

	insets(attr, &l, &t, &r, &b);
	work->c.left = (H)(frame->c.left + l);
	work->c.top = (H)(frame->c.top + t);
	work->c.right = (H)(frame->c.right - r);
	work->c.bottom = (H)(frame->c.bottom - b);
}

/* ------------------------------------------------------------------ */
/* 描画                                                                */
/* ------------------------------------------------------------------ */

/* 画面の描画環境に、手前のウィンドウを除いた前置長方形を入れる。
 * 枠やタイトルを描くのに使う（作業領域の GID は作業領域しか描けない）。 */
LOCAL void screen_fore(WID wid)
{
	RLIST rl[DP_MAX_FORE];
	W z[W_MAX_WND];
	W nz = zsort(z);
	W nfore = 0;
	W me = -1;
	W i;

	for (i = 0; i < nz; i++) {
		if (z[i] == wid) {
			me = i;
			break;
		}
	}
	if (me > 0) {
		for (i = 0; i < me && nfore < DP_MAX_FORE; i++) {
			rl[nfore].comp = wnd[z[i]].frame;
			rl[nfore].next = 0;
			if (nfore > 0) {
				rl[nfore - 1].next = &rl[nfore];
			}
			nfore++;
		}
	}
	(void)gset_for(screen_gid, (nfore > 0) ? &rl[0] : 0);
}

/* 枠・タイトル・スクロールバーを描く。 */
LOCAL void draw_frame(WID wid)
{
	Window *w = &wnd[wid];
	RECT r;
	W l;
	W t;
	W rr;
	W b;
	Bool act;

	if (screen_gid < 1 || !visible(wid)) {
		return;
	}
	act = (class_of(wid) <= WC_ACTMAIN) ? True : False;
	screen_fore(wid);
	insets(w->attr, &l, &t, &rr, &b);

	/* 外枠。入力受付状態なら太く（黒）、そうでなければ薄く（50%）。
	 * 仕様は表示の見え方を規定していないので、入力受付状態が一目で
	 * 分かることだけを守る。 */
	(void)gfra_rec(screen_gid, w->frame, LATTR_MAKE(LN_SOLID, WIN_BORDER),
	               act ? (CONST PAT *)BLACK100 : (CONST PAT *)BLACK50, 0,
	               G_STORE);

	/* タイトルバー。 */
	if ((w->attr & WA_TITL) == 0) {
		r.c.left = (H)(w->frame.c.left + WIN_BORDER);
		r.c.right = (H)(w->frame.c.right - WIN_BORDER);
		r.c.top = (H)(w->frame.c.top + WIN_BORDER);
		r.c.bottom = (H)(r.c.top + WIN_TITLE_H);
		if (!rect_empty(&r)) {
			(void)gfil_rec(screen_gid, r,
			               act ? (CONST PAT *)BLACK25
			                   : (CONST PAT *)WHITE0,
			               0, G_STORE);
			/* タイトル文字。 */
			if (w->title[0] != TNULL) {
				COLOR fg = (w->disp.tlcol != (COLOR)-1) ? w->disp.tlcol
				                                        : COL_BLACK;

				(void)gset_chc(screen_gid, fg, COL_TRANS);
				(void)gdra_stp(screen_gid, r.c.left + 4,
				               r.c.top + WIN_TITLE_H - 4, w->title,
				               W_MAX_TITLE, G_STORE);
			}
		}
	}

	/* 四隅のハンドル。**掴める場所が見えていなければ掴めない。**
	 * wfnd_wnd が返す領域と同じ大きさで描く。 */
	if ((w->attr & WA_SIZE) != 0) {
		RECT h[4];
		W i;

		h[0].c.left = w->frame.c.left;
		h[0].c.top = w->frame.c.top;
		h[1].c.left = (H)(w->frame.c.right - WIN_HANDLE);
		h[1].c.top = w->frame.c.top;
		h[2].c.left = w->frame.c.left;
		h[2].c.top = (H)(w->frame.c.bottom - WIN_HANDLE);
		h[3].c.left = (H)(w->frame.c.right - WIN_HANDLE);
		h[3].c.top = (H)(w->frame.c.bottom - WIN_HANDLE);
		for (i = 0; i < 4; i++) {
			h[i].c.right = (H)(h[i].c.left + WIN_HANDLE);
			h[i].c.bottom = (H)(h[i].c.top + WIN_HANDLE);
			(void)gfil_rec(screen_gid, h[i],
			               act ? (CONST PAT *)BLACK100
			                   : (CONST PAT *)BLACK50,
			               0, G_STORE);
		}
	}

	/* スクロールバー。溝だけ描く（つまみの操作はドラッグ待ち）。 */
	{
		RECT rb;
		RECT bb;
		RECT lb;

		if (wget_bar(wid, &rb, &bb, &lb) == EX_OK) {
			RECT s[3];
			W i;

			s[0] = rb;
			s[1] = bb;
			s[2] = lb;
			for (i = 0; i < 3; i++) {
				RECT a = s[i];

				if (rect_empty(&a)) {
					continue;
				}
				/* wget_bar は相対座標で返すので絶対へ戻す */
				a.c.left = (H)(a.c.left + w->work.c.left);
				a.c.right = (H)(a.c.right + w->work.c.left);
				a.c.top = (H)(a.c.top + w->work.c.top);
				a.c.bottom = (H)(a.c.bottom + w->work.c.top);
				(void)gfil_rec(screen_gid, a, (CONST PAT *)BLACK12, 0,
				               G_STORE);
			}
		}
	}
	(void)gset_for(screen_gid, 0);
}

/* 作業領域を背景パターンで塗る。 */
LOCAL void draw_work(WID wid, CONST RECT *area)
{
	Window *w = &wnd[wid];
	RECT rel;

	if (w->gid < 1 || !visible(wid)) {
		return;
	}
	rel.c.left = 0;
	rel.c.top = 0;
	rel.c.right = (H)(w->work.c.right - w->work.c.left);
	rel.c.bottom = (H)(w->work.c.bottom - w->work.c.top);
	if (area != 0) {
		rel = rect_isect(&rel, area);
	}
	if (rect_empty(&rel)) {
		return;
	}
	(void)gfil_rec(w->gid, rel,
	               w->has_bgpat ? &w->bgpat : (CONST PAT *)WHITE0, 0,
	               G_STORE);
}

/* ウィンドウ 1 枚を描き直す。 */
LOCAL void redraw(WID wid)
{
	draw_frame(wid);
	draw_work(wid, 0);
}

/* 消えた領域を背景（画面）で埋め、下のウィンドウに再表示要求を出す。 */
LOCAL void expose(CONST RECT *area)
{
	W z[W_MAX_WND];
	W nz;
	W i;

	if (screen_gid < 1 || rect_empty(area)) {
		return;
	}
	(void)gset_for(screen_gid, 0);
	(void)gfil_rec(screen_gid, *area, (CONST PAT *)BLACK50, 0, G_STORE);

	nz = zsort(z);
	for (i = 0; i < nz; i++) {
		RECT hit = rect_isect(area, &wnd[z[i]].frame);

		if (!rect_empty(&hit)) {
			(void)wreq_dsp(z[i]);
		}
	}
}

/* ------------------------------------------------------------------ */
/* 入力受付状態                                                        */
/* ------------------------------------------------------------------ */

/* 管理プロセスへ EV_INACT を送る。 */
LOCAL void send_inact(WID wid)
{
	Window *w;
	MESSAGE m;
	WEVENT we;
	UB *dst;
	CONST UB *src;
	W i;

	w = get(wid);
	if (w == 0 || w->pid < 1) {
		return;
	}
	we.g.type = EV_INACT;
	for (i = 0; i < 4; i++) {
		we.g.data[i] = 0;
	}
	we.g.cmd = 0;
	we.g.wid = (H)wid;
	we.g.src = 0;
	m.msg_type = MS_MNG0; /* 外殻予約のタイプ。ウィンドウイベント用 */
	m.msg_size = (W)sizeof(WEVENT);
	dst = (UB *)(VP)&m.msg_body;
	src = (CONST UB *)(CONST VP)&we;
	for (i = 0; i < (W)sizeof(WEVENT); i++) {
		dst[i] = src[i];
	}
	(void)snd_msg(w->pid, &m, NOWAIT);
}

/* 入力受付状態を wid へ移す。 */
LOCAL void activate(WID wid)
{
	W old = active;
	W i;

	if (old == wid) {
		return;
	}
	active = wid;
	/* 仕様「以前に入力受付状態であったウィンドウは入力不可状態となり、
	 * その管理プロセスに対して EV_INACT が送られる」 */
	if (old >= 1 && get(old) != 0) {
		send_inact(old);
	}
	/* 新しく受付状態になった主ウィンドウを同一クラス内の先頭へ。 */
	for (i = 0; i < norder; i++) {
		if (order[i] == wid) {
			W k;

			for (k = i; k > 0; k--) {
				order[k] = order[k - 1];
			}
			order[0] = wid;
			break;
		}
	}
	refresh_fore();
	/* 前後関係が変わったので全部描き直す。 */
	{
		W z[W_MAX_WND];
		W nz = zsort(z);

		for (i = nz - 1; i >= 0; i--) {
			redraw(z[i]);
			(void)wreq_dsp(z[i]);
		}
	}
}

/* ------------------------------------------------------------------ */
/* 生成と削除                                                          */
/* ------------------------------------------------------------------ */

LOCAL WID alloc_wnd(void)
{
	W i;

	for (i = 1; i <= W_MAX_WND; i++) {
		if (!wnd[i].used) {
			return i;
		}
	}
	return 0;
}

LOCAL void push_order(WID wid)
{
	W i;

	if (norder >= W_MAX_WND) {
		return;
	}
	for (i = norder; i > 0; i--) {
		order[i] = order[i - 1];
	}
	order[0] = wid; /* 新しいものが手前 */
	norder++;
}

LOCAL void drop_order(WID wid)
{
	W i;
	W k = 0;

	for (i = 0; i < norder; i++) {
		if (order[i] != wid) {
			order[k++] = order[i];
		}
	}
	norder = k;
}

LOCAL WID open_common(UW attr, UW par, RECT *r, CONST RECT *org, W pict,
                      CONST TC *tit, CONST PAT *bgpat, CONST WDDISP *atr,
                      Bool panel)
{
	WID wid;
	Window *w;
	RECT frame;
	RECT scr;
	W me = prc_self();
	W i;

	if (r == 0) {
		return EX_ADR;
	}
	if (!win_on || screen_gid < 1) {
		return EX_NOSPT; /* 画面が無い構成 */
	}
	/* 指定座標系を外枠に直す。 */
	frame = *r;
	if ((attr & WA_WORK) != 0) {
		(void)wcnv_rec(attr, &frame, WA_WORK);
	} else if ((attr & WA_FRAME) != 0) {
		/* すでに外枠座標 */
	} else {
		/* WA_STD: 正規座標 = 外枠座標として扱う */
	}
	if (rect_empty(&frame)) {
		return EX_PAR;
	}
	(void)gget_bnd(screen_gid, &scr);
	/* 仕様「WA_FMOVE の指定により、オープン時、画面内に強制移動」 */
	if ((attr & WA_FMOVE) != 0) {
		W dx = 0;
		W dy = 0;

		if (frame.c.right > scr.c.right) {
			dx = scr.c.right - frame.c.right;
		}
		if (frame.c.bottom > scr.c.bottom) {
			dy = scr.c.bottom - frame.c.bottom;
		}
		if (frame.c.left + dx < scr.c.left) {
			dx = scr.c.left - frame.c.left;
		}
		if (frame.c.top + dy < scr.c.top) {
			dy = scr.c.top - frame.c.top;
		}
		frame.c.left = (H)(frame.c.left + dx);
		frame.c.right = (H)(frame.c.right + dx);
		frame.c.top = (H)(frame.c.top + dy);
		frame.c.bottom = (H)(frame.c.bottom + dy);
	}
	/* 従属ウィンドウは主ウィンドウが要る。
	 * 仕様「主ウィンドウは同一プロセスにより管理されていなければならず、
	 * そうでない場合はエラー」 */
	if ((attr & WA_SUBW) != 0 && (attr & WA_FRONT) == 0) {
		Window *m = get((W)par);

		if (m == 0 || m->pid != me) {
			return EX_WPRC;
		}
		/* 仕様「アクティブプロセスのみが、従属ウィンドウを生成すること
		 * が可能」 */
		if (active < 1 || wnd[active].pid != me) {
			return EX_WPRC;
		}
	}
	wid = alloc_wnd();
	if (wid == 0) {
		return EX_LIMIT;
	}
	w = &wnd[wid];
	w->attr = attr;
	w->parent = (W)par;
	w->pid = me;
	w->frame = frame;
	work_of(attr, &frame, &w->work);
	if (rect_empty(&w->work)) {
		return EX_PAR; /* 枠が小さすぎて作業領域が残らない */
	}
	w->org.c.left = 0;
	w->org.c.top = 0;
	w->org.c.right = 0;
	w->org.c.bottom = 0;
	if (org != 0 && (attr & WA_SUBW) == 0 && par != 0) {
		w->org = *org;
	}
	w->pict = pict;
	for (i = 0; i <= W_MAX_TITLE; i++) {
		w->title[i] = (tit != 0 && i < W_MAX_TITLE) ? tit[i] : TNULL;
		if (tit == 0 || (i < W_MAX_TITLE && tit[i] == TNULL)) {
			break;
		}
	}
	w->title[W_MAX_TITLE] = TNULL;
	if (atr != 0) {
		w->disp = *atr;
	} else {
		w->disp.frame = WIN_BORDER;
		w->disp.tlbg = 0;
		w->disp.barpat = 0;
		w->disp.barbg = 0;
		w->disp.tlcol = (COLOR)-1; /* 既定 */
	}
	if (bgpat != 0) {
		w->bgpat = *bgpat;
		w->has_bgpat = True;
	} else {
		w->has_bgpat = False;
	}
	w->dat = 0;
	w->wrk = w->org;
	w->redisp = False;
	w->in_disp = False;
	w->panel = panel;

	/* 作業領域の描画環境。画面の描画環境を複製して座標系を絞る。 */
	w->gid = gnew_env(screen_gid);
	if (w->gid < 1) {
		return EX_NOSPC;
	}
	w->used = True;
	push_order(wid);

	/* 仕様「生成したウィンドウは、必ず入力受付状態となり前面に表示される」。
	 * ただし従属ウィンドウと前面ウィンドウでは入力受付状態は変化しない。 */
	if (panel) {
		act_saved = active;
		activate(wid);
	} else if ((attr & (WA_SUBW | WA_FRONT)) == 0) {
		activate(wid);
	} else {
		refresh_fore();
		redraw(wid);
	}
	setup_gid(wid);
	redraw(wid);

	/* 仕様「実行後には、実際に生成されたウィンドウの作業領域の相対座標が
	 * 格納される (左上は常に ( 0, 0 ) となっている)」 */
	r->c.left = 0;
	r->c.top = 0;
	r->c.right = (H)(w->work.c.right - w->work.c.left);
	r->c.bottom = (H)(w->work.c.bottom - w->work.c.top);
	return wid;
}

EXPORT WID wopn_wnd(UW attr, UW par, RECT *r, CONST RECT *org, W pict,
                    CONST TC *tit, CONST PAT *bgpat, CONST WDDISP *atr)
{
	/* 仕様「WA_FRONT = 0 の時のみ有効」なので、前面ウィンドウに
	 * WA_SUBW は意味を持たない。 */
	if ((attr & WA_FRONT) != 0) {
		attr &= ~(UW)WA_SUBW;
	}
	return open_common(attr, par, r, org, pict, tit, bgpat, atr, False);
}

EXPORT WID wopn_pwd(CONST RECT *r)
{
	RECT rr;

	if (r == 0) {
		return EX_ADR;
	}
	rr = *r;
	/* パネルウィンドウはタイトルも変形もスクロールバーも持たない。 */
	return open_common(WA_TITL, 0, &rr, 0, 0, 0, 0, 0, True);
}

EXPORT ERR wcls_wnd(WID wid, W opt)
{
	Window *w = get(wid);
	RECT gone;
	W me = prc_self();
	W i;

	if (w == 0) {
		return EX_WID;
	}
	if (w->pid != me) {
		return EX_WPRC;
	}
	/* 仕様「ドラッグ描画対象のウィンドウが削除された場合…自動的に
	 * wend_drg() の処理が行なわれる」。**残すと解放済みのウィンドウを
	 * 指したまま影を描き続ける。** */
	if (drag_wid == wid) {
		(void)wend_drg();
	}
	gone = w->frame;
	if (w->gid >= 1) {
		(void)gcls_env(w->gid);
		w->gid = 0;
	}
	/* 従属ウィンドウも一緒に閉じる（主ウィンドウが消えたら意味を失う）。 */
	for (i = 1; i <= W_MAX_WND; i++) {
		if (wnd[i].used && i != wid && (wnd[i].attr & WA_SUBW) != 0 &&
		    wnd[i].parent == wid) {
			if (wnd[i].gid >= 1) {
				(void)gcls_env(wnd[i].gid);
			}
			wnd[i].used = False;
			drop_order(i);
		}
	}
	w->used = False;
	drop_order(wid);

	if (w->panel) {
		/* 仕様「パネルウィンドウがクローズされた時点で、入力受付状態は
		 * 元の主ウィンドウに戻される」 */
		active = 0;
		activate(act_saved);
		act_saved = 0;
	} else if (active == wid) {
		/* 次の主ウィンドウへ移す。 */
		W next = 0;

		for (i = 0; i < norder; i++) {
			if (wnd[order[i]].used &&
			    (wnd[order[i]].attr & (WA_SUBW | WA_FRONT)) == 0 &&
			    !wnd[order[i]].panel) {
				next = order[i];
				break;
			}
		}
		active = 0;
		if (next != 0) {
			activate(next);
		} else {
			refresh_fore();
		}
	} else {
		refresh_fore();
	}
	/* 仕様: opt = CLR なら表示をクリアする。 */
	if ((opt & NOCLR) == 0) {
		expose(&gone);
	}
	return EX_OK;
}

/* ------------------------------------------------------------------ */
/* 情報の取得と設定                                                    */
/* ------------------------------------------------------------------ */

EXPORT GID wget_gid(WID wid)
{
	Window *w = get(wid);

	if (w == 0) {
		return EX_WID;
	}
	return w->gid;
}

EXPORT W wget_sts(WID wid, WDSTAT *stat, WDDISP *atr)
{
	Window *w = get(wid);

	if (w == 0) {
		return EX_WID;
	}
	if (stat != 0) {
		stat->attr = w->attr;
		stat->parent = w->parent;
		stat->pid = w->pid;
		stat->r = w->frame;
		stat->wr.c.left = 0;
		stat->wr.c.top = 0;
		stat->wr.c.right = (H)(w->work.c.right - w->work.c.left);
		stat->wr.c.bottom = (H)(w->work.c.bottom - w->work.c.top);
		stat->org = w->org;
	}
	if (atr != 0) {
		*atr = w->disp;
	}
	/* 戻り値は入力受付状態か。 */
	return (class_of(wid) <= WC_ACTMAIN) ? 1 : 0;
}

EXPORT W wget_tit(WID wid, W *pict, TC *title)
{
	Window *w = get(wid);
	W n = 0;

	if (w == 0) {
		return EX_WID;
	}
	if (pict != 0) {
		*pict = w->pict;
	}
	while (w->title[n] != TNULL) {
		n++;
	}
	if (title != 0) {
		W i;

		for (i = 0; i <= n; i++) {
			title[i] = w->title[i];
		}
	}
	return n;
}

EXPORT ERR wset_tit(WID wid, W pict, CONST TC *title, W mode)
{
	Window *w = get(wid);
	W i;

	(void)mode;
	if (w == 0) {
		return EX_WID;
	}
	if (w->pid != prc_self()) {
		return EX_WPRC;
	}
	w->pict = pict;
	for (i = 0; i < W_MAX_TITLE; i++) {
		w->title[i] = (title != 0) ? title[i] : TNULL;
		if (w->title[i] == TNULL) {
			break;
		}
	}
	w->title[W_MAX_TITLE] = TNULL;
	draw_frame(wid);
	return EX_OK;
}

EXPORT ERR wset_bgp(WID wid, CONST PAT *pat)
{
	Window *w = get(wid);

	if (w == 0) {
		return EX_WID;
	}
	if (w->pid != prc_self()) {
		return EX_WPRC;
	}
	if (pat == 0) {
		w->has_bgpat = False;
	} else {
		w->bgpat = *pat;
		w->has_bgpat = True;
	}
	return EX_OK;
}

EXPORT W wget_bgp(WID wid, PAT *pat, W size)
{
	Window *w = get(wid);

	if (w == 0) {
		return EX_WID;
	}
	if (!w->has_bgpat) {
		return 0;
	}
	if (pat != 0 && size >= (W)sizeof(PAT)) {
		*pat = w->bgpat;
	}
	return (W)sizeof(PAT);
}

EXPORT ERR wget_dat(WID wid, W *dat)
{
	Window *w = get(wid);

	if (w == 0) {
		return EX_WID;
	}
	if (dat == 0) {
		return EX_ADR;
	}
	*dat = w->dat;
	return EX_OK;
}

EXPORT ERR wset_dat(WID wid, W dat)
{
	Window *w = get(wid);

	if (w == 0) {
		return EX_WID;
	}
	w->dat = dat;
	return EX_OK;
}

EXPORT ERR wget_wrk(WID wid, RECT *r)
{
	Window *w = get(wid);

	if (w == 0) {
		return EX_WID;
	}
	if (r == 0) {
		return EX_ADR;
	}
	*r = w->wrk;
	return EX_OK;
}

EXPORT ERR wset_wrk(WID wid, CONST RECT *r)
{
	Window *w = get(wid);

	if (w == 0) {
		return EX_WID;
	}
	if (r == 0) {
		return EX_ADR;
	}
	w->wrk = *r;
	return EX_OK;
}

EXPORT ERR wget_org(WID wid, W *parent, RECT *org)
{
	Window *w = get(wid);

	if (w == 0) {
		return EX_WID;
	}
	if (parent != 0) {
		*parent = w->parent;
	}
	if (org != 0) {
		*org = w->org;
	}
	return EX_OK;
}

EXPORT ERR wset_org(WID wid, W parent, CONST RECT *org)
{
	Window *w = get(wid);

	if (w == 0) {
		return EX_WID;
	}
	w->parent = parent;
	if (org != 0) {
		w->org = *org;
	}
	return EX_OK;
}

EXPORT ERR wget_bar(WID wid, RECT *rbar, RECT *bbar, RECT *lbar)
{
	Window *w = get(wid);
	W ww;
	W wh;

	if (w == 0) {
		return EX_WID;
	}
	ww = w->work.c.right - w->work.c.left;
	wh = w->work.c.bottom - w->work.c.top;
	/* 相対座標で返す。バーは作業領域の外側に接して置く。 */
	if (rbar != 0) {
		rbar->c.left = (H)((w->attr & WA_RBAR) ? ww : 0);
		rbar->c.top = 0;
		rbar->c.right = (H)((w->attr & WA_RBAR) ? (ww + WIN_BAR_W) : 0);
		rbar->c.bottom = (H)((w->attr & WA_RBAR) ? wh : 0);
	}
	if (bbar != 0) {
		bbar->c.left = 0;
		bbar->c.top = (H)((w->attr & WA_BBAR) ? wh : 0);
		bbar->c.right = (H)((w->attr & WA_BBAR) ? ww : 0);
		bbar->c.bottom = (H)((w->attr & WA_BBAR) ? (wh + WIN_BAR_W) : 0);
	}
	if (lbar != 0) {
		lbar->c.left = (H)((w->attr & WA_LBAR) ? -WIN_BAR_W : 0);
		lbar->c.top = 0;
		lbar->c.right = 0;
		lbar->c.bottom = (H)((w->attr & WA_LBAR) ? wh : 0);
	}
	return EX_OK;
}

/* ------------------------------------------------------------------ */
/* 位置と大きさ                                                        */
/* ------------------------------------------------------------------ */

LOCAL W move_resize(WID wid, RECT *newr, Bool keep_size)
{
	Window *w = get(wid);
	RECT old;
	RECT frame;

	if (w == 0) {
		return EX_WID;
	}
	if (w->pid != prc_self()) {
		return EX_WPRC;
	}
	if (newr == 0) {
		return EX_ADR;
	}
	/* 仕様「対象ウィンドウが入力受付状態でない場合は、EX_WND のエラー」 */
	if (class_of(wid) > WC_ACTMAIN) {
		return EX_WND;
	}
	old = w->frame;
	frame = *newr;
	if (keep_size) {
		/* 移動。大きさは変えない。 */
		frame.c.right = (H)(frame.c.left + (old.c.right - old.c.left));
		frame.c.bottom = (H)(frame.c.top + (old.c.bottom - old.c.top));
	}
	if (rect_empty(&frame)) {
		return EX_PAR;
	}
	w->frame = frame;
	work_of(w->attr, &frame, &w->work);
	if (rect_empty(&w->work)) {
		w->frame = old;
		work_of(w->attr, &old, &w->work);
		return EX_PAR;
	}
	/* 仕様「移動 / 変形を行なった場合、その対象としたウィンドウが一番
	 * 前面に表示される」 */
	drop_order(wid);
	push_order(wid);
	refresh_fore();
	setup_gid(wid);
	/* 元の場所を消して、動いた先を描く。 */
	expose(&old);
	redraw(wid);
	/* 仕様「対象ウィンドウへの再表示要求イベントは発行されないが、
	 * 再表示領域は設定されるため、関数値が "1" の場合は、
	 * wsta_dsp() 〜 wend_dsp() による再表示を行なう必要がある」。
	 * **だから戻り値が 1 でなければならない。** 0 を返すと、
	 * 呼び出し側は描き直さず、中身が消えたままになる。 */
	(void)wreq_dsp(wid);
	/* 仕様「最終的な新しいウィンドウの外枠が newr で指定した領域に
	 * 絶対座標値で格納される」。入出力である。 */
	*newr = w->frame;
	return 1;
}

EXPORT W wmov_wnd(WID wid, RECT *newr)
{
	return move_resize(wid, newr, True);
}

EXPORT W wrsz_wnd(WID wid, RECT *newr)
{
	return move_resize(wid, newr, False);
}

EXPORT W wchg_wnd(WID wid, RECT *r, W mode)
{
	(void)mode;
	return move_resize(wid, r, False);
}

EXPORT W wscr_wnd(WID wid, CONST RECT *r, W dh, W dv, W mode)
{
	Window *w = get(wid);
	RECT area;

	if (w == 0) {
		return EX_WID;
	}
	if (w->gid < 1) {
		return EX_WND;
	}
	(void)mode;
	if (r != 0) {
		area = *r;
	} else {
		area.c.left = 0;
		area.c.top = 0;
		area.c.right = (H)(w->work.c.right - w->work.c.left);
		area.c.bottom = (H)(w->work.c.bottom - w->work.c.top);
	}
	return gscr_rec(w->gid, area, dh, dv,
	                0, w->has_bgpat ? &w->bgpat : (CONST PAT *)WHITE0);
}

EXPORT ERR wera_wnd(WID wid, CONST RECT *r)
{
	Window *w = get(wid);

	if (w == 0) {
		return EX_WID;
	}
	draw_work(wid, r);
	return EX_OK;
}

/* ------------------------------------------------------------------ */
/* 再表示                                                              */
/* ------------------------------------------------------------------ */

EXPORT ERR wreq_dsp(WID wid)
{
	Window *w = get(wid);

	if (w == 0) {
		return EX_WID;
	}
	/* 仕様「WA_BGDSP 指定のウィンドウにのみ、パネルの消去時に再表示要求が
	 * 送られる」— 入力不可状態で更新しないウィンドウには溜めるだけ。 */
	w->redisp = True;
	w->redisp_r.c.left = 0;
	w->redisp_r.c.top = 0;
	w->redisp_r.c.right = (H)(w->work.c.right - w->work.c.left);
	w->redisp_r.c.bottom = (H)(w->work.c.bottom - w->work.c.top);
	return EX_OK;
}

EXPORT W wchk_dsp(WID wid)
{
	Window *w = get(wid);

	if (w == 0) {
		return EX_WID;
	}
	return w->redisp ? 1 : 0;
}

EXPORT W wsta_dsp(WID wid, RECT *r, RLIST *rlst)
{
	Window *w = get(wid);

	if (w == 0) {
		return EX_WID;
	}
	if (!w->redisp) {
		return 0; /* 再表示すべき領域が無い */
	}
	w->in_disp = True;
	if (r != 0) {
		*r = w->redisp_r;
	}
	if (rlst != 0) {
		rlst->comp = w->redisp_r;
		rlst->next = 0;
	}
	/* 再表示の間は作業領域だけに絞る（枠は巻き込まない）。 */
	(void)gset_clp(w->gid, 0);
	return 1;
}

EXPORT W wend_dsp(WID wid)
{
	Window *w = get(wid);

	if (w == 0) {
		return EX_WID;
	}
	w->in_disp = False;
	w->redisp = False;
	return EX_OK;
}

/* ------------------------------------------------------------------ */
/* 入力受付状態とイベント                                              */
/* ------------------------------------------------------------------ */

EXPORT ERR wswi_wnd(WID wid, CONST WEVENT *evt)
{
	Window *w = get(wid);

	(void)evt;
	if (w == 0) {
		return EX_WID;
	}
	/* 仕様: 入力受付状態は主ウィンドウ間で切り換える。 */
	if ((w->attr & (WA_SUBW | WA_FRONT)) != 0) {
		return EX_WND;
	}
	activate(wid);
	return EX_OK;
}

EXPORT WID wget_act(W *pid)
{
	if (pid != 0) {
		*pid = (active >= 1) ? wnd[active].pid : 0;
	}
	return active;
}

EXPORT W wfnd_wnd(CONST PNT *gpos, PNT *lpos, W *wid)
{
	W z[W_MAX_WND];
	W nz;
	W i;

	if (gpos == 0) {
		return EX_ADR;
	}
	nz = zsort(z);
	for (i = 0; i < nz; i++) {
		Window *w = &wnd[z[i]];
		W l;
		W t;
		W r;
		W b;

		if (!rect_in(&w->frame, gpos->x, gpos->y)) {
			continue;
		}
		if (wid != 0) {
			*wid = z[i];
		}
		if (lpos != 0) {
			lpos->x = (H)(gpos->x - w->work.c.left);
			lpos->y = (H)(gpos->y - w->work.c.top);
		}
		if (rect_in(&w->work, gpos->x, gpos->y)) {
			return W_WORK;
		}
		insets(w->attr, &l, &t, &r, &b);
		/* 四隅のハンドル。**タイトル部より先に見る。**
		 * 後にすると、上辺のハンドルがタイトルバーに食われて
		 * 枠線の 2 ドットしか残らず、事実上つかめない。
		 * 見た目どおり「四隅はハンドル、間がタイトル」にする。 */
		if ((w->attr & WA_SIZE) != 0) {
			Bool left = (gpos->x < w->frame.c.left + WIN_HANDLE) ? True
			                                                     : False;
			Bool top = (gpos->y < w->frame.c.top + WIN_HANDLE) ? True
			                                                   : False;
			Bool right = (gpos->x >= w->frame.c.right - WIN_HANDLE)
			                 ? True
			                 : False;
			Bool bot = (gpos->y >= w->frame.c.bottom - WIN_HANDLE)
			               ? True
			               : False;

			if (left && top) {
				return W_LTHD;
			}
			if (right && top) {
				return W_RTHD;
			}
			if (left && bot) {
				return W_LBHD;
			}
			if (right && bot) {
				return W_RBHD;
			}
		}
		/* タイトル部 */
		if ((w->attr & WA_TITL) == 0 &&
		    gpos->y < w->frame.c.top + WIN_BORDER + WIN_TITLE_H &&
		    gpos->y >= w->frame.c.top + WIN_BORDER) {
			return W_TITL;
		}
		/* スクロールバー */
		if ((w->attr & WA_RBAR) != 0 && gpos->x >= w->work.c.right) {
			return W_RBAR;
		}
		if ((w->attr & WA_LBAR) != 0 && gpos->x < w->work.c.left &&
		    gpos->x >= w->frame.c.left + WIN_BORDER) {
			return W_LBAR;
		}
		if ((w->attr & WA_BBAR) != 0 && gpos->y >= w->work.c.bottom) {
			return W_BBAR;
		}
		return W_FRAM;
	}
	if (wid != 0) {
		*wid = 0;
	}
	if (lpos != 0) {
		*lpos = *gpos;
	}
	return -1; /* どのウィンドウにも当たらない */
}

EXPORT W wlst_wnd(WID wid, W size, W *wids)
{
	W me = prc_self();
	W n = 0;
	W z[W_MAX_WND];
	W nz = zsort(z);
	W i;

	(void)wid;
	for (i = 0; i < nz; i++) {
		if (wnd[z[i]].pid != me) {
			continue;
		}
		if (wids != 0 && n < size) {
			wids[n] = z[i];
		}
		n++;
	}
	return n;
}

EXPORT W wget_evt(WEVENT *evt, W mode)
{
	EVENT ev;
	W t;
	W pos_code;
	W hit = 0;
	PNT lpos;
	W me = prc_self();

	if (evt == 0) {
		return EX_ADR;
	}
	if ((mode & ~(W)(W_NOWAIT | W_NOMSG | W_RAWEVT | W_DRGEVT)) != 0) {
		return EX_PAR;
	}
	for (;;) {
		t = get_evt(EM_ALL, &ev, CLR);
		if (t < 0) {
			return EX_NOSPT; /* イベント管理が使えない */
		}
		if (t == EV_NULL) {
			if ((mode & W_NOWAIT) != 0) {
				/* 仕様「有効なイベントが何も存在していない場合に
				 * 得られるイベントであり、PD の位置するウィンドウ
				 * ID、位置コード、および PD の現在位置が設定されて
				 * いる」。**ヌルイベントにも窓と位置コードが載る。**
				 * 主ループはこれだけで「いまどこを指しているか」が
				 * 分かる。 */
				W nw = 0;
				PNT nl;
				W nc = wfnd_wnd(&ev.pos, &nl, &nw);

				evt->s.type = EV_NULL;
				evt->s.time = ev.time;
				evt->s.pos = (nw != 0 && (mode & W_DRGEVT) == W_DRGEVT)
				                 ? nl
				                 : ev.pos;
				evt->s.cmd = (H)((nc < 0) ? W_WORK : nc);
				evt->s.wid = (H)nw;
				evt->s.stat = ev.stat;
				return EV_NULL;
			}
			/* **待つが、イベントは別のタスクが入れる。**
			 * イベント管理に待ちの口が無いので、ここでは短く遅延して
			 * 回す。本来はウィンドウマネージャがサーバになっていて、
			 * rcv_msg(WAIEVT) + brk_msg で待つ（詳細 E1）。 */
			(void)dly_tsk(10);
			continue;
		}
		break;
	}
	/* **判定の順番が効く。** `W_DRGEVT` (0x0030) は `W_RAWEVT` (0x0020) の
	 * ビットを含んでいるので、RAWEVT を先に見ると DRGEVT が必ず
	 * RAWEVT として処理されてしまう。§1.5 の `M_SYSTEM` が `M_COMMON` を
	 * 含むのと同じ構図（Phase 2 ㊳）。細かい方を先に見る。 */
	if ((mode & W_DRGEVT) == W_DRGEVT) {
		/* パーツ用。作業領域の相対座標で渡す。下で位置を直す。 */
	} else if ((mode & W_RAWEVT) == W_RAWEVT) {
		/* メニュー用。絶対座標の生イベントをそのまま渡す。 */
		evt->e = ev;
		return t;
	}
	if (t != EV_BUTDWN && t != EV_BUTUP) {
		/* キーイベントなどは入力受付状態のウィンドウへそのまま。 */
		evt->e = ev;
		return t;
	}
	pos_code = wfnd_wnd(&ev.pos, &lpos, &hit);
	if (pos_code < 0 || hit == 0) {
		evt->e = ev;
		return t; /* どのウィンドウでもない場所 */
	}
	/* **切換イベントになるのはプレスで、かつ実際に切り換わったときだけ。**
	 *
	 * 仕様 §3.1.2.2 の EV_SWITCH の説明:
	 *   「位置コードの場合は、PD のプレスにより入力受付状態の切換えが
	 *     発生したことを意味し、プレスした位置の位置コード、および
	 *     相対座標位置が入っている」
	 *   「このイベントを取り出した時点では、既に wid で示される
	 *     ウィンドウに入力受付状態は切り換わっており」
	 *
	 * つまり
	 *   - リリース (EV_BUTUP) は常に EV_BUTUP。仕様の EV_BUTUP の説明も
	 *     「PD のリリースにより発生するイベント」で切換には触れていない
	 *   - 入力受付状態のウィンドウを押したら、枠の上でも EV_BUTDWN
	 *     （EV_BUTDWN の説明「対応するウィンドウが現在入力受付状態の
	 *     主ウィンドウまたは従属ウィンドウである場合に得られる」）。
	 *     どこを押したかは wfnd_wnd で引ける
	 *   - cmd は W_SWITCH ではなく**押した位置の位置コード**。W_SWITCH は
	 *     アプリからの要求やパネルのクローズによる切換のときの値
	 *   - 切換は**マネージャが行う**。アプリが wswi_wnd を呼ぶのではない
	 */
	if (t == EV_BUTDWN && class_of(hit) > WC_ACTMAIN) {
		Window *hw = get(hit);

		/* 従属ウィンドウや前面ウィンドウは主ウィンドウ間の切換の
		 * 対象外。activate() が扱えるものだけ切り換える。 */
		if (hw != 0 && (hw->attr & (WA_SUBW | WA_FRONT)) == 0) {
			activate(hit);
		}
		evt->s.type = EV_SWITCH;
		evt->s.time = ev.time;
		evt->s.pos = ((mode & W_DRGEVT) == W_DRGEVT) ? lpos : ev.pos;
		evt->s.cmd = (H)pos_code;
		evt->s.wid = (H)hit;
		evt->s.stat = ev.stat;
		return EV_SWITCH;
	}
	/* 入力受付状態のウィンドウ。生イベントとして渡す。
	 * 相対座標に直して渡す指定なら直す。 */
	evt->e = ev;
	if ((mode & W_DRGEVT) == W_DRGEVT) {
		evt->e.pos = lpos;
	}
	(void)me;
	return t;
}

/* ------------------------------------------------------------------ */
/* ドラッグ (§3.1.2.5 / §3.1.4)                                        */
/* ------------------------------------------------------------------ */

EXPORT GID wsta_drg(WID wid, W lock)
{
	Window *w = get(wid);
	GID g;
	RECT bnd;
	RECT rel;
	RECT scr;

	if (w == 0) {
		return EX_WID;
	}
	/* 仕様「この関数は排他的に実行され、既に他のプロセスがドラッグ描画を
	 * 実行していた場合は、EX_DRAG のエラーとなる」。**同じプロセスでも
	 * 二重には始められない。**影が 2 つ残る。 */
	if (drag_wid != 0) {
		return EX_DRAG;
	}
	if (class_of(wid) > WC_ACTMAIN) {
		return EX_WND;
	}
	if (w->pid != prc_self()) {
		/* 仕様の解説は EX_WPRC、エラーコード一覧は EX_ADR と食い違って
		 * いる。解説の方を採る（一覧は転記の崩れと見た）。 */
		return EX_WPRC;
	}
	g = gopn_dev(nm_screen, 0);
	if (g < 1) {
		return EX_NOSPC;
	}
	/* **座標系は元のウィンドウと同じにする。**こうしておくと、影の座標を
	 * 計算し直さずに済む（仕様 §3.1.2.5 が座標系の一致を要求している）。 */
	(void)gget_bnd(g, &bnd);
	(void)gmov_bnd(g, -w->work.c.left - bnd.c.left, -w->work.c.top - bnd.c.top);

	/* フレーム長方形は画面全体。表示長方形は元の作業領域。 */
	(void)gget_bnd(g, &scr);
	(void)gset_fra(g, scr);
	rel.c.left = 0;
	rel.c.top = 0;
	rel.c.right = (H)(w->work.c.right - w->work.c.left);
	rel.c.bottom = (H)(w->work.c.bottom - w->work.c.top);
	(void)gset_vis(g, rel);

	/* 前置長方形リストは元のウィンドウと同じ。
	 * setup_gid が入れた値をそのまま写す。
	 *
	 * **`gget_for` は連結リストを辿る。**仕様に「リストの各要素の
	 * r_next は正しく設定されているものとする」とある通り、
	 * 鎖を張ってから渡さなければ野良ポインタを踏む。 */
	{
		RLIST rl[DP_MAX_FORE];
		W i;

		for (i = 0; i < DP_MAX_FORE; i++) {
			rl[i].next = (i + 1 < DP_MAX_FORE) ? &rl[i + 1] : 0;
			rl[i].comp.c.left = 0;
			rl[i].comp.c.top = 0;
			rl[i].comp.c.right = 0;
			rl[i].comp.c.bottom = 0;
		}
		if (gget_for(w->gid, rl) == EG_OK) {
			(void)gset_for(g, rl);
		}
	}

	drag_wid = wid;
	drag_gid = g;
	drag_pid = prc_self();
	drag_lock = (lock != 0);
	drag_org.x = w->work.c.left;
	drag_org.y = w->work.c.top;
	return g;
}

EXPORT ERR wend_drg(void)
{
	if (drag_wid == 0) {
		return EX_OK; /* 仕様「行なわれていなかった場合は何もしない」 */
	}
	if (drag_pid != prc_self()) {
		return EX_DRAG;
	}
	if (drag_gid >= 1) {
		(void)gcls_env(drag_gid);
	}
	drag_wid = 0;
	drag_gid = 0;
	drag_pid = 0;
	drag_lock = False;
	return EX_OK;
}

EXPORT W wget_drg(PNT *pos, WEVENT *evt)
{
	EVENT ev;
	PNT p;
	W t;

	if (drag_wid == 0) {
		return EX_DRAG;
	}
	if (get_pdp(&p) < 0) {
		return EX_NOSPT;
	}
	if (pos != 0) {
		pos->x = (H)(p.x - drag_org.x);
		pos->y = (H)(p.y - drag_org.y);
	}
	/* 仕様「EV_REQUEST、EV_SWITCH を除くウィンドウイベントが発生して
	 * いた場合は、そのイベントを取り出し」「その他のイベントが発生した
	 * 場合はイベントキューに残される」。
	 * **マスクで絞ることが「残す」の実装**で、取り出してから戻すのでは
	 * 順序が狂う。 */
	t = get_evt(EM_BUTDWN | EM_BUTUP | EM_KEYDWN | EM_KEYUP | EM_AUTKEY, &ev,
	            CLR);
	if (t < 0) {
		return EX_NOSPT;
	}
	if (evt == 0) {
		return t;
	}
	if (t == EV_NULL) {
		/* 仕様「ウィンドウ ID、位置コード、および発生時刻の値は
		 * 設定されず、PD 位置とメタキー状態のみ設定され、
		 * PD 位置は pos に戻される値と同一となる」 */
		evt->e.type = EV_NULL;
		evt->e.time = 0;
		evt->e.pos.x = (H)(p.x - drag_org.x);
		evt->e.pos.y = (H)(p.y - drag_org.y);
		evt->e.data.info = 0;
		evt->e.stat = ev.stat;
		return EV_NULL;
	}
	evt->e = ev;
	return t;
}

/* ドラッグ中の共通処理 ----------------------------------------------- */

/* 影の矩形を XOR で描く。2 度描けば消える。 */
LOCAL void shadow(GID g, CONST RECT *r)
{
	(void)gfra_rec(g, *r, LATTR_MAKE(LN_SOLID, 2), (CONST PAT *)BLACK100, 0,
	               G_XOR);
}

/* 契機となったイベントを検める。戻り値は位置コード、負ならエラー。 */
LOCAL W drag_event(CONST WEVENT *evt, WID *wid)
{
	W t;
	W code;

	if (evt == 0) {
		return EX_ADR;
	}
	t = evt->s.type;
	/* 仕様「*evt には、ドラッグの契機となった EV_SWITCH または
	 * EV_BUTDWN または EV_RSWITCH のウィンドウイベントを指定する」 */
	if (t != EV_SWITCH && t != EV_BUTDWN && t != EV_RSWITCH) {
		return EX_PAR;
	}
	code = evt->s.cmd;
	*wid = evt->s.wid;
	return code;
}

/* シフトによる拘束方向。0:自由 1:縦 2:横 */
#define DRG_FREE 0
#define DRG_VERT 1
#define DRG_HORZ 2

LOCAL W constrain(UW stat, W dx, W dy, W cur)
{
	if ((stat & (ES_LSHFT | ES_RSHFT)) == 0) {
		return DRG_FREE; /* 仕様「シフトキーを離した場合は、拘束は解除」 */
	}
	if (cur != DRG_FREE) {
		return cur;
	}
	/* 仕様「拘束の方向は、PD の最初の位置から最初に移動した変位の
	 * 長辺の方向となる」 */
	if (dx == 0 && dy == 0) {
		return DRG_FREE;
	}
	{
		W ax = (dx < 0) ? -dx : dx;
		W ay = (dy < 0) ? -dy : dy;

		return (ax >= ay) ? DRG_HORZ : DRG_VERT;
	}
}

EXPORT W wmov_drg(WEVENT *evt, RECT *newr)
{
	WID wid = 0;
	W code = drag_event(evt, &wid);
	Window *w;
	GID g;
	PNT start;
	RECT base;
	RECT cur;
	RECT prev;
	Bool shown = False;
	W dir = DRG_FREE;
	PTRSTS saved;
	W r;

	if (code < 0) {
		return code;
	}
	/* 仕様「このイベントの位置コードは、W_FRAM(1)、W_LTHD(4) 〜
	 * W_RBHD(7) のいずれかでなくてはいけない」 */
	if (code != W_FRAM && (code < W_LTHD || code > W_RBHD)) {
		return EX_PAR;
	}
	w = get(wid);
	if (w == 0) {
		return EX_WID;
	}
	g = wsta_drg(wid, 1);
	if (g < 1) {
		return g;
	}
	/* 影は作業領域をはみ出す。表示長方形を画面全体に広げる
	 * （仕様「元のウィンドウをはみ出た描画を行なう必要がある場合は、
	 * 表示長方形を変更する必要がある」）。 */
	{
		RECT scr;

		(void)gget_bnd(g, &scr);
		(void)gset_vis(g, scr);
	}

	(void)gget_ptr(&saved, 0);
	(void)gset_ptr(PS_GRIP, 0, COL_BLACK, COL_WHITE);

	/* 影の基準。外枠を描画環境の相対座標に直す。 */
	base.c.left = (H)(w->frame.c.left - drag_org.x);
	base.c.top = (H)(w->frame.c.top - drag_org.y);
	base.c.right = (H)(w->frame.c.right - drag_org.x);
	base.c.bottom = (H)(w->frame.c.bottom - drag_org.y);
	(void)wget_drg(&start, 0);
	cur = base;
	prev = base;

	for (;;) {
		PNT p;
		WEVENT we;
		W t = wget_drg(&p, &we);
		W dx;
		W dy;

		if (t < 0) {
			break;
		}
		dx = p.x - start.x;
		dy = p.y - start.y;
		dir = constrain(we.e.stat, dx, dy, dir);
		if (dir == DRG_VERT) {
			dx = 0;
		} else if (dir == DRG_HORZ) {
			dy = 0;
		}
		cur.c.left = (H)(base.c.left + dx);
		cur.c.right = (H)(base.c.right + dx);
		cur.c.top = (H)(base.c.top + dy);
		cur.c.bottom = (H)(base.c.bottom + dy);

		if (!shown || cur.c.left != prev.c.left || cur.c.top != prev.c.top) {
			/* **ポインタを消してから影を動かす。**ポインタは下地を
			 * 退避して持っているので、その上から描くと戻すときに
			 * 影の残骸が出る (§2.6)。 */
			(void)gdsp_ptr(0);
			if (shown) {
				shadow(g, &prev);
			}
			shadow(g, &cur);
			(void)gdsp_ptr(1);
			prev = cur;
			shown = True;
		}
		/* 拘束の向きに合わせてポインタを変える（仕様）。 */
		(void)gset_ptr(dir == DRG_VERT  ? PS_VGRIP
		               : dir == DRG_HORZ ? PS_HGRIP
		                                 : PS_GRIP,
		               0, COL_BLACK, COL_WHITE);

		if (t == EV_BUTUP) {
			break; /* 仕様「リリース時の EV_BUTUP は取り除かれる」 */
		}
		/* EV_KEYDWN / EV_KEYUP / EV_AUTKEY は取り出した時点で
		 * 取り除かれている。無視する（仕様）。 */
		(void)dly_tsk(10);
	}

	(void)gdsp_ptr(0);
	if (shown) {
		shadow(g, &prev);
	}
	(void)gdsp_ptr(1);
	(void)wend_drg();
	(void)gset_ptr(saved.style, 0, saved.fgcol, saved.bgcol);

	/* 影の位置へ動かす。絶対座標に戻す。 */
	{
		RECT nf;

		nf.c.left = (H)(prev.c.left + drag_org.x);
		nf.c.top = (H)(prev.c.top + drag_org.y);
		nf.c.right = (H)(prev.c.right + drag_org.x);
		nf.c.bottom = (H)(prev.c.bottom + drag_org.y);
		r = wmov_wnd(wid, &nf);
		if (r >= 0 && newr != 0) {
			*newr = nf;
		}
	}
	return r;
}

EXPORT W wrsz_drg(WEVENT *evt, CONST RECT *limit, RECT *newr)
{
	WID wid = 0;
	W code = drag_event(evt, &wid);
	Window *w;
	GID g;
	PNT start;
	RECT base;
	RECT cur;
	RECT prev;
	Bool shown = False;
	W dir = DRG_FREE;
	PTRSTS saved;
	W minw = W_MIN_WIDTH;
	W minh = W_MIN_HEIGHT;
	W maxw = 0x7FFF;
	W maxh = 0x7FFF;
	W r;

	if (code < 0) {
		return code;
	}
	/* 仕様「位置コードは、W_LTHD(4)〜W_RBHD(7) のいずれか」。
	 * **移動と違って W_FRAM は受けない。**枠を掴んだら移動である。 */
	if (code < W_LTHD || code > W_RBHD) {
		return EX_PAR;
	}
	w = get(wid);
	if (w == 0) {
		return EX_WID;
	}
	if (limit != 0) {
		/* 仕様「limit->lefttop はウィンドウの最小サイズ
		 * ( lefttop.c.v が縦、lefttop.c.h が横の大きさ )」。
		 * **PNT の x/y ではなく SIZE の h/v として読む**という、
		 * 取り違えやすい約束。 */
		minw = limit->p.lefttop.x;
		minh = limit->p.lefttop.y;
		maxw = limit->p.rightbot.x;
		maxh = limit->p.rightbot.y;
		if (minw < W_MIN_WIDTH) {
			minw = W_MIN_WIDTH;
		}
		if (minh < W_MIN_HEIGHT) {
			minh = W_MIN_HEIGHT;
		}
		if (maxw <= 0) {
			maxw = 0x7FFF;
		}
		if (maxh <= 0) {
			maxh = 0x7FFF;
		}
	}
	g = wsta_drg(wid, 1);
	if (g < 1) {
		return g;
	}
	{
		RECT scr;

		(void)gget_bnd(g, &scr);
		(void)gset_vis(g, scr);
	}
	(void)gget_ptr(&saved, 0);
	(void)gset_ptr(PS_PICK, 0, COL_BLACK, COL_WHITE);

	base.c.left = (H)(w->frame.c.left - drag_org.x);
	base.c.top = (H)(w->frame.c.top - drag_org.y);
	base.c.right = (H)(w->frame.c.right - drag_org.x);
	base.c.bottom = (H)(w->frame.c.bottom - drag_org.y);
	(void)wget_drg(&start, 0);
	cur = base;
	prev = base;

	for (;;) {
		PNT p;
		WEVENT we;
		W t = wget_drg(&p, &we);
		W dx;
		W dy;
		W wdt;
		W hgt;

		if (t < 0) {
			break;
		}
		dx = p.x - start.x;
		dy = p.y - start.y;
		/* 仕様「上下左右ともに変形可のウィンドウに対してのみ有効」 */
		if ((w->attr & (WA_HHDL | WA_VHDL)) == (WA_HHDL | WA_VHDL)) {
			dir = constrain(we.e.stat, dx, dy, dir);
		} else {
			dir = DRG_FREE;
		}
		if (dir == DRG_VERT) {
			dx = 0;
		} else if (dir == DRG_HORZ) {
			dy = 0;
		}
		/* 変形できない向きは動かさない。 */
		if ((w->attr & WA_HHDL) == 0) {
			dx = 0;
		}
		if ((w->attr & WA_VHDL) == 0) {
			dy = 0;
		}
		cur = base;
		/* 位置コードが掴んだ隅を決める (W_LTHD 4 〜 W_RBHD 7)。 */
		if (code == W_LTHD || code == W_LBHD) {
			cur.c.left = (H)(base.c.left + dx);
		} else {
			cur.c.right = (H)(base.c.right + dx);
		}
		if (code == W_LTHD || code == W_RTHD) {
			cur.c.top = (H)(base.c.top + dy);
		} else {
			cur.c.bottom = (H)(base.c.bottom + dy);
		}
		/* 仕様「最大サイズ以上になった場合は…最大サイズでクランプし、
		 * PD の位置に追従しなくなる」。最小も同じ。 */
		wdt = cur.c.right - cur.c.left;
		hgt = cur.c.bottom - cur.c.top;
		if (wdt < minw) {
			wdt = minw;
		}
		if (wdt > maxw) {
			wdt = maxw;
		}
		if (hgt < minh) {
			hgt = minh;
		}
		if (hgt > maxh) {
			hgt = maxh;
		}
		if (code == W_LTHD || code == W_LBHD) {
			cur.c.left = (H)(cur.c.right - wdt);
		} else {
			cur.c.right = (H)(cur.c.left + wdt);
		}
		if (code == W_LTHD || code == W_RTHD) {
			cur.c.top = (H)(cur.c.bottom - hgt);
		} else {
			cur.c.bottom = (H)(cur.c.top + hgt);
		}

		if (!shown || cur.c.left != prev.c.left || cur.c.top != prev.c.top ||
		    cur.c.right != prev.c.right || cur.c.bottom != prev.c.bottom) {
			(void)gdsp_ptr(0);
			if (shown) {
				shadow(g, &prev);
			}
			shadow(g, &cur);
			(void)gdsp_ptr(1);
			prev = cur;
			shown = True;
		}
		(void)gset_ptr(dir == DRG_VERT  ? PS_VPICK
		               : dir == DRG_HORZ ? PS_HPICK
		                                 : PS_PICK,
		               0, COL_BLACK, COL_WHITE);

		if (t == EV_BUTUP) {
			break;
		}
		(void)dly_tsk(10);
	}

	(void)gdsp_ptr(0);
	if (shown) {
		shadow(g, &prev);
	}
	(void)gdsp_ptr(1);
	(void)wend_drg();
	(void)gset_ptr(saved.style, 0, saved.fgcol, saved.bgcol);

	{
		RECT nf;

		nf.c.left = (H)(prev.c.left + drag_org.x);
		nf.c.top = (H)(prev.c.top + drag_org.y);
		nf.c.right = (H)(prev.c.right + drag_org.x);
		nf.c.bottom = (H)(prev.c.bottom + drag_org.y);
		r = wrsz_wnd(wid, &nf);
		if (r >= 0 && newr != 0) {
			*newr = nf;
		}
	}
	return r;
}

EXPORT W wsnd_evt(CONST WEVENT *evt)
{
	EVENT ev;

	if (evt == 0) {
		return EX_ADR;
	}
	ev = evt->e;
	/* ウィンドウイベントは核のイベントキューを通して配る。 */
	if (put_evt(&ev, EP_TIME) < 0) {
		return EX_NOSPC;
	}
	return EX_OK;
}

/* ------------------------------------------------------------------ */
/* 起動と後片付け                                                      */
/* ------------------------------------------------------------------ */

EXPORT ERR win_init(void)
{
	CONST TC dev[] = {(TC)'S', (TC)'C', (TC)'R', (TC)'E', (TC)'E',
	                  (TC)'N', TNULL};
	W i;

	for (i = 0; i <= W_MAX_WND; i++) {
		wnd[i].used = False;
		wnd[i].gid = 0;
	}
	norder = 0;
	active = 0;
	act_saved = 0;
	screen_gid = gopn_dev(dev, 0);
	if (screen_gid < 1) {
		screen_gid = 0;
		return EX_NOSPT; /* 画面が無い構成 */
	}
	win_on = True;
	/* 仕様はイベント管理のマスク設定を外殻の仕事としている (§1.7.2)。
	 * ウィンドウマネージャが立つ時点でボタンとキーを受け付ける。 */
	(void)chg_emk(EM_BUTDWN | EM_BUTUP | EM_KEYDWN | EM_KEYUP |
	              EM_AUTKEY | EM_DEVICE);
	return EX_OK;
}

EXPORT void win_finish(void)
{
	W i;

	if (drag_gid >= 1) {
		(void)gcls_env(drag_gid);
	}
	drag_wid = 0;
	drag_gid = 0;
	drag_pid = 0;
	drag_lock = False;

	for (i = 1; i <= W_MAX_WND; i++) {
		if (wnd[i].used) {
			if (wnd[i].gid >= 1) {
				(void)gcls_env(wnd[i].gid);
			}
			wnd[i].used = False;
		}
	}
	norder = 0;
	active = 0;
	if (screen_gid >= 1) {
		(void)gcls_env(screen_gid);
		screen_gid = 0;
	}
	win_on = False;
}

EXPORT void win_release_process(W pid)
{
	W i;

	if (pid < 1) {
		return;
	}
	/* 仕様「wsta_drg() を実行したプロセスが終了した場合は、自動的に
	 * wend_drg() の処理が行なわれる」。 */
	if (drag_wid != 0 && drag_pid == pid) {
		if (drag_gid >= 1) {
			(void)gcls_env(drag_gid);
		}
		drag_wid = 0;
		drag_gid = 0;
		drag_pid = 0;
		drag_lock = False;
	}
	for (i = 1; i <= W_MAX_WND; i++) {
		if (wnd[i].used && wnd[i].pid == pid) {
			(void)wcls_wnd(i, CLR);
		}
	}
}

EXPORT W win_count(void)
{
	W n = 0;
	W i;

	for (i = 1; i <= W_MAX_WND; i++) {
		if (wnd[i].used) {
			n++;
		}
	}
	return n;
}

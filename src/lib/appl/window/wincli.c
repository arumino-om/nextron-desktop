/*
 * wincli.c — ウィンドウマネージャの利用側
 *
 * **ここは `src/lib` 側（LGPL 予定。J2 / 規律 R14）。** サーバ本体は
 * `src/subsystems/window/`（GPL）で、こちらはアプリケーションが
 * 結合する部分である。規律 R11 のとおり、アプリから見える関数
 * シグネチャは仕様どおりのままで、サーバ呼び出しであることを隠す。
 *
 * 要求の形式は include/btron/winmsg.h。
 *
 * ------------------------------------------------------------------------
 * 描画環境はここが持つ
 * ------------------------------------------------------------------------
 *
 * `wget_gid` が返すのは**このプロセスの中で有効な描画環境 ID** である。
 * ディスプレイプリミティブはプロセスごとのライブラリ（詳細 E1）なので、
 * サーバの GID をもらっても使えない。
 *
 * サーバからは「作業領域の絶対座標」と「手前にあるウィンドウの矩形」
 * （`WinEnv`）だけをもらい、環境はここで組み立てる。前後関係が変わると
 * サーバの世代番号が進むので、それを見て組み直す。
 *
 * **この組み立てが前置長方形リストを張る。** これが無いと、奥の
 * ウィンドウが描画するたびに手前を消してしまう。
 */

#include <btron/window.h>

#include <btron/manager.h>
#include <btron/proc.h>
#include <btron/taskcomm.h>
#include <btron/winmsg.h>

/* ------------------------------------------------------------------ */
/* サーバ呼び出し                                                      */
/* ------------------------------------------------------------------ */

LOCAL ID win_por;

LOCAL W wcall(WinMsg *m)
{
	W try;

	/* **ポートは覚えない。毎回グローバル名から引く。**
	 *
	 * サーバが立て直されるとポート ID は変わる。しかも ID は使い回され
	 * るので、覚えたままだと**別のサーバのポートに話しかけてしまう**。
	 * 誰も受け付けていない番号なら永久に待つし、別のサーバが受け付けて
	 * しまえばでたらめな答えが返る。どちらも原因が見えない。
	 *
	 * 引き直しは `get_nam` 1 回（システムコール 1 回）なので、
	 * ウィンドウ操作の頻度では問題にならない。 */
	for (try = 0; try < 2; try++) {
		ERR er;
		WERR p = mgr_lookup(MGR_WINDOW);

		if (p < 1) {
			return EX_NOSPT; /* ウィンドウマネージャが立っていない */
		}
		win_por = (ID)p;
		m->result = EX_OK;
		/* **`mgr_call` は使えない。** あちらは `MgrMsg` の大きさで
		 * 送るので、`WinMsg` の data 部が切り落とされる。ランデブを
		 * 直に呼ぶ。（切れても前半は通るので、**小さい要求だけが
		 * 動いて見える**という始末の悪い壊れ方をする。） */
		er = cal_por(win_por, MGR_PTN, (VP)m, (W)sizeof(WinMsg),
		             T_FOREVER);
		if (er >= 0) {
			return m->result;
		}
		win_por = 0; /* 引き直して、もう 1 度 */
	}
	return EX_NOSPT;
}

LOCAL void wmsg_init(WinMsg *m, W func)
{
	W i;

	m->func = func;
	m->result = 0;
	for (i = 0; i < 4; i++) {
		m->a[i] = 0;
	}
	m->inlen = 0;
	m->outlen = 0;
}

LOCAL void pack(WinMsg *m, CONST void *src, W len)
{
	CONST UB *s = (CONST UB *)src;
	W i;

	if (m->inlen + len > WIN_MSG_DATA) {
		return;
	}
	for (i = 0; i < len; i++) {
		m->data[m->inlen + i] = s[i];
	}
	m->inlen += len;
}

LOCAL void unpack(CONST WinMsg *m, W off, void *dst, W len)
{
	UB *d = (UB *)dst;
	W i;

	if (dst == 0 || off < 0 || off + len > WIN_MSG_DATA) {
		return;
	}
	for (i = 0; i < len; i++) {
		d[i] = m->data[off + i];
	}
}

#define SZ(t) ((W)sizeof(t))

/* ------------------------------------------------------------------ */
/* 描画環境（このプロセスのもの）                                      */
/* ------------------------------------------------------------------ */

typedef struct CliEnv {
	Bool used;
	WID wid;
	GID gid;
	W gen; /* 組み立てたときのサーバの世代 */
	UD buf; /* 組み立てたときの裏画面。変わったら開き直す */
	BMP bmp;
} CliEnv;

LOCAL CliEnv cenv[W_MAX_WND + 1];
LOCAL GID cli_screen;
LOCAL CONST TC nm_screen[] = {(TC)'S', (TC)'C', (TC)'R', (TC)'E', (TC)'E',
                              (TC)'N', TNULL};

LOCAL W get_env(WID wid, WinEnv *env)
{
	WinMsg m;
	W r;

	wmsg_init(&m, WF_get_env);
	m.a[0] = wid;
	r = wcall(&m);
	if (r >= 0) {
		unpack(&m, 0, env, SZ(WinEnv));
	}
	return r;
}

/* サーバからもらった情報で描画環境を組み立てる。
 *
 * **描画先はウィンドウごとの裏画面**（詳細 E2）。共有メモリなので
 * 番地をそのまま受け取れる。相対 (0,0) が作業領域の左上に来るように
 * 座標系をずらす —— ビットマップの列 0 は外枠の左端なので、
 * bounds.left = -(work.left - frame.left) になる。 */
LOCAL Bool open_env(CliEnv *c, CONST WinEnv *env)
{
	if (env->buf == 0) {
		return False;
	}
	c->bmp.planes = 1;
	c->bmp.pixbits = (UH)env->pixbits;
	c->bmp.rowbytes = (UH)env->rowbytes;
	c->bmp.bounds.c.left = 0;
	c->bmp.bounds.c.top = 0;
	c->bmp.bounds.c.right = (H)env->bufw;
	c->bmp.bounds.c.bottom = (H)env->bufh;
	c->bmp.baseaddr[0] = (UB *)(VP)env->buf;
	if (c->gid >= 1) {
		(void)gcls_env(c->gid);
		c->gid = 0;
	}
	/* 画面と同じデバイスを指定する。カラー情報が揃っていないと、
	 * 同じ COLOR が画面と裏画面で違う画素値になる。 */
	c->gid = gopn_mem(nm_screen, &c->bmp, 0);
	if (c->gid < 1) {
		return False;
	}
	c->buf = env->buf;
	return True;
}

LOCAL void setup_env(CliEnv *c, CONST WinEnv *env)
{
	RECT bnd;
	RECT rel;
	W dx = env->work.c.left - env->frame.c.left;
	W dy = env->work.c.top - env->frame.c.top;

	(void)gget_bnd(c->gid, &bnd);
	(void)gmov_bnd(c->gid, -dx - bnd.c.left, -dy - bnd.c.top);
	rel.c.left = 0;
	rel.c.top = 0;
	rel.c.right = (H)(env->work.c.right - env->work.c.left);
	rel.c.bottom = (H)(env->work.c.bottom - env->work.c.top);
	(void)gset_fra(c->gid, rel);
	(void)gset_vis(c->gid, rel);

	/* **前置長方形リストは空。** 自分のビットマップに手前も奥も無い。
	 * サーバも空を返してくる（詳細 E2）。 */
	(void)gset_for(c->gid, 0);
	c->gen = env->gen;
}

EXPORT GID wget_gid(WID wid)
{
	WinEnv env;
	CliEnv *c;

	if (wid < 1 || wid > W_MAX_WND) {
		return EX_WID;
	}
	if (get_env(wid, &env) < 0) {
		return EX_WID;
	}
	c = &cenv[wid];
	if (!c->used) {
		c->gid = 0;
		c->buf = 0;
		c->used = True;
		c->wid = wid;
		c->gen = 0;
	}
	/* **裏画面が取り直されていたら開き直す。** 変形すると番地が変わる。
	 * 古い番地へ描き続けると、解放済みの共有メモリを踏む。 */
	if (c->gid < 1 || c->buf != env.buf) {
		if (!open_env(c, &env)) {
			c->used = False;
			return EX_NOSPT;
		}
		c->gen = 0;
	}
	/* 世代が変わっていたら座標系を組み直す（移動・変形）。 */
	if (c->gen != env.gen) {
		setup_env(c, &env);
	}
	return c->gid;
}

/* ウィンドウを閉じたら環境も返す。 */
LOCAL void drop_env(WID wid)
{
	if (wid >= 1 && wid <= W_MAX_WND && cenv[wid].used) {
		if (cenv[wid].gid >= 1) {
			(void)gcls_env(cenv[wid].gid);
		}
		cenv[wid].used = False;
		cenv[wid].gid = 0;
		cenv[wid].buf = 0;
	}
}

/* ------------------------------------------------------------------ */
/* 仕様の関数 (§3.1)                                                   */
/* ------------------------------------------------------------------ */

/* 開いているウィンドウの数。**BTRON3 の API ではない。** 検証用。 */
EXPORT W win_count(void)
{
	WinMsg m;

	wmsg_init(&m, WF_count);
	return wcall(&m);
}

EXPORT ERR wcnv_rec(UW attr, RECT *r, UW opt)
{
	WinMsg m;
	W res;

	if (r == 0) {
		return EX_ADR;
	}
	wmsg_init(&m, WF_cnv_rec);
	m.a[0] = (W)attr;
	m.a[1] = (W)opt;
	pack(&m, r, SZ(RECT));
	res = wcall(&m);
	if (res >= 0) {
		unpack(&m, 0, r, SZ(RECT));
	}
	return res;
}

EXPORT WID wopn_wnd(UW attr, UW par, RECT *r, CONST RECT *org, W pict,
                    CONST TC *title, CONST PAT *bgpat, CONST WDDISP *atr)
{
	WinMsg m;
	W res;
	W flags = 0;

	if (r == 0) {
		return EX_ADR;
	}
	wmsg_init(&m, WF_opn_wnd);
	m.a[0] = (W)attr;
	m.a[1] = (W)par;
	m.a[3] = pict;
	pack(&m, r, SZ(RECT));
	if (org != 0) {
		flags |= 1;
		pack(&m, org, SZ(RECT));
	}
	if (title != 0) {
		TC t[W_MAX_TITLE + 1];
		W i;

		flags |= 2;
		for (i = 0; i <= W_MAX_TITLE; i++) {
			t[i] = (i < W_MAX_TITLE) ? title[i] : TNULL;
			if (t[i] == TNULL) {
				for (; i <= W_MAX_TITLE; i++) {
					t[i] = TNULL;
				}
				break;
			}
		}
		pack(&m, t, (W)sizeof(t));
	}
	if (bgpat != 0) {
		flags |= 4;
		pack(&m, bgpat, SZ(PAT));
	}
	if (atr != 0) {
		flags |= 8;
		pack(&m, atr, SZ(WDDISP));
	}
	m.a[2] = flags;
	res = wcall(&m);
	if (res >= 0) {
		unpack(&m, 0, r, SZ(RECT));
	}
	return res;
}

EXPORT WID wopn_pwd(CONST RECT *r)
{
	WinMsg m;

	if (r == 0) {
		return EX_ADR;
	}
	wmsg_init(&m, WF_opn_pwd);
	pack(&m, r, SZ(RECT));
	return wcall(&m);
}

EXPORT ERR wcls_wnd(WID wid, W opt)
{
	WinMsg m;
	W res;

	wmsg_init(&m, WF_cls_wnd);
	m.a[0] = wid;
	m.a[1] = opt;
	res = wcall(&m);
	if (res >= 0) {
		drop_env(wid);
	}
	return res;
}

EXPORT W wget_sts(WID wid, WDSTAT *stat, WDDISP *atr)
{
	WinMsg m;
	W res;

	wmsg_init(&m, WF_get_sts);
	m.a[0] = wid;
	res = wcall(&m);
	if (res >= 0) {
		unpack(&m, 0, stat, SZ(WDSTAT));
		unpack(&m, SZ(WDSTAT), atr, SZ(WDDISP));
	}
	return res;
}

EXPORT W wget_tit(WID wid, W *pict, TC *title)
{
	WinMsg m;
	W res;

	wmsg_init(&m, WF_get_tit);
	m.a[0] = wid;
	res = wcall(&m);
	if (res >= 0) {
		unpack(&m, 0, pict, SZ(W));
		unpack(&m, SZ(W), title, (W)((W_MAX_TITLE + 1) * 2));
	}
	return res;
}

EXPORT ERR wset_tit(WID wid, W pict, CONST TC *title, W mode)
{
	WinMsg m;

	wmsg_init(&m, WF_set_tit);
	m.a[0] = wid;
	m.a[1] = pict;
	m.a[2] = mode;
	if (title != 0) {
		TC t[W_MAX_TITLE + 1];
		W i;
		Bool end = False;

		for (i = 0; i <= W_MAX_TITLE; i++) {
			TC c = 0;

			if (!end && i < W_MAX_TITLE) {
				c = title[i];
				if (c == TNULL) {
					end = True;
					c = 0;
				}
			}
			t[i] = c;
		}
		pack(&m, t, (W)sizeof(t));
	}
	return wcall(&m);
}

EXPORT ERR wset_bgp(WID wid, CONST PAT *pat)
{
	WinMsg m;

	wmsg_init(&m, WF_set_bgp);
	m.a[0] = wid;
	if (pat != 0) {
		pack(&m, pat, SZ(PAT));
	}
	return wcall(&m);
}

EXPORT W wget_bgp(WID wid, PAT *pat, W size)
{
	WinMsg m;
	W res;

	wmsg_init(&m, WF_get_bgp);
	m.a[0] = wid;
	m.a[1] = size;
	res = wcall(&m);
	if (res >= 0 && pat != 0 && size >= SZ(PAT)) {
		unpack(&m, 0, pat, SZ(PAT));
	}
	return res;
}

EXPORT ERR wget_dat(WID wid, W *dat)
{
	WinMsg m;
	W res;

	wmsg_init(&m, WF_get_dat);
	m.a[0] = wid;
	res = wcall(&m);
	if (res >= 0) {
		unpack(&m, 0, dat, SZ(W));
	}
	return res;
}

EXPORT ERR wset_dat(WID wid, W dat)
{
	WinMsg m;

	wmsg_init(&m, WF_set_dat);
	m.a[0] = wid;
	m.a[1] = dat;
	return wcall(&m);
}

EXPORT ERR wget_wrk(WID wid, RECT *r)
{
	WinMsg m;
	W res;

	wmsg_init(&m, WF_get_wrk);
	m.a[0] = wid;
	res = wcall(&m);
	if (res >= 0) {
		unpack(&m, 0, r, SZ(RECT));
	}
	return res;
}

EXPORT ERR wset_wrk(WID wid, CONST RECT *r)
{
	WinMsg m;

	if (r == 0) {
		return EX_ADR;
	}
	wmsg_init(&m, WF_set_wrk);
	m.a[0] = wid;
	pack(&m, r, SZ(RECT));
	return wcall(&m);
}

EXPORT ERR wget_org(WID wid, W *parent, RECT *org)
{
	WinMsg m;
	W res;

	wmsg_init(&m, WF_get_org);
	m.a[0] = wid;
	res = wcall(&m);
	if (res >= 0) {
		unpack(&m, 0, parent, SZ(W));
		unpack(&m, SZ(W), org, SZ(RECT));
	}
	return res;
}

EXPORT ERR wset_org(WID wid, W parent, CONST RECT *org)
{
	WinMsg m;

	if (org == 0) {
		return EX_ADR;
	}
	wmsg_init(&m, WF_set_org);
	m.a[0] = wid;
	m.a[1] = parent;
	pack(&m, org, SZ(RECT));
	return wcall(&m);
}

EXPORT ERR wget_bar(WID wid, RECT *rbar, RECT *bbar, RECT *lbar)
{
	WinMsg m;
	W res;

	wmsg_init(&m, WF_get_bar);
	m.a[0] = wid;
	res = wcall(&m);
	if (res >= 0) {
		unpack(&m, 0, rbar, SZ(RECT));
		unpack(&m, SZ(RECT), bbar, SZ(RECT));
		unpack(&m, 2 * SZ(RECT), lbar, SZ(RECT));
	}
	return res;
}

LOCAL W move_like(W func, WID wid, RECT *newr, W mode)
{
	WinMsg m;
	W res;

	if (newr == 0) {
		return EX_ADR;
	}
	wmsg_init(&m, func);
	m.a[0] = wid;
	m.a[1] = mode;
	pack(&m, newr, SZ(RECT));
	res = wcall(&m);
	if (res >= 0) {
		unpack(&m, 0, newr, SZ(RECT));
	}
	return res;
}

EXPORT W wmov_wnd(WID wid, RECT *newr)
{
	return move_like(WF_mov_wnd, wid, newr, 0);
}

EXPORT W wrsz_wnd(WID wid, RECT *newr)
{
	return move_like(WF_rsz_wnd, wid, newr, 0);
}

EXPORT W wchg_wnd(WID wid, RECT *r, W mode)
{
	return move_like(WF_chg_wnd, wid, r, mode);
}

EXPORT W wscr_wnd(WID wid, CONST RECT *r, W dh, W dv, W mode)
{
	WinMsg m;

	wmsg_init(&m, WF_scr_wnd);
	m.a[0] = wid;
	m.a[1] = dh;
	m.a[2] = dv;
	m.a[3] = mode;
	if (r != 0) {
		pack(&m, r, SZ(RECT));
	}
	return wcall(&m);
}

EXPORT ERR wera_wnd(WID wid, CONST RECT *r)
{
	WinMsg m;

	wmsg_init(&m, WF_era_wnd);
	m.a[0] = wid;
	if (r != 0) {
		pack(&m, r, SZ(RECT));
	}
	return wcall(&m);
}

EXPORT ERR wreq_dsp(WID wid)
{
	WinMsg m;

	wmsg_init(&m, WF_req_dsp);
	m.a[0] = wid;
	return wcall(&m);
}

EXPORT W wchk_dsp(WID wid)
{
	WinMsg m;

	wmsg_init(&m, WF_chk_dsp);
	m.a[0] = wid;
	return wcall(&m);
}

EXPORT W wsta_dsp(WID wid, RECT *r, RLIST *rlst)
{
	WinMsg m;
	W res;

	(void)rlst; /* 矩形リストは前置長方形リストで代用している */
	wmsg_init(&m, WF_sta_dsp);
	m.a[0] = wid;
	res = wcall(&m);
	if (res >= 0) {
		unpack(&m, 0, r, SZ(RECT));
		/* **再表示の前に描画環境を組み直す。**
		 * 前後関係が変わったからこその再表示なので、ここが一番
		 * 確実な組み直しの機会になる（仕様の `wsta_dsp` が矩形
		 * リストを返すのと同じ役割）。 */
		(void)wget_gid(wid);
	}
	return res;
}

EXPORT W wend_dsp(WID wid)
{
	WinMsg m;

	wmsg_init(&m, WF_end_dsp);
	m.a[0] = wid;
	return wcall(&m);
}

EXPORT ERR wswi_wnd(WID wid, CONST WEVENT *evt)
{
	WinMsg m;

	wmsg_init(&m, WF_swi_wnd);
	m.a[0] = wid;
	if (evt != 0) {
		pack(&m, evt, SZ(WEVENT));
	}
	return wcall(&m);
}

EXPORT WID wget_act(W *pid)
{
	WinMsg m;
	W res;

	wmsg_init(&m, WF_get_act);
	res = wcall(&m);
	if (res >= 0) {
		unpack(&m, 0, pid, SZ(W));
	}
	return res;
}

EXPORT W wfnd_wnd(CONST PNT *gpos, PNT *lpos, W *wid)
{
	WinMsg m;
	W res;

	if (gpos == 0) {
		return EX_ADR;
	}
	wmsg_init(&m, WF_fnd_wnd);
	pack(&m, gpos, SZ(PNT));
	res = wcall(&m);
	if (res >= 0 || res == -1) {
		unpack(&m, 0, lpos, SZ(PNT));
		unpack(&m, SZ(PNT), wid, SZ(W));
	}
	return res;
}

EXPORT W wlst_wnd(WID wid, W size, W *wids)
{
	WinMsg m;
	W res;

	wmsg_init(&m, WF_lst_wnd);
	m.a[0] = wid;
	m.a[1] = size;
	res = wcall(&m);
	if (res >= 0 && wids != 0 && size > 0) {
		W n = (size < res) ? size : res;

		if (n * SZ(W) > WIN_MSG_DATA) {
			n = WIN_MSG_DATA / SZ(W);
		}
		unpack(&m, 0, wids, n * SZ(W));
	}
	return res;
}

EXPORT W wget_evt(WEVENT *evt, W mode)
{
	WinMsg m;
	W res;

	if (evt == 0) {
		return EX_ADR;
	}
	if ((mode & ~(W)(W_NOWAIT | W_NOMSG | W_RAWEVT | W_DRGEVT)) != 0) {
		return EX_PAR;
	}
	for (;;) {
		wmsg_init(&m, WF_get_evt);
		m.a[0] = mode;
		res = wcall(&m);
		if (res < 0) {
			return res;
		}
		unpack(&m, 0, evt, SZ(WEVENT));
		if (res != EV_NULL || (mode & W_NOWAIT) != 0) {
			return res;
		}
		/* **待つのは利用側。** サーバを待たせると、その間ほかの
		 * アプリケーションの要求が一切通らなくなる。 */
		(void)dly_tsk(10);
	}
}

EXPORT W wsnd_evt(CONST WEVENT *evt)
{
	WinMsg m;

	if (evt == 0) {
		return EX_ADR;
	}
	wmsg_init(&m, WF_snd_evt);
	pack(&m, evt, SZ(WEVENT));
	return wcall(&m);
}

/* ドラッグ「影」を描くための環境。これも**呼んだプロセスのもの**。 */
LOCAL GID cli_drag;

EXPORT GID wsta_drg(WID wid, W lock)
{
	WinMsg m;
	WinEnv env;
	RECT bnd;
	RECT scr;
	RLIST rl[WIN_MAX_ENVFORE];
	W res;
	W i;

	wmsg_init(&m, WF_sta_drg);
	m.a[0] = wid;
	m.a[1] = lock;
	res = wcall(&m);
	if (res < 0) {
		return res;
	}
	/* サーバが返す GID は**サーバの中でしか意味を持たない**ので捨てる。
	 * こちらで同じ約束の環境を組む（仕様 §3.1.2.5）。
	 *
	 *   フレーム長方形   画面全体
	 *   表示長方形       元のウィンドウの作業領域
	 *   前置長方形リスト 元のウィンドウと同じ
	 *   座標系           元のウィンドウと同じ */
	if (get_env(wid, &env) < 0) {
		return EX_WID;
	}
	if (cli_screen < 1) {
		cli_screen = gopn_dev(nm_screen, 0);
		if (cli_screen < 1) {
			return EX_NOSPT;
		}
	}
	if (cli_drag < 1) {
		cli_drag = gnew_env(cli_screen);
		if (cli_drag < 1) {
			return EX_NOSPC;
		}
	}
	(void)gget_bnd(cli_drag, &bnd);
	(void)gmov_bnd(cli_drag, -env.work.c.left - bnd.c.left,
	               -env.work.c.top - bnd.c.top);
	(void)gget_bnd(cli_drag, &scr);
	(void)gset_fra(cli_drag, scr);
	{
		RECT rel;

		rel.c.left = 0;
		rel.c.top = 0;
		rel.c.right = (H)(env.work.c.right - env.work.c.left);
		rel.c.bottom = (H)(env.work.c.bottom - env.work.c.top);
		(void)gset_vis(cli_drag, rel);
	}
	for (i = 0; i < env.nfore && i < WIN_MAX_ENVFORE; i++) {
		RECT f = env.fore[i];

		f.c.left = (H)(f.c.left - env.work.c.left);
		f.c.right = (H)(f.c.right - env.work.c.left);
		f.c.top = (H)(f.c.top - env.work.c.top);
		f.c.bottom = (H)(f.c.bottom - env.work.c.top);
		rl[i].comp = f;
		rl[i].next = (i + 1 < env.nfore) ? &rl[i + 1] : 0;
	}
	(void)gset_for(cli_drag, (env.nfore > 0) ? &rl[0] : 0);
	return cli_drag;
}

EXPORT ERR wend_drg(void)
{
	WinMsg m;
	W res;

	wmsg_init(&m, WF_end_drg);
	res = wcall(&m);
	if (cli_drag >= 1) {
		(void)gcls_env(cli_drag);
		cli_drag = 0;
	}
	return res;
}

EXPORT W wget_drg(PNT *pos, WEVENT *evt)
{
	WinMsg m;
	W res;

	wmsg_init(&m, WF_get_drg);
	res = wcall(&m);
	if (res >= 0) {
		unpack(&m, 0, pos, SZ(PNT));
		unpack(&m, SZ(PNT), evt, SZ(WEVENT));
	}
	return res;
}

EXPORT W wmov_drg(WEVENT *evt, RECT *newr)
{
	WinMsg m;
	W res;

	if (evt == 0) {
		return EX_ADR;
	}
	wmsg_init(&m, WF_mov_drg);
	pack(&m, evt, SZ(WEVENT));
	res = wcall(&m);
	if (res >= 0) {
		unpack(&m, 0, newr, SZ(RECT));
	}
	return res;
}

EXPORT W wrsz_drg(WEVENT *evt, CONST RECT *limit, RECT *newr)
{
	WinMsg m;
	W res;

	if (evt == 0) {
		return EX_ADR;
	}
	wmsg_init(&m, WF_rsz_drg);
	pack(&m, evt, SZ(WEVENT));
	if (limit != 0) {
		m.a[0] = 1;
		pack(&m, limit, SZ(RECT));
	}
	res = wcall(&m);
	if (res >= 0) {
		unpack(&m, 0, newr, SZ(RECT));
	}
	return res;
}

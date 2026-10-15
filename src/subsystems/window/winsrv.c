/*
 * winsrv.c — ウィンドウマネージャのサーバ
 *
 * 詳細 E22「1 プロセス 1 サーバ」。ウィンドウマネージャは 1 つのサーバで、
 * アプリケーションはランデブで要求を出す。形式は include/btron/winmsg.h。
 *
 * ------------------------------------------------------------------------
 * なぜ配りを 1 本ずつ書くのか
 * ------------------------------------------------------------------------
 *
 * システムコールの配り (src/kernel/core/syscall.c) と同じ理由である。
 * 引数の型と個数が関数ごとに違うので、表にまとめると「何をどう詰めるか」が
 * 見えなくなる。利用側 (wincli.c) と**対になっている**ことが読んで
 * 分かる形にしておきたい。片方だけ直すと黙って壊れる。
 *
 * ------------------------------------------------------------------------
 * 待つのは利用側
 * ------------------------------------------------------------------------
 *
 * `wget_evt` を `W_WAIT` で呼ばれたとき、**サーバが待ってはいけない。**
 * 待っている間、他のアプリケーションの要求が一切通らなくなる。
 * サーバは常に「いま取れるか」だけを答え、待つのは利用側の仕事にする。
 * 仕様が想定する形（詳細 E1 の `rcv_msg(WAIEVT)` + `brk_msg`）に
 * 置き換えるのは、メッセージ待ちの口ができてからでよい。
 */

#include "winsrv.h"

#include <btron/gname.h>
#include <btron/manager.h>
#include <btron/proc.h>
#include <btron/syscall.h>
#include <btron/taskcomm.h>

LOCAL ID win_por;

/* data から取り出す / data へ詰める小道具。
 * **境界を越えないことをここで一度だけ確かめる。** */
LOCAL Bool take(CONST WinMsg *m, W off, VP dst, W len)
{
	CONST UB *src = m->data + off;
	UB *d = (UB *)dst;
	W i;

	if (off < 0 || len < 0 || off + len > m->inlen ||
	    off + len > WIN_MSG_DATA) {
		return False;
	}
	for (i = 0; i < len; i++) {
		d[i] = src[i];
	}
	return True;
}

LOCAL Bool put(WinMsg *m, W off, CONST VP src, W len)
{
	CONST UB *s = (CONST UB *)src;
	W i;

	if (off < 0 || len < 0 || off + len > WIN_MSG_DATA) {
		return False;
	}
	for (i = 0; i < len; i++) {
		m->data[off + i] = s[i];
	}
	if (off + len > m->outlen) {
		m->outlen = off + len;
	}
	return True;
}

#define SZ(t) ((W)sizeof(t))

LOCAL void dispatch(WinMsg *m)
{
	m->outlen = 0;
	switch (m->func) {
	case WF_cnv_rec: {
		RECT r;

		if (!take(m, 0, &r, SZ(RECT))) {
			m->result = EX_ADR;
			break;
		}
		m->result = wsv_cnv_rec((UW)m->a[0], &r, (UW)m->a[1]);
		(void)put(m, 0, &r, SZ(RECT));
		break;
	}
	case WF_opn_wnd: {
		RECT r;
		RECT org;
		TC title[W_MAX_TITLE + 1];
		PAT bg;
		WDDISP di;
		W off = 0;

		if (!take(m, 0, &r, SZ(RECT))) {
			m->result = EX_ADR;
			break;
		}
		off = SZ(RECT);
		/* **a[2] のビットで「どれが指定されたか」を伝える。**
		 * NULL かどうかで振る舞いが変わる引数があるので、
		 * 「詰めた長さ」だけでは足りない。 */
		if ((m->a[2] & 1) != 0) {
			(void)take(m, off, &org, SZ(RECT));
			off += SZ(RECT);
		}
		if ((m->a[2] & 2) != 0) {
			(void)take(m, off, title, (W)sizeof(title));
			off += (W)sizeof(title);
		}
		if ((m->a[2] & 4) != 0) {
			(void)take(m, off, &bg, SZ(PAT));
			off += SZ(PAT);
		}
		if ((m->a[2] & 8) != 0) {
			(void)take(m, off, &di, SZ(WDDISP));
			off += SZ(WDDISP);
		}
		m->result = wsv_opn_wnd((UW)m->a[0], (UW)m->a[1], &r,
		                        ((m->a[2] & 1) != 0) ? &org : 0, m->a[3],
		                        ((m->a[2] & 2) != 0) ? title : 0,
		                        ((m->a[2] & 4) != 0) ? &bg : 0,
		                        ((m->a[2] & 8) != 0) ? &di : 0);
		(void)put(m, 0, &r, SZ(RECT));
		break;
	}
	case WF_opn_pwd: {
		RECT r;

		if (!take(m, 0, &r, SZ(RECT))) {
			m->result = EX_ADR;
			break;
		}
		m->result = wsv_opn_pwd(&r);
		break;
	}
	case WF_cls_wnd:
		m->result = wsv_cls_wnd(m->a[0], m->a[1]);
		break;
	case WF_get_sts: {
		WDSTAT st;
		WDDISP di;

		m->result = wsv_get_sts(m->a[0], &st, &di);
		(void)put(m, 0, &st, SZ(WDSTAT));
		(void)put(m, SZ(WDSTAT), &di, SZ(WDDISP));
		break;
	}
	case WF_get_tit: {
		W pict = 0;
		TC title[W_MAX_TITLE + 1];

		m->result = wsv_get_tit(m->a[0], &pict, title);
		(void)put(m, 0, &pict, SZ(W));
		(void)put(m, SZ(W), title, (W)sizeof(title));
		break;
	}
	case WF_set_tit: {
		TC title[W_MAX_TITLE + 1];

		if (m->inlen > 0 && !take(m, 0, title, (W)sizeof(title))) {
			m->result = EX_ADR;
			break;
		}
		m->result = wsv_set_tit(m->a[0], m->a[1],
		                        (m->inlen > 0) ? title : 0, m->a[2]);
		break;
	}
	case WF_set_bgp: {
		PAT pat;

		if (m->inlen > 0 && !take(m, 0, &pat, SZ(PAT))) {
			m->result = EX_ADR;
			break;
		}
		m->result = wsv_set_bgp(m->a[0], (m->inlen > 0) ? &pat : 0);
		break;
	}
	case WF_get_bgp: {
		PAT pat;

		m->result = wsv_get_bgp(m->a[0], &pat, SZ(PAT));
		(void)put(m, 0, &pat, SZ(PAT));
		break;
	}
	case WF_get_dat: {
		W d = 0;

		m->result = wsv_get_dat(m->a[0], &d);
		(void)put(m, 0, &d, SZ(W));
		break;
	}
	case WF_set_dat:
		m->result = wsv_set_dat(m->a[0], m->a[1]);
		break;
	case WF_get_wrk: {
		RECT r;

		m->result = wsv_get_wrk(m->a[0], &r);
		(void)put(m, 0, &r, SZ(RECT));
		break;
	}
	case WF_set_wrk: {
		RECT r;

		if (!take(m, 0, &r, SZ(RECT))) {
			m->result = EX_ADR;
			break;
		}
		m->result = wsv_set_wrk(m->a[0], &r);
		break;
	}
	case WF_get_org: {
		W parent = 0;
		RECT org;

		m->result = wsv_get_org(m->a[0], &parent, &org);
		(void)put(m, 0, &parent, SZ(W));
		(void)put(m, SZ(W), &org, SZ(RECT));
		break;
	}
	case WF_set_org: {
		RECT org;

		if (!take(m, 0, &org, SZ(RECT))) {
			m->result = EX_ADR;
			break;
		}
		m->result = wsv_set_org(m->a[0], m->a[1], &org);
		break;
	}
	case WF_get_bar: {
		RECT rb;
		RECT bb;
		RECT lb;

		m->result = wsv_get_bar(m->a[0], &rb, &bb, &lb);
		(void)put(m, 0, &rb, SZ(RECT));
		(void)put(m, SZ(RECT), &bb, SZ(RECT));
		(void)put(m, 2 * SZ(RECT), &lb, SZ(RECT));
		break;
	}
	case WF_mov_wnd:
	case WF_rsz_wnd:
	case WF_chg_wnd: {
		RECT r;

		if (!take(m, 0, &r, SZ(RECT))) {
			m->result = EX_ADR;
			break;
		}
		if (m->func == WF_mov_wnd) {
			m->result = wsv_mov_wnd(m->a[0], &r);
		} else if (m->func == WF_rsz_wnd) {
			m->result = wsv_rsz_wnd(m->a[0], &r);
		} else {
			m->result = wsv_chg_wnd(m->a[0], &r, m->a[1]);
		}
		(void)put(m, 0, &r, SZ(RECT));
		break;
	}
	case WF_scr_wnd: {
		RECT r;

		if (m->inlen > 0 && !take(m, 0, &r, SZ(RECT))) {
			m->result = EX_ADR;
			break;
		}
		m->result = wsv_scr_wnd(m->a[0], (m->inlen > 0) ? &r : 0, m->a[1],
		                        m->a[2], m->a[3]);
		break;
	}
	case WF_era_wnd: {
		RECT r;

		if (m->inlen > 0 && !take(m, 0, &r, SZ(RECT))) {
			m->result = EX_ADR;
			break;
		}
		m->result = wsv_era_wnd(m->a[0], (m->inlen > 0) ? &r : 0);
		break;
	}
	case WF_req_dsp:
		m->result = wsv_req_dsp(m->a[0]);
		break;
	case WF_chk_dsp:
		m->result = wsv_chk_dsp(m->a[0]);
		break;
	case WF_sta_dsp: {
		RECT r;

		m->result = wsv_sta_dsp(m->a[0], &r, 0);
		(void)put(m, 0, &r, SZ(RECT));
		break;
	}
	case WF_end_dsp:
		m->result = wsv_end_dsp(m->a[0]);
		break;
	case WF_swi_wnd: {
		WEVENT ev;

		if (m->inlen > 0 && !take(m, 0, &ev, SZ(WEVENT))) {
			m->result = EX_ADR;
			break;
		}
		m->result = wsv_swi_wnd(m->a[0], (m->inlen > 0) ? &ev : 0);
		break;
	}
	case WF_get_act: {
		W pid = 0;

		m->result = wsv_get_act(&pid);
		(void)put(m, 0, &pid, SZ(W));
		break;
	}
	case WF_fnd_wnd: {
		PNT gpos;
		PNT lpos;
		W wid = 0;

		if (!take(m, 0, &gpos, SZ(PNT))) {
			m->result = EX_ADR;
			break;
		}
		m->result = wsv_fnd_wnd(&gpos, &lpos, &wid);
		(void)put(m, 0, &lpos, SZ(PNT));
		(void)put(m, SZ(PNT), &wid, SZ(W));
		break;
	}
	case WF_lst_wnd: {
		W wids[W_MAX_WND];
		W n = m->a[1];

		if (n > W_MAX_WND) {
			n = W_MAX_WND;
		}
		if (n * SZ(W) > WIN_MSG_DATA) {
			n = WIN_MSG_DATA / SZ(W);
		}
		m->result = wsv_lst_wnd(m->a[0], n, wids);
		if (n > 0) {
			(void)put(m, 0, wids, n * SZ(W));
		}
		break;
	}
	case WF_get_evt: {
		WEVENT ev;

		/* **サーバは待たない。** 待つのは利用側（冒頭の注記）。 */
		m->result = wsv_get_evt(&ev, m->a[0] | W_NOWAIT);
		(void)put(m, 0, &ev, SZ(WEVENT));
		break;
	}
	case WF_snd_evt: {
		WEVENT ev;

		if (!take(m, 0, &ev, SZ(WEVENT))) {
			m->result = EX_ADR;
			break;
		}
		m->result = wsv_snd_evt(&ev);
		break;
	}
	case WF_sta_drg:
		m->result = wsv_sta_drg(m->a[0], m->a[1]);
		break;
	case WF_end_drg:
		m->result = wsv_end_drg();
		break;
	case WF_get_drg: {
		PNT p;
		WEVENT ev;

		m->result = wsv_get_drg(&p, &ev);
		(void)put(m, 0, &p, SZ(PNT));
		(void)put(m, SZ(PNT), &ev, SZ(WEVENT));
		break;
	}
	case WF_mov_drg: {
		WEVENT ev;
		RECT nr;

		if (!take(m, 0, &ev, SZ(WEVENT))) {
			m->result = EX_ADR;
			break;
		}
		m->result = wsv_mov_drg(&ev, &nr);
		(void)put(m, 0, &nr, SZ(RECT));
		break;
	}
	case WF_rsz_drg: {
		WEVENT ev;
		RECT lim;
		RECT nr;

		if (!take(m, 0, &ev, SZ(WEVENT))) {
			m->result = EX_ADR;
			break;
		}
		if ((m->a[0] & 1) != 0 &&
		    !take(m, SZ(WEVENT), &lim, SZ(RECT))) {
			m->result = EX_ADR;
			break;
		}
		m->result = wsv_rsz_drg(&ev, ((m->a[0] & 1) != 0) ? &lim : 0, &nr);
		(void)put(m, 0, &nr, SZ(RECT));
		break;
	}
	case WF_count:
		m->result = win_count();
		break;
	case WF_get_env: {
		WinEnv env;

		m->result = wsv_get_env(m->a[0], &env);
		(void)put(m, 0, &env, SZ(WinEnv));
		break;
	}
	default:
		m->result = EX_NOSPT;
		break;
	}
}

/* ------------------------------------------------------------------ */
/* サーバ本体                                                          */
/* ------------------------------------------------------------------ */

/* 持ち主が居なくなったウィンドウを片付ける。
 *
 * **カーネルから教えてもらえない。** サーバがユーザランドのプロセスに
 * なった以上、プロセス終了をカーネルが直接知らせる口は無い。
 * 要求が来るたびに持ち主の生死を見て、居なければ閉じる。
 * ウィンドウは高々 16 枚なので、毎回見ても安い。 */
LOCAL void sweep_dead(void)
{
	W i;
	W saved = win_req_pid;

	for (i = 1; i <= W_MAX_WND; i++) {
		WDSTAT st;

		if (wsv_get_sts(i, &st, 0) < 0) {
			continue;
		}
		if (st.pid < 1) {
			continue;
		}
		if (prc_sts(st.pid, 0, 0) != ER_NOPRC) {
			continue;
		}
		/* そのプロセスの代理として閉じる。 */
		win_req_pid = st.pid;
		(void)wsv_cls_wnd(i, CLR);
	}
	win_req_pid = saved;
}

EXPORT ERR winsrv_loop(void)
{
	WinMsg m;
	W rdv = 0;

	if (win_por < 1) {
		return EX_NOSPT;
	}
	for (;;) {
		WERR n = acp_por(win_por, MGR_PTN, &rdv, (VP)&m, T_FOREVER);

		if (n < 0) {
			if (n == ER_DLT) {
				break; /* ポートが消えた = 終わり */
			}
			continue;
		}
		/* **誰からの要求かはランデブの記録から引く。**
		 * 要求の中に入れさせると偽れる。 */
		win_req_pid = rdv_caller(rdv);
		/* **配る前に掃く。** 後にすると、持ち主が死んだ直後の
		 * 1 回目の問い合わせに古い答えを返してしまう。 */
		sweep_dead();
		m.result = EX_OK;
		dispatch(&m);
		win_req_pid = 0;
		/* **配り終えてから画面を作り直す。**
		 * 利用側は自分の裏画面へ好きなときに描くので、どこが
		 * 変わったかは分からない。要求のたびに貼り直す（詳細 E2）。 */
		win_flush();
		(void)rpl_rdv(rdv, (VP)&m, (W)sizeof(WinMsg));
	}
	return EX_OK;
}

EXPORT ERR winsrv_open(void)
{
	TC key[N_NAMELEN];
	WERR por;
	ERR er;

	if (win_por >= 1) {
		return EX_OK; /* 既に開いている */
	}
	/* 要求と返答は同じ構造体なので同じ大きさ。 */
	por = cre_por((W)sizeof(WinMsg), (W)sizeof(WinMsg), DELEXIT);
	if (por < 1) {
		return (ERR)por;
	}
	mgr_name(MGR_WINDOW, key);
	er = cre_nam(key, (W)por, N_FORCE | DELEXIT);
	if (er < 0) {
		(void)del_por(por);
		return er;
	}
	win_por = por;
	return EX_OK;
}

EXPORT void winsrv_stop(void)
{
	TC key[N_NAMELEN];

	if (win_por < 1) {
		return;
	}
	mgr_name(MGR_WINDOW, key);
	(void)del_nam(key);
	(void)del_por(win_por); /* 待っているサーバは ER_DLT で抜ける */
	win_por = 0;
}

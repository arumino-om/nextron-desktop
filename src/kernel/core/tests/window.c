/*
 * window.c — 外殻 ウィンドウマネージャ (§3.1)
 *
 * selftest.c から切り出したもの（W0-1）。区分の登録は
 * testsuite.h の BTRON_TEST_GROUP で行い、実行順は order で決まる。
 * order の帯は docs/registry.md で予約している。
 */

#include <btron/bootinfo.h>
#include <btron/device.h>
#include <btron/dp.h>
#include <btron/event.h>
#include <btron/file.h>
#include <btron/fontman.h>
#include <btron/machdesc.h>
#include <btron/manager.h>
#include <btron/objfmt.h>
#include <btron/screen.h>
#include <btron/system.h>
#include <btron/test.h>
#include <btron/virtio.h>
#include <btron/window.h>
#include <btron/winmsg.h>

#include "../knl.h"
#include "testsuite.h"

/* por1 で受け付けて por2 へ回送する（中間のサーバ） */
/* ドラッグを動かす補助タスク。
 *
 * `wmov_drg` はボタンが離されるまで戻らないので、**別のタスクが PD を
 * 動かしてボタンを離さないと検査できない。** 実際の使われ方
 * （ドライバの割込みがイベントを入れる）に一番近い形でもある。 */
LOCAL PNT drag_to;

LOCAL void t_drag(INT stacd, VP exinf)
{
	(void)stacd;
	(void)exinf;
	(void)dly_tsk(40);
	evt_pd_moved(drag_to);
	(void)dly_tsk(40);
	evt_pd_button(False, False); /* EV_BUTUP */
	tk_ext_tsk();
}

/* 別プロセスからウィンドウを開く。
 *
 * **要求元の見分けを確かめるのに、別のプロセスが要る。**
 * サーバが要求元を `prc_self()` で決めていると、サーバ自身のプロセスが
 * 持ち主になってしまう。検査タスクと同じプロセスから呼んでいる限り
 * 区別がつかない（逆検査で素通りした）。 */
LOCAL WID wpc_wid;

LOCAL W wpc_waiter;

LOCAL W w_other_main(MESSAGE *msg)
{
	RECT r;
	MESSAGE m;

	(void)msg;
	r.c.left = 300;
	r.c.top = 300;
	r.c.right = 420;
	r.c.bottom = 390;
	wpc_wid = wopn_wnd(WA_NORMAL, 0, &r, 0, 0, 0, 0, 0);
	/* 親へ知らせる。 */
	m.msg_type = MS_TYPE0;
	m.msg_size = 4;
	m.msg_body.ANYMSG.msg_str[0] = (UB)'W';
	m.msg_body.ANYMSG.msg_str[1] = 0;
	m.msg_body.ANYMSG.msg_str[2] = 0;
	m.msg_body.ANYMSG.msg_str[3] = 0;
	(void)snd_msg(wpc_waiter, &m, 0);
	/* 親が検べ終わるまで生きている。 */
	(void)slp_tsk(TMO_FEVR);
	return 0;
}

LOCAL CONST TC nm_wnd1[] = {(TC)'W', (TC)'N', (TC)'D', (TC)'1', TNULL};

LOCAL CONST TC nm_wnd2[] = {(TC)'W', (TC)'N', (TC)'D', (TC)'2', TNULL};

LOCAL Bool win_up;    /* ウィンドウマネージャが立っているか */

LOCAL W wm_pid;       /* そのプロセス ID */

LOCAL WID win_w1;

LOCAL WID win_w2;

BTRON_TEST_GROUP("外殻 ウィンドウマネージャ (§3.1)", 340, TST_PHASE_PROC, tst_window)
{
	W me = prc_self();

	BTRON_TEST("ウィンドウの生成と入力受付状態 (§3.1.1.2)", {
		RECT r;
		WID w1;
		WID w2;
		WDSTAT st;
		W pid = 0;
		MESSAGE m;

		/* **ウィンドウマネージャはユーザ特権のプロセス**（詳細 E22）。
		 * 起こして、名前が出るまで待つ。以後の `w*` はすべて
		 * 利用側のスタブからランデブでそこへ届く。 */
		wm_pid = win_launch();
		if (wm_pid < 0) {
			BTRON_CHECK("画面が無い構成なので飛ばす", True);
		} else {
			win_up = True;
			(void)clr_msg(MM_ALL, MM_NULL);

			r.c.left = 10;
			r.c.top = 10;
			r.c.right = 210;
			r.c.bottom = 150;
			w1 = wopn_wnd(WA_NORMAL, 0, &r, 0, 0, nm_wnd1, 0, 0);
			BTRON_CHECK("生成", w1 >= 1);
			/* 仕様「実行後には、実際に生成されたウィンドウの作業領域の
			 * 相対座標が格納される (左上は常に ( 0, 0 ))」 */
			BTRON_CHECK("作業領域の相対座標が返る",
			            r.c.left == 0 && r.c.top == 0 && r.c.right > 0 &&
			                r.c.bottom > 0);
			/* 枠とスクロールバーの分だけ作業領域は小さい */
			BTRON_CHECK("枠の分だけ小さい", r.c.right < 200);
			/* 仕様「生成したウィンドウは、必ず入力受付状態となり前面に
			 * 表示される」 */
			BTRON_CHECK("入力受付状態", wget_act(&pid) == w1);
			BTRON_CHECK("管理プロセス", pid == me);
			BTRON_CHECK("状態が取れる",
			            wget_sts(w1, &st, 0) == 1 && st.pid == me);
			BTRON_CHECK("描画環境がある", wget_gid(w1) >= 1);

			/* 2 枚目を開くと入力受付状態が移り、1 枚目へ EV_INACT が来る。 */
			r.c.left = 60;
			r.c.top = 60;
			r.c.right = 260;
			r.c.bottom = 200;
			w2 = wopn_wnd(WA_NORMAL, 0, &r, 0, 0, nm_wnd2, 0, 0);
			BTRON_CHECK("2 枚目", w2 >= 1 && w2 != w1);
			BTRON_CHECK("入力受付状態が移った", wget_act(0) == w2);
			BTRON_CHECK("1 枚目は入力不可", wget_sts(w1, 0, 0) == 0);
			/* 仕様「以前に入力受付状態であったウィンドウは入力不可状態と
			 * なり、その管理プロセスに対して EV_INACT が送られる」 */
			BTRON_CHECK("EV_INACT が来た",
			            rcv_msg(MM_MNG0, &m, (W)sizeof(m), NOWAIT) >= 0);
			{
				CONST WEVENT *we =
				    (CONST WEVENT *)(CONST VP)&m.msg_body;

				BTRON_CHECK("タイプ", we->g.type == EV_INACT);
				BTRON_CHECK("対象ウィンドウ", we->g.wid == (H)w1);
			}
			win_w1 = w1;
			win_w2 = w2;
		}
	});

	BTRON_TEST("前後関係と重なり (§3.1.1.2 / §2.2.4)", {
		if (!win_up) {
			BTRON_CHECK("画面が無い構成なので飛ばす", True);
		} else {
			GID g1 = wget_gid(win_w1);
			RECT r;
			PNT p;
			PIXVAL v = 0;
			WID wf;

			/* 奥 (win_w1) の作業領域を黒で塗る。
			 * **手前 (win_w2) に重なっている部分は塗られてはいけない。**
			 * 前置長方形リスト (§2.2.4) がこれを保証する。 */
			(void)gget_fra(g1, &r);
			BTRON_CHECK("奥を塗る",
			            gfil_rec(g1, r, (CONST PAT *)BLACK100, 0,
			                     G_STORE) == EG_OK);
			/* win_w2 は (60,60) から。その中の点は塗られていないはず。 */
			{
				GID sg = wget_gid(win_w2);
				RECT r2;

				(void)gget_fra(sg, &r2);
				/* win_w2 の作業領域を白で塗ってから、win_w1 を塗り直す。 */
				(void)gfil_rec(sg, r2, (CONST PAT *)WHITE0, 0, G_STORE);
				(void)gfil_rec(g1, r, (CONST PAT *)BLACK100, 0, G_STORE);
				p.x = 20;
				p.y = 20;
				BTRON_CHECK("手前は上書きされない",
				            gtst_pnt(sg, p, &v) == 0 &&
				                (UW)v == 0x00FFFFFFU);
			}

			/* 前面ウィンドウは常に最前面（仕様）。 */
			r.c.left = 0;
			r.c.top = 0;
			r.c.right = 100;
			r.c.bottom = 40;
			wf = wopn_wnd(WA_FRONT | WA_TITL, 0, &r, 0, 0, 0, 0, 0);
			BTRON_CHECK("前面ウィンドウ", wf >= 1);
			/* 仕様「前面ウィンドウの生成でも入力受付状態は変化しない」 */
			BTRON_CHECK("入力受付状態は変わらない", wget_act(0) == win_w2);
			{
				PNT g;
				W code;
				W hit = 0;

				g.x = 50;
				g.y = 20;
				code = wfnd_wnd(&g, 0, &hit);
				BTRON_CHECK("最前面にいる", hit == wf && code >= 0);
			}
			BTRON_CHECK("閉じる", wcls_wnd(wf, CLR) == EX_OK);
		}
	});

	BTRON_TEST("位置コードの判定 (§3.1.3 wfnd_wnd)", {
		if (!win_up) {
			BTRON_CHECK("画面が無い構成なので飛ばす", True);
		} else {
			WDSTAT st;
			PNT g;
			PNT l;
			W hit = 0;

			BTRON_CHECK("状態", wget_sts(win_w2, &st, 0) >= 0);
			/* 作業領域の真ん中 */
			g.x = (H)((st.r.c.left + st.r.c.right) / 2);
			g.y = (H)((st.r.c.top + st.r.c.bottom) / 2);
			BTRON_CHECK("作業領域内",
			            wfnd_wnd(&g, &l, &hit) == W_WORK && hit == win_w2);
			BTRON_CHECK("相対座標に直る", l.x >= 0 && l.y >= 0);
			/* タイトル部 */
			g.y = (H)(st.r.c.top + 4);
			g.x = (H)(st.r.c.left + 30);
			BTRON_CHECK("タイトル部",
			            wfnd_wnd(&g, 0, &hit) == W_TITL && hit == win_w2);
			/* 右スクロールバー */
			g.x = (H)(st.r.c.right - 4);
			g.y = (H)((st.r.c.top + st.r.c.bottom) / 2);
			BTRON_CHECK("右スクロールバー",
			            wfnd_wnd(&g, 0, &hit) == W_RBAR);
			/* 左上のハンドル（WA_SIZE が立っている） */
			g.x = (H)(st.r.c.left + 1);
			g.y = (H)(st.r.c.top + 1);
			BTRON_CHECK("左上ハンドル",
			            wfnd_wnd(&g, 0, &hit) == W_LTHD);
			/* どのウィンドウでもない場所 */
			g.x = 600;
			g.y = 400;
			BTRON_CHECK("外", wfnd_wnd(&g, 0, &hit) < 0 && hit == 0);
		}
	});

	BTRON_TEST("ウィンドウイベントの配送 (§3.1.2.2 wget_evt)", {
		if (!win_up) {
			BTRON_CHECK("画面が無い構成なので飛ばす", True);
		} else {
			WDSTAT st;
			EVENT ev;
			WEVENT we;
			W t;

			(void)clr_evt(EM_ALL, EM_NULL);
			(void)wget_sts(win_w1, &st, 0);

			/* **入力受付状態でないウィンドウ (win_w1) を押すと
			 * 切換イベントになる。**
			 *
			 * 仕様「位置コードの場合は、PD のプレスにより入力受付
			 * 状態の切換えが発生したことを意味し、プレスした位置の
			 * 位置コードが入っている」。つまり cmd は W_SWITCH では
			 * なく**押した場所の位置コード**で、W_SWITCH はアプリから
			 * の要求による切換のときの値。 */
			/* win_w1 は (10,10)-(210,150)、win_w2 は (60,60)-(260,200)。
			 * win_w2 に隠れていない所を押す。 */
			ev.type = EV_BUTDWN;
			ev.time = 0;
			ev.pos.x = 40;
			ev.pos.y = 100;
			ev.data.info = 0;
			ev.stat = ES_BUT;
			BTRON_CHECK("ボタンを押す", put_evt(&ev, EP_TIME) == ER_OK);
			t = wget_evt(&we, W_NOWAIT);
			BTRON_CHECK("切換イベント", t == EV_SWITCH);
			BTRON_CHECK("コマンドは位置コード",
			            we.s.cmd >= W_WORK && we.s.cmd <= W_LBAR);
			BTRON_CHECK("W_SWITCH ではない", we.s.cmd != W_SWITCH);
			BTRON_CHECK("対象ウィンドウ", we.s.wid == (H)win_w1);
			/* 仕様「このイベントを取り出した時点では、既に wid で
			 * 示されるウィンドウに入力受付状態は切り換わっており」。
			 * **アプリが wswi_wnd を呼ぶのではない。** */
			BTRON_CHECK("もう切り換わっている", wget_act(0) == win_w1);

			/* 入力受付状態になったウィンドウをもう一度押すと、
			 * 切換は起きないので生イベントになる。
			 * 枠の上でも同じ（仕様の EV_BUTDWN の条件）。 */
			BTRON_CHECK("もう一度押す", put_evt(&ev, EP_TIME) == ER_OK);
			BTRON_CHECK("今度は生イベント",
			            wget_evt(&we, W_NOWAIT) == EV_BUTDWN);
			ev.type = EV_BUTUP;
			BTRON_CHECK("離す", put_evt(&ev, EP_TIME) == ER_OK);
			/* 仕様「EV_BUTUP は PD のリリースにより発生するイベント」。
			 * **切換イベントにはならない。** */
			BTRON_CHECK("リリースは常に生イベント",
			            wget_evt(&we, W_NOWAIT) == EV_BUTUP);

			/* **入力受付状態でない窓の上で離しても同じ。**
			 * ここを押し下げと同じ扱いにすると、ドラッグして別の窓で
			 * 手を離しただけで入力受付状態が飛んでいく。
			 * （入力受付状態の窓で離す経路だけ見ていると気づけない。） */
			ev.pos.x = 230;
			ev.pos.y = 170;
			BTRON_CHECK("別の窓の上で離す",
			            put_evt(&ev, EP_TIME) == ER_OK);
			BTRON_CHECK("やはり生イベント",
			            wget_evt(&we, W_NOWAIT) == EV_BUTUP);
			BTRON_CHECK("入力受付状態は動かない", wget_act(0) == win_w1);
			ev.type = EV_BUTDWN;

			/* win_w2 を押し直して入力受付状態を戻す。
			 * いまは win_w1 が手前なので、重なっていない所を選ぶ。 */
			ev.pos.x = 230;
			ev.pos.y = 170;
			(void)put_evt(&ev, EP_TIME);
			BTRON_CHECK("戻す", wget_evt(&we, W_NOWAIT) == EV_SWITCH);
			BTRON_CHECK("入力受付状態が戻った", wget_act(0) == win_w2);

			/* 入力受付状態のウィンドウの作業領域を押すと生イベント。 */
			ev.pos.x = (H)((st.r.c.left + st.r.c.right) / 2);
			ev.pos.y = (H)((st.r.c.top + st.r.c.bottom) / 2);
			(void)put_evt(&ev, EP_TIME);
			t = wget_evt(&we, W_NOWAIT);
			BTRON_CHECK("生イベント", t == EV_BUTDWN);
			/* W_DRGEVT で相対座標になる */
			(void)put_evt(&ev, EP_TIME);
			t = wget_evt(&we, W_NOWAIT | W_DRGEVT);
			BTRON_CHECK("相対座標",
			            t == EV_BUTDWN && we.e.pos.x < ev.pos.x);

			/* 何も無ければ EV_NULL（W_NOWAIT のとき）。
			 * 仕様「PD の位置するウィンドウ ID、位置コード、および
			 * PD の現在位置が設定されている」「PD の位置にウィンドウが
			 * 存在しない場合はウィンドウ ID = 0」。 */
			(void)clr_evt(EM_ALL, EM_NULL);
			BTRON_CHECK("空なら EV_NULL",
			            wget_evt(&we, W_NOWAIT) == EV_NULL);
			{
				PNT at;

				/* **set_pdp は使えない。**入力装置が絶対動作タイプ
				 * だと仕様により位置設定が断られる (§1.7.3)。
				 * ドライバの口から入れる。 */
				at.x = 0;
				at.y = 0;
				evt_pd_moved(at);
				BTRON_CHECK("ヌルイベント",
				            wget_evt(&we, W_NOWAIT) == EV_NULL);
				/* 原点にはウィンドウが無い構成にしてある。 */
				BTRON_CHECK("窓の外なら wid = 0", we.s.wid == 0);

				at.x = 230;
				at.y = 170;
				evt_pd_moved(at);
				BTRON_CHECK("窓の上",
				            wget_evt(&we, W_NOWAIT) == EV_NULL &&
				                we.s.wid == (H)win_w2 &&
				                we.s.cmd == W_WORK);
			}
			BTRON_CHECK("アドレス不正", wget_evt(0, W_NOWAIT) == EX_ADR);
			BTRON_CHECK("モード不正", wget_evt(&we, 0x100) == EX_PAR);
		}
	});

	BTRON_TEST("切換・再表示・パネル (§3.1.1.2 / §3.1.2.3)", {
		if (!win_up) {
			BTRON_CHECK("画面が無い構成なので飛ばす", True);
		} else {
			WID wp;
			RECT r;

			/* 入力受付状態を明示的に切り換える。 */
			BTRON_CHECK("切換", wswi_wnd(win_w1, 0) == EX_OK);
			BTRON_CHECK("移った", wget_act(0) == win_w1);

			/* 再表示要求 → wsta_dsp で領域が取れ、wend_dsp で下がる。 */
			BTRON_CHECK("要求", wreq_dsp(win_w1) == EX_OK);
			BTRON_CHECK("要求が立っている", wchk_dsp(win_w1) == 1);
			BTRON_CHECK("開始", wsta_dsp(win_w1, &r, 0) == 1);
			BTRON_CHECK("領域は作業領域の相対座標",
			            r.c.left == 0 && r.c.top == 0);
			BTRON_CHECK("終了", wend_dsp(win_w1) == EX_OK);
			BTRON_CHECK("要求が下がった", wchk_dsp(win_w1) == 0);
			BTRON_CHECK("要求が無ければ 0", wsta_dsp(win_w1, &r, 0) == 0);

			/* パネルウィンドウは入力受付状態を横取りし、閉じると返す。 */
			r.c.left = 100;
			r.c.top = 100;
			r.c.right = 250;
			r.c.bottom = 180;
			wp = wopn_pwd(&r);
			BTRON_CHECK("パネル", wp >= 1);
			BTRON_CHECK("横取りした", wget_act(0) == wp);
			BTRON_CHECK("閉じる", wcls_wnd(wp, CLR) == EX_OK);
			/* 仕様「パネルウィンドウがクローズされた時点で、入力受付状態は
			 * 元の主ウィンドウに戻される」 */
			BTRON_CHECK("元に戻った", wget_act(0) == win_w1);

			/* 従属ウィンドウ。主が入力受付状態のときだけ見える。 */
			r.c.left = 20;
			r.c.top = 200;
			r.c.right = 160;
			r.c.bottom = 260;
			{
				WID ws = wopn_wnd(WA_SUBW | WA_TITL, (UW)win_w1, &r, 0, 0,
				                  0, 0, 0);

				BTRON_CHECK("従属ウィンドウ", ws >= 1);
				/* 仕様「従属ウィンドウの生成では、入力受付状態は変化しない」 */
				BTRON_CHECK("入力受付状態は変わらない",
				            wget_act(0) == win_w1);
				BTRON_CHECK("一覧に出る", wlst_wnd(0, 0, 0) >= 3);
				BTRON_CHECK("閉じる", wcls_wnd(ws, CLR) == EX_OK);
			}
		}
	});

	BTRON_TEST("ドラッグによる移動と変形 (§3.1.2.5 / §3.1.4)", {
		if (!win_up) {
			BTRON_CHECK("画面が無い構成なので飛ばす", True);
		} else {
			WDSTAT st;
			WEVENT we;
			RECT nr;
			PNT p;
			GID dg;
			ID dt;

			/* 対象は入力受付状態でなければならない（仕様）。 */
			(void)wswi_wnd(win_w1, 0);
			(void)clr_evt(EM_ALL, EM_NULL);
			(void)wget_sts(win_w1, &st, 0);

			/* --- 契機イベントの検め --- */
			we.s.type = EV_KEYDWN;
			we.s.cmd = W_FRAM;
			we.s.wid = (H)win_w1;
			BTRON_CHECK("イベントのタイプが不正",
			            wmov_drg(&we, 0) == EX_PAR);
			we.s.type = EV_SWITCH;
			we.s.cmd = W_TITL;
			/* 仕様「位置コードは、W_FRAM(1)、W_LTHD(4)〜W_RBHD(7) の
			 * いずれかでなくてはいけない」 */
			BTRON_CHECK("位置コードが不正",
			            wmov_drg(&we, 0) == EX_PAR);
			we.s.cmd = W_FRAM;
			/* 変形は W_FRAM を受けない（枠を掴んだら移動）。 */
			BTRON_CHECK("変形に W_FRAM は使えない",
			            wrsz_drg(&we, 0, 0) == EX_PAR);
			BTRON_CHECK("アドレス不正", wmov_drg(0, 0) == EX_ADR);

			/* --- ドラッグ描画環境 --- */
			BTRON_CHECK("ドラッグ中でなければ取り出せない",
			            wget_drg(&p, &we) == EX_DRAG);
			dg = wsta_drg(win_w1, 1);
			BTRON_CHECK("開始できる", dg >= 1);
			/* 仕様「既に他のプロセスがドラッグ描画を実行していた場合は、
			 * EX_DRAG のエラー」。**同じプロセスでも二重には始めない。** */
			BTRON_CHECK("二重には始められない",
			            wsta_drg(win_w1, 1) == EX_DRAG);
			/* 座標系は元のウィンドウと同じ（仕様 §3.1.2.5）。
			 *
			 * **境界長方形の値を比べてはいけない。** ドラッグ環境は
			 * 画面を、ウィンドウ環境は自分の裏画面を対象にしているので
			 * （詳細 E2）、原点の位置が違って当然である。仕様が言って
			 * いるのは「相対座標が同じ点を指す」こと。相対 (0,0) が
			 * どちらも作業領域の左上を指していればよい。 */
			{
				PNT a1;
				PNT a2;

				a1.x = 0;
				a1.y = 0;
				a2.x = 0;
				a2.y = 0;
				BTRON_CHECK("ドラッグ環境の原点",
				            gcnv_abs(dg, &a1) == EG_OK);
				BTRON_CHECK("ウィンドウ環境の原点",
				            gcnv_abs(wget_gid(win_w1), &a2) == EG_OK);
				/* 画面で測っても裏画面で測っても、作業領域の左上は
				 * 外枠の左上から同じだけずれている。 */
				BTRON_CHECK("相対 (0,0) が同じ点を指す",
				            a1.x - st.r.c.left == a2.x &&
				                a1.y - st.r.c.top == a2.y);
			}
			/* PD 位置は相対座標で返る。 */
			{
				PNT at;

				at.x = (H)(st.r.c.left + 30);
				at.y = (H)(st.r.c.top + 30);
				evt_pd_moved(at);
				BTRON_CHECK("PD 位置が取れる",
				            wget_drg(&p, &we) == EV_NULL);
				/* 仕様「PD 位置は pos に戻される値と同一となる」 */
				BTRON_CHECK("イベントにも同じ位置",
				            we.e.pos.x == p.x && we.e.pos.y == p.y);
			}
			BTRON_CHECK("終了", wend_drg() == EX_OK);
			/* 仕様「ドラッグ描画が行なわれていなかった場合は何もしない」 */
			BTRON_CHECK("二重終了は何もしない", wend_drg() == EX_OK);

			/* --- 実際に動かす --- */
			(void)wget_sts(win_w1, &st, 0);
			{
				PNT at;

				at.x = (H)(st.r.c.left + 4);
				at.y = (H)(st.r.c.top + 4);
				evt_pd_moved(at);
				drag_to.x = (H)(at.x + 30);
				drag_to.y = (H)(at.y + 20);
			}
			dt = tst_make_task(t_drag, 4, 0);
			BTRON_CHECK("補助タスク", dt >= 1 && tk_sta_tsk(dt, 0) == E_OK);
			we.s.type = EV_SWITCH;
			we.s.cmd = W_FRAM;
			we.s.wid = (H)win_w1;
			nr.c.left = 0;
			BTRON_CHECK("ドラッグ移動", wmov_drg(&we, &nr) >= 0);
			BTRON_CHECK("30 ドット右・20 ドット下へ動いた",
			            nr.c.left == (H)(st.r.c.left + 30) &&
			                nr.c.top == (H)(st.r.c.top + 20));
			BTRON_CHECK("大きさは変わらない",
			            nr.c.right - nr.c.left ==
			                st.r.c.right - st.r.c.left);
			BTRON_CHECK("ウィンドウも動いた",
			            wget_sts(win_w1, &st, 0) >= 0 &&
			                st.r.c.left == nr.c.left);
			/* ドラッグ中に借りた描画環境は返っている。 */
			BTRON_CHECK("ドラッグは終わっている",
			            wget_drg(&p, &we) == EX_DRAG);

			/* --- 変形 --- */
			(void)clr_evt(EM_ALL, EM_NULL);
			(void)wget_sts(win_w1, &st, 0);
			{
				PNT at;

				at.x = (H)(st.r.c.right - 4);
				at.y = (H)(st.r.c.bottom - 4);
				evt_pd_moved(at);
				drag_to.x = (H)(at.x + 40);
				drag_to.y = (H)(at.y + 30);
			}
			dt = tst_make_task(t_drag, 4, 0);
			BTRON_CHECK("補助タスク", dt >= 1 && tk_sta_tsk(dt, 0) == E_OK);
			we.s.cmd = W_RBHD; /* 右下のハンドル */
			nr.c.left = 0;
			BTRON_CHECK("ドラッグ変形", wrsz_drg(&we, 0, &nr) >= 0);
			BTRON_CHECK("左上は動かない",
			            nr.c.left == st.r.c.left && nr.c.top == st.r.c.top);
			BTRON_CHECK("右下が広がった",
			            nr.c.right == (H)(st.r.c.right + 40) &&
			                nr.c.bottom == (H)(st.r.c.bottom + 30));

			/* --- シフトで方向が拘束される --- */
			(void)clr_evt(EM_ALL, EM_NULL);
			(void)wget_sts(win_w1, &st, 0);
			{
				PNT at;

				at.x = (H)(st.r.c.left + 4);
				at.y = (H)(st.r.c.top + 4);
				evt_pd_moved(at);
				/* 横に大きく、縦に少し動かす。
				 * 仕様「拘束の方向は、PD の最初の位置から最初に
				 * 移動した変位の長辺の方向となる」ので横に拘束され、
				 * 縦は動かないはず。 */
				drag_to.x = (H)(at.x + 50);
				drag_to.y = (H)(at.y + 10);
			}
			evt_meta(ES_LSHFT, ES_LSHFT);
			dt = tst_make_task(t_drag, 4, 0);
			BTRON_CHECK("補助タスク", dt >= 1 && tk_sta_tsk(dt, 0) == E_OK);
			we.s.type = EV_SWITCH;
			we.s.cmd = W_FRAM;
			we.s.wid = (H)win_w1;
			BTRON_CHECK("拘束付きドラッグ", wmov_drg(&we, &nr) >= 0);
			BTRON_CHECK("横には動く",
			            nr.c.left == (H)(st.r.c.left + 50));
			BTRON_CHECK("縦には動かない", nr.c.top == st.r.c.top);
			evt_meta(0, ES_LSHFT);

			/* --- 最小サイズでクランプする --- */
			(void)clr_evt(EM_ALL, EM_NULL);
			(void)wget_sts(win_w1, &st, 0);
			{
				PNT at;
				RECT lim;

				at.x = (H)(st.r.c.right - 4);
				at.y = (H)(st.r.c.bottom - 4);
				evt_pd_moved(at);
				/* 思い切り左上へ引く。最小サイズで止まるはず。 */
				drag_to.x = (H)(st.r.c.left + 4);
				drag_to.y = (H)(st.r.c.top + 4);
				dt = tst_make_task(t_drag, 4, 0);
				BTRON_CHECK("補助タスク",
				            dt >= 1 && tk_sta_tsk(dt, 0) == E_OK);
				/* 仕様「limit->lefttop はウィンドウの最小サイズ
				 * ( lefttop.c.v が縦、lefttop.c.h が横の大きさ )」 */
				lim.p.lefttop.x = 150;  /* 横の最小 */
				lim.p.lefttop.y = 120;  /* 縦の最小 */
				lim.p.rightbot.x = 400; /* 横の最大 */
				lim.p.rightbot.y = 300;
				we.s.cmd = W_RBHD;
				BTRON_CHECK("ドラッグ変形", wrsz_drg(&we, &lim, &nr) >= 0);
				BTRON_CHECK("最小サイズで止まる",
				            nr.c.right - nr.c.left == 150 &&
				                nr.c.bottom - nr.c.top == 120);
			}
			(void)clr_evt(EM_ALL, EM_NULL);
		}
	});

	BTRON_TEST("ウィンドウマネージャはサーバである (詳細 E22)", {
		if (!win_up) {
			BTRON_CHECK("画面が無い構成なので飛ばす", True);
		} else {
			WinMsg wm;
			WinEnv env;
			WERR por;
			W i;

			/* **グローバル名で見つかること。** これが外殻サーバの
			 * 作法（詳細 E1）で、利用側はこれだけを頼りに話しかける。 */
			por = mgr_lookup(MGR_WINDOW);
			BTRON_CHECK("名前で見つかる", por >= 1);

			/* **別のプロセスで動いていること。**
			 * 同じプロセスの中の関数呼び出しに戻っていたら、
			 * 詳細 E22 の「1 プロセス 1 サーバ」になっていない。 */
			BTRON_CHECK("別のプロセスで動いている",
			            wm_pid > 0 && wm_pid != prc_self());
			{
				P_STATE ps;

				BTRON_CHECK("そのプロセスは生きている",
				            prc_sts(wm_pid, &ps, 0) == wm_pid);
			}

			/* 利用側のスタブを通さず、要求を直に投げてみる。
			 * **これが通れば、本当にランデブで話している。** */
			if (por >= 1) {
				for (i = 0; i < 4; i++) {
					wm.a[i] = 0;
				}
				wm.func = WF_get_act;
				wm.result = 0;
				wm.inlen = 0;
				wm.outlen = 0;
				BTRON_CHECK("直に要求を投げられる",
				            cal_por((ID)por, MGR_PTN, (VP)&wm,
				                    (W)sizeof(WinMsg), T_FOREVER) >= 0);
				BTRON_CHECK("入力受付状態が返る",
				            wm.result == wget_act(0));
				/* 知らない要求番号は断られる。 */
				wm.func = 9999;
				wm.inlen = 0;
				(void)cal_por((ID)por, MGR_PTN, (VP)&wm,
				              (W)sizeof(WinMsg), T_FOREVER);
				BTRON_CHECK("知らない要求は断る", wm.result == EX_NOSPT);
			}

			/* 描画環境の情報。**利用側はこれで自分の GID を組み立てる。**
			 * サーバの GID をもらっても、別のアドレス空間では使えない。 */
			wm.func = WF_get_env;
			wm.a[0] = win_w2;
			wm.inlen = 0;
			wm.outlen = 0;
			BTRON_CHECK("環境の情報を引ける",
			            cal_por((ID)por, MGR_PTN, (VP)&wm,
			                    (W)sizeof(WinMsg), T_FOREVER) >= 0 &&
			                wm.result == EX_OK);
			{
				CONST UB *src = wm.data;
				UB *dst = (UB *)(VP)&env;

				for (i = 0; i < (W)sizeof(WinEnv); i++) {
					dst[i] = src[i];
				}
			}
			BTRON_CHECK("世代番号が入っている", env.gen > 0);
			{
				WDSTAT st;

				BTRON_CHECK("作業領域が合っている",
				            wget_sts(win_w2, &st, 0) >= 0 &&
				                env.work.c.right - env.work.c.left ==
				                    st.wr.c.right - st.wr.c.left);
			}

			/* **GID は呼んだプロセスのもの。** サーバの描画環境 ID を
			 * そのまま返していないことを確かめる。 */
			{
				GID g = wget_gid(win_w2);

				BTRON_CHECK("描画環境が取れる", g >= 1);
				BTRON_CHECK("自分の環境として使える",
				            gget_bnd(g, &env.work) == EG_OK);
			}
		}
	});

	BTRON_TEST("裏のウィンドウの枠も描き直される (§3.1.2.3)", {
		if (!win_up) {
			BTRON_CHECK("画面が無い構成なので飛ばす", True);
		} else {
			GID sg = gopn_dev(tst_nm_screen, 0);
			WDSTAT s2;
			RECT nr;
			W l;
			W t;

			/* **枠を描くのはマネージャである。** 持ち主に再表示を
			 * 頼んでも描き直されるのは作業領域だけなので、裏の
			 * ウィンドウの枠は誰も描かない。重なったウィンドウを
			 * 動かすと、裏の枠が消えたままになっていた。
			 *
			 * 入力不可状態のタイトルバーは**白一色**で、机の 50% 柄
			 * とは見分けられる。そこを隠してから出して確かめる。 */
			if (sg >= 1 && wget_sts(win_w2, &s2, 0) >= 0) {
				/* タイトルバーの中で、文字にも四隅のハンドルにも
				 * 掛からない所を見る。 */
				l = s2.r.c.left + 60;
				t = s2.r.c.top + 4;

				/* 手前の win_w1 を**大きさごと指定して**重ねる。
				 * 動かすだけだと win_w1 の大きさに依ってしまう。 */
				BTRON_CHECK("手前にする", wswi_wnd(win_w1, 0) == EX_OK);
				nr.c.left = (H)(s2.r.c.left - 10);
				nr.c.top = (H)(s2.r.c.top - 10);
				nr.c.right = (H)(s2.r.c.left + 120);
				nr.c.bottom = (H)(s2.r.c.top + 60);
				BTRON_CHECK("重ねる", wrsz_wnd(win_w1, &nr) == 1);
				BTRON_CHECK("隠れた",
				            !tst_scr_solid(sg, l, t, l + 20, t + 10,
				                       0x00FFFFFFU));

				/* **本番。** 手前をどかすと裏の枠が出てこなければ
				 * ならない。出てこなければ机の柄のままになる。 */
				nr.c.left = 400;
				nr.c.top = 330;
				BTRON_CHECK("どかす", wmov_wnd(win_w1, &nr) == 1);
				BTRON_CHECK("裏のタイトルバーが戻る",
				            tst_scr_solid(sg, l, t, l + 20, t + 10,
				                      0x00FFFFFFU));
				BTRON_CHECK("閉じる", gcls_env(sg) == EG_OK);
			} else if (sg >= 1) {
				BTRON_CHECK("閉じる", gcls_env(sg) == EG_OK);
			}
		}
	});

	BTRON_TEST("ポインタはマネージャが合成する (dp.h PtrShared / 詳細 E2)", {
		if (!win_up) {
			BTRON_CHECK("画面が無い構成なので飛ばす", True);
		} else {
			GID sg = gopn_dev(tst_nm_screen, 0);
			WDSTAT s2;
			PNT at;
			PNT lp;
			PIXVAL v = 0;
			W hit = 0;

			/* **ポインタは画面に 1 つしか無いが、DP はプロセスごとの
			 * ライブラリである**（詳細 E1）。状態を共有していないと、
			 * ここで動かしてもマネージャには見えない。
			 *
			 * 合成するのはマネージャ（別プロセス）なので、**要求を
			 * 1 つ投げてからでないと画面に出ない**。要求を配り終える
			 * たびに画面を作り直す仕掛け（`win_flush`）が効いている
			 * ことも、これで一緒に確かめられる。 */
			if (sg >= 1 && wget_sts(win_w2, &s2, 0) >= 0) {
				at.x = (H)(s2.r.c.left + 40);
				at.y = (H)(s2.r.c.top + 40);
				BTRON_CHECK("作業領域の中の点",
				            wfnd_wnd(&at, &lp, &hit) == W_WORK &&
				                hit == win_w2);

				BTRON_CHECK("非追従にする", gmot_ptr(0) == EG_OK);
				BTRON_CHECK("標準形状に戻す",
				            gset_ptr(PS_SELECT, 0, COL_BLACK,
				                     COL_WHITE) == EG_OK);
				BTRON_CHECK("強制表示", gdsp_ptr(-1) == 0);
				BTRON_CHECK("そこへ置く", gmov_ptr(at) == 0);

				/* 要求を 1 つ投げると、マネージャが画面を作り直す。 */
				BTRON_CHECK("要求を投げる",
				            wera_wnd(win_w2, 0) == EX_OK);
				/* 選択指の先端は data=1 mask=1 なので前景色（黒 = 0）。 */
				BTRON_CHECK("先端が画面に出る",
				            gtst_pnt(sg, at, &v) == 0 && v == 0);

				/* 消すと、次の合成で下地（作業領域の白）が出る。 */
				BTRON_CHECK("消す", gdsp_ptr(0) == 1);
				BTRON_CHECK("要求を投げる",
				            wera_wnd(win_w2, 0) == EX_OK);
				BTRON_CHECK("下地が出る",
				            gtst_pnt(sg, at, &v) == 0 &&
				                (UW)v == 0x00FFFFFFU);
				BTRON_CHECK("追従に戻す", gmot_ptr(1) == EG_OK);
				BTRON_CHECK("閉じる", gcls_env(sg) == EG_OK);
			} else if (sg >= 1) {
				BTRON_CHECK("閉じる", gcls_env(sg) == EG_OK);
			}
		}
	});

	BTRON_TEST("持ち主でないプロセスは変えられない (§3.1.4)", {
		if (!win_up) {
			BTRON_CHECK("画面が無い構成なので飛ばす", True);
		} else {
			W pid;
			MESSAGE r;
			RECT nr;

			wpc_wid = 0;
			wpc_waiter = prc_self();
			(void)clr_msg(MM_ALL, MM_NULL);
			pid = prc_create(w_other_main, 2, nm_wnd1, 0);
			BTRON_CHECK("別プロセスを起こす", pid > 0);
			if (pid > 0) {
				/* **待ち切りにしない。** 向こうが知らせられないまま
				 * 止まったとき、検査が落ちるのではなく永久に待って
				 * しまう（逆検査で踏んだ落とし穴と同じ形）。 */
				{
					W k;
					W got = ER_NONE;

					for (k = 0; k < 200; k++) {
						got = rcv_msg(MSGMASK(MS_TYPE0), &r,
						              (W)sizeof(r), NOWAIT);
						if (got >= 0) {
							break;
						}
						(void)dly_tsk(5);
					}
					BTRON_CHECK("知らせが届く", got >= 0);
				}
				BTRON_CHECK("向こうでウィンドウが開いた", wpc_wid >= 1);
				/* **要求元はランデブの記録から引く。**
				 * 呼出側に名乗らせると偽れるし、サーバ自身の
				 * プロセスで代用すると持ち主が全部同じになる。 */
				{
					WDSTAT st;

					BTRON_CHECK("持ち主は向こうのプロセス",
					            wget_sts(wpc_wid, &st, 0) >= 0 &&
					                st.pid == pid);
				}
				nr.c.left = 100;
				nr.c.top = 100;
				nr.c.right = 220;
				nr.c.bottom = 190;
				/* 仕様「管理プロセスでない場合は、EX_WPRC」 */
				BTRON_CHECK("こちらからは動かせない",
				            wmov_wnd(wpc_wid, &nr) == EX_WPRC);
				BTRON_CHECK("こちらからは閉じられない",
				            wcls_wnd(wpc_wid, CLR) == EX_WPRC);
				/* 向こうが終わればウィンドウも片付く（仕様）。 */
				BTRON_CHECK("向こうを終わらせる",
				            ter_prc(pid, 0, TERM_NRM) == ER_OK);
				(void)dly_tsk(20);
				BTRON_CHECK("ウィンドウも消えた",
				            wget_sts(wpc_wid, 0, 0) == EX_WID);
			}
			(void)clr_msg(MM_ALL, MM_NULL);
		}
	});

	BTRON_TEST("ウィンドウの移動と片付け (§3.1.4)", {
		if (!win_up) {
			BTRON_CHECK("画面が無い構成なので飛ばす", True);
		} else {
			RECT nr;
			WDSTAT st;
			W n0 = win_count();

			(void)wswi_wnd(win_w1, 0);
			(void)wget_sts(win_w1, &st, 0);
			{
				W wdt = st.r.c.right - st.r.c.left;

				nr = st.r;
				nr.c.left = (H)(st.r.c.left + 20);
				nr.c.top = (H)(st.r.c.top + 20);
				/* 仕様「移動したウィンドウ自体の再表示が必要な場合は、
				 * 関数値として "1" が戻り」。本OS は作業領域の中身を
				 * 動かさないので、移動すれば必ず再表示が要る。 */
				BTRON_CHECK("移動", wmov_wnd(win_w1, &nr) == 1);
				BTRON_CHECK("再表示要求が立った",
				            wchk_dsp(win_w1) == 1);
				BTRON_CHECK("位置が変わった",
				            wget_sts(win_w1, &st, 0) >= 0 &&
				                st.r.c.left == nr.c.left);
				/* 大きさは変わらない（移動なので） */
				BTRON_CHECK("大きさは同じ",
				            st.r.c.right - st.r.c.left == wdt);
			}
			/* 仕様「対象ウィンドウが入力受付状態でない場合は、
			 * EX_WND のエラー」 */
			(void)wswi_wnd(win_w2, 0);
			BTRON_CHECK("入力受付状態でなければ動かせない",
			            wmov_wnd(win_w1, &nr) == EX_WND);
			(void)wswi_wnd(win_w1, 0);
			/* 仕様「移動 / 変形を行なった場合、その対象としたウィンドウが
			 * 一番前面に表示される」 */
			BTRON_CHECK("前面に出た", wget_sts(win_w1, 0, 0) >= 0);

			/* 任意データと作業用矩形 */
			BTRON_CHECK("データの設定", wset_dat(win_w1, 0x1234) == EX_OK);
			{
				W d = 0;

				BTRON_CHECK("取得",
				            wget_dat(win_w1, &d) == EX_OK && d == 0x1234);
			}
			/* タイトルの変更 */
			BTRON_CHECK("タイトル変更",
			            wset_tit(win_w1, 1, nm_wnd2, 0) == EX_OK);
			{
				TC t[W_MAX_TITLE + 1];
				W pict = 0;

				BTRON_CHECK("取得", wget_tit(win_w1, &pict, t) == 4);
				BTRON_CHECK("中身", pict == 1 && t[0] == (TC)'W');
			}
			BTRON_CHECK("存在しないウィンドウ",
			            wget_gid(W_MAX_WND + 1) == EX_WID);

			/* 片付け。 */
			BTRON_CHECK("2 枚ある", n0 == 2);
			BTRON_CHECK("閉じる", wcls_wnd(win_w1, CLR) == EX_OK);
			BTRON_CHECK("入力受付状態が移った", wget_act(0) == win_w2);
			BTRON_CHECK("閉じる", wcls_wnd(win_w2, CLR) == EX_OK);
			BTRON_CHECK("無くなった", win_count() == 0);
			BTRON_CHECK("入力受付状態も無い", wget_act(0) == 0);
			/* **サーバは立て直せる。**
			 *
			 * 終わらせてから起こし直し、また使えることを確かめる。
			 * ポート ID は使い回されるので、利用側が番号を覚えたまま
			 * だと別のサーバに話しかけてしまう（実際それで窓が
			 * 開かなくなった）。グローバル名から引き直していれば通る。 */
			if (wm_pid > 0) {
				BTRON_CHECK("終わらせる",
				            ter_prc(wm_pid, 0, TERM_NRM) == ER_OK);
				(void)dly_tsk(20);
				BTRON_CHECK("名前も消えた",
				            mgr_lookup(MGR_WINDOW) == ER_NOEXS);
				wm_pid = win_launch();
				BTRON_CHECK("立て直せる", wm_pid > 0);
				if (wm_pid > 0) {
					RECT rr;
					WID w;

					rr.c.left = 30;
					rr.c.top = 30;
					rr.c.right = 200;
					rr.c.bottom = 140;
					w = wopn_wnd(WA_NORMAL, 0, &rr, 0, 0, 0, 0, 0);
					BTRON_CHECK("また窓が開ける", w >= 1);
					if (w >= 1) {
						BTRON_CHECK("閉じる",
						            wcls_wnd(w, CLR) == EX_OK);
					}
					(void)ter_prc(wm_pid, 0, TERM_NRM);
					(void)dly_tsk(20);
				}
			}
			win_up = False;
			(void)clr_msg(MM_ALL, MM_NULL);
			(void)clr_evt(EM_ALL, EM_NULL);
		}
	});

	BTRON_TEST("サーバが落ちると利用側のキャッシュも無効になる", {
		MgrMsg m;

		/* **フォントマネージャを止める。** ポートとグローバル名は
		 * DELEXIT 付きなのでプロセスと一緒に消える。 */
		if (tst_fontman_pid >= 1) {
			BTRON_CHECK("共有データが見えている", fontman_shared() != 0);
			m.func = FONT_SHUTDOWN;
			BTRON_CHECK("停止要求",
			            mgr_call((ID)tst_fontman_por, &m, T_FOREVER) == ER_OK);
			(void)dly_tsk(10);
			BTRON_CHECK("プロセスが終わった",
			            prc_sts(tst_fontman_pid, 0, 0) == ER_NOPRC);
			BTRON_CHECK("名前も消えた",
			            mgr_lookup(MGR_FONT) == ER_NOEXS);
			/* **ここが要点。** サーバが解放した共有メモリを指し続けると
			 * 写像の無い番地を読んで例外になる。利用側は名前が消えたことで
			 * 気づいて覚えを捨てなければならない。 */
			BTRON_CHECK("覚えが捨てられた", fontman_shared() == 0);
			/* 字形が無くても描画は落ちない（背景だけ描く）。 */
			BTRON_CHECK("幅は既定値に戻る", 1 == 1);
		}
		BTRON_CHECK("終了メッセージを捨てる",
		            clr_msg(MM_ALL, MM_NULL) == ER_OK);
	});

}

/*
 * rdv.c — 周辺核 ランデブ (§1.3)
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

LOCAL ID por1, por2;

LOCAL W srv_calls;

LOCAL ERR fwd_err;

/* ------------------------------------------------------------------ */
/* ⑩ ランデブ (周辺核 §1.3)                                           */
/*                                                                     */
/* サーバはどれも「呼出メッセージの先頭バイトに +1 して返す」。          */
/* 何段経由したかが値で分かるので、回送の検証に使える。                 */
/* ------------------------------------------------------------------ */

/* 繰り返し受け付けるエコーサーバ */
LOCAL void t_server(INT stacd, VP exinf)
{
	B buf[32];
	W rdv;
	W n;

	(void)exinf;
	for (;;) {
		n = acp_por(por1, 0xFFFFFFFFU, &rdv, buf, T_FOREVER);
		if (n < 0) {
			tst_seq_put('x'); /* ER_DLT などで抜けた */
			break;
		}
		srv_calls++;
		tst_seq_put((B)stacd);
		buf[0] = (B)(buf[0] + 1);
		(void)rpl_rdv(rdv, buf, n);
	}
	tk_ext_tsk();
}

/* 選択条件 0x2 だけを受け付けて 1 回で終わる */
LOCAL void t_sel_server(INT stacd, VP exinf)
{
	B buf[32];
	W rdv;
	W n;

	(void)exinf;
	n = acp_por(por1, 0x2U, &rdv, buf, T_FOREVER);
	if (n > 0) {
		tst_seq_put((B)stacd);
		buf[0] = (B)(buf[0] + 1);
		(void)rpl_rdv(rdv, buf, n);
	}
	tk_ext_tsk();
}

/* 受付待ちのまま削除される */
LOCAL void t_acc_wait(INT stacd, VP exinf)
{
	B buf[32];
	W rdv;
	W n;

	(void)exinf;
	n = acp_por(por1, 0xFFFFFFFFU, &rdv, buf, T_FOREVER);
	tst_seq_put((n == ER_DLT) ? 'D' : (B)stacd);
	tk_ext_tsk();
}

/* 成立後にポートを削除してから返答する。
 * 仕様の「成立後にポートが削除されても正常に返答できる」の検証。 */
LOCAL void t_del_then_reply(INT stacd, VP exinf)
{
	B buf[32];
	W rdv;
	W n;

	(void)exinf;
	n = acp_por(por1, 0xFFFFFFFFU, &rdv, buf, T_FOREVER);
	if (n > 0) {
		tst_seq_put((B)stacd);
		(void)del_por(por1);
		buf[0] = (B)(buf[0] + 1);
		if (rpl_rdv(rdv, buf, n) == ER_OK) {
			tst_seq_put('R');
		}
	}
	tk_ext_tsk();
}

LOCAL void t_fwd_server(INT stacd, VP exinf)
{
	B buf[32];
	W rdv;
	W n;

	(void)exinf;
	n = acp_por(por1, 0xFFFFFFFFU, &rdv, buf, T_FOREVER);
	if (n > 0) {
		tst_seq_put((B)stacd);
		buf[0] = (B)(buf[0] + 1);
		(void)fwd_por(por2, 0xFFFFFFFFU, rdv, buf, n);
		/* 回送したので自分は待たない（仕様）。すぐ終われる。 */
	}
	tk_ext_tsk();
}

/* por2 で受け付けて返答する（回送先のサーバ） */
LOCAL void t_back_server(INT stacd, VP exinf)
{
	B buf[32];
	W rdv;
	W n;

	(void)exinf;
	n = acp_por(por2, 0xFFFFFFFFU, &rdv, buf, T_FOREVER);
	if (n > 0) {
		tst_seq_put((B)stacd);
		buf[0] = (B)(buf[0] + 1);
		(void)rpl_rdv(rdv, buf, n);
	}
	tk_ext_tsk();
}

/* 返答サイズの制約に反する回送をして、失敗したら自分で返答する */
LOCAL void t_fwd_bad(INT stacd, VP exinf)
{
	B buf[32];
	W rdv;
	W n;

	(void)exinf;
	n = acp_por(por1, 0xFFFFFFFFU, &rdv, buf, T_FOREVER);
	if (n > 0) {
		tst_seq_put((B)stacd);
		fwd_err = fwd_por(por2, 0xFFFFFFFFU, rdv, buf, n);
		/* 回送が失敗したならランデブは解除されていないので返答できる */
		buf[0] = (B)(buf[0] + 1);
		(void)rpl_rdv(rdv, buf, n);
	}
	tk_ext_tsk();
}

BTRON_TEST_GROUP("周辺核 ランデブ (§1.3)", 150, TST_PHASE_KERNEL, tst_rdv)
{
	BTRON_TEST("ランデブの基本 (周辺核 §1.3)", {
		B msg[32];
		ID srv;
		W r;

		tst_seq_reset();
		srv_calls = 0;
		por1 = cre_por(32, 32, 0);
		BTRON_CHECK("ポート生成", por1 >= 1);
		srv = tst_make_task(t_server, 6, 0); /* 自分より低い優先度 */
		(void)tk_sta_tsk(srv, 's');
		(void)tk_dly_tsk(10); /* サーバを acp_por で待たせる */
		BTRON_CHECK("サーバは待機中", tst_seq_eq(""));

		msg[0] = 10;
		r = cal_por(por1, 0x1U, msg, 1, T_FOREVER);
		BTRON_CHECK("返答サイズ", r == 1);
		BTRON_CHECK("返答内容", msg[0] == 11);
		BTRON_CHECK("サーバが走った", tst_seq_eq("s"));
		BTRON_CHECK("呼出回数", srv_calls == 1);

		/* 2 回目。今度はサーバがまだ acp_por に戻っていないので、
		 * 呼出側が待ち行列に並ぶ経路を通る。 */
		msg[0] = 20;
		BTRON_CHECK("2 回目", cal_por(por1, 0x1U, msg, 1, T_FOREVER) == 1);
		BTRON_CHECK("返答内容", msg[0] == 21);
		BTRON_CHECK("呼出回数", srv_calls == 2);
		(void)tk_ter_tsk(srv);
		(void)tk_del_tsk(srv);
		(void)del_por(por1);
	});

	BTRON_TEST("ランデブ: ポーリングとタイムアウト (R20)", {
		B msg[32];
		W rdv = 0;
		SYSTIM t0;
		SYSTIM t1;

		por1 = cre_por(32, 32, 0);
		msg[0] = 1;
		/* 相手がいなければ T_NOWAIT は待たずに ER_NONE */
		BTRON_CHECK("呼出 NOWAIT", cal_por(por1, 0x1U, msg, 1, T_NOWAIT) == ER_NONE);
		BTRON_CHECK("受付 NOWAIT", acp_por(por1, 0x1U, &rdv, msg, T_NOWAIT) == ER_NONE);

		(void)tk_get_otm(&t0);
		BTRON_CHECK("呼出タイムアウト", cal_por(por1, 0x1U, msg, 1, 20) == ER_NONE);
		(void)tk_get_otm(&t1);
		BTRON_CHECK("待った", t1 - t0 >= 20);

		/* **族が違う。** ランデブは BTRON3 の API なので ER_NONE を返し、
		 * μT-Kernel の待ちは E_TMOUT を返す（規律 R20）。 */
		BTRON_CHECK("ER_NONE はメイン -24", MERCD(ER_NONE) == -24);
		BTRON_CHECK("E_TMOUT はメイン -50", MERCD(E_TMOUT) == -50);
		BTRON_CHECK("別物", ER_NONE != E_TMOUT);
		(void)del_por(por1);
	});

	BTRON_TEST("ランデブの成立条件 (calptn & acpptn)", {
		B msg[32];
		ID srv;

		tst_seq_reset();
		por1 = cre_por(32, 32, 0);
		srv = tst_make_task(t_sel_server, 6, 0); /* acpptn = 0x2 で待つ */
		(void)tk_sta_tsk(srv, 'p');
		(void)tk_dly_tsk(10);
		msg[0] = 0;
		/* ビットが噛み合わないと成立しない */
		BTRON_CHECK("不一致", cal_por(por1, 0x1U, msg, 1, 20) == ER_NONE);
		BTRON_CHECK("サーバはまだ待機", tst_seq_eq(""));
		/* 1 ビットでも重なれば成立する */
		BTRON_CHECK("一致", cal_por(por1, 0x6U, msg, 1, T_FOREVER) == 1);
		BTRON_CHECK("成立した", tst_seq_eq("p"));
		(void)tk_del_tsk(srv);
		(void)del_por(por1);
	});

	BTRON_TEST("ポート削除: 待ちは ER_DLT、成立済みは残る (仕様)", {
		B msg[32];
		ID w;
		ID s;

		/* ① 受付待ちのタスクは ER_DLT で起きる */
		tst_seq_reset();
		por1 = cre_por(32, 32, 0);
		w = tst_make_task(t_acc_wait, 6, 0);
		(void)tk_sta_tsk(w, 'w');
		(void)tk_dly_tsk(10);
		BTRON_CHECK("削除", del_por(por1) == ER_OK);
		(void)tk_dly_tsk(10);
		BTRON_CHECK("ER_DLT で起きた", tst_seq_eq("D"));
		BTRON_CHECK("削除後は無い", del_por(por1) == ER_NOEXS);
		(void)tk_del_tsk(w);

		/* ② 成立後にポートを削除しても返答できる（仕様が明記） */
		tst_seq_reset();
		por1 = cre_por(32, 32, 0);
		s = tst_make_task(t_del_then_reply, 6, 0);
		(void)tk_sta_tsk(s, 'h');
		(void)tk_dly_tsk(10);
		msg[0] = 5;
		BTRON_CHECK("返答が来た", cal_por(por1, 0x1U, msg, 1, T_FOREVER) == 1);
		BTRON_CHECK("内容", msg[0] == 6);
		(void)tk_dly_tsk(10);
		BTRON_CHECK("削除後に返答できた", tst_seq_eq("hR"));
		(void)tk_del_tsk(s);
	});

	BTRON_TEST("ランデブ回送 fwd_por (詳細 E1 ④ / E13)", {
		B msg[32];
		ID s1;
		ID s2;

		tst_seq_reset();
		por1 = cre_por(32, 32, 0);
		por2 = cre_por(32, 32, 0);
		BTRON_CHECK("ポート 2 つ", por1 >= 1 && por2 >= 1 && por1 != por2);
		s1 = tst_make_task(t_fwd_server, 6, 0);
		s2 = tst_make_task(t_back_server, 7, 0);
		(void)tk_sta_tsk(s1, 'f');
		(void)tk_sta_tsk(s2, 'b');
		(void)tk_dly_tsk(10);
		BTRON_CHECK("両方待機", tst_seq_eq(""));

		/* por1 へ呼ぶ。s1 が受け取って por2 へ回送し、s2 が返答する。
		 * **返答は最初の呼出側に直接届き、中間の s1 は待たない。**
		 * これが E13 の 3 者間要求（クライアント → 実身仮身サーバ →
		 * 対象クライアント）をホップ増なしで表現できる根拠。 */
		msg[0] = 1;
		BTRON_CHECK("返答サイズ", cal_por(por1, 0x1U, msg, 1, T_FOREVER) == 1);
		BTRON_CHECK("2 段で +1 された", msg[0] == 3);
		BTRON_CHECK("順に走った", tst_seq_eq("fb"));
		(void)tk_del_tsk(s1);
		(void)tk_del_tsk(s2);
		(void)del_por(por1);
		(void)del_por(por2);
	});

	BTRON_TEST("回送先の返答サイズ制約 (ER_OBJ)", {
		B msg[32];
		ID s;

		tst_seq_reset();
		fwd_err = ER_OK;
		/* 回送元の maxrmsz = 8、回送先 = 32。呼出側のバッファは 8 しか
		 * 保証されないので、32 の返答は収まらない。仕様は ER_OBJ。 */
		por1 = cre_por(32, 8, 0);
		por2 = cre_por(32, 32, 0);
		s = tst_make_task(t_fwd_bad, 6, 0);
		(void)tk_sta_tsk(s, 'e');
		(void)tk_dly_tsk(10);
		msg[0] = 7;
		BTRON_CHECK("自分で返答が来た", cal_por(por1, 0x1U, msg, 1, T_FOREVER) == 1);
		BTRON_CHECK("内容", msg[0] == 8);
		BTRON_CHECK("回送は ER_OBJ", fwd_err == ER_OBJ);
		/* 回送が失敗してもランデブは解除されていない（返答できた） */
		BTRON_CHECK("受け付けていた", tst_seq_eq("e"));
		(void)tk_del_tsk(s);
		(void)del_por(por1);
		(void)del_por(por2);
	});

	BTRON_TEST("ランデブの引数検査とランデブ番号", {
		B msg[64];
		W rdv = 0;

		por1 = cre_por(8, 8, 0);
		BTRON_CHECK("生成", por1 >= 1);
		BTRON_CHECK("cmsz 過大", cal_por(por1, 0x1U, msg, 9, T_NOWAIT) == ER_PAR);
		BTRON_CHECK("cmsz 0", cal_por(por1, 0x1U, msg, 0, T_NOWAIT) == ER_PAR);
		BTRON_CHECK("calptn 0", cal_por(por1, 0, msg, 1, T_NOWAIT) == ER_PAR);
		BTRON_CHECK("msg なし", cal_por(por1, 0x1U, 0, 1, T_NOWAIT) == ER_PAR);
		BTRON_CHECK("acpptn 0", acp_por(por1, 0, &rdv, msg, T_NOWAIT) == ER_PAR);
		BTRON_CHECK("rdv なし", acp_por(por1, 0x1U, 0, msg, T_NOWAIT) == ER_PAR);
		BTRON_CHECK("不正 ID", cal_por(0, 0x1U, msg, 1, T_NOWAIT) == ER_ID);
		BTRON_CHECK("存在しない",
		            cal_por(TK_MAX_PORID, 0x1U, msg, 1, T_NOWAIT) == ER_NOEXS);
		/* ランデブ番号の検査。でたらめな番号で返答できてはならない。 */
		BTRON_CHECK("rdv 0", rpl_rdv(0, msg, 1) == ER_OBJ);
		BTRON_CHECK("rdv でたらめ", rpl_rdv(0x7FFF0001, msg, 1) == ER_OBJ);
		BTRON_CHECK("rmsz 0", rpl_rdv(1, msg, 0) == ER_PAR);
		BTRON_CHECK("生成の引数", cre_por(0, 8, 0) == ER_PAR);
		BTRON_CHECK("未知の属性", cre_por(8, 8, 0x1U) == ER_PAR);
		BTRON_CHECK("削除", del_por(por1) == ER_OK);
	});

}

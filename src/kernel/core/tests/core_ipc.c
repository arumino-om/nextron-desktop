/*
 * core_ipc.c — 中心核 ミューテックス・メールボックス・メモリプール・メッセージバッファ
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

LOCAL ID mtx1;

LOCAL ID mbx1;

LOCAL ID mbf1;

/* ------------------------------------------------------------------ */
/* ⑥ ミューテックスの優先度継承                                       */
/*                                                                     */
/*   L(pri 20) が TA_INHERIT のミューテックスをロックしている状態で、    */
/*   H(pri 4) がロックを待つと、L の優先度が 4 まで引き上げられる。      */
/*   引き上げないと、中優先度の M(pri 10) が L を追い越し続けて、        */
/*   H の待ち時間に上限が無くなる（上限のない優先度逆転）。              */
/* ------------------------------------------------------------------ */

LOCAL void t_mtx_low(INT stacd, VP exinf)
{
	(void)exinf;
	(void)tk_loc_mtx(mtx1, TMO_FEVR);
	tst_seq_put((B)stacd); /* ロックした */
	(void)tk_slp_tsk(TMO_FEVR); /* ここで H が待ちに入る */
	tst_seq_put('L');
	(void)tk_unl_mtx(mtx1);
	tk_ext_tsk();
}

LOCAL void t_mtx_high(INT stacd, VP exinf)
{
	(void)exinf;
	tst_seq_put('h');
	(void)tk_loc_mtx(mtx1, TMO_FEVR);
	tst_seq_put((B)stacd); /* 譲られた */
	(void)tk_unl_mtx(mtx1);
	tk_ext_tsk();
}

LOCAL void t_mtx_mid(INT stacd, VP exinf)
{
	(void)exinf;
	tst_seq_put((B)stacd);
	tk_ext_tsk();
}

/* ------------------------------------------------------------------ */
/* ⑦ メールボックス                                                   */
/* ------------------------------------------------------------------ */

LOCAL void t_mbx_recv(INT stacd, VP exinf)
{
	T_MSG *m = 0;

	(void)exinf;
	if (tk_rcv_mbx(mbx1, &m, TMO_FEVR) == E_OK && m != 0) {
		tst_seq_put((B)stacd);
	}
	tk_ext_tsk();
}

/* ------------------------------------------------------------------ */
/* ⑧ メッセージバッファ（バッファサイズ 0 = 同期通信）                */
/* ------------------------------------------------------------------ */

LOCAL void t_mbf_send(INT stacd, VP exinf)
{
	B buf[8];

	(void)exinf;
	buf[0] = (B)stacd;
	/* バッファが 0 なら受信側が来るまで待つ */
	if (tk_snd_mbf(mbf1, buf, 1, TMO_FEVR) == E_OK) {
		tst_seq_put((B)stacd);
	}
	tk_ext_tsk();
}

LOCAL void t_mbf_recv(INT stacd, VP exinf)
{
	B buf[8];

	(void)exinf;
	if (tk_rcv_mbf(mbf1, buf, TMO_FEVR) == 1) {
		tst_seq_put(buf[0]);
		tst_seq_put((B)stacd);
	}
	tk_ext_tsk();
}

BTRON_TEST_GROUP("中心核 ミューテックス・メールボックス・メモリプール・メッセージバッファ", 130, TST_PHASE_KERNEL, tst_core_ipc)
{
	BTRON_TEST("ミューテックスの基本 (μT-Kernel 拡張同期)", {
		T_CMTX c;
		T_RMTX r;
		W i;

		for (i = 0; i < 8; i++) {
			c.dsname[i] = 0;
		}
		c.exinf = 0;
		c.mtxatr = TA_TFIFO;
		c.ceilpri = 0;
		mtx1 = tk_cre_mtx(&c);
		BTRON_CHECK("生成", mtx1 >= 1);
		BTRON_CHECK("未ロック", tk_ref_mtx(mtx1, &r) == E_OK && r.htsk == 0);
		BTRON_CHECK("ロック", tk_loc_mtx(mtx1, TMO_POL) == E_OK);
		BTRON_CHECK("保持者は自分",
		            tk_ref_mtx(mtx1, &r) == E_OK && r.htsk == tk_get_tid());
		/* 多重ロックは E_ILUSE。セマフォとの違い。 */
		BTRON_CHECK("多重ロック", tk_loc_mtx(mtx1, TMO_POL) == E_ILUSE);
		BTRON_CHECK("解除", tk_unl_mtx(mtx1) == E_OK);
		/* ロックしていないのに解除するのも E_ILUSE */
		BTRON_CHECK("未ロックで解除", tk_unl_mtx(mtx1) == E_ILUSE);
		BTRON_CHECK("不正 ID", tk_loc_mtx(0, TMO_POL) == E_ID);
		BTRON_CHECK("削除", tk_del_mtx(mtx1) == E_OK);
		BTRON_CHECK("削除後", tk_ref_mtx(mtx1, &r) == E_NOEXS);

		/* TA_CEILING の引数検査 */
		c.mtxatr = TA_CEILING;
		c.ceilpri = 0;
		BTRON_CHECK("上限優先度 0", tk_cre_mtx(&c) == E_PAR);
		c.ceilpri = TK_MAX_TSKPRI + 1;
		BTRON_CHECK("上限優先度 過大", tk_cre_mtx(&c) == E_PAR);
	});

	BTRON_TEST("ミューテックスの優先度継承 (TA_INHERIT)", {
		T_CMTX c;
		T_RTSK r;
		ID lo;
		ID hi;
		ID mid;
		W i;

		for (i = 0; i < 8; i++) {
			c.dsname[i] = 0;
		}
		c.exinf = 0;
		c.mtxatr = TA_INHERIT;
		c.ceilpri = 0;
		mtx1 = tk_cre_mtx(&c);
		BTRON_CHECK("生成", mtx1 >= 1);

		tst_seq_reset();
		lo = tst_make_task(t_mtx_low, 20, 0);  /* 低優先度。先にロックする */
		hi = tst_make_task(t_mtx_high, 4, 0);  /* 高優先度。自分より上 */
		mid = tst_make_task(t_mtx_mid, 10, 0); /* 中優先度。割り込む側 */
		(void)tk_sta_tsk(lo, 'l');
		(void)tk_dly_tsk(10); /* L にロックさせる */
		BTRON_CHECK("L がロックした", tst_seq_eq("l"));
		BTRON_CHECK("優先度は 20", tk_ref_tsk(lo, &r) == E_OK && r.tskpri == 20);

		/* H を起動すると、自分(pri 5)より高いので即座に走り、
		 * ロック待ちに入って L の優先度を 4 へ引き上げる。 */
		(void)tk_sta_tsk(hi, 'H');
		BTRON_CHECK("H が待ちに入った", tst_seq_eq("lh"));
		BTRON_CHECK("L の現在優先度が継承された",
		            tk_ref_tsk(lo, &r) == E_OK && r.tskpri == 4);
		BTRON_CHECK("ベース優先度は変わらない", r.tskbpri == 20);

		/* 中優先度のタスクを起動しても、継承後の L (pri 4) には勝てない。
		 * 継承していなければ M(10) が L(20) を追い越してしまう。 */
		(void)tk_sta_tsk(mid, 'M');
		(void)tk_wup_tsk(lo); /* L を進める。L → 解除 → H → M の順になる */
		(void)tk_dly_tsk(20);
		if (!tst_seq_eq("lhLHM")) {
			tst_seq_show("lhLHM");
		}
		BTRON_CHECK("継承により L が M を追い越した", tst_seq_eq("lhLHM"));
		/* ロックが無くなったのでベース優先度に戻る（簡略化した規則） */
		BTRON_CHECK("優先度が戻った",
		            tk_ref_tsk(lo, &r) == E_OK && r.tskpri == 20);
		(void)tk_del_tsk(lo);
		(void)tk_del_tsk(hi);
		(void)tk_del_tsk(mid);
		(void)tk_del_mtx(mtx1);
	});

	BTRON_TEST("ミューテックスの優先度上限 (TA_CEILING)", {
		T_CMTX c;
		T_RTSK r;
		W i;

		for (i = 0; i < 8; i++) {
			c.dsname[i] = 0;
		}
		c.exinf = 0;
		c.mtxatr = TA_CEILING;
		c.ceilpri = 3; /* 自分 (pri 5) より高い上限 */
		mtx1 = tk_cre_mtx(&c);
		BTRON_CHECK("生成", mtx1 >= 1);
		BTRON_CHECK("ロック", tk_loc_mtx(mtx1, TMO_POL) == E_OK);
		/* ロックしたら上限優先度まで引き上げられる */
		BTRON_CHECK("上限まで上がった",
		            tk_ref_tsk(TSK_SELF, &r) == E_OK && r.tskpri == 3);
		BTRON_CHECK("ベースはそのまま", r.tskbpri == 5);
		/* 上限より高いベース優先度にはできない（仕様） */
		BTRON_CHECK("chg_pri は E_ILUSE", tk_chg_pri(TSK_SELF, 2) == E_ILUSE);
		BTRON_CHECK("上限と同じなら可", tk_chg_pri(TSK_SELF, 3) == E_OK);
		BTRON_CHECK("解除", tk_unl_mtx(mtx1) == E_OK);
		BTRON_CHECK("ベースに戻った",
		            tk_ref_tsk(TSK_SELF, &r) == E_OK && r.tskpri == 3);
		(void)tk_chg_pri(TSK_SELF, 5); /* 元に戻す */
		(void)tk_del_mtx(mtx1);

		/* 上限より高いベース優先度のタスクはロックできない */
		c.ceilpri = 8; /* 自分 (pri 5) より低い上限 */
		mtx1 = tk_cre_mtx(&c);
		BTRON_CHECK("上限優先度違反", tk_loc_mtx(mtx1, TMO_POL) == E_ILUSE);
		(void)tk_del_mtx(mtx1);
	});

	BTRON_TEST("タスク終了でミューテックスは自動解除 (仕様)", {
		T_CMTX c;
		T_RMTX r;
		ID lo;
		W i;

		for (i = 0; i < 8; i++) {
			c.dsname[i] = 0;
		}
		c.exinf = 0;
		c.mtxatr = TA_TFIFO;
		c.ceilpri = 0;
		mtx1 = tk_cre_mtx(&c);

		tst_seq_reset();
		lo = tst_make_task(t_mtx_low, 20, 0);
		(void)tk_sta_tsk(lo, 'l');
		(void)tk_dly_tsk(10);
		BTRON_CHECK("ロック中", tk_ref_mtx(mtx1, &r) == E_OK && r.htsk == lo);
		/* 強制終了する。仕様はロックを自動解除すると規定している。
		 * これをしないと資源が永久に握られる。 */
		BTRON_CHECK("強制終了", tk_ter_tsk(lo) == E_OK);
		BTRON_CHECK("自動解除された", tk_ref_mtx(mtx1, &r) == E_OK && r.htsk == 0);
		BTRON_CHECK("自分がロックできる", tk_loc_mtx(mtx1, TMO_POL) == E_OK);
		(void)tk_unl_mtx(mtx1);
		(void)tk_del_tsk(lo);
		(void)tk_del_mtx(mtx1);
	});

	BTRON_TEST("メールボックス", {
		T_CMBX c;
		T_RMBX r;
		T_MSG m1;
		T_MSG m2;
		T_MSG *got = 0;
		ID rcv;
		W i;

		for (i = 0; i < 8; i++) {
			c.dsname[i] = 0;
		}
		c.exinf = 0;
		c.mbxatr = TA_TFIFO | TA_MFIFO;
		mbx1 = tk_cre_mbx(&c);
		BTRON_CHECK("生成", mbx1 >= 1);
		BTRON_CHECK("空", tk_rcv_mbx(mbx1, &got, TMO_POL) == E_TMOUT);

		/* 送ってから受け取る。カーネルは**複製しない**のでポインタが一致する */
		BTRON_CHECK("送信", tk_snd_mbx(mbx1, &m1) == E_OK);
		BTRON_CHECK("送信", tk_snd_mbx(mbx1, &m2) == E_OK);
		BTRON_CHECK("先頭が見える", tk_ref_mbx(mbx1, &r) == E_OK && r.pk_msg == &m1);
		BTRON_CHECK("FIFO 1", tk_rcv_mbx(mbx1, &got, TMO_POL) == E_OK && got == &m1);
		BTRON_CHECK("FIFO 2", tk_rcv_mbx(mbx1, &got, TMO_POL) == E_OK && got == &m2);
		BTRON_CHECK("空に戻った", tk_rcv_mbx(mbx1, &got, TMO_POL) == E_TMOUT);

		/* 受信待ちのタスクへ直接渡す経路 */
		tst_seq_reset();
		rcv = tst_make_task(t_mbx_recv, 6, 0);
		(void)tk_sta_tsk(rcv, 'm');
		(void)tk_dly_tsk(10);
		BTRON_CHECK("待っている", tk_ref_mbx(mbx1, &r) == E_OK && r.wtsk == rcv);
		BTRON_CHECK("送信", tk_snd_mbx(mbx1, &m1) == E_OK);
		(void)tk_dly_tsk(10);
		BTRON_CHECK("受け取った", tst_seq_eq("m"));
		(void)tk_del_tsk(rcv);

		BTRON_CHECK("NULL", tk_snd_mbx(mbx1, 0) == E_PAR);
		BTRON_CHECK("削除", tk_del_mbx(mbx1) == E_OK);
		BTRON_CHECK("削除後", tk_rcv_mbx(mbx1, &got, TMO_POL) == E_NOEXS);
	});

	BTRON_TEST("メールボックスのメッセージ優先度 (TA_MPRI)", {
		T_CMBX c;
		T_MSG_PRI p1;
		T_MSG_PRI p2;
		T_MSG_PRI p3;
		T_MSG *got = 0;
		W i;

		for (i = 0; i < 8; i++) {
			c.dsname[i] = 0;
		}
		c.exinf = 0;
		c.mbxatr = TA_TFIFO | TA_MPRI;
		mbx1 = tk_cre_mbx(&c);
		p1.msgpri = 5;
		p2.msgpri = 1; /* 1 が最高（仕様） */
		p3.msgpri = 5; /* 同一優先度は FIFO */
		BTRON_CHECK("送信", tk_snd_mbx(mbx1, (T_MSG *)&p1) == E_OK);
		BTRON_CHECK("送信", tk_snd_mbx(mbx1, (T_MSG *)&p2) == E_OK);
		BTRON_CHECK("送信", tk_snd_mbx(mbx1, (T_MSG *)&p3) == E_OK);
		BTRON_CHECK("優先度 1 が先",
		            tk_rcv_mbx(mbx1, &got, TMO_POL) == E_OK && got == (T_MSG *)&p2);
		BTRON_CHECK("同一優先度は FIFO",
		            tk_rcv_mbx(mbx1, &got, TMO_POL) == E_OK && got == (T_MSG *)&p1);
		BTRON_CHECK("その次",
		            tk_rcv_mbx(mbx1, &got, TMO_POL) == E_OK && got == (T_MSG *)&p3);
		/* 優先度は正の値でなければならない（仕様） */
		p1.msgpri = 0;
		BTRON_CHECK("優先度 0", tk_snd_mbx(mbx1, (T_MSG *)&p1) == E_PAR);
		(void)tk_del_mbx(mbx1);
	});

	BTRON_TEST("固定長メモリプール", {
		T_CMPF c;
		T_RMPF r;
		VP b1 = 0;
		VP b2 = 0;
		VP b3 = 0;
		W i;

		for (i = 0; i < 8; i++) {
			c.dsname[i] = 0;
		}
		c.exinf = 0;
		c.mpfatr = TA_TFIFO;
		c.mpfcnt = 2;
		c.blfsz = 64;
		c.bufptr = 0;
		{
			ID id = tk_cre_mpf(&c);

			BTRON_CHECK("生成", id >= 1);
			BTRON_CHECK("空きは 2", tk_ref_mpf(id, &r) == E_OK && r.frbcnt == 2);
			BTRON_CHECK("獲得 1", tk_get_mpf(id, &b1, TMO_POL) == E_OK && b1 != 0);
			BTRON_CHECK("獲得 2", tk_get_mpf(id, &b2, TMO_POL) == E_OK && b2 != 0);
			BTRON_CHECK("別のブロック", b1 != b2);
			BTRON_CHECK("空きなし", tk_ref_mpf(id, &r) == E_OK && r.frbcnt == 0);
			BTRON_CHECK("枯渇", tk_get_mpf(id, &b3, TMO_POL) == E_TMOUT);
			BTRON_CHECK("タイムアウト", tk_get_mpf(id, &b3, 10) == E_TMOUT);
			/* 書いて読める = 本当に使えるメモリである */
			*(UW *)b1 = 0xB7120001U;
			*(UW *)b2 = 0xB7120002U;
			BTRON_CHECK("独立している",
			            *(UW *)b1 == 0xB7120001U && *(UW *)b2 == 0xB7120002U);
			BTRON_CHECK("返却", tk_rel_mpf(id, b1) == E_OK);
			BTRON_CHECK("空きが戻った", tk_ref_mpf(id, &r) == E_OK && r.frbcnt == 1);
			/* 他所のポインタを返されると空きリストが壊れるので弾く */
			BTRON_CHECK("範囲外", tk_rel_mpf(id, (VP)&r) == E_PAR);
			BTRON_CHECK("削除", tk_del_mpf(id) == E_OK);
		}
	});

	BTRON_TEST("可変長メモリプール", {
		T_CMPL c;
		T_RMPL r;
		VP b1 = 0;
		VP b2 = 0;
		VP b3 = 0;
		W free0;
		W i;

		for (i = 0; i < 8; i++) {
			c.dsname[i] = 0;
		}
		c.exinf = 0;
		c.mplatr = TA_TFIFO;
		c.mplsz = 4096;
		c.bufptr = 0;
		{
			ID id = tk_cre_mpl(&c);

			BTRON_CHECK("生成", id >= 1);
			BTRON_CHECK("初期状態", tk_ref_mpl(id, &r) == E_OK && r.frsz > 3000);
			free0 = r.frsz;
			BTRON_CHECK("獲得 1", tk_get_mpl(id, 100, &b1, TMO_POL) == E_OK);
			BTRON_CHECK("獲得 2", tk_get_mpl(id, 200, &b2, TMO_POL) == E_OK);
			BTRON_CHECK("重ならない", b1 != b2);
			*(UW *)b1 = 0xAAAAAAAAU;
			*(UW *)b2 = 0xBBBBBBBBU;
			BTRON_CHECK("独立している",
			            *(UW *)b1 == 0xAAAAAAAAU && *(UW *)b2 == 0xBBBBBBBBU);
			BTRON_CHECK("空きが減った", tk_ref_mpl(id, &r) == E_OK && r.frsz < free0);
			/* プールより大きい要求は永久に満たせないので待たせない */
			BTRON_CHECK("過大な要求", tk_get_mpl(id, 100000, &b3, TMO_FEVR) == E_PAR);
			BTRON_CHECK("返却 1", tk_rel_mpl(id, b1) == E_OK);
			BTRON_CHECK("二重解放", tk_rel_mpl(id, b1) == E_PAR);
			BTRON_CHECK("返却 2", tk_rel_mpl(id, b2) == E_OK);
			/* 融合して元の大きさに戻る */
			BTRON_CHECK("融合した", tk_ref_mpl(id, &r) == E_OK && r.frsz == free0);
			BTRON_CHECK("最大も戻った", r.maxsz == free0);
			BTRON_CHECK("削除", tk_del_mpl(id) == E_OK);
		}
	});

	BTRON_TEST("メッセージバッファ", {
		T_CMBF c;
		T_RMBF r;
		B out[32];
		B in[32];
		W i;

		for (i = 0; i < 8; i++) {
			c.dsname[i] = 0;
		}
		c.exinf = 0;
		c.mbfatr = TA_TFIFO;
		c.bufsz = 128;
		c.maxmsz = 32;
		c.bufptr = 0;
		mbf1 = tk_cre_mbf(&c);
		BTRON_CHECK("生成", mbf1 >= 1);
		BTRON_CHECK("空", tk_rcv_mbf(mbf1, in, TMO_POL) == E_TMOUT);

		/* **内容を複製する**のがメールボックスとの違い。
		 * 送信後に送信元を書き換えても、受信内容は変わらない。 */
		out[0] = 'A';
		out[1] = 'B';
		out[2] = 'C';
		BTRON_CHECK("送信", tk_snd_mbf(mbf1, out, 3, TMO_POL) == E_OK);
		out[0] = 'X'; /* 送信後に書き換える */
		BTRON_CHECK("先頭の長さが見える",
		            tk_ref_mbf(mbf1, &r) == E_OK && r.msgsz == 3);
		BTRON_CHECK("受信", tk_rcv_mbf(mbf1, in, TMO_POL) == 3);
		BTRON_CHECK("複製されている", in[0] == 'A' && in[1] == 'B' && in[2] == 'C');

		/* FIFO。可変長が混ざっても順序を保つ */
		out[0] = '1';
		BTRON_CHECK("送信 1", tk_snd_mbf(mbf1, out, 1, TMO_POL) == E_OK);
		out[0] = '2';
		out[1] = '2';
		BTRON_CHECK("送信 2", tk_snd_mbf(mbf1, out, 2, TMO_POL) == E_OK);
		BTRON_CHECK("受信 1", tk_rcv_mbf(mbf1, in, TMO_POL) == 1 && in[0] == '1');
		BTRON_CHECK("受信 2", tk_rcv_mbf(mbf1, in, TMO_POL) == 2 && in[0] == '2');

		BTRON_CHECK("長さ 0", tk_snd_mbf(mbf1, out, 0, TMO_POL) == E_PAR);
		BTRON_CHECK("最大超過", tk_snd_mbf(mbf1, out, 33, TMO_POL) == E_PAR);
		BTRON_CHECK("NULL", tk_snd_mbf(mbf1, 0, 1, TMO_POL) == E_PAR);
		BTRON_CHECK("削除", tk_del_mbf(mbf1) == E_OK);
		BTRON_CHECK("削除後", tk_rcv_mbf(mbf1, in, TMO_POL) == E_NOEXS);
	});

	BTRON_TEST("メッセージバッファが一杯なら送信側も待つ", {
		T_CMBF c;
		T_RMBF r;
		B out[32];
		B in[32];
		W i;
		W n;

		for (i = 0; i < 8; i++) {
			c.dsname[i] = 0;
		}
		c.exinf = 0;
		c.mbfatr = TA_TFIFO;
		c.bufsz = 32;
		c.maxmsz = 16;
		c.bufptr = 0;
		mbf1 = tk_cre_mbf(&c);
		/* 枠は 4 + 切り上げ。16 バイトなら 20 バイト使うので 1 個しか入らない。 */
		out[0] = 'f';
		BTRON_CHECK("1 個目", tk_snd_mbf(mbf1, out, 16, TMO_POL) == E_OK);
		BTRON_CHECK("2 個目は一杯", tk_snd_mbf(mbf1, out, 16, TMO_POL) == E_TMOUT);
		BTRON_CHECK("タイムアウト", tk_snd_mbf(mbf1, out, 16, 10) == E_TMOUT);
		BTRON_CHECK("空きが見える", tk_ref_mbf(mbf1, &r) == E_OK && r.frbufsz < 32);
		n = tk_rcv_mbf(mbf1, in, TMO_POL);
		BTRON_CHECK("受信して空いた", n == 16 && in[0] == 'f');
		BTRON_CHECK("また入る", tk_snd_mbf(mbf1, out, 16, TMO_POL) == E_OK);
		(void)tk_rcv_mbf(mbf1, in, TMO_POL);
		(void)tk_del_mbf(mbf1);
	});

	BTRON_TEST("バッファサイズ 0 の同期通信 (仕様)", {
		T_CMBF c;
		ID snd;
		ID rcv;
		W i;

		for (i = 0; i < 8; i++) {
			c.dsname[i] = 0;
		}
		c.exinf = 0;
		c.mbfatr = TA_TFIFO;
		c.bufsz = 0; /* **これが合法**。仕様が同期メッセージとして規定している */
		c.maxmsz = 16;
		c.bufptr = 0;
		mbf1 = tk_cre_mbf(&c);
		BTRON_CHECK("生成", mbf1 >= 1);

		/* ① 送信側が先。受信側が来るまで待つ。 */
		tst_seq_reset();
		snd = tst_make_task(t_mbf_send, 6, 0);
		(void)tk_sta_tsk(snd, 's');
		(void)tk_dly_tsk(10);
		BTRON_CHECK("送信側が待っている", tst_seq_eq(""));
		{
			B in[32];

			BTRON_CHECK("受信で成立", tk_rcv_mbf(mbf1, in, TMO_FEVR) == 1);
			BTRON_CHECK("内容", in[0] == 's');
		}
		(void)tk_dly_tsk(10);
		BTRON_CHECK("送信側が進んだ", tst_seq_eq("s"));
		(void)tk_del_tsk(snd);

		/* ② 受信側が先。送信側が来るまで待つ。 */
		tst_seq_reset();
		rcv = tst_make_task(t_mbf_recv, 6, 0);
		(void)tk_sta_tsk(rcv, 'r');
		(void)tk_dly_tsk(10);
		BTRON_CHECK("受信側が待っている", tst_seq_eq(""));
		{
			B out[4];

			out[0] = 'z';
			BTRON_CHECK("送信で成立", tk_snd_mbf(mbf1, out, 1, TMO_FEVR) == E_OK);
		}
		(void)tk_dly_tsk(10);
		if (!tst_seq_eq("zr")) {
			tst_seq_show("zr");
		}
		BTRON_CHECK("受信側が受け取った", tst_seq_eq("zr"));
		(void)tk_del_tsk(rcv);
		(void)tk_del_mbf(mbf1);
	});
}

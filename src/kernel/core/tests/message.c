/*
 * message.c — 周辺核 メッセージ管理とグローバル名 (§1.2 / §1.4)
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

LOCAL CONST TC nm_other[] = {(TC)'O', (TC)'T', (TC)'H', (TC)'E', (TC)'R',
                             TNULL};

LOCAL CONST TC nm_empty[] = {TNULL};

LOCAL CONST TC nm_child[] = {(TC)'C', (TC)'H', (TC)'I', (TC)'L', (TC)'D',
                             TNULL};

LOCAL W msg_h_called;      /* 利用者定義ハンドラが呼ばれた回数 */

LOCAL W msg_h_from;        /* ハンドラが受け取った送信元 PID */

LOCAL W msg_h_type;        /* ハンドラが受け取ったメッセージタイプ */

LOCAL W msg_h_code;        /* ハンドラが受け取った本体の 1 語目 */

LOCAL ERR msg_slp_result;  /* ハンドラで中断された待ちの戻り値 */

LOCAL W msg_child_ready;   /* 子が待ちに入ったことの印 */

/* 利用者定義のメッセージハンドラ (§1.2.3)。
 * **メインタスクの処理に割り込む形で走る** — 子プロセスが slp_tsk で
 * 待っている最中に、そのタスクのスタックの上で実行される。 */
LOCAL void h_msg(W pid, MESSAGE *r_msg)
{
	msg_h_called++;
	msg_h_from = pid;
	if (r_msg != 0) {
		msg_h_type = r_msg->msg_type;
		msg_h_code = r_msg->msg_body.ANYMSG.msg_str[0];
	}
	(void)ret_msg(0); /* 仕様: ハンドラの最後には必ずこれを呼ぶ */
}

/* ハンドラを定義して待つ子プロセス。
 * ハンドラが走ると slp_tsk が ER_MINTR で中断される（仕様）。 */
LOCAL W p_msghdr(MESSAGE *msg)
{
	VP raw = (VP)h_msg;

	(void)msg;
	(void)def_msg(MM_TYPE0, (FUNCP)raw);
	msg_child_ready = 1;
	msg_slp_result = slp_tsk(T_FOREVER);
	msg_child_ready = 2;
	(void)slp_tsk(T_FOREVER); /* 2 度目。親が片付ける */
	return 0;
}

/* MH_TERM を定義して待つ子プロセス。メッセージが来ると異常終了する。 */
LOCAL W p_mhterm(MESSAGE *msg)
{
	(void)msg;
	(void)def_msg(MM_TYPE1, MH_TERM);
	msg_child_ready = 1;
	(void)slp_tsk(T_FOREVER);
	return 0;
}

/* 自分の終了コードを返して終わる子プロセス。MS_EXIT の検査用。 */
LOCAL W p_exit42(MESSAGE *msg)
{
	(void)msg;
	return 42;
}

/* グローバル名を DELEXIT 付きで登録して終わる子プロセス。 */
LOCAL W p_gname(MESSAGE *msg)
{
	(void)msg;
	(void)cre_nam(nm_child, 0x1234, N_CREATE | DELEXIT);
	return 0;
}

BTRON_TEST_GROUP("周辺核 メッセージ管理とグローバル名 (§1.2 / §1.4)", 220, TST_PHASE_PROC, tst_message)
{
	W me = prc_self();

	BTRON_TEST("メッセージの送受信は FIFO (§1.2.1)", {
		MESSAGE s;
		MESSAGE r;
		W i;
		Bool order = True;

		/* **ここまでの検査が残した終了メッセージを捨てる。**
		 *
		 * §1.1 と §1.5 の検査は子プロセスを 130 個以上作って終了させて
		 * いる。その MS_EXIT / MS_TERM / MS_ABORT は全部この親の
		 * キューに入るので、放っておくと容量いっぱいまで溜まり、
		 * 通常メッセージが ER_NOSPC で入らなくなる。
		 *
		 * これは実装の都合ではなく**プロセスを作る側の責任**である。
		 * 仕様が「子プロセスが終了した場合の終了メッセージもこの
		 * メッセージキューに入る」と規定しているので、子を作る親は
		 * 終了メッセージを受け取るか捨てるかしなければならない。 */
		BTRON_CHECK("前の検査の残りを捨てる",
		            clr_msg(MM_ALL, MM_NULL) == ER_OK);
		BTRON_CHECK("空になった", msg_count(me) == 0);

		s.msg_size = (W)sizeof(W);
		for (i = 0; i < 3; i++) {
			s.msg_type = MS_TYPE0;
			s.msg_body.ANYMSG.msg_str[0] = (UB)('a' + i);
			BTRON_CHECK("送信", snd_msg(P_SELF, &s, NOWAIT) == ER_OK);
		}
		BTRON_CHECK("3 通たまった", msg_count(me) == 3);
		for (i = 0; i < 3; i++) {
			/* 送信元が自プロセスなので戻り値は 0（仕様） */
			if (rcv_msg(MM_TYPE0, &r, (W)sizeof(r), WAIT) != 0 ||
			    r.msg_body.ANYMSG.msg_str[0] != (UB)('a' + i)) {
				order = False;
			}
		}
		BTRON_CHECK("送信順に出てくる", order);
		BTRON_CHECK("空になった", msg_count(me) == 0);
		BTRON_CHECK("無いときは ER_NONE",
		            rcv_msg(MM_ALL, &r, (W)sizeof(r), NOWAIT) == ER_NONE);
	});

	BTRON_TEST("メッセージのエラーコード (§1.2.5)", {
		MESSAGE s;
		MESSAGE r;

		s.msg_type = MS_TYPE0;
		s.msg_size = (W)sizeof(W);

		BTRON_CHECK("アドレス不正", snd_msg(P_SELF, 0, NOWAIT) == ER_ADR);
		BTRON_CHECK("属性不正", snd_msg(P_SELF, &s, 7) == ER_PAR);
		BTRON_CHECK("親が無い", snd_msg(P_PARENT, &s, NOWAIT) == ER_NOPRC);
		BTRON_CHECK("存在しない",
		            snd_msg(PRC_MAX_PRCID, &s, NOWAIT) == ER_NOPRC);
		/* 仕様「自プロセスを指定した (CONFM 指定時)」 */
		BTRON_CHECK("自プロセスへの CONFM",
		            snd_msg(P_SELF, &s, CONFM) == ER_SELF);

		s.msg_type = 0;
		BTRON_CHECK("タイプ 0", snd_msg(P_SELF, &s, NOWAIT) == ER_PAR);
		s.msg_type = MS_MAX + 1;
		BTRON_CHECK("タイプ 32", snd_msg(P_SELF, &s, NOWAIT) == ER_PAR);
		s.msg_type = MS_TYPE0;
		s.msg_size = 0;
		BTRON_CHECK("本体 0 バイト", snd_msg(P_SELF, &s, NOWAIT) == ER_SZOVR);
		s.msg_size = MSG_MAX_BODY + 1;
		BTRON_CHECK("本体が大きすぎる",
		            snd_msg(P_SELF, &s, NOWAIT) == ER_SZOVR);

		BTRON_CHECK("格納先が不正",
		            rcv_msg(MM_ALL, 0, (W)sizeof(r), NOWAIT) == ER_ADR);
		/* 仕様「msgsz ≧ 8 でなくてはいけない」 */
		BTRON_CHECK("領域が小さすぎる",
		            rcv_msg(MM_ALL, &r, 7, NOWAIT) == ER_PAR);
		BTRON_CHECK("マスクが 0",
		            rcv_msg(0, &r, (W)sizeof(r), NOWAIT) == ER_PAR);
		BTRON_CHECK("クリアのマスクが 0", clr_msg(0, MM_NULL) == ER_PAR);
	});

	BTRON_TEST("入り切らない受信はキューに残る (§1.2.5)", {
		MESSAGE s;
		UB buf[MSG_HEADER_SIZE + 8];
		MESSAGE *r = (MESSAGE *)(VP)buf;

		s.msg_type = MS_TYPE0;
		s.msg_size = 32;
		s.msg_body.ANYMSG.msg_str[0] = 0xAB;
		BTRON_CHECK("送信", snd_msg(P_SELF, &s, NOWAIT) == ER_OK);

		/* 8 バイトしか入らない領域で 32 バイトの本体を受けようとする。
		 * 仕様「msgsz の範囲内のみ格納されてエラー終了する。この時は、
		 * CLR 指定があってもメッセージはバッファに残る」 */
		BTRON_CHECK("入り切らない",
		            rcv_msg(MM_TYPE0, r, (W)sizeof(buf), WAIT) == ER_PAR);
		BTRON_CHECK("必要な大きさが分かる", r->msg_size == 32);
		BTRON_CHECK("キューに残っている", msg_count(me) == 1);

		/* 十分な領域で取り直せる。 */
		{
			MESSAGE full;

			BTRON_CHECK("取り直せる",
			            rcv_msg(MM_TYPE0, &full, (W)sizeof(full), WAIT) == 0);
			BTRON_CHECK("中身が合っている",
			            full.msg_body.ANYMSG.msg_str[0] == 0xAB);
		}
		BTRON_CHECK("空になった", msg_count(me) == 0);
	});

	BTRON_TEST("NOCLR と CHECK (§1.2.5)", {
		MESSAGE s;
		MESSAGE r;

		s.msg_type = MS_TYPE1;
		s.msg_size = (W)sizeof(W);
		s.msg_body.ANYMSG.msg_str[0] = 0x5A;
		BTRON_CHECK("送信", snd_msg(P_SELF, &s, NOWAIT) == ER_OK);

		BTRON_CHECK("NOCLR で受信",
		            rcv_msg(MM_TYPE1, &r, (W)sizeof(r), WAIT | NOCLR) == 0);
		BTRON_CHECK("残っている", msg_count(me) == 1);
		BTRON_CHECK("CLR で受信",
		            rcv_msg(MM_TYPE1, &r, (W)sizeof(r), WAIT) == 0);
		BTRON_CHECK("消えた", msg_count(me) == 0);

		/* CHECK: 指定タイプが無く別のタイプがある場合は**ヘッダだけ**。 */
		BTRON_CHECK("送信", snd_msg(P_SELF, &s, NOWAIT) == ER_OK);
		r.msg_body.ANYMSG.msg_str[0] = 0;
		BTRON_CHECK("別タイプを指定して CHECK",
		            rcv_msg(MM_TYPE2, &r, (W)sizeof(r), NOWAIT | CHECK) == 0);
		BTRON_CHECK("タイプは分かる", r.msg_type == MS_TYPE1);
		BTRON_CHECK("本体は入っていない",
		            r.msg_body.ANYMSG.msg_str[0] == 0);
		BTRON_CHECK("キューに残る", msg_count(me) == 1);
		BTRON_CHECK("片付け", clr_msg(MM_ALL, MM_NULL) == ER_OK);
		BTRON_CHECK("空になった", msg_count(me) == 0);
	});

	BTRON_TEST("clr_msg の last_mask (§1.2.5)", {
		MESSAGE s;
		W i;

		s.msg_size = (W)sizeof(W);
		/* TYPE0, TYPE1, TYPE0 の順に積む */
		for (i = 0; i < 3; i++) {
			s.msg_type = (i == 1) ? MS_TYPE1 : MS_TYPE0;
			BTRON_CHECK("送信", snd_msg(P_SELF, &s, NOWAIT) == ER_OK);
		}
		/* 仕様: last_mask で指定したタイプの**直前まで**クリアする。
		 * TYPE1 の手前の TYPE0 が 1 通だけ消える。 */
		BTRON_CHECK("直前まで", clr_msg(MM_TYPE0, MM_TYPE1) == ER_OK);
		BTRON_CHECK("1 通消えた", msg_count(me) == 2);

		/* 仕様の特例: last_mask = MM_ALL は「1 つだけクリア」。
		 * ここを特例にしないと、MM_ALL が全タイプを含むので先頭で
		 * 止まって何も消えない。 */
		BTRON_CHECK("1 つだけ", clr_msg(MM_ALL, MM_ALL) == ER_OK);
		BTRON_CHECK("もう 1 通消えた", msg_count(me) == 1);
		BTRON_CHECK("全部", clr_msg(MM_ALL, MM_NULL) == ER_OK);
		BTRON_CHECK("空になった", msg_count(me) == 0);
	});

	BTRON_TEST("システムメッセージは捨てられない (§1.2.2)", {
		MESSAGE s;
		W i;
		Bool filled = True;

		s.msg_type = MS_TYPE0;
		s.msg_size = (W)sizeof(W);
		for (i = 0; i < MSG_QUEUE_MAX; i++) {
			if (snd_msg(P_SELF, &s, NOWAIT) != ER_OK) {
				filled = False;
			}
		}
		BTRON_CHECK("通常の容量まで入る", filled);
		/* 仕様「送信先のメッセージキューに空きがない時はエラー終了する」 */
		BTRON_CHECK("満杯で NOWAIT は ER_NOSPC",
		            snd_msg(P_SELF, &s, NOWAIT) == ER_NOSPC);

		/* 仕様「これらは、基本的にメッセージキューのオーバーフローの
		 * 影響を受けず、捨てられることはないようになっている」 */
		s.msg_type = MS_SYSEVT;
		BTRON_CHECK("システムメッセージは入る",
		            snd_msg(P_SELF, &s, NOWAIT) == ER_OK);
		BTRON_CHECK("容量を超えている", msg_count(me) == MSG_QUEUE_MAX + 1);
		BTRON_CHECK("片付け", clr_msg(MM_ALL, MM_NULL) == ER_OK);
		BTRON_CHECK("空になった", msg_count(me) == 0);
	});

	BTRON_TEST("子プロセスの終了メッセージ (§1.2.2 / §1.1.9)", {
		MESSAGE r;
		W child;

		BTRON_CHECK("空から始める", msg_count(me) == 0);

		/* ext_prc（MAIN からの return）→ MS_EXIT */
		child = prc_create(p_exit42, 1, 0, 0);
		BTRON_CHECK("子が走って終わった", child >= 1 && prc_count() == 1);
		BTRON_CHECK("終了メッセージが来た",
		            rcv_msg(MM_EXIT, &r, (W)sizeof(r), NOWAIT) == child);
		BTRON_CHECK("MS_EXIT", r.msg_type == MS_EXIT);
		BTRON_CHECK("本体は 8 バイト", r.msg_size == 8);
		BTRON_CHECK("終了した子の PID", r.msg_body.EXIT.pid == child);
		/* **ここが §1.1 と §1.2 の継ぎ目。** MAIN() の戻り値が
		 * 終了コードとして親に届く。 */
		BTRON_CHECK("終了コード 42", r.msg_body.EXIT.code == 42);

		/* ter_prc → MS_TERM */
		child = prc_create(tst_p_sleeper, 1, 0, 0);
		BTRON_CHECK("強制終了", ter_prc(child, 7, TERM_NRM) == ER_OK);
		BTRON_CHECK("強制終了メッセージ",
		            rcv_msg(MM_TERM, &r, (W)sizeof(r), NOWAIT) == child);
		BTRON_CHECK("MS_TERM と コード",
		            r.msg_type == MS_TERM && r.msg_body.TERM.code == 7);

		/* prc_abort → MS_ABORT */
		child = prc_create(tst_p_sleeper, 1, 0, 0);
		BTRON_CHECK("異常終了", prc_abort(child, 9) == ER_OK);
		BTRON_CHECK("異常終了メッセージ",
		            rcv_msg(MM_ABORT, &r, (W)sizeof(r), NOWAIT) == child);
		BTRON_CHECK("MS_ABORT と コード",
		            r.msg_type == MS_ABORT && r.msg_body.ABORT.code == 9);
		BTRON_CHECK("空になった", msg_count(me) == 0);
	});

	BTRON_TEST("タイムアウトメッセージ (§1.2.5 req_tmg)", {
		MESSAGE r;

		BTRON_CHECK("負の時間", req_tmg(0, 0) == ER_PAR);
		BTRON_CHECK("要求", req_tmg(10, 0x55) == ER_OK);
		BTRON_CHECK("まだ来ていない",
		            rcv_msg(MM_TMOUT, &r, (W)sizeof(r), NOWAIT) == ER_NONE);
		/* 自プロセス宛なので戻り値は 0（仕様） */
		BTRON_CHECK("待てば来る",
		            rcv_msg(MM_TMOUT, &r, (W)sizeof(r), WAIT) == 0);
		BTRON_CHECK("MS_TMOUT とコード",
		            r.msg_type == MS_TMOUT && r.msg_body.TMOUT.code == 0x55);

		/* 仕様「自プロセスのタイムアウトメッセージ要求をすべて取り消す」 */
		BTRON_CHECK("要求", req_tmg(10, 0x66) == ER_OK);
		BTRON_CHECK("取り消し", can_tmg() == ER_OK);
		(void)dly_tsk(30);
		BTRON_CHECK("来ない", msg_count(me) == 0);
	});

	BTRON_TEST("システム定義のメッセージハンドラ (§1.2.3)", {
		MESSAGE s;
		MESSAGE r;

		s.msg_type = MS_TYPE2;
		s.msg_size = (W)sizeof(W);

		/* MH_NONE: 仕様「何もせずにメッセージは無視される」 */
		BTRON_CHECK("定義", def_msg(MM_TYPE2, MH_NONE) == ER_OK);
		BTRON_CHECK("送信は成功する", snd_msg(P_SELF, &s, NOWAIT) == ER_OK);
		BTRON_CHECK("キューには入らない", msg_count(me) == 0);

		/* MH_BREAK も同じく捨てるが、待ちを中断する。
		 * 中断の方は子プロセスで確かめる（下の検査）。 */
		BTRON_CHECK("定義", def_msg(MM_TYPE2, MH_BREAK) == ER_OK);
		BTRON_CHECK("送信は成功する", snd_msg(P_SELF, &s, NOWAIT) == ER_OK);
		BTRON_CHECK("キューには入らない", msg_count(me) == 0);

		BTRON_CHECK("定義解除", def_msg(MM_TYPE2, 0) == ER_OK);
		BTRON_CHECK("入るようになった",
		            snd_msg(P_SELF, &s, NOWAIT) == ER_OK &&
		                msg_count(me) == 1);
		BTRON_CHECK("受信", rcv_msg(MM_TYPE2, &r, (W)sizeof(r), WAIT) == 0);
		BTRON_CHECK("マスクが 0", def_msg(0, MH_NONE) == ER_PAR);
	});

	BTRON_TEST("メッセージハンドラが待ちに割り込む (§1.2.3)", {
		MESSAGE s;
		W child;

		msg_h_called = 0;
		msg_h_from = -1;
		msg_h_type = 0;
		msg_h_code = 0;
		msg_slp_result = ER_OK;
		msg_child_ready = 0;

		/* 子は優先度 1（自分より高い）なので、生成した瞬間に走って
		 * ハンドラを定義し、slp_tsk で待ちに入る。 */
		child = prc_create(p_msghdr, 1, 0, 0);
		BTRON_CHECK("子が待ちに入った", child >= 1 && msg_child_ready == 1);

		s.msg_type = MS_TYPE0;
		s.msg_size = (W)sizeof(W);
		s.msg_body.ANYMSG.msg_str[0] = 0x3C;
		BTRON_CHECK("送信", snd_msg(child, &s, NOWAIT) == ER_OK);

		/* **ここが §1.2.3 の核心。** 子のメインタスクのスタックに
		 * ハンドラ呼び出しが差し込まれ、待ちは ER_MINTR で中断され、
		 * ハンドラが走ってから割り込んだ位置へ帰る。 */
		BTRON_CHECK("ハンドラが呼ばれた", msg_h_called == 1);
		BTRON_CHECK("送信元が渡った", msg_h_from == me);
		BTRON_CHECK("メッセージが渡った",
		            msg_h_type == MS_TYPE0 && msg_h_code == 0x3C);
		/* 仕様「中断されたシステムコールは ER_MINTR のエラーとして戻る」 */
		BTRON_CHECK("待ちは中断された", msg_slp_result == ER_MINTR);
		BTRON_CHECK("割り込んだ位置から再開した", msg_child_ready == 2);
		BTRON_CHECK("キューには残らない", msg_count(child) == 0);

		BTRON_CHECK("片付け", ter_prc(child, 0, TERM_NRM) == ER_OK);
		BTRON_CHECK("終了メッセージを捨てる",
		            clr_msg(MM_ALL, MM_NULL) == ER_OK);
	});

	BTRON_TEST("MH_TERM はプロセスを異常終了させる (§1.2.3)", {
		MESSAGE s;
		MESSAGE r;
		W child;

		msg_child_ready = 0;
		child = prc_create(p_mhterm, 1, 0, 0);
		BTRON_CHECK("子が待ちに入った", child >= 1 && msg_child_ready == 1);

		s.msg_type = MS_TYPE1;
		s.msg_size = (W)sizeof(W);
		BTRON_CHECK("送信", snd_msg(child, &s, NOWAIT) == ER_OK);
		/* 仕様「自プロセスを異常終了し、MS_ABORT メッセージ
		 * （エラーコードは 0）を親プロセスに送信する」 */
		BTRON_CHECK("子は消えた", prc_sts(child, 0, 0) == ER_NOPRC);
		BTRON_CHECK("MS_ABORT が来た",
		            rcv_msg(MM_ABORT, &r, (W)sizeof(r), NOWAIT) == child);
		BTRON_CHECK("エラーコードは 0", r.msg_body.ABORT.code == 0);
		BTRON_CHECK("空になった", msg_count(me) == 0);
	});

	BTRON_TEST("brk_msg と WAIEVT (§1.2.5)", {
		MESSAGE r;

		/* 仕様「brk_msg() を呼び出したとき WAIEVT 指定で待ちに入っている
		 * タスクが無ければ、待ち解除の要求を記録する」 */
		BTRON_CHECK("先に通知", brk_msg() == ER_OK);
		/* 記録された要求で即座に解ける。仕様「この場合、エラー
		 * (ER_NONE) 終了する」 */
		BTRON_CHECK("記録で解ける",
		            rcv_msg(0, &r, (W)sizeof(r), WAIT | WAIEVT) == ER_NONE);
		/* 要求回数は記録されない（仕様）。2 度目は待ちに入るので
		 * ここでは NOWAIT で確かめる。 */
		BTRON_CHECK("2 度は解けない",
		            rcv_msg(0, &r, (W)sizeof(r), NOWAIT | WAIEVT) == ER_NONE);
		/* WAIEVT のときだけ t_mask = 0 が許される（仕様） */
		BTRON_CHECK("WAIEVT 以外で 0 は不正",
		            rcv_msg(0, &r, (W)sizeof(r), NOWAIT) == ER_PAR);
	});

	/* -------------------------------------------------------------- */
	/* §1.4 グローバル名管理                                          */
	/* -------------------------------------------------------------- */

	BTRON_TEST("グローバル名データ (§1.4)", {
		CONST TC *n1 = tst_nm_test;
		CONST TC *n2 = nm_other;
		W n0 = gnm_count();
		W data = 0;

		BTRON_CHECK("生成", cre_nam(n1, 0xABCD, N_CREATE) == ER_OK);
		BTRON_CHECK("数が増えた", gnm_count() == n0 + 1);
		BTRON_CHECK("取得", get_nam(n1, &data) == ER_OK && data == 0xABCD);
		/* 仕様「すでに存在している場合はエラー」 */
		BTRON_CHECK("二重生成", cre_nam(n1, 1, N_CREATE) == ER_EXS);
		BTRON_CHECK("変更", cre_nam(n1, 0x1111, N_MODIFY) == ER_OK);
		BTRON_CHECK("変わった",
		            get_nam(n1, &data) == ER_OK && data == 0x1111);
		/* 仕様「存在していない場合はエラー」 */
		BTRON_CHECK("無いものの変更", cre_nam(n2, 1, N_MODIFY) == ER_NOEXS);
		/* 仕様「無ければ新規に生成し、すでに存在している場合は変更」 */
		BTRON_CHECK("強制生成", cre_nam(n2, 0x2222, N_FORCE) == ER_OK);
		BTRON_CHECK("強制変更", cre_nam(n2, 0x3333, N_FORCE) == ER_OK);
		BTRON_CHECK("変わった",
		            get_nam(n2, &data) == ER_OK && data == 0x3333);

		BTRON_CHECK("アドレス不正", cre_nam(0, 0, N_CREATE) == ER_ADR);
		BTRON_CHECK("空の名前", cre_nam(nm_empty, 0, N_CREATE) == ER_PAR);
		BTRON_CHECK("属性不正", cre_nam(n1, 0, 3) == ER_PAR);
		BTRON_CHECK("削除", del_nam(n1) == ER_OK && del_nam(n2) == ER_OK);
		BTRON_CHECK("無い", get_nam(n1, &data) == ER_NOEXS);
		BTRON_CHECK("無いものの削除", del_nam(n1) == ER_NOEXS);
		BTRON_CHECK("数が戻った", gnm_count() == n0);
	});

	BTRON_TEST("DELEXIT のグローバル名はプロセス終了で消える (§1.4.1)", {
		CONST TC *n = nm_child;
		W n0 = gnm_count();
		W data = 0;
		W child;

		child = prc_create(p_gname, 1, 0, 0);
		BTRON_CHECK("子が走って終わった", child >= 1 && prc_count() == 1);
		/* 子は DELEXIT 付きで登録して終了した。 */
		BTRON_CHECK("消えている", get_nam(n, &data) == ER_NOEXS);
		BTRON_CHECK("数が戻った", gnm_count() == n0);
		BTRON_CHECK("終了メッセージを捨てる",
		            clr_msg(MM_ALL, MM_NULL) == ER_OK);
	});

	/* -------------------------------------------------------------- */
	/* §1.3 の残り（セマフォ・イベントフラグ・メッセージバッファ）    */
	/* -------------------------------------------------------------- */

}

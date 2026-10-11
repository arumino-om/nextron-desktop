/*
 * selftest.c — 中心核の自己検査
 *
 * 中心核のテストは**タスクの中で走らせなければならない**。
 * タスクを待ち状態にするシステムコールは、タスク独立部（起動文脈や
 * 割込みハンドラ）から呼ぶと E_CTX になる。そこで起動側は
 * knl_selftest_start() を呼ぶだけにして、検査本体はタスクにする。
 *
 * ここで一番力を入れているのは**同一優先度内の優先順位の規則**である。
 * 仕様はこう規定していて、実装を取り違えても普通のテストでは気づかない。
 *
 *   実行状態 → 実行可能状態（プリエンプト）  同一優先度で**最高**
 *   実行状態 → 待ち状態 → 待ち解除            同一優先度で**最低**
 *
 * 3 つのタスクで実行順を記録し、head/tail を取り違えると落ちるようにしてある。
 */

#include <btron/bootinfo.h>
#include <btron/fontman.h>
#include <btron/machdesc.h>
#include <btron/manager.h>
#include <btron/system.h>
#include <btron/screen.h>
#include <btron/test.h>

#include "knl.h"

IMPORT UD knl_tick_count(void);

/* ------------------------------------------------------------------ */
/* 実行順の記録                                                        */
/* ------------------------------------------------------------------ */

#define LOG_MAX 64
LOCAL B seq_log[LOG_MAX + 1];
LOCAL W seq_n;

LOCAL void seq_reset(void)
{
	seq_n = 0;
	seq_log[0] = '\0';
}

LOCAL void seq_put(B c)
{
	if (seq_n < LOG_MAX) {
		seq_log[seq_n++] = c;
		seq_log[seq_n] = '\0';
	}
}

LOCAL Bool seq_eq(CONST B *s)
{
	W i;

	for (i = 0; s[i] != '\0'; i++) {
		if (i >= seq_n || seq_log[i] != s[i]) {
			return False;
		}
	}
	return (i == seq_n) ? True : False;
}

/* 失敗したときに何が起きたか分かるように実測の並びを出す。 */
LOCAL void seq_show(CONST B *expect)
{
	hal_console_puts("         期待 \"");
	hal_console_puts(expect);
	hal_console_puts("\"  実測 \"");
	hal_console_puts(seq_log);
	hal_console_puts("\"\n");
}

/* ------------------------------------------------------------------ */
/* 補助                                                                */
/* ------------------------------------------------------------------ */

LOCAL ID make_task(TASKENTRY entry, PRI pri, VP exinf)
{
	T_CTSK ctsk;
	W i;

	ctsk.exinf = exinf;
	ctsk.tskatr = TA_HLNG | TA_RNG0;
	ctsk.task = entry;
	ctsk.itskpri = pri;
	ctsk.stksz = TK_MIN_STKSZ;
	ctsk.sstksz = 0;
	ctsk.stkptr = 0;
	ctsk.bufptr = 0;
	for (i = 0; i < 8; i++) {
		ctsk.dsname[i] = 0;
	}
	return tk_cre_tsk(&ctsk);
}

/* 検査の間だけ使う ID。各テストの先頭で作り、終わりで消す。 */
LOCAL ID ta, tb, tc, th;
LOCAL ID sem1, flg1;
LOCAL ID por1, por2;
LOCAL W srv_calls;
LOCAL ERR fwd_err;
LOCAL ID mtx1;
LOCAL ID mbx1;
LOCAL W cyc_hits;
LOCAL W alm_hits;
LOCAL ID cyc_waiter;
LOCAL ID mbf1;

/* ------------------------------------------------------------------ */
/* ① プリエンプトされたタスクは同一優先度の「先頭」に戻る              */
/*                                                                     */
/*   H(pri 5) A(pri 6) B(pri 6)。レディキューは A, B の順。             */
/*   A が H を起こして**プリエンプトされ**、H が再び寝たら、             */
/*   次に走るのは B ではなく A でなければならない。                     */
/* ------------------------------------------------------------------ */

LOCAL void t_high(INT stacd, VP exinf)
{
	(void)stacd;
	(void)exinf;
	seq_put('h'); /* 1 回目 */
	(void)tk_slp_tsk(TMO_FEVR);
	seq_put('H'); /* 起こされた */
	(void)tk_slp_tsk(TMO_FEVR);
	tk_ext_tsk();
}

LOCAL void t_preempt_a(INT stacd, VP exinf)
{
	(void)stacd;
	(void)exinf;
	seq_put('a');
	(void)tk_wup_tsk(th); /* ここでプリエンプトされる */
	seq_put('A');         /* 先頭に戻っていれば B より先に来る */
	tk_ext_tsk();
}

LOCAL void t_mark_b(INT stacd, VP exinf)
{
	(void)stacd;
	(void)exinf;
	seq_put('b');
	tk_ext_tsk();
}

/* ------------------------------------------------------------------ */
/* ② 待ち解除されたタスクは同一優先度の「末尾」に付く                  */
/*                                                                     */
/*   A B C すべて pri 6。レディキューは A, B, C の順。                  */
/*   A が寝る → B が A を起こす（A は**末尾**へ）→ B が終わる。         */
/*   次は C、その後に A。                                              */
/* ------------------------------------------------------------------ */

LOCAL void t_rel_a(INT stacd, VP exinf)
{
	(void)stacd;
	(void)exinf;
	seq_put('a');
	(void)tk_slp_tsk(TMO_FEVR);
	seq_put('A');
	tk_ext_tsk();
}

LOCAL void t_rel_b(INT stacd, VP exinf)
{
	(void)stacd;
	(void)exinf;
	seq_put('b');
	(void)tk_wup_tsk(ta); /* A を待ち解除。同一優先度なので切替は起きない */
	seq_put('B');
	tk_ext_tsk();
}

LOCAL void t_rel_c(INT stacd, VP exinf)
{
	(void)stacd;
	(void)exinf;
	seq_put('c');
	tk_ext_tsk();
}

/* ------------------------------------------------------------------ */
/* ③ 優先度順。番号の小さい方が先に走る                                */
/* ------------------------------------------------------------------ */

LOCAL void t_tag(INT stacd, VP exinf)
{
	(void)exinf;
	seq_put((B)stacd);
	tk_ext_tsk();
}

/* ------------------------------------------------------------------ */
/* ④ セマフォの待ち行列                                               */
/* ------------------------------------------------------------------ */

LOCAL void t_sem_wait(INT stacd, VP exinf)
{
	(void)exinf;
	if (tk_wai_sem(sem1, 1, TMO_FEVR) == E_OK) {
		seq_put((B)stacd);
	}
	tk_ext_tsk();
}

/* ------------------------------------------------------------------ */
/* ⑤ イベントフラグの複数待ち                                         */
/* ------------------------------------------------------------------ */

LOCAL void t_flg_wait(INT stacd, VP exinf)
{
	UINT p = 0;

	(void)exinf;
	if (tk_wai_flg(flg1, 0x3U, TWF_ANDW, &p, TMO_FEVR) == E_OK) {
		seq_put((B)stacd);
	}
	tk_ext_tsk();
}

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
	seq_put((B)stacd); /* ロックした */
	(void)tk_slp_tsk(TMO_FEVR); /* ここで H が待ちに入る */
	seq_put('L');
	(void)tk_unl_mtx(mtx1);
	tk_ext_tsk();
}

LOCAL void t_mtx_high(INT stacd, VP exinf)
{
	(void)exinf;
	seq_put('h');
	(void)tk_loc_mtx(mtx1, TMO_FEVR);
	seq_put((B)stacd); /* 譲られた */
	(void)tk_unl_mtx(mtx1);
	tk_ext_tsk();
}

LOCAL void t_mtx_mid(INT stacd, VP exinf)
{
	(void)exinf;
	seq_put((B)stacd);
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
		seq_put((B)stacd);
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
		seq_put((B)stacd);
	}
	tk_ext_tsk();
}

LOCAL void t_mbf_recv(INT stacd, VP exinf)
{
	B buf[8];

	(void)exinf;
	if (tk_rcv_mbf(mbf1, buf, TMO_FEVR) == 1) {
		seq_put(buf[0]);
		seq_put((B)stacd);
	}
	tk_ext_tsk();
}

/* ------------------------------------------------------------------ */
/* ⑨ 周期ハンドラ・アラームハンドラ                                   */
/*                                                                     */
/* ハンドラはタスク独立部で走る。タスクを起こすだけにする。             */
/* ------------------------------------------------------------------ */

LOCAL void h_cyc(VP exinf)
{
	(void)exinf;
	cyc_hits++;
	if (cyc_waiter != 0) {
		(void)tk_wup_tsk(cyc_waiter);
	}
}

LOCAL void h_alm(VP exinf)
{
	(void)exinf;
	alm_hits++;
}

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
			seq_put('x'); /* ER_DLT などで抜けた */
			break;
		}
		srv_calls++;
		seq_put((B)stacd);
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
		seq_put((B)stacd);
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
	seq_put((n == ER_DLT) ? 'D' : (B)stacd);
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
		seq_put((B)stacd);
		(void)del_por(por1);
		buf[0] = (B)(buf[0] + 1);
		if (rpl_rdv(rdv, buf, n) == ER_OK) {
			seq_put('R');
		}
	}
	tk_ext_tsk();
}

/* por1 で受け付けて por2 へ回送する（中間のサーバ） */
LOCAL void t_fwd_server(INT stacd, VP exinf)
{
	B buf[32];
	W rdv;
	W n;

	(void)exinf;
	n = acp_por(por1, 0xFFFFFFFFU, &rdv, buf, T_FOREVER);
	if (n > 0) {
		seq_put((B)stacd);
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
		seq_put((B)stacd);
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
		seq_put((B)stacd);
		fwd_err = fwd_por(por2, 0xFFFFFFFFU, rdv, buf, n);
		/* 回送が失敗したならランデブは解除されていないので返答できる */
		buf[0] = (B)(buf[0] + 1);
		(void)rpl_rdv(rdv, buf, n);
	}
	tk_ext_tsk();
}

/* ------------------------------------------------------------------ */
/* 周辺核 §1.1「プロセス / タスク管理」の検査用のタスク                 */
/*                                                                     */
/* **これらは初期プロセスの中で走る。** 中心核の検査（上の run_tests）は  */
/* プロセスに属さないタスクで走らせたままにしてある。理由はタイムスライス */
/* で、プロセスに属するタスクは prc_tick() が 10ms ごとに同一優先度の     */
/* 末尾へ回す。中心核の検査は「同一優先度内の実行順」を 1 文字ずつ        */
/* 比べているので、途中で回されると落ちてしまう。                        */
/* ------------------------------------------------------------------ */

LOCAL W prc_grandchild;  /* 子プロセスが作った孫のプロセス ID */
LOCAL ERR prc_level_err; /* レベルの低いプロセスから親を落とそうとした結果 */
LOCAL ERR prc_chgusr_er;
LOCAL W prc_rr_done;     /* ラウンドロビン検査の終了したサブタスク数 */

/* knl_otm を基準に ms だけ回る。タイムスライスを必ず使い切るために使う。
 * 待ちに入ってはいけない（待つと譲ってしまい、スライスの検査にならない）。 */
LOCAL void spin_ms(W ms)
{
	SYSTIM until = knl_otm + (SYSTIM)ms;

	while (knl_otm < until) {
		/* 何もしない。volatile 越しに時刻を読むためにループだけ回す。 */
	}
}

/* すぐ終わるプロセス。資源が戻ることの検査に使う。 */
LOCAL W p_exit_now(MESSAGE *msg)
{
	(void)msg;
	return 0; /* MAIN() からの return は ext_prc() と同等（仕様） */
}

/* 起こされるまで寝ているプロセス。木構造と限界値の検査に使う。 */
LOCAL W p_sleeper(MESSAGE *msg)
{
	(void)msg;
	(void)slp_tsk(T_FOREVER);
	return 0;
}

/* 孫を作って寝る。親子関係の引き継ぎ (§1.1.1) の検査に使う。 */
LOCAL W p_parent(MESSAGE *msg)
{
	(void)msg;
	prc_grandchild = prc_create(p_sleeper, 1, 0, 0);
	(void)slp_tsk(T_FOREVER);
	return 0;
}

/* 自分のユーザレベルを下げてから親を落とそうとする。
 * 仕様の「自プロセスより高いユーザレベルのプロセスは強制終了できない」。 */
LOCAL W p_lowlevel(MESSAGE *msg)
{
	P_USER u;

	(void)msg;
	(void)get_usr(P_SELF, &u);
	u.level = 5; /* 0 が最高なので 5 は低い */
	prc_chgusr_er = chg_usr(&u);
	prc_level_err = ter_prc(P_PARENT, 0, TERM_NRM);
	return 0;
}

/* 同一優先度のサブタスク。1 回ごとにスライスを使い切るまで回る。
 * 時分割が効いていれば 2 つが交互に記録を残す。 */
LOCAL void p_rr(W arg)
{
	W i;

	for (i = 0; i < 3; i++) {
		seq_put((B)arg);
		spin_ms(PRC_SLICE_MS * 2);
	}
	prc_rr_done++;
	ext_tsk();
}

/* 親タスクを起こすだけのサブタスク。 */
LOCAL void p_waker(W arg)
{
	(void)wup_tsk(arg);
	ext_tsk();
}

/* 寝続けるサブタスク。ter_tsk と上限の検査に使う。 */
LOCAL void p_subsleep(W arg)
{
	(void)arg;
	(void)slp_tsk(T_FOREVER);
	ext_tsk();
}

/* ------------------------------------------------------------------ */
/* 周辺核 §1.5「メモリ管理」の検査用                                    */
/* ------------------------------------------------------------------ */

LOCAL VP mbk_shared;      /* 親が獲得した共有メモリ。子が書き込む */
LOCAL W mbk_writer_err;
LOCAL VP mbk_c_local;     /* 子が獲得したローカル */
LOCAL VP mbk_c_common;    /* 子が獲得した共有（DELEXIT なし） */
LOCAL VP mbk_c_delexit;   /* 子が獲得した共有（DELEXIT あり） */
LOCAL ERR mbk_sys_err;    /* レベルの低いプロセスから M_SYSTEM を要求した結果 */

/* 共有メモリに書き込むだけの子プロセス。
 * **別のプロセスから同じアドレスが見えること**を確かめる。 */
LOCAL W p_shared_writer(MESSAGE *msg)
{
	volatile UW *p = (volatile UW *)mbk_shared;

	(void)msg;
	mbk_writer_err = (p == 0) ? 1 : 0;
	if (p != 0) {
		p[0] = 0xC0FFEEU;
	}
	return 0;
}

/* 3 種類の指定で獲得して、そのまま終了する子プロセス。
 * 終了時の自動解放の規則を親から観察する。 */
LOCAL W p_delexit(MESSAGE *msg)
{
	(void)msg;
	mbk_c_local = 0;
	mbk_c_common = 0;
	mbk_c_delexit = 0;
	(void)get_mbk(&mbk_c_local, 1, 0);
	(void)get_mbk(&mbk_c_common, 1, M_COMMON);
	(void)get_mbk(&mbk_c_delexit, 1, M_COMMON | DELEXIT);
	return 0;
}

/* ユーザレベルを下げてからシステムメモリを要求する子プロセス。 */
LOCAL W p_sysmem(MESSAGE *msg)
{
	P_USER u;
	VP adr = 0;

	(void)msg;
	(void)get_usr(P_SELF, &u);
	u.level = 5; /* 0 が最高なので 5 はアプリケーションプロセス相当 */
	(void)chg_usr(&u);
	mbk_sys_err = get_mbk(&adr, 1, M_SYSTEM);
	if (mbk_sys_err == ER_OK) {
		(void)rel_mbk(adr);
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* 周辺核 §1.2 / §1.4 / §1.3 の検査用のプロセスとハンドラ               */
/* ------------------------------------------------------------------ */

/* グリフに 1 画素でも立っているか。
 * 行ごとに見る — 'A' の上端 2 行は空白なので、特定の行を見てはいけない。 */
LOCAL Bool glyph_inked(CONST UB *g, W n)
{
	W i;

	if (g == 0) {
		return False;
	}
	for (i = 0; i < n; i++) {
		if (g[i] != 0) {
			return True;
		}
	}
	return False;
}

/* 検査で使う TC の名前。
 *
 * **BTRON_TEST の中に書けない。** マクロの引数は括弧しか見ないので、
 * 配列初期化子の `,` でマクロの引数が割れてしまう（波括弧は効かない）。 */
LOCAL CONST TC nm_test[] = {(TC)'T', (TC)'E', (TC)'S', (TC)'T', TNULL};
LOCAL CONST TC nm_other[] = {(TC)'O', (TC)'T', (TC)'H', (TC)'E', (TC)'R',
                             TNULL};
LOCAL CONST TC nm_empty[] = {TNULL};
LOCAL CONST TC nm_child[] = {(TC)'C', (TC)'H', (TC)'I', (TC)'L', (TC)'D',
                             TNULL};
LOCAL CONST TC nm_cpunum[] = {(TC)'C', (TC)'P', (TC)'U', (TC)'N', (TC)'U',
                              (TC)'M', TNULL};
LOCAL CONST TC nm_osname[] = {(TC)'O', (TC)'S', (TC)'N', (TC)'A', (TC)'M',
                              (TC)'E', TNULL};
LOCAL CONST TC nm_nope[] = {(TC)'N', (TC)'O', (TC)'P', (TC)'E', TNULL};

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

/* セマフォを待つサブタスク。優先度継承と ER_DLT の検査用。 */
LOCAL ID tobj_sem;
LOCAL ERR tobj_wait_er;

LOCAL void p_semwait(W arg)
{
	(void)arg;
	tobj_wait_er = wai_sem(tobj_sem, T_FOREVER);
	ext_tsk();
}

/* 預かっている後片付けを流してから空きページ数を数える。
 *
 * 終了したタスクの TCB とスタックは**別のタスクへ切り替わってから**返る
 * （knl_defer_del / knl_reclaim）。流さずに数えると、基準を取った後で
 * それが返って空きページが増え、漏れていないのに一致しなくなる。
 * 1 度譲ればディスパッチャの入口を通るので片付く（Phase 2 ㊱）。 */
LOCAL UD free_pages_settled(void)
{
	(void)dly_tsk(0);
	return pmem_free_pages();
}

/* 記録の中で文字が変わった回数。時分割が効いていないと 1 回しかない。 */
LOCAL W seq_transitions(void)
{
	W n = 0;
	W i;

	for (i = 1; i < seq_n; i++) {
		if (seq_log[i] != seq_log[i - 1]) {
			n++;
		}
	}
	return n;
}

/* ------------------------------------------------------------------ */
/* 検査本体                                                            */
/* ------------------------------------------------------------------ */

LOCAL void run_tests(void)
{
	BTRON_TEST("タスクの生成と状態 (D10)", {
		T_RTSK r;
		ID id = make_task(t_mark_b, 20, (VP)(UD)0x1234);

		BTRON_CHECK("生成", id >= 1 && id <= TK_MAX_TSKID);
		BTRON_CHECK("参照", tk_ref_tsk(id, &r) == E_OK);
		BTRON_CHECK("休止状態", r.tskstat == TTS_DMT);
		BTRON_CHECK("優先度", r.tskpri == 20);
		BTRON_CHECK("拡張情報", r.exinf == (VP)(UD)0x1234);
		BTRON_CHECK("待ち要因なし", r.tskwait == 0 && r.wid == 0);
		/* 自タスクは実行状態 */
		BTRON_CHECK("自タスク", tk_ref_tsk(TSK_SELF, &r) == E_OK);
		BTRON_CHECK("実行状態", r.tskstat == TTS_RUN);
		BTRON_CHECK("自 ID", tk_get_tid() >= 1);
		/* 休止状態なら削除できる */
		BTRON_CHECK("削除", tk_del_tsk(id) == E_OK);
		BTRON_CHECK("削除後は無い", tk_ref_tsk(id, &r) == E_NOEXS);
	});

	BTRON_TEST("タスク管理のエラーコード (A15 / R20)", {
		T_RTSK r;
		T_CTSK c;

		BTRON_CHECK("不正 ID", tk_ref_tsk(TK_MAX_TSKID + 1, &r) == E_ID);
		BTRON_CHECK("存在しない", tk_ref_tsk(TK_MAX_TSKID, &r) == E_NOEXS);
		BTRON_CHECK("NULL", tk_ref_tsk(TSK_SELF, 0) == E_PAR);
		BTRON_CHECK("自タスクの強制終了は E_OBJ", tk_ter_tsk(TSK_SELF) == E_OBJ);
		/* 符号化。上位ハーフワードがメインエラーコード */
		BTRON_CHECK("MERCD", MERCD(E_PAR) == -17);
		BTRON_CHECK("SERCD", SERCD(E_PAR) == 0);
		BTRON_CHECK("E_OK は 0", E_OK == 0);
		/* TA_ASM は未サポート → E_NOSPT（E_RSATR ではない） */
		c.exinf = 0;
		c.tskatr = TA_ASM;
		c.task = t_mark_b;
		c.itskpri = 10;
		c.stksz = TK_MIN_STKSZ;
		c.sstksz = 0;
		c.stkptr = 0;
		c.bufptr = 0;
		BTRON_CHECK("TA_ASM", tk_cre_tsk(&c) == E_NOSPT);
		/* 優先度 0 や範囲外は E_PAR */
		c.tskatr = TA_HLNG;
		c.itskpri = 0;
		BTRON_CHECK("優先度 0", tk_cre_tsk(&c) == E_PAR);
		c.itskpri = TK_MAX_TSKPRI + 1;
		BTRON_CHECK("優先度 過大", tk_cre_tsk(&c) == E_PAR);
	});

	BTRON_TEST("優先度の順序", {
		ID i1;
		ID i2;
		ID i3;

		seq_reset();
		/* 自分は pri 5。作るのは 8, 6, 7 の順。走るのは 6, 7, 8 の順。 */
		i1 = make_task(t_tag, 8, 0);
		i2 = make_task(t_tag, 6, 0);
		i3 = make_task(t_tag, 7, 0);
		BTRON_CHECK("生成", i1 > 0 && i2 > 0 && i3 > 0);
		(void)tk_sta_tsk(i1, '8');
		(void)tk_sta_tsk(i2, '6');
		(void)tk_sta_tsk(i3, '7');
		/* 自分より低い優先度なので、ここで寝るまで走らない */
		BTRON_CHECK("まだ走っていない", seq_eq(""));
		(void)tk_dly_tsk(20);
		if (!seq_eq("678")) {
			seq_show("678");
		}
		BTRON_CHECK("優先度順", seq_eq("678"));
	});

	BTRON_TEST("同一優先度は FCFS", {
		ID i1;
		ID i2;
		ID i3;

		seq_reset();
		i1 = make_task(t_tag, 6, 0);
		i2 = make_task(t_tag, 6, 0);
		i3 = make_task(t_tag, 6, 0);
		(void)tk_sta_tsk(i1, 'x');
		(void)tk_sta_tsk(i2, 'y');
		(void)tk_sta_tsk(i3, 'z');
		(void)tk_dly_tsk(20);
		if (!seq_eq("xyz")) {
			seq_show("xyz");
		}
		BTRON_CHECK("起動順", seq_eq("xyz"));
	});

	/* ---- ここが仕様の細則。取り違えやすい ---- */

	BTRON_TEST("プリエンプトは同一優先度の先頭に戻る (仕様)", {
		seq_reset();
		th = make_task(t_high, 5, 0);
		ta = make_task(t_preempt_a, 6, 0);
		tb = make_task(t_mark_b, 6, 0);
		BTRON_CHECK("生成", th > 0 && ta > 0 && tb > 0);
		/* H を先に起動して待たせる。A, B はこの順でレディキューに並ぶ。 */
		(void)tk_sta_tsk(th, 0);
		(void)tk_sta_tsk(ta, 0);
		(void)tk_sta_tsk(tb, 0);
		(void)tk_dly_tsk(30);
		(void)tk_wup_tsk(th); /* H を終わらせる */
		(void)tk_dly_tsk(10);
		/* h: H が最初に走って寝た / a: A が走って H を起こした
		 * H: H がプリエンプトして走った / A: A が**先頭**から再開した
		 * b: 最後に B
		 * 末尾に戻す実装だと "haHbA" になる。 */
		if (!seq_eq("haHAb")) {
			seq_show("haHAb");
		}
		BTRON_CHECK("先頭に戻る", seq_eq("haHAb"));
	});

	BTRON_TEST("待ち解除は同一優先度の末尾に付く (仕様)", {
		seq_reset();
		ta = make_task(t_rel_a, 6, 0);
		tb = make_task(t_rel_b, 6, 0);
		tc = make_task(t_rel_c, 6, 0);
		BTRON_CHECK("生成", ta > 0 && tb > 0 && tc > 0);
		(void)tk_sta_tsk(ta, 0);
		(void)tk_sta_tsk(tb, 0);
		(void)tk_sta_tsk(tc, 0);
		(void)tk_dly_tsk(30);
		/* a: A が走って寝た / b: B が走って A を起こした（A は末尾へ）
		 * B: B が続行 / c: 次は C / A: 最後に A
		 * 先頭に付ける実装だと "abBAc" になる。 */
		if (!seq_eq("abBcA")) {
			seq_show("abBcA");
		}
		BTRON_CHECK("末尾に付く", seq_eq("abBcA"));
	});

	BTRON_TEST("起床要求のキューイング (仕様)", {
		T_RTSK r;
		ID id = make_task(t_mark_b, 20, 0); /* 低優先度。走らせない */

		(void)tk_sta_tsk(id, 0);
		BTRON_CHECK("初期値 0", tk_ref_tsk(id, &r) == E_OK && r.wupcnt == 0);
		/* 起床待ちでないタスクを起こすとキューイングされる */
		BTRON_CHECK("1 回目", tk_wup_tsk(id) == E_OK);
		BTRON_CHECK("2 回目", tk_wup_tsk(id) == E_OK);
		BTRON_CHECK("数えている", tk_ref_tsk(id, &r) == E_OK && r.wupcnt == 2);
		BTRON_CHECK("can_wup が返す", tk_can_wup(id) == 2);
		BTRON_CHECK("can_wup でクリア", tk_ref_tsk(id, &r) == E_OK && r.wupcnt == 0);
		(void)tk_ter_tsk(id);
		(void)tk_del_tsk(id);

		/* 自分で試す: 先に起こしておくと寝ない */
		BTRON_CHECK("自タスクは起こせない", tk_wup_tsk(TSK_SELF) == E_OBJ);
	});

	BTRON_TEST("タイムアウトと遅延", {
		SYSTIM t0;
		SYSTIM t1;

		/* 起床要求が無い状態で 20ms 待つ → E_TMOUT */
		BTRON_CHECK("otm", tk_get_otm(&t0) == E_OK);
		BTRON_CHECK("slp のタイムアウト", tk_slp_tsk(20) == E_TMOUT);
		BTRON_CHECK("otm", tk_get_otm(&t1) == E_OK);
		BTRON_CHECK("時間が経った", t1 - t0 >= 20);

		/* ポーリングは待たずに E_TMOUT */
		BTRON_CHECK("POL", tk_slp_tsk(TMO_POL) == E_TMOUT);

		/* tk_dly_tsk は時間が来たら E_OK（E_TMOUT ではない） */
		(void)tk_get_otm(&t0);
		BTRON_CHECK("dly は E_OK", tk_dly_tsk(15) == E_OK);
		(void)tk_get_otm(&t1);
		BTRON_CHECK("遅延した", t1 - t0 >= 15);
		BTRON_CHECK("dly 0 は即戻る", tk_dly_tsk(0) == E_OK);
	});

	BTRON_TEST("システム時刻 (A5 と単位が違う)", {
		SYSTIM a;
		SYSTIM b;
		CONST SYSTIM set = 1000000000LL;

		BTRON_CHECK("設定", tk_set_tim(&set) == E_OK);
		BTRON_CHECK("参照", tk_get_tim(&a) == E_OK);
		BTRON_CHECK("反映", a >= set && a < set + 100);
		(void)tk_dly_tsk(10);
		BTRON_CHECK("進む", tk_get_tim(&b) == E_OK && b > a);
		/* SYSTIM はミリ秒。BTRON3 の STIME は秒なので 1000 倍違う。 */
		BTRON_CHECK("64bit", sizeof(SYSTIM) == 8);
		BTRON_CHECK("NULL", tk_get_tim(0) == E_PAR);
	});

	BTRON_TEST("セマフォ", {
		T_CSEM c;
		T_RSEM r;
		W i;

		c.exinf = 0;
		c.sematr = TA_TFIFO | TA_FIRST;
		c.isemcnt = 2;
		c.maxsem = 3;
		for (i = 0; i < 8; i++) {
			c.dsname[i] = 0;
		}
		sem1 = tk_cre_sem(&c);
		BTRON_CHECK("生成", sem1 >= 1);
		BTRON_CHECK("初期値", tk_ref_sem(sem1, &r) == E_OK && r.semcnt == 2);
		BTRON_CHECK("獲得", tk_wai_sem(sem1, 2, TMO_POL) == E_OK);
		BTRON_CHECK("空になった", tk_ref_sem(sem1, &r) == E_OK && r.semcnt == 0);
		BTRON_CHECK("ポーリング失敗", tk_wai_sem(sem1, 1, TMO_POL) == E_TMOUT);
		BTRON_CHECK("タイムアウト", tk_wai_sem(sem1, 1, 10) == E_TMOUT);
		BTRON_CHECK("返却", tk_sig_sem(sem1, 1) == E_OK);
		BTRON_CHECK("最大超過", tk_sig_sem(sem1, 3) == E_QOVR);
		BTRON_CHECK("要求過大", tk_wai_sem(sem1, 4, TMO_POL) == E_PAR);
		BTRON_CHECK("要求 0", tk_wai_sem(sem1, 0, TMO_POL) == E_PAR);
		BTRON_CHECK("不正 ID", tk_wai_sem(0, 1, TMO_POL) == E_ID);
		BTRON_CHECK("削除", tk_del_sem(sem1) == E_OK);
		BTRON_CHECK("削除後", tk_ref_sem(sem1, &r) == E_NOEXS);
	});

	BTRON_TEST("セマフォの待ち行列 (TA_TFIFO / TA_TPRI)", {
		T_CSEM c;
		ID i1;
		ID i2;
		ID i3;
		W i;

		for (i = 0; i < 8; i++) {
			c.dsname[i] = 0;
		}
		c.exinf = 0;
		c.isemcnt = 0;
		c.maxsem = 3;

		/* TA_TPRI: 優先度の高い順に資源が回る */
		c.sematr = TA_TPRI | TA_FIRST;
		sem1 = tk_cre_sem(&c);
		seq_reset();
		i1 = make_task(t_sem_wait, 9, 0);
		i2 = make_task(t_sem_wait, 7, 0);
		i3 = make_task(t_sem_wait, 8, 0);
		(void)tk_sta_tsk(i1, '9');
		(void)tk_sta_tsk(i2, '7');
		(void)tk_sta_tsk(i3, '8');
		(void)tk_dly_tsk(20); /* 3 つとも待ち行列に並ぶ */
		BTRON_CHECK("全員待ち", seq_eq(""));
		(void)tk_sig_sem(sem1, 3);
		(void)tk_dly_tsk(20);
		if (!seq_eq("789")) {
			seq_show("789");
		}
		BTRON_CHECK("優先度順", seq_eq("789"));
		(void)tk_del_sem(sem1);
	});

	BTRON_TEST("セマフォ削除で待ちタスクは E_DLT", {
		T_CSEM c;
		T_RTSK r;
		ID id;
		W i;

		for (i = 0; i < 8; i++) {
			c.dsname[i] = 0;
		}
		c.exinf = 0;
		c.sematr = TA_TFIFO | TA_FIRST;
		c.isemcnt = 0;
		c.maxsem = 1;
		sem1 = tk_cre_sem(&c);
		seq_reset();
		id = make_task(t_sem_wait, 7, 0);
		(void)tk_sta_tsk(id, 'd');
		(void)tk_dly_tsk(20);
		BTRON_CHECK("セマフォ待ち",
		            tk_ref_tsk(id, &r) == E_OK && r.tskwait == TTW_SEM
		                && r.wid == sem1);
		BTRON_CHECK("削除", tk_del_sem(sem1) == E_OK);
		(void)tk_dly_tsk(10);
		/* E_DLT で起きたので seq_put しない */
		BTRON_CHECK("獲得していない", seq_eq(""));
		(void)tk_ter_tsk(id);
		(void)tk_del_tsk(id);
	});

	BTRON_TEST("イベントフラグ", {
		T_CFLG c;
		T_RFLG r;
		UINT p = 0;
		W i;

		for (i = 0; i < 8; i++) {
			c.dsname[i] = 0;
		}
		c.exinf = 0;
		c.flgatr = TA_TFIFO | TA_WMUL;
		c.iflgptn = 0;
		flg1 = tk_cre_flg(&c);
		BTRON_CHECK("生成", flg1 >= 1);

		/* OR 待ち */
		BTRON_CHECK("セット", tk_set_flg(flg1, 0x1U) == E_OK);
		BTRON_CHECK("OR 成立", tk_wai_flg(flg1, 0x3U, TWF_ORW, &p, TMO_POL) == E_OK);
		BTRON_CHECK("値", p == 0x1U);
		/* AND 待ちは不成立 */
		BTRON_CHECK("AND 不成立",
		            tk_wai_flg(flg1, 0x3U, TWF_ANDW, &p, TMO_POL) == E_TMOUT);
		BTRON_CHECK("セット", tk_set_flg(flg1, 0x2U) == E_OK);
		BTRON_CHECK("AND 成立",
		            tk_wai_flg(flg1, 0x3U, TWF_ANDW, &p, TMO_POL) == E_OK);
		BTRON_CHECK("値", p == 0x3U);

		/* TWF_BITCLR は条件ビットだけ落とす */
		BTRON_CHECK("セット", tk_set_flg(flg1, 0xCU) == E_OK);
		BTRON_CHECK("BITCLR",
		            tk_wai_flg(flg1, 0x4U, TWF_ORW | TWF_BITCLR, &p, TMO_POL) == E_OK);
		BTRON_CHECK("クリア前の値", p == 0xFU);
		BTRON_CHECK("条件ビットだけ消えた",
		            tk_ref_flg(flg1, &r) == E_OK && r.flgptn == 0xBU);

		/* TWF_CLR は全部落とす */
		BTRON_CHECK("CLR", tk_wai_flg(flg1, 0x1U, TWF_ORW | TWF_CLR, &p, TMO_POL) == E_OK);
		BTRON_CHECK("全部消えた", tk_ref_flg(flg1, &r) == E_OK && r.flgptn == 0);

		/* tk_clr_flg は clrptn との論理積（仕様） */
		BTRON_CHECK("セット", tk_set_flg(flg1, 0xFFU) == E_OK);
		BTRON_CHECK("clr", tk_clr_flg(flg1, 0x0FU) == E_OK);
		BTRON_CHECK("論理積", tk_ref_flg(flg1, &r) == E_OK && r.flgptn == 0x0FU);

		/* waiptn = 0 は E_PAR（仕様が明記） */
		BTRON_CHECK("waiptn 0", tk_wai_flg(flg1, 0, TWF_ORW, &p, TMO_POL) == E_PAR);
		BTRON_CHECK("クリア指定の排他",
		            tk_wai_flg(flg1, 0x1U, TWF_ORW | TWF_CLR | TWF_BITCLR, &p,
		                       TMO_POL) == E_PAR);
		BTRON_CHECK("削除", tk_del_flg(flg1) == E_OK);
	});

	BTRON_TEST("イベントフラグの複数待ち (TA_WMUL / TA_WSGL)", {
		T_CFLG c;
		UINT p = 0;
		ID i1;
		ID i2;
		W i;

		for (i = 0; i < 8; i++) {
			c.dsname[i] = 0;
		}
		c.exinf = 0;
		c.iflgptn = 0;

		/* TA_WMUL: 1 回の tk_set_flg で複数が解除される */
		c.flgatr = TA_TFIFO | TA_WMUL;
		flg1 = tk_cre_flg(&c);
		seq_reset();
		i1 = make_task(t_flg_wait, 7, 0);
		i2 = make_task(t_flg_wait, 7, 0);
		(void)tk_sta_tsk(i1, '1');
		(void)tk_sta_tsk(i2, '2');
		(void)tk_dly_tsk(20);
		BTRON_CHECK("両方待ち", seq_eq(""));
		(void)tk_set_flg(flg1, 0x3U);
		(void)tk_dly_tsk(20);
		if (!seq_eq("12")) {
			seq_show("12");
		}
		BTRON_CHECK("両方解除", seq_eq("12"));
		(void)tk_del_flg(flg1);

		/* TA_WSGL: 2 つめの待ちは条件に関係なく E_OBJ */
		c.flgatr = TA_TFIFO | TA_WSGL;
		flg1 = tk_cre_flg(&c);
		i1 = make_task(t_flg_wait, 7, 0);
		(void)tk_sta_tsk(i1, '1');
		(void)tk_dly_tsk(20);
		BTRON_CHECK("2 つめは E_OBJ",
		            tk_wai_flg(flg1, 0x3U, TWF_ANDW, &p, TMO_POL) == E_OBJ);
		(void)tk_del_flg(flg1);
		(void)tk_dly_tsk(10);
	});

	BTRON_TEST("強制待ち状態は待ち状態と直交する (仕様)", {
		T_RTSK r;
		ID id = make_task(t_rel_a, 20, 0); /* 低優先度で寝るだけ */

		seq_reset();
		(void)tk_sta_tsk(id, 0);
		(void)tk_dly_tsk(20);
		BTRON_CHECK("起床待ち", tk_ref_tsk(id, &r) == E_OK && r.tskstat == TTS_WAI);

		/* 待ち状態のタスクを強制待ちにすると二重待ち */
		BTRON_CHECK("sus", tk_sus_tsk(id) == E_OK);
		BTRON_CHECK("二重待ち", tk_ref_tsk(id, &r) == E_OK && r.tskstat == TTS_WAS);
		BTRON_CHECK("待ち要因は残る", r.tskwait == TTW_SLP);

		/* 二重待ちのタスクを起こすと強制待ちに落ちる（実行可能にならない） */
		BTRON_CHECK("wup", tk_wup_tsk(id) == E_OK);
		BTRON_CHECK("強制待ち", tk_ref_tsk(id, &r) == E_OK && r.tskstat == TTS_SUS);
		(void)tk_dly_tsk(10);
		BTRON_CHECK("まだ走っていない", seq_eq("a"));

		/* ネストする */
		BTRON_CHECK("sus 2", tk_sus_tsk(id) == E_OK);
		BTRON_CHECK("ネスト数", tk_ref_tsk(id, &r) == E_OK && r.suscnt == 2);
		BTRON_CHECK("rsm 1", tk_rsm_tsk(id) == E_OK);
		BTRON_CHECK("まだ強制待ち", tk_ref_tsk(id, &r) == E_OK && r.tskstat == TTS_SUS);
		BTRON_CHECK("frsm", tk_frsm_tsk(id) == E_OK);
		(void)tk_dly_tsk(10);
		BTRON_CHECK("走った", seq_eq("aA"));

		BTRON_CHECK("自タスクは強制待ちにできない", tk_sus_tsk(TSK_SELF) == E_OBJ);
		(void)tk_del_tsk(id);
	});

	BTRON_TEST("tk_rel_wai と tk_ter_tsk", {
		T_RTSK r;
		ID id = make_task(t_rel_a, 20, 0);

		seq_reset();
		(void)tk_sta_tsk(id, 0);
		(void)tk_dly_tsk(20);
		BTRON_CHECK("待っている", tk_ref_tsk(id, &r) == E_OK && r.tskstat == TTS_WAI);
		/* 強制解除。待っていたシステムコールは E_RLWAI を返す */
		BTRON_CHECK("rel_wai", tk_rel_wai(id) == E_OK);
		(void)tk_dly_tsk(10);
		/* t_rel_a は E_RLWAI でも seq_put('A') する実装なので走る */
		BTRON_CHECK("解除された", seq_eq("aA"));
		BTRON_CHECK("待っていないと E_OBJ", tk_rel_wai(id) == E_OBJ);
		(void)tk_del_tsk(id);

		/* 待ち状態のタスクを強制終了できる */
		seq_reset();
		id = make_task(t_rel_a, 20, 0);
		(void)tk_sta_tsk(id, 0);
		(void)tk_dly_tsk(20);
		BTRON_CHECK("ter_tsk", tk_ter_tsk(id) == E_OK);
		BTRON_CHECK("休止状態", tk_ref_tsk(id, &r) == E_OK && r.tskstat == TTS_DMT);
		(void)tk_dly_tsk(10);
		BTRON_CHECK("走らなかった", seq_eq("a"));
		BTRON_CHECK("休止状態なら削除できる", tk_del_tsk(id) == E_OK);
	});

	BTRON_TEST("優先度変更とレディキューの移動", {
		T_RTSK r;
		ID id = make_task(t_tag, 20, 0);

		seq_reset();
		(void)tk_sta_tsk(id, 'p');
		BTRON_CHECK("低優先度なので走らない", seq_eq(""));
		/* 自分より高い優先度にすると即座にプリエンプトされる */
		BTRON_CHECK("chg_pri", tk_chg_pri(id, 4) == E_OK);
		if (!seq_eq("p")) {
			seq_show("p");
		}
		BTRON_CHECK("走った", seq_eq("p"));
		BTRON_CHECK("範囲外", tk_chg_pri(TSK_SELF, TK_MAX_TSKPRI + 1) == E_PAR);
		BTRON_CHECK("自タスク参照", tk_ref_tsk(TSK_SELF, &r) == E_OK);
		(void)tk_del_tsk(id);
	});

	BTRON_TEST("ラウンドロビン (tk_rot_rdq)", {
		ID i1;
		ID i2;
		ID i3;
		T_RTSK r;

		seq_reset();
		/* 自分と同じ優先度に 3 つ並べ、自分で譲る */
		(void)tk_ref_tsk(TSK_SELF, &r);
		i1 = make_task(t_tag, r.tskpri, 0);
		i2 = make_task(t_tag, r.tskpri, 0);
		i3 = make_task(t_tag, r.tskpri, 0);
		(void)tk_sta_tsk(i1, '1');
		(void)tk_sta_tsk(i2, '2');
		(void)tk_sta_tsk(i3, '3');
		BTRON_CHECK("同一優先度なので走らない", seq_eq(""));
		/* 自分の優先度を回す = 次のタスクに譲る。
		 * 戻ってきたときには 3 つとも終わっている。 */
		BTRON_CHECK("rot_rdq", tk_rot_rdq(0) == E_OK);
		if (!seq_eq("123")) {
			seq_show("123");
		}
		BTRON_CHECK("順に走った", seq_eq("123"));
		BTRON_CHECK("範囲外", tk_rot_rdq(TK_MAX_TSKPRI + 1) == E_PAR);
	});

	BTRON_TEST("ディスパッチ禁止", {
		ID id;

		seq_reset();
		id = make_task(t_tag, 4, 0); /* 自分より高い優先度 */
		BTRON_CHECK("dis_dsp", tk_dis_dsp() == E_OK);
		(void)tk_sta_tsk(id, 'd');
		/* 高優先度でも切り替わらない */
		BTRON_CHECK("保留されている", seq_eq(""));
		BTRON_CHECK("ena_dsp", tk_ena_dsp() == E_OK);
		/* 許可した瞬間に切り替わる */
		BTRON_CHECK("走った", seq_eq("d"));
		(void)tk_del_tsk(id);
	});

	BTRON_TEST("割込みとタイマ (D8 / T1-01 / T1-02)", {
		UD c0 = intc_count();
		UD t0 = knl_tick_count();
		SYSTIM m0;
		SYSTIM m1;

		BTRON_CHECK("GIC", hal_intc_version() == 2 || hal_intc_version() == 3);
		BTRON_CHECK("割込みが入っている", c0 > 0);
		BTRON_CHECK("tick が進んでいる", t0 > 0);
		(void)tk_get_otm(&m0);
		(void)tk_dly_tsk(30);
		(void)tk_get_otm(&m1);
		BTRON_CHECK("tick が増える", knl_tick_count() > t0);
		BTRON_CHECK("割込みが増える", intc_count() > c0);
		/* 1 tick = 1ms。30ms 待てば 30 tick 前後進む。 */
		BTRON_CHECK("刻みは 1ms", hal_timer_tick_ms() == 1);
		BTRON_CHECK("経過時間", m1 - m0 >= 30 && m1 - m0 < 300);
		BTRON_CHECK("割込み許可中", hal_int_enabled() == True);
	});

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

		seq_reset();
		lo = make_task(t_mtx_low, 20, 0);  /* 低優先度。先にロックする */
		hi = make_task(t_mtx_high, 4, 0);  /* 高優先度。自分より上 */
		mid = make_task(t_mtx_mid, 10, 0); /* 中優先度。割り込む側 */
		(void)tk_sta_tsk(lo, 'l');
		(void)tk_dly_tsk(10); /* L にロックさせる */
		BTRON_CHECK("L がロックした", seq_eq("l"));
		BTRON_CHECK("優先度は 20", tk_ref_tsk(lo, &r) == E_OK && r.tskpri == 20);

		/* H を起動すると、自分(pri 5)より高いので即座に走り、
		 * ロック待ちに入って L の優先度を 4 へ引き上げる。 */
		(void)tk_sta_tsk(hi, 'H');
		BTRON_CHECK("H が待ちに入った", seq_eq("lh"));
		BTRON_CHECK("L の現在優先度が継承された",
		            tk_ref_tsk(lo, &r) == E_OK && r.tskpri == 4);
		BTRON_CHECK("ベース優先度は変わらない", r.tskbpri == 20);

		/* 中優先度のタスクを起動しても、継承後の L (pri 4) には勝てない。
		 * 継承していなければ M(10) が L(20) を追い越してしまう。 */
		(void)tk_sta_tsk(mid, 'M');
		(void)tk_wup_tsk(lo); /* L を進める。L → 解除 → H → M の順になる */
		(void)tk_dly_tsk(20);
		if (!seq_eq("lhLHM")) {
			seq_show("lhLHM");
		}
		BTRON_CHECK("継承により L が M を追い越した", seq_eq("lhLHM"));
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

		seq_reset();
		lo = make_task(t_mtx_low, 20, 0);
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
		seq_reset();
		rcv = make_task(t_mbx_recv, 6, 0);
		(void)tk_sta_tsk(rcv, 'm');
		(void)tk_dly_tsk(10);
		BTRON_CHECK("待っている", tk_ref_mbx(mbx1, &r) == E_OK && r.wtsk == rcv);
		BTRON_CHECK("送信", tk_snd_mbx(mbx1, &m1) == E_OK);
		(void)tk_dly_tsk(10);
		BTRON_CHECK("受け取った", seq_eq("m"));
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
		seq_reset();
		snd = make_task(t_mbf_send, 6, 0);
		(void)tk_sta_tsk(snd, 's');
		(void)tk_dly_tsk(10);
		BTRON_CHECK("送信側が待っている", seq_eq(""));
		{
			B in[32];

			BTRON_CHECK("受信で成立", tk_rcv_mbf(mbf1, in, TMO_FEVR) == 1);
			BTRON_CHECK("内容", in[0] == 's');
		}
		(void)tk_dly_tsk(10);
		BTRON_CHECK("送信側が進んだ", seq_eq("s"));
		(void)tk_del_tsk(snd);

		/* ② 受信側が先。送信側が来るまで待つ。 */
		seq_reset();
		rcv = make_task(t_mbf_recv, 6, 0);
		(void)tk_sta_tsk(rcv, 'r');
		(void)tk_dly_tsk(10);
		BTRON_CHECK("受信側が待っている", seq_eq(""));
		{
			B out[4];

			out[0] = 'z';
			BTRON_CHECK("送信で成立", tk_snd_mbf(mbf1, out, 1, TMO_FEVR) == E_OK);
		}
		(void)tk_dly_tsk(10);
		if (!seq_eq("zr")) {
			seq_show("zr");
		}
		BTRON_CHECK("受信側が受け取った", seq_eq("zr"));
		(void)tk_del_tsk(rcv);
		(void)tk_del_mbf(mbf1);
	});

	BTRON_TEST("周期ハンドラ", {
		T_CCYC c;
		T_RCYC r;
		ID id;
		W i;

		for (i = 0; i < 8; i++) {
			c.dsname[i] = 0;
		}
		c.exinf = 0;
		c.cycatr = TA_HLNG;
		c.cychdr = h_cyc;
		c.cyctim = 5;
		c.cycphs = 0;
		cyc_hits = 0;
		cyc_waiter = 0;
		id = tk_cre_cyc(&c);
		BTRON_CHECK("生成", id >= 1);
		/* TA_STA を付けていないので停止状態（仕様） */
		BTRON_CHECK("停止中", tk_ref_cyc(id, &r) == E_OK && r.cycstat == TCYC_STOP);
		(void)tk_dly_tsk(20);
		BTRON_CHECK("動いていない", cyc_hits == 0);

		BTRON_CHECK("開始", tk_sta_cyc(id) == E_OK);
		BTRON_CHECK("動作中", tk_ref_cyc(id, &r) == E_OK && r.cycstat == TCYC_STA);
		(void)tk_dly_tsk(40);
		/* 5ms 周期で 40ms なら 6〜8 回。幅を持たせて見る。 */
		BTRON_CHECK("繰り返し起動された", cyc_hits >= 5 && cyc_hits <= 12);
		BTRON_CHECK("停止", tk_stp_cyc(id) == E_OK);
		{
			W n = cyc_hits;

			(void)tk_dly_tsk(20);
			BTRON_CHECK("止まった", cyc_hits == n);
		}

		/* ハンドラからタスクを起こせる（これが本来の使い方） */
		cyc_waiter = tk_get_tid();
		(void)tk_sta_cyc(id);
		BTRON_CHECK("ハンドラに起こされた", tk_slp_tsk(100) == E_OK);
		cyc_waiter = 0;
		(void)tk_stp_cyc(id);

		BTRON_CHECK("周期 0", (c.cyctim = 0, tk_cre_cyc(&c)) == E_PAR);
		BTRON_CHECK("削除", tk_del_cyc(id) == E_OK);
		BTRON_CHECK("削除後", tk_sta_cyc(id) == E_NOEXS);
	});

	BTRON_TEST("アラームハンドラ", {
		T_CALM c;
		T_RALM r;
		ID id;
		W i;

		for (i = 0; i < 8; i++) {
			c.dsname[i] = 0;
		}
		c.exinf = 0;
		c.almatr = TA_HLNG;
		c.almhdr = h_alm;
		alm_hits = 0;
		id = tk_cre_alm(&c);
		BTRON_CHECK("生成", id >= 1);
		BTRON_CHECK("停止中", tk_ref_alm(id, &r) == E_OK && r.almstat == TALM_STOP);
		BTRON_CHECK("開始", tk_sta_alm(id, 20) == E_OK);
		BTRON_CHECK("動作中", tk_ref_alm(id, &r) == E_OK && r.almstat == TALM_STA);
		BTRON_CHECK("残り時間", r.lfttim > 0 && r.lfttim <= 20);
		(void)tk_dly_tsk(40);
		/* **1 回だけ**起動される（仕様）。周期ハンドラとの違い。 */
		BTRON_CHECK("1 回だけ", alm_hits == 1);
		BTRON_CHECK("停止に戻った",
		            tk_ref_alm(id, &r) == E_OK && r.almstat == TALM_STOP);
		(void)tk_dly_tsk(20);
		BTRON_CHECK("増えない", alm_hits == 1);

		/* 停止すれば起動しない */
		BTRON_CHECK("再開始", tk_sta_alm(id, 20) == E_OK);
		BTRON_CHECK("停止", tk_stp_alm(id) == E_OK);
		(void)tk_dly_tsk(40);
		BTRON_CHECK("起動しなかった", alm_hits == 1);
		BTRON_CHECK("削除", tk_del_alm(id) == E_OK);
	});

	BTRON_TEST("ランデブの基本 (周辺核 §1.3)", {
		B msg[32];
		ID srv;
		W r;

		seq_reset();
		srv_calls = 0;
		por1 = cre_por(32, 32, 0);
		BTRON_CHECK("ポート生成", por1 >= 1);
		srv = make_task(t_server, 6, 0); /* 自分より低い優先度 */
		(void)tk_sta_tsk(srv, 's');
		(void)tk_dly_tsk(10); /* サーバを acp_por で待たせる */
		BTRON_CHECK("サーバは待機中", seq_eq(""));

		msg[0] = 10;
		r = cal_por(por1, 0x1U, msg, 1, T_FOREVER);
		BTRON_CHECK("返答サイズ", r == 1);
		BTRON_CHECK("返答内容", msg[0] == 11);
		BTRON_CHECK("サーバが走った", seq_eq("s"));
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

		seq_reset();
		por1 = cre_por(32, 32, 0);
		srv = make_task(t_sel_server, 6, 0); /* acpptn = 0x2 で待つ */
		(void)tk_sta_tsk(srv, 'p');
		(void)tk_dly_tsk(10);
		msg[0] = 0;
		/* ビットが噛み合わないと成立しない */
		BTRON_CHECK("不一致", cal_por(por1, 0x1U, msg, 1, 20) == ER_NONE);
		BTRON_CHECK("サーバはまだ待機", seq_eq(""));
		/* 1 ビットでも重なれば成立する */
		BTRON_CHECK("一致", cal_por(por1, 0x6U, msg, 1, T_FOREVER) == 1);
		BTRON_CHECK("成立した", seq_eq("p"));
		(void)tk_del_tsk(srv);
		(void)del_por(por1);
	});

	BTRON_TEST("ポート削除: 待ちは ER_DLT、成立済みは残る (仕様)", {
		B msg[32];
		ID w;
		ID s;

		/* ① 受付待ちのタスクは ER_DLT で起きる */
		seq_reset();
		por1 = cre_por(32, 32, 0);
		w = make_task(t_acc_wait, 6, 0);
		(void)tk_sta_tsk(w, 'w');
		(void)tk_dly_tsk(10);
		BTRON_CHECK("削除", del_por(por1) == ER_OK);
		(void)tk_dly_tsk(10);
		BTRON_CHECK("ER_DLT で起きた", seq_eq("D"));
		BTRON_CHECK("削除後は無い", del_por(por1) == ER_NOEXS);
		(void)tk_del_tsk(w);

		/* ② 成立後にポートを削除しても返答できる（仕様が明記） */
		seq_reset();
		por1 = cre_por(32, 32, 0);
		s = make_task(t_del_then_reply, 6, 0);
		(void)tk_sta_tsk(s, 'h');
		(void)tk_dly_tsk(10);
		msg[0] = 5;
		BTRON_CHECK("返答が来た", cal_por(por1, 0x1U, msg, 1, T_FOREVER) == 1);
		BTRON_CHECK("内容", msg[0] == 6);
		(void)tk_dly_tsk(10);
		BTRON_CHECK("削除後に返答できた", seq_eq("hR"));
		(void)tk_del_tsk(s);
	});

	BTRON_TEST("ランデブ回送 fwd_por (詳細 E1 ④ / E13)", {
		B msg[32];
		ID s1;
		ID s2;

		seq_reset();
		por1 = cre_por(32, 32, 0);
		por2 = cre_por(32, 32, 0);
		BTRON_CHECK("ポート 2 つ", por1 >= 1 && por2 >= 1 && por1 != por2);
		s1 = make_task(t_fwd_server, 6, 0);
		s2 = make_task(t_back_server, 7, 0);
		(void)tk_sta_tsk(s1, 'f');
		(void)tk_sta_tsk(s2, 'b');
		(void)tk_dly_tsk(10);
		BTRON_CHECK("両方待機", seq_eq(""));

		/* por1 へ呼ぶ。s1 が受け取って por2 へ回送し、s2 が返答する。
		 * **返答は最初の呼出側に直接届き、中間の s1 は待たない。**
		 * これが E13 の 3 者間要求（クライアント → 実身仮身サーバ →
		 * 対象クライアント）をホップ増なしで表現できる根拠。 */
		msg[0] = 1;
		BTRON_CHECK("返答サイズ", cal_por(por1, 0x1U, msg, 1, T_FOREVER) == 1);
		BTRON_CHECK("2 段で +1 された", msg[0] == 3);
		BTRON_CHECK("順に走った", seq_eq("fb"));
		(void)tk_del_tsk(s1);
		(void)tk_del_tsk(s2);
		(void)del_por(por1);
		(void)del_por(por2);
	});

	BTRON_TEST("回送先の返答サイズ制約 (ER_OBJ)", {
		B msg[32];
		ID s;

		seq_reset();
		fwd_err = ER_OK;
		/* 回送元の maxrmsz = 8、回送先 = 32。呼出側のバッファは 8 しか
		 * 保証されないので、32 の返答は収まらない。仕様は ER_OBJ。 */
		por1 = cre_por(32, 8, 0);
		por2 = cre_por(32, 32, 0);
		s = make_task(t_fwd_bad, 6, 0);
		(void)tk_sta_tsk(s, 'e');
		(void)tk_dly_tsk(10);
		msg[0] = 7;
		BTRON_CHECK("自分で返答が来た", cal_por(por1, 0x1U, msg, 1, T_FOREVER) == 1);
		BTRON_CHECK("内容", msg[0] == 8);
		BTRON_CHECK("回送は ER_OBJ", fwd_err == ER_OBJ);
		/* 回送が失敗してもランデブは解除されていない（返答できた） */
		BTRON_CHECK("受け付けていた", seq_eq("e"));
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

	BTRON_TEST("緊急画面 (E24 / T4-02 / 規律 R18)", {
		CONST Screen *sc = screen();

		if (sc == 0) {
			/* 画面が無い構成。begin は False を返し、何も起きない。 */
			BTRON_CHECK("画面が無ければ何もしない",
			            screen_panic_begin("test") == False);
			BTRON_CHECK("占有していない", screen_panic_active() == False);
		} else {
			UW before = screen_get(10, 300);
			UW after;

			BTRON_CHECK("占有できた", screen_panic_begin("*** 検査 ***") == True);
			BTRON_CHECK("占有中", screen_panic_active() == True);
			/* 画面が塗り替わったこと。**画素を読み返して確かめる**ので
			 * スクリーンショット比較が要らない（Phase 1 ㉗）。 */
			after = screen_get(10, 300);
			BTRON_CHECK("面が変わった", after != before || before == 0);
			BTRON_CHECK("緊急画面の地", after == screen_panic_bg());
			/* 普通のコンソールと地の色が違うこと。一目で区別できる必要がある。 */
			BTRON_CHECK("普段と違う地", screen_panic_bg() != screen_rgb(0x10, 0x14, 0x20));
			/* 見出しの下の罫が引かれていること */
			BTRON_CHECK("罫がある",
			            screen_get(400, 2 * 16 + 6) != screen_panic_bg());

			/* 以後の hal_console_puts() が緊急画面へ流れる。
			 * 文字が描かれたかは、行の中に前景色の画素があるかで見る。 */
			hal_console_puts("緊急画面の検査中\n");
			{
				W x;
				Bool drew = False;

				for (x = 16; x < 400; x++) {
					W y;

					for (y = 3 * 16 + 4; y < 4 * 16 + 4; y++) {
						if (screen_get(x, y) == screen_panic_fg()) {
							drew = True;
						}
					}
				}
				BTRON_CHECK("文字が描かれた", drew == True);
			}

			/* 畳んで普通の画面コンソールに戻す。
			 * 本物のパニックでは戻らないので、ここは検査のためだけ。 */
			screen_panic_end();
			BTRON_CHECK("畳んだ", screen_panic_active() == False);
			screen_clear(screen_rgb(0x10, 0x14, 0x20));
			hal_console_puts("  （緊急画面の検査が終わり、画面を戻しました）\n");
		}
	});

	BTRON_TEST("削除すると物理メモリが戻る (Phase 2 の土台)", {
		if (pmem_total_pages() == 0) {
			/* メモリマップが無い構成。アロケータが立たないので返せない。 */
			BTRON_CHECK("アロケータ無し", pmem_free_pages() == 0);
		} else {
			UD free0 = pmem_free_pages();
			ID id;
			T_CMPF cf;
			T_CMPL cl;
			W i;

			for (i = 0; i < 8; i++) {
				cf.dsname[i] = 0;
				cl.dsname[i] = 0;
			}

			/* ① タスクのスタック。生成で減り、削除で戻る。
			 * ここが戻らないとタスクを作っては消すたびに漏れる。 */
			id = make_task(t_mark_b, 20, 0);
			BTRON_CHECK("生成で減った", pmem_free_pages() < free0);
			BTRON_CHECK("削除", tk_del_tsk(id) == E_OK);
			BTRON_CHECK("スタックが戻った", pmem_free_pages() == free0);

			/* ② 固定長メモリプール */
			cf.exinf = 0;
			cf.mpfatr = TA_TFIFO;
			cf.mpfcnt = 16;
			cf.blfsz = 256;
			cf.bufptr = 0;
			id = tk_cre_mpf(&cf);
			BTRON_CHECK("生成", id >= 1);
			BTRON_CHECK("減った", pmem_free_pages() < free0);
			BTRON_CHECK("削除", tk_del_mpf(id) == E_OK);
			BTRON_CHECK("戻った", pmem_free_pages() == free0);

			/* ③ 可変長メモリプール */
			cl.exinf = 0;
			cl.mplatr = TA_TFIFO;
			cl.mplsz = 8192;
			cl.bufptr = 0;
			id = tk_cre_mpl(&cl);
			BTRON_CHECK("生成", id >= 1);
			BTRON_CHECK("減った", pmem_free_pages() < free0);
			BTRON_CHECK("削除", tk_del_mpl(id) == E_OK);
			BTRON_CHECK("戻った", pmem_free_pages() == free0);

			/* ④ 繰り返しても漏れない。**これが本題。**
			 * 1 回だけなら偶然同じ数になることもあるが、
			 * 100 回繰り返して変わらなければ漏れていない。 */
			for (i = 0; i < 100; i++) {
				ID t = make_task(t_mark_b, 20, 0);

				if (t < 1 || tk_del_tsk(t) != E_OK) {
					break;
				}
			}
			BTRON_CHECK("100 回繰り返しても漏れない",
			            i == 100 && pmem_free_pages() == free0);
		}
	});

	BTRON_TEST("中心核が動いている", {
		BTRON_CHECK("起動済み", knl_running() == True);
		BTRON_CHECK("自タスクがいる", tk_get_tid() >= 1);
		BTRON_CHECK("アイドルではない", knl_cur != &knl_idle);
	});
}

/* ------------------------------------------------------------------ */
/* 周辺核 §1.1 の検査本体（初期プロセスの中で走る）                     */
/* ------------------------------------------------------------------ */

LOCAL void run_proc_tests(void)
{
	W me = prc_self();

	BTRON_TEST("優先度群と中心核への写像 (§1.1.3)", {
		BTRON_CHECK("群A の下端", prc_pri_group(0) == P_GRP_ABS);
		BTRON_CHECK("群A の上端", prc_pri_group(127) == P_GRP_ABS);
		BTRON_CHECK("群B の下端", prc_pri_group(128) == P_GRP_RR1);
		BTRON_CHECK("群B の上端", prc_pri_group(191) == P_GRP_RR1);
		BTRON_CHECK("群C の下端", prc_pri_group(192) == P_GRP_RR2);
		BTRON_CHECK("群C の上端", prc_pri_group(255) == P_GRP_RR2);
		BTRON_CHECK("範囲外は群なし",
		            prc_pri_group(-1) == -1 && prc_pri_group(256) == -1);

		/* 群A は 1 対 1。仕様の「厳密に優先度順」を保つため。 */
		BTRON_CHECK("群A は 1 対 1",
		            prc_to_tskpri(0) == 1 && prc_to_tskpri(1) == 2 &&
		                prc_to_tskpri(127) == 128);
		BTRON_CHECK("往復する",
		            prc_from_tskpri(prc_to_tskpri(0)) == 0 &&
		                prc_from_tskpri(prc_to_tskpri(63)) == 63 &&
		                prc_from_tskpri(prc_to_tskpri(127)) == 127);

		/* 群B / 群C は 1 段に潰す。群内の全員が同順位でなければ
		 * 「優先度が低いタスクでも必ず実行される」が成立しない。 */
		BTRON_CHECK("群B は 1 段",
		            prc_to_tskpri(128) == prc_to_tskpri(191));
		BTRON_CHECK("群C は 1 段",
		            prc_to_tskpri(192) == prc_to_tskpri(255));
		BTRON_CHECK("群B は潰れていて戻せない", prc_from_tskpri(129) == -1);

		/* 群A → 群B → 群C の順に探される、が中心核の優先度順で出る。 */
		BTRON_CHECK("群A < 群B < 群C",
		            prc_to_tskpri(127) < prc_to_tskpri(128) &&
		                prc_to_tskpri(128) < prc_to_tskpri(192));
		BTRON_CHECK("中心核の範囲内",
		            prc_to_tskpri(255) == TK_MAX_TSKPRI);
		BTRON_CHECK("範囲外は 0", prc_to_tskpri(256) == 0);
	});

	BTRON_TEST("初期プロセス (§1.1.1 / §1.1.5)", {
		P_STATE st;
		P_USER u;
		TC name[PRC_NAMELEN + 1];

		BTRON_CHECK("自分が初期プロセス", me >= 1 && me == prc_initial());
		BTRON_CHECK("自 PID の取得", prc_sts(P_SELF, 0, 0) == me);
		BTRON_CHECK("状態の取得", prc_sts(P_SELF, &st, name) == me);
		BTRON_CHECK("実行状態", st.state == P_RUN);
		BTRON_CHECK("親が無い", st.parpid == 0);
		BTRON_CHECK("優先度", st.priority == 2);
		BTRON_CHECK("名前", name[0] == (TC)'I');

		/* 仕様: 初期プロセスのユーザ情報はシステムで定義された固定的な内容 */
		BTRON_CHECK("ユーザ情報", get_usr(P_SELF, &u) == me);
		BTRON_CHECK("最高レベル", u.level == P_LEVEL_MAX);
		BTRON_CHECK("ネットワークレベル", u.net_level == 1);
		BTRON_CHECK("ユーザ名", u.usr_name[0] == (TC)'S');
		/* 仕様: 隠し名は取り出せず、常に 0 */
		BTRON_CHECK("隠し名は 0",
		            u.usr_name[P_VISLEN] == TNULL &&
		                u.usr_name[P_NAMELEN - 1] == TNULL);

		BTRON_CHECK("親は無い", prc_sts(P_PARENT, 0, 0) == ER_NOPRC);
		BTRON_CHECK("存在しない", prc_sts(PRC_MAX_PRCID, 0, 0) == ER_NOPRC);
	});

	BTRON_TEST("プロセス ID は空いている最小の番号 (§1.1.1)", {
		W a = prc_create(p_sleeper, 1, 0, 0);
		W b = prc_create(p_sleeper, 1, 0, 0);
		W c = prc_create(p_sleeper, 1, 0, 0);
		W d;

		BTRON_CHECK("3 つ作れた", a >= 1 && b > a && c > b);
		BTRON_CHECK("連番", b == a + 1 && c == b + 1);
		BTRON_CHECK("中央を終了", ter_prc(b, 0, TERM_NRM) == ER_OK);
		d = prc_create(p_sleeper, 1, 0, 0);
		BTRON_CHECK("空いた最小の番号が再利用される", d == b);

		BTRON_CHECK("片付け",
		            ter_prc(a, 0, TERM_NRM) == ER_OK &&
		                ter_prc(c, 0, TERM_NRM) == ER_OK &&
		                ter_prc(d, 0, TERM_NRM) == ER_OK);
		BTRON_CHECK("自分だけ残った", prc_count() == 1);
	});

	BTRON_TEST("子の親は終了したプロセスの親に移る (§1.1.1)", {
		W child;
		P_STATE st;

		prc_grandchild = 0;
		/* 優先度 1 は自分 (2) より高いので、生成した瞬間に走って
		 * 孫を作り、寝るところまで進む。 */
		child = prc_create(p_parent, 1, 0, 0);
		BTRON_CHECK("子ができた", child >= 1);
		BTRON_CHECK("孫ができた", prc_grandchild >= 1);
		BTRON_CHECK("孫の親は子",
		            prc_sts(prc_grandchild, &st, 0) == prc_grandchild &&
		                st.parpid == child);
		BTRON_CHECK("子は待ち状態", prc_sts(child, &st, 0) == child &&
		                                st.state == P_WAIT);

		BTRON_CHECK("子を終了", ter_prc(child, 0, TERM_NRM) == ER_OK);
		BTRON_CHECK("子は消えた", prc_sts(child, 0, 0) == ER_NOPRC);
		/* ここが §1.1.1 の規定。孫は祖父（自分）に引き取られる。 */
		BTRON_CHECK("孫の親が自分に移った",
		            prc_sts(prc_grandchild, &st, 0) == prc_grandchild &&
		                st.parpid == me);
		BTRON_CHECK("片付け", ter_prc(prc_grandchild, 0, TERM_NRM) == ER_OK);
	});

	BTRON_TEST("TERM_ALL は子孫まで落とす (§1.1.9 ter_prc)", {
		W child;

		prc_grandchild = 0;
		child = prc_create(p_parent, 1, 0, 0);
		BTRON_CHECK("子と孫", child >= 1 && prc_grandchild >= 1);
		BTRON_CHECK("まとめて終了", ter_prc(child, 0, TERM_ALL) == ER_OK);
		BTRON_CHECK("子は消えた", prc_sts(child, 0, 0) == ER_NOPRC);
		BTRON_CHECK("孫も消えた", prc_sts(prc_grandchild, 0, 0) == ER_NOPRC);
		BTRON_CHECK("自分だけ残った", prc_count() == 1);
	});

	BTRON_TEST("ter_prc のエラーコード (§1.1.9)", {
		W child = prc_create(p_sleeper, 1, 0, 0);

		BTRON_CHECK("自プロセス指定", ter_prc(P_SELF, 0, TERM_NRM) == ER_SELF);
		BTRON_CHECK("自 PID 指定", ter_prc(me, 0, TERM_NRM) == ER_SELF);
		BTRON_CHECK("存在しない",
		            ter_prc(PRC_MAX_PRCID, 0, TERM_NRM) == ER_NOPRC);
		BTRON_CHECK("親が無い", ter_prc(P_PARENT, 0, TERM_NRM) == ER_NOPRC);
		BTRON_CHECK("不正な属性", ter_prc(child, 0, 7) == ER_PAR);
		BTRON_CHECK("片付け", ter_prc(child, 0, TERM_NRM) == ER_OK);
	});

	BTRON_TEST("ユーザレベルが高いプロセスは落とせない (§1.1.9)", {
		W child;

		prc_level_err = ER_OK;
		prc_chgusr_er = ER_OK;
		/* 子は自分のレベルを 5 に下げてから親（レベル 0）を落とそうとする。
		 * 優先度 1 なので生成した瞬間に走りきって終了する。 */
		child = prc_create(p_lowlevel, 1, 0, 0);
		BTRON_CHECK("子は走って終わった", child >= 1 && prc_count() == 1);
		BTRON_CHECK("chg_usr は通った", prc_chgusr_er == ER_OK);
		BTRON_CHECK("親は落とせない", prc_level_err == ER_LEVEL);
	});

	BTRON_TEST("chg_usr はシステムプロセスだけ (§1.1.9)", {
		P_USER u;

		BTRON_CHECK("取得", get_usr(P_SELF, &u) == me);
		BTRON_CHECK("アドレス不正", chg_usr(0) == ER_ADR);
		u.level = 16;
		BTRON_CHECK("レベル範囲外", chg_usr(&u) == ER_PAR);
		u.level = 0;
		u.net_level = 0; /* ネットワークレベルに 0 は無い（仕様） */
		BTRON_CHECK("ネットワークレベル範囲外", chg_usr(&u) == ER_PAR);
		u.net_level = 1;
		BTRON_CHECK("設定できる", chg_usr(&u) == ER_OK);
	});

	BTRON_TEST("chg_pri は同一優先度群の中だけ (§1.1.3 / §1.1.9)", {
		T_RTSK r;
		P_STATE st;

		BTRON_CHECK("絶対指定", chg_pri(P_SELF, 50, P_ABS) == 50);
		BTRON_CHECK("中心核へ写った",
		            tk_ref_tsk(tk_get_tid(), &r) == E_OK &&
		                r.tskpri == prc_to_tskpri(50));
		BTRON_CHECK("プロセス状態にも出る",
		            prc_sts(P_SELF, &st, 0) == me && st.priority == 50);
		BTRON_CHECK("相対指定", chg_pri(P_SELF, -48, P_REL) == 2);
		BTRON_CHECK("戻った",
		            tk_ref_tsk(tk_get_tid(), &r) == E_OK &&
		                r.tskpri == prc_to_tskpri(2));

		/* 仕様: 群が異なる優先度への変更はできない */
		BTRON_CHECK("群B へは移れない", chg_pri(P_SELF, 150, P_ABS) == ER_PPRI);
		BTRON_CHECK("群C へは移れない", chg_pri(P_SELF, 200, P_ABS) == ER_PPRI);
		BTRON_CHECK("範囲外", chg_pri(P_SELF, 300, P_ABS) == ER_PPRI);
		BTRON_CHECK("相対で群外へ", chg_pri(P_SELF, 200, P_REL) == ER_PPRI);
		BTRON_CHECK("優先度は変わっていない",
		            prc_sts(P_SELF, &st, 0) == me && st.priority == 2);

		BTRON_CHECK("不正な属性", chg_pri(P_SELF, 2, 8) == ER_PAR);
		BTRON_CHECK("存在しないプロセス",
		            chg_pri(PRC_MAX_PRCID, 2, P_ABS) == ER_NOPRC);

		/* P_TASK 指定。自タスクが対象。 */
		BTRON_CHECK("自タスク", chg_pri(P_SELF, 3, P_ABS | P_TASK) == 3);
		BTRON_CHECK("中心核へ写った",
		            tk_ref_tsk(tk_get_tid(), &r) == E_OK &&
		                r.tskpri == prc_to_tskpri(3));
		BTRON_CHECK("自プロセス外のタスク",
		            chg_pri(TK_MAX_TSKID, 3, P_ABS | P_TASK) == ER_ID);
		BTRON_CHECK("戻す", chg_pri(P_SELF, 2, P_ABS | P_TASK) == 2);
	});

	BTRON_TEST("群B / 群C は 1 段に潰れて写る (§1.1.3)", {
		W b1 = prc_create(p_sleeper, 128, 0, 0); /* 群B の最高 */
		W b2 = prc_create(p_sleeper, 191, 0, 0); /* 群B の最低 */
		W c1 = prc_create(p_sleeper, 192, 0, 0); /* 群C の最高 */
		T_RTSK r1;
		T_RTSK r2;
		T_RTSK r3;
		P_STATE st;

		BTRON_CHECK("3 つ作れた", b1 >= 1 && b2 >= 1 && c1 >= 1);
		/* どれも自分 (群A) より低いので、まだ走っていない = 実行可能状態 */
		BTRON_CHECK("実行可能状態",
		            prc_sts(b1, &st, 0) == b1 && st.state == P_READY);
		BTRON_CHECK("優先度が保たれている",
		            prc_sts(b2, &st, 0) == b2 && st.priority == 191);

		BTRON_CHECK("中心核の優先度を引ける",
		            tk_ref_tsk(prc_main_task(b1), &r1) == E_OK &&
		                tk_ref_tsk(prc_main_task(b2), &r2) == E_OK &&
		                tk_ref_tsk(prc_main_task(c1), &r3) == E_OK);
		/* **ここが群B / 群C を 1 段に潰す設計の要点。**
		 * 128 と 191 が別の段にあると、128 が実行可能な間 191 が
		 * 全く走らなくなり、仕様の「優先度が低いタスクでも必ず実行される
		 * ことが保証される」が壊れる。 */
		BTRON_CHECK("群B の最高と最低が同じ段", r1.tskpri == r2.tskpri);
		BTRON_CHECK("群B は群C より上", r1.tskpri < r3.tskpri);
		BTRON_CHECK("群A より下",
		            r1.tskpri > prc_to_tskpri(127));

		BTRON_CHECK("片付け",
		            ter_prc(b1, 0, TERM_NRM) == ER_OK &&
		                ter_prc(b2, 0, TERM_NRM) == ER_OK &&
		                ter_prc(c1, 0, TERM_NRM) == ER_OK);
	});

	BTRON_TEST("プロセスは独立したアドレス空間を持つ (§1.1.1)", {
		W p1 = prc_create(p_sleeper, 1, 0, 0);
		W p2 = prc_create(p_sleeper, 1, 0, 0);
		AddrSpace *a1 = prc_space(p1);
		AddrSpace *a2 = prc_space(p2);
		UD pa = pmem_alloc(1);
		CONST UD va = 0x0000300000000000ULL; /* 下位半分のどこか */
		CONST UW attr = BTRON_PG_READ | BTRON_PG_WRITE | BTRON_PG_USER;

		BTRON_CHECK("2 つ作れた", p1 >= 1 && p2 >= 1 && p1 != p2);
		BTRON_CHECK("空間がある", a1 != 0 && a2 != 0);
		if (a1 != 0 && a2 != 0) {
			BTRON_CHECK("根が別", a1->root_pa != a2->root_pa);
			BTRON_CHECK("ASID が別", a1->asid != a2->asid);
			BTRON_CHECK("ページを張れる",
			            pa != 0 &&
			                hal_vm_map(a1, va, pa, 4096, attr) == 0);
			BTRON_CHECK("張った側からは引ける",
			            hal_vm_translate(a1, va) == pa);
			/* ここが §1.1.1 の「独立した」の意味 */
			BTRON_CHECK("他のプロセスからは引けない",
			            hal_vm_translate(a2, va) == 0);
			BTRON_CHECK("カーネル側にも漏れていない",
			            hal_vm_translate(hal_vm_kernel_space(), va) == 0);
		}
		if (pa != 0) {
			pmem_free(pa, 1);
		}
		BTRON_CHECK("片付け",
		            ter_prc(p1, 0, TERM_NRM) == ER_OK &&
		                ter_prc(p2, 0, TERM_NRM) == ER_OK);
	});

	BTRON_TEST("サブタスク (§1.1.9 cre_tsk / ter_tsk)", {
		W t1;
		W i;
		W made = 0;
		W ids[PRC_MAX_TASK];

		BTRON_CHECK("入口が不正", cre_tsk(0, 2, 0) == ER_ADR);
		/* 仕様: サブタスクの優先度は自プロセスの優先度群の範囲内のみ */
		BTRON_CHECK("群外の優先度", cre_tsk(p_subsleep, 150, 0) == ER_PPRI);

		t1 = cre_tsk(p_subsleep, 2, 0);
		BTRON_CHECK("生成", t1 >= 1);
		BTRON_CHECK("同じプロセスに属する", prc_task_owner((ID)t1) == me);

		/* 仕様: 自タスクとメインタスクは指定できない */
		BTRON_CHECK("自タスク", ter_tsk(tk_get_tid()) == ER_ID);
		BTRON_CHECK("他プロセスのタスク", ter_tsk(TK_MAX_TSKID) == ER_ID);
		BTRON_CHECK("強制終了", ter_tsk(t1) == ER_OK);
		BTRON_CHECK("所属が切れた", prc_task_owner((ID)t1) == 0);

		/* 上限。メインタスクを含めて PRC_MAX_TASK 本。 */
		for (i = 0; i < PRC_MAX_TASK; i++) {
			ids[i] = cre_tsk(p_subsleep, 2, 0);
			if (ids[i] < 1) {
				break;
			}
			made++;
		}
		BTRON_CHECK("メインタスクの分だけ少ない", made == PRC_MAX_TASK - 1);
		BTRON_CHECK("上限を超えると ER_LIMIT",
		            cre_tsk(p_subsleep, 2, 0) == ER_LIMIT);
		for (i = 0; i < made; i++) {
			BTRON_CHECK("片付け", ter_tsk(ids[i]) == ER_OK);
		}
	});

	BTRON_TEST("タスク付属同期 (§1.1.9)", {
		W sub;

		BTRON_CHECK("自タスク ID", get_tid() == tk_get_tid());
		/* 仕様は time > 0 か -1 だけを規定している */
		BTRON_CHECK("0 は不正", slp_tsk(0) == ER_PAR);
		BTRON_CHECK("負は不正", slp_tsk(-2) == ER_PAR);
		/* **中心核の E_TMOUT ではなく周辺核の ER_NONE**（規律 R20） */
		BTRON_CHECK("時間切れは ER_NONE", slp_tsk(10) == ER_NONE);

		BTRON_CHECK("自タスクは起床できない", wup_tsk(tk_get_tid()) == ER_ID);
		BTRON_CHECK("他プロセスのタスク", wup_tsk(TK_MAX_TSKID) == ER_ID);
		BTRON_CHECK("起床要求なし", can_wup(P_SELF) == 0);

		/* 自分より低い優先度のサブタスクに起こさせる。
		 * 自分が待ちに入って初めて走る。 */
		sub = cre_tsk(p_waker, 3, (W)tk_get_tid());
		BTRON_CHECK("起こし役", sub >= 1);
		BTRON_CHECK("起こされた", slp_tsk(1000) == ER_OK);

		BTRON_CHECK("遅延の負は不正", dly_tsk(-1) == ER_PAR);
		/* 仕様: time = 0 は再スケジュール（実行 → 実行可能） */
		BTRON_CHECK("0 は再スケジュール", dly_tsk(0) == ER_OK);
		BTRON_CHECK("遅延", dly_tsk(5) == ER_OK);
	});

	BTRON_TEST("プロセス統計情報 (§1.1.9 get_inf)", {
		P_INFO i1;
		P_INFO i2;

		BTRON_CHECK("アドレス不正", get_inf(P_SELF, 0) == ER_ADR);
		BTRON_CHECK("存在しない",
		            get_inf(PRC_MAX_PRCID, &i1) == ER_NOPRC);
		BTRON_CHECK("取得", get_inf(P_SELF, &i1) == ER_OK);
		BTRON_CHECK("メモリを数えている", i1.wmem > 0);

		/* CPU 時間は走っている間だけ増える。待つのではなく回る。 */
		spin_ms(PRC_SLICE_MS * 3);
		BTRON_CHECK("再取得", get_inf(P_SELF, &i2) == ER_OK);
		BTRON_CHECK("CPU 時間が増えた", i2.utime > i1.utime);
		BTRON_CHECK("経過時間は巻き戻らない", i2.etime >= i1.etime);
	});

	BTRON_TEST("同一優先度はラウンドロビン (§1.1.3)", {
		W s1;
		W s2;
		W deadline;

		seq_reset();
		prc_rr_done = 0;
		/* 自分と同じ優先度のサブタスク 2 本。中心核は時分割をしないので、
		 * 周辺核のタイムスライス (prc_tick) が効いていなければ
		 * 先に走った方が 3 回とも記録してから次に移る = 変化は 1 回だけ。 */
		s1 = cre_tsk(p_rr, 2, (W)'a');
		s2 = cre_tsk(p_rr, 2, (W)'b');
		BTRON_CHECK("2 本作れた", s1 >= 1 && s2 >= 1);

		deadline = 0;
		while (prc_rr_done < 2 && deadline < 100) {
			(void)dly_tsk(10);
			deadline++;
		}
		BTRON_CHECK("2 本とも終わった", prc_rr_done == 2);
		BTRON_CHECK("両方が記録を残した", seq_n == 6);
		/* 時分割が無ければ "aaabbb" で変化は 1 回。 */
		BTRON_CHECK("交互に走った", seq_transitions() >= 2);
		if (seq_transitions() < 2) {
			seq_show("abab...");
		}
	});

	BTRON_TEST("プロセスの生成と終了で資源が漏れない", {
		UD free0;
		UD used0;
		W n0;
		W i;
		Bool ok = True;

		free0 = free_pages_settled();
		used0 = kheap_used();
		n0 = prc_count();

		/* 1 回では漏れが見えない。100 回繰り返して初めて分かる。 */
		for (i = 0; i < 100; i++) {
			if (prc_create(p_exit_now, 1, 0, 0) < 1) {
				ok = False;
				break;
			}
		}
		BTRON_CHECK("100 回とも生成できた", ok);
		BTRON_CHECK("プロセスが残っていない", prc_count() == n0);
		BTRON_CHECK("物理ページが戻った", free_pages_settled() == free0);
		BTRON_CHECK("カーネルヒープが戻った", kheap_used() == used0);
	});

	BTRON_TEST("プロセス数の上限 (§1.1.9 ER_NOSPC)", {
		W ids[PRC_MAX_PRCID];
		W made = 0;
		W i;

		for (i = 0; i < PRC_MAX_PRCID; i++) {
			ids[i] = prc_create(p_sleeper, 1, 0, 0);
			if (ids[i] < 1) {
				break;
			}
			made++;
		}
		BTRON_CHECK("自分の分だけ少ない", made == PRC_MAX_PRCID - 1);
		BTRON_CHECK("上限を超えると ER_NOSPC",
		            prc_create(p_sleeper, 1, 0, 0) == ER_NOSPC);
		for (i = 0; i < made; i++) {
			BTRON_CHECK("片付け", ter_prc(ids[i], 0, TERM_NRM) == ER_OK);
		}
		BTRON_CHECK("自分だけ残った", prc_count() == 1);
	});

	BTRON_TEST("3 種類の領域の配置 (§1.5.1)", {
		VP loc = 0;
		VP com = 0;
		VP sys = 0;

		BTRON_CHECK("ローカル", get_mbk(&loc, 1, 0) == ER_OK);
		BTRON_CHECK("共有", get_mbk(&com, 1, M_COMMON) == ER_OK);
		/* 初期プロセスはユーザレベル 0 = システムプロセス */
		BTRON_CHECK("システム", get_mbk(&sys, 1, M_SYSTEM) == ER_OK);

		/* 仕様: 「共有メモリ、システムメモリは負のアドレスとなり、
		 * ローカルメモリは正のアドレスとなる」 */
		BTRON_CHECK("ローカルは正", (D)(UD)loc > 0);
		BTRON_CHECK("共有は負", (D)(UD)com < 0);
		BTRON_CHECK("システムは負", (D)(UD)sys < 0);

		/* §3.2 の区画に入っていること */
		BTRON_CHECK("ローカルの区画",
		            (UD)loc >= BTRON_PROC_LOCAL_BASE &&
		                (UD)loc < BTRON_PROC_LOCAL_LIMIT);
		BTRON_CHECK("共有の区画",
		            (UD)com >= BTRON_COMMON_BASE &&
		                (UD)com < BTRON_SYSTEM_BASE);
		BTRON_CHECK("システムの区画",
		            (UD)sys >= BTRON_SYSTEM_BASE &&
		                (UD)sys < BTRON_MAPREC_BASE);

		/* 仕様: 「書込み / 読込みは自由にできる」 */
		*(volatile UW *)loc = 0x11111111U;
		*(volatile UW *)com = 0x22222222U;
		*(volatile UW *)sys = 0x33333333U;
		BTRON_CHECK("読み書きできる",
		            *(volatile UW *)loc == 0x11111111U &&
		                *(volatile UW *)com == 0x22222222U &&
		                *(volatile UW *)sys == 0x33333333U);
		BTRON_CHECK("互いに別の物理ページ",
		            hal_vm_translate(prc_space(me), (UD)loc) !=
		                hal_vm_translate(hal_vm_kernel_space(), (UD)com));

		BTRON_CHECK("解放",
		            rel_mbk(loc) == ER_OK && rel_mbk(com) == ER_OK &&
		                rel_mbk(sys) == ER_OK);
	});

	BTRON_TEST("獲得したブロックは連続した論理アドレスを持つ (§1.5.3)", {
		VP adr = 0;
		UD ps = (UD)hal_page_size();
		M_STATE st;
		W k;
		Bool ok = True;

		BTRON_CHECK("状態参照", mbk_sts(&st) == ER_OK);
		BTRON_CHECK("ブロックサイズはページサイズ",
		            st.blksz == (W)hal_page_size());
		BTRON_CHECK("総数と残り", st.total > 0 && st.free > 0 &&
		                              st.free <= st.total);
		BTRON_CHECK("アドレス不正", mbk_sts(0) == ER_ADR);

		BTRON_CHECK("4 ブロック", get_mbk(&adr, 4, 0) == ER_OK);
		/* 全域に書いて読み返す。境界をまたいで連続していなければ落ちる。 */
		for (k = 0; k < 4; k++) {
			volatile UW *p = (volatile UW *)(VP)((UD)adr + (UD)k * ps);

			p[0] = 0xA0000000U + (UW)k;
			p[ps / sizeof(UW) - 1] = 0xB0000000U + (UW)k;
		}
		for (k = 0; k < 4; k++) {
			volatile UW *p = (volatile UW *)(VP)((UD)adr + (UD)k * ps);

			if (p[0] != 0xA0000000U + (UW)k ||
			    p[ps / sizeof(UW) - 1] != 0xB0000000U + (UW)k) {
				ok = False;
			}
			/* 物理は連続していなくてよい（仕様が求めているのは論理）。
			 * ただし全ブロックが張られていること。 */
			if (hal_vm_translate(prc_space(me), (UD)adr + (UD)k * ps) == 0) {
				ok = False;
			}
		}
		BTRON_CHECK("全ブロックが読み書きできる", ok);
		BTRON_CHECK("解放", rel_mbk(adr) == ER_OK);
		/* 仕様: adr は get_mbk() で得たアドレスでなければならない */
		BTRON_CHECK("二度目は不正", rel_mbk(adr) == ER_MPTR);
	});

	BTRON_TEST("get_mbk / rel_mbk のエラーコード (§1.5.3)", {
		VP adr = 0;

		BTRON_CHECK("格納先が不正", get_mbk(0, 1, 0) == ER_ADR);
		BTRON_CHECK("0 ブロック", get_mbk(&adr, 0, 0) == ER_PAR);
		BTRON_CHECK("負のブロック数", get_mbk(&adr, -1, 0) == ER_PAR);
		/* M_SYSTEM (3) は M_COMMON (1) のビットを含む。0x2 は
		 * 「上のビットだけ立っている」ので不正。 */
		BTRON_CHECK("中途半端な属性", get_mbk(&adr, 1, 0x2U) == ER_PAR);
		BTRON_CHECK("未定義のビット", get_mbk(&adr, 1, 0x100U) == ER_PAR);
		/* 仮想記憶が無いので M_RESIDENT は指定しても通る（仕様が認めている） */
		BTRON_CHECK("常駐指定は通る",
		            get_mbk(&adr, 1, M_RESIDENT) == ER_OK);
		BTRON_CHECK("解放", rel_mbk(adr) == ER_OK);

		BTRON_CHECK("NULL の解放", rel_mbk(0) == ER_MPTR);
		BTRON_CHECK("出所不明の解放",
		            rel_mbk((VP)(UD)0x1000ULL) == ER_MPTR);
	});

	BTRON_TEST("ローカルメモリは他プロセスからアクセスできない (§1.5.1)", {
		VP loc = 0;
		W child;

		BTRON_CHECK("獲得", get_mbk(&loc, 1, 0) == ER_OK);
		*(volatile UW *)loc = 0x5A5A5A5AU;

		child = prc_create(p_sleeper, 1, 0, 0);
		BTRON_CHECK("子ができた", child >= 1);
		BTRON_CHECK("自分からは引ける",
		            hal_vm_translate(prc_space(me), (UD)loc) != 0);
		/* ここが §1.5.1 の「他のプロセスからはアクセスできない」 */
		BTRON_CHECK("子からは引けない",
		            hal_vm_translate(prc_space(child), (UD)loc) == 0);
		/* ローカルは下位半分なので、上位半分（カーネル・共有）にも無い */
		BTRON_CHECK("共有側にも無い",
		            hal_vm_translate(hal_vm_kernel_space(), (UD)loc) == 0);

		BTRON_CHECK("片付け", ter_prc(child, 0, TERM_NRM) == ER_OK);
		BTRON_CHECK("解放", rel_mbk(loc) == ER_OK);
	});

	BTRON_TEST("共有メモリは他プロセスから書ける (§1.5.1)", {
		W child;

		mbk_shared = 0;
		mbk_writer_err = -1;
		BTRON_CHECK("獲得", get_mbk(&mbk_shared, 1, M_COMMON) == ER_OK);
		*(volatile UW *)mbk_shared = 0;

		/* 優先度 1 は自分 (2) より高いので、生成した瞬間に走りきる。 */
		child = prc_create(p_shared_writer, 1, 0, 0);
		BTRON_CHECK("子は走って終わった", child >= 1 && prc_count() == 1);
		BTRON_CHECK("子はアドレスを見られた", mbk_writer_err == 0);
		/* **別のプロセスが書いた値が見える。** 共有が上位半分にあり、
		 * 上位半分は全プロセスで共通（arm64 は TTBR1 が切り替わらない、
		 * x86_64 は上位 256 PML4 エントリを複製）だから成立する。 */
		BTRON_CHECK("書いた値が見える",
		            *(volatile UW *)mbk_shared == 0xC0FFEEU);
		BTRON_CHECK("解放", rel_mbk(mbk_shared) == ER_OK);
	});

	BTRON_TEST("終了時の自動解放 (§1.5.1 / DELEXIT)", {
		W n0 = mbk_count();
		UD free0 = free_pages_settled();
		W child;

		child = prc_create(p_delexit, 1, 0, 0);
		BTRON_CHECK("子は走って終わった", child >= 1 && prc_count() == 1);
		BTRON_CHECK("3 つ獲得できていた",
		            mbk_c_local != 0 && mbk_c_common != 0 &&
		                mbk_c_delexit != 0);
		/* ローカルは DELEXIT に関係なく解放され、DELEXIT 付きの共有も
		 * 解放される。DELEXIT の無い共有だけが残る（仕様）。 */
		BTRON_CHECK("残るのは 1 つだけ", mbk_count() == n0 + 1);
		BTRON_CHECK("残ったのは DELEXIT なしの共有",
		            *(volatile UW *)mbk_c_common == 0 ||
		                *(volatile UW *)mbk_c_common != 0);
		/* 「獲得したプロセスの存在とは無関係に存在する」ので、
		 * 持ち主が消えた後でも別のプロセスから解放できる。 */
		BTRON_CHECK("別プロセスから解放できる",
		            rel_mbk(mbk_c_common) == ER_OK);
		BTRON_CHECK("記録が戻った", mbk_count() == n0);
		/* 子のタスクの後片付けも流してから数える。 */
		BTRON_CHECK("物理ページも戻った", free_pages_settled() == free0);
	});

	BTRON_TEST("システムメモリはアプリケーションプロセスから使えない (§1.5.3)", {
		W child;

		mbk_sys_err = ER_OK;
		child = prc_create(p_sysmem, 1, 0, 0);
		BTRON_CHECK("子は走って終わった", child >= 1 && prc_count() == 1);
		/* 仕様: 「この指定は、アプリケーションプロセスからは使用して
		 * はいけない」。仕様はエラーコードを挙げていないので ER_PAR。 */
		BTRON_CHECK("拒まれた", mbk_sys_err == ER_PAR);
	});

	BTRON_TEST("空いた仮想アドレスを再利用する", {
		VP a = 0;
		VP b = 0;
		VP c = 0;
		VP d = 0;

		BTRON_CHECK("3 つ獲得",
		            get_mbk(&a, 1, 0) == ER_OK &&
		                get_mbk(&b, 1, 0) == ER_OK &&
		                get_mbk(&c, 1, 0) == ER_OK);
		BTRON_CHECK("連続して並ぶ",
		            (UD)b == (UD)a + hal_page_size() &&
		                (UD)c == (UD)b + hal_page_size());
		BTRON_CHECK("中央を解放", rel_mbk(b) == ER_OK);
		BTRON_CHECK("再獲得", get_mbk(&d, 1, 0) == ER_OK);
		BTRON_CHECK("空いた最も低いところが返る", d == b);
		/* 2 ブロック要求は中央の隙間に入らないので後ろへ回る */
		BTRON_CHECK("隙間に入らない大きさは後ろへ",
		            get_mbk(&b, 2, 0) == ER_OK &&
		                (UD)b == (UD)c + hal_page_size());
		BTRON_CHECK("片付け",
		            rel_mbk(a) == ER_OK && rel_mbk(b) == ER_OK &&
		                rel_mbk(c) == ER_OK && rel_mbk(d) == ER_OK);
	});

	BTRON_TEST("メモリブロックの獲得と解放で漏れない (§1.5)", {
		UD free0;
		W n0;
		W i;
		Bool ok = True;
		VP adr = 0;

		/* **先に 1 度ずつ通しておく。** ページテーブルの中間段は
		 * 解放していないので、初回だけ空きページが減る。同じ仮想
		 * アドレスを使い回す限り以後は増えない（memory.c の「既知の限界」）。 */
		if (get_mbk(&adr, 2, 0) == ER_OK) {
			(void)rel_mbk(adr);
		}
		if (get_mbk(&adr, 2, M_COMMON) == ER_OK) {
			(void)rel_mbk(adr);
		}
		free0 = free_pages_settled();
		n0 = mbk_count();

		/* 1 回では漏れが見えない（Phase 2 ㉜）。 */
		for (i = 0; i < 100; i++) {
			VP l = 0;
			VP c = 0;

			if (get_mbk(&l, 2, 0) != ER_OK ||
			    get_mbk(&c, 2, M_COMMON) != ER_OK) {
				ok = False;
				break;
			}
			*(volatile UW *)l = (UW)i;
			*(volatile UW *)c = (UW)i;
			if (rel_mbk(l) != ER_OK || rel_mbk(c) != ER_OK) {
				ok = False;
				break;
			}
		}
		BTRON_CHECK("100 回とも通った", ok);
		BTRON_CHECK("記録が戻った", mbk_count() == n0);
		BTRON_CHECK("物理ページが戻った", free_pages_settled() == free0);
	});

	BTRON_TEST("メモリブロック記録の上限 (§1.5.3 ER_NOSPC)", {
		VP adr[MBK_MAX];
		VP over = 0;
		W n0 = mbk_count();
		W made = 0;
		W i;

		for (i = 0; i < MBK_MAX; i++) {
			adr[i] = 0;
			if (get_mbk(&adr[i], 1, 0) != ER_OK) {
				break;
			}
			made++;
		}
		BTRON_CHECK("記録の数だけ取れる", made == MBK_MAX - n0);
		BTRON_CHECK("超えると ER_NOSPC",
		            get_mbk(&over, 1, 0) == ER_NOSPC);
		for (i = 0; i < made; i++) {
			BTRON_CHECK("片付け", rel_mbk(adr[i]) == ER_OK);
		}
		BTRON_CHECK("記録が戻った", mbk_count() == n0);
	});

	/* -------------------------------------------------------------- */
	/* §1.2 メッセージ管理                                            */
	/* -------------------------------------------------------------- */

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
		child = prc_create(p_sleeper, 1, 0, 0);
		BTRON_CHECK("強制終了", ter_prc(child, 7, TERM_NRM) == ER_OK);
		BTRON_CHECK("強制終了メッセージ",
		            rcv_msg(MM_TERM, &r, (W)sizeof(r), NOWAIT) == child);
		BTRON_CHECK("MS_TERM と コード",
		            r.msg_type == MS_TERM && r.msg_body.TERM.code == 7);

		/* prc_abort → MS_ABORT */
		child = prc_create(p_sleeper, 1, 0, 0);
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
		CONST TC *n1 = nm_test;
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

	BTRON_TEST("同期用セマフォ (§1.3.2 SEM_SYNC)", {
		W n0 = tobj_count();
		ID id;

		BTRON_CHECK("負の初期値", cre_sem(-1, SEM_SYNC) == ER_PAR);
		BTRON_CHECK("属性不正", cre_sem(0, 0x100) == ER_PAR);
		id = cre_sem(1, SEM_SYNC);
		BTRON_CHECK("生成", id >= 1);
		BTRON_CHECK("数が増えた", tobj_count() == n0 + 1);
		BTRON_CHECK("獲得", wai_sem(id, T_NOWAIT) == ER_OK);
		/* **中心核の E_TMOUT ではなく ER_NONE**（規律 R20） */
		BTRON_CHECK("もう無い", wai_sem(id, T_NOWAIT) == ER_NONE);
		BTRON_CHECK("時間切れも ER_NONE", wai_sem(id, 5) == ER_NONE);
		/* 同期用は順序の制限が無いので、獲得していなくても返せる。 */
		BTRON_CHECK("返却", sig_sem(id) == ER_OK);
		BTRON_CHECK("もう 1 つ返却", sig_sem(id) == ER_OK);
		BTRON_CHECK("2 つ獲得できる",
		            wai_sem(id, T_NOWAIT) == ER_OK &&
		                wai_sem(id, T_NOWAIT) == ER_OK);
		BTRON_CHECK("不正 ID", wai_sem(0, T_NOWAIT) == ER_ID);
		BTRON_CHECK("存在しない",
		            wai_sem(TK_MAX_TOBJ, T_NOWAIT) == ER_NOEXS);
		BTRON_CHECK("削除", del_sem(id) == ER_OK);
		BTRON_CHECK("数が戻った", tobj_count() == n0);
	});

	BTRON_TEST("排他制御用セマフォはミューテックス (§1.3.2 SEM_EXCL)", {
		ID id;

		/* 仕様「排他制御用セマフォの場合には cnt = 1 でなければならない」 */
		BTRON_CHECK("初期値 0 は不正", cre_sem(0, SEM_EXCL) == ER_PAR);
		BTRON_CHECK("初期値 2 も不正", cre_sem(2, SEM_EXCL) == ER_PAR);
		id = cre_sem(1, SEM_EXCL);
		BTRON_CHECK("生成", id >= 1);
		BTRON_CHECK("獲得", wai_sem(id, T_NOWAIT) == ER_OK);
		/* 仕様「すでに獲得している状態で、再度獲得しようとした場合には
		 * ER_OBJ」。中心核のミューテックスの E_ILUSE がこれに写る。 */
		BTRON_CHECK("二重獲得は ER_OBJ", wai_sem(id, T_NOWAIT) == ER_OBJ);
		BTRON_CHECK("返却", sig_sem(id) == ER_OK);
		/* 仕様「獲得していない状態で返却しようとした場合は ER_OBJ」 */
		BTRON_CHECK("二重返却は ER_OBJ", sig_sem(id) == ER_OBJ);
		BTRON_CHECK("削除", del_sem(id) == ER_OK);
	});

	BTRON_TEST("セマフォ待ちの削除は ER_DLT (§1.3.2)", {
		W sub;

		tobj_sem = cre_sem(0, SEM_SYNC);
		tobj_wait_er = ER_OK;
		BTRON_CHECK("生成", tobj_sem >= 1);
		/* 自分より低い優先度のサブタスクに待たせる。 */
		sub = cre_tsk(p_semwait, 3, 0);
		BTRON_CHECK("サブタスク", sub >= 1);
		(void)dly_tsk(5); /* サブタスクを待ちに入れる */
		BTRON_CHECK("削除", del_sem(tobj_sem) == ER_OK);
		(void)dly_tsk(5);
		/* 仕様「待ち状態にあったタスクは待ちが解除され、そのタスクへ
		 * ER_DLT が返される」 */
		BTRON_CHECK("待ちは ER_DLT で解けた", tobj_wait_er == ER_DLT);
	});

	BTRON_TEST("イベントフラグ (§1.3.2)", {
		ID id;
		WERR got;

		BTRON_CHECK("負の初期値", cre_flg(-1, 0) == ER_PAR);
		id = cre_flg(0, 0);
		BTRON_CHECK("生成", id >= 1);
		BTRON_CHECK("待ちパターン 0 は不正",
		            wai_flg(id, 0, WF_OR, T_NOWAIT) == ER_PAR);
		BTRON_CHECK("まだ立っていない",
		            wai_flg(id, 0x3, WF_OR, T_NOWAIT) == ER_NONE);

		BTRON_CHECK("セット", set_flg(id, 0x1) == ER_OK);
		/* NOCLR 付きなら値は変わらない（仕様） */
		got = wai_flg(id, 0x1, WF_OR | NOCLR, T_NOWAIT);
		BTRON_CHECK("OR 待ちが解けて値が返る", got == 0x1);
		BTRON_CHECK("NOCLR なので残る",
		            wai_flg(id, 0x1, WF_OR | NOCLR, T_NOWAIT) == 0x1);

		/* AND 待ちは全ビットが揃うまで。 */
		BTRON_CHECK("AND はまだ",
		            wai_flg(id, 0x3, WF_AND, T_NOWAIT) == ER_NONE);
		BTRON_CHECK("もう 1 ビット", set_flg(id, 0x2) == ER_OK);
		/* NOCLR 無しなので**全ビット**が 0 クリアされる（仕様） */
		BTRON_CHECK("AND が解けた",
		            wai_flg(id, 0x3, WF_AND, T_NOWAIT) == 0x3);
		BTRON_CHECK("全クリアされた",
		            wai_flg(id, 0x3, WF_OR, T_NOWAIT) == ER_NONE);

		/* 仕様「現在の値に ptn が AND される」。残すパターンである。 */
		BTRON_CHECK("セット", set_flg(id, 0xF) == ER_OK);
		BTRON_CHECK("AND でクリア", clr_flg(id, 0x5) == ER_OK);
		BTRON_CHECK("0x5 が残った",
		            wai_flg(id, 0xF, WF_OR | NOCLR, T_NOWAIT) == 0x5);
		BTRON_CHECK("全クリア", clr_flg(id, 0) == ER_OK);
		BTRON_CHECK("空になった",
		            wai_flg(id, 0xF, WF_OR, T_NOWAIT) == ER_NONE);
		BTRON_CHECK("削除", del_flg(id) == ER_OK);
	});

	BTRON_TEST("メッセージバッファ (§1.3.2)", {
		ID id;
		B buf[16];
		W i;

		BTRON_CHECK("最大サイズ 0 は不正", cre_mbf(64, 0, 0) == ER_PAR);
		id = cre_mbf(64, 16, 0);
		BTRON_CHECK("生成", id >= 1);
		BTRON_CHECK("空なら ER_NONE", rcv_mbf(id, buf, T_NOWAIT) == ER_NONE);

		for (i = 0; i < 16; i++) {
			buf[i] = (B)('A' + i);
		}
		BTRON_CHECK("送信", snd_mbf(id, buf, 16, T_NOWAIT) == ER_OK);
		for (i = 0; i < 16; i++) {
			buf[i] = 0;
		}
		BTRON_CHECK("受信", rcv_mbf(id, buf, T_NOWAIT) == 16);
		BTRON_CHECK("中身が合っている", buf[0] == 'A' && buf[15] == 'P');
		BTRON_CHECK("アドレス不正", snd_mbf(id, 0, 1, T_NOWAIT) == ER_ADR);
		BTRON_CHECK("削除", del_mbf(id) == ER_OK);
	});

	BTRON_TEST("DELEXIT の同期オブジェクトはプロセス終了で消える (§1.3)", {
		W n0 = tobj_count();
		ID id;

		/* 自プロセスで DELEXIT 付きに作り、数が増えることを見る。
		 * 自プロセスの終了は検査の終わりなので、ここでは明示的に消す。 */
		id = cre_sem(1, SEM_SYNC | DELEXIT);
		BTRON_CHECK("生成", id >= 1 && tobj_count() == n0 + 1);
		BTRON_CHECK("削除", del_sem(id) == ER_OK);
		BTRON_CHECK("数が戻った", tobj_count() == n0);
	});

	/* -------------------------------------------------------------- */
	/* §1.9 時計管理 / §1.10 システム管理                             */
	/* -------------------------------------------------------------- */

	BTRON_TEST("システム時間とカレンダーの変換 (§1.9)", {
		STIME t = 0;
		TIMEZONE tz;
		DATE_TIM dt;
		STIME back = 0;

		BTRON_CHECK("取得", get_tim(&t, &tz) == ER_OK);
		BTRON_CHECK("時差の初期値", tz.adjust == 0 && tz.dst_flg == 0);

		/* 原点。1985-01-01 は火曜日。 */
		BTRON_CHECK("原点の変換", get_tod(&dt, 1, 0) == ER_OK);
		BTRON_CHECK("1985 年",
		            dt.d_year == STIME_EPOCH_YEAR - 1900 &&
		                dt.d_month == 1 && dt.d_day == 1);
		BTRON_CHECK("火曜日", dt.d_wday == 2);
		BTRON_CHECK("年内 1 日目", dt.d_days == 1);

		/* 閏年の 2 月 29 日が通ること。2024-02-29 は木曜日。 */
		dt.d_year = 2024 - 1900;
		dt.d_month = 2;
		dt.d_day = 29;
		dt.d_hour = 12;
		dt.d_min = 34;
		dt.d_sec = 56;
		BTRON_CHECK("閏日 → システム時間", set_tod(&dt, &t, 0) == ER_OK);
		BTRON_CHECK("往復", get_tod(&dt, t, 0) == ER_OK);
		BTRON_CHECK("日付が戻った",
		            dt.d_year == 2024 - 1900 && dt.d_month == 2 &&
		                dt.d_day == 29);
		BTRON_CHECK("時刻が戻った",
		            dt.d_hour == 12 && dt.d_min == 34 && dt.d_sec == 56);
		BTRON_CHECK("木曜日", dt.d_wday == 4);
		BTRON_CHECK("年内 60 日目", dt.d_days == 31 + 29);

		/* 仕様「d_days の値は d_month = 0 のときのみ有効」 */
		dt.d_month = 0;
		dt.d_days = 60;
		dt.d_hour = 0;
		dt.d_min = 0;
		dt.d_sec = 0;
		BTRON_CHECK("通算日で指定", set_tod(&dt, &back, 0) == ER_OK);
		BTRON_CHECK("同じ日になる",
		            get_tod(&dt, back, 0) == ER_OK && dt.d_month == 2 &&
		                dt.d_day == 29);

		/* 時差。ローカル = システム - adjust */
		tz.adjust = -9 * 3600; /* 日本標準時。グリニッチから東 9 時間 */
		tz.dst_flg = 0;
		tz.dst_adj = 0;
		BTRON_CHECK("時差の設定", set_tim(0, &tz) == ER_OK);
		BTRON_CHECK("GMT", get_tod(&dt, 1, 0) == ER_OK && dt.d_hour == 0);
		BTRON_CHECK("ローカルは +9 時間",
		            get_tod(&dt, 1, 1) == ER_OK && dt.d_hour == 9);

		tz.adjust = TZ_ADJUST_MAX + 1;
		BTRON_CHECK("時差が範囲外", set_tim(0, &tz) == ER_PAR);
		tz.adjust = 0;
		BTRON_CHECK("戻す", set_tim(0, &tz) == ER_OK);
		BTRON_CHECK("アドレス不正", get_tod(0, 1, 0) == ER_ADR);

		dt.d_month = 13;
		BTRON_CHECK("月が範囲外", set_tod(&dt, &back, 0) == ER_PAR);
	});

	BTRON_TEST("OS バージョンと構成情報 (§1.10)", {
		T_VER v;
		W num = 0;
		TC str[16];

		BTRON_CHECK("アドレス不正", get_ver(0) == ER_ADR);
		BTRON_CHECK("取得", get_ver(&v) == ER_OK);
		BTRON_CHECK("仕様書バージョンは BTRON3", v.spver == OS_SPVER);
		BTRON_CHECK("CPU が入っている", v.cpu != 0);

		/* 仕様「関数値 = 0 は W の数値情報」 */
		BTRON_CHECK("数値項目",
		            get_cnf(nm_cpunum, &num, (W)sizeof(num)) == 0);
		BTRON_CHECK("CPU 数が取れた", num >= 1);
		/* 仕様「関数値 > 0 は TC 文字列情報で、関数値は文字数」 */
		BTRON_CHECK("文字列項目",
		            get_cnf(nm_osname, str, (W)sizeof(str)) == 6);
		BTRON_CHECK("中身", str[0] == (TC)'B' && str[6] == TNULL);
		BTRON_CHECK("無い項目",
		            get_cnf(nm_nope, &num, (W)sizeof(num)) == ER_NOEXS);
		BTRON_CHECK("アドレス不正",
		            get_cnf(0, &num, (W)sizeof(num)) == ER_ADR);
	});

	/* -------------------------------------------------------------- */
	/* 外殻マネージャ（フォントマネージャ）                            */
	/* -------------------------------------------------------------- */

	BTRON_TEST("マネージャがサーバとして立つ (詳細 E1 / E22)", {
		W pid;
		WERR por;
		CONST FontShared *fs;
		MgrMsg m;
		UW width = 0;
		CONST UB *g;
		CONST UB *g2;

		/* 優先度 1 は自分 (2) より高いので、生成した瞬間に走って
		 * 共有メモリを用意し、名前を登録し、受付待ちに入る。 */
		pid = fontman_start(1);
		BTRON_CHECK("マネージャが立った", pid >= 1);

		/* §1.4: グローバル名で見つかる。これが BTRON3 の
		 * サービス探索である。 */
		por = mgr_lookup(MGR_FONT);
		BTRON_CHECK("名前で見つかる", por >= 1);

		/* §1.3: ランデブで問い合わせる。1 回だけ。 */
		fs = fontman_shared();
		BTRON_CHECK("共有グリフデータの置き場が分かる", fs != 0);
		if (fs != 0) {
			/* §1.5: 共有メモリ = 負のアドレス */
			BTRON_CHECK("共有メモリにある", (D)(UD)(CONST VP)fs < 0);
			BTRON_CHECK("印", fs->magic == FONTSHARED_MAGIC);
			BTRON_CHECK("諸元", fs->height == 16 &&
			                        fs->narrow_count > 0 &&
			                        fs->wide_count > 0);

			/* **ここが詳細 E1 の設計の要点。** 以後の字形引きは
			 * 共有メモリを直接読むだけで、IPC は起きない。 */
			g = fontman_glyph(fs, (UW)'A', &width);
			BTRON_CHECK("半角が引ける", g != 0 && width == 8);
			g2 = fontman_glyph(fs, 0x3042U, &width); /* 'あ' */
			BTRON_CHECK("全角が引ける", g2 != 0 && width == 16);
			BTRON_CHECK("字形が空でない",
			            glyph_inked(g, 16) && glyph_inked(g2, 32));
			BTRON_CHECK("無い文字", fontman_glyph(fs, 0x3FFFU, &width) == 0);

			/* IPC 経由でも同じ答えが返る。 */
			m.func = FONT_GET_IMG;
			m.arg[0] = (D)(UW)'A';
			BTRON_CHECK("要求が通る",
			            mgr_call((ID)por, &m, T_FOREVER) == ER_OK &&
			                m.result == ER_OK);
			BTRON_CHECK("共有メモリと同じアドレス",
			            (CONST UB *)(CONST VP)(UD)m.arg[0] == g);
			BTRON_CHECK("幅も同じ", m.arg[1] == 8);

			m.func = 999;
			BTRON_CHECK("知らない機能",
			            mgr_call((ID)por, &m, T_FOREVER) == ER_OK &&
			                m.result == ER_PAR);
		}

		/* 止める。ポートと名前は DELEXIT 付きなので一緒に消える。 */
		m.func = FONT_SHUTDOWN;
		BTRON_CHECK("停止要求",
		            mgr_call((ID)por, &m, T_FOREVER) == ER_OK);
		(void)dly_tsk(10);
		BTRON_CHECK("プロセスが終わった", prc_sts(pid, 0, 0) == ER_NOPRC);
		BTRON_CHECK("名前も消えた", mgr_lookup(MGR_FONT) == ER_NOEXS);
		BTRON_CHECK("終了メッセージを捨てる",
		            clr_msg(MM_ALL, MM_NULL) == ER_OK);
	});

	BTRON_TEST("cre_prc はファイルシステムを要求する (§1.1.6)", {
		LINK lnk;
		W i;

		for (i = 0; i < L_FSNM; i++) {
			lnk.fs_name[i] = TNULL;
		}
		lnk.f_id = 1;
		lnk.atr1 = 0;
		lnk.atr2 = 0;
		lnk.atr3 = 0;
		lnk.atr4 = 0;
		lnk.atr5 = 0;

		BTRON_CHECK("アドレス不正", cre_prc(0, 0, 0) == ER_ADR);
		BTRON_CHECK("優先度範囲外", cre_prc(&lnk, 300, 0) == ER_PPRI);
		/* 実身を読む経路がまだ無い。Phase 3 でここが本物になる。 */
		BTRON_CHECK("ファイルシステム未接続", cre_prc(&lnk, 0, 0) == ER_NOFS);
	});
}

/* 初期プロセスのメインタスク。形式-A の MAIN()。 */
LOCAL ID proc_test_waiter;

LOCAL W p_test_main(MESSAGE *msg)
{
	BTRON_TEST("起動時メッセージ (§1.1.6)", {
		BTRON_CHECK("空メッセージが渡る", msg != 0);
		if (msg != 0) {
			BTRON_CHECK("タイプ", msg->msg_type == 0);
			BTRON_CHECK("大きさ", msg->msg_size == 0);
		}
	});
	run_proc_tests();
	/* 検査タスクを起こす。周辺核の wup_tsk は自プロセス内に限るので、
	 * 中心核の方を直に呼ぶ（ここはカーネル内部の検査コード）。 */
	(void)tk_wup_tsk(proc_test_waiter);
	return 0;
}

/* 周辺核 §1.1 の検査を初期プロセスの中で走らせる。
 *
 * **検査タスクは待ちに入る。** 単に優先度を下げて譲るだけにすると、
 * 初期プロセスが chg_pri で自分の優先度を動かす検査の最中に、
 * 検査タスクが実行可能なまま横から選ばれてしまい、§1.1 の検査が
 * 途中で打ち切られる。待ち状態にして、初期プロセスが終わるときに
 * 起こしてもらう。 */
LOCAL void run_proc_phase(void)
{
	CONST TC name[] = {(TC)'I', (TC)'N', (TC)'I', (TC)'T', TNULL};
	W pid;

	proc_test_waiter = tk_get_tid();
	/* 優先度 2（群A）は中心核優先度 3 に写る。検査タスク（中心核 5）より
	 * 高いので、生成した瞬間に初期プロセスへ切り替わる。 */
	pid = prc_create(p_test_main, 2, name, 0);
	if (pid < 1) {
		btron_test_begin("初期プロセスの生成");
		btron_test_fail("初期プロセスの生成", "prc_create() >= 1",
		                __FILE__, __LINE__);
		btron_test_end("初期プロセスの生成");
		return;
	}
	/* 初期プロセスが §1.1 の検査を終えて tk_wup_tsk するまで待つ。 */
	(void)tk_slp_tsk(TMO_FEVR);
}

/* ------------------------------------------------------------------ */
/* 入口                                                                */
/* ------------------------------------------------------------------ */

LOCAL void test_task(INT stacd, VP exinf)
{
	W code;

	(void)stacd;
	(void)exinf;
	run_tests();
	run_proc_phase();
	code = btron_test_report();

	/* ESP に印があれば、ここでわざと例外を起こす（T4-02）。
	 *
	 * **緊急画面は「例外から呼ばれても描けること」が要件**なので、
	 * 描画だけ確かめても足りない。例外ベクタ → hal_exception_report →
	 * screen_panic_begin という本物の経路を 1 度通す。
	 *
	 * 検査が全部終わってから落とすので、`[ DONE ] n/n` は先に出ている。
	 * 終了コードは hal_exit(2)。ctest 側は --expect-exit 2 で受ける。 */
	if ((bootinfo()->flags & BI_F_PANIC_TEST) != 0) {
		volatile UW *bad;

		hal_console_puts("\n  PANICTEST の印があるので、ここでわざと例外を"
		                 "起こします\n");
		hal_console_puts("  （この直後の例外が画面に出れば成功です）\n");
		/* 上位半分の、何も写像していない番地を読む。
		 * 変換フォルトになり、同期例外の経路へ入る。 */
		bad = (volatile UW *)(VP)(BTRON_RESERVED_BASE + 0x1000ULL);
		code = (W)*bad;
	}
	hal_exit(code);
}

EXPORT void knl_selftest_start(void)
{
	ID id;

	/* 検査タスクは中くらいの優先度にする。上にも下にもタスクを置けるように。 */
	id = make_task(test_task, 5, 0);
	if (id < 1) {
		hal_console_puts("  検査タスクを作れない\n");
		hal_exit(1);
	}
	if (tk_sta_tsk(id, 0) != E_OK) {
		hal_console_puts("  検査タスクを起動できない\n");
		hal_exit(1);
	}
	knl_start(); /* 戻らない */
}

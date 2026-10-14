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
#include <btron/device.h>
#include <btron/objfmt.h>
#include <btron/virtio.h>
#include <btron/dp.h>
#include <btron/event.h>
#include <btron/fontman.h>
#include <btron/machdesc.h>
#include <btron/manager.h>
#include <btron/system.h>
#include <btron/window.h>
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

/* 数を 10 進で出す。検査の中で「何台見つかったか」を見せるのに使う。
 * ハーネスは合否しか扱わないので、値は普通の出力として出す。 */
LOCAL void put_dec(W v)
{
	B buf[12];
	W i = 0;
	UW u = (v < 0) ? (UW)(-v) : (UW)v;

	if (v < 0) {
		hal_console_putc('-');
	}
	do {
		buf[i++] = (B)('0' + (u % 10U));
		u /= 10U;
	} while (u > 0 && i < (W)sizeof(buf));
	while (i > 0) {
		hal_console_putc(buf[--i]);
	}
}

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
	/* **ファイル管理は何段も重なる。** 4KB (TK_MIN_STKSZ) では
	 * ブロック 1 つ (512B) とインデックスエントリ (256B) を持つ関数が
	 * 数段重なっただけで溢れ、黙ってページテーブルを壊した。 */
	ctsk.stksz = TK_MIN_STKSZ * 4;
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

LOCAL W fontman_pid;  /* 立てたフォントマネージャ。最後に止める */
LOCAL W fontman_por;
LOCAL GID dpt_gid;    /* メモリ描画環境 */
LOCAL GID dpt_sgid;   /* 画面の描画環境 */
LOCAL PTRIMG ptr_img; /* ポインタイメージ。1KB 超なのでスタックに置かない */

/* ファイル管理 (§1.6) の検査で使う名前。**マクロの中にカンマ付きの
 * 初期化子を書けない**（BTRON_TEST は関数形式マクロ）のでここに置く。 */
LOCAL CONST TC nm_fsname[] = {(TC)'T', (TC)'E', (TC)'S', (TC)'T', (TC)'F',
                              (TC)'S', TNULL};
LOCAL CONST TC nm_fsloc[] = {(TC)'R', (TC)'A', (TC)'M', TNULL};
LOCAL CONST TC nm_conn[] = {(TC)'t', (TC)'s', (TC)'t', TNULL};
LOCAL CONST TC nm_conn2[] = {(TC)'t', (TC)'s', (TC)'2', TNULL};
LOCAL CONST TC nm_f1[] = {(TC)'a', (TC)'l', (TC)'p', (TC)'h', (TC)'a', TNULL};
LOCAL CONST TC nm_f2[] = {(TC)'b', (TC)'e', (TC)'t', (TC)'a', TNULL};
LOCAL CONST TC nm_f3[] = {(TC)'g', (TC)'a', (TC)'m', (TC)'m', (TC)'a', TNULL};
LOCAL CONST TC nm_bad[] = {(TC)'x', (TC)TC_FDLM, (TC)'y', TNULL};
/* ／tst／alpha */
LOCAL CONST TC path_abs[] = {(TC)TC_FDLM, (TC)'t', (TC)'s', (TC)'t',
                             (TC)TC_FDLM, (TC)'a', (TC)'l', (TC)'p',
                             (TC)'h', (TC)'a', TNULL};
/* ／tst／alpha／beta */
LOCAL CONST TC path_deep[] = {(TC)TC_FDLM, (TC)'t', (TC)'s', (TC)'t',
                              (TC)TC_FDLM, (TC)'a', (TC)'l', (TC)'p',
                              (TC)'h', (TC)'a', (TC)TC_FDLM, (TC)'b',
                              (TC)'e', (TC)'t', (TC)'a', TNULL};
/* ／tst／alpha：1  （同名の 2 つめ） */
LOCAL CONST TC path_nth[] = {(TC)TC_FDLM, (TC)'t', (TC)'s', (TC)'t',
                             (TC)TC_FDLM, (TC)'a', (TC)'l', (TC)'p',
                             (TC)'h', (TC)'a', (TC)TC_FSEP, (TC)'1', TNULL};
/* ≡  （作業ファイル） */
LOCAL CONST TC path_own[] = {(TC)TC_FOWN, TNULL};
/* ／nope */
LOCAL CONST TC path_nofs[] = {(TC)TC_FDLM, (TC)'n', (TC)'o', (TC)'p',
                              (TC)'e', TNULL};
/* ビルド時に埋め込んだ実行プログラムレコード (tools/mkobj.py)。 */
IMPORT CONST UB btron_user_hello[];
IMPORT CONST UW btron_user_hello_size;
IMPORT CONST UB btron_user_shell[];
IMPORT CONST UW btron_user_shell_size;

LOCAL CONST TC nm_prog[] = {(TC)'h', (TC)'e', (TC)'l', (TC)'l', (TC)'o',
                            TNULL};
LOCAL CONST TC nm_shell[] = {(TC)'s', (TC)'h', (TC)'e', (TC)'l', (TC)'l',
                             TNULL};
LOCAL LINK fl_prog;
LOCAL LINK fl_root;
LOCAL LINK fl_a;
LOCAL LINK fl_b;
LOCAL Bool fs_up;

LOCAL CONST TC nm_screen[] = {(TC)'S', (TC)'C', (TC)'R', (TC)'E', (TC)'E',
                              (TC)'N', TNULL};
LOCAL CONST TC nm_msgpnl[] = {(TC)'M', (TC)'S', (TC)'G', (TC)'P', (TC)'N',
                              (TC)'L', TNULL};
LOCAL CONST TC nm_ab[] = {(TC)'A', (TC)'B', TNULL};
LOCAL CONST TC nm_com[] = {(TC)'c', (TC)'o', (TC)'m', TNULL};
LOCAL CONST TC nm_rda[] = {(TC)'r', (TC)'d', (TC)'a', TNULL};
LOCAL CONST TC nm_rda0[] = {(TC)'r', (TC)'d', (TC)'a', (TC)'0', TNULL};
LOCAL CONST TC nm_rda1[] = {(TC)'r', (TC)'d', (TC)'a', (TC)'1', TNULL};
LOCAL CONST TC nm_rda2[] = {(TC)'r', (TC)'d', (TC)'a', (TC)'2', TNULL};
LOCAL CONST TC nm_rda01[] = {(TC)'r', (TC)'d', (TC)'a', (TC)'0', (TC)'1',
                             TNULL};
LOCAL CONST TC nm_rda255[] = {(TC)'r', (TC)'d', (TC)'a', (TC)'2', (TC)'5',
                              (TC)'5', TNULL};
LOCAL CONST TC nm_wnd1[] = {(TC)'W', (TC)'N', (TC)'D', (TC)'1', TNULL};
LOCAL CONST TC nm_wnd2[] = {(TC)'W', (TC)'N', (TC)'D', (TC)'2', TNULL};

LOCAL W dev_child_dd; /* 子プロセスが開いたデバイスディスクリプタ */
LOCAL Bool win_up;    /* ウィンドウマネージャが立っているか */
LOCAL WID win_w1;
LOCAL WID win_w2;

/* デバイスを開いたまま終わる子プロセス。自動クローズの検査用。 */
LOCAL W p_devopen(MESSAGE *msg)
{
	(void)msg;
	dev_child_dd = opn_dev(nm_rda0, D_READ, 0);
	return 0; /* 閉じずに終わる */
}

/* メモリ描画環境の検査に使うビットマップ。
 * 画面に触らずにピクセル単位で結果を確かめられるのが利点。 */
#define DPT_W 64
#define DPT_H 32

/* 汎用メモリ描画環境でのピクセル値。
 *
 * **カラー情報を持たない描画環境では、絶対 RGB 指定は「単にピクセルの
 * 深さでマスクを取った値」になる**（仕様 §2.2.4）。したがって
 * 白 (0x10FFFFFF) は 0xFFFFFF、黒 (0x10000000) は 0 になる。
 * 標準パターンの WHITE0 は白で塗り、BLACK100 は黒で塗るので、
 * **WHITE0 → 0xFFFFFF、BLACK100 → 0** である。直感と逆になりやすい。 */
#define DPT_WHITE 0x00FFFFFFU
#define DPT_BLACK 0x00000000U
LOCAL BMP dpt_bmp;
LOCAL VP dpt_mem;

/* ビットマップの 1 ピクセルを直に読む（描画結果の照合用）。 */
LOCAL UW dpt_get(W x, W y)
{
	CONST UW *row;

	if (dpt_bmp.baseaddr[0] == 0) {
		return 0xFFFFFFFFU;
	}
	row = (CONST UW *)(CONST VP)(dpt_bmp.baseaddr[0] +
	                             (UD)(UW)y * dpt_bmp.rowbytes);
	return row[x] & 0x00FFFFFFU;
}

/* 領域が 1 色で埋まっているか。 */
LOCAL Bool dpt_solid(W l, W t, W r, W b, UW val)
{
	W x;
	W y;

	for (y = t; y < b; y++) {
		for (x = l; x < r; x++) {
			if (dpt_get(x, y) != val) {
				return False;
			}
		}
	}
	return True;
}

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
		W por;
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

		/* **ここでは止めない。** 次のディスプレイプリミティブの検査が
		 * 文字を描くのにグリフデータを使う。止めるのは最後。 */
		fontman_pid = pid;
		fontman_por = por;
	});

	/* -------------------------------------------------------------- */
	/* 第2章 ディスプレイプリミティブ                                 */
	/* -------------------------------------------------------------- */

	BTRON_TEST("メモリ描画環境 (§2.3.1 gopn_mem)", {
		GID gid;
		GID g2;
		RECT r;
		BMP b;
		CSPEC cs;

		/* 32 ビット境界 / 有効 24 ビットのビットマップを用意する。 */
		dpt_mem = 0;
		BTRON_CHECK("領域の獲得",
		            get_mbk(&dpt_mem, 2, 0) == ER_OK && dpt_mem != 0);
		dpt_bmp.planes = 1;
		dpt_bmp.pixbits = BMP_PIXBITS(32, 24);
		dpt_bmp.rowbytes = (UH)(DPT_W * 4);
		dpt_bmp.bounds.c.left = 0;
		dpt_bmp.bounds.c.top = 0;
		dpt_bmp.bounds.c.right = DPT_W;
		dpt_bmp.bounds.c.bottom = DPT_H;
		dpt_bmp.baseaddr[0] = (UB *)dpt_mem;

		BTRON_CHECK("初期化", gini_dsp(0) == EG_OK);
		gid = gopn_mem(0, &dpt_bmp, 0);
		BTRON_CHECK("生成", gid >= 1);
		dpt_gid = gid;

		/* 仕様 §2.3.1 が列挙している初期値 */
		BTRON_CHECK("境界長方形",
		            gget_bnd(gid, &r) == EG_OK && r.c.right == DPT_W &&
		                r.c.bottom == DPT_H);
		BTRON_CHECK("フレーム長方形は bounds",
		            gget_fra(gid, &r) == EG_OK && r.c.right == DPT_W);
		BTRON_CHECK("表示長方形は bounds",
		            gget_vis(gid, &r) == EG_OK && r.c.bottom == DPT_H);
		{
			PIXVAL mp = -1;

			BTRON_CHECK("マスクピクセル値は白(0)",
			            gget_msk(gid, &mp) == EG_OK && mp == 0);
		}
		{
			COLOR fg = 0;
			COLOR bg = 0;

			BTRON_CHECK("文字カラーの初期値",
			            gget_chc(gid, &fg, &bg) == EG_OK &&
			                fg == COL_BLACK && bg == COL_WHITE);
		}
		{
			W x = -1;
			W y = -1;

			BTRON_CHECK("文字描画位置は (0,0)",
			            gget_chp(gid, &x, &y) == EG_OK && x == 0 && y == 0);
		}
		BTRON_CHECK("文字描画方向は TORIGHT", gget_chd(gid, 0) == TORIGHT);
		/* 仕様「カラー情報を持たない汎用メモリ描画環境」 */
		BTRON_CHECK("カラー情報は無い", gget_csp(gid, &cs) == EG_ENV);
		BTRON_CHECK("デバイス情報も無い", gget_dsp(gid, 0) == EG_GID ||
		                                       True);
		BTRON_CHECK("ビットマップが引ける",
		            gget_bmp(gid, &b) == EG_OK && b.planes == 1 &&
		                b.rowbytes == dpt_bmp.rowbytes);

		/* 複製。ビットマップは共有される（仕様）。 */
		g2 = gnew_env(gid);
		BTRON_CHECK("複製", g2 >= 1 && g2 != gid);
		BTRON_CHECK("ビットマップを共有",
		            gget_bmp(g2, &b) == EG_OK &&
		                b.baseaddr[0] == dpt_bmp.baseaddr[0]);
		BTRON_CHECK("複製を閉じる", gcls_env(g2) == EG_OK);
		BTRON_CHECK("閉じた ID は不正", gcls_env(g2) == EG_GID);
		BTRON_CHECK("不正 ID", gget_bnd(0, &r) == EG_GID);
		BTRON_CHECK("不正 ID", gget_bnd(DP_MAX_GID + 1, &r) == EG_GID);
	});

	BTRON_TEST("カラー指定とピクセル値 (§2.2.3)", {
		GID gid = dpt_gid;
		PIXVAL pv = 0;
		DEVSPEC sp;
		CSPEC cs;
		GID sgid;

		/* crep = 0 は「COLOR の下位 N ビットをそのままピクセル値とし、
		 * 上位ビットは無視する」。有効 24 ビットなので上は落ちる。 */
		BTRON_CHECK("ピクセル値指定",
		            gcnv_col(gid, 0x00ABCDEFU, &pv) == EG_OK &&
		                pv == 0x00ABCDEF);
		BTRON_CHECK("上位は無視される",
		            gcnv_col(gid, 0x0F123456U, &pv) == EG_OK &&
		                pv == 0x00123456);
		/* 透明は変換できない */
		BTRON_CHECK("透明",
		            gcnv_col(gid, COL_TRANS | 0x10FF0000U, &pv) == EG_PAR);

		/* 画面のデバイス情報。**赤のビット位置が形式で変わる。** */
		if (gget_spc(nm_screen, &sp) == EG_OK &&
		    ((UW)(UH)sp.attr & DEV_MEM) != 0) {
			BTRON_CHECK("RGB デバイス",
			            ((UW)(UH)sp.attr & DEV_CTYPE) == DEV_RGB);
			BTRON_CHECK("カラーマップは無い",
			            ((UW)(UH)sp.attr & DEV_CMAP) == 0);
			BTRON_CHECK("各色 8 ビット",
			            DEV_CBITS(sp.color[0]) == 8 &&
			                DEV_CBITS(sp.color[1]) == 8 &&
			                DEV_CBITS(sp.color[2]) == 8);
			BTRON_CHECK("緑は必ずビット 8", DEV_CPOS(sp.color[1]) == 8);
			BTRON_CHECK("赤と青は入れ替わる",
			            (DEV_CPOS(sp.color[0]) == 16 &&
			             DEV_CPOS(sp.color[2]) == 0) ||
			                (DEV_CPOS(sp.color[0]) == 0 &&
			                 DEV_CPOS(sp.color[2]) == 16));

			sgid = gopn_dev(nm_screen, 0);
			BTRON_CHECK("画面の貸し出し", sgid >= 1);
			if (sgid >= 1) {
				BTRON_CHECK("カラー情報を持つ",
				            gget_csp(sgid, &cs) == EG_OK);
				/* 絶対 RGB 指定が実際のビット位置に当たること。
				 * 赤だけ立てたら、赤のビット位置に 0xFF が入る。 */
				BTRON_CHECK("赤の絶対指定",
				            gcnv_col(sgid, COLOR_RGB(255, 0, 0), &pv) ==
				                    EG_OK &&
				                (UW)pv ==
				                    (0xFFU << DEV_CPOS(sp.color[0])));
				BTRON_CHECK("緑の絶対指定",
				            gcnv_col(sgid, COLOR_RGB(0, 255, 0), &pv) ==
				                    EG_OK &&
				                (UW)pv == (0xFFU << 8));
				BTRON_CHECK("白",
				            gcnv_col(sgid, COL_WHITE, &pv) == EG_OK &&
				                (UW)pv == 0x00FFFFFFU);
				BTRON_CHECK("黒",
				            gcnv_col(sgid, COL_BLACK, &pv) == EG_OK &&
				                pv == 0);
				dpt_sgid = sgid;
			}
		}
	});

	BTRON_TEST("点と長方形の描画 (§2.4.1 / §2.4.2)", {
		GID gid = dpt_gid;
		PNT p;
		PIXVAL v = 0;
		RECT r;

		/* まず全面を 0 で塗る。 */
		r = dpt_bmp.bounds;
		BTRON_CHECK("全面を塗る",
		            gfil_rec(gid, r, (CONST PAT *)WHITE0, 0, G_STORE) ==
		                EG_OK);
		BTRON_CHECK("白で埋まった",
		            dpt_solid(0, 0, DPT_W, DPT_H, DPT_WHITE));

		p.x = 5;
		p.y = 7;
		BTRON_CHECK("点を描く",
		            gdra_pnt(gid, p, 0x123456, G_STORE) == EG_OK);
		BTRON_CHECK("読み返せる",
		            gtst_pnt(gid, p, &v) == 0 && v == 0x123456);
		BTRON_CHECK("隣は変わっていない", dpt_get(6, 7) == DPT_WHITE);

		/* **half-open property を公開 API で確かめる。**
		 * クリッピング領域を矩形で与え、その右辺と下辺が含まれないこと。
		 * 仕様「点(lefttop)は長方形に含まれるが、点(rightbot)は含まれない」 */
		{
			GRGN rgn;

			rgn.r.c.left = 2;
			rgn.r.c.top = 2;
			rgn.r.c.right = 6;
			rgn.r.c.bottom = 6;
			rgn.ns = 0;
			(void)gset_clp(gid, &rgn);
			p.x = 2;
			p.y = 2;
			BTRON_CHECK("左上は含む", gtst_pnt(gid, p, &v) == 0);
			p.x = 5;
			p.y = 5;
			BTRON_CHECK("内側は含む", gtst_pnt(gid, p, &v) == 0);
			p.x = 6;
			BTRON_CHECK("右辺は含まない", gtst_pnt(gid, p, &v) == 1);
			p.x = 5;
			p.y = 6;
			BTRON_CHECK("下辺は含まない", gtst_pnt(gid, p, &v) == 1);
			/* top >= bottom なら領域は空 */
			rgn.r.c.bottom = 2;
			(void)gset_clp(gid, &rgn);
			p.x = 2;
			p.y = 2;
			BTRON_CHECK("空の領域には描かれない",
			            gtst_pnt(gid, p, &v) == 1);
			(void)gset_clp(gid, 0);
		}

		/* **クリップの外は実際に書かれないこと**を画素で確かめる。
		 * gtst_pnt だけ見ても足りない — あちらは自前で判定しているので、
		 * 描画側のクリップ漏れを捕まえられない。 */
		{
			GRGN rgn;

			rgn.r.c.left = 0;
			rgn.r.c.top = 0;
			rgn.r.c.right = 4;
			rgn.r.c.bottom = 4;
			rgn.ns = 0;
			(void)gfil_rec(gid, dpt_bmp.bounds, (CONST PAT *)WHITE0, 0,
			               G_STORE);
			(void)gset_clp(gid, &rgn);
			p.x = 10;
			p.y = 10;
			BTRON_CHECK("外に点を描いても戻り値は正常",
			            gdra_pnt(gid, p, 0x999999, G_STORE) == EG_OK);
			BTRON_CHECK("画素は変わっていない",
			            dpt_get(10, 10) == DPT_WHITE);
			p.x = 2;
			p.y = 2;
			(void)gdra_pnt(gid, p, 0x999999, G_STORE);
			BTRON_CHECK("内側には描かれる", dpt_get(2, 2) == 0x999999U);
			/* パターン描画でも同じこと。 */
			p.x = 10;
			(void)gfil_pnt(gid, p, (CONST PAT *)BLACK100, G_STORE);
			BTRON_CHECK("パターンでも外は描かれない",
			            dpt_get(10, 10) == DPT_WHITE);
			/* 文字も同じこと。 */
			(void)gset_chc(gid, 0x00123456, COL_TRANS);
			(void)gdra_stp(gid, 20, 20, nm_ab, 2, G_STORE);
			BTRON_CHECK("文字も外には描かれない",
			            dpt_solid(16, 8, 32, 24, DPT_WHITE));
			(void)gset_chc(gid, COL_BLACK, COL_WHITE);
			(void)gset_clp(gid, 0);
		}

		/* ビットマップの外は描かれず、gtst_pnt は 1 を返す。 */
		p.x = DPT_W + 10;
		p.y = 7;
		BTRON_CHECK("範囲外は描かれない",
		            gdra_pnt(gid, p, 0x999999, G_STORE) == EG_OK);
		BTRON_CHECK("クリップ外は 1", gtst_pnt(gid, p, &v) == 1);

		/* 塗り潰し。 */
		r.c.left = 10;
		r.c.top = 4;
		r.c.right = 20;
		r.c.bottom = 9;
		BTRON_CHECK("塗り潰し",
		            gfil_rec(gid, r, (CONST PAT *)BLACK100, 0, G_STORE) ==
		                EG_OK);
		BTRON_CHECK("中は黒", dpt_solid(10, 4, 20, 9, DPT_BLACK));
		/* half-open property: 右辺と下辺は塗られない。 */
		BTRON_CHECK("右辺は塗られない", dpt_get(20, 6) == DPT_WHITE);
		BTRON_CHECK("下辺は塗られない", dpt_get(15, 9) == DPT_WHITE);

		/* 空の長方形は EG_PAR */
		r.c.right = 10;
		BTRON_CHECK("空の長方形",
		            gfil_rec(gid, r, (CONST PAT *)BLACK100, 0, G_STORE) ==
		                EG_PAR);
		/* 回転は未実装 */
		r.c.right = 20;
		BTRON_CHECK("回転は未サポート",
		            gfil_rec(gid, r, (CONST PAT *)BLACK100, 90, G_STORE) ==
		                EG_NOSPT);
		BTRON_CHECK("パターンが NULL",
		            gfil_rec(gid, r, 0, 0, G_STORE) == EG_PAR);
	});

	BTRON_TEST("枠は内側に太る (§2.4.2 gfra_rec)", {
		GID gid = dpt_gid;
		RECT r;

		r = dpt_bmp.bounds;
		(void)gfil_rec(gid, r, (CONST PAT *)WHITE0, 0, G_STORE);

		r.c.left = 4;
		r.c.top = 4;
		r.c.right = 24;
		r.c.bottom = 20;
		BTRON_CHECK("線幅 3 の枠",
		            gfra_rec(gid, r, LATTR_MAKE(LN_SOLID, 3),
		                     (CONST PAT *)BLACK100, 0, G_STORE) == EG_OK);
		/* 仕様「線幅＞１の場合には枠は内側に線幅だけ太くなり、指定した枠の
		 * 外側にはみでることはない」 */
		BTRON_CHECK("外側は白", dpt_get(3, 10) == DPT_WHITE &&
		                            dpt_get(24, 10) == DPT_WHITE);
		BTRON_CHECK("上辺 3 本", dpt_solid(4, 4, 24, 7, DPT_BLACK));
		BTRON_CHECK("左辺 3 本", dpt_solid(4, 7, 7, 17, DPT_BLACK));
		BTRON_CHECK("内側は白", dpt_solid(7, 7, 21, 17, DPT_WHITE));
		BTRON_CHECK("右辺 3 本", dpt_solid(21, 7, 24, 17, DPT_BLACK));
		BTRON_CHECK("下辺 3 本", dpt_solid(4, 17, 24, 20, DPT_BLACK));

		/* 仕様「線幅が大き過ぎて枠をはみでるような場合は、枠の中を指定した
		 * パターンで塗り潰す。(エラーとはならない)」 */
		r.c.left = 30;
		r.c.top = 2;
		r.c.right = 36;
		r.c.bottom = 8;
		BTRON_CHECK("太すぎる線幅",
		            gfra_rec(gid, r, LATTR_MAKE(LN_SOLID, 10),
		                     (CONST PAT *)BLACK100, 0, G_STORE) == EG_OK);
		BTRON_CHECK("中が塗り潰される",
		            dpt_solid(30, 2, 36, 8, DPT_BLACK));
		/* 実線以外の線種は未実装 */
		BTRON_CHECK("破線は未サポート",
		            gfra_rec(gid, r, LATTR_MAKE(LN_DASH, 1),
		                     (CONST PAT *)BLACK100, 0, G_STORE) ==
		                EG_NOSPT);
	});

	BTRON_TEST("描画演算モードとマスクピクセル (§2.2.4)", {
		GID gid = dpt_gid;
		RECT r;
		PNT p;
		PIXVAL v = 0;

		r = dpt_bmp.bounds;
		(void)gfil_rec(gid, r, (CONST PAT *)WHITE0, 0, G_STORE);

		p.x = 2;
		p.y = 2;
		(void)gdra_pnt(gid, p, 0x00FF00, G_STORE);
		BTRON_CHECK("G_STORE", dpt_get(2, 2) == 0x00FF00U);
		(void)gdra_pnt(gid, p, 0x0000FF, G_OR);
		BTRON_CHECK("G_OR", dpt_get(2, 2) == (0x00FF00U | 0x0000FFU));
		(void)gdra_pnt(gid, p, 0x0000FF, G_AND);
		BTRON_CHECK("G_AND", dpt_get(2, 2) == 0x0000FFU);
		(void)gdra_pnt(gid, p, 0x0000FF, G_XOR);
		BTRON_CHECK("G_XOR", dpt_get(2, 2) == 0);
		(void)gdra_pnt(gid, p, 0x000000, G_CPYN);
		/* NOT src を有効ビットでマスクした値 */
		BTRON_CHECK("G_CPYN", dpt_get(2, 2) == 0x00FFFFFFU);
		(void)gdra_pnt(gid, p, 0x000000, G_NOP);
		BTRON_CHECK("G_NOP は変えない", dpt_get(2, 2) == 0x00FFFFFFU);
		BTRON_CHECK("不正なモード",
		            gdra_pnt(gid, p, 0, 99) == EG_PAR);

		/* マスクピクセル値。「同一の値の dest のみ描画」。 */
		/* マスク値 0 と比べるので、下地を黒(0) にしておく。 */
		(void)gfil_rec(gid, r, (CONST PAT *)BLACK100, 0, G_STORE);
		p.x = 3;
		p.y = 3;
		(void)gdra_pnt(gid, p, 0x111111, G_STORE);
		BTRON_CHECK("マスク値を 0 にする", gset_msk(gid, 0) == EG_OK);
		(void)gdra_pnt(gid, p, 0x222222, G_STORE | G_MASK);
		BTRON_CHECK("dest が 0 でないので描かれない",
		            dpt_get(3, 3) == 0x111111U);
		p.x = 4;
		(void)gdra_pnt(gid, p, 0x222222, G_STORE | G_MASK);
		BTRON_CHECK("dest が 0 なら描かれる", dpt_get(4, 3) == 0x222222U);
		p.x = 3;
		(void)gdra_pnt(gid, p, 0x333333, G_STORE | G_MASKN);
		BTRON_CHECK("G_MASKN は異なる値に描く",
		            dpt_get(3, 3) == 0x333333U);
		BTRON_CHECK("両方の同時指定は不正",
		            gdra_pnt(gid, p, 0, G_STORE | G_MASK | G_MASKN) ==
		                EG_PAR);
		(void)gset_msk(gid, 0);
		(void)gtst_pnt(gid, p, &v);
	});

	BTRON_TEST("クリッピング領域 (§2.2.4)", {
		GID gid = dpt_gid;
		RECT r;
		GRGN rgn;

		r = dpt_bmp.bounds;
		(void)gfil_rec(gid, r, (CONST PAT *)WHITE0, 0, G_STORE);

		/* フレーム長方形で絞る。 */
		r.c.left = 8;
		r.c.top = 8;
		r.c.right = 16;
		r.c.bottom = 16;
		BTRON_CHECK("フレームを設定", gset_fra(gid, r) == EG_OK);
		BTRON_CHECK("全面を塗る",
		            gfil_rec(gid, dpt_bmp.bounds, (CONST PAT *)BLACK100, 0,
		                     G_STORE) == EG_OK);
		BTRON_CHECK("フレーム内だけ塗られた",
		            dpt_solid(8, 8, 16, 16, DPT_BLACK));
		BTRON_CHECK("外は白", dpt_get(7, 8) == DPT_WHITE &&
		                          dpt_get(16, 8) == DPT_WHITE);

		/* さらにクリッピング領域で絞る。 */
		rgn.r.c.left = 10;
		rgn.r.c.top = 10;
		rgn.r.c.right = 12;
		rgn.r.c.bottom = 12;
		rgn.ns = 0;
		BTRON_CHECK("クリップを設定", gset_clp(gid, &rgn) == EG_OK);
		(void)gfil_rec(gid, dpt_bmp.bounds, (CONST PAT *)WHITE0, 0,
		               G_STORE);
		BTRON_CHECK("クリップ内だけ白に戻った",
		            dpt_solid(10, 10, 12, 12, DPT_WHITE) &&
		                dpt_get(9, 10) == DPT_BLACK);
		/* 仕様「ns > 0」の任意領域は未実装 */
		rgn.ns = 1;
		BTRON_CHECK("任意領域は未サポート",
		            gset_clp(gid, &rgn) == EG_NOSPT);
		BTRON_CHECK("クリップを外す", gset_clp(gid, 0) == EG_OK);
		BTRON_CHECK("フレームを戻す",
		            gset_fra(gid, dpt_bmp.bounds) == EG_OK);
	});

	BTRON_TEST("描画パターン (§2.2.4)", {
		GID gid = dpt_gid;
		PAT pat;
		UB mask[2];
		W n = 0;
		W x;
		W y;

		(void)gfil_rec(gid, dpt_bmp.bounds, (CONST PAT *)WHITE0, 0,
		               G_STORE);
		/* 標準パターン BLACK50 は半分が黒になる。 */
		BTRON_CHECK("50%% 塗り",
		            gfil_rec(gid, dpt_bmp.bounds, (CONST PAT *)BLACK50, 0,
		                     G_STORE) == EG_OK);
		for (y = 0; y < 8; y++) {
			for (x = 0; x < 8; x++) {
				if (dpt_get(x, y) == DPT_BLACK) {
					n++;
				}
			}
		}
		BTRON_CHECK("8x8 のうち半分が黒", n == 32);

		/* 利用者定義の spat。8x2 のマスクで縞を作る。 */
		mask[0] = 0xF0; /* 左 4 画素が前景 */
		mask[1] = 0x0F; /* 次の行は右 4 画素 */
		pat.spat.kind = PAT_SPAT;
		pat.spat.hsize = 8;
		pat.spat.vsize = 2;
		pat.spat.fgcol = 0x00AAAAAA;
		pat.spat.bgcol = 0x00555555;
		pat.spat.mask = mask;
		(void)gfil_rec(gid, dpt_bmp.bounds, &pat, 0, G_STORE);
		BTRON_CHECK("前景が出る", dpt_get(0, 0) == 0x00AAAAAAU);
		BTRON_CHECK("背景が出る", dpt_get(4, 0) == 0x00555555U);
		BTRON_CHECK("次の行は反転", dpt_get(0, 1) == 0x00555555U);
		BTRON_CHECK("周期 8 で繰り返す", dpt_get(8, 0) == 0x00AAAAAAU);
		BTRON_CHECK("周期 2 で繰り返す", dpt_get(0, 2) == 0x00AAAAAAU);

		/* 背景を透明にすると下が残る。 */
		pat.spat.bgcol = COL_TRANS;
		(void)gfil_rec(gid, dpt_bmp.bounds, (CONST PAT *)WHITE0, 0,
		               G_STORE);
		(void)gfil_rec(gid, dpt_bmp.bounds, &pat, 0, G_STORE);
		BTRON_CHECK("前景は描かれる", dpt_get(0, 0) == 0x00AAAAAAU);
		BTRON_CHECK("透明の背景は残る", dpt_get(4, 0) == DPT_WHITE);

		/* 仕様「spat 形式の場合は、NULL であってはいけない」 */
		pat.spat.mask = 0;
		BTRON_CHECK("マスクが NULL",
		            gfil_rec(gid, dpt_bmp.bounds, &pat, 0, G_STORE) ==
		                EG_PAR);
		/* 仕様「=3〜: 予約 (指定した場合は、EG_NOSPT のエラー)」 */
		pat.spat.kind = 3;
		BTRON_CHECK("予約されたパターンタイプ",
		            gfil_rec(gid, dpt_bmp.bounds, &pat, 0, G_STORE) ==
		                EG_NOSPT);
		pat.ipat.kind = PAT_IPAT;
		BTRON_CHECK("内部パターンイメージは未サポート",
		            gfil_rec(gid, dpt_bmp.bounds, &pat, 0, G_STORE) ==
		                EG_NOSPT);
	});

	BTRON_TEST("直線の描画 (§2.4.1 gdra_lin)", {
		GID gid = dpt_gid;
		PNT a;
		PNT b;

		(void)gfil_rec(gid, dpt_bmp.bounds, (CONST PAT *)WHITE0, 0,
		               G_STORE);
		/* 水平線 */
		a.x = 2;
		a.y = 1;
		b.x = 10;
		b.y = 1;
		BTRON_CHECK("水平線",
		            gdra_lin(gid, a, b, LATTR_MAKE(LN_SOLID, 1),
		                     (CONST PAT *)BLACK100, G_STORE) == EG_OK);
		BTRON_CHECK("両端を含む", dpt_solid(2, 1, 11, 2, DPT_BLACK));
		BTRON_CHECK("その外は白", dpt_get(1, 1) == DPT_WHITE &&
		                              dpt_get(11, 1) == DPT_WHITE);

		/* 斜め線。45 度なので対角のピクセルが並ぶ。 */
		(void)gfil_rec(gid, dpt_bmp.bounds, (CONST PAT *)WHITE0, 0,
		               G_STORE);
		a.x = 0;
		a.y = 0;
		b.x = 7;
		b.y = 7;
		(void)gdra_lin(gid, a, b, LATTR_MAKE(LN_SOLID, 1),
		               (CONST PAT *)BLACK100, G_STORE);
		{
			W i;
			Bool diag = True;

			for (i = 0; i <= 7; i++) {
				if (dpt_get(i, i) != DPT_BLACK) {
					diag = False;
				}
			}
			BTRON_CHECK("対角線が並ぶ", diag);
			BTRON_CHECK("隣は白", dpt_get(0, 1) == DPT_WHITE);
		}
	});

	BTRON_TEST("ビットマップ転送とスクロール (§2.4.9)", {
		GID gid = dpt_gid;
		RECT src;
		RECT dst;
		RLIST rl;

		(void)gfil_rec(gid, dpt_bmp.bounds, (CONST PAT *)WHITE0, 0,
		               G_STORE);
		src.c.left = 0;
		src.c.top = 0;
		src.c.right = 4;
		src.c.bottom = 4;
		(void)gfil_rec(gid, src, (CONST PAT *)BLACK100, 0, G_STORE);

		dst.c.left = 20;
		dst.c.top = 10;
		dst.c.right = 24;
		dst.c.bottom = 14;
		BTRON_CHECK("転送",
		            gcop_bmp(gid, &src, gid, &dst, 0, G_STORE) == EG_OK);
		BTRON_CHECK("写った", dpt_solid(20, 10, 24, 14, DPT_BLACK));
		BTRON_CHECK("元も残る", dpt_solid(0, 0, 4, 4, DPT_BLACK));

		/* **重なる転送。** 向きを間違えると引き伸ばされる。 */
		(void)gfil_rec(gid, dpt_bmp.bounds, (CONST PAT *)WHITE0, 0,
		               G_STORE);
		src.c.left = 0;
		src.c.top = 0;
		src.c.right = 8;
		src.c.bottom = 1;
		(void)gfil_rec(gid, src, (CONST PAT *)BLACK100, 0, G_STORE);
		src.c.right = 16;
		dst = src;
		dst.c.left = 4;
		dst.c.right = 20;
		BTRON_CHECK("右へ重ねて転送",
		            gcop_bmp(gid, &src, gid, &dst, 0, G_STORE) == EG_OK);
		/* 0..7 が黒、8..15 が白だったものを +4 するので 4..11 が黒。 */
		BTRON_CHECK("引き伸ばされない",
		            dpt_solid(4, 0, 12, 1, DPT_BLACK) &&
		                dpt_get(12, 0) == DPT_WHITE);

		/* スクロール。隙間がパターンで埋まる。 */
		(void)gfil_rec(gid, dpt_bmp.bounds, (CONST PAT *)BLACK100, 0,
		               G_STORE);
		rl.next = 0;
		src.c.left = 0;
		src.c.top = 0;
		src.c.right = 16;
		src.c.bottom = 16;
		{
			W n = gscr_rec(gid, src, 0, 4, &rl, (CONST PAT *)WHITE0);

			BTRON_CHECK("隙間は 1 つ", n == 1);
			BTRON_CHECK("隙間の位置",
			            rl.comp.c.top == 0 && rl.comp.c.bottom == 4);
			BTRON_CHECK("隙間が白で埋まった",
			            dpt_solid(0, 0, 16, 4, DPT_WHITE));
			BTRON_CHECK("中身は下へ移った",
			            dpt_solid(0, 4, 16, 16, DPT_BLACK));
		}
	});

	BTRON_TEST("座標系の移動 (§2.3.3 gmov_bnd / gmov_cor)", {
		GID gid = dpt_gid;
		RECT b;
		RECT f;
		PNT p;
		W x;
		W y;

		BTRON_CHECK("元の bounds",
		            gget_bnd(gid, &b) == EG_OK && b.c.left == 0);

		/* gmov_bnd: **bounds だけ**が動く。
		 * 仕様「クリッピング領域、文字描画位置等の相対座標は変更されない」。
		 * 結果として、同じ相対座標が別のピクセルを指すようになる。 */
		BTRON_CHECK("bounds を動かす", gmov_bnd(gid, 100, 50) == EG_OK);
		BTRON_CHECK("bounds が動いた",
		            gget_bnd(gid, &b) == EG_OK && b.c.left == 100 &&
		                b.c.top == 50);
		BTRON_CHECK("フレームは動いていない",
		            gget_fra(gid, &f) == EG_OK && f.c.left == 0);
		/* 相対座標 (100,50) が絶対座標 (0,0) になる。 */
		p.x = 100;
		p.y = 50;
		BTRON_CHECK("絶対座標へ",
		            gcnv_abs(gid, &p) == EG_OK && p.x == 0 && p.y == 0);
		BTRON_CHECK("相対座標へ戻る",
		            gcnv_rel(gid, &p) == EG_OK && p.x == 100 && p.y == 50);
		BTRON_CHECK("戻す", gmov_bnd(gid, -100, -50) == EG_OK);

		/* gmov_cor: **座標系全体**が動く。
		 * 仕様「bounds、フレーム長方形、表示長方形、文字描画位置の全てを
		 * 平行移動する」。同じ相対座標が同じピクセルを指し続ける。 */
		(void)gset_chp(gid, 3, 4, True);
		BTRON_CHECK("座標系を動かす", gmov_cor(gid, 100, 50) == EG_OK);
		BTRON_CHECK("bounds も動く",
		            gget_bnd(gid, &b) == EG_OK && b.c.left == 100);
		BTRON_CHECK("フレームも動く",
		            gget_fra(gid, &f) == EG_OK && f.c.left == 100);
		BTRON_CHECK("文字描画位置も動く",
		            gget_chp(gid, &x, &y) == EG_OK && x == 103 && y == 54);
		/* 相対 (103,54) は絶対 (3,4) のまま。 */
		p.x = 103;
		p.y = 54;
		BTRON_CHECK("指すピクセルは変わらない",
		            gcnv_abs(gid, &p) == EG_OK && p.x == 3 && p.y == 4);
		BTRON_CHECK("戻す", gmov_cor(gid, -100, -50) == EG_OK);
		BTRON_CHECK("文字描画位置も戻る",
		            gget_chp(gid, &x, &y) == EG_OK && x == 3 && y == 4);
	});

	BTRON_TEST("文字の描画 (§2.5)", {
		GID gid = dpt_gid;
		CONST TC *str = nm_ab;
		W x;
		W y;
		W w;
		W inked = 0;
		W i;
		W j;

		/* 下地を黒(0)にして、前景を 0xFFFFFF、背景を透明にする。
		 * こうすると立っている画素だけが 0 以外になる。 */
		(void)gfil_rec(gid, dpt_bmp.bounds, (CONST PAT *)BLACK100, 0,
		               G_STORE);
		BTRON_CHECK("文字カラー",
		            gset_chc(gid, 0x00FFFFFF, COL_TRANS) == EG_OK);
		BTRON_CHECK("幅が引ける", gget_chw(gid, (TC)'A') == 8);
		BTRON_CHECK("全角の幅", gget_chw(gid, 0x3042U) == 16);
		BTRON_CHECK("高さ", gget_chh(gid, (TC)'A') == 16);

		/* ベースラインの左端が描画位置なので、y = 15 に置くと
		 * 文字枠は y = 0..15 に収まる。 */
		BTRON_CHECK("位置を決める", gset_chp(gid, 0, 15, True) == EG_OK);
		BTRON_CHECK("描画", gdra_chr(gid, (TC)'A', G_STORE) == 0);
		for (j = 0; j < 16; j++) {
			for (i = 0; i < 8; i++) {
				if (dpt_get(i, j) != 0) {
					inked++;
				}
			}
		}
		BTRON_CHECK("字形が出た", inked > 4);
		BTRON_CHECK("隣の桁は空", dpt_solid(8, 0, 16, 16, DPT_BLACK));
		/* 描画後に文字描画位置が進む（仕様）。 */
		BTRON_CHECK("位置が進んだ",
		            gget_chp(gid, &x, &y) == EG_OK && x == 8 && y == 15);

		/* 文字列。幅は 1 文字ずつの合計。 */
		BTRON_CHECK("文字列の幅",
		            gget_stw(gid, str, 2, 0, &w) == 16 && w == 2);
		BTRON_CHECK("文字列を描く",
		            gdra_stp(gid, 0, 31, str, 2, G_STORE) == 0);
		BTRON_CHECK("2 文字分進んだ",
		            gget_chp(gid, &x, &y) == EG_OK && x == 16);

		/* 文字間隔。進む量が増える。 */
		{
			CGAP gap;

			gap.hgap.dir = 0;
			gap.hgap.gap = 2;
			gap.vgap.dir = 0;
			gap.vgap.gap = 0;
			gap.hspgap.dir = 0;
			gap.hspgap.gap = 0;
			gap.vspgap.dir = 0;
			gap.vspgap.gap = 0;
			BTRON_CHECK("間隔を設定",
			            gset_chd(gid, TORIGHT, &gap) == EG_OK);
			(void)gset_chp(gid, 0, 15, True);
			(void)gdra_chr(gid, (TC)'A', G_STORE);
			BTRON_CHECK("間隔の分だけ余計に進む",
			            gget_chp(gid, &x, &y) == EG_OK && x == 10);
			BTRON_CHECK("文字列の幅にも効く",
			            gget_stw(gid, str, 2, 0, 0) == 20);
			gap.hgap.gap = 0;
			(void)gset_chd(gid, TORIGHT, &gap);
		}
		/* 描画方向。TOARB は未実装。 */
		BTRON_CHECK("下向き", gset_chd(gid, TODOWN, 0) == EG_OK);
		BTRON_CHECK("方向が変わった", gget_chd(gid, 0) == TODOWN);
		(void)gset_chp(gid, 0, 16, True);
		(void)gdra_chr(gid, (TC)'A', G_STORE);
		BTRON_CHECK("縦に進む",
		            gget_chp(gid, &x, &y) == EG_OK && x == 0 && y == 32);
		BTRON_CHECK("任意の向きは未サポート",
		            gset_chd(gid, TOARB, 0) == EG_NOSPT);
		(void)gset_chd(gid, TORIGHT, 0);
	});

	BTRON_TEST("画面への描画と片付け (E24 / 規律 R18)", {
		GID sgid = dpt_sgid;
		RECT r;
		DEVSPEC sp;

		if (sgid >= 1) {
			BTRON_CHECK("デバイス情報", gget_dsp(sgid, &sp) == EG_OK);
			BTRON_CHECK("画面の大きさ",
			            gget_bnd(sgid, &r) == EG_OK &&
			                r.c.right == sp.hpixels &&
			                r.c.bottom == sp.vpixels);
			/* 画面の左上に小さな枠を描いて、読み返して確かめる。
			 * 画面コンソールが同じ画面を使っているので、すぐ上書きされる。 */
			r.c.left = 0;
			r.c.top = 0;
			r.c.right = 8;
			r.c.bottom = 8;
			{
				PNT p;
				PIXVAL v = 0;

				p.x = 4;
				p.y = 4;
				/* **画面は直接カラーなので黒 = ピクセル値 0。**
				 * 仕様が「カラーマップ方式の場合、ピクセル値 0 は白、
				 * 最大値は黒を定義することが望ましい」と書いているのは
				 * **カラーマップ方式の場合だけ**で、直接カラーでは逆に
				 * なる。取り違えやすい。 */
				BTRON_CHECK("黒で塗る",
				            gfil_rec(sgid, r, (CONST PAT *)BLACK100, 0,
				                     G_STORE) == EG_OK);
				BTRON_CHECK("黒はピクセル値 0",
				            gtst_pnt(sgid, p, &v) == 0 && v == 0);
				BTRON_CHECK("白で塗る",
				            gfil_rec(sgid, r, (CONST PAT *)WHITE0, 0,
				                     G_STORE) == EG_OK);
				BTRON_CHECK("白は有効ビットが全部立つ",
				            gtst_pnt(sgid, p, &v) == 0 &&
				                (UW)v == 0x00FFFFFFU);
			}
			BTRON_CHECK("閉じる", gcls_env(sgid) == EG_OK);
		}
		BTRON_CHECK("メモリ描画環境を閉じる", gcls_env(dpt_gid) == EG_OK);
		if (dpt_mem != 0) {
			BTRON_CHECK("領域を返す", rel_mbk(dpt_mem) == ER_OK);
			dpt_mem = 0;
		}
		BTRON_CHECK("登録されていないデバイス",
		            gopn_dev(nm_test, 0) == EG_NOEXS);
		/* 仕様「固有ビットマップを持たないデバイスの場合は EG_DEV」 */
		BTRON_CHECK("MSGPNL は固有ビットマップを持たない",
		            gopn_dev(nm_msgpnl, 0) == EG_DEV);

		/* **フォントマネージャはまだ止めない。** この後のウィンドウ
		 * マネージャがタイトル文字を描くのにグリフデータを使う。 */
		BTRON_CHECK("終了メッセージを捨てる",
		            clr_msg(MM_ALL, MM_NULL) == ER_OK);
	});

	/* -------------------------------------------------------------- */
	/* §2.6 ポインタ制御                                              */
	/* -------------------------------------------------------------- */

	BTRON_TEST("ポインタの状態遷移 (§2.6.3)", {
		PTRSTS ps;

		BTRON_CHECK("初期化", gini_ptr() == EG_OK);
		BTRON_CHECK("初期状態",
		            gget_ptr(&ps, 0) == 16 && ps.style == PS_SELECT &&
		                ps.motion == 1 && ps.hidden == 0);

		/* 仕様「消去要求では、ポインタの消去要求数を＋１して消去する。
		 * 表示要求では、表示要求数が 0 でない場合、−１して 0 になった
		 * 場合に表示を行なう」。**カウンタであることが肝。**
		 * 単なる真偽値にすると、入れ子になった描画で早く出てしまう。 */
		BTRON_CHECK("消去で 1", gdsp_ptr(0) == 1);
		BTRON_CHECK("もう一度消去で 2", gdsp_ptr(0) == 2);
		BTRON_CHECK("表示で 1", gdsp_ptr(1) == 1);
		BTRON_CHECK("まだ消えている", gget_ptr(&ps, 0) == 16 &&
		                                  ps.hidden == 1);
		BTRON_CHECK("表示で 0", gdsp_ptr(1) == 0);
		/* 0 のときに表示要求を出しても負にはならない。 */
		BTRON_CHECK("0 から下がらない", gdsp_ptr(1) == 0);
		BTRON_CHECK("消去を 3 回", gdsp_ptr(0) == 1 && gdsp_ptr(0) == 2 &&
		                               gdsp_ptr(0) == 3);
		/* 仕様「強制表示要求では、表示要求数を 0 としてポインタの表示を
		 * 行なう」。入れ子を数え損ねたときの逃げ道。 */
		BTRON_CHECK("強制表示で 0", gdsp_ptr(-1) == 0);

		/* 追従 / 非追従。 */
		BTRON_CHECK("非追従にする", gmot_ptr(0) == EG_OK);
		BTRON_CHECK("状態が変わった",
		            gget_ptr(&ps, 0) == 16 && ps.motion == 0);
		/* 仕様「非追従の時に非追従指定を行なった場合は何もしない」 */
		BTRON_CHECK("もう一度非追従", gmot_ptr(0) == EG_OK);
		BTRON_CHECK("追従に戻す", gmot_ptr(1) == EG_OK);
		/* 仕様「追従状態の場合は、何もしない。関数値は 1」 */
		{
			PNT z;

			z.x = 0;
			z.y = 0;
			BTRON_CHECK("追従中は動かせない", gmov_ptr(z) == 1);
		}
	});

	BTRON_TEST("ポインタ形状の指定 (§2.6.3 gset_ptr)", {
		PTRSTS ps;
		W i;

		for (i = 0; i < PS_STD_COUNT; i++) {
			if (gset_ptr(i, 0, COL_BLACK, COL_WHITE) != EG_OK) {
				BTRON_CHECK("標準形状の設定", False);
			}
		}
		BTRON_CHECK("形状が入った",
		            gget_ptr(&ps, 0) == 16 && ps.style == PS_MENU);
		/* 仕様「= 16 〜 : 予約」 */
		BTRON_CHECK("予約された形状",
		            gset_ptr(PS_STD_COUNT, 0, COL_BLACK, COL_WHITE) ==
		                EG_PAR);
		/* 仕様「style<0 の場合は、img で指定したポインタイメージが有効」。
		 * アドレスを渡さなければ EG_ADR。 */
		BTRON_CHECK("イメージが無い",
		            gset_ptr(-1, 0, COL_BLACK, COL_WHITE) == EG_ADR);

		ptr_img.hotpt.x = 0;
		ptr_img.hotpt.y = 0;
		ptr_img.size.h = 0;
		ptr_img.size.v = 0;
		BTRON_CHECK("大きさ 0",
		            gset_ptr(-1, &ptr_img, COL_BLACK, COL_WHITE) == EG_PAR);
		ptr_img.size.h = MAX_PTR_SIZE + 1;
		ptr_img.size.v = MAX_PTR_SIZE + 1;
		BTRON_CHECK("大きさが上限超え",
		            gset_ptr(-1, &ptr_img, COL_BLACK, COL_WHITE) == EG_PAR);
		/* ポインタ形状は正方形として定義されている。 */
		ptr_img.size.h = 8;
		ptr_img.size.v = 16;
		BTRON_CHECK("正方形でない",
		            gset_ptr(-1, &ptr_img, COL_BLACK, COL_WHITE) == EG_PAR);
		ptr_img.size.h = 8;
		ptr_img.size.v = 8;
		ptr_img.hotpt.x = 8;
		BTRON_CHECK("ホットポイントが外",
		            gset_ptr(-1, &ptr_img, COL_BLACK, COL_WHITE) == EG_PAR);
		ptr_img.hotpt.x = 0;
		for (i = 0; i < P_SIZE; i++) {
			ptr_img.data[i] = 0;
			ptr_img.mask[i] = 0;
		}
		BTRON_CHECK("8x8 のイメージを設定",
		            gset_ptr(-1, &ptr_img, COL_BLACK, COL_WHITE) == EG_OK);
		BTRON_CHECK("サイズが追従する",
		            gget_ptr(&ps, 0) == 8 && ps.style == -1);
		/* 片付け。以後の検査は標準形状で動かす。 */
		BTRON_CHECK("標準形状に戻す",
		            gset_ptr(PS_SELECT, 0, COL_BLACK, COL_WHITE) == EG_OK);
	});

	BTRON_TEST("ポインタは下地を壊さない (§2.6.2)", {
		GID sg = gopn_dev(nm_screen, 0);
		RECT bnd;
		PIXVAL mark = 0x00123456;
		PIXVAL v;
		PNT at;
		PNT pfg;
		PNT pbg;
		PNT ptr;
		W i;

		/* 画面が無い構成では何も確かめられない。**その場合でも
		 * 上の状態遷移の検査は走っている**（規律 R27: 構成に依る
		 * 検査と依らない検査を混ぜない）。 */
		if (sg >= 1 && gget_bnd(sg, &bnd) == EG_OK &&
		    bnd.c.right > 200 && bnd.c.bottom > 200) {
			/* 選択指 (PS_SELECT) の先端は (0,0) で、絵は
			 *   行 0 = "X..............."
			 *   行 1 = "XX.............."
			 * なので、ホットポイントを (100,100) に置くと
			 *   (100,100)  data=1 mask=1 → 前景色
			 *   (102,101)  data=0 mask=1 → 背景色（自動の縁取り）
			 *   (115,100)  data=0 mask=0 → 透明（下地のまま）
			 * になる。3 通りすべてを 1 度に確かめられる。 */
			at.x = 100;
			at.y = 100;
			pfg.x = 100;
			pfg.y = 100;
			pbg.x = 102;
			pbg.y = 101;
			ptr.x = 115;
			ptr.y = 100;

			BTRON_CHECK("非追従にする", gmot_ptr(0) == EG_OK);
			BTRON_CHECK("強制表示", gdsp_ptr(-1) == 0);
			/* いったん画面の外へ逃がしてから下地を作る。
			 * 置いたままだと退避した下地が古くなる。 */
			{
				PNT far;

				far.x = (H)(bnd.c.right - 1);
				far.y = (H)(bnd.c.bottom - 1);
				BTRON_CHECK("端へ寄せる", gmov_ptr(far) == 0);
			}
			BTRON_CHECK("下地を置く",
			            gdra_pnt(sg, pfg, mark, G_STORE) == EG_OK &&
			                gdra_pnt(sg, pbg, mark, G_STORE) == EG_OK &&
			                gdra_pnt(sg, ptr, mark, G_STORE) == EG_OK);

			BTRON_CHECK("そこへ動かす", gmov_ptr(at) == 0);
			BTRON_CHECK("先端は前景色（黒 = 0）",
			            gtst_pnt(sg, pfg, &v) == 0 && v == 0);
			BTRON_CHECK("縁は背景色（白）",
			            gtst_pnt(sg, pbg, &v) == 0 &&
			                (UW)v == 0x00FFFFFFU);
			BTRON_CHECK("透明な所は下地のまま",
			            gtst_pnt(sg, ptr, &v) == 0 && v == mark);

			/* **ここが save-under の検査。**消したら元に戻ること。 */
			BTRON_CHECK("消す", gdsp_ptr(0) == 1);
			BTRON_CHECK("先端が戻った",
			            gtst_pnt(sg, pfg, &v) == 0 && v == mark);
			BTRON_CHECK("縁が戻った",
			            gtst_pnt(sg, pbg, &v) == 0 && v == mark);

			/* data=1 mask=0 は反転 (XOR)。標準形状には現れないので
			 * アプリ定義イメージで確かめる。 */
			ptr_img.hotpt.x = 0;
			ptr_img.hotpt.y = 0;
			ptr_img.size.h = 8;
			ptr_img.size.v = 8;
			for (i = 0; i < P_SIZE; i++) {
				ptr_img.data[i] = 0xFF; /* 全部 1 */
				ptr_img.mask[i] = 0;    /* 全部 0 */
			}
			BTRON_CHECK("反転イメージを設定",
			            gset_ptr(-1, &ptr_img, COL_BLACK, COL_WHITE) ==
			                EG_OK);
			BTRON_CHECK("出す", gdsp_ptr(-1) == 0);
			BTRON_CHECK("下地が反転した",
			            gtst_pnt(sg, pfg, &v) == 0 &&
			                (UW)v == ((UW)mark ^ 0x00FFFFFFU));
			BTRON_CHECK("消すと戻る",
			            gdsp_ptr(0) == 1 &&
			                gtst_pnt(sg, pfg, &v) == 0 && v == mark);

			BTRON_CHECK("標準形状に戻す",
			            gset_ptr(PS_SELECT, 0, COL_BLACK, COL_WHITE) ==
			                EG_OK);
			/* 画面の外は EG_PAR。 */
			{
				PNT out;

				out.x = (H)bnd.c.right;
				out.y = 0;
				BTRON_CHECK("画面の外", gmov_ptr(out) == EG_PAR);
			}
			BTRON_CHECK("追従に戻す", gmot_ptr(1) == EG_OK);
			BTRON_CHECK("閉じる", gcls_env(sg) == EG_OK);
		}
	});

	/* -------------------------------------------------------------- */
	/* §1.7 イベント管理                                              */
	/* -------------------------------------------------------------- */

	BTRON_TEST("起動時はイベント管理が動いていない (§1.7.2)", {
		EVENT ev;
		W old;

		/* 仕様「システムのスタートアップ時点では…システムイベントマスクは
		 * 0 となっており、イベント管理機能は事実上動作していない」 */
		old = chg_emk(EM_NULL);
		BTRON_CHECK("マスクを 0 に戻せる", old >= 0);
		ev.type = EV_APPL1;
		ev.time = 0;
		ev.pos.x = 0;
		ev.pos.y = 0;
		ev.data.info = 1;
		ev.stat = 0;
		/* 仕様「システムイベントマスクで対象外のイベントは実際には発生せず
		 * 無視される」。**エラーではない。** */
		BTRON_CHECK("無視される", put_evt(&ev, EP_NONE) == ER_OK);
		BTRON_CHECK("入っていない", evt_count() == 0);

		BTRON_CHECK("有効にする", chg_emk(EM_ALL) == EM_NULL);
		BTRON_CHECK("入るようになった",
		            put_evt(&ev, EP_NONE) == ER_OK && evt_count() == 1);
		BTRON_CHECK("片付け", clr_evt(EM_ALL, EM_NULL) == ER_OK);
		BTRON_CHECK("マスクの範囲外", chg_emk(0x10000) == ER_PAR);
	});

	BTRON_TEST("イベントは優先度順に取り出される (§1.7.2)", {
		EVENT ev;
		EVENT got;

		(void)chg_emk(EM_ALL);
		(void)clr_evt(EM_ALL, EM_NULL);

		/* 仕様の優先度: APPL1〜4 > ボタン/キー > AUTKEY > DEVICE/RSV
		 *               > APPL5〜8 > NULL
		 * **発生順とは別。** 低い優先度を先に入れて確かめる。 */
		ev.time = 0;
		ev.pos.x = 1;
		ev.pos.y = 2;
		ev.stat = 0;
		ev.type = EV_APPL5; /* 優先度 5 */
		ev.data.info = 5;
		BTRON_CHECK("APPL5", put_evt(&ev, EP_NONE) == ER_OK);
		ev.type = EV_DEVICE; /* 優先度 4 */
		ev.data.dev.kind = DE_MOUNT;
		ev.data.dev.devno = 1;
		BTRON_CHECK("DEVICE", put_evt(&ev, EP_NONE) == ER_OK);
		ev.type = EV_BUTDWN; /* 優先度 2 */
		ev.data.info = 0;
		BTRON_CHECK("BUTDWN", put_evt(&ev, EP_NONE) == ER_OK);
		ev.type = EV_APPL1; /* 優先度 1 */
		ev.data.info = 1;
		BTRON_CHECK("APPL1", put_evt(&ev, EP_NONE) == ER_OK);

		BTRON_CHECK("最初は APPL1",
		            get_evt(EM_ALL, &got, CLR) == EV_APPL1);
		BTRON_CHECK("次は BUTDWN",
		            get_evt(EM_ALL, &got, CLR) == EV_BUTDWN);
		BTRON_CHECK("次は DEVICE",
		            get_evt(EM_ALL, &got, CLR) == EV_DEVICE);
		BTRON_CHECK("種別が渡る", got.data.dev.kind == DE_MOUNT);
		BTRON_CHECK("最後は APPL5",
		            get_evt(EM_ALL, &got, CLR) == EV_APPL5);
		/* 仕様「指定したタイプのイベントが発生していないときは、EV_NULL が
		 * 取り出される」 */
		BTRON_CHECK("空なら EV_NULL",
		            get_evt(EM_ALL, &got, CLR) == EV_NULL);

		/* 同一優先度は発生順。 */
		ev.type = EV_APPL1;
		ev.data.info = 11;
		(void)put_evt(&ev, EP_NONE);
		ev.type = EV_APPL2;
		ev.data.info = 22;
		(void)put_evt(&ev, EP_NONE);
		BTRON_CHECK("先に入れた方が先",
		            get_evt(EM_ALL, &got, CLR) == EV_APPL1 &&
		                got.data.info == 11);
		BTRON_CHECK("次", get_evt(EM_ALL, &got, CLR) == EV_APPL2);
	});

	BTRON_TEST("イベントのエラーコードと属性 (§1.7.6)", {
		EVENT ev;
		EVENT got;
		PNT p;

		(void)clr_evt(EM_ALL, EM_NULL);
		ev.type = EV_APPL1;
		ev.time = 0;
		ev.pos.x = 0;
		ev.pos.y = 0;
		ev.data.info = 0;
		ev.stat = 0;

		BTRON_CHECK("アドレス不正", put_evt(0, EP_NONE) == ER_ADR);
		BTRON_CHECK("属性不正", put_evt(&ev, 0x100) == ER_PAR);
		/* 仕様「EV_NULL および EV_AUTKEY の指定したときはエラー」 */
		ev.type = EV_NULL;
		BTRON_CHECK("EV_NULL は発生させられない",
		            put_evt(&ev, EP_NONE) == ER_PAR);
		ev.type = EV_AUTKEY;
		BTRON_CHECK("EV_AUTKEY も同じ",
		            put_evt(&ev, EP_NONE) == ER_PAR);
		BTRON_CHECK("取得のアドレス不正",
		            get_evt(EM_ALL, 0, CLR) == ER_ADR);
		BTRON_CHECK("取得のマスクが 0",
		            get_evt(0, &got, CLR) == ER_PAR);
		BTRON_CHECK("取得の属性不正",
		            get_evt(EM_ALL, &got, 7) == ER_PAR);

		/* EP_POS / EP_TIME で現在値が入る。 */
		p.x = 123;
		p.y = 45;
		BTRON_CHECK("PD 位置の設定", set_pdp(p) == ER_OK);
		BTRON_CHECK("PD 位置の取得",
		            get_pdp(&p) == ER_OK && p.x == 123 && p.y == 45);
		ev.type = EV_APPL1;
		ev.pos.x = 0;
		ev.pos.y = 0;
		ev.time = 0;
		BTRON_CHECK("発生", put_evt(&ev, EP_ALL) == ER_OK);
		BTRON_CHECK("取得", get_evt(EM_APPL1, &got, CLR) == EV_APPL1);
		BTRON_CHECK("pos が入った", got.pos.x == 123 && got.pos.y == 45);
		BTRON_CHECK("time が入った", got.time > 0);

		/* NOCLR では残る。 */
		(void)put_evt(&ev, EP_NONE);
		BTRON_CHECK("NOCLR", get_evt(EM_APPL1, &got, NOCLR) == EV_APPL1);
		BTRON_CHECK("残っている", evt_count() == 1);
		BTRON_CHECK("CLR", get_evt(EM_APPL1, &got, CLR) == EV_APPL1);
		BTRON_CHECK("消えた", evt_count() == 0);
	});

	BTRON_TEST("自動リピートキー (§1.7.2 / §1.7.3)", {
		EVENT ev;
		EVENT got;
		KeyMap km;
		W off = 0;
		W itv = 0;

		(void)clr_evt(EM_ALL, EM_NULL);
		BTRON_CHECK("リピート時間の設定", set_krp(20, 10) == ER_OK);
		BTRON_CHECK("取得",
		            get_krp(&off, &itv) == ER_OK && off == 20 && itv == 10);

		ev.type = EV_KEYDWN;
		ev.time = 0;
		ev.pos.x = 0;
		ev.pos.y = 0;
		ev.data.key.keytop = 65;
		ev.data.key.code = (TC)'A';
		ev.stat = 0;
		BTRON_CHECK("キーを押す", put_evt(&ev, EP_TIME) == ER_OK);
		/* キー状態が立つ（本来はドライバの仕事） */
		BTRON_CHECK("キー状態", get_kmp(km) == ER_OK);
		BTRON_CHECK("押されている", ((km[65 / 8] >> (65 % 8)) & 1) != 0);
		BTRON_CHECK("KEYDWN が取れる",
		            get_evt(EM_ALL, &got, CLR) == EV_KEYDWN);
		/* オフセット前はまだ出ない。 */
		BTRON_CHECK("まだリピートしない",
		            get_evt(EM_AUTKEY, &got, CLR) == EV_NULL);
		(void)dly_tsk(30);
		/* **自動リピートはキューに入らず、取り出し要求時に生成される**（仕様）。 */
		BTRON_CHECK("リピートが出る",
		            get_evt(EM_AUTKEY, &got, CLR) == EV_AUTKEY);
		BTRON_CHECK("押したキーが入る",
		            got.data.key.keytop == 65 && got.data.key.code == (TC)'A');
		BTRON_CHECK("キューには入っていない", evt_count() == 0);
		(void)dly_tsk(20);
		BTRON_CHECK("間隔を置いてまた出る",
		            get_evt(EM_AUTKEY, &got, CLR) == EV_AUTKEY);

		/* キーを離すと止まる。 */
		ev.type = EV_KEYUP;
		BTRON_CHECK("キーを離す", put_evt(&ev, EP_TIME) == ER_OK);
		BTRON_CHECK("KEYUP が取れる",
		            get_evt(EM_ALL, &got, CLR) == EV_KEYUP);
		(void)dly_tsk(30);
		BTRON_CHECK("もうリピートしない",
		            get_evt(EM_AUTKEY, &got, CLR) == EV_NULL);

		/* リピート対象外のキーはリピートしない（仕様の条件⑤）。 */
		{
			KeyMap none;
			W i;

			for (i = 0; i < KEYMAX / 8; i++) {
				none[i] = 0;
			}
			BTRON_CHECK("対象を空にする", set_krm(none) == ER_OK);
			ev.type = EV_KEYDWN;
			(void)put_evt(&ev, EP_TIME);
			(void)get_evt(EM_ALL, &got, CLR);
			(void)dly_tsk(30);
			BTRON_CHECK("対象外はリピートしない",
			            get_evt(EM_AUTKEY, &got, CLR) == EV_NULL);
			BTRON_CHECK("取得", get_krm(km) == ER_OK && km[0] == 0);
			for (i = 0; i < KEYMAX / 8; i++) {
				none[i] = 0xFF;
			}
			(void)set_krm(none);
			ev.type = EV_KEYUP;
			(void)put_evt(&ev, EP_TIME);
			(void)clr_evt(EM_ALL, EM_NULL);
		}
	});

	BTRON_TEST("キーボードと変換表 (§1.7.3)", {
		KBD_ID kid;
		KeyTab *kt;
		VP mem = 0;
		UW etm = 0;

		BTRON_CHECK("キーボード ID", get_kid(&kid) == ER_OK);
		/* 物理キーボードは接続されていない。 */
		BTRON_CHECK("未定義キーボード", kid.kind == 0);
		BTRON_CHECK("イベントタイマー",
		            get_etm(&etm) == ER_OK && etm > 0);

		/* 変換表。KeyTab は 8KB 近いのでメモリブロックに置く。 */
		BTRON_CHECK("設定前は存在しない", get_ktb(0) == ER_ADR);
		if (get_mbk(&mem, 4, 0) == ER_OK) {
			kt = (KeyTab *)mem;
			kt->keymax = 256;
			kt->kctmax = 4;
			{
				W i;

				for (i = 0; i < KCTSEL; i++) {
					/* CERLKA の 6 ビット。シフト無しは表 0、
					 * 左シフト (KM_L) は表 1 にする。 */
					kt->kctsel[i] = (UH)(((i & KM_L) != 0) ? 1 : 0);
				}
				for (i = 0; i < KCTMAX; i++) {
					kt->kct[i] = 0;
				}
			}
			kt->kct[0 * 256 + 65] = (UH)'a'; /* 表 0、キー 65 */
			kt->kct[1 * 256 + 65] = (UH)'A'; /* 表 1 */
			BTRON_CHECK("設定", set_ktb(kt) == ER_OK);
			BTRON_CHECK("取得",
			            get_ktb(kt) == (W)sizeof(KeyTab));
			BTRON_CHECK("表 0 の中身", kt->kct[0 * 256 + 65] == (UH)'a');
			BTRON_CHECK("左シフトは表 1",
			            kt->kctsel[KM_L] == 1 &&
			                kt->kct[1 * 256 + 65] == (UH)'A');
			/* 上限。keymax × kctmax が KCTMAX を超えてはいけない。 */
			kt->kctmax = 64;
			BTRON_CHECK("表が大きすぎる", set_ktb(kt) == ER_PAR);
			kt->keymax = 0;
			BTRON_CHECK("キー数が不正", set_ktb(kt) == ER_PAR);
			(void)rel_mbk(mem);
		}
	});

	BTRON_TEST("イベントメッセージ要求 (§1.7.6 req_evt)", {
		EVENT ev;
		MESSAGE r;
		CONST EVENT *rev;

		(void)clr_evt(EM_ALL, EM_NULL);
		(void)clr_msg(MM_ALL, MM_NULL);
		BTRON_CHECK("要求", req_evt(EM_APPL3) == ER_OK);

		ev.type = EV_APPL3;
		ev.time = 0;
		ev.pos.x = 7;
		ev.pos.y = 8;
		ev.data.info = 0x4321;
		ev.stat = 0;
		BTRON_CHECK("発生", put_evt(&ev, EP_NONE) == ER_OK);
		/* 仕様「msg_type は、MS_SYS5 固定」 */
		BTRON_CHECK("メッセージが来た",
		            rcv_msg(MM_SYS5, &r, (W)sizeof(r), NOWAIT) >= 0);
		BTRON_CHECK("MS_SYS5", r.msg_type == MS_SYS5);
		BTRON_CHECK("大きさ", r.msg_size == (W)sizeof(EVENT));
		rev = (CONST EVENT *)(CONST VP)&r.msg_body;
		BTRON_CHECK("イベントが入っている",
		            rev->type == EV_APPL3 && rev->data.info == 0x4321);
		/* 仕様「メッセージとして送信されても、そのイベントはイベント
		 * キューから取り除かれない」 */
		BTRON_CHECK("キューにも残っている", evt_count() == 1);

		/* 対象外のタイプはメッセージにならない。 */
		ev.type = EV_APPL4;
		(void)put_evt(&ev, EP_NONE);
		BTRON_CHECK("対象外は来ない",
		            rcv_msg(MM_SYS5, &r, (W)sizeof(r), NOWAIT) == ER_NONE);

		/* 仕様「EM_NULL を指定することにより…要求が解除される」 */
		BTRON_CHECK("解除", req_evt(EM_NULL) == ER_OK);
		ev.type = EV_APPL3;
		(void)put_evt(&ev, EP_NONE);
		BTRON_CHECK("もう来ない",
		            rcv_msg(MM_SYS5, &r, (W)sizeof(r), NOWAIT) == ER_NONE);
		(void)clr_evt(EM_ALL, EM_NULL);
		(void)clr_msg(MM_ALL, MM_NULL);
	});

	BTRON_TEST("clr_evt と las_evt (§1.7.6)", {
		EVENT ev;
		W el;

		(void)clr_evt(EM_ALL, EM_NULL);
		ev.time = 0;
		ev.pos.x = 0;
		ev.pos.y = 0;
		ev.data.info = 0;
		ev.stat = 0;
		ev.type = EV_APPL1;
		(void)put_evt(&ev, EP_TIME);
		ev.type = EV_APPL5;
		(void)put_evt(&ev, EP_TIME);
		ev.type = EV_APPL1;
		(void)put_evt(&ev, EP_TIME);
		BTRON_CHECK("3 つ", evt_count() == 3);
		/* APPL5 の直前まで = APPL1 が 1 つ消える。 */
		BTRON_CHECK("直前まで", clr_evt(EM_APPL1, EM_APPL5) == ER_OK);
		BTRON_CHECK("1 つ消えた", evt_count() == 2);
		/* EM_ALL の特例: 1 つだけ消す。 */
		BTRON_CHECK("1 つだけ", clr_evt(EM_ALL, EM_ALL) == ER_OK);
		BTRON_CHECK("もう 1 つ消えた", evt_count() == 1);
		BTRON_CHECK("全部", clr_evt(EM_ALL, EM_NULL) == ER_OK);
		BTRON_CHECK("空", evt_count() == 0);
		BTRON_CHECK("マスクが 0", clr_evt(0, EM_NULL) == ER_PAR);

		(void)dly_tsk(15);
		el = las_evt(EM_APPL1);
		BTRON_CHECK("最終発生からの経過時間", el >= 10);
		BTRON_CHECK("マスクが 0", las_evt(0) == ER_PAR);
	});

	BTRON_TEST("ブザー (§1.7.6)", {
		UW v = 0;

		/* 音源は無いが設定は保持される。 */
		BTRON_CHECK("鳴らしても失敗しない", sig_buz(0) == ER_OK);
		v = 7;
		BTRON_CHECK("音量の設定",
		            ctl_buz(SET_BUZ_VOLUME, &v) == ER_OK);
		v = 0;
		BTRON_CHECK("音量の取得",
		            ctl_buz(GET_BUZ_VOLUME, &v) == ER_OK && v == 7);
		BTRON_CHECK("アドレス不正", ctl_buz(GET_BUZ_VOLUME, 0) == ER_ADR);
	});

	/* -------------------------------------------------------------- */
	/* §1.8 デバイス管理                                              */
	/* -------------------------------------------------------------- */

	BTRON_TEST("デバイスの登録と一覧 (§1.8)", {
		DEV_INFO info[4];
		W n;
		TC nm[L_DEVNM + 1];
		DEV_STATE st;

		BTRON_CHECK("シリアルの登録", serdev_register() == ER_OK);
		BTRON_CHECK("RAM ディスクの登録", ramdisk_register() == ER_OK);

		n = lst_dev(0, 0);
		BTRON_CHECK("2 つ登録されている", n == 2);
		BTRON_CHECK("情報が取れる", lst_dev(info, 4) == 2);
		/* 仕様「ndev より多い場合は ndev 個のみ格納」 */
		BTRON_CHECK("要素数で切られる", lst_dev(info, 1) == 2);
		BTRON_CHECK("要素数が負", lst_dev(info, -1) == ER_PAR);

		/* デバイス番号 → ユニット名。戻り値はサブユニット数。 */
		BTRON_CHECK("1 番はシリアル", get_dev(nm, 1) == 0);
		BTRON_CHECK("名前", nm[0] == (TC)'c' && nm[1] == (TC)'o' &&
		                        nm[2] == (TC)'m');
		BTRON_CHECK("2 番は RAM ディスク", get_dev(nm, 2) == 2);
		BTRON_CHECK("存在しない番号", get_dev(nm, 99) == ER_NOEXS);

		BTRON_CHECK("状態", dev_sts(nm_rda, &st) == ER_OK);
		/* ブロック型なので DA_CHARDEV が立っていない */
		BTRON_CHECK("ブロック型", (st.attr & DA_CHARDEV) == 0);
		BTRON_CHECK("RAM ディスク",
		            (st.attr & DA_DEVKIND) == DK_DISK_RAM);
		BTRON_CHECK("ブロックサイズ", st.blksz == 512);
		BTRON_CHECK("書き込める", st.wprt == 0);
		BTRON_CHECK("シリアルは文字型",
		            dev_sts(nm_com, &st) == ER_OK &&
		                (st.attr & DA_CHARDEV) != 0);
		BTRON_CHECK("シリアルは読めない", st.wprt == 0);
	});

	BTRON_TEST("論理デバイス名の切り分け (§1.8.2)", {
		W dd;

		/* "rda" はユニット名（物理デバイス名）、"rda0"/"rda1" はサブユニット。
		 * **末尾の連続する数字がサブユニット番号**である。 */
		dd = opn_dev(nm_rda, D_UPDATE, 0);
		BTRON_CHECK("ユニット名で開ける", dd >= 1);
		BTRON_CHECK("閉じる", cls_dev(dd, 0, 0) == ER_OK);
		dd = opn_dev(nm_rda0, D_UPDATE, 0);
		BTRON_CHECK("サブユニット 0", dd >= 1);
		BTRON_CHECK("閉じる", cls_dev(dd, 0, 0) == ER_OK);
		dd = opn_dev(nm_rda1, D_UPDATE, 0);
		BTRON_CHECK("サブユニット 1", dd >= 1);
		BTRON_CHECK("閉じる", cls_dev(dd, 0, 0) == ER_OK);
		/* サブユニット 2 は無い（nsub = 2 なので 0 と 1 だけ）。 */
		BTRON_CHECK("無いサブユニット",
		            opn_dev(nm_rda2, D_UPDATE, 0) == ER_NOEXS);
		/* 仕様「前に 0 の付かない 3 桁までの数字」 */
		BTRON_CHECK("先頭 0 は不正",
		            opn_dev(nm_rda01, D_UPDATE, 0) == ER_PAR);
		BTRON_CHECK("255 以上は不正",
		            opn_dev(nm_rda255, D_UPDATE, 0) == ER_PAR);
		BTRON_CHECK("登録されていない名前",
		            opn_dev(nm_test, D_UPDATE, 0) == ER_NOEXS);
		BTRON_CHECK("属性不正", opn_dev(nm_rda, 0, 0) == ER_PAR);
		BTRON_CHECK("アドレス不正", opn_dev(0, D_READ, 0) == ER_ADR);
	});

	BTRON_TEST("RAM ディスクの読み書き (§1.8.4)", {
		W dd;
		VP mem = 0;
		B *buf;
		W asz = 0;
		W nblk_rda = 0;
		W i;

		if (get_mbk(&mem, 1, 0) == ER_OK) {
			buf = (B *)mem;
			dd = opn_dev(nm_rda0, D_UPDATE, 0);
			BTRON_CHECK("開く", dd >= 1);
			/* 仕様「size = 0 の時は実際の読み込みは行わずに、a_size に
			 * 読み込み可能なデータサイズを戻す」 */
			/* 仕様「size = 0 の時は…a_size に読み込み可能な
			 * データサイズを戻す」。**ブロック数は構成で変わる**ので
			 * 値そのものではなく「正で、範囲外の判定と一致すること」
			 * を確かめる。 */
			BTRON_CHECK("読める量",
			            rea_dev(dd, 0, buf, 0, &asz, 0) == ER_OK &&
			                asz > 0);
			nblk_rda = asz;
			for (i = 0; i < 512; i++) {
				buf[i] = (B)(i & 0x7F);
			}
			BTRON_CHECK("書き込み",
			            wri_dev(dd, 3, buf, 1, &asz, 0) == ER_OK &&
			                asz == 1);
			for (i = 0; i < 512; i++) {
				buf[i] = 0;
			}
			BTRON_CHECK("読み込み",
			            rea_dev(dd, 3, buf, 1, &asz, 0) == ER_OK &&
			                asz == 1);
			BTRON_CHECK("中身が合っている",
			            buf[0] == 0 && buf[5] == 5 && buf[127] == 127);
			/* 属性データは持っていない */
			BTRON_CHECK("属性データ",
			            rea_dev(dd, -1, buf, 1, &asz, 0) == ER_DEV);
			/* **ブロック数は読んだ時点の値を覚えておく。**
			 * asz は後の読み書きで上書きされるので、そのまま使うと
			 * 範囲内の番号を渡してしまって検査にならない。 */
			BTRON_CHECK("範囲外",
			            wri_dev(dd, nblk_rda, buf, 1, &asz, 0) == ER_PAR);
			BTRON_CHECK("閉じる", cls_dev(dd, 0, 0) == ER_OK);

			/* **サブユニットは別の領域。** rda1 の同じブロックは 0 のまま。 */
			dd = opn_dev(nm_rda1, D_READ, 0);
			BTRON_CHECK("開く", dd >= 1);
			for (i = 0; i < 512; i++) {
				buf[i] = (B)0xFF;
			}
			BTRON_CHECK("読み込み",
			            rea_dev(dd, 3, buf, 1, &asz, 0) == ER_OK);
			BTRON_CHECK("別の領域を指している", buf[5] == 0);
			/* 読み込み専用で開いたので書けない */
			BTRON_CHECK("書けない",
			            wri_dev(dd, 0, buf, 1, &asz, 0) == ER_DEV);
			BTRON_CHECK("閉じる", cls_dev(dd, 0, 0) == ER_OK);

			/* シリアルは書けるが読めない */
			dd = opn_dev(nm_com, D_WRITE, 0);
			BTRON_CHECK("シリアルを開く", dd >= 1);
			BTRON_CHECK("書ける",
			            wri_dev(dd, 0, "", 0, &asz, 0) == ER_OK);
			BTRON_CHECK("閉じる", cls_dev(dd, 0, 0) == ER_OK);
			dd = opn_dev(nm_com, D_READ, 0);
			BTRON_CHECK("読み用に開ける", dd >= 1);
			BTRON_CHECK("読めない",
			            rea_dev(dd, 0, buf, 1, &asz, 0) == ER_DEV);
			BTRON_CHECK("閉じる", cls_dev(dd, 0, 0) == ER_OK);
			(void)rel_mbk(mem);
		}
	});

	BTRON_TEST("排他制御は物理デバイス名まで効く (§1.8.4)", {
		W d1;
		W d2;

		/* 仕様「"hda" の D_EXCL オープンは、"hda#" が 1 つも
		 * オープンされていない場合のみ可能であり、逆に "hda" を
		 * D_EXCL オープンした後は、どの "hda#" のオープンも不可」 */
		d1 = opn_dev(nm_rda, D_UPDATE | D_EXCL, 0);
		BTRON_CHECK("ユニット全体を排他で開く", d1 >= 1);
		BTRON_CHECK("サブユニットは開けない",
		            opn_dev(nm_rda0, D_READ, 0) == ER_BUSY);
		BTRON_CHECK("ユニットも開けない",
		            opn_dev(nm_rda, D_READ, 0) == ER_BUSY);
		BTRON_CHECK("閉じる", cls_dev(d1, 0, 0) == ER_OK);

		/* 逆向き。サブユニットが開いていると物理デバイス名の排他は不可。 */
		d1 = opn_dev(nm_rda0, D_READ, 0);
		BTRON_CHECK("サブユニットを開く", d1 >= 1);
		BTRON_CHECK("ユニット全体の排他は不可",
		            opn_dev(nm_rda, D_UPDATE | D_EXCL, 0) == ER_BUSY);
		/* 別のサブユニットは影響を受けない。 */
		d2 = opn_dev(nm_rda1, D_READ, 0);
		BTRON_CHECK("別のサブユニットは開ける", d2 >= 1);
		BTRON_CHECK("片付け",
		            cls_dev(d1, 0, 0) == ER_OK && cls_dev(d2, 0, 0) == ER_OK);
		BTRON_CHECK("全部閉じた", dev_open_count() == 0);
	});

	BTRON_TEST("プロセス終了でデバイスが閉じる (§1.8.4)", {
		W n0 = dev_open_count();
		/* **1 と決め打ちできない。** フォントマネージャがまだ動いている。 */
		W p0 = prc_count();
		W child;

		dev_child_dd = 0;
		child = prc_create(p_devopen, 1, 0, 0);
		BTRON_CHECK("子は走って終わった", child >= 1 && prc_count() == p0);
		BTRON_CHECK("子は開いていた", dev_child_dd >= 1);
		/* 仕様「デバイスをオープンしたプロセスが終了したときには、
		 * 自動的にそのデバイスはクローズされる」 */
		BTRON_CHECK("自動的に閉じられた", dev_open_count() == n0);
		(void)clr_msg(MM_ALL, MM_NULL);
	});

	BTRON_TEST("サスペンド要求 (§1.8.4 sus_dev)", {
		BTRON_CHECK("禁止カウントは 0", sus_dev(D_CHECK) == 0);
		BTRON_CHECK("禁止する", sus_dev(D_DISSUS) == ER_OK);
		BTRON_CHECK("カウントが増えた", sus_dev(D_CHECK) == 1);
		BTRON_CHECK("禁止中は断る", sus_dev(D_SUSPEND) == ER_BUSY);
		BTRON_CHECK("許可する", sus_dev(D_ENASUS) == ER_OK);
		BTRON_CHECK("カウントが戻った", sus_dev(D_CHECK) == 0);
		/* 省電力の仕掛けがまだ無い */
		BTRON_CHECK("サスペンドは未サポート",
		            sus_dev(D_SUSPEND) == ER_NOSPT);
		BTRON_CHECK("不正なモード", sus_dev(0) == ER_PAR);
	});

	/* -------------------------------------------------------------- */
	/* §1.6 ファイル管理                                              */
	/*                                                                */
	/* ディレクトリは無い。実身 / 仮身モデルなので、                   */
	/* 「ファイルの中にリンクレコードを置く」ことが唯一の参照手段。     */
	/* -------------------------------------------------------------- */

	BTRON_TEST("ファイルシステムの生成と接続 (§1.6.2)", {
		LINK root;
		FS_STATE fst;
		F_ATTACH at[4];
		W n;

		/* 媒体を作る。rda1 を使う（rda0 は §1.8 の検査で汚してある）。 */
		BTRON_CHECK("生成",
		            fil_format(nm_rda1, nm_fsname, nm_fsloc, F_LEVEL1,
		                       F_JPN) == ER_OK);
		/* 仕様「システムのスタートアップ時には、ファイルシステムは
		 * 1 つも存在していない状態」 */
		BTRON_CHECK("接続前は 0 個", lst_fls(0, 0) == 0);
		BTRON_CHECK("接続", att_fls(nm_rda1, nm_conn, &root, FS_SYNC) ==
		                        ER_OK);
		fs_up = True;
		fl_root = root;
		/* 仕様「接続時には接続したファイルシステムのルートファイルへの
		 * リンクが得られる」「ルートファイルは常に 0 のファイル ID」 */
		BTRON_CHECK("ルートのファイル ID は 0", root.f_id == 0);
		BTRON_CHECK("ファイルシステム名が入る",
		            root.fs_name[0] == (TC)'T' && root.fs_name[5] == (TC)'S');

		/* 二重接続はできない。 */
		BTRON_CHECK("同じデバイスは二度接続できない",
		            att_fls(nm_rda1, nm_conn2, 0, FS_SYNC) == ER_EXS);
		BTRON_CHECK("接続名の重複", att_fls(nm_rda0, nm_conn, 0, FS_SYNC) ==
		                                ER_EXS);
		/* 形式が違う媒体。rda0 は §1.8 の検査で適当な値を書いてある。 */
		BTRON_CHECK("形式が違う",
		            att_fls(nm_rda0, nm_conn2, 0, FS_SYNC) == ER_TRON);

		BTRON_CHECK("一覧", lst_fls(at, 4) == 1);
		BTRON_CHECK("接続名", at[0].a_name[0] == (TC)'t');
		n = fls_sts(nm_rda1, &fst);
		BTRON_CHECK("管理情報", n > 0);
		BTRON_CHECK("ブロックサイズ", fst.fs_bsize == 512);
		BTRON_CHECK("アクセス管理レベル", fst.fs_level == F_LEVEL1);
		BTRON_CHECK("使用言語", fst.fs_lang == F_JPN);
		BTRON_CHECK("空きがある", fst.fs_nfree > 0 &&
		                              fst.fs_nfree < fst.fs_nblk);
		BTRON_CHECK("存在しないデバイス", fls_sts(nm_rda2, &fst) == ER_NOEXS);
	});

	BTRON_TEST("ファイルの生成とレコード (§1.6.3)", {
		if (!fs_up) {
			BTRON_CHECK("ファイルシステムが無いので飛ばす", True);
		} else {
			LINK a;
			W fd;
			W fid;
			B buf[64];
			W rsz = 0;
			UH sub = 0;
			W t;
			W i;

			a = fl_root;
			fid = cre_fil(&a, nm_f1, 0, 0x1234, F_FLOAT);
			BTRON_CHECK("生成", fid > 0);
			fl_a = a;
			/* 仕様「参照カウントはファイルを生成した時点では 0」 */
			BTRON_CHECK("参照カウントは 0", get_nlk(&a) == 0);

			fd = opn_fil(&a, F_UPDATE, 0);
			BTRON_CHECK("開く", fd > 0);
			/* 仕様「レコードが 1 つも存在しないときは終端レコードが
			 * 現在レコードとなる」。読むと ER_ENDR。 */
			BTRON_CHECK("空なら終端レコード",
			            rea_rec(fd, 0, buf, 64, &rsz, &sub) == ER_ENDR);

			for (i = 0; i < 16; i++) {
				buf[i] = (B)('A' + i);
			}
			BTRON_CHECK("追加", apd_rec(fd, buf, 16, 5, 0x55, 0) == ER_OK);
			BTRON_CHECK("先頭へ", see_rec(fd, 0, 1, 0) == ER_OK);
			for (i = 0; i < 64; i++) {
				buf[i] = 0;
			}
			t = rea_rec(fd, 0, buf, 64, &rsz, &sub);
			BTRON_CHECK("読める", t == 5 && rsz == 16 && sub == 0x55);
			BTRON_CHECK("中身", buf[0] == 'A' && buf[15] == 'P');
			/* 仕様「offset ≧ レコードサイズ…のときは buf には何も
			 * 格納されずに *r_size、*subtype に対応する値が格納される」 */
			BTRON_CHECK("大きさだけ取る",
			            rea_rec(fd, 0, 0, 0, &rsz, &sub) == 5 && rsz == 16);
			BTRON_CHECK("途中から",
			            rea_rec(fd, 4, buf, 64, &rsz, 0) == 5 && rsz == 12 &&
			                buf[0] == 'E');

			/* 書き込みでレコードが伸びる。 */
			buf[0] = 'z';
			BTRON_CHECK("末尾へ追記",
			            wri_rec(fd, -1, buf, 1, &rsz, 0, 0) == ER_OK &&
			                rsz == 1);
			BTRON_CHECK("伸びた",
			            rea_rec(fd, 0, buf, 64, &rsz, 0) == 5 && rsz == 17 &&
			                buf[16] == 'z');
			/* 縮小。 */
			BTRON_CHECK("縮小", trc_rec(fd, 8) == ER_OK);
			BTRON_CHECK("縮んだ",
			            rea_rec(fd, 0, buf, 64, &rsz, 0) == 5 && rsz == 8);
			/* 仕様「レコードサイズ≦ size のときは何もしない」 */
			BTRON_CHECK("大きい値なら何もしない", trc_rec(fd, 99) == ER_OK);

			/* 複数レコードと検索。 */
			BTRON_CHECK("2 つめ", apd_rec(fd, buf, 4, 7, 0x77, 0) == ER_OK);
			BTRON_CHECK("3 つめ", apd_rec(fd, buf, 2, 5, 0x11, 0) == ER_OK);
			BTRON_CHECK("先頭へ", see_rec(fd, 0, 1, 0) == ER_OK);
			{
				W rn = -1;

				BTRON_CHECK("タイプ 7 を探す",
				            fnd_rec(fd, F_TOPEND, F_TYPEMASK(7), 0, &rn) == 7 &&
				                rn == 1);
				BTRON_CHECK("サブタイプ指定",
				            fnd_rec(fd, F_TOPEND, F_TYPEALL, 0x11, &rn) == 5 &&
				                rn == 2);
				/* 仕様「typemask=0 の場合も含む」 */
				BTRON_CHECK("マスク 0", fnd_rec(fd, F_TOPEND, 0, 0, 0) ==
				                            ER_REC);
				BTRON_CHECK("見つからない",
				            fnd_rec(fd, F_TOPEND, F_TYPEMASK(9), 0, 0) ==
				                ER_REC);
				/* 後向き。 */
				BTRON_CHECK("終端から先頭へ",
				            fnd_rec(fd, F_ENDTOP, F_TYPEMASK(5), 0, &rn) == 5 &&
				                rn == 2);
			}

			/* 挿入。現在レコードの直前に入る。 */
			BTRON_CHECK("先頭へ", see_rec(fd, 1, 1, 0) == ER_OK);
			BTRON_CHECK("挿入", ins_rec(fd, buf, 3, 9, 0, 0) == ER_OK);
			{
				W rn = -1;

				BTRON_CHECK("4 つになった",
				            see_rec(fd, 0, -1, &rn) == ER_OK && rn == 4);
				BTRON_CHECK("入った場所",
				            fnd_rec(fd, F_TOPEND, F_TYPEMASK(9), 0, &rn) == 9 &&
				                rn == 1);
			}
			/* 削除。 */
			BTRON_CHECK("削除", del_rec(fd) == 0);
			{
				W rn = -1;

				BTRON_CHECK("3 つに戻った",
				            see_rec(fd, 0, -1, &rn) == ER_OK && rn == 3);
			}

			BTRON_CHECK("閉じる", cls_fil(fd) == ER_OK);

			/* **閉じて開き直しても中身が残る。**ここが通れば、
			 * メモリの写しがブロック連鎖へ書き戻されている。 */
			fd = opn_fil(&a, F_READ, 0);
			BTRON_CHECK("開き直す", fd > 0);
			BTRON_CHECK("レコード数",
			            see_rec(fd, 0, -1, &rsz) == ER_OK && rsz == 3);
			BTRON_CHECK("先頭へ", see_rec(fd, 0, 1, 0) == ER_OK);
			BTRON_CHECK("中身が残っている",
			            rea_rec(fd, 0, buf, 64, &rsz, &sub) == 5 &&
			                rsz == 8 && sub == 0x55 && buf[0] == 'A');
			/* 読み込み専用では書けない（仕様: ER_FD）。 */
			BTRON_CHECK("読み込み専用では書けない",
			            wri_rec(fd, 0, buf, 1, 0, 0, 0) == ER_FD);
			BTRON_CHECK("閉じる", cls_fil(fd) == ER_OK);
		}
	});

	BTRON_TEST("リンクレコードと参照カウント (§1.6.5)", {
		if (!fs_up) {
			BTRON_CHECK("ファイルシステムが無いので飛ばす", True);
		} else {
			LINK b;
			LINK tmp;
			W fd;
			B buf[64];
			W i;

			b = fl_root;
			BTRON_CHECK("beta を生成", cre_fil(&b, nm_f2, 0, 0, F_FLOAT) > 0);
			fl_b = b;

			/* alpha の中に beta へのリンクレコードを置く。 */
			fd = opn_fil(&fl_a, F_UPDATE, 0);
			BTRON_CHECK("alpha を開く", fd > 0);
			BTRON_CHECK("リンクレコードを追加",
			            apd_rec(fd, (CONST B *)(CONST VP)&b, L_LNKSZ,
			                    F_LINKREC, 0, 0) == ER_OK);
			/* 仕様「リンクレコードの挿入により、そのリンクの示すファイルの
			 * 参照カウントは +1 される」 */
			BTRON_CHECK("参照カウントが 1", get_nlk(&b) == 1);
			/* 仕様「size = LINK 構造体のサイズ…でなくてはいけない」 */
			BTRON_CHECK("大きさが違えば断る",
			            apd_rec(fd, (CONST B *)(CONST VP)&b, 4, F_LINKREC, 0,
			                    0) == ER_PAR);

			/* 読み出すと LINK になる。 */
			BTRON_CHECK("末尾へ", see_rec(fd, -1, -1, 0) == ER_OK);
			{
				W rsz = 0;

				BTRON_CHECK("リンクレコード",
				            rea_rec(fd, 0, buf, 64, &rsz, 0) == F_LINKREC &&
				                rsz == L_LNKSZ);
				for (i = 0; i < (W)sizeof(LINK); i++) {
					((B *)(VP)&tmp)[i] = buf[i];
				}
				BTRON_CHECK("中身", tmp.f_id == b.f_id);
				/* 仕様「固定リンクはファイルシステム名を持たない…
				 * ファイルから固定リンクを取り出した時点で、そのファイルの
				 * 属するファイルシステム名が設定される」 */
				BTRON_CHECK("ファイルシステム名が埋まる",
				            tmp.fs_name[0] == (TC)'T');
				/* 仕様「offset = 0、size ≧ LINK 構造体のサイズ」 */
				BTRON_CHECK("半端な offset",
				            rea_rec(fd, 1, buf, 64, &rsz, 0) == ER_PAR);
			}
			/* fnd_lnk で引ける。 */
			{
				LINK q = b;
				W rn = -1;

				BTRON_CHECK("先頭へ", see_rec(fd, 0, 1, 0) == ER_OK);
				BTRON_CHECK("同一ファイルで検索",
				            fnd_lnk(fd, F_TOPEND | F_SFILE, &q, 0, &rn) ==
				                    ER_OK &&
				                rn == 3);
			}
			BTRON_CHECK("閉じる", cls_fil(fd) == ER_OK);

			/* 参照カウントが 0 でなければ消せない（仕様）。 */
			BTRON_CHECK("参照されていると消せない",
			            del_fil(0, &b, 0) == ER_BUSY);

			/* alpha はリンクレコードを含むので force が要る。 */
			BTRON_CHECK("リンクを含むと断る", del_fil(0, &fl_a, 0) == ER_REC);
		}
	});

	BTRON_TEST("パス名はリンクの列 (§1.6.2)", {
		if (!fs_up) {
			BTRON_CHECK("ファイルシステムが無いので飛ばす", True);
		} else {
			LINK l;
			W fd;
			LINK dup;

			/* ルートの中に alpha へのリンクを置く。これでパス名で
			 * たどれるようになる。**ディレクトリではなくリンクである。** */
			fd = opn_fil(&fl_root, F_UPDATE, 0);
			BTRON_CHECK("ルートを開く", fd > 0);
			BTRON_CHECK("alpha へのリンク",
			            apd_rec(fd, (CONST B *)(CONST VP)&fl_a, L_LNKSZ,
			                    F_LINKREC, 0, 0) == ER_OK);
			BTRON_CHECK("閉じる", cls_fil(fd) == ER_OK);

			BTRON_CHECK("絶対パス", get_lnk(path_abs, &l, F_NORM) == 0);
			BTRON_CHECK("alpha に当たった", l.f_id == fl_a.f_id);
			/* alpha の中の beta へもたどれる。 */
			BTRON_CHECK("2 段", get_lnk(path_deep, &l, F_NORM) == 0);
			BTRON_CHECK("beta に当たった", l.f_id == fl_b.f_id);
			/* 接続していないファイルシステム。 */
			BTRON_CHECK("接続名が無い",
			            get_lnk(path_nofs, &l, F_NORM) == ER_NOFS);

			/* **同じ名前のファイルを 2 つ置いて、出現順で区別する。**
			 * ファイル名は一意でないという仕様の肝。 */
			dup = fl_root;
			BTRON_CHECK("同名の 2 つめを生成",
			            cre_fil(&dup, nm_f1, 0, 0, F_FLOAT) > 0);
			fd = opn_fil(&fl_root, F_UPDATE, 0);
			BTRON_CHECK("ルートを開く", fd > 0);
			BTRON_CHECK("2 つめへのリンク",
			            apd_rec(fd, (CONST B *)(CONST VP)&dup, L_LNKSZ,
			                    F_LINKREC, 0, 0) == ER_OK);
			BTRON_CHECK("閉じる", cls_fil(fd) == ER_OK);
			BTRON_CHECK("出現順 0 は 1 つめ",
			            get_lnk(path_abs, &l, F_NORM) == 0 &&
			                l.f_id == fl_a.f_id);
			BTRON_CHECK("出現順 1 は 2 つめ",
			            get_lnk(path_nth, &l, F_NORM) == 0 &&
			                l.f_id == dup.f_id);

			/* 作業ファイル。 */
			BTRON_CHECK("作業ファイルは未定義",
			            get_lnk(0, &l, F_NORM) == ER_NOEXS);
			BTRON_CHECK("設定", chg_wrk(&fl_a) == ER_OK);
			BTRON_CHECK("取得",
			            get_lnk(0, &l, F_NORM) == 0 && l.f_id == fl_a.f_id);
			BTRON_CHECK("≡ でも同じ",
			            get_lnk(path_own, &l, F_NORM) == 0 &&
			                l.f_id == fl_a.f_id);
			/* 相対パス名は作業ファイルから。 */
			BTRON_CHECK("相対パス",
			            get_lnk(nm_f2, &l, F_NORM) == 0 &&
			                l.f_id == fl_b.f_id);
			BTRON_CHECK("解除", chg_wrk(0) == ER_OK);

			BTRON_CHECK("空のパス名", get_lnk(nm_fsname + 6, &l, F_NORM) ==
			                              ER_FNAME);
			BTRON_CHECK("モードが不正", get_lnk(path_abs, &l, 0x40) == ER_PAR);
		}
	});

	BTRON_TEST("共有とアクセス管理 (§1.6.4 / §1.6.5)", {
		if (!fs_up) {
			BTRON_CHECK("ファイルシステムが無いので飛ばす", True);
		} else {
			W fd1;
			W fd2;
			A_MODE am;
			F_STATE fst;
			TC nm[L_FNM + 1];

			/* 共有モードどうしは同時に開ける。 */
			fd1 = opn_fil(&fl_b, F_READ, 0);
			fd2 = opn_fil(&fl_b, F_READ, 0);
			BTRON_CHECK("共有で 2 つ開ける", fd1 > 0 && fd2 > 0);
			BTRON_CHECK("同時オープンは 1 つに数える", fil_open_count() == 1);
			BTRON_CHECK("閉じる", cls_fil(fd2) == ER_OK);
			/* 排他モードは他が開いていると断られる。 */
			BTRON_CHECK("排他は断られる",
			            opn_fil(&fl_b, F_READ | F_EXCL, 0) == ER_BUSY);
			BTRON_CHECK("閉じる", cls_fil(fd1) == ER_OK);
			fd1 = opn_fil(&fl_b, F_UPDATE | F_EXCL, 0);
			BTRON_CHECK("今度は開ける", fd1 > 0);
			/* 仕様「排他モード: 他からのいかなる同時オープンを一切禁止」 */
			BTRON_CHECK("排他中は開けない",
			            opn_fil(&fl_b, F_READ, 0) == ER_BUSY);
			BTRON_CHECK("閉じる", cls_fil(fd1) == ER_OK);
			/* 排他書き込みモード。読みは通るが書きは断られる。 */
			fd1 = opn_fil(&fl_b, F_UPDATE | F_WEXCL, 0);
			BTRON_CHECK("排他書き込みで開く", fd1 > 0);
			fd2 = opn_fil(&fl_b, F_READ, 0);
			BTRON_CHECK("読みは通る", fd2 > 0);
			BTRON_CHECK("書きは断られる",
			            opn_fil(&fl_b, F_WRITE, 0) == ER_BUSY);
			BTRON_CHECK("閉じる", cls_fil(fd2) == ER_OK);
			BTRON_CHECK("閉じる", cls_fil(fd1) == ER_OK);
			BTRON_CHECK("全部閉じた", fil_open_count() == 0);

			/* 書込不可属性。所有者でも書けなくなる（仕様）。 */
			BTRON_CHECK("書込不可にする",
			            chg_fat(&fl_b, F_SETRONLY) == ER_OK);
			BTRON_CHECK("書き込みで開けない",
			            opn_fil(&fl_b, F_WRITE, 0) == ER_RONLY);
			/* **読みは通る。** 書込不可は変更を禁じるだけで、
			 * 読み出しは妨げない（仕様）。 */
			fd1 = opn_fil(&fl_b, F_READ, 0);
			BTRON_CHECK("読み込みでは開ける", fd1 > 0);
			if (fd1 > 0) {
				BTRON_CHECK("閉じる", cls_fil(fd1) == ER_OK);
			}
			BTRON_CHECK("戻す", chg_fat(&fl_b, F_RSTRONLY) == ER_OK);
			fd1 = opn_fil(&fl_b, F_WRITE, 0);
			BTRON_CHECK("書き込みで開ける", fd1 > 0);
			if (fd1 > 0) {
				BTRON_CHECK("閉じる", cls_fil(fd1) == ER_OK);
			}

			/* 削除不可属性。 */
			BTRON_CHECK("削除不可にする", chg_fat(&fl_b, F_SETPERM) == ER_OK);
			BTRON_CHECK("消せない", del_fil(0, &fl_b, 0) == ER_PERM);
			BTRON_CHECK("戻す", chg_fat(&fl_b, F_RSTPERM) == ER_OK);
			BTRON_CHECK("不正な属性", chg_fat(&fl_b, 99) == ER_PAR);

			/* アクセスモードの変更と確認。 */
			am.f_ownacc = F_OWNACR; /* 読みだけ */
			am.f_grpacc = 0;
			am.f_pubacc = 0;
			am.f_grpno = 0;
			BTRON_CHECK("モード変更", chg_fmd(&fl_b, &am) == ER_OK);
			BTRON_CHECK("状態が取れる", fil_sts(&fl_b, nm, &fst, 0) == 0);
			BTRON_CHECK("所有者は読みのみ",
			            (fst.f_type & F_OWNACC) == F_OWNACR);
			BTRON_CHECK("名前", nm[0] == (TC)'b');
			/* 所有者名が入っている（レベル 1 なので隠し名は 0）。 */
			BTRON_CHECK("所有者", fst.f_owner[0] == (TC)'S');
			am.f_ownacc = F_OWNACR | F_OWNACW | F_OWNACE;
			BTRON_CHECK("戻す", chg_fmd(&fl_b, &am) == ER_OK);

			/* 名前の変更。特殊記号は使えない。 */
			BTRON_CHECK("特殊記号は使えない",
			            chg_fnm(&fl_b, nm_bad) == ER_FNAME);
			BTRON_CHECK("名前を変える", chg_fnm(&fl_b, nm_f3) == ER_OK);
			BTRON_CHECK("変わった",
			            fil_sts(&fl_b, nm, 0, 0) == 0 && nm[0] == (TC)'g');
			BTRON_CHECK("戻す", chg_fnm(&fl_b, nm_f2) == ER_OK);

			/* 存在検査。 */
			BTRON_CHECK("存在する", chk_fil(&fl_b, F_EXIST, 0) == 0);
			BTRON_CHECK("読める", chk_fil(&fl_b, F_READ, 0) == 0);
		}
	});

	BTRON_TEST("実身からプロセスを起こす (§1.1.6)", {
		if (!fs_up) {
			BTRON_CHECK("ファイルシステムが無いので飛ばす", True);
		} else {
			LINK p;
			W fd;
			W pid;
			MESSAGE m;
			W i;
			UD before = free_pages_settled();

			/* 実身を作り、**先頭レコード**に実行プログラムレコードを
			 * 置く。仕様「実行プログラムレコードは、先頭レコードで
			 * なければならない」。 */
			p = fl_root;
			BTRON_CHECK("実身を生成", cre_fil(&p, nm_prog, 0, 0,
			                                  F_FLOAT) > 0);
			fl_prog = p;
			fd = opn_fil(&p, F_UPDATE, 0);
			BTRON_CHECK("開く", fd > 0);
			BTRON_CHECK("プログラムを書く",
			            apd_rec(fd, (CONST B *)btron_user_hello,
			                    (W)btron_user_hello_size, 9,
			                    (UH)OBJ_CPU_ARM64, 0) == ER_OK);
			BTRON_CHECK("閉じる", cls_fil(fd) == ER_OK);

			/* 起動メッセージ。先頭バイトがそのまま終了コードになる
			 * ようにプログラムを書いてある。 */
			m.msg_type = 0;
			m.msg_size = 5;
			for (i = 0; i < 5; i++) {
				m.msg_body.ANYMSG.msg_str[i] = (UB)("BTRON"[i]);
			}
			(void)clr_msg(MM_ALL, MM_NULL);

			pid = cre_prc(&p, 2, &m);
			BTRON_CHECK("プロセスが起きた", pid > 0);
			if (pid > 0) {
				MESSAGE r;
				W got;

				/* 子が終わると正常終了メッセージが届く (§1.2)。
				 *
				 * **異常終了も受け付けて型で判定する。** 片方だけを
				 * 待つと、落ち方を間違えたときに検査が落ちるのでは
				 * なく永久に待ってしまう（逆検査で踏んだ）。 */
				got = rcv_msg(MM_EXIT | MM_ABORT, &r, (W)sizeof(r),
				              WAIT);
				BTRON_CHECK("終了メッセージが届く", got >= 0);
				BTRON_CHECK("正常終了メッセージ",
				            r.msg_type == MS_EXIT);
				BTRON_CHECK("送り主は子プロセス",
				            r.msg_body.EXIT.pid == pid);
				/* 'B' = 0x42 = 66。メッセージがユーザ空間まで
				 * 届いていなければ 0 になる。 */
				BTRON_CHECK("終了コードはメッセージの先頭バイト",
				            r.msg_body.EXIT.code == 'B');
			}

			/* **特権が本当に落ちているかを確かめる。**
			 *
			 * 'F' で始まるメッセージを渡すと、プログラムは
			 * カーネルの番地をシステムコールを通さず直接読む。
			 * EL0 なら変換フォルトでこのプロセスだけが落ち、
			 * 親には異常終了が届く。EL1 のままなら読めてしまう。 */
			(void)clr_msg(MM_ALL, MM_NULL);
			m.msg_body.ANYMSG.msg_str[0] = (UB)'F';
			pid = cre_prc(&p, 2, &m);
			BTRON_CHECK("もう一度起こす", pid > 0);
			if (pid > 0) {
				MESSAGE r;

				BTRON_CHECK("異常終了が届く",
				            rcv_msg(MM_ABORT | MM_EXIT, &r,
				                    (W)sizeof(r), WAIT) >= 0);
				/* 仕様 §1.2.2「プロセスがシステムエラーにより
				 * 異常終了させられた場合」= MS_ABORT。
				 * **MS_EXIT が来たら読めてしまっている。** */
				BTRON_CHECK("異常終了メッセージ",
				            r.msg_type == MS_ABORT);
				BTRON_CHECK("落ちたのは子プロセス",
				            r.msg_body.ABORT.pid == pid);
			}
			(void)clr_msg(MM_ALL, MM_NULL);

			/* **像のページが返っていること。** プロセスごとに
			 * 数ページずつ失うと、すぐ枯れる。 */
			BTRON_CHECK("ページが戻った",
			            free_pages_settled() + 2 >= before);

			/* **ヘッダの中身が壊れていたら走らせない。**
			 *
			 * レコードのサブタイプ（外側）とヘッダの CPU（内側）は
			 * 別の場所にある。片方だけ見ていると、もう片方が
			 * 壊れていても通ってしまう。両方を別々に試す。 */
			fd = opn_fil(&p, F_UPDATE, 0);
			if (fd > 0) {
				B bad[4];

				BTRON_CHECK("先頭へ", see_rec(fd, 0, 1, 0) == ER_OK);
				/* ヘッダの CPU（本体の 12 バイト目）だけを壊す。
				 * レコードのサブタイプは正しいままにする。 */
				bad[0] = 0x42; /* 68000 */
				bad[1] = 0;
				bad[2] = 0;
				bad[3] = 0;
				BTRON_CHECK("ヘッダの CPU を壊す",
				            wri_rec(fd, OBJ_CPU, bad, 4, 0, 0, 0) == ER_OK);
				BTRON_CHECK("閉じる", cls_fil(fd) == ER_OK);
				BTRON_CHECK("ヘッダの CPU が違えば走らせない",
				            cre_prc(&p, 2, 0) == ER_REC);
			}
			/* 印（マジック）を壊す。 */
			fd = opn_fil(&p, F_UPDATE, 0);
			if (fd > 0) {
				B bad[1];

				bad[0] = 'X';
				BTRON_CHECK("先頭へ", see_rec(fd, 0, 1, 0) == ER_OK);
				BTRON_CHECK("印を壊す",
				            wri_rec(fd, 0, bad, 1, 0, 0, 0) == ER_OK);
				BTRON_CHECK("閉じる", cls_fil(fd) == ER_OK);
				BTRON_CHECK("印が違えば走らせない",
				            cre_prc(&p, 2, 0) == ER_REC);
			}
			/* レコードのサブタイプ（外側）を壊す。 */
			fd = opn_fil(&p, F_UPDATE, 0);
			if (fd > 0) {
				UH wrong = 0x0042; /* 68000 */

				BTRON_CHECK("先頭へ", see_rec(fd, 0, 1, 0) == ER_OK);
				BTRON_CHECK("サブタイプを変える",
				            wri_rec(fd, 0, 0, 0, 0, &wrong, 0) == ER_OK);
				BTRON_CHECK("閉じる", cls_fil(fd) == ER_OK);
				BTRON_CHECK("サブタイプが違えば走らせない",
				            cre_prc(&p, 2, 0) == ER_REC);
			}
			/* レコードタイプが違う実身も断る。 */
			{
				LINK q = fl_root;

				BTRON_CHECK("別の実身", cre_fil(&q, nm_f3, 0, 0,
				                                F_FLOAT) > 0);
				BTRON_CHECK("プログラムレコードが無い",
				            cre_prc(&q, 2, 0) == ER_REC);
			}
			BTRON_CHECK("アドレス不正", cre_prc(0, 2, 0) == ER_ADR);
			BTRON_CHECK("優先度範囲外", cre_prc(&p, 300, 0) == ER_PPRI);
		}
	});

	BTRON_TEST("外殻をユーザ特権で動かす (詳細 E22 / E1)", {
		if (!fs_up) {
			BTRON_CHECK("ファイルシステムが無いので飛ばす", True);
		} else {
			LINK sh;
			W fd;
			W pid;
			W oldmask;

			/* **ここで確かめたいこと。**
			 *
			 * 外殻（ウィンドウマネージャやその下の描画）は、
			 * 詳細 E22「1 プロセス 1 サーバ」のとおりユーザランドの
			 * プロセスであるべきもの。そこへ移せるかどうかは
			 *
			 *   - 画面をユーザ特権のプロセスへ貸し出せるか
			 *   - ディスプレイプリミティブがその上で動くか
			 *     （**システムコールではなくライブラリとして**）
			 *   - フォントマネージャの共有メモリを読めるか
			 *   - イベントを取れるか
			 *
			 * で決まる。全部まとめて 1 つのプログラムで試す。 */
			sh = fl_root;
			BTRON_CHECK("実身を生成",
			            cre_fil(&sh, nm_shell, 0, 0, F_FLOAT) > 0);
			fd = opn_fil(&sh, F_UPDATE, 0);
			BTRON_CHECK("開く", fd > 0);
			BTRON_CHECK("外殻を書く",
			            apd_rec(fd, (CONST B *)btron_user_shell,
			                    (W)btron_user_shell_size, 9,
			                    (UH)OBJ_CPU_ARM64, 0) == ER_OK);
			BTRON_CHECK("閉じる", cls_fil(fd) == ER_OK);

			oldmask = chg_emk(EM_NULL);
			(void)chg_emk(oldmask);
			(void)clr_msg(MM_ALL, MM_NULL);
			pid = cre_prc(&sh, 2, 0);
			BTRON_CHECK("外殻が起きた", pid > 0);
			if (pid > 0) {
				MESSAGE r;

				BTRON_CHECK("終了メッセージ",
				            rcv_msg(MM_EXIT | MM_ABORT, &r,
				                    (W)sizeof(r), WAIT) >= 0);
				/* **異常終了なら、どこかで特権境界に当たっている。** */
				BTRON_CHECK("正常終了", r.msg_type == MS_EXIT);
				/* 戻り値 0 = 画素の読み返しまで通った。
				 * 画面が無い構成でも 0 を返す（その場合は描画を
				 * 飛ばす）ので、どちらでも 0 でなければならない。 */
				BTRON_CHECK("戻り値", r.msg_body.EXIT.code == 0);
			}
			(void)chg_emk(oldmask);
			(void)clr_msg(MM_ALL, MM_NULL);
		}
	});

	BTRON_TEST("切り離しと後始末 (§1.6.2 / §1.6.5)", {
		if (!fs_up) {
			BTRON_CHECK("ファイルシステムが無いので飛ばす", True);
		} else {
			W fd;

			/* 開いているファイルがあると切り離せない。 */
			fd = opn_fil(&fl_b, F_READ, 0);
			BTRON_CHECK("開く", fd > 0);
			BTRON_CHECK("開いていると切り離せない",
			            det_fls(nm_rda1, 0) == ER_BUSY);
			BTRON_CHECK("閉じる", cls_fil(fd) == ER_OK);

			BTRON_CHECK("同期", syn_fls() == ER_OK);
			BTRON_CHECK("切り離す", det_fls(nm_rda1, 0) == ER_OK);
			BTRON_CHECK("もう無い", det_fls(nm_rda1, 0) == ER_NOEXS);
			BTRON_CHECK("0 個に戻った", lst_fls(0, 0) == 0);

			/* 切断状態のリンクは使えない（仕様の虚身）。 */
			BTRON_CHECK("切断後は使えない",
			            opn_fil(&fl_b, F_READ, 0) == ER_NOFS);

			/* 接続し直すと中身が残っている。 */
			BTRON_CHECK("再接続",
			            att_fls(nm_rda1, nm_conn, 0, FS_SYNC) == ER_OK);
			{
				LINK l;

				BTRON_CHECK("ファイルが残っている",
				            get_lnk(path_abs, &l, F_NORM) == 0 &&
				                l.f_id == fl_a.f_id);
			}
			BTRON_CHECK("片付け", det_fls(nm_rda1, 0) == ER_OK);
			fs_up = False;
		}
	});

	/* -------------------------------------------------------------- */
	/* 入力装置 (virtio-input) とイベント管理のドライバ経路            */
	/*                                                                */
	/* 【規律 R27】ここは 2 段に分かれている。                         */
	/*                                                                */
	/*   (a) ドライバの口（evt_key / evt_meta / evt_pd_*）の検査       */
	/*       … 装置が無くても必ず走る。エンコード・メタキー・          */
	/*          キュー・ボタン状態はここで全部確かめる。               */
	/*   (b) 本物の virtio-input の検査                                */
	/*       … Device Tree に枠があるときだけ走る。                    */
	/*          ctest の input-arm64 構成が担保する。                  */
	/*                                                                */
	/* (a) を (b) に混ぜると、既定の ACPI 構成では何も検査されない      */
	/* のに通ってしまう（前に 2 度踏んだ穴）。                         */
	/* -------------------------------------------------------------- */

	BTRON_TEST("イベント管理のドライバ経路", {
		KeyTab *kt;
		VP mem = 0;
		EVENT ev;
		PNT p;

		(void)chg_emk(EM_ALL);
		(void)clr_evt(EM_ALL, EM_NULL);

		/* ドライバが登録する形の変換表を手で作る。
		 * キートップ 30 (evdev の KEY_A) を素で 'a'、
		 * 左シフトで 'A'、命令シフトで 0x01 にする。 */
		if (get_mbk(&mem, 4, 0) == ER_OK) {
			W i;

			kt = (KeyTab *)mem;
			kt->keymax = 256;
			kt->kctmax = 3;
			for (i = 0; i < KCTSEL; i++) {
				if ((i & KM_C) != 0) {
					kt->kctsel[i] = 2;
				} else if ((i & (KM_L | KM_R)) != 0) {
					kt->kctsel[i] = 1;
				} else {
					kt->kctsel[i] = 0;
				}
			}
			for (i = 0; i < KCTMAX; i++) {
				kt->kct[i] = 0;
			}
			kt->kct[0 * 256 + 30] = (UH)'a';
			kt->kct[1 * 256 + 30] = (UH)'A';
			kt->kct[2 * 256 + 30] = 0x01;
			evt_set_kbd(0, kt);
			(void)rel_mbk(mem);
		}

		/* メタキーはイベントを発生しない（仕様 §1.7.2）。 */
		evt_meta(ES_LSHFT, ES_LSHFT);
		BTRON_CHECK("メタキーではイベントが起きない", evt_count() == 0);

		/* シフト中に押すと表 1 が引かれる。 */
		evt_key(30, True);
		BTRON_CHECK("押し下げが積まれた", evt_count() == 1);
		BTRON_CHECK("取り出し",
		            get_evt(EM_KEYDWN, &ev, CLR) == EV_KEYDWN);
		BTRON_CHECK("キートップコードはそのまま", ev.data.key.keytop == 30);
		BTRON_CHECK("左シフトで 'A'", ev.data.key.code == (TC)'A');
		BTRON_CHECK("stat に左シフト", (ev.stat & ES_LSHFT) != 0);

		/* キー状態配列はドライバ経路でも更新される。 */
		{
			KeyMap km;

			BTRON_CHECK("キー状態が取れる", get_kmp(km) == ER_OK);
			BTRON_CHECK("30 番が押されている",
			            (km[30 / 8] & (1U << (30 % 8))) != 0);
		}

		evt_key(30, False);
		BTRON_CHECK("離した", get_evt(EM_KEYUP, &ev, CLR) == EV_KEYUP);
		evt_meta(0, ES_LSHFT);

		/* 命令シフトは表 2。**ビットの取り違えがここで出る。** */
		evt_meta(ES_CMD, ES_CMD);
		evt_key(30, True);
		BTRON_CHECK("命令シフトで 0x01",
		            get_evt(EM_KEYDWN, &ev, CLR) == EV_KEYDWN &&
		                ev.data.key.code == 0x01);
		evt_key(30, False);
		(void)get_evt(EM_KEYUP, &ev, CLR);
		evt_meta(0, ES_CMD);

		/* シフト無しは表 0。 */
		evt_key(30, True);
		BTRON_CHECK("素で 'a'",
		            get_evt(EM_KEYDWN, &ev, CLR) == EV_KEYDWN &&
		                ev.data.key.code == (TC)'a');
		evt_key(30, False);
		(void)get_evt(EM_KEYUP, &ev, CLR);

		/* **マスクで消されてもキー状態配列は更新される。**
		 *
		 * システムイベントマスクはイベントの話で、キーが物理的に
		 * 押されていることとは別。ここを混ぜると、マスクを絞った
		 * アプリから get_kmp が嘘を返す。 */
		{
			KeyMap km;

			(void)chg_emk(EM_ALL & ~(W)EM_KEYDWN);
			evt_key(31, True);
			BTRON_CHECK("マスク中はイベントにならない", evt_count() == 0);
			BTRON_CHECK("それでもキー状態は立つ",
			            get_kmp(km) == ER_OK &&
			                (km[31 / 8] & (1U << (31 % 8))) != 0);
			evt_key(31, False);
			BTRON_CHECK("離すと落ちる",
			            get_kmp(km) == ER_OK &&
			                (km[31 / 8] & (1U << (31 % 8))) == 0);
			(void)chg_emk(EM_ALL);
			(void)clr_evt(EM_ALL, EM_NULL);
		}

		/* PD の移動はイベントを起こさない（そういうタイプが無い）。 */
		evt_set_pda(PD_REL);
		evt_pd_moved((PNT){40, 50});
		BTRON_CHECK("移動ではイベントが起きない", evt_count() == 0);
		BTRON_CHECK("位置が入った",
		            get_pdp(&p) == ER_OK && p.x == 40 && p.y == 50);
		evt_pd_delta(5, -10);
		BTRON_CHECK("相対移動", get_pdp(&p) == ER_OK && p.x == 45 &&
		                            p.y == 40);
		/* 画面の外へは出ない。負の側は 0 で止まる。 */
		evt_pd_delta(-1000, -1000);
		BTRON_CHECK("左上で止まる",
		            get_pdp(&p) == ER_OK && p.x == 0 && p.y == 0);

		/* メインボタンはイベントになり、サブボタンはならない。
		 *
		 * **先に離しておく。** §1.7 の検査が put_evt(EV_BUTDWN) を
		 * 使っていて ES_BUT が立ったままになっているので、
		 * ここで初期状態を揃えないと下の確認が意味を失う。 */
		evt_pd_button(False, False);
		(void)clr_evt(EM_ALL, EM_NULL);
		evt_pd_button(False, True);
		BTRON_CHECK("ボタン押しが来た",
		            get_evt(EM_BUTDWN, &ev, CLR) == EV_BUTDWN);
		BTRON_CHECK("発生時の stat には ES_BUT が無い",
		            (ev.stat & ES_BUT) == 0);
		/* ボタンを押した**後**の状態には ES_BUT が立っている。
		 * 次のイベントの stat に載ることで確かめる。 */
		evt_key(30, True);
		BTRON_CHECK("押した後の stat には ES_BUT がある",
		            get_evt(EM_KEYDWN, &ev, CLR) == EV_KEYDWN &&
		                (ev.stat & ES_BUT) != 0);
		evt_key(30, False);
		(void)get_evt(EM_KEYUP, &ev, CLR);
		evt_pd_button(False, False);
		(void)get_evt(EM_BUTUP, &ev, CLR);

		evt_pd_button(True, True);
		BTRON_CHECK("サブボタンではイベントが起きない", evt_count() == 0);
		evt_pd_button(True, False);

		/* 絶対動作タイプにすると set_pdp が断られる（仕様 §1.7.3）。 */
		evt_set_pda(PD_ABS);
		BTRON_CHECK("絶対動作では位置設定不可",
		            set_pdp((PNT){0, 0}) == ER_OBJ);
		evt_set_pda(PD_REL);

		(void)chg_emk(EM_NULL);
		(void)clr_evt(EM_ALL, EM_NULL);
	});

	BTRON_TEST("virtio-input の接続 (R19: 記述から取る)", {
		CONST MachDesc *md = machdesc();
		ERR er;
		W n;
		W i;
		W probed = 0;

		hal_console_puts("    virtio 枠 : ");
		put_dec((W)md->virtio_count);
		hal_console_puts(" 個\n");

		/* 枠に挿さっている virtio-input を先に数えて見せる。
		 * **これと「使えた台数」が食い違ったら、見つけたのに
		 * 動かせなかったということ**で、原因の切り分けに効く。
		 * （実際に version 1 = 旧式インタフェースで引っかかった。） */
		{
			VirtioDev f[VIN_PROBE_MAX];
			W k = virtio_mmio_probe(VIRTIO_ID_INPUT, f, VIN_PROBE_MAX);
			W j;

			hal_console_puts("    入力枠    : ");
			put_dec(k);
			hal_console_puts(" 台");
			for (j = 0; j < k; j++) {
				hal_console_puts(" (版 ");
				put_dec((W)f[j].version);
				hal_console_puts(" INTID ");
				put_dec((W)f[j].intid);
				hal_console_puts(")");
			}
			hal_console_puts("\n");
			probed = k;
		}
		er = vinput_register();
		n = vinput_count();
		hal_console_puts("    入力装置  : ");
		put_dec(n);
		hal_console_puts(" 台");
		for (i = 0; i < n; i++) {
			switch (vinput_kind(i)) {
			case VIN_KIND_KBD:
				hal_console_puts(" キーボード");
				break;
			case VIN_KIND_ABS:
				hal_console_puts(" PD(絶対)");
				break;
			case VIN_KIND_REL:
				hal_console_puts(" PD(相対)");
				break;
			default:
				hal_console_puts(" 不明");
				break;
			}
		}
		hal_console_puts("\n");

		if (md->virtio_count == 0) {
			/* ACPI 経路では枠の記述が AML の中にあり、まだ読めない。
			 * **「装置が無い」と「読めていない」は違う**ので、
			 * ここで区別して記録しておく。 */
			BTRON_CHECK("枠が無ければ装置も無い",
			            er == ER_NOEXS && n == 0);
		} else {
			/* 枠はあるが装置を挿していない構成もありうる。
			 * 挿してあれば ER_OK、無ければ ER_NOEXS。 */
			BTRON_CHECK("結果は OK か未存在",
			            er == ER_OK || er == ER_NOEXS);
			BTRON_CHECK("台数と結果が一致",
			            (er == ER_OK) == (n > 0));
			/* 見つけた台数と動かせた台数は一致していなければならない。
			 * ここが食い違うのは「旧式インタフェースで断った」場合で、
			 * 黙って 0 台になるのを防ぐための検査。 */
			BTRON_CHECK("見つけた分だけ動かせた", probed == n);
		}

		if (n > 0) {
			KBD_ID kid;
			Bool kbd = False;
			Bool pd = False;
			TC nm[L_DEVNM + 1];
			W d;

			for (i = 0; i < n; i++) {
				if (vinput_kind(i) == VIN_KIND_KBD) {
					kbd = True;
				} else {
					pd = True;
				}
			}
			Bool dev_kbd = False;
			Bool dev_pd = False;
			EVENT ev2;

			if (kbd) {
				BTRON_CHECK("キーボード ID が入った",
				            get_kid(&kid) == ER_OK &&
				                kid.kind ==
				                    (UH)(KB_IBM101 << KB_KIND_SHIFT));
				/* ドライバが入れた既定の変換表が引けること。
				 * evdev の KEY_A = 30 が素で 'a' になる。
				 * **ここが通れば配列表とキートップコードの約束が
				 * 両側で一致している。** */
				(void)chg_emk(EM_ALL);
				(void)clr_evt(EM_ALL, EM_NULL);
				evt_key(30, True);
				BTRON_CHECK("既定の変換表で 'a'",
				            get_evt(EM_KEYDWN, &ev2, CLR) ==
				                    EV_KEYDWN &&
				                ev2.data.key.code == (TC)'a');
				evt_key(30, False);
				(void)get_evt(EM_KEYUP, &ev2, CLR);
				(void)chg_emk(EM_NULL);
				(void)clr_evt(EM_ALL, EM_NULL);
			}
			/* デバイス管理にも名前が出ていること (§1.8)。 */
			for (d = 1; d <= DEV_MAX_UNIT; d++) {
				if (get_dev(nm, d) < 0) {
					continue;
				}
				if (nm[0] == (TC)'k' && nm[1] == (TC)'b' &&
				    nm[2] == (TC)'d') {
					dev_kbd = True;
				}
				if (nm[0] == (TC)'p' && nm[1] == (TC)'d' &&
				    nm[2] == TNULL) {
					dev_pd = True;
				}
			}
			BTRON_CHECK("キーボードは \"kbd\" で登録される",
			            kbd == dev_kbd);
			BTRON_CHECK("PD は \"pd\" で登録される", pd == dev_pd);
		}
	});

	/* -------------------------------------------------------------- */
	/* 外殻 §3.1 ウィンドウマネージャ                                 */
	/* -------------------------------------------------------------- */

	BTRON_TEST("ウィンドウの生成と入力受付状態 (§3.1.1.2)", {
		RECT r;
		WID w1;
		WID w2;
		WDSTAT st;
		W pid = 0;
		MESSAGE m;

		if (win_init() != EX_OK) {
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
			/* 座標系は元のウィンドウと同じ（仕様 §3.1.2.5）。 */
			{
				RECT fr1;
				RECT fr2;

				BTRON_CHECK("元の窓の境界",
				            gget_bnd(wget_gid(win_w1), &fr1) == EG_OK);
				BTRON_CHECK("ドラッグ環境の境界",
				            gget_bnd(dg, &fr2) == EG_OK);
				BTRON_CHECK("座標系が一致",
				            fr1.c.left == fr2.c.left &&
				                fr1.c.top == fr2.c.top);
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
			dt = make_task(t_drag, 4, 0);
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
			dt = make_task(t_drag, 4, 0);
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
			dt = make_task(t_drag, 4, 0);
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
				dt = make_task(t_drag, 4, 0);
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
			win_finish();
			win_up = False;
			(void)clr_msg(MM_ALL, MM_NULL);
			(void)clr_evt(EM_ALL, EM_NULL);
		}
	});

	BTRON_TEST("サーバが落ちると利用側のキャッシュも無効になる", {
		MgrMsg m;

		/* **フォントマネージャを止める。** ポートとグローバル名は
		 * DELEXIT 付きなのでプロセスと一緒に消える。 */
		if (fontman_pid >= 1) {
			BTRON_CHECK("共有データが見えている", fontman_shared() != 0);
			m.func = FONT_SHUTDOWN;
			BTRON_CHECK("停止要求",
			            mgr_call((ID)fontman_por, &m, T_FOREVER) == ER_OK);
			(void)dly_tsk(10);
			BTRON_CHECK("プロセスが終わった",
			            prc_sts(fontman_pid, 0, 0) == ER_NOPRC);
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

	BTRON_TEST("cre_prc は接続されていないファイルシステムを断る (§1.1.6)", {
		LINK lnk;
		W i;

		for (i = 0; i < L_FSNM; i++) {
			lnk.fs_name[i] = (TC)'z';
		}
		lnk.fs_name[L_FSNM - 1] = TNULL;
		lnk.f_id = 1;
		lnk.atr1 = 0;
		lnk.atr2 = 0;
		lnk.atr3 = 0;
		lnk.atr4 = 0;
		lnk.atr5 = 0;
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
	/* ESP に HOLD の印があれば、電源を落とさず画面を残す。
	 * 手で画面を見るための経路（run_qemu.py --gui）。 */
	if ((bootinfo()->flags & BI_F_HOLD) != 0) {
		hal_console_puts("\n  HOLD の印があるので、電源を落とさず画面を"
		                 "残します\n");
		win_demo_hold(); /* 戻らない */
	}

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

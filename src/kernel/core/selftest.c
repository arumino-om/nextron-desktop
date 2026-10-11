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

#include <btron/machdesc.h>
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

	BTRON_TEST("中心核が動いている", {
		BTRON_CHECK("起動済み", knl_running() == True);
		BTRON_CHECK("自タスクがいる", tk_get_tid() >= 1);
		BTRON_CHECK("アイドルではない", knl_cur != &knl_idle);
	});
}

/* ------------------------------------------------------------------ */
/* 入口                                                                */
/* ------------------------------------------------------------------ */

LOCAL void test_task(INT stacd, VP exinf)
{
	(void)stacd;
	(void)exinf;
	run_tests();
	hal_exit(btron_test_report());
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

/*
 * core_sync.c — 中心核 セマフォとイベントフラグ
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

LOCAL ID sem1, flg1;

/* ------------------------------------------------------------------ */
/* ④ セマフォの待ち行列                                               */
/* ------------------------------------------------------------------ */

LOCAL void t_sem_wait(INT stacd, VP exinf)
{
	(void)exinf;
	if (tk_wai_sem(sem1, 1, TMO_FEVR) == E_OK) {
		tst_seq_put((B)stacd);
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
		tst_seq_put((B)stacd);
	}
	tk_ext_tsk();
}

BTRON_TEST_GROUP("中心核 セマフォとイベントフラグ", 110, TST_PHASE_KERNEL, tst_core_sync)
{
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
		tst_seq_reset();
		i1 = tst_make_task(t_sem_wait, 9, 0);
		i2 = tst_make_task(t_sem_wait, 7, 0);
		i3 = tst_make_task(t_sem_wait, 8, 0);
		(void)tk_sta_tsk(i1, '9');
		(void)tk_sta_tsk(i2, '7');
		(void)tk_sta_tsk(i3, '8');
		(void)tk_dly_tsk(20); /* 3 つとも待ち行列に並ぶ */
		BTRON_CHECK("全員待ち", tst_seq_eq(""));
		(void)tk_sig_sem(sem1, 3);
		(void)tk_dly_tsk(20);
		if (!tst_seq_eq("789")) {
			tst_seq_show("789");
		}
		BTRON_CHECK("優先度順", tst_seq_eq("789"));
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
		tst_seq_reset();
		id = tst_make_task(t_sem_wait, 7, 0);
		(void)tk_sta_tsk(id, 'd');
		(void)tk_dly_tsk(20);
		BTRON_CHECK("セマフォ待ち",
		            tk_ref_tsk(id, &r) == E_OK && r.tskwait == TTW_SEM
		                && r.wid == sem1);
		BTRON_CHECK("削除", tk_del_sem(sem1) == E_OK);
		(void)tk_dly_tsk(10);
		/* E_DLT で起きたので tst_seq_put しない */
		BTRON_CHECK("獲得していない", tst_seq_eq(""));
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
		tst_seq_reset();
		i1 = tst_make_task(t_flg_wait, 7, 0);
		i2 = tst_make_task(t_flg_wait, 7, 0);
		(void)tk_sta_tsk(i1, '1');
		(void)tk_sta_tsk(i2, '2');
		(void)tk_dly_tsk(20);
		BTRON_CHECK("両方待ち", tst_seq_eq(""));
		(void)tk_set_flg(flg1, 0x3U);
		(void)tk_dly_tsk(20);
		if (!tst_seq_eq("12")) {
			tst_seq_show("12");
		}
		BTRON_CHECK("両方解除", tst_seq_eq("12"));
		(void)tk_del_flg(flg1);

		/* TA_WSGL: 2 つめの待ちは条件に関係なく E_OBJ */
		c.flgatr = TA_TFIFO | TA_WSGL;
		flg1 = tk_cre_flg(&c);
		i1 = tst_make_task(t_flg_wait, 7, 0);
		(void)tk_sta_tsk(i1, '1');
		(void)tk_dly_tsk(20);
		BTRON_CHECK("2 つめは E_OBJ",
		            tk_wai_flg(flg1, 0x3U, TWF_ANDW, &p, TMO_POL) == E_OBJ);
		(void)tk_del_flg(flg1);
		(void)tk_dly_tsk(10);
	});

}

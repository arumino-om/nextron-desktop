/*
 * core_task.c — 中心核 タスク管理とスケジューラ
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

/* 検査の間だけ使う ID。各テストの先頭で作り、終わりで消す。 */
LOCAL ID ta, tb, tc, th;

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
	tst_seq_put('h'); /* 1 回目 */
	(void)tk_slp_tsk(TMO_FEVR);
	tst_seq_put('H'); /* 起こされた */
	(void)tk_slp_tsk(TMO_FEVR);
	tk_ext_tsk();
}

LOCAL void t_preempt_a(INT stacd, VP exinf)
{
	(void)stacd;
	(void)exinf;
	tst_seq_put('a');
	(void)tk_wup_tsk(th); /* ここでプリエンプトされる */
	tst_seq_put('A');         /* 先頭に戻っていれば B より先に来る */
	tk_ext_tsk();
}

LOCAL void t_rel_b(INT stacd, VP exinf)
{
	(void)stacd;
	(void)exinf;
	tst_seq_put('b');
	(void)tk_wup_tsk(ta); /* A を待ち解除。同一優先度なので切替は起きない */
	tst_seq_put('B');
	tk_ext_tsk();
}

LOCAL void t_rel_c(INT stacd, VP exinf)
{
	(void)stacd;
	(void)exinf;
	tst_seq_put('c');
	tk_ext_tsk();
}

BTRON_TEST_GROUP("中心核 タスク管理とスケジューラ", 100, TST_PHASE_KERNEL, tst_core_task)
{
	BTRON_TEST("タスクの生成と状態 (D10)", {
		T_RTSK r;
		ID id = tst_make_task(tst_t_mark_b, 20, (VP)(UD)0x1234);

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
		c.task = tst_t_mark_b;
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

		tst_seq_reset();
		/* 自分は pri 5。作るのは 8, 6, 7 の順。走るのは 6, 7, 8 の順。 */
		i1 = tst_make_task(tst_t_tag, 8, 0);
		i2 = tst_make_task(tst_t_tag, 6, 0);
		i3 = tst_make_task(tst_t_tag, 7, 0);
		BTRON_CHECK("生成", i1 > 0 && i2 > 0 && i3 > 0);
		(void)tk_sta_tsk(i1, '8');
		(void)tk_sta_tsk(i2, '6');
		(void)tk_sta_tsk(i3, '7');
		/* 自分より低い優先度なので、ここで寝るまで走らない */
		BTRON_CHECK("まだ走っていない", tst_seq_eq(""));
		(void)tk_dly_tsk(20);
		if (!tst_seq_eq("678")) {
			tst_seq_show("678");
		}
		BTRON_CHECK("優先度順", tst_seq_eq("678"));
	});

	BTRON_TEST("同一優先度は FCFS", {
		ID i1;
		ID i2;
		ID i3;

		tst_seq_reset();
		i1 = tst_make_task(tst_t_tag, 6, 0);
		i2 = tst_make_task(tst_t_tag, 6, 0);
		i3 = tst_make_task(tst_t_tag, 6, 0);
		(void)tk_sta_tsk(i1, 'x');
		(void)tk_sta_tsk(i2, 'y');
		(void)tk_sta_tsk(i3, 'z');
		(void)tk_dly_tsk(20);
		if (!tst_seq_eq("xyz")) {
			tst_seq_show("xyz");
		}
		BTRON_CHECK("起動順", tst_seq_eq("xyz"));
	});

	/* ---- ここが仕様の細則。取り違えやすい ---- */

	BTRON_TEST("プリエンプトは同一優先度の先頭に戻る (仕様)", {
		tst_seq_reset();
		th = tst_make_task(t_high, 5, 0);
		ta = tst_make_task(t_preempt_a, 6, 0);
		tb = tst_make_task(tst_t_mark_b, 6, 0);
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
		if (!tst_seq_eq("haHAb")) {
			tst_seq_show("haHAb");
		}
		BTRON_CHECK("先頭に戻る", tst_seq_eq("haHAb"));
	});

	BTRON_TEST("待ち解除は同一優先度の末尾に付く (仕様)", {
		tst_seq_reset();
		ta = tst_make_task(tst_t_rel_a, 6, 0);
		tb = tst_make_task(t_rel_b, 6, 0);
		tc = tst_make_task(t_rel_c, 6, 0);
		BTRON_CHECK("生成", ta > 0 && tb > 0 && tc > 0);
		(void)tk_sta_tsk(ta, 0);
		(void)tk_sta_tsk(tb, 0);
		(void)tk_sta_tsk(tc, 0);
		(void)tk_dly_tsk(30);
		/* a: A が走って寝た / b: B が走って A を起こした（A は末尾へ）
		 * B: B が続行 / c: 次は C / A: 最後に A
		 * 先頭に付ける実装だと "abBAc" になる。 */
		if (!tst_seq_eq("abBcA")) {
			tst_seq_show("abBcA");
		}
		BTRON_CHECK("末尾に付く", tst_seq_eq("abBcA"));
	});

	BTRON_TEST("起床要求のキューイング (仕様)", {
		T_RTSK r;
		ID id = tst_make_task(tst_t_mark_b, 20, 0); /* 低優先度。走らせない */

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

}

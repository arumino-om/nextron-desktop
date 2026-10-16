/*
 * proc.c — 周辺核 プロセス / タスク管理 (§1.1)
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

/* ------------------------------------------------------------------ */
/* 周辺核 §1.1「プロセス / タスク管理」の検査用のタスク                 */
/*                                                                     */
/* **これらは初期プロセスの中で走る** (TST_PHASE_PROC)。中心核の検査     */
/* (TST_PHASE_KERNEL) はプロセスに属さないタスクで走らせる。タイムスライス */
/* で、プロセスに属するタスクは prc_tick() が 10ms ごとに同一優先度の     */
/* 末尾へ回す。中心核の検査は「同一優先度内の実行順」を 1 文字ずつ        */
/* 比べているので、途中で回されると落ちてしまう。                        */
/* ------------------------------------------------------------------ */

LOCAL W prc_grandchild;  /* 子プロセスが作った孫のプロセス ID */

LOCAL ERR prc_level_err; /* レベルの低いプロセスから親を落とそうとした結果 */

LOCAL ERR prc_chgusr_er;

LOCAL W prc_rr_done;     /* ラウンドロビン検査の終了したサブタスク数 */

/* すぐ終わるプロセス。資源が戻ることの検査に使う。 */
LOCAL W p_exit_now(MESSAGE *msg)
{
	(void)msg;
	return 0; /* MAIN() からの return は ext_prc() と同等（仕様） */
}

/* 孫を作って寝る。親子関係の引き継ぎ (§1.1.1) の検査に使う。 */
LOCAL W p_parent(MESSAGE *msg)
{
	(void)msg;
	prc_grandchild = prc_create(tst_p_sleeper, 1, 0, 0);
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
		tst_seq_put((B)arg);
		tst_spin_ms(PRC_SLICE_MS * 2);
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

BTRON_TEST_GROUP("周辺核 プロセス / タスク管理 (§1.1)", 200, TST_PHASE_PROC, tst_proc)
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
		W a = prc_create(tst_p_sleeper, 1, 0, 0);
		W b = prc_create(tst_p_sleeper, 1, 0, 0);
		W c = prc_create(tst_p_sleeper, 1, 0, 0);
		W d;

		BTRON_CHECK("3 つ作れた", a >= 1 && b > a && c > b);
		BTRON_CHECK("連番", b == a + 1 && c == b + 1);
		BTRON_CHECK("中央を終了", ter_prc(b, 0, TERM_NRM) == ER_OK);
		d = prc_create(tst_p_sleeper, 1, 0, 0);
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
		W child = prc_create(tst_p_sleeper, 1, 0, 0);

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
		W b1 = prc_create(tst_p_sleeper, 128, 0, 0); /* 群B の最高 */
		W b2 = prc_create(tst_p_sleeper, 191, 0, 0); /* 群B の最低 */
		W c1 = prc_create(tst_p_sleeper, 192, 0, 0); /* 群C の最高 */
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
		W p1 = prc_create(tst_p_sleeper, 1, 0, 0);
		W p2 = prc_create(tst_p_sleeper, 1, 0, 0);
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
		tst_spin_ms(PRC_SLICE_MS * 3);
		BTRON_CHECK("再取得", get_inf(P_SELF, &i2) == ER_OK);
		BTRON_CHECK("CPU 時間が増えた", i2.utime > i1.utime);
		BTRON_CHECK("経過時間は巻き戻らない", i2.etime >= i1.etime);
	});

	BTRON_TEST("同一優先度はラウンドロビン (§1.1.3)", {
		W s1;
		W s2;
		W deadline;

		tst_seq_reset();
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
		BTRON_CHECK("両方が記録を残した", tst_seq_n == 6);
		/* 時分割が無ければ "aaabbb" で変化は 1 回。 */
		BTRON_CHECK("交互に走った", tst_seq_transitions() >= 2);
		if (tst_seq_transitions() < 2) {
			tst_seq_show("abab...");
		}
	});

	BTRON_TEST("プロセスの生成と終了で資源が漏れない", {
		UD free0;
		UD used0;
		W n0;
		W i;
		Bool ok = True;

		free0 = tst_free_pages_settled();
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
		BTRON_CHECK("物理ページが戻った", tst_free_pages_settled() == free0);
		BTRON_CHECK("カーネルヒープが戻った", kheap_used() == used0);
	});

	BTRON_TEST("プロセス数の上限 (§1.1.9 ER_NOSPC)", {
		W ids[PRC_MAX_PRCID];
		W made = 0;
		W i;

		for (i = 0; i < PRC_MAX_PRCID; i++) {
			ids[i] = prc_create(tst_p_sleeper, 1, 0, 0);
			if (ids[i] < 1) {
				break;
			}
			made++;
		}
		BTRON_CHECK("自分の分だけ少ない", made == PRC_MAX_PRCID - 1);
		BTRON_CHECK("上限を超えると ER_NOSPC",
		            prc_create(tst_p_sleeper, 1, 0, 0) == ER_NOSPC);
		for (i = 0; i < made; i++) {
			BTRON_CHECK("片付け", ter_prc(ids[i], 0, TERM_NRM) == ER_OK);
		}
		BTRON_CHECK("自分だけ残った", prc_count() == 1);
	});

}

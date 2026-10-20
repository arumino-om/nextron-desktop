/*
 * core_sched.c — 中心核 待ち状態と優先度の操作
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

IMPORT UD knl_tick_count(void);

BTRON_TEST_GROUP("中心核 待ち状態と優先度の操作", 120, TST_PHASE_KERNEL, tst_core_sched)
{
	BTRON_TEST("強制待ち状態は待ち状態と直交する (仕様)", {
		T_RTSK r;
		ID id = tst_make_task(tst_t_rel_a, 20, 0); /* 低優先度で寝るだけ */

		tst_seq_reset();
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
		BTRON_CHECK("まだ走っていない", tst_seq_eq("a"));

		/* ネストする */
		BTRON_CHECK("sus 2", tk_sus_tsk(id) == E_OK);
		BTRON_CHECK("ネスト数", tk_ref_tsk(id, &r) == E_OK && r.suscnt == 2);
		BTRON_CHECK("rsm 1", tk_rsm_tsk(id) == E_OK);
		BTRON_CHECK("まだ強制待ち", tk_ref_tsk(id, &r) == E_OK && r.tskstat == TTS_SUS);
		BTRON_CHECK("frsm", tk_frsm_tsk(id) == E_OK);
		(void)tk_dly_tsk(10);
		BTRON_CHECK("走った", tst_seq_eq("aA"));

		BTRON_CHECK("自タスクは強制待ちにできない", tk_sus_tsk(TSK_SELF) == E_OBJ);
		(void)tk_del_tsk(id);
	});

	BTRON_TEST("tk_rel_wai と tk_ter_tsk", {
		T_RTSK r;
		ID id = tst_make_task(tst_t_rel_a, 20, 0);

		tst_seq_reset();
		(void)tk_sta_tsk(id, 0);
		(void)tk_dly_tsk(20);
		BTRON_CHECK("待っている", tk_ref_tsk(id, &r) == E_OK && r.tskstat == TTS_WAI);
		/* 強制解除。待っていたシステムコールは E_RLWAI を返す */
		BTRON_CHECK("rel_wai", tk_rel_wai(id) == E_OK);
		(void)tk_dly_tsk(10);
		/* tst_t_rel_a は E_RLWAI でも tst_seq_put('A') する実装なので走る */
		BTRON_CHECK("解除された", tst_seq_eq("aA"));
		BTRON_CHECK("待っていないと E_OBJ", tk_rel_wai(id) == E_OBJ);
		(void)tk_del_tsk(id);

		/* 待ち状態のタスクを強制終了できる */
		tst_seq_reset();
		id = tst_make_task(tst_t_rel_a, 20, 0);
		(void)tk_sta_tsk(id, 0);
		(void)tk_dly_tsk(20);
		BTRON_CHECK("ter_tsk", tk_ter_tsk(id) == E_OK);
		BTRON_CHECK("休止状態", tk_ref_tsk(id, &r) == E_OK && r.tskstat == TTS_DMT);
		(void)tk_dly_tsk(10);
		BTRON_CHECK("走らなかった", tst_seq_eq("a"));
		BTRON_CHECK("休止状態なら削除できる", tk_del_tsk(id) == E_OK);
	});

	BTRON_TEST("優先度変更とレディキューの移動", {
		T_RTSK r;
		ID id = tst_make_task(tst_t_tag, 20, 0);

		tst_seq_reset();
		(void)tk_sta_tsk(id, 'p');
		BTRON_CHECK("低優先度なので走らない", tst_seq_eq(""));
		/* 自分より高い優先度にすると即座にプリエンプトされる */
		BTRON_CHECK("chg_pri", tk_chg_pri(id, 4) == E_OK);
		if (!tst_seq_eq("p")) {
			tst_seq_show("p");
		}
		BTRON_CHECK("走った", tst_seq_eq("p"));
		BTRON_CHECK("範囲外", tk_chg_pri(TSK_SELF, TK_MAX_TSKPRI + 1) == E_PAR);
		BTRON_CHECK("自タスク参照", tk_ref_tsk(TSK_SELF, &r) == E_OK);
		(void)tk_del_tsk(id);
	});

	BTRON_TEST("ラウンドロビン (tk_rot_rdq)", {
		ID i1;
		ID i2;
		ID i3;
		T_RTSK r;

		tst_seq_reset();
		/* 自分と同じ優先度に 3 つ並べ、自分で譲る */
		(void)tk_ref_tsk(TSK_SELF, &r);
		i1 = tst_make_task(tst_t_tag, r.tskpri, 0);
		i2 = tst_make_task(tst_t_tag, r.tskpri, 0);
		i3 = tst_make_task(tst_t_tag, r.tskpri, 0);
		(void)tk_sta_tsk(i1, '1');
		(void)tk_sta_tsk(i2, '2');
		(void)tk_sta_tsk(i3, '3');
		BTRON_CHECK("同一優先度なので走らない", tst_seq_eq(""));
		/* 自分の優先度を回す = 次のタスクに譲る。
		 * 戻ってきたときには 3 つとも終わっている。 */
		BTRON_CHECK("rot_rdq", tk_rot_rdq(0) == E_OK);
		if (!tst_seq_eq("123")) {
			tst_seq_show("123");
		}
		BTRON_CHECK("順に走った", tst_seq_eq("123"));
		BTRON_CHECK("範囲外", tk_rot_rdq(TK_MAX_TSKPRI + 1) == E_PAR);
	});

	BTRON_TEST("ディスパッチ禁止", {
		ID id;

		tst_seq_reset();
		id = tst_make_task(tst_t_tag, 4, 0); /* 自分より高い優先度 */
		BTRON_CHECK("dis_dsp", tk_dis_dsp() == E_OK);
		(void)tk_sta_tsk(id, 'd');
		/* 高優先度でも切り替わらない */
		BTRON_CHECK("保留されている", tst_seq_eq(""));
		BTRON_CHECK("ena_dsp", tk_ena_dsp() == E_OK);
		/* 許可した瞬間に切り替わる */
		BTRON_CHECK("走った", tst_seq_eq("d"));
		(void)tk_del_tsk(id);
	});

	BTRON_TEST("割込みとタイマ (D8 / T1-01 / T1-02)", {
		UD c0 = intc_count();
		UD t0 = knl_tick_count();
		SYSTIM m0;
		SYSTIM m1;

		/* **割込みコントローラの「版」はアーキで値が違う。** arm64 は
		 * GIC の版 (2 / 3)、x86_64 は Local APIC の版レジスタ
		 * （xAPIC は 0x10 以上）。見たいのは「コントローラが応答して
		 * いること」なので、アーキごとに当てる値を変える。 */
#if defined(__aarch64__)
		BTRON_CHECK("GIC", hal_intc_version() == 2 || hal_intc_version() == 3);
#else
		BTRON_CHECK("APIC", hal_intc_version() >= 0x10U);
#endif
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

}

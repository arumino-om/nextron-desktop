/*
 * core_time.c — 中心核 周期ハンドラとアラームハンドラ
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

LOCAL W cyc_hits;

LOCAL W alm_hits;

LOCAL ID cyc_waiter;

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

BTRON_TEST_GROUP("中心核 周期ハンドラとアラームハンドラ", 140, TST_PHASE_KERNEL, tst_core_time)
{
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

}

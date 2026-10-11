/*
 * time.c — 時間管理と中心核の起動
 *
 * 【システム時刻の表現】仕様の T_SYSTIM は {W hi; UW lo} の構造体だが、
 * A5 で「64bit 化する」と決めたので単一の 64bit 整数にした（kernel.h）。
 * 単位はミリ秒。μT-Kernel 3.0 も 64bit 環境では `typedef D SYSTIM` を認めている。
 *
 * BTRON3 の `STIME`（1985-01-01 GMT からの**秒**、A5）とは単位が違う。
 * 周辺核が境界で変換する。混ぜると 1000 倍ずれる。
 *
 * 【tick】ハードウェアタイマの周期割込みで刻む。周期は KNL_TICK_HZ。
 * タイムアウトの検査は TCB を全走査する。TK_MAX_TSKID = 64 なので
 * 1kHz で 64,000 回/秒、無視できる。ソート済みキューにするのは
 * タスク数が増えてからでよい（D9 と一緒に見直す）。
 */

#include <btron/machdesc.h>

#include "knl.h"

#define KNL_TICK_HZ 1000 /* 1 tick = 1ms。仕様のシステム時刻の基準時間 */

EXPORT SYSTIM knl_otm;           /* 起動からの経過ミリ秒 */
EXPORT SYSTIM knl_systim_offset; /* tk_set_tim で設定された分 */

LOCAL UD tick_count;

EXPORT void knl_timer_handler(UW intid, VP arg)
{
	(void)intid;
	(void)arg;

	hal_timer_tick_reload();
	tick_count++;
	knl_otm += (SYSTIM)hal_timer_tick_ms();
	knl_timeout_check();
	/* 切替は intc_dispatch() の最後で行う。ここではしない。 */
}

EXPORT ER tk_get_tim(SYSTIM *pk_tim)
{
	UD state;

	if (pk_tim == 0) {
		return E_PAR;
	}
	state = hal_int_disable();
	*pk_tim = knl_systim_offset + knl_otm;
	hal_int_restore(state);
	return E_OK;
}

EXPORT ER tk_set_tim(CONST SYSTIM *pk_tim)
{
	UD state;

	if (pk_tim == 0) {
		return E_PAR;
	}
	state = hal_int_disable();
	knl_systim_offset = *pk_tim - knl_otm;
	hal_int_restore(state);
	return E_OK;
}

EXPORT ER tk_get_otm(SYSTIM *pk_tim)
{
	UD state;

	if (pk_tim == 0) {
		return E_PAR;
	}
	state = hal_int_disable();
	*pk_tim = knl_otm;
	hal_int_restore(state);
	return E_OK;
}

EXPORT UD knl_tick_count(void)
{
	return tick_count;
}

/* ------------------------------------------------------------------ */
/* 中心核の起動                                                        */
/* ------------------------------------------------------------------ */

IMPORT void knl_sem_init(void);
IMPORT void knl_flg_init(void);

EXPORT ER knl_init(void)
{
	CONST MachDesc *md = machdesc();
	ER er;
	W i;

	knl_sched_init();
	knl_sem_init();
	knl_flg_init();

	for (i = 0; i <= TK_MAX_TSKID; i++) {
		knl_tcb[i].used = False;
		knl_tcb[i].state = 0;
		knl_tcb[i].queue = 0;
		knl_tcb[i].tskid = i;
	}

	/* アイドルタスク。レディキューには入らず、他に誰もいないときに走る。
	 * 走っている文脈（起動スタック）をそのまま使うので sp は knl_start()
	 * の中で hal_ctx_switch が埋める。 */
	knl_idle.tskid = 0;
	knl_idle.pri = TK_MAX_TSKPRI + 1; /* レディキューの範囲外。番兵 */
	knl_idle.bpri = knl_idle.pri;
	knl_idle.itskpri = knl_idle.pri;
	knl_idle.state = TTS_RDY;
	knl_idle.used = True;
	knl_idle.queue = 0;
	knl_cur = &knl_idle;

	knl_otm = 0;
	knl_systim_offset = 0;
	tick_count = 0;

	er = intc_init();
	if (er != E_OK) {
		return er;
	}
	er = intc_set_handler(md->timer_intid[MD_TIMER_NONSECURE], knl_timer_handler, 0);
	if (er != E_OK) {
		return er;
	}
	return hal_timer_tick_start(KNL_TICK_HZ);
}

EXPORT void knl_start(void)
{
	UD state;

	state = hal_int_disable();
	knl_on = True;
	knl_cur = &knl_idle;
	knl_idle.state = TTS_RUN;
	hal_int_restore(state);

	hal_int_enable();

	/* ここから先がアイドルループ。
	 * 他に実行できるタスクがあれば knl_dispatch() が切り替える。
	 * 戻ってくるのは全タスクが待ちに入ったときだけ。 */
	for (;;) {
		UD s = hal_int_disable();

		knl_dispatch();
		hal_int_restore(s);
		hal_cpu_idle();
	}
}

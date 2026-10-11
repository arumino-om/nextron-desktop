/*
 * tasksync.c — タスク付属同期機能 (μT-Kernel 3.0)
 *
 * tk_slp_tsk / tk_wup_tsk / tk_can_wup / tk_rel_wai /
 * tk_sus_tsk / tk_rsm_tsk / tk_frsm_tsk / tk_dly_tsk
 *
 * 【起床要求のキューイング】仕様の要点。
 * タスクは TCB に起床要求キューイング数 (wupcnt) を持つ。
 *
 *   起床待ちでないタスクに tk_wup_tsk  → wupcnt += 1
 *   tk_slp_tsk で wupcnt >= 1          → wupcnt -= 1 して**待たずに続行**
 *   tk_slp_tsk で wupcnt == 0          → 待ち状態になる
 *
 * これを忘れると「先に起こしてから寝る」順序で永久に待つ。
 *
 * 【待ち状態と強制待ち状態は直交する】仕様が明示している。
 * 資源待ちのタスクに強制待ちを要求して二重待ちになっても、
 * 資源の割り付け条件は変わらない。だから二重待ちのタスクも待ち行列に
 * 並んだままにして、解除されたら強制待ち状態へ落とす（knl_wait_release）。
 */

#include "knl.h"

EXPORT ER tk_slp_tsk(TMO tmout)
{
	UD state;
	TCB *me;
	ER er;

	if (tmout < TMO_FEVR) {
		return E_PAR;
	}
	state = hal_int_disable();
	me = knl_cur;
	if (me == &knl_idle) {
		hal_int_restore(state);
		return E_CTX; /* タスク独立部からは自タスクを待たせられない */
	}
	if (me->wupcnt > 0) {
		me->wupcnt--;
		hal_int_restore(state);
		return E_OK;
	}
	if (tmout == TMO_POL) {
		hal_int_restore(state);
		return E_TMOUT;
	}
	er = knl_wait(0, TTW_SLP, 0, tmout);
	hal_int_restore(state);
	return er;
}

EXPORT ER tk_wup_tsk(ID tskid)
{
	UD state;
	ER er = E_OK;
	TCB *t;

	if (tskid == TSK_SELF) {
		/* 自タスクを起こすのは意味が無い（仕様は E_OBJ）。 */
		return E_OBJ;
	}
	state = hal_int_disable();
	t = knl_get_tcb(tskid, &er);
	if (t == 0) {
		hal_int_restore(state);
		return er;
	}
	if (t == knl_cur || t->state == TTS_DMT) {
		hal_int_restore(state);
		return E_OBJ;
	}
	if ((t->state & TTS_WAI) != 0 && t->waitcause == TTW_SLP) {
		knl_wait_release(t, E_OK);
		knl_dispatch();
		hal_int_restore(state);
		return E_OK;
	}
	/* 起床待ちでないのでキューイングする */
	if (t->wupcnt >= TK_WAKEUP_MAXCNT) {
		hal_int_restore(state);
		return E_QOVR;
	}
	t->wupcnt++;
	hal_int_restore(state);
	return E_OK;
}

EXPORT INT tk_can_wup(ID tskid)
{
	UD state;
	ER er = E_OK;
	TCB *t;
	INT n;

	state = hal_int_disable();
	t = knl_get_tcb(tskid, &er);
	if (t == 0) {
		hal_int_restore(state);
		return er;
	}
	if (t->state == TTS_DMT) {
		hal_int_restore(state);
		return E_OBJ;
	}
	n = t->wupcnt;
	t->wupcnt = 0;
	hal_int_restore(state);
	return n;
}

EXPORT ER tk_rel_wai(ID tskid)
{
	UD state;
	ER er = E_OK;
	TCB *t;

	state = hal_int_disable();
	t = knl_get_tcb(tskid, &er);
	if (t == 0) {
		hal_int_restore(state);
		return er;
	}
	if ((t->state & TTS_WAI) == 0) {
		hal_int_restore(state);
		return E_OBJ;
	}
	knl_wait_release(t, E_RLWAI);
	knl_dispatch();
	hal_int_restore(state);
	return E_OK;
}

/* ------------------------------------------------------------------ */
/* 強制待ち                                                            */
/*                                                                     */
/* 自タスクが自ら強制待ちになることはできない（仕様）。                 */
/* 要求はネストする。                                                  */
/* ------------------------------------------------------------------ */

EXPORT ER tk_sus_tsk(ID tskid)
{
	UD state;
	ER er = E_OK;
	TCB *t;

	if (tskid == TSK_SELF) {
		return E_OBJ;
	}
	state = hal_int_disable();
	t = knl_get_tcb(tskid, &er);
	if (t == 0) {
		hal_int_restore(state);
		return er;
	}
	if (t == knl_cur || t->state == TTS_DMT) {
		hal_int_restore(state);
		return E_OBJ;
	}
	if (t->suscnt >= TK_SUSPEND_MAXCNT) {
		hal_int_restore(state);
		return E_QOVR;
	}
	t->suscnt++;
	if ((t->state & TTS_WAI) != 0) {
		/* 待ち状態 → 二重待ち状態。待ち行列には並んだままにする。 */
		t->state = TTS_WAS;
	} else {
		/* 実行可能 → 強制待ち */
		knl_unready(t);
		t->state = TTS_SUS;
	}
	hal_int_restore(state);
	return E_OK;
}

LOCAL ER resume(ID tskid, Bool force)
{
	UD state;
	ER er = E_OK;
	TCB *t;

	state = hal_int_disable();
	t = knl_get_tcb(tskid, &er);
	if (t == 0) {
		hal_int_restore(state);
		return er;
	}
	if ((t->state & TTS_SUS) == 0) {
		hal_int_restore(state);
		return E_OBJ;
	}
	if (force) {
		t->suscnt = 0;
	} else if (t->suscnt > 0) {
		t->suscnt--;
	}
	if (t->suscnt > 0) {
		hal_int_restore(state);
		return E_OK;
	}
	if ((t->state & TTS_WAI) != 0) {
		/* 二重待ち → 待ち状態。まだ待ち行列にいる。 */
		t->state = TTS_WAI;
	} else {
		/* 強制待ち → 実行可能。同一優先度で**最低**の優先順位（仕様）。 */
		knl_make_ready(t, False);
		knl_dispatch();
	}
	hal_int_restore(state);
	return E_OK;
}

EXPORT ER tk_rsm_tsk(ID tskid)
{
	return resume(tskid, False);
}

EXPORT ER tk_frsm_tsk(ID tskid)
{
	return resume(tskid, True);
}

/* ------------------------------------------------------------------ */
/* 遅延                                                                */
/*                                                                     */
/* tk_wup_tsk では解除されない。時間が来たら E_OK で戻る（E_TMOUT では    */
/* ない）。この区別は knl_timeout_check() が TTW_DLY を見て行う。        */
/* ------------------------------------------------------------------ */

EXPORT ER tk_dly_tsk(RELTIM dlytim)
{
	UD state;
	ER er;

	state = hal_int_disable();
	if (knl_cur == &knl_idle) {
		hal_int_restore(state);
		return E_CTX;
	}
	if (dlytim == 0) {
		hal_int_restore(state);
		return E_OK;
	}
	er = knl_wait(0, TTW_DLY, 0, (TMO)dlytim);
	hal_int_restore(state);
	return er;
}

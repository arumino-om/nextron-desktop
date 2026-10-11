/*
 * flag.c — イベントフラグ (μT-Kernel 3.0)
 *
 * 【TA_WSGL と TA_WMUL】仕様の要点。
 *
 *   TA_WSGL  複数タスクの待ちを許さない。既に待ちタスクがいる状態で別の
 *            タスクが tk_wai_flg を呼ぶと、**待ち解除条件を満たしていても**
 *            E_OBJ になる
 *   TA_WMUL  複数タスクが同時に待てる。1 回の tk_set_flg で複数が解除されうる
 *
 * 【クリア指定の順序依存】TA_WMUL で待ち行列ができた場合、仕様はこう規定する。
 *
 *   「待ち行列中にクリア指定のタスクがあれば、そのタスクが待ち解除になる時に
 *     フラグをクリアする。クリア指定を行っていたタスクよりも後ろの待ち行列に
 *     あったタスクは、既にクリアされた後のイベントフラグを見ることになる」
 *
 * つまり**行列を先頭から順に処理し、クリアはその場で効かせる**。
 * まとめて判定してからクリアすると仕様と違う結果になる。
 *
 * 【タイムアウト時はクリアしない】仕様が明示している。
 */

#include "knl.h"

typedef struct FLGCB {
	Bool used;
	VP exinf;
	ATR flgatr;
	UINT flgptn;
	Queue wait;
	UB dsname[8];
} FLGCB;

LOCAL FLGCB flgcb[TK_MAX_FLGID + 1];

LOCAL FLGCB *get_flg(ID flgid, ER *er)
{
	if (flgid < 1 || flgid > TK_MAX_FLGID) {
		*er = E_ID;
		return 0;
	}
	if (!flgcb[flgid].used) {
		*er = E_NOEXS;
		return 0;
	}
	return &flgcb[flgid];
}

/* 待ち解除条件を満たしているか */
LOCAL Bool cond_met(UINT flgptn, UINT waiptn, UINT mode)
{
	if ((mode & TWF_ORW) != 0) {
		return ((flgptn & waiptn) != 0) ? True : False;
	}
	return ((flgptn & waiptn) == waiptn) ? True : False;
}

EXPORT void knl_flg_init(void)
{
	W i;

	for (i = 0; i <= TK_MAX_FLGID; i++) {
		flgcb[i].used = False;
		knl_queue_init(&flgcb[i].wait, TA_TFIFO);
	}
}

EXPORT ID tk_cre_flg(CONST T_CFLG *pk_cflg)
{
	UD state;
	ID id = 0;
	W i;

	if (pk_cflg == 0) {
		return E_PAR;
	}
	if ((pk_cflg->flgatr & ~(TA_TPRI | TA_WMUL | TA_DSNAME | TA_NODISWAI)) != 0) {
		return E_RSATR;
	}
	state = hal_int_disable();
	for (i = 1; i <= TK_MAX_FLGID; i++) {
		if (!flgcb[i].used) {
			id = i;
			break;
		}
	}
	if (id == 0) {
		hal_int_restore(state);
		return E_LIMIT;
	}
	flgcb[id].used = True;
	flgcb[id].exinf = pk_cflg->exinf;
	flgcb[id].flgatr = pk_cflg->flgatr;
	flgcb[id].flgptn = pk_cflg->iflgptn;
	knl_queue_init(&flgcb[id].wait, pk_cflg->flgatr);
	for (i = 0; i < 8; i++) {
		flgcb[id].dsname[i] =
		    ((pk_cflg->flgatr & TA_DSNAME) != 0) ? pk_cflg->dsname[i] : 0;
	}
	hal_int_restore(state);
	return id;
}

EXPORT ER tk_del_flg(ID flgid)
{
	UD state;
	ER er = E_OK;
	FLGCB *f;

	state = hal_int_disable();
	f = get_flg(flgid, &er);
	if (f == 0) {
		hal_int_restore(state);
		return er;
	}
	while (!knl_queue_empty(&f->wait)) {
		knl_wait_release(f->wait.head, E_DLT);
	}
	f->used = False;
	knl_dispatch();
	hal_int_restore(state);
	return E_OK;
}

EXPORT ER tk_set_flg(ID flgid, UINT setptn)
{
	UD state;
	ER er = E_OK;
	FLGCB *f;
	TCB *t;

	state = hal_int_disable();
	f = get_flg(flgid, &er);
	if (f == 0) {
		hal_int_restore(state);
		return er;
	}
	f->flgptn |= setptn;

	/* 待ち行列を**先頭から順に**処理する。クリアはその場で効かせるので、
	 * 後ろのタスクはクリア後の値を見る（仕様）。 */
	t = f->wait.head;
	while (t != 0) {
		TCB *nx = t->next;

		if (cond_met(f->flgptn, t->wait_ptn, t->wait_mode)) {
			if (t->wait_flgptn != 0) {
				*t->wait_flgptn = f->flgptn; /* クリア前の値 */
			}
			if ((t->wait_mode & TWF_CLR) != 0) {
				f->flgptn = 0;
			} else if ((t->wait_mode & TWF_BITCLR) != 0) {
				f->flgptn &= ~t->wait_ptn;
			}
			knl_wait_release(t, E_OK);
		}
		t = nx;
	}
	knl_dispatch();
	hal_int_restore(state);
	return E_OK;
}

EXPORT ER tk_clr_flg(ID flgid, UINT clrptn)
{
	UD state;
	ER er = E_OK;
	FLGCB *f;

	state = hal_int_disable();
	f = get_flg(flgid, &er);
	if (f == 0) {
		hal_int_restore(state);
		return er;
	}
	/* 仕様は「clrptn との論理積にする」。clrptn のビットが残る。 */
	f->flgptn &= clrptn;
	hal_int_restore(state);
	return E_OK;
}

EXPORT ER tk_wai_flg(ID flgid, UINT waiptn, UINT wfmode, UINT *p_flgptn, TMO tmout)
{
	UD state;
	ER er = E_OK;
	FLGCB *f;
	TCB *me;

	if (waiptn == 0 || p_flgptn == 0 || tmout < TMO_FEVR) {
		return E_PAR;
	}
	if ((wfmode & ~(TWF_ORW | TWF_CLR | TWF_BITCLR)) != 0) {
		return E_PAR;
	}
	if ((wfmode & TWF_CLR) != 0 && (wfmode & TWF_BITCLR) != 0) {
		return E_PAR; /* クリア指定は排他 */
	}

	state = hal_int_disable();
	f = get_flg(flgid, &er);
	if (f == 0) {
		hal_int_restore(state);
		return er;
	}
	/* TA_WSGL で既に待ちタスクがいる場合は、条件を満たしていても E_OBJ。
	 * 仕様が「待ち状態に入るかどうかにかかわらず」と明記している。 */
	if ((f->flgatr & TA_WMUL) == 0 && !knl_queue_empty(&f->wait)) {
		hal_int_restore(state);
		return E_OBJ;
	}

	if (cond_met(f->flgptn, waiptn, wfmode)) {
		*p_flgptn = f->flgptn;
		if ((wfmode & TWF_CLR) != 0) {
			f->flgptn = 0;
		} else if ((wfmode & TWF_BITCLR) != 0) {
			f->flgptn &= ~waiptn;
		}
		hal_int_restore(state);
		return E_OK;
	}
	if (tmout == TMO_POL) {
		hal_int_restore(state);
		return E_TMOUT;
	}
	me = knl_cur;
	if (me == &knl_idle) {
		hal_int_restore(state);
		return E_CTX;
	}
	me->wait_ptn = waiptn;
	me->wait_mode = wfmode;
	me->wait_flgptn = p_flgptn;
	er = knl_wait(&f->wait, TTW_FLG, flgid, tmout);
	hal_int_restore(state);
	return er;
}

EXPORT ER tk_ref_flg(ID flgid, T_RFLG *pk_rflg)
{
	UD state;
	ER er = E_OK;
	FLGCB *f;

	if (pk_rflg == 0) {
		return E_PAR;
	}
	state = hal_int_disable();
	f = get_flg(flgid, &er);
	if (f == 0) {
		hal_int_restore(state);
		return er;
	}
	pk_rflg->exinf = f->exinf;
	pk_rflg->wtsk = (f->wait.head != 0) ? f->wait.head->tskid : 0;
	pk_rflg->flgptn = f->flgptn;
	hal_int_restore(state);
	return E_OK;
}

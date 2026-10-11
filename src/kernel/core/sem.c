/*
 * sem.c — セマフォ (μT-Kernel 3.0)
 *
 * 【属性の 2 軸】仕様は 2 つの独立した軸を定義している。混同しやすい。
 *
 *   TA_TFIFO / TA_TPRI    **待ち行列の並び方**。FIFO か優先度順か
 *   TA_FIRST / TA_CNT     **資源の割り当て順**。並び方は変えない
 *
 *   TA_FIRST  要求数に関係なく行列の先頭から順に割り当てる。先頭が要求分を
 *             獲得できない限り、後ろのタスクは獲得できない
 *   TA_CNT    行列の先頭から順に検査し、要求分を割り当てられるタスクに
 *             割り当てる（要求数の少ない順ではない）
 *
 * 【資源は自動で返らない】仕様が明記している。カーネルはタスク終了時に
 * セマフォ資源を解放しない。解放はアプリケーションの責任。
 */

#include "knl.h"

typedef struct SEMCB {
	Bool used;
	VP exinf;
	ATR sematr;
	INT semcnt; /* 現在の資源数 */
	INT maxsem; /* 最大資源数 */
	Queue wait;
	UB dsname[8];
} SEMCB;

LOCAL SEMCB semcb[TK_MAX_SEMID + 1];

LOCAL SEMCB *get_sem(ID semid, ER *er)
{
	if (semid < 1 || semid > TK_MAX_SEMID) {
		*er = E_ID;
		return 0;
	}
	if (!semcb[semid].used) {
		*er = E_NOEXS;
		return 0;
	}
	return &semcb[semid];
}

EXPORT void knl_sem_init(void)
{
	W i;

	for (i = 0; i <= TK_MAX_SEMID; i++) {
		semcb[i].used = False;
		knl_queue_init(&semcb[i].wait, TA_TFIFO);
	}
}

EXPORT ID tk_cre_sem(CONST T_CSEM *pk_csem)
{
	UD state;
	ID id = 0;
	W i;

	if (pk_csem == 0) {
		return E_PAR;
	}
	if (pk_csem->maxsem < 1 || pk_csem->isemcnt < 0 ||
	    pk_csem->isemcnt > pk_csem->maxsem) {
		return E_PAR;
	}
	/* 未定義の属性ビットは E_RSATR。 */
	if ((pk_csem->sematr & ~(TA_TPRI | TA_CNT | TA_DSNAME | TA_NODISWAI)) != 0) {
		return E_RSATR;
	}

	state = hal_int_disable();
	for (i = 1; i <= TK_MAX_SEMID; i++) {
		if (!semcb[i].used) {
			id = i;
			break;
		}
	}
	if (id == 0) {
		hal_int_restore(state);
		return E_LIMIT;
	}
	semcb[id].used = True;
	semcb[id].exinf = pk_csem->exinf;
	semcb[id].sematr = pk_csem->sematr;
	semcb[id].semcnt = pk_csem->isemcnt;
	semcb[id].maxsem = pk_csem->maxsem;
	knl_queue_init(&semcb[id].wait, pk_csem->sematr);
	for (i = 0; i < 8; i++) {
		semcb[id].dsname[i] =
		    ((pk_csem->sematr & TA_DSNAME) != 0) ? pk_csem->dsname[i] : 0;
	}
	hal_int_restore(state);
	return id;
}

EXPORT ER tk_del_sem(ID semid)
{
	UD state;
	ER er = E_OK;
	SEMCB *s;

	state = hal_int_disable();
	s = get_sem(semid, &er);
	if (s == 0) {
		hal_int_restore(state);
		return er;
	}
	/* 待っているタスクは E_DLT で起こす（仕様）。 */
	while (!knl_queue_empty(&s->wait)) {
		knl_wait_release(s->wait.head, E_DLT);
	}
	s->used = False;
	knl_dispatch();
	hal_int_restore(state);
	return E_OK;
}

/* 資源を待ち行列のタスクに配る。割込み禁止で呼ぶこと。 */
LOCAL void distribute(SEMCB *s)
{
	TCB *t = s->wait.head;

	while (t != 0) {
		TCB *nx = t->next;

		if (s->semcnt >= t->wait_cnt) {
			s->semcnt -= t->wait_cnt;
			knl_wait_release(t, E_OK);
		} else if ((s->sematr & TA_CNT) == 0) {
			/* TA_FIRST: 先頭が獲得できないなら後ろには回さない */
			break;
		}
		t = nx;
	}
}

EXPORT ER tk_sig_sem(ID semid, INT cnt)
{
	UD state;
	ER er = E_OK;
	SEMCB *s;

	if (cnt < 1) {
		return E_PAR;
	}
	state = hal_int_disable();
	s = get_sem(semid, &er);
	if (s == 0) {
		hal_int_restore(state);
		return er;
	}
	if (s->semcnt + cnt > s->maxsem) {
		hal_int_restore(state);
		return E_QOVR;
	}
	s->semcnt += cnt;
	distribute(s);
	knl_dispatch();
	hal_int_restore(state);
	return E_OK;
}

EXPORT ER tk_wai_sem(ID semid, INT cnt, TMO tmout)
{
	UD state;
	ER er = E_OK;
	SEMCB *s;
	TCB *me;

	if (cnt < 1 || tmout < TMO_FEVR) {
		return E_PAR;
	}
	state = hal_int_disable();
	s = get_sem(semid, &er);
	if (s == 0) {
		hal_int_restore(state);
		return er;
	}
	if (cnt > s->maxsem) {
		hal_int_restore(state);
		return E_PAR; /* 絶対に満たせない要求 */
	}
	/* 待っているタスクがいるなら割り込まない。待ち行列の順序を壊すと
	 * TA_FIRST の意味が無くなるし、TA_CNT でも飢餓が起きる。 */
	if (knl_queue_empty(&s->wait) && s->semcnt >= cnt) {
		s->semcnt -= cnt;
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
	me->wait_cnt = cnt;
	er = knl_wait(&s->wait, TTW_SEM, semid, tmout);
	hal_int_restore(state);
	return er;
}

EXPORT ER tk_ref_sem(ID semid, T_RSEM *pk_rsem)
{
	UD state;
	ER er = E_OK;
	SEMCB *s;

	if (pk_rsem == 0) {
		return E_PAR;
	}
	state = hal_int_disable();
	s = get_sem(semid, &er);
	if (s == 0) {
		hal_int_restore(state);
		return er;
	}
	pk_rsem->exinf = s->exinf;
	pk_rsem->wtsk = (s->wait.head != 0) ? s->wait.head->tskid : 0;
	pk_rsem->semcnt = s->semcnt;
	hal_int_restore(state);
	return E_OK;
}

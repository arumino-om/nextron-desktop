/*
 * mbx.c — メールボックス (μT-Kernel 3.0)
 *
 * **メッセージの本体はアプリが用意し、カーネルはヘッダで繋ぐだけ。**
 * 内容の複製をしない。これは規律 R10（大きなデータを IPC で運ばない）と
 * 同じ考え方が、仕様の側に既に入っているということ。
 *
 * そのかわり**メッセージ領域の所有権がアプリ側にある**ので、送ったあとに
 * 送信側が書き換えたり解放したりしてはならない。仕様もそう規定している。
 *
 * 属性:
 *   TA_TFIFO / TA_TPRI   受信待ちタスクの並び
 *   TA_MFIFO / TA_MPRI   メッセージキューの並び。TA_MPRI なら
 *                        T_MSG_PRI.msgpri 順（1 が最高、同一優先度は FIFO）
 */

#include "knl.h"

typedef struct MBXCB {
	Bool used;
	VP exinf;
	ATR mbxatr;
	T_MSG *mq_head; /* 受信されるのを待つメッセージの列 */
	T_MSG *mq_tail;
	Queue wait; /* 受信待ちのタスク */
	UB dsname[8];
} MBXCB;

LOCAL MBXCB mbxcb[TK_MAX_MBXID + 1];

LOCAL MBXCB *get_mbx(ID mbxid, ER *er)
{
	if (mbxid < 1 || mbxid > TK_MAX_MBXID) {
		*er = E_ID;
		return 0;
	}
	if (!mbxcb[mbxid].used) {
		*er = E_NOEXS;
		return 0;
	}
	return &mbxcb[mbxid];
}

EXPORT void knl_mbx_init(void)
{
	W i;

	for (i = 0; i <= TK_MAX_MBXID; i++) {
		mbxcb[i].used = False;
		mbxcb[i].mq_head = 0;
		mbxcb[i].mq_tail = 0;
		knl_queue_init(&mbxcb[i].wait, TA_TFIFO);
	}
}

/* ------------------------------------------------------------------ */
/* メッセージキュー                                                    */
/* ------------------------------------------------------------------ */

LOCAL void mq_push(MBXCB *b, T_MSG *msg)
{
	msg->msgque = 0;

	if ((b->mbxatr & TA_MPRI) == 0) {
		/* FIFO */
		if (b->mq_tail != 0) {
			b->mq_tail->msgque = msg;
		} else {
			b->mq_head = msg;
		}
		b->mq_tail = msg;
		return;
	}
	/* 優先度順。1 が最高で、同一優先度は FIFO（仕様）。 */
	{
		PRI pri = ((T_MSG_PRI *)msg)->msgpri;
		T_MSG **pp = &b->mq_head;
		T_MSG *prev = 0;

		while (*pp != 0 && ((T_MSG_PRI *)*pp)->msgpri <= pri) {
			prev = *pp;
			pp = &(*pp)->msgque;
		}
		msg->msgque = *pp;
		*pp = msg;
		if (msg->msgque == 0) {
			b->mq_tail = msg;
		}
		(void)prev;
	}
}

LOCAL T_MSG *mq_pop(MBXCB *b)
{
	T_MSG *msg = b->mq_head;

	if (msg == 0) {
		return 0;
	}
	b->mq_head = msg->msgque;
	if (b->mq_head == 0) {
		b->mq_tail = 0;
	}
	msg->msgque = 0;
	return msg;
}

/* ------------------------------------------------------------------ */
/* 生成と削除                                                          */
/* ------------------------------------------------------------------ */

EXPORT ID tk_cre_mbx(CONST T_CMBX *pk_cmbx)
{
	UD state;
	ID id = 0;
	W i;

	if (pk_cmbx == 0) {
		return E_PAR;
	}
	if ((pk_cmbx->mbxatr & ~(TA_TPRI | TA_MPRI | TA_DSNAME | TA_NODISWAI)) != 0) {
		return E_RSATR;
	}
	state = hal_int_disable();
	for (i = 1; i <= TK_MAX_MBXID; i++) {
		if (!mbxcb[i].used) {
			id = i;
			break;
		}
	}
	if (id == 0) {
		hal_int_restore(state);
		return E_LIMIT;
	}
	mbxcb[id].used = True;
	mbxcb[id].exinf = pk_cmbx->exinf;
	mbxcb[id].mbxatr = pk_cmbx->mbxatr;
	mbxcb[id].mq_head = 0;
	mbxcb[id].mq_tail = 0;
	/* TA_MPRI (0x02) はメッセージの並びでタスクの並びではない。正規化する。 */
	knl_queue_init(&mbxcb[id].wait, KNL_ORDER((pk_cmbx->mbxatr & TA_TPRI) != 0));
	for (i = 0; i < 8; i++) {
		mbxcb[id].dsname[i] =
		    ((pk_cmbx->mbxatr & TA_DSNAME) != 0) ? pk_cmbx->dsname[i] : 0;
	}
	hal_int_restore(state);
	return id;
}

EXPORT ER tk_del_mbx(ID mbxid)
{
	UD state;
	ER er = E_OK;
	MBXCB *b;

	state = hal_int_disable();
	b = get_mbx(mbxid, &er);
	if (b == 0) {
		hal_int_restore(state);
		return er;
	}
	while (!knl_queue_empty(&b->wait)) {
		knl_wait_release(b->wait.head, E_DLT);
	}
	/* キューに残っているメッセージは捨てる。領域はアプリのものなので
	 * カーネルが解放することはない。 */
	b->mq_head = 0;
	b->mq_tail = 0;
	b->used = False;
	knl_dispatch();
	hal_int_restore(state);
	return E_OK;
}

/* ------------------------------------------------------------------ */
/* 送受信                                                              */
/* ------------------------------------------------------------------ */

EXPORT ER tk_snd_mbx(ID mbxid, T_MSG *pk_msg)
{
	UD state;
	ER er = E_OK;
	MBXCB *b;

	if (pk_msg == 0) {
		return E_PAR;
	}
	state = hal_int_disable();
	b = get_mbx(mbxid, &er);
	if (b == 0) {
		hal_int_restore(state);
		return er;
	}
	if ((b->mbxatr & TA_MPRI) != 0 && ((T_MSG_PRI *)pk_msg)->msgpri < 1) {
		hal_int_restore(state);
		return E_PAR; /* メッセージ優先度は正の値（仕様） */
	}
	if (!knl_queue_empty(&b->wait)) {
		/* 受信待ちがいれば直接渡す。キューを経由しない。 */
		TCB *t = b->wait.head;

		*(T_MSG **)t->rdv_msg = pk_msg;
		knl_wait_release(t, E_OK);
		knl_dispatch();
		hal_int_restore(state);
		return E_OK;
	}
	mq_push(b, pk_msg);
	hal_int_restore(state);
	return E_OK;
}

EXPORT ER tk_rcv_mbx(ID mbxid, T_MSG **ppk_msg, TMO tmout)
{
	UD state;
	ER er = E_OK;
	MBXCB *b;
	TCB *me;
	T_MSG *msg;

	if (ppk_msg == 0 || tmout < TMO_FEVR) {
		return E_PAR;
	}
	state = hal_int_disable();
	b = get_mbx(mbxid, &er);
	if (b == 0) {
		hal_int_restore(state);
		return er;
	}
	msg = mq_pop(b);
	if (msg != 0) {
		*ppk_msg = msg;
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
	/* 受け取り先のポインタを TCB に預ける。送信側がここへ書き込む。
	 * ランデブと同じ欄を使い回している（待ち要因が排他なので衝突しない）。 */
	me->rdv_msg = (VP)ppk_msg;
	er = knl_wait(&b->wait, TTW_MBX, mbxid, tmout);
	hal_int_restore(state);
	return er;
}

EXPORT ER tk_ref_mbx(ID mbxid, T_RMBX *pk_rmbx)
{
	UD state;
	ER er = E_OK;
	MBXCB *b;

	if (pk_rmbx == 0) {
		return E_PAR;
	}
	state = hal_int_disable();
	b = get_mbx(mbxid, &er);
	if (b == 0) {
		hal_int_restore(state);
		return er;
	}
	pk_rmbx->exinf = b->exinf;
	pk_rmbx->wtsk = (b->wait.head != 0) ? b->wait.head->tskid : 0;
	pk_rmbx->pk_msg = b->mq_head;
	hal_int_restore(state);
	return E_OK;
}

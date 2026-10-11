/*
 * mbf.c — メッセージバッファ (μT-Kernel 3.0 拡張同期・通信機能)
 *
 * メールボックスとの違いは **メッセージ内容を複製する** ことと、
 * **バッファが一杯なら送信側も待つ** こと。
 *
 * 【バッファサイズ 0 が合法】仕様の面白いところ。
 * 領域を 0 にすると、送信側と受信側が互いを待ち合わせる**同期メッセージ**に
 * なる。両者がシステムコールを呼んだ時点で受け渡しが起きる。
 * この経路があるので「空きが無ければ必ず待つ」実装では足りず、
 * **受信側が待っているときは直接渡す**経路が必要になる。
 *
 * 【送信待ちは厳密に行列順】仕様が例を挙げて明示している。
 *
 *   「40バイトを送るタスクAと10バイトを送るタスクBがこの順で待っている時に、
 *     20バイトの空きができたとする。このような場合でも、タスクAが
 *     メッセージを送信するまで、タスクBはメッセージを送信できない」
 *
 * つまり**先頭が入らなければ後ろには回さない**。セマフォの TA_FIRST と
 * 同じ考え方で、追い越しを許すと行列の意味が無くなる。
 *
 * 【枠の形式】リングバッファに「4 バイトの長さ + 本体」を並べる。
 * 本体は 4 バイト境界に切り上げる。可変長なので長さを持たないと
 * 取り出せない。
 */

#include "knl.h"

IMPORT VP memcpy(VP dst, CONST VP src, UD n);

#define MBF_HDRSZ 4
#define MBF_ALIGN(n) (((n) + 3) & ~3)
#define MBF_FRAME(n) (MBF_HDRSZ + MBF_ALIGN(n))

typedef struct MBFCB {
	Bool used;
	VP exinf;
	ATR mbfatr;
	W bufsz;  /* リングバッファのバイト数。0 なら同期通信 */
	W maxmsz; /* 1 メッセージの最大バイト数 */
	UD base;  /* リングバッファの先頭 */
	W head;   /* 取り出し位置 */
	W tail;   /* 書き込み位置 */
	W inuse;  /* 使用中のバイト数（枠を含む） */
	W msgcnt; /* 入っているメッセージ数 */
	Queue send; /* 送信待ち */
	Queue recv; /* 受信待ち。FIFO 固定（仕様は送信待ちの並びだけ規定） */
	UB dsname[8];
} MBFCB;

LOCAL MBFCB mbfcb[TK_MAX_MBFID + 1];

LOCAL MBFCB *get_mbf(ID id, ER *er)
{
	if (id < 1 || id > TK_MAX_MBFID) {
		*er = E_ID;
		return 0;
	}
	if (!mbfcb[id].used) {
		*er = E_NOEXS;
		return 0;
	}
	return &mbfcb[id];
}

EXPORT void knl_mbf_init(void)
{
	W i;

	for (i = 0; i <= TK_MAX_MBFID; i++) {
		mbfcb[i].used = False;
		knl_queue_init(&mbfcb[i].send, TA_TFIFO);
		knl_queue_init(&mbfcb[i].recv, TA_TFIFO);
	}
}

/* ------------------------------------------------------------------ */
/* リングバッファ                                                      */
/* ------------------------------------------------------------------ */

LOCAL void ring_put(MBFCB *m, CONST VP msg, W sz)
{
	UB *b = (UB *)(VP)m->base;
	W frame = MBF_FRAME(sz);
	W i;

	/* 長さを 4 バイトで書く。リングの折り返しを跨ぐことがあるので
	 * バイト単位で置く（規律 R7 と同じ理由で、構造体を被せない）。 */
	for (i = 0; i < 4; i++) {
		b[m->tail] = (UB)(((UW)sz >> (UW)(i * 8)) & 0xFFU);
		m->tail = (m->tail + 1) % m->bufsz;
	}
	for (i = 0; i < sz; i++) {
		b[m->tail] = ((CONST UB *)msg)[i];
		m->tail = (m->tail + 1) % m->bufsz;
	}
	/* 整列のための詰め物 */
	for (i = sz; i < MBF_ALIGN(sz); i++) {
		m->tail = (m->tail + 1) % m->bufsz;
	}
	m->inuse += frame;
	m->msgcnt++;
}

/* 先頭のメッセージの長さを覗く。無ければ 0。 */
LOCAL W ring_peek(CONST MBFCB *m)
{
	CONST UB *b = (CONST UB *)(VP)m->base;
	UW sz = 0;
	W i;
	W p = m->head;

	if (m->msgcnt == 0) {
		return 0;
	}
	for (i = 0; i < 4; i++) {
		sz |= (UW)b[p] << (UW)(i * 8);
		p = (p + 1) % m->bufsz;
	}
	return (W)sz;
}

LOCAL W ring_get(MBFCB *m, VP msg)
{
	CONST UB *b = (CONST UB *)(VP)m->base;
	W sz = ring_peek(m);
	W i;

	if (sz == 0) {
		return 0;
	}
	m->head = (m->head + 4) % m->bufsz;
	for (i = 0; i < sz; i++) {
		((UB *)msg)[i] = b[m->head];
		m->head = (m->head + 1) % m->bufsz;
	}
	for (i = sz; i < MBF_ALIGN(sz); i++) {
		m->head = (m->head + 1) % m->bufsz;
	}
	m->inuse -= MBF_FRAME(sz);
	m->msgcnt--;
	return sz;
}

LOCAL Bool ring_fits(CONST MBFCB *m, W sz)
{
	return (m->bufsz > 0 && m->inuse + MBF_FRAME(sz) <= m->bufsz) ? True : False;
}

/* 送信待ちの行列を先頭から処理する。
 * **先頭が入らなければ打ち切る**（仕様。追い越しを許さない）。 */
LOCAL void drain_senders(MBFCB *m)
{
	while (!knl_queue_empty(&m->send)) {
		TCB *t = m->send.head;

		if (!ring_fits(m, t->rdv_size)) {
			break;
		}
		ring_put(m, t->rdv_msg, t->rdv_size);
		knl_wait_release(t, E_OK);
	}
}

/* ------------------------------------------------------------------ */
/* 生成と削除                                                          */
/* ------------------------------------------------------------------ */

EXPORT ID tk_cre_mbf(CONST T_CMBF *pk_cmbf)
{
	UD state;
	ID id = 0;
	W i;
	UD base = 0;

	if (pk_cmbf == 0 || pk_cmbf->bufsz < 0 || pk_cmbf->maxmsz < 1) {
		return E_PAR;
	}
	if ((pk_cmbf->mbfatr & ~(TA_TPRI | TA_DSNAME | TA_NODISWAI)) != 0) {
		return E_RSATR;
	}
	if (pk_cmbf->bufsz > 0) {
		UD ps = (UD)hal_page_size();
		UD total = ((UD)pk_cmbf->bufsz + ps - 1) & ~(ps - 1);

		base = knl_alloc_pages((SZ)total);
		if (base == 0) {
			return E_NOMEM;
		}
	}

	state = hal_int_disable();
	for (i = 1; i <= TK_MAX_MBFID; i++) {
		if (!mbfcb[i].used) {
			id = i;
			break;
		}
	}
	if (id == 0) {
		hal_int_restore(state);
		return E_LIMIT;
	}
	mbfcb[id].used = True;
	mbfcb[id].exinf = pk_cmbf->exinf;
	mbfcb[id].mbfatr = pk_cmbf->mbfatr;
	mbfcb[id].bufsz = pk_cmbf->bufsz;
	mbfcb[id].maxmsz = pk_cmbf->maxmsz;
	mbfcb[id].base = base;
	mbfcb[id].head = 0;
	mbfcb[id].tail = 0;
	mbfcb[id].inuse = 0;
	mbfcb[id].msgcnt = 0;
	/* 仕様が並びを規定しているのは**送信待ち行列**だけ。受信側は FIFO。 */
	knl_queue_init(&mbfcb[id].send, KNL_ORDER((pk_cmbf->mbfatr & TA_TPRI) != 0));
	knl_queue_init(&mbfcb[id].recv, TA_TFIFO);
	for (i = 0; i < 8; i++) {
		mbfcb[id].dsname[i] =
		    ((pk_cmbf->mbfatr & TA_DSNAME) != 0) ? pk_cmbf->dsname[i] : 0;
	}
	hal_int_restore(state);
	return id;
}

EXPORT ER tk_del_mbf(ID mbfid)
{
	UD state;
	ER er = E_OK;
	MBFCB *m;

	state = hal_int_disable();
	m = get_mbf(mbfid, &er);
	if (m == 0) {
		hal_int_restore(state);
		return er;
	}
	while (!knl_queue_empty(&m->send)) {
		knl_wait_release(m->send.head, E_DLT);
	}
	while (!knl_queue_empty(&m->recv)) {
		knl_wait_release(m->recv.head, E_DLT);
	}
	m->used = False;
	knl_dispatch();
	hal_int_restore(state);
	if (m->base != 0) {
		knl_free_pages(m->base, (SZ)m->bufsz);
		m->base = 0;
	}
	return E_OK;
}

/* ------------------------------------------------------------------ */
/* 送受信                                                              */
/* ------------------------------------------------------------------ */

EXPORT ER tk_snd_mbf(ID mbfid, CONST VP msg, INT msgsz, TMO tmout)
{
	UD state;
	ER er = E_OK;
	MBFCB *m;
	TCB *me;

	if (msg == 0 || msgsz < 1 || tmout < TMO_FEVR) {
		return E_PAR;
	}
	state = hal_int_disable();
	m = get_mbf(mbfid, &er);
	if (m == 0) {
		hal_int_restore(state);
		return er;
	}
	if (msgsz > m->maxmsz) {
		hal_int_restore(state);
		return E_PAR;
	}

	/* 受信側が待っているなら直接渡す。
	 * **バッファサイズ 0 の同期通信はこの経路しかない。** */
	if (!knl_queue_empty(&m->recv)) {
		TCB *t = m->recv.head;

		memcpy(t->rdv_msg, msg, (UD)msgsz);
		t->rdv_result = msgsz;
		knl_wait_release(t, E_OK);
		knl_dispatch();
		hal_int_restore(state);
		return E_OK;
	}
	/* 送信待ちがいるなら割り込まない（行列の順序を守る。仕様） */
	if (knl_queue_empty(&m->send) && ring_fits(m, msgsz)) {
		ring_put(m, msg, msgsz);
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
	me->rdv_msg = msg;
	me->rdv_size = msgsz;
	er = knl_wait(&m->send, TTW_SMBF, mbfid, tmout);
	hal_int_restore(state);
	return er;
}

EXPORT INT tk_rcv_mbf(ID mbfid, VP msg, TMO tmout)
{
	UD state;
	ER er = E_OK;
	MBFCB *m;
	TCB *me;
	W sz;

	if (msg == 0 || tmout < TMO_FEVR) {
		return E_PAR;
	}
	state = hal_int_disable();
	m = get_mbf(mbfid, &er);
	if (m == 0) {
		hal_int_restore(state);
		return er;
	}
	if (m->msgcnt > 0) {
		sz = ring_get(m, msg);
		drain_senders(m); /* 空いたので送信待ちを進める */
		knl_dispatch();
		hal_int_restore(state);
		return sz;
	}
	if (!knl_queue_empty(&m->send)) {
		/* バッファに入っていないが送信待ちがいる。
		 * バッファサイズ 0 か、バッファより大きいメッセージの場合。 */
		TCB *t = m->send.head;

		sz = t->rdv_size;
		memcpy(msg, t->rdv_msg, (UD)sz);
		knl_wait_release(t, E_OK);
		drain_senders(m);
		knl_dispatch();
		hal_int_restore(state);
		return sz;
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
	me->rdv_msg = msg;
	me->rdv_result = 0;
	er = knl_wait(&m->recv, TTW_RMBF, mbfid, tmout);
	if (er != E_OK) {
		hal_int_restore(state);
		return er;
	}
	sz = me->rdv_result;
	hal_int_restore(state);
	return sz;
}

EXPORT ER tk_ref_mbf(ID mbfid, T_RMBF *pk_rmbf)
{
	UD state;
	ER er = E_OK;
	MBFCB *m;

	if (pk_rmbf == 0) {
		return E_PAR;
	}
	state = hal_int_disable();
	m = get_mbf(mbfid, &er);
	if (m == 0) {
		hal_int_restore(state);
		return er;
	}
	pk_rmbf->exinf = m->exinf;
	pk_rmbf->wtsk = (m->recv.head != 0) ? m->recv.head->tskid : 0;
	pk_rmbf->stsk = (m->send.head != 0) ? m->send.head->tskid : 0;
	pk_rmbf->msgsz = ring_peek(m);
	pk_rmbf->frbufsz = m->bufsz - m->inuse;
	pk_rmbf->maxmsz = m->maxmsz;
	hal_int_restore(state);
	return E_OK;
}

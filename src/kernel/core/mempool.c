/*
 * mempool.c — メモリプール管理 (μT-Kernel 3.0)
 *
 * 固定長 (mpf) と可変長 (mpl) の 2 種類。
 *
 *   固定長  同じ大きさのブロックを配る。空きブロックの単方向リストだけで
 *           済むので断片化しない。獲得・返却が O(1)
 *   可変長  任意の大きさを配る。最初に合う穴を使う (first fit) 単純な
 *           実装にしてある。断片化はする
 *
 * 【プールの領域をどこから取るか】物理ページアロケータ (mm/page.c) から
 * `knl_alloc_pages()` 経由で取り、削除で返す。A14 でメモリブロックサイズを
 * 4KB = ページサイズと決めてあるので、プールの単位とページの単位が揃う。
 *
 * メモリマップが無い構成（QEMU の -kernel 直接起動）ではアロケータが
 * 立たないので、起動初期のバンプアロケータに落ちる。その場合は返せない。
 *
 * 【規律 R2】ページサイズを直書きしない。`hal_page_size()` を使う。
 */

#include "knl.h"

/* ------------------------------------------------------------------ */
/* 固定長メモリプール                                                  */
/* ------------------------------------------------------------------ */

typedef struct MPFCB {
	Bool used;
	VP exinf;
	ATR mpfatr;
	W blfsz;  /* 1 ブロックのバイト数（整列済み） */
	W mpfcnt; /* ブロック総数 */
	W frcnt;  /* 空きブロック数 */
	UD base;  /* 領域の先頭 */
	VP free;  /* 空きブロックの単方向リスト。先頭 8 バイトに次を書く */
	Queue wait;
	UB dsname[8];
} MPFCB;

LOCAL MPFCB mpfcb[TK_MAX_MPFID + 1];

LOCAL MPFCB *get_mpf(ID id, ER *er)
{
	if (id < 1 || id > TK_MAX_MPFID) {
		*er = E_ID;
		return 0;
	}
	if (!mpfcb[id].used) {
		*er = E_NOEXS;
		return 0;
	}
	return &mpfcb[id];
}

/* 空きリストの push / pop。ブロックの先頭 8 バイトを次ポインタに使う。
 * 配る前に上書きされるので中身を壊す心配はない。 */
LOCAL void mpf_push(MPFCB *m, VP blk)
{
	*(VP *)blk = m->free;
	m->free = blk;
	m->frcnt++;
}

LOCAL VP mpf_pop(MPFCB *m)
{
	VP blk = m->free;

	if (blk == 0) {
		return 0;
	}
	m->free = *(VP *)blk;
	m->frcnt--;
	return blk;
}

EXPORT ID tk_cre_mpf(CONST T_CMPF *pk_cmpf)
{
	UD state;
	ID id = 0;
	W i;
	W blfsz;
	UD total;
	UD base;

	if (pk_cmpf == 0) {
		return E_PAR;
	}
	if (pk_cmpf->mpfcnt < 1 || pk_cmpf->blfsz < 1) {
		return E_PAR;
	}
	if ((pk_cmpf->mpfatr & ~(TA_TPRI | TA_DSNAME | TA_NODISWAI)) != 0) {
		return E_RSATR;
	}
	/* 空きリストの次ポインタを置くので 8 バイト以上に切り上げ、
	 * かつ 8 バイト整列する。 */
	blfsz = (W)(((UD)pk_cmpf->blfsz + 7U) & ~7U);
	if (blfsz < 8) {
		blfsz = 8;
	}
	total = (UD)blfsz * (UD)pk_cmpf->mpfcnt;

	base = knl_alloc_pages((SZ)total);
	if (base == 0) {
		return E_NOMEM;
	}

	state = hal_int_disable();
	for (i = 1; i <= TK_MAX_MPFID; i++) {
		if (!mpfcb[i].used) {
			id = i;
			break;
		}
	}
	if (id == 0) {
		hal_int_restore(state);
		return E_LIMIT;
	}
	mpfcb[id].used = True;
	mpfcb[id].exinf = pk_cmpf->exinf;
	mpfcb[id].mpfatr = pk_cmpf->mpfatr;
	mpfcb[id].blfsz = blfsz;
	mpfcb[id].mpfcnt = pk_cmpf->mpfcnt;
	mpfcb[id].frcnt = 0;
	mpfcb[id].base = base;
	mpfcb[id].free = 0;
	knl_queue_init(&mpfcb[id].wait, KNL_ORDER((pk_cmpf->mpfatr & TA_TPRI) != 0));
	for (i = 0; i < pk_cmpf->mpfcnt; i++) {
		mpf_push(&mpfcb[id], (VP)(base + (UD)i * (UD)blfsz));
	}
	for (i = 0; i < 8; i++) {
		mpfcb[id].dsname[i] =
		    ((pk_cmpf->mpfatr & TA_DSNAME) != 0) ? pk_cmpf->dsname[i] : 0;
	}
	hal_int_restore(state);
	return id;
}

EXPORT ER tk_del_mpf(ID mpfid)
{
	UD state;
	ER er = E_OK;
	MPFCB *m;

	state = hal_int_disable();
	m = get_mpf(mpfid, &er);
	if (m == 0) {
		hal_int_restore(state);
		return er;
	}
	while (!knl_queue_empty(&m->wait)) {
		knl_wait_release(m->wait.head, E_DLT);
	}
	m->used = False;
	knl_dispatch();
	hal_int_restore(state);
	/* 領域を返す。割込みを許してから（used = False なのでもう掴まれない）。 */
	knl_free_pages(m->base, (SZ)((UD)m->blfsz * (UD)m->mpfcnt));
	m->base = 0;
	return E_OK;
}

EXPORT ER tk_get_mpf(ID mpfid, VP *p_blf, TMO tmout)
{
	UD state;
	ER er = E_OK;
	MPFCB *m;
	TCB *me;
	VP blk;

	if (p_blf == 0 || tmout < TMO_FEVR) {
		return E_PAR;
	}
	state = hal_int_disable();
	m = get_mpf(mpfid, &er);
	if (m == 0) {
		hal_int_restore(state);
		return er;
	}
	/* 待っているタスクがいるなら割り込まない（待ち行列の順序を守る） */
	if (knl_queue_empty(&m->wait)) {
		blk = mpf_pop(m);
		if (blk != 0) {
			*p_blf = blk;
			hal_int_restore(state);
			return E_OK;
		}
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
	me->rdv_msg = (VP)p_blf; /* 返却側がここへ書き込む */
	er = knl_wait(&m->wait, TTW_MPF, mpfid, tmout);
	hal_int_restore(state);
	return er;
}

EXPORT ER tk_rel_mpf(ID mpfid, VP blf)
{
	UD state;
	ER er = E_OK;
	MPFCB *m;
	UD off;

	if (blf == 0) {
		return E_PAR;
	}
	state = hal_int_disable();
	m = get_mpf(mpfid, &er);
	if (m == 0) {
		hal_int_restore(state);
		return er;
	}
	/* このプールのブロックか確かめる。他所のポインタを返されると
	 * 空きリストが壊れて、後で無関係な場所を配ることになる。 */
	off = (UD)blf - m->base;
	if ((UD)blf < m->base || off % (UD)m->blfsz != 0 ||
	    off / (UD)m->blfsz >= (UD)m->mpfcnt) {
		hal_int_restore(state);
		return E_PAR;
	}
	if (!knl_queue_empty(&m->wait)) {
		/* 待っているタスクへ直接渡す */
		TCB *t = m->wait.head;

		*(VP *)t->rdv_msg = blf;
		knl_wait_release(t, E_OK);
		knl_dispatch();
		hal_int_restore(state);
		return E_OK;
	}
	mpf_push(m, blf);
	hal_int_restore(state);
	return E_OK;
}

EXPORT ER tk_ref_mpf(ID mpfid, T_RMPF *pk_rmpf)
{
	UD state;
	ER er = E_OK;
	MPFCB *m;

	if (pk_rmpf == 0) {
		return E_PAR;
	}
	state = hal_int_disable();
	m = get_mpf(mpfid, &er);
	if (m == 0) {
		hal_int_restore(state);
		return er;
	}
	pk_rmpf->exinf = m->exinf;
	pk_rmpf->wtsk = (m->wait.head != 0) ? m->wait.head->tskid : 0;
	pk_rmpf->frbcnt = m->frcnt;
	hal_int_restore(state);
	return E_OK;
}

/* ------------------------------------------------------------------ */
/* 可変長メモリプール                                                  */
/*                                                                     */
/* 各ブロックの直前に 16 バイトのヘッダを置く双方向リスト方式。          */
/* first fit で分割し、返却時に前後の空きと融合する。                   */
/* ------------------------------------------------------------------ */

typedef struct MPLHDR {
	struct MPLHDR *next;
	struct MPLHDR *prev;
	UD size; /* ヘッダを含まない利用可能バイト数 */
	UD inuse;
} MPLHDR;

typedef struct MPLCB {
	Bool used;
	VP exinf;
	ATR mplatr;
	W mplsz;
	MPLHDR *head;
	Queue wait;
	UB dsname[8];
} MPLCB;

LOCAL MPLCB mplcb[TK_MAX_MPLID + 1];

#define MPL_HDRSZ ((UD)sizeof(MPLHDR))
#define MPL_MIN_SPLIT 16U /* これ未満の残りでは分割しない */

LOCAL MPLCB *get_mpl(ID id, ER *er)
{
	if (id < 1 || id > TK_MAX_MPLID) {
		*er = E_ID;
		return 0;
	}
	if (!mplcb[id].used) {
		*er = E_NOEXS;
		return 0;
	}
	return &mplcb[id];
}

LOCAL VP mpl_alloc(MPLCB *m, UD size)
{
	MPLHDR *h;

	size = (size + 7U) & ~7U;
	for (h = m->head; h != 0; h = h->next) {
		if (h->inuse != 0 || h->size < size) {
			continue;
		}
		if (h->size >= size + MPL_HDRSZ + MPL_MIN_SPLIT) {
			/* 分割する */
			MPLHDR *n = (MPLHDR *)(VP)((UD)(VP)h + MPL_HDRSZ + size);

			n->size = h->size - size - MPL_HDRSZ;
			n->inuse = 0;
			n->next = h->next;
			n->prev = h;
			if (h->next != 0) {
				h->next->prev = n;
			}
			h->next = n;
			h->size = size;
		}
		h->inuse = 1;
		return (VP)((UD)(VP)h + MPL_HDRSZ);
	}
	return 0;
}

LOCAL void mpl_free(MPLCB *m, MPLHDR *h)
{
	h->inuse = 0;
	/* 後ろの空きと融合 */
	if (h->next != 0 && h->next->inuse == 0) {
		MPLHDR *n = h->next;

		h->size += MPL_HDRSZ + n->size;
		h->next = n->next;
		if (n->next != 0) {
			n->next->prev = h;
		}
	}
	/* 前の空きと融合 */
	if (h->prev != 0 && h->prev->inuse == 0) {
		MPLHDR *p = h->prev;

		p->size += MPL_HDRSZ + h->size;
		p->next = h->next;
		if (h->next != 0) {
			h->next->prev = p;
		}
	}
	(void)m;
}

/* 待ち行列を先頭から見て、配れるものに配る。 */
LOCAL void mpl_distribute(MPLCB *m)
{
	TCB *t = m->wait.head;

	while (t != 0) {
		TCB *nx = t->next;
		VP blk = mpl_alloc(m, (UD)t->wait_cnt);

		if (blk != 0) {
			*(VP *)t->rdv_msg = blk;
			knl_wait_release(t, E_OK);
		}
		t = nx;
	}
}

EXPORT ID tk_cre_mpl(CONST T_CMPL *pk_cmpl)
{
	UD state;
	ID id = 0;
	W i;
	UD ps;
	UD total;
	UD base;
	MPLHDR *h;

	if (pk_cmpl == 0 || pk_cmpl->mplsz < (W)(MPL_HDRSZ + 16U)) {
		return E_PAR;
	}
	if ((pk_cmpl->mplatr & ~(TA_TPRI | TA_DSNAME | TA_NODISWAI)) != 0) {
		return E_RSATR;
	}
	ps = (UD)hal_page_size();
	total = ((UD)pk_cmpl->mplsz + ps - 1) & ~(ps - 1);
	base = knl_alloc_pages((SZ)total);
	if (base == 0) {
		return E_NOMEM;
	}

	state = hal_int_disable();
	for (i = 1; i <= TK_MAX_MPLID; i++) {
		if (!mplcb[i].used) {
			id = i;
			break;
		}
	}
	if (id == 0) {
		hal_int_restore(state);
		return E_LIMIT;
	}
	h = (MPLHDR *)(VP)base;
	h->next = 0;
	h->prev = 0;
	h->size = total - MPL_HDRSZ;
	h->inuse = 0;

	mplcb[id].used = True;
	mplcb[id].exinf = pk_cmpl->exinf;
	mplcb[id].mplatr = pk_cmpl->mplatr;
	mplcb[id].mplsz = (W)total;
	mplcb[id].head = h;
	knl_queue_init(&mplcb[id].wait, KNL_ORDER((pk_cmpl->mplatr & TA_TPRI) != 0));
	for (i = 0; i < 8; i++) {
		mplcb[id].dsname[i] =
		    ((pk_cmpl->mplatr & TA_DSNAME) != 0) ? pk_cmpl->dsname[i] : 0;
	}
	hal_int_restore(state);
	return id;
}

EXPORT ER tk_del_mpl(ID mplid)
{
	UD state;
	ER er = E_OK;
	MPLCB *m;

	state = hal_int_disable();
	m = get_mpl(mplid, &er);
	if (m == 0) {
		hal_int_restore(state);
		return er;
	}
	while (!knl_queue_empty(&m->wait)) {
		knl_wait_release(m->wait.head, E_DLT);
	}
	m->used = False;
	knl_dispatch();
	hal_int_restore(state);
	knl_free_pages((UD)(VP)m->head, (SZ)m->mplsz);
	m->head = 0;
	return E_OK;
}

EXPORT ER tk_get_mpl(ID mplid, W blksz, VP *p_blk, TMO tmout)
{
	UD state;
	ER er = E_OK;
	MPLCB *m;
	TCB *me;
	VP blk;

	if (p_blk == 0 || blksz < 1 || tmout < TMO_FEVR) {
		return E_PAR;
	}
	state = hal_int_disable();
	m = get_mpl(mplid, &er);
	if (m == 0) {
		hal_int_restore(state);
		return er;
	}
	if ((UD)blksz > (UD)m->mplsz) {
		hal_int_restore(state);
		return E_PAR; /* プールより大きい要求は永久に満たせない */
	}
	if (knl_queue_empty(&m->wait)) {
		blk = mpl_alloc(m, (UD)blksz);
		if (blk != 0) {
			*p_blk = blk;
			hal_int_restore(state);
			return E_OK;
		}
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
	me->wait_cnt = blksz;
	me->rdv_msg = (VP)p_blk;
	er = knl_wait(&m->wait, TTW_MPL, mplid, tmout);
	hal_int_restore(state);
	return er;
}

EXPORT ER tk_rel_mpl(ID mplid, VP blk)
{
	UD state;
	ER er = E_OK;
	MPLCB *m;
	MPLHDR *h;

	if (blk == 0) {
		return E_PAR;
	}
	state = hal_int_disable();
	m = get_mpl(mplid, &er);
	if (m == 0) {
		hal_int_restore(state);
		return er;
	}
	h = (MPLHDR *)(VP)((UD)blk - MPL_HDRSZ);
	if (h->inuse == 0) {
		hal_int_restore(state);
		return E_PAR; /* 二重解放 */
	}
	mpl_free(m, h);
	mpl_distribute(m);
	knl_dispatch();
	hal_int_restore(state);
	return E_OK;
}

EXPORT ER tk_ref_mpl(ID mplid, T_RMPL *pk_rmpl)
{
	UD state;
	ER er = E_OK;
	MPLCB *m;
	CONST MPLHDR *h;
	UD fr = 0;
	UD mx = 0;

	if (pk_rmpl == 0) {
		return E_PAR;
	}
	state = hal_int_disable();
	m = get_mpl(mplid, &er);
	if (m == 0) {
		hal_int_restore(state);
		return er;
	}
	for (h = m->head; h != 0; h = h->next) {
		if (h->inuse != 0) {
			continue;
		}
		fr += h->size;
		if (h->size > mx) {
			mx = h->size;
		}
	}
	pk_rmpl->exinf = m->exinf;
	pk_rmpl->wtsk = (m->wait.head != 0) ? m->wait.head->tskid : 0;
	pk_rmpl->frsz = (W)fr;
	pk_rmpl->maxsz = (W)mx;
	hal_int_restore(state);
	return E_OK;
}

EXPORT void knl_mp_init(void)
{
	W i;

	for (i = 0; i <= TK_MAX_MPFID; i++) {
		mpfcb[i].used = False;
		knl_queue_init(&mpfcb[i].wait, TA_TFIFO);
	}
	for (i = 0; i <= TK_MAX_MPLID; i++) {
		mplcb[i].used = False;
		mplcb[i].head = 0;
		knl_queue_init(&mplcb[i].wait, TA_TFIFO);
	}
}

/*
 * task.c — タスク管理機能 (μT-Kernel 3.0)
 *
 * tk_cre_tsk / tk_del_tsk / tk_sta_tsk / tk_ext_tsk / tk_exd_tsk /
 * tk_ter_tsk / tk_chg_pri / tk_get_tid / tk_ref_tsk
 *
 * 【仕様の要点】
 *   - 生成直後は休止状態 (DORMANT)。tk_sta_tsk で実行可能状態になる
 *   - 優先度は 1 以上で、小さい方が高い
 *   - 自タスク操作と他タスク操作は明確に分かれている
 *     自: tk_ext_tsk (実行 → 休止) / tk_exd_tsk (実行 → 未登録)
 *     他: tk_ter_tsk (実行可能・待ち → 休止) / tk_del_tsk (休止 → 未登録)
 *   - **カーネルはタスク終了時に資源を解放しない。** セマフォ資源などを
 *     解放するのはアプリケーションの責任（仕様が明記している）
 */

#include "knl.h"

EXPORT TCB knl_tcb[TK_MAX_TSKID + 1];

/* ------------------------------------------------------------------ */
/* 補助                                                                */
/* ------------------------------------------------------------------ */

EXPORT TCB *knl_get_tcb(ID tskid, ER *er)
{
	TCB *t;

	if (tskid == TSK_SELF) {
		if (knl_cur == &knl_idle) {
			*er = E_CTX; /* アイドル（タスク独立部相当）からの自タスク指定 */
			return 0;
		}
		return knl_cur;
	}
	if (tskid < 1 || tskid > TK_MAX_TSKID) {
		*er = E_ID;
		return 0;
	}
	t = &knl_tcb[tskid];
	if (!t->used) {
		*er = E_NOEXS;
		return 0;
	}
	return t;
}

EXPORT UD knl_alloc_pages(SZ size)
{
	UD ps = (UD)hal_page_size();
	UD pages = ((UD)size + ps - 1) / ps;
	UD base;
	UD i;

	if (pages == 0) {
		return 0;
	}
	/* 本物のアロケータがあればそちらから。解放できる。 */
	base = pmem_alloc((UW)pages);
	if (base != 0) {
		return BTRON_PA_TO_VA(base);
	}
	/* 立っていない（メモリマップが無い直接起動）。起動初期のバンプ
	 * アロケータから取る。連続を返す前提なので、崩れたら諦める。 */
	base = 0;
	for (i = 0; i < pages; i++) {
		UD pa = pmem_early_alloc_page();

		if (pa == 0) {
			return 0;
		}
		if (i == 0) {
			base = pa;
		} else if (pa != base + i * ps) {
			return 0;
		}
	}
	return (base != 0) ? BTRON_PA_TO_VA(base) : 0;
}

EXPORT void knl_free_pages(UD va, SZ size)
{
	UD ps = (UD)hal_page_size();
	UD pages = ((UD)size + ps - 1) / ps;

	if (va == 0 || pages == 0 || va < BTRON_DIRECTMAP_BASE) {
		return;
	}
	/* 起動初期のバンプアロケータから出たものは返せない。
	 * pmem_owns() が管理外と答えるので、そのまま捨てる。 */
	pmem_free(va - BTRON_DIRECTMAP_BASE, (UW)pages);
}

EXPORT UD knl_alloc_stack(SZ size)
{
	UD ps = (UD)hal_page_size();
	UD pages = ((UD)size + ps - 1) / ps;
	UD base = knl_alloc_pages(size);

	if (base == 0) {
		return 0;
	}
	/* 上端を返す。スタックは下向きに伸びる。 */
	return base + pages * ps;
}

/* ------------------------------------------------------------------ */
/* 生成と削除                                                          */
/* ------------------------------------------------------------------ */

EXPORT ID tk_cre_tsk(CONST T_CTSK *pk_ctsk)
{
	UD state;
	TCB *t = 0;
	ID id = 0;
	W i;
	UD stack_top;
	SZ stksz;

	if (pk_ctsk == 0 || pk_ctsk->task == 0) {
		return E_PAR;
	}
	if (pk_ctsk->itskpri < 1 || pk_ctsk->itskpri > TK_MAX_TSKPRI) {
		return E_PAR;
	}
	/* TA_ASM は未サポート。仕様は E_NOSPT を返せと言っている。 */
	if ((pk_ctsk->tskatr & TA_HLNG) == 0) {
		return E_NOSPT;
	}
	if ((pk_ctsk->tskatr & (TA_USERSTACK | TA_USERBUF | TA_SSTKSZ)) != 0) {
		return E_NOSPT;
	}
	stksz = (pk_ctsk->stksz < TK_MIN_STKSZ) ? TK_MIN_STKSZ : pk_ctsk->stksz;

	state = hal_int_disable();
	for (i = 1; i <= TK_MAX_TSKID; i++) {
		if (!knl_tcb[i].used) {
			t = &knl_tcb[i];
			id = i;
			break;
		}
	}
	hal_int_restore(state);
	if (t == 0) {
		return E_LIMIT;
	}

	/* スタックの確保は割込みを許したまま行う（確保が長いので）。
	 * この TCB はまだ used = False なので他から掴まれない。 */
	stack_top = knl_alloc_stack(stksz);
	if (stack_top == 0) {
		return E_NOMEM;
	}

	state = hal_int_disable();
	t->next = 0;
	t->prev = 0;
	t->queue = 0;
	t->tskid = id;
	t->exinf = pk_ctsk->exinf;
	t->tskatr = pk_ctsk->tskatr;
	t->entry = pk_ctsk->task;
	t->itskpri = pk_ctsk->itskpri;
	t->bpri = pk_ctsk->itskpri;
	t->pri = pk_ctsk->itskpri;
	t->state = TTS_DMT;
	t->waitcause = 0;
	t->wid = 0;
	t->waitresult = E_OK;
	t->wupcnt = 0;
	t->suscnt = 0;
	t->wait_cnt = 0;
	t->wait_ptn = 0;
	t->wait_mode = 0;
	t->wait_flgptn = 0;
	t->timeout = 0;
	t->mtxlist = 0;
	t->sp = 0;
	t->stack = stack_top - (UD)stksz;
	t->stksz = stksz;
	for (i = 0; i < 8; i++) {
		t->dsname[i] = ((pk_ctsk->tskatr & TA_DSNAME) != 0) ? pk_ctsk->dsname[i] : 0;
	}
	t->used = True;
	hal_int_restore(state);
	return id;
}

EXPORT ER tk_del_tsk(ID tskid)
{
	UD state;
	ER er = E_OK;
	TCB *t;

	if (tskid < 1 || tskid > TK_MAX_TSKID) {
		return E_ID;
	}
	state = hal_int_disable();
	t = knl_get_tcb(tskid, &er);
	if (t == 0) {
		hal_int_restore(state);
		return er;
	}
	/* 休止状態のタスクしか削除できない（仕様）。 */
	if (t->state != TTS_DMT) {
		hal_int_restore(state);
		return E_OBJ;
	}
	t->used = False;
	t->state = 0;
	hal_int_restore(state);

	/* スタックを返す。割込みを許してから返す（解放は長い）。
	 * この TCB はもう used = False なので他から掴まれない。 */
	knl_free_pages(t->stack, t->stksz);
	t->stack = 0;
	t->stksz = 0;
	return E_OK;
}

/* ------------------------------------------------------------------ */
/* 起動と終了                                                          */
/* ------------------------------------------------------------------ */

EXPORT ER tk_sta_tsk(ID tskid, INT stacd)
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
	if (t->state != TTS_DMT) {
		hal_int_restore(state);
		return E_OBJ;
	}
	/* 休止状態からの起動なのでレジスタは保証されない（仕様）。
	 * 起動時優先度に戻し、起床要求と強制待ち要求をクリアする。 */
	t->pri = t->itskpri;
	t->bpri = t->itskpri;
	t->wupcnt = 0;
	t->suscnt = 0;
	t->waitcause = 0;
	t->wid = 0;
	t->timeout = 0;
	t->sp = hal_ctx_init(t->stack + (UD)t->stksz, t->entry, stacd, t->exinf);

	/* 起動は「実行できる状態になった」ので同一優先度の末尾 */
	knl_make_ready(t, False);
	knl_dispatch();
	hal_int_restore(state);
	return E_OK;
}

/* 自タスクを休止状態にする。戻らない。 */
EXPORT void tk_ext_tsk(void)
{
	TCB *me;

	(void)hal_int_disable();
	me = knl_cur;
	if (me == &knl_idle) {
		/* アイドルは終了できない。設計の誤りなので止める。 */
		hal_exit(3);
	}
	/* 仕様: タスクが終了する時にロックしているミューテックスが残っていたら
	 * すべてロック解除する。これをしないと資源が永久に握られる。
	 * （なおセマフォ資源などは解放しない。仕様がアプリの責任としている。） */
	knl_mtx_release_all(me);
	knl_queue_remove(me);
	knl_unready(me);
	me->state = TTS_DMT;
	me->waitcause = 0;
	me->wid = 0;
	me->wupcnt = 0;
	me->suscnt = 0;
	knl_dispatch();
	/* 休止したタスクは選ばれないので戻らない。 */
	hal_exit(3);
}

EXPORT void tk_exd_tsk(void)
{
	TCB *me;

	(void)hal_int_disable();
	me = knl_cur;
	if (me == &knl_idle) {
		hal_exit(3);
	}
	knl_mtx_release_all(me);
	knl_queue_remove(me);
	knl_unready(me);
	me->state = TTS_DMT;
	me->used = False;
	knl_dispatch();
	hal_exit(3);
}

EXPORT ER tk_ter_tsk(ID tskid)
{
	UD state;
	ER er = E_OK;
	TCB *t;

	if (tskid == TSK_SELF) {
		/* 自タスクには tk_ext_tsk を使う（仕様が自他を分けている）。 */
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
	knl_mtx_release_all(t);
	knl_queue_remove(t); /* 待ち行列から外す */
	knl_unready(t);
	t->state = TTS_DMT;
	t->waitcause = 0;
	t->wid = 0;
	t->wupcnt = 0;
	t->suscnt = 0;
	t->timeout = 0;
	hal_int_restore(state);
	return E_OK;
}

/* ------------------------------------------------------------------ */
/* 優先度と参照                                                        */
/* ------------------------------------------------------------------ */

EXPORT ER tk_chg_pri(ID tskid, PRI tskpri)
{
	UD state;
	ER er = E_OK;
	TCB *t;
	PRI newpri;

	state = hal_int_disable();
	t = knl_get_tcb(tskid, &er);
	if (t == 0) {
		hal_int_restore(state);
		return er;
	}
	/* tskpri = 0 は起動時優先度に戻す指定（仕様）。 */
	newpri = (tskpri == 0) ? t->itskpri : tskpri;
	if (newpri < 1 || newpri > TK_MAX_TSKPRI) {
		hal_int_restore(state);
		return E_PAR;
	}
	/* TA_CEILING のミューテックスに関わっているタスクのベース優先度を、
	 * 上限優先度より高くしようとすると E_ILUSE（仕様）。 */
	if (knl_mtx_ceiling_violation(t, newpri)) {
		hal_int_restore(state);
		return E_ILUSE;
	}
	t->bpri = newpri;
	/* ミューテックスをロックしていない間は現在優先度 = ベース優先度。
	 * ロック中なら継承した優先度を保ったまま、ベースだけ変える。 */
	if (t->mtxlist == 0) {
		knl_set_pri(t, newpri);
	}
	knl_dispatch();
	hal_int_restore(state);
	return E_OK;
}

EXPORT ID tk_get_tid(void)
{
	return (knl_cur == &knl_idle) ? 0 : knl_cur->tskid;
}

EXPORT ER tk_ref_tsk(ID tskid, T_RTSK *pk_rtsk)
{
	UD state;
	ER er = E_OK;
	TCB *t;

	if (pk_rtsk == 0) {
		return E_PAR;
	}
	state = hal_int_disable();
	t = knl_get_tcb(tskid, &er);
	if (t == 0) {
		hal_int_restore(state);
		return er;
	}
	pk_rtsk->exinf = t->exinf;
	pk_rtsk->tskpri = t->pri;
	pk_rtsk->tskbpri = t->bpri;
	pk_rtsk->tskstat = t->state;
	/* 待ち状態でなければ tskwait, wid は 0（仕様） */
	pk_rtsk->tskwait = ((t->state & TTS_WAI) != 0) ? t->waitcause : 0;
	pk_rtsk->wid = ((t->state & TTS_WAI) != 0) ? t->wid : 0;
	pk_rtsk->wupcnt = t->wupcnt;
	pk_rtsk->suscnt = t->suscnt;
	hal_int_restore(state);
	return E_OK;
}

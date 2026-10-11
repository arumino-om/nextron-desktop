/*
 * cyclic.c — 周期ハンドラとアラームハンドラ (μT-Kernel 3.0 時間管理機能)
 *
 *   周期ハンドラ    一定周期で繰り返し起動される
 *   アラームハンドラ 指定時刻に 1 回だけ起動される
 *
 * **どちらもタスク独立部で走る。** タイマ割込みの中から呼ばれるので、
 * 自タスクを待ち状態にするシステムコールを呼んではならない（E_CTX）。
 * ハンドラの中から tk_wup_tsk や tk_set_flg でタスクを起こすのが基本形。
 *
 * 【TA_PHS の意味】起動位相を保存するかどうか。
 *
 *   TA_PHS なし  tk_sta_cyc を呼んだ時点から cyctim ごとに起動する
 *   TA_PHS あり  **生成時を基点にした周期を保ち続ける**。停止・再開しても
 *                位相がずれない。周期的なサンプリングで位相が重要な場合に使う
 *
 * 【tick ごとの走査】オブジェクト数が少ない（各 16）ので、毎 tick に全数
 * 走査する。ソート済みのタイマキューにするのは、数が増えてからでよい
 * （タイムアウト検査と同じ判断。D9 と一緒に見直す）。
 */

#include "knl.h"

typedef struct CYCCB {
	Bool used;
	VP exinf;
	ATR cycatr;
	CYCHANDLER hdr;
	RELTIM cyctim;  /* 周期 */
	SYSTIM next;    /* 次に起動する時刻 */
	Bool running;
	UB dsname[8];
} CYCCB;

typedef struct ALMCB {
	Bool used;
	VP exinf;
	ATR almatr;
	ALMHANDLER hdr;
	SYSTIM at; /* 起動時刻。running でなければ意味なし */
	Bool running;
	UB dsname[8];
} ALMCB;

LOCAL CYCCB cyccb[TK_MAX_CYCID + 1];
LOCAL ALMCB almcb[TK_MAX_ALMID + 1];

EXPORT void knl_cyc_init(void)
{
	W i;

	for (i = 0; i <= TK_MAX_CYCID; i++) {
		cyccb[i].used = False;
		cyccb[i].running = False;
	}
	for (i = 0; i <= TK_MAX_ALMID; i++) {
		almcb[i].used = False;
		almcb[i].running = False;
	}
}

/* ------------------------------------------------------------------ */
/* 周期ハンドラ                                                        */
/* ------------------------------------------------------------------ */

LOCAL CYCCB *get_cyc(ID id, ER *er)
{
	if (id < 1 || id > TK_MAX_CYCID) {
		*er = E_ID;
		return 0;
	}
	if (!cyccb[id].used) {
		*er = E_NOEXS;
		return 0;
	}
	return &cyccb[id];
}

EXPORT ID tk_cre_cyc(CONST T_CCYC *pk_ccyc)
{
	UD state;
	ID id = 0;
	W i;

	if (pk_ccyc == 0 || pk_ccyc->cychdr == 0 || pk_ccyc->cyctim == 0) {
		return E_PAR;
	}
	if ((pk_ccyc->cycatr & ~(TA_HLNG | TA_STA | TA_PHS | TA_DSNAME)) != 0) {
		return E_RSATR;
	}
	if ((pk_ccyc->cycatr & TA_HLNG) == 0) {
		return E_NOSPT; /* TA_ASM は未サポート */
	}
	state = hal_int_disable();
	for (i = 1; i <= TK_MAX_CYCID; i++) {
		if (!cyccb[i].used) {
			id = i;
			break;
		}
	}
	if (id == 0) {
		hal_int_restore(state);
		return E_LIMIT;
	}
	cyccb[id].used = True;
	cyccb[id].exinf = pk_ccyc->exinf;
	cyccb[id].cycatr = pk_ccyc->cycatr;
	cyccb[id].hdr = pk_ccyc->cychdr;
	cyccb[id].cyctim = pk_ccyc->cyctim;
	/* 最初の起動は生成時 + cycphs。TA_PHS の有無に関わらずここが基点。 */
	cyccb[id].next = knl_otm + (SYSTIM)pk_ccyc->cycphs;
	cyccb[id].running = ((pk_ccyc->cycatr & TA_STA) != 0) ? True : False;
	for (i = 0; i < 8; i++) {
		cyccb[id].dsname[i] =
		    ((pk_ccyc->cycatr & TA_DSNAME) != 0) ? pk_ccyc->dsname[i] : 0;
	}
	hal_int_restore(state);
	return id;
}

EXPORT ER tk_del_cyc(ID cycid)
{
	UD state;
	ER er = E_OK;
	CYCCB *c;

	state = hal_int_disable();
	c = get_cyc(cycid, &er);
	if (c == 0) {
		hal_int_restore(state);
		return er;
	}
	c->used = False;
	c->running = False;
	hal_int_restore(state);
	return E_OK;
}

EXPORT ER tk_sta_cyc(ID cycid)
{
	UD state;
	ER er = E_OK;
	CYCCB *c;

	state = hal_int_disable();
	c = get_cyc(cycid, &er);
	if (c == 0) {
		hal_int_restore(state);
		return er;
	}
	if ((c->cycatr & TA_PHS) != 0) {
		/* 位相を保存する。生成時を基点にした周期の、次の境界まで進める。
		 * 停止・再開しても位相がずれないのがこの属性の値打ち。 */
		while (c->next <= knl_otm) {
			c->next += (SYSTIM)c->cyctim;
		}
	} else {
		/* 位相を保存しない。いまから数え直す。 */
		c->next = knl_otm + (SYSTIM)c->cyctim;
	}
	c->running = True;
	hal_int_restore(state);
	return E_OK;
}

EXPORT ER tk_stp_cyc(ID cycid)
{
	UD state;
	ER er = E_OK;
	CYCCB *c;

	state = hal_int_disable();
	c = get_cyc(cycid, &er);
	if (c == 0) {
		hal_int_restore(state);
		return er;
	}
	c->running = False;
	hal_int_restore(state);
	return E_OK;
}

EXPORT ER tk_ref_cyc(ID cycid, T_RCYC *pk_rcyc)
{
	UD state;
	ER er = E_OK;
	CYCCB *c;

	if (pk_rcyc == 0) {
		return E_PAR;
	}
	state = hal_int_disable();
	c = get_cyc(cycid, &er);
	if (c == 0) {
		hal_int_restore(state);
		return er;
	}
	pk_rcyc->exinf = c->exinf;
	pk_rcyc->lfttim = (c->next > knl_otm) ? (RELTIM)(c->next - knl_otm) : 0;
	pk_rcyc->cycstat = c->running ? TCYC_STA : TCYC_STOP;
	hal_int_restore(state);
	return E_OK;
}

/* ------------------------------------------------------------------ */
/* アラームハンドラ                                                    */
/* ------------------------------------------------------------------ */

LOCAL ALMCB *get_alm(ID id, ER *er)
{
	if (id < 1 || id > TK_MAX_ALMID) {
		*er = E_ID;
		return 0;
	}
	if (!almcb[id].used) {
		*er = E_NOEXS;
		return 0;
	}
	return &almcb[id];
}

EXPORT ID tk_cre_alm(CONST T_CALM *pk_calm)
{
	UD state;
	ID id = 0;
	W i;

	if (pk_calm == 0 || pk_calm->almhdr == 0) {
		return E_PAR;
	}
	if ((pk_calm->almatr & ~(TA_HLNG | TA_DSNAME)) != 0) {
		return E_RSATR;
	}
	if ((pk_calm->almatr & TA_HLNG) == 0) {
		return E_NOSPT;
	}
	state = hal_int_disable();
	for (i = 1; i <= TK_MAX_ALMID; i++) {
		if (!almcb[i].used) {
			id = i;
			break;
		}
	}
	if (id == 0) {
		hal_int_restore(state);
		return E_LIMIT;
	}
	almcb[id].used = True;
	almcb[id].exinf = pk_calm->exinf;
	almcb[id].almatr = pk_calm->almatr;
	almcb[id].hdr = pk_calm->almhdr;
	almcb[id].running = False; /* 生成時は停止状態（仕様） */
	almcb[id].at = 0;
	for (i = 0; i < 8; i++) {
		almcb[id].dsname[i] =
		    ((pk_calm->almatr & TA_DSNAME) != 0) ? pk_calm->dsname[i] : 0;
	}
	hal_int_restore(state);
	return id;
}

EXPORT ER tk_del_alm(ID almid)
{
	UD state;
	ER er = E_OK;
	ALMCB *a;

	state = hal_int_disable();
	a = get_alm(almid, &er);
	if (a == 0) {
		hal_int_restore(state);
		return er;
	}
	a->used = False;
	a->running = False;
	hal_int_restore(state);
	return E_OK;
}

EXPORT ER tk_sta_alm(ID almid, RELTIM almtim)
{
	UD state;
	ER er = E_OK;
	ALMCB *a;

	state = hal_int_disable();
	a = get_alm(almid, &er);
	if (a == 0) {
		hal_int_restore(state);
		return er;
	}
	/* 既に動作中なら上書きする（仕様: 以前の起動要求は取り消される）。 */
	a->at = knl_otm + (SYSTIM)almtim;
	a->running = True;
	hal_int_restore(state);
	return E_OK;
}

EXPORT ER tk_stp_alm(ID almid)
{
	UD state;
	ER er = E_OK;
	ALMCB *a;

	state = hal_int_disable();
	a = get_alm(almid, &er);
	if (a == 0) {
		hal_int_restore(state);
		return er;
	}
	a->running = False;
	hal_int_restore(state);
	return E_OK;
}

EXPORT ER tk_ref_alm(ID almid, T_RALM *pk_ralm)
{
	UD state;
	ER er = E_OK;
	ALMCB *a;

	if (pk_ralm == 0) {
		return E_PAR;
	}
	state = hal_int_disable();
	a = get_alm(almid, &er);
	if (a == 0) {
		hal_int_restore(state);
		return er;
	}
	pk_ralm->exinf = a->exinf;
	pk_ralm->lfttim =
	    (a->running && a->at > knl_otm) ? (RELTIM)(a->at - knl_otm) : 0;
	pk_ralm->almstat = a->running ? TALM_STA : TALM_STOP;
	hal_int_restore(state);
	return E_OK;
}

/* ------------------------------------------------------------------ */
/* tick ごとの処理                                                     */
/*                                                                     */
/* タイマ割込みの中から呼ばれる。ハンドラもここで走るので、            */
/* **ハンドラの実行中は割込み禁止**になっている。長い処理を書くと        */
/* 割込み応答が悪くなるので、ハンドラはタスクを起こすだけにする。       */
/* ------------------------------------------------------------------ */

EXPORT void knl_cyc_tick(void)
{
	W i;

	for (i = 1; i <= TK_MAX_CYCID; i++) {
		CYCCB *c = &cyccb[i];

		if (!c->used || !c->running || knl_otm < c->next) {
			continue;
		}
		/* 次の起動時刻を先に決める。ハンドラが tk_stp_cyc を呼んでも
		 * 辻褄が合うようにしておく。
		 * 遅れた分は飛ばす（起動が溜まって追いかけ続けるのを防ぐ）。 */
		do {
			c->next += (SYSTIM)c->cyctim;
		} while (c->next <= knl_otm);
		c->hdr(c->exinf);
	}
	for (i = 1; i <= TK_MAX_ALMID; i++) {
		ALMCB *a = &almcb[i];

		if (!a->used || !a->running || knl_otm < a->at) {
			continue;
		}
		a->running = False; /* 1 回だけ（仕様） */
		a->hdr(a->exinf);
	}
}

/*
 * mtx.c — ミューテックス (μT-Kernel 3.0 拡張同期・通信機能)
 *
 * セマフォと違い、**ロックしたタスクしか解除できず、タスク終了時に自動で
 * 解除される**。そして上限のない優先度逆転を防ぐ 2 つのプロトコルを持つ。
 *
 *   TA_INHERIT  優先度継承プロトコル。ロック待ちに入るとき、ロックしている
 *               タスクの優先度を自分と同じまで引き上げる
 *   TA_CEILING  優先度上限プロトコル。生成時に上限優先度を決め、ロックした
 *               タスクをそこまで引き上げる（厳密には highest locker protocol）
 *
 * 【属性は列挙であってビットフラグではない】ここが引っかかりやすい。
 *
 *   TA_TFIFO  = 0x00   待ち行列は FIFO
 *   TA_TPRI   = 0x01   待ち行列は優先度順
 *   TA_INHERIT = 0x02  優先度順 ＋ 優先度継承
 *   TA_CEILING = 0x03  優先度順 ＋ 優先度上限
 *
 * **下位 2 ビットの値で選ぶ**ので `atr & TA_TPRI` で並びを判定すると
 * TA_INHERIT(0x02) が FIFO になってしまう。待ち行列の並びは
 * 「TA_TFIFO 以外なら優先度順」で決める。
 * 同じ 0x02 がセマフォでは TA_CNT、メールボックスでは TA_MPRI なので、
 * 生成時に KNL_ORDER() で正規化してから Queue へ渡す。
 *
 * 【優先度制御規則は簡略版を採る】仕様は 2 つを規定し、どちらを使うかを
 * 実装定義としている。
 *
 *   厳密な規則    現在優先度を常に「ベース優先度・待ちタスクの最高優先度・
 *                 上限優先度」の最高値に一致させる。推移的な優先度継承も行う
 *   簡略化した規則 **引き上げは全部行い、引き下げはロックしているものが
 *                 無くなったときだけ**（ベース優先度に戻す）
 *
 * **簡略版を採る。** 引き下げの判定には「このタスクがロックしている全
 * ミューテックスの待ち行列を走査する」必要があり、ロック・アンロックの
 * たびにそれをやると、現代的な多コア機ではロック区間が長くなる。
 * 仕様が明示的に認めている逃げ道なので使う。
 */

#include "knl.h"

struct MTXCB {
	Bool used;
	VP exinf;
	ATR mtxatr;
	PRI ceilpri; /* TA_CEILING の上限優先度 */
	TCB *holder; /* ロックしているタスク。0 なら未ロック */
	MTXCB *next_locked; /* holder がロックしている連鎖の次 */
	Queue wait;
	UB dsname[8];
};

LOCAL MTXCB mtxcb[TK_MAX_MTXID + 1];

LOCAL MTXCB *get_mtx(ID mtxid, ER *er)
{
	if (mtxid < 1 || mtxid > TK_MAX_MTXID) {
		*er = E_ID;
		return 0;
	}
	if (!mtxcb[mtxid].used) {
		*er = E_NOEXS;
		return 0;
	}
	return &mtxcb[mtxid];
}

LOCAL inline UW proto(CONST MTXCB *m)
{
	return m->mtxatr & TA_MTXPROTO;
}

EXPORT void knl_mtx_init(void)
{
	W i;

	for (i = 0; i <= TK_MAX_MTXID; i++) {
		mtxcb[i].used = False;
		mtxcb[i].holder = 0;
		mtxcb[i].next_locked = 0;
		knl_queue_init(&mtxcb[i].wait, TA_TFIFO);
	}
}

/* ------------------------------------------------------------------ */
/* ロック連鎖の出し入れ                                                */
/* ------------------------------------------------------------------ */

LOCAL void lock_link(TCB *t, MTXCB *m)
{
	m->holder = t;
	m->next_locked = t->mtxlist;
	t->mtxlist = m;
}

LOCAL void lock_unlink(TCB *t, MTXCB *m)
{
	MTXCB **pp = &t->mtxlist;

	while (*pp != 0) {
		if (*pp == m) {
			*pp = m->next_locked;
			break;
		}
		pp = &(*pp)->next_locked;
	}
	m->next_locked = 0;
	m->holder = 0;
}

/* 簡略化した規則の引き下げ。ロックしているものが無くなったら
 * ベース優先度に戻す。残っていれば触らない。 */
LOCAL void maybe_restore_pri(TCB *t)
{
	if (t->mtxlist == 0 && t->pri != t->bpri) {
		knl_set_pri(t, t->bpri);
	}
}

/* ロックを獲得したときの優先度の引き上げ（TA_CEILING） */
LOCAL void raise_for_ceiling(TCB *t, CONST MTXCB *m)
{
	if (proto(m) == TA_CEILING && t->pri > m->ceilpri) {
		knl_set_pri(t, m->ceilpri);
	}
}

/* ------------------------------------------------------------------ */
/* 生成と削除                                                          */
/* ------------------------------------------------------------------ */

EXPORT ID tk_cre_mtx(CONST T_CMTX *pk_cmtx)
{
	UD state;
	ID id = 0;
	W i;
	UW pr;

	if (pk_cmtx == 0) {
		return E_PAR;
	}
	if ((pk_cmtx->mtxatr & ~(TA_MTXPROTO | TA_DSNAME | TA_NODISWAI)) != 0) {
		return E_RSATR;
	}
	pr = pk_cmtx->mtxatr & TA_MTXPROTO;
	if (pr == TA_CEILING &&
	    (pk_cmtx->ceilpri < 1 || pk_cmtx->ceilpri > TK_MAX_TSKPRI)) {
		return E_PAR;
	}

	state = hal_int_disable();
	for (i = 1; i <= TK_MAX_MTXID; i++) {
		if (!mtxcb[i].used) {
			id = i;
			break;
		}
	}
	if (id == 0) {
		hal_int_restore(state);
		return E_LIMIT;
	}
	mtxcb[id].used = True;
	mtxcb[id].exinf = pk_cmtx->exinf;
	mtxcb[id].mtxatr = pk_cmtx->mtxatr;
	mtxcb[id].ceilpri = (pr == TA_CEILING) ? pk_cmtx->ceilpri : 0;
	mtxcb[id].holder = 0;
	mtxcb[id].next_locked = 0;
	/* TA_INHERIT / TA_CEILING も優先度順。TA_TFIFO 以外は全部そう。 */
	knl_queue_init(&mtxcb[id].wait, KNL_ORDER(pr != TA_TFIFO));
	for (i = 0; i < 8; i++) {
		mtxcb[id].dsname[i] =
		    ((pk_cmtx->mtxatr & TA_DSNAME) != 0) ? pk_cmtx->dsname[i] : 0;
	}
	hal_int_restore(state);
	return id;
}

EXPORT ER tk_del_mtx(ID mtxid)
{
	UD state;
	ER er = E_OK;
	MTXCB *m;

	state = hal_int_disable();
	m = get_mtx(mtxid, &er);
	if (m == 0) {
		hal_int_restore(state);
		return er;
	}
	while (!knl_queue_empty(&m->wait)) {
		knl_wait_release(m->wait.head, E_DLT);
	}
	if (m->holder != 0) {
		TCB *h = m->holder;

		lock_unlink(h, m);
		maybe_restore_pri(h);
	}
	m->used = False;
	knl_dispatch();
	hal_int_restore(state);
	return E_OK;
}

/* ------------------------------------------------------------------ */
/* ロックとアンロック                                                  */
/* ------------------------------------------------------------------ */

EXPORT ER tk_loc_mtx(ID mtxid, TMO tmout)
{
	UD state;
	ER er = E_OK;
	MTXCB *m;
	TCB *me;

	if (tmout < TMO_FEVR) {
		return E_PAR;
	}
	state = hal_int_disable();
	m = get_mtx(mtxid, &er);
	if (m == 0) {
		hal_int_restore(state);
		return er;
	}
	me = knl_cur;
	if (me == &knl_idle) {
		hal_int_restore(state);
		return E_CTX;
	}
	if (m->holder == me) {
		hal_int_restore(state);
		return E_ILUSE; /* 多重ロック */
	}
	/* TA_CEILING: 上限優先度より高いベース優先度のタスクはロックできない。
	 * 「高い」は数が小さい方なので比較が逆に見える（仕様）。 */
	if (proto(m) == TA_CEILING && me->bpri < m->ceilpri) {
		hal_int_restore(state);
		return E_ILUSE; /* 上限優先度違反 */
	}

	if (m->holder == 0) {
		lock_link(me, m);
		raise_for_ceiling(me, m);
		knl_dispatch();
		hal_int_restore(state);
		return E_OK;
	}
	if (tmout == TMO_POL) {
		hal_int_restore(state);
		return E_TMOUT;
	}
	/* TA_INHERIT: ロックしているタスクの優先度が自分より低ければ、
	 * 自分と同じまで引き上げる。これが優先度継承。
	 * 引き上げないと、低優先度のロック保持者が中優先度のタスクに
	 * 追い越され続け、自分の待ち時間に上限が無くなる。 */
	if (proto(m) == TA_INHERIT && m->holder->pri > me->pri) {
		knl_set_pri(m->holder, me->pri);
	}
	er = knl_wait(&m->wait, TTW_MTX, mtxid, tmout);
	hal_int_restore(state);
	return er;
}

EXPORT ER tk_unl_mtx(ID mtxid)
{
	UD state;
	ER er = E_OK;
	MTXCB *m;
	TCB *me;

	state = hal_int_disable();
	m = get_mtx(mtxid, &er);
	if (m == 0) {
		hal_int_restore(state);
		return er;
	}
	me = knl_cur;
	if (m->holder != me) {
		/* ロックしたタスクしか解除できない。セマフォとの違い。 */
		hal_int_restore(state);
		return E_ILUSE;
	}
	lock_unlink(me, m);
	maybe_restore_pri(me);

	/* 待っているタスクがいれば、その先頭に**ロックを渡す**。
	 * ここで渡さずに起こすだけだと、起きた先で別のタスクに
	 * 横取りされうる（ロック待ち行列の順序が意味を失う）。 */
	if (!knl_queue_empty(&m->wait)) {
		TCB *t = m->wait.head;

		knl_wait_release(t, E_OK);
		lock_link(t, m);
		raise_for_ceiling(t, m);
	}
	knl_dispatch();
	hal_int_restore(state);
	return E_OK;
}

EXPORT ER tk_ref_mtx(ID mtxid, T_RMTX *pk_rmtx)
{
	UD state;
	ER er = E_OK;
	MTXCB *m;

	if (pk_rmtx == 0) {
		return E_PAR;
	}
	state = hal_int_disable();
	m = get_mtx(mtxid, &er);
	if (m == 0) {
		hal_int_restore(state);
		return er;
	}
	pk_rmtx->exinf = m->exinf;
	pk_rmtx->htsk = (m->holder != 0) ? m->holder->tskid : 0;
	pk_rmtx->wtsk = (m->wait.head != 0) ? m->wait.head->tskid : 0;
	hal_int_restore(state);
	return E_OK;
}

/* ------------------------------------------------------------------ */
/* 他のモジュールから使うもの                                          */
/* ------------------------------------------------------------------ */

/* タスク終了時にロックを全部外す（仕様が要求している）。
 * 外す順序は実装依存なので、連鎖の先頭から外す。 */
EXPORT void knl_mtx_release_all(TCB *t)
{
	while (t->mtxlist != 0) {
		MTXCB *m = t->mtxlist;

		lock_unlink(t, m);
		if (!knl_queue_empty(&m->wait)) {
			TCB *w = m->wait.head;

			knl_wait_release(w, E_OK);
			lock_link(w, m);
			raise_for_ceiling(w, m);
		}
	}
	/* 終了するタスクなので優先度は戻さなくてよい。
	 * tk_sta_tsk で起動時優先度に初期化される。 */
}

EXPORT Bool knl_mtx_ceiling_violation(CONST TCB *t, PRI newpri)
{
	CONST MTXCB *m;
	W i;

	/* ロック中の TA_CEILING ミューテックスの上限より高くはできない */
	for (m = t->mtxlist; m != 0; m = m->next_locked) {
		if (proto(m) == TA_CEILING && newpri < m->ceilpri) {
			return True;
		}
	}
	/* ロック待ちの TA_CEILING ミューテックスについても同じ（仕様）。
	 * 待ち行列を辿るより、待ち要因とオブジェクト ID から引く方が速い。 */
	if ((t->state & TTS_WAI) != 0 && t->waitcause == TTW_MTX) {
		i = t->wid;
		if (i >= 1 && i <= TK_MAX_MTXID && mtxcb[i].used &&
		    proto(&mtxcb[i]) == TA_CEILING && newpri < mtxcb[i].ceilpri) {
			return True;
		}
	}
	return False;
}

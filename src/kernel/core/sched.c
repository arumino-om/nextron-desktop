/*
 * sched.c — スケジューラとディスパッチャ
 *
 * 優先度ベースのプリエンプティブ方式。同一優先度内は FCFS。
 * **時分割はしない**（仕様: 優先順位の高いタスクが実行できる状態にある限り、
 * それより低いタスクは全く実行されない）。ラウンドロビンは tk_rot_rdq。
 *
 * 【仕様の細則】同一優先度内の優先順位について、仕様はこう規定している。
 *
 *   実行状態 → 実行可能状態（プリエンプト）  同一優先度で**最高**
 *   実行状態 → 待ち状態 → 待ち解除            同一優先度で**最低**
 *   強制待ち状態 → 実行できる状態             同一優先度で**最低**
 *
 * つまり**プリエンプトは待ち行列の先頭に戻し、待ち解除は末尾に付ける**。
 * ここを取り違えると、優先度が同じタスクの実行順が仕様と食い違う。
 * knl_make_ready() の head 引数がこの区別である。
 */

#include "knl.h"

EXPORT TCB *knl_cur;
EXPORT TCB knl_idle;
EXPORT Bool knl_on = False;
EXPORT W knl_dsp_disabled;

/* 優先度ごとのレディキュー。添字 1..TK_MAX_TSKPRI。 */
LOCAL Queue ready[TK_MAX_TSKPRI + 1];

/* どのキューに在るかのビットマップ。bit (pri-1)。
 *
 * 優先度が 130 段（周辺核のプロセス優先度を 1 対 1 で受ける。kernel.h）
 * あるので UW 1 枚では足りない。語に分けて持ち、最高優先度の探索は
 * 下位の語から順に見る。語の数は 5 なので最悪でも 5 回。 */
#define READY_WORDS ((TK_MAX_TSKPRI + 31) / 32)
LOCAL UW ready_map[READY_WORDS];

/* ------------------------------------------------------------------ */
/* 待ち行列                                                            */
/* ------------------------------------------------------------------ */

EXPORT void knl_queue_init(Queue *q, ATR order)
{
	q->head = 0;
	q->tail = 0;
	q->order = order;
}

EXPORT Bool knl_queue_empty(CONST Queue *q)
{
	return (q->head == 0) ? True : False;
}

EXPORT void knl_queue_insert_tail(Queue *q, TCB *t)
{
	t->queue = q;
	t->next = 0;
	t->prev = q->tail;
	if (q->tail != 0) {
		q->tail->next = t;
	} else {
		q->head = t;
	}
	q->tail = t;
}

LOCAL void queue_insert_head(Queue *q, TCB *t)
{
	t->queue = q;
	t->prev = 0;
	t->next = q->head;
	if (q->head != 0) {
		q->head->prev = t;
	} else {
		q->tail = t;
	}
	q->head = t;
}

/* 行列の並び方（TA_TFIFO / TA_TPRI）に従って挿入する。
 * TA_TPRI でも**同じ優先度の中では FIFO**（後から来た方が後ろ）。 */
EXPORT void knl_queue_insert(Queue *q, TCB *t)
{
	TCB *p;

	if ((q->order & TA_TPRI) == 0) {
		knl_queue_insert_tail(q, t);
		return;
	}
	for (p = q->head; p != 0; p = p->next) {
		if (p->pri > t->pri) {
			break;
		}
	}
	if (p == 0) {
		knl_queue_insert_tail(q, t);
		return;
	}
	t->queue = q;
	t->prev = p->prev;
	t->next = p;
	if (p->prev != 0) {
		p->prev->next = t;
	} else {
		q->head = t;
	}
	p->prev = t;
}

EXPORT void knl_queue_remove(TCB *t)
{
	Queue *q = t->queue;

	if (q == 0) {
		return;
	}
	if (t->prev != 0) {
		t->prev->next = t->next;
	} else {
		q->head = t->next;
	}
	if (t->next != 0) {
		t->next->prev = t->prev;
	} else {
		q->tail = t->prev;
	}
	t->next = 0;
	t->prev = 0;
	t->queue = 0;
}

/* ------------------------------------------------------------------ */
/* レディキュー                                                        */
/* ------------------------------------------------------------------ */

EXPORT void knl_sched_init(void)
{
	W i;

	for (i = 0; i <= TK_MAX_TSKPRI; i++) {
		knl_queue_init(&ready[i], TA_TFIFO);
	}
	for (i = 0; i < READY_WORDS; i++) {
		ready_map[i] = 0;
	}
	knl_dsp_disabled = 0;
}

EXPORT void knl_make_ready(TCB *t, Bool head)
{
	t->state = TTS_RDY;
	if (t == &knl_idle) {
		return; /* アイドルはキューに入れない。常に実行可能扱い */
	}
	if (head) {
		queue_insert_head(&ready[t->pri], t);
	} else {
		knl_queue_insert_tail(&ready[t->pri], t);
	}
	ready_map[(t->pri - 1) / 32] |= 1U << (UW)((t->pri - 1) % 32);
}

EXPORT void knl_unready(TCB *t)
{
	PRI pri = t->pri;

	/* アイドルの優先度は TK_MAX_TSKPRI + 1（番兵）で、レディキューの
	 * 添字の範囲外。&ready[pri] を作る前に外しておく。 */
	if (t == &knl_idle) {
		return;
	}
	if (t->queue != &ready[pri]) {
		return;
	}
	knl_queue_remove(t);
	if (knl_queue_empty(&ready[pri])) {
		ready_map[(pri - 1) / 32] &= ~(1U << (UW)((pri - 1) % 32));
	}
}

EXPORT void knl_set_pri(TCB *t, PRI newpri)
{
	if (t->pri == newpri) {
		return;
	}
	if (t->state == TTS_RDY) {
		/* レディキューを移す。新しい優先度の**末尾**に付く。
		 * 仕様はミューテックスによる優先度変更時の同一優先度内の順序を
		 * 実装依存としているので、tk_chg_pri と同じ扱いで統一する。 */
		knl_unready(t);
		t->pri = newpri;
		knl_make_ready(t, False);
		return;
	}
	if ((t->state & TTS_WAI) != 0 && t->queue != 0 &&
	    (t->queue->order & TA_TPRI) != 0) {
		/* 優先度順の待ち行列なら並び直す */
		Queue *q = t->queue;

		knl_queue_remove(t);
		t->pri = newpri;
		knl_queue_insert(q, t);
		return;
	}
	t->pri = newpri;
}

EXPORT TCB *knl_highest(void)
{
	W i;

	for (i = 0; i < READY_WORDS; i++) {
		UW m = ready_map[i];

		if (m != 0) {
			/* 立っている最下位ビット = 最も小さい優先度番号 = 最高優先度 */
			return ready[i * 32 + __builtin_ctz(m) + 1].head;
		}
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* ディスパッチャ                                                      */
/* ------------------------------------------------------------------ */

EXPORT void knl_dispatch(void)
{
	TCB *prev = knl_cur;
	TCB *next;

	/* 中心核が起動する前は切り替えない。knl_start() より前に
	 * tk_sta_tsk() されたタスクは、レディキューに積まれるだけになる。 */
	if (!knl_on || knl_dsp_disabled > 0) {
		return;
	}
	/* 終了して ID を預けたタスクの TCB とスタックを返す。
	 * ここで呼ぶのは、**いま走っているのが死んだタスクではない**ことが
	 * 保証される唯一の場所だから（死んだタスクはもう選ばれない）。 */
	knl_reclaim();
	next = knl_highest();
	if (next == 0) {
		next = &knl_idle;
	}
	if (next == prev) {
		/* 自分がそのまま選ばれた。tk_rot_rdq のようにレディキューへ
		 * 入れてから呼ばれる場合があるので、入っていたら外して戻す。 */
		knl_unready(prev);
		prev->state = TTS_RUN;
		return;
	}

	if (prev->state == TTS_RUN) {
		/* **同一優先度のタスクには奪わせない。**
		 * 仕様は「異なる優先度では高い方が、同じ優先度では先に実行できる
		 * 状態になった方が高い優先順位」と規定している。走っている方が
		 * 先になっているので、同順位の相手に譲る理由がない。
		 * ここを `!=` で書くと、同優先度のタスクを起動した瞬間に
		 * 切り替わってしまい FCFS が壊れる。 */
		if (next->pri >= prev->pri) {
			return;
		}
		if (prev == &knl_idle) {
			prev->state = TTS_RDY; /* アイドルはキューに入れない */
		} else {
			/* プリエンプトは同一優先度の**先頭**に戻る（仕様） */
			knl_make_ready(prev, True);
		}
	}
	/* prev が TTS_RUN でない = 待ち・休止・自ら譲った。無条件に切り替える。 */

	knl_unready(next);
	next->state = TTS_RUN;
	knl_cur = next;
	/* 周辺核にアドレス空間を切り替えさせる。
	 *
	 * BTRON3 §1.1.1「1 つのプロセスは 1 つの独立したアドレス空間を持つ」。
	 * 中心核はプロセスという概念を持たないので、タスク ID だけ渡して
	 * 判断は周辺核に任せる。カーネルは上位半分で走っているので、
	 * 下位半分 (TTBR0 / CR3) を差し替えても実行中のコードは壊れない。 */
	prc_task_switch(next->tskid);
	/* **切り替える直前に、降りるタスクのスタックを検める。**
	 * ここが溢れの一番近い検査点になる。 */
	knl_check_stack(prev);
	hal_ctx_switch(&prev->sp, next->sp);
	/* 戻ってきた = このタスクが再び選ばれた。割込みは禁止のまま。 */
}

/* 割込みハンドラの最後に呼ばれる (intc.c)。 */
EXPORT void knl_dispatch_from_irq(void)
{
	if (!knl_on) {
		return;
	}
	knl_dispatch();
}

EXPORT Bool knl_running(void)
{
	return knl_on;
}

/* ------------------------------------------------------------------ */
/* 待ちと解除                                                          */
/* ------------------------------------------------------------------ */

EXPORT ER knl_wait(Queue *q, UW cause, ID wid, TMO tmout)
{
	TCB *me = knl_cur;

	me->state = TTS_WAI;
	me->waitcause = cause;
	me->wid = wid;
	me->waitresult = E_OK;
	me->timeout = (tmout == TMO_FEVR) ? 0 : knl_otm + (SYSTIM)tmout;
	if (q != 0) {
		knl_queue_insert(q, me);
	}
	knl_unready(me); /* 念のため。RUN なら入っていない */
	knl_dispatch();
	return me->waitresult;
}

EXPORT void knl_wait_release(TCB *t, ER result)
{
	knl_queue_remove(t);
	t->waitresult = result;
	t->timeout = 0;
	t->waitcause = 0;
	t->wid = 0;
	t->wait_flgptn = 0;

	if ((t->state & TTS_SUS) != 0) {
		/* 二重待ち → 強制待ち。実行できる状態には戻さない。 */
		t->state = TTS_SUS;
		return;
	}
	/* 待ち解除は同一優先度で**最低**の優先順位（仕様） */
	knl_make_ready(t, False);
}

/* タイムアウトしたときに返す値。**待ち要因によってエラーコードの族が違う。**
 *
 * ランデブは BTRON3 §1.3 の API なので周辺核の族 (`ER_*`) を返さなければ
 * ならない。ここを一律 `E_TMOUT` にすると、`cal_por()` の呼び出し側が
 * 受け取る族が食い違う（規律 R20）。 */
LOCAL ER timeout_result(UW cause)
{
	switch (cause) {
	case TTW_DLY:
		return E_OK; /* tk_dly_tsk は時間が来たら正常終了 */
	case TTW_CAL:
	case TTW_ACP:
		return ER_NONE; /* BTRON3 §1.3: ランデブが成立せずにタイムアウト */
	default:
		return E_TMOUT; /* μT-Kernel の待ち */
	}
}

EXPORT void knl_timeout_check(void)
{
	W i;

	for (i = 1; i <= TK_MAX_TSKID; i++) {
		TCB *t = &knl_tcb[i];

		if (!t->used || (t->state & TTS_WAI) == 0 || t->timeout == 0) {
			continue;
		}
		if (knl_otm < t->timeout) {
			continue;
		}
		knl_wait_release(t, timeout_result(t->waitcause));
	}
}

EXPORT TCB *knl_find_match(Queue *q, UW ptn)
{
	TCB *t;

	for (t = q->head; t != 0; t = t->next) {
		if ((t->rdv_ptn & ptn) != 0) {
			return t;
		}
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* tk_rot_rdq / ディスパッチ禁止                                       */
/* ------------------------------------------------------------------ */

EXPORT ER tk_rot_rdq(PRI tskpri)
{
	UD state;
	PRI pri;

	if (tskpri < 0 || tskpri > TK_MAX_TSKPRI) {
		return E_PAR;
	}
	state = hal_int_disable();
	pri = (tskpri == 0) ? knl_cur->pri : tskpri;

	if (pri < 1 || pri > TK_MAX_TSKPRI) {
		hal_int_restore(state);
		return E_PAR;
	}
	if (knl_cur != &knl_idle && pri == knl_cur->pri) {
		/* 自分の優先度を回す = 同一優先度の次のタスクに譲る。
		 * 自タスクは末尾に付く（プリエンプトではないので先頭には戻さない）。 */
		knl_make_ready(knl_cur, False);
		knl_dispatch();
	} else if (!knl_queue_empty(&ready[pri])) {
		TCB *t = ready[pri].head;

		knl_queue_remove(t);
		knl_queue_insert_tail(&ready[pri], t);
		knl_dispatch();
	}
	hal_int_restore(state);
	return E_OK;
}

/* タイムスライスを使い切ったので譲る。タイマ割込みの中から呼ぶ。
 *
 * **ここでは切り替えない。** 自タスクを同一優先度の待ち行列の**末尾**に
 * 戻すだけで、実際の切替は割込み処理の最後の knl_dispatch_from_irq() が
 * 行う。割込みハンドラの途中でコンテキストを切り替えると、ハンドラの
 * 残りが別のタスクのスタックで走ってしまう。
 *
 * 末尾なのはプリエンプトではないから。自分で時間を使い切って譲るので、
 * 同一優先度の中では最後に回る（§1.1.3 の「同一優先度の場合は
 * ラウンドロビン方式で平等にスケジューリング」がこれで成立する）。 */
EXPORT void knl_yield_slice(void)
{
	TCB *me = knl_cur;

	if (me == &knl_idle || me->state != TTS_RUN || knl_dsp_disabled > 0) {
		return;
	}
	if (knl_queue_empty(&ready[me->pri])) {
		return; /* 同一優先度に誰もいない。譲る相手がいないので何もしない */
	}
	knl_make_ready(me, False);
}

EXPORT ER tk_dis_dsp(void)
{
	UD state = hal_int_disable();

	knl_dsp_disabled++;
	hal_int_restore(state);
	return E_OK;
}

EXPORT ER tk_ena_dsp(void)
{
	UD state = hal_int_disable();

	if (knl_dsp_disabled > 0) {
		knl_dsp_disabled--;
	}
	if (knl_dsp_disabled == 0) {
		knl_dispatch();
	}
	hal_int_restore(state);
	return E_OK;
}

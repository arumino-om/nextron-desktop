/*
 * event.c — 周辺核 §1.7「イベント管理」
 *
 * ------------------------------------------------------------------------
 * キューは 1 本、しかし取り出し順は発生順ではない
 * ------------------------------------------------------------------------
 *
 * 仕様 §1.7.2 はイベントに**優先度**を与えている。
 *
 *   1 (最高)  EV_APPL1〜4
 *   2         EV_BUTDWN / EV_BUTUP / EV_KEYDWN / EV_KEYUP
 *   3         EV_AUTKEY
 *   4         EV_DEVICE / EV_RSV
 *   5         EV_APPL5〜8
 *   6 (最低)  EV_NULL
 *
 * 「高い優先度のイベントから取り出される。同一優先度の場合は、発生順に
 * 取り出される」。つまり**格納は FIFO だが取り出しは優先度順**で、
 * §1.2 のメッセージキュー（純粋な FIFO）とは違う。
 *
 * アプリケーションイベントが 2 つの優先度に分かれている（1〜4 が最高、
 * 5〜8 が低い）のが特徴的で、外殻が緊急の通知に 1〜4 を使えるようになって
 * いる。
 *
 * ------------------------------------------------------------------------
 * キューに入らない 2 つのイベント
 * ------------------------------------------------------------------------
 *
 * 仕様「ヌルイベント(EV_NULL)、および自動リピートキーイベント(EV_AUTKEY)
 * は、実際にはイベントキューには入れられず、イベントの取り出し要求時に
 * 自動的に生成される」。
 *
 *   EV_NULL    要求されたタイプが発生していないときに生成される
 *   EV_AUTKEY  5 つの条件（下記 autkey_ready）がすべて揃ったときに生成される
 *
 * 自動リピートを**キューに積まない**のが要点である。積むと、取り出しが
 * 遅れた分だけリピートが溜まって一気に出る。生成式なら「いま押されて
 * いるか」だけで決まる。
 *
 * ------------------------------------------------------------------------
 * 入力受付状態はここでは保証しない
 * ------------------------------------------------------------------------
 *
 * 仕様「ある時点で入力受付状態となっているプロセスのみがイベント管理機能を
 * 使用してイベントを取り出すというルールを前提としており…イベント管理機能
 * 自体では、このルールを保証する機構は特に提供しておらず、外殻レベルで
 * 提供することになる」。
 *
 * だから `get_evt` は誰からでも通る。入力受付状態の管理はウィンドウ
 * マネージャの仕事で、アプリは `wget_evt` を呼ぶ。
 */

#include <btron/event.h>
#include <btron/message.h>
#include <btron/proc.h>
#include <btron/screen.h>

#include "../core/knl.h"

/* ------------------------------------------------------------------ */
/* 状態                                                                */
/* ------------------------------------------------------------------ */

typedef struct EvEnt {
	EVENT ev;
	UW seq;  /* 発生順。同一優先度内の順序に使う */
	W from;  /* put_evt を呼んだプロセス。0 なら初期プロセス扱い */
	Bool used;
} EvEnt;

LOCAL EvEnt queue[EV_QUEUE_MAX];
LOCAL UW next_seq;

/* システムイベントマスク。**起動時は 0**（仕様）。 */
LOCAL W sys_mask;

LOCAL PNT pd_pos;    /* PD の現在位置 */
LOCAL W pd_attr;     /* PD の属性 */
LOCAL UW cur_stat;   /* 現在のメタキー / ボタン状態 */
LOCAL KeyMap key_state; /* キー状態 */
LOCAL KeyMap rep_mask;  /* 自動リピートの対象キー */
LOCAL W rep_offset;     /* 最初のリピートまでの時間 (ms) */
LOCAL W rep_interval;   /* その後の間隔 (ms) */
LOCAL KBD_ID kbd_id;
LOCAL KeyTab keytab;
LOCAL Bool keytab_set;

/* 最後の EV_KEYDWN。自動リピートの生成に使う。 */
LOCAL UH last_keytop;
LOCAL TC last_keycode;
LOCAL UW last_keydwn_time; /* 0 なら押されていない */
LOCAL UW last_autkey_time;

/* タイプごとの最終発生時刻。las_evt に使う。 */
LOCAL UW last_time[EV_MAX + 1];

/* ブザーの設定値。音源が無いので保持するだけ。 */
LOCAL UW buz_val[0x12];

/* イベントメッセージ要求 (§1.7.6 req_evt) */
typedef struct EvReq {
	W pid;
	W mask;
} EvReq;

LOCAL EvReq req[EV_REQ_MAX];

/* ------------------------------------------------------------------ */
/* 補助                                                                */
/* ------------------------------------------------------------------ */

/* イベントタイマー。仕様「ミリ秒単位で加算される 32 ビットのタイマー…
 * 32 ビットをオーバーフローした場合は単に上位ビットが捨てられる」。 */
LOCAL UW etimer(void)
{
	return (UW)(knl_otm & 0xFFFFFFFFLL);
}

/* 仕様 §1.7.2 の優先度。1 が最高、6 が最低。 */
LOCAL W ev_prio(W type)
{
	switch (type) {
	case EV_APPL1:
	case EV_APPL2:
	case EV_APPL3:
	case EV_APPL4:
		return 1;
	case EV_BUTDWN:
	case EV_BUTUP:
	case EV_KEYDWN:
	case EV_KEYUP:
		return 2;
	case EV_AUTKEY:
		return 3;
	case EV_DEVICE:
	case EV_RSV:
		return 4;
	case EV_APPL5:
	case EV_APPL6:
	case EV_APPL7:
	case EV_APPL8:
		return 5;
	default: /* EV_NULL */
		return 6;
	}
}

LOCAL Bool key_bit(CONST UB *map, UW k)
{
	return ((map[k >> 3] >> (k & 7U)) & 1U) != 0 ? True : False;
}

LOCAL void key_set(UB *map, UW k, Bool on)
{
	if (on) {
		map[k >> 3] |= (UB)(1U << (k & 7U));
	} else {
		map[k >> 3] &= (UB)~(1U << (k & 7U));
	}
}

/* 要求されたマスクに合う中で**最も優先度が高く、同優先度では最も古い**もの。 */
LOCAL EvEnt *pick(W t_mask)
{
	EvEnt *best = 0;
	W bestp = 7;
	W i;

	for (i = 0; i < EV_QUEUE_MAX; i++) {
		EvEnt *e = &queue[i];
		W p;

		if (!e->used || (EVMASK(e->ev.type) & t_mask) == 0) {
			continue;
		}
		p = ev_prio(e->ev.type);
		if (best == 0 || p < bestp ||
		    (p == bestp && e->seq < best->seq)) {
			best = e;
			bestp = p;
		}
	}
	return best;
}

/* ------------------------------------------------------------------ */
/* 自動リピート                                                        */
/* ------------------------------------------------------------------ */

/* 仕様が挙げている 5 つの条件がすべて揃ったか。 */
LOCAL Bool autkey_ready(W t_mask)
{
	UW now = etimer();
	UW since;

	/* ① EV_AUTKEY は要求されたタイプの 1 つである */
	if ((t_mask & EM_AUTKEY) == 0) {
		return False;
	}
	/* ② システムイベントマスクのビットが "1" である */
	if ((sys_mask & EM_AUTKEY) == 0) {
		return False;
	}
	/* ③ EV_AUTKEY より優先度の高いイベントが発生していない */
	if (pick(EM_ALL) != 0 && ev_prio(pick(EM_ALL)->ev.type) < 3) {
		return False;
	}
	/* ④ 最新の EV_KEYDWN の発生キーが押され続けられている */
	if (last_keydwn_time == 0 || !key_bit(key_state, last_keytop)) {
		return False;
	}
	/* ⑤ 自動リピートの対象キーである */
	if (!key_bit(rep_mask, last_keytop)) {
		return False;
	}
	/* ⑥ オフセット時間、またはインターバル時間が経過した */
	if (last_autkey_time == 0) {
		since = now - last_keydwn_time;
		return (since >= (UW)rep_offset) ? True : False;
	}
	since = now - last_autkey_time;
	return (since >= (UW)rep_interval) ? True : False;
}

LOCAL void make_autkey(EVENT *evt)
{
	evt->type = EV_AUTKEY;
	evt->time = etimer();
	evt->pos = pd_pos;
	evt->data.key.keytop = last_keytop;
	evt->data.key.code = last_keycode;
	evt->stat = cur_stat;
	last_autkey_time = evt->time;
	last_time[EV_AUTKEY] = evt->time;
}

LOCAL void make_null(EVENT *evt)
{
	evt->type = EV_NULL;
	evt->time = etimer();
	evt->pos = pd_pos;
	evt->data.info = 0; /* 仕様「info は常に 0」 */
	evt->stat = cur_stat;
}

/* ------------------------------------------------------------------ */
/* イベントメッセージ要求 (§1.7.6 req_evt)                             */
/* ------------------------------------------------------------------ */

/* イベントを MS_SYS5 のメッセージとして要求元へ送る。
 *
 * 仕様「メッセージとして送信されても、そのイベントはイベントキューから
 * 取り除かれない」。また「プロセスのメッセージキューが一杯で送信できな
 * かった場合には、そのイベントメッセージは単に捨てられる」。 */
LOCAL void notify_req(CONST EVENT *ev, W from)
{
	W i;

	for (i = 0; i < EV_REQ_MAX; i++) {
		MESSAGE m;
		UB *body;
		CONST UB *src;
		W k;

		if (req[i].pid < 1 || (req[i].mask & EVMASK(ev->type)) == 0) {
			continue;
		}
		/* 仕様: EV_NULL / EV_AUTKEY はメッセージにできない */
		if (ev->type == EV_NULL || ev->type == EV_AUTKEY) {
			continue;
		}
		m.msg_type = MS_SYS5;
		m.msg_size = (W)sizeof(EVENT);
		body = (UB *)(VP)&m.msg_body;
		src = (CONST UB *)(CONST VP)ev;
		for (k = 0; k < (W)sizeof(EVENT); k++) {
			body[k] = src[k];
		}
		/* 送信元 PID は「put_evt() を呼び出したプロセスの ID」、
		 * それ以外は初期プロセス (ID = 1)（仕様）。
		 * snd_msg は自プロセスを送信元にするので、ここでは要求元へ
		 * 直接積む形にできない — 実際の送信元が誰であれ、要求元の
		 * キューに入ることが要点なので snd_msg を使う。 */
		(void)from;
		(void)snd_msg(req[i].pid, &m, NOWAIT);
	}
}

/* ------------------------------------------------------------------ */
/* システムコール                                                      */
/* ------------------------------------------------------------------ */

/* キューの末尾に 1 つ積む。割込み禁止の中で呼ぶこと。
 *
 * 空きが無ければ 0 を返す。**捨てられるのは新しい方**（仕様 §1.7.2
 * 「新規に発生したイベント、即ち、一番新しいイベントはキューに
 * 入れられず、捨てられる」）。だから既存の要素を追い出すことはしない。
 *
 * キー状態・最後のキー押し・ボタン状態の面倒もここで見る。put_evt と
 * ドライバ経路の両方が通るので、自動リピートの条件が片方だけ整う
 * という食い違いが起きない。 */
LOCAL EvEnt *enqueue(CONST EVENT *ev, W from)
{
	EvEnt *slot = 0;
	W i;

	for (i = 0; i < EV_QUEUE_MAX; i++) {
		if (!queue[i].used) {
			slot = &queue[i];
			break;
		}
	}
	if (slot == 0) {
		return 0;
	}
	/* 仕様「常にイベントキューの最後に入れられる」 */
	slot->ev = *ev;
	slot->seq = next_seq++;
	slot->from = from;
	slot->used = True;
	last_time[ev->type] = etimer();

	if (ev->type == EV_KEYDWN) {
		key_set(key_state, ev->data.key.keytop, True);
		last_keytop = ev->data.key.keytop;
		last_keycode = ev->data.key.code;
		last_keydwn_time = ev->time;
		last_autkey_time = 0;
	} else if (ev->type == EV_KEYUP) {
		key_set(key_state, ev->data.key.keytop, False);
		if (ev->data.key.keytop == last_keytop) {
			last_keydwn_time = 0;
		}
	} else if (ev->type == EV_BUTDWN) {
		cur_stat |= ES_BUT;
	} else if (ev->type == EV_BUTUP) {
		cur_stat &= ~(UW)ES_BUT;
	}
	return slot;
}

EXPORT WERR get_evt(W t_mask, EVENT *evt, W opt)
{
	UD state;
	EvEnt *e;

	if (evt == 0) {
		return ER_ADR;
	}
	if (t_mask <= 0) {
		return ER_PAR;
	}
	if (opt != CLR && opt != NOCLR) {
		return ER_PAR;
	}
	state = hal_int_disable();
	e = pick(t_mask);
	if (e != 0 && ev_prio(e->ev.type) <= 2) {
		/* 優先度 2 以上（アプリ 1〜4・ボタン・キー）は自動リピートより
		 * 先に出す。仕様の条件③がこれを要求している。 */
		*evt = e->ev;
		if (opt == CLR) {
			e->used = False;
		}
		hal_int_restore(state);
		return evt->type;
	}
	if (autkey_ready(t_mask)) {
		make_autkey(evt);
		hal_int_restore(state);
		return EV_AUTKEY;
	}
	if (e != 0) {
		*evt = e->ev;
		if (opt == CLR) {
			e->used = False;
		}
		hal_int_restore(state);
		return evt->type;
	}
	/* 仕様「指定したタイプのイベントが発生していないときは、EV_NULL が
	 * 取り出される」。エラーではない。 */
	make_null(evt);
	hal_int_restore(state);
	return EV_NULL;
}

EXPORT ERR put_evt(CONST EVENT *evt, W opt)
{
	UD state;
	EvEnt *slot = 0;
	EVENT ev;
	W me = prc_self();

	if (evt == 0) {
		return ER_ADR;
	}
	if ((opt & ~(W)EP_ALL) != 0) {
		return ER_PAR;
	}
	/* 仕様「EV_NULL および EV_AUTKEY の指定したときはエラー」 */
	if (evt->type < EV_MIN || evt->type > EV_MAX || evt->type == EV_NULL ||
	    evt->type == EV_AUTKEY) {
		return ER_PAR;
	}
	ev = *evt;

	state = hal_int_disable();
	if ((opt & EP_POS) != 0) {
		ev.pos = pd_pos;
	}
	if ((opt & EP_STAT) != 0) {
		ev.stat = cur_stat;
	}
	if ((opt & EP_TIME) != 0) {
		ev.time = etimer();
	}
	/* 仕様「システムイベントマスクで対象外のイベントは実際には発生せず
	 * 無視される」。**エラーではない。** */
	if ((sys_mask & EVMASK(ev.type)) == 0) {
		hal_int_restore(state);
		return ER_OK;
	}
	slot = enqueue(&ev, me);
	if (slot == 0) {
		/* 仕様「イベントキューに空きがない場合…エラーとなる」。
		 * （ドライバ由来のイベントは「一番新しいイベントが捨てられる」が、
		 * put_evt はエラーを返す形で区別されている。） */
		hal_int_restore(state);
		return ER_NOSPC;
	}
	hal_int_restore(state);

	notify_req(&ev, me);
	return ER_OK;
}

/* 宛先のイベントを発生順に並べる。clr_evt が使う。 */
LOCAL W collect(EvEnt **q, W max)
{
	W n = 0;
	UW base = 0;
	Bool first = True;

	for (;;) {
		EvEnt *best = 0;
		W i;

		for (i = 0; i < EV_QUEUE_MAX; i++) {
			if (!queue[i].used) {
				continue;
			}
			if (!first && queue[i].seq <= base) {
				continue;
			}
			if (best == 0 || queue[i].seq < best->seq) {
				best = &queue[i];
			}
		}
		if (best == 0 || n >= max) {
			return n;
		}
		q[n++] = best;
		base = best->seq;
		first = False;
	}
}

EXPORT ERR clr_evt(W t_mask, W last_mask)
{
	UD state;
	EvEnt *q[EV_QUEUE_MAX];
	W n;
	W i;

	if (t_mask <= 0 || last_mask < 0) {
		return ER_PAR;
	}
	state = hal_int_disable();
	n = collect(q, EV_QUEUE_MAX);
	if (last_mask == EM_ALL) {
		/* §1.2 の clr_msg と同じ特例。MM_ALL / EM_ALL は全タイプを
		 * 含むので、特例にしないと先頭で止まって何も消えない。 */
		for (i = 0; i < n; i++) {
			if ((EVMASK(q[i]->ev.type) & t_mask) != 0) {
				q[i]->used = False;
				break;
			}
		}
	} else {
		for (i = 0; i < n; i++) {
			if ((EVMASK(q[i]->ev.type) & last_mask) != 0) {
				break;
			}
			if ((EVMASK(q[i]->ev.type) & t_mask) != 0) {
				q[i]->used = False;
			}
		}
	}
	hal_int_restore(state);
	return ER_OK;
}

EXPORT WERR get_pdp(PNT *pos)
{
	if (pos == 0) {
		return ER_ADR;
	}
	*pos = pd_pos;
	return ER_OK;
}

EXPORT WERR set_pdp(PNT pos)
{
	/* 仕様「相対動作タイプの場合は…位置を任意に設定することが可能である
	 * が、絶対動作タイプの場合は、位置の設定は不可」 */
	if ((pd_attr & PD_ABS) != 0) {
		return ER_OBJ;
	}
	pd_pos = pos;
	return ER_OK;
}

EXPORT ERR get_etm(UW *time)
{
	if (time == 0) {
		return ER_ADR;
	}
	*time = etimer();
	return ER_OK;
}

EXPORT ERR get_kmp(KeyMap keymap)
{
	W i;

	if (keymap == 0) {
		return ER_ADR;
	}
	for (i = 0; i < KEYMAX / 8; i++) {
		keymap[i] = key_state[i];
	}
	return ER_OK;
}

EXPORT WERR chg_emk(W mask)
{
	UD state;
	W old;

	if (mask < 0 || mask > EM_ALL) {
		return ER_PAR;
	}
	state = hal_int_disable();
	old = sys_mask;
	sys_mask = mask;
	hal_int_restore(state);
	return old;
}

EXPORT ERR set_krp(W offset, W interval)
{
	if (offset < 0 || interval < 0) {
		return ER_PAR;
	}
	rep_offset = offset;
	rep_interval = interval;
	return ER_OK;
}

EXPORT ERR get_krp(W *offset, W *interval)
{
	if (offset == 0 || interval == 0) {
		return ER_ADR;
	}
	*offset = rep_offset;
	*interval = rep_interval;
	return ER_OK;
}

EXPORT ERR set_krm(CONST UB *keymap)
{
	W i;

	if (keymap == 0) {
		return ER_ADR;
	}
	for (i = 0; i < KEYMAX / 8; i++) {
		rep_mask[i] = keymap[i];
	}
	return ER_OK;
}

EXPORT ERR get_krm(KeyMap keymap)
{
	W i;

	if (keymap == 0) {
		return ER_ADR;
	}
	for (i = 0; i < KEYMAX / 8; i++) {
		keymap[i] = rep_mask[i];
	}
	return ER_OK;
}

EXPORT ERR get_kid(KBD_ID *id)
{
	if (id == 0) {
		return ER_ADR;
	}
	*id = kbd_id;
	return ER_OK;
}

EXPORT WERR get_ktb(KeyTab *keytab_out)
{
	W need = (W)sizeof(KeyTab);

	if (keytab_out == 0) {
		return ER_ADR;
	}
	if (!keytab_set) {
		return ER_NOEXS; /* 変換表が設定されていない */
	}
	*keytab_out = keytab;
	return need;
}

EXPORT ERR set_ktb(CONST KeyTab *tab)
{
	if (tab == 0) {
		return ER_ADR;
	}
	if (tab->keymax < 1 || tab->keymax > KEYMAX || tab->kctmax < 1 ||
	    tab->kctmax > KCTSEL) {
		return ER_PAR;
	}
	/* 仕様「keymax × kctmax の最大許容値はインプリメントに依存するが、
	 * 少なくとも 2048 が保証される」。本OS は KCTMAX = 4000。 */
	if (tab->keymax * tab->kctmax > KCTMAX) {
		return ER_PAR;
	}
	keytab = *tab;
	keytab_set = True;
	return ER_OK;
}

EXPORT WERR chg_pda(W atr)
{
	W old = pd_attr;

	pd_attr = atr;
	return old;
}

EXPORT ERR sig_buz(W type)
{
	/* 音源が無い構成では鳴らない。鳴らないことをエラーにはしない —
	 * 警告音が出ないだけで処理は続けられるべきである。 */
	(void)type;
	return ER_OK;
}

EXPORT ERR ctl_buz(W kind, UW *val)
{
	W idx;

	if (val == 0) {
		return ER_ADR;
	}
	idx = kind & 0xFF;
	if (idx > 0x11) {
		return ER_PAR;
	}
	if ((kind & 0x0100) != 0) {
		buz_val[idx] = *val; /* 設定 */
	} else {
		*val = buz_val[idx]; /* 取り出し */
	}
	return ER_OK;
}

EXPORT ERR req_evt(W t_mask)
{
	UD state;
	W me = prc_self();
	W free_slot = -1;
	W i;

	if (me < 1) {
		return ER_CTX;
	}
	if (t_mask < 0 || t_mask > EM_ALL) {
		return ER_PAR;
	}
	state = hal_int_disable();
	for (i = 0; i < EV_REQ_MAX; i++) {
		if (req[i].pid == me) {
			/* 仕様「t_mask に EM_NULL を指定することにより、イベント
			 * メッセージの要求が解除される」 */
			if (t_mask == EM_NULL) {
				req[i].pid = 0;
				req[i].mask = 0;
			} else {
				req[i].mask = t_mask;
			}
			hal_int_restore(state);
			return ER_OK;
		}
		if (req[i].pid < 1 && free_slot < 0) {
			free_slot = i;
		}
	}
	if (t_mask == EM_NULL) {
		hal_int_restore(state);
		return ER_OK; /* 要求していないものを解除するのは正常 */
	}
	if (free_slot < 0) {
		hal_int_restore(state);
		return ER_LIMIT;
	}
	req[free_slot].pid = me;
	req[free_slot].mask = t_mask;
	hal_int_restore(state);
	return ER_OK;
}

EXPORT WERR las_evt(W t_mask)
{
	UW now = etimer();
	UW newest = 0;
	Bool any = False;
	W i;

	if (t_mask <= 0) {
		return ER_PAR;
	}
	for (i = EV_MIN + 1; i <= EV_MAX; i++) {
		if ((EVMASK(i) & t_mask) == 0 || last_time[i] == 0) {
			continue;
		}
		if (!any || last_time[i] > newest) {
			newest = last_time[i];
			any = True;
		}
	}
	if (!any) {
		return (W)now; /* 一度も起きていない。起動からの時間を返す */
	}
	return (W)(now - newest);
}

/* ------------------------------------------------------------------ */
/* 内部の口                                                            */
/* ------------------------------------------------------------------ */

EXPORT void evt_release_process(W pid)
{
	UD state;
	W i;

	if (pid < 1) {
		return;
	}
	/* 仕様「プロセスが終了したときにも自動的に解除される」 */
	state = hal_int_disable();
	for (i = 0; i < EV_REQ_MAX; i++) {
		if (req[i].pid == pid) {
			req[i].pid = 0;
			req[i].mask = 0;
		}
	}
	hal_int_restore(state);
}

EXPORT W evt_count(void)
{
	W n = 0;
	W i;

	for (i = 0; i < EV_QUEUE_MAX; i++) {
		if (queue[i].used) {
			n++;
		}
	}
	return n;
}

/* ------------------------------------------------------------------ */
/* ドライバからの口。BTRON3 の API ではない                            */
/*                                                                     */
/* 【役割の分け方】キーボードの**配列**を知っているのはドライバだけで、  */
/* 「どのキートップがシフトか」はドライバが決める。一方、文字コードへの  */
/* エンコード・キュー・自動リピート・ボタン状態は仕様が定めた周辺核の    */
/* 仕事なので、ここに置く。だからドライバは                             */
/*                                                                     */
/*   evt_meta()  … メタキーの状態が変わった（文字にはならない）         */
/*   evt_key()   … 文字になるキーが上下した（キートップコードで渡す）    */
/*                                                                     */
/* の 2 つを呼ぶだけでよい。                                           */
/* ------------------------------------------------------------------ */

/* 画面の範囲に収める。
 *
 * **負の側は画面の有無に関係なく 0 で止める。** スクリーン左上を (0,0)
 * とした絶対座標（仕様 §1.7.3）に負の位置は無いので、画面が無い構成でも
 * 同じでなければならない。上限だけが画面の大きさに依る。 */
LOCAL PNT clamp_to_screen(PNT p)
{
	CONST Screen *sc = screen();

	if (p.x < 0) {
		p.x = 0;
	}
	if (p.y < 0) {
		p.y = 0;
	}
	if (sc == 0 || sc->width == 0 || sc->height == 0) {
		return p;
	}
	if (p.x > (H)(sc->width - 1U)) {
		p.x = (H)(sc->width - 1U);
	}
	if (p.y > (H)(sc->height - 1U)) {
		p.y = (H)(sc->height - 1U);
	}
	return p;
}

EXPORT void evt_set_kbd(CONST KBD_ID *id, CONST KeyTab *tab)
{
	UD state = hal_int_disable();

	if (id != 0) {
		kbd_id = *id;
	}
	if (tab != 0 && tab->keymax >= 1 && tab->keymax <= KEYMAX &&
	    tab->kctmax >= 1 && tab->kctmax <= KCTSEL &&
	    tab->keymax * tab->kctmax <= KCTMAX) {
		keytab = *tab;
		keytab_set = True;
	}
	hal_int_restore(state);
}

EXPORT void evt_set_pda(W atr)
{
	pd_attr = atr;
}

EXPORT void evt_meta(UW bits, UW mask)
{
	UD state = hal_int_disable();

	/* 仕様「メタキーは押されてもイベントを発生しない。状態だけが載る」。
	 * だからキューには触らない。 */
	cur_stat = (cur_stat & ~mask) | (bits & mask);
	hal_int_restore(state);
}

/* メタキー状態 (CERLKA) を kctsel の添字に組み立てる。
 * **ビットの順番を取り違えると全く別の文字が出る**（event.h の注意）。 */
LOCAL UW meta_index(void)
{
	UW v = 0;

	if ((cur_stat & ES_ALPH) != 0) {
		v |= KM_A;
	}
	if ((cur_stat & ES_KANA) != 0) {
		v |= KM_K;
	}
	if ((cur_stat & (ES_LSHFT | ES_LLSHFT | ES_TLSHFT)) != 0) {
		v |= KM_L;
	}
	if ((cur_stat & (ES_RSHFT | ES_LRSHFT | ES_TRSHFT)) != 0) {
		v |= KM_R;
	}
	if ((cur_stat & (ES_EXT | ES_LEXT | ES_TEXT)) != 0) {
		v |= KM_E;
	}
	if ((cur_stat & (ES_CMD | ES_LCMD | ES_TCMD)) != 0) {
		v |= KM_C;
	}
	return v;
}

/* キートップコードを現在のメタキー状態で文字コードに直す。 */
LOCAL TC encode(UH keytop)
{
	UW sel;
	W tbl;

	if (!keytab_set || (W)keytop >= keytab.keymax) {
		return TNULL;
	}
	sel = meta_index();
	tbl = keytab.kctsel[sel];
	if (tbl < 0 || tbl >= keytab.kctmax) {
		return TNULL;
	}
	return keytab.kct[tbl * keytab.keymax + (W)keytop];
}

EXPORT void evt_key(UH keytop, Bool down)
{
	UD state;
	EVENT ev;

	if (keytop >= KEYMAX) {
		return;
	}
	state = hal_int_disable();
	ev.type = down ? EV_KEYDWN : EV_KEYUP;
	ev.time = etimer();
	ev.pos = pd_pos;
	ev.data.key.keytop = keytop;
	ev.data.key.code = encode(keytop);
	ev.stat = cur_stat;
	/* 仕様「システムイベントマスクの "0" のビットに対応するタイプの
	 * イベントはシステム全体として無視されて捨てられる」。
	 * **ただしキー状態配列は更新しなければならない。**押されている
	 * ことは get_kmp で見えるべきで、マスクはイベントの話である。 */
	key_set(key_state, keytop, down);
	if ((sys_mask & EVMASK(ev.type)) == 0) {
		hal_int_restore(state);
		return;
	}
	(void)enqueue(&ev, 0); /* 満杯なら黙って捨てる（仕様）*/
	hal_int_restore(state);
	notify_req(&ev, 0);
}

EXPORT void evt_pd_moved(PNT pos)
{
	UD state = hal_int_disable();

	/* 仕様 §1.7.3「スクリーンの解像度に対応したレンジの絶対座標値を持つ」。
	 * 位置が変わっただけではイベントは起きない（イベントタイプが無い）。 */
	pd_pos = clamp_to_screen(pos);
	hal_int_restore(state);
}

EXPORT void evt_pd_delta(W dx, W dy)
{
	PNT p;
	UD state = hal_int_disable();

	p = pd_pos;
	/* PNT は 16bit なので、足してから丸めるのではなく W で計算して
	 * 画面の範囲に収めてから詰める。溢れた向きを取り違えないため。 */
	p.x = (H)((W)p.x + dx > 32767 ? 32767
	                             : ((W)p.x + dx < -32768 ? -32768 : (W)p.x + dx));
	p.y = (H)((W)p.y + dy > 32767 ? 32767
	                             : ((W)p.y + dy < -32768 ? -32768 : (W)p.y + dy));
	pd_pos = clamp_to_screen(p);
	hal_int_restore(state);
}

EXPORT void evt_pd_button(Bool sub, Bool down)
{
	UD state;
	EVENT ev;

	state = hal_int_disable();
	if (sub) {
		/* 仕様「PD のサブボタン（メニューボタン）も同様に
		 * イベントを発生しない」。状態だけを載せる。 */
		if (down) {
			cur_stat |= ES_BUT2;
		} else {
			cur_stat &= ~(UW)ES_BUT2;
		}
		hal_int_restore(state);
		return;
	}
	ev.type = down ? EV_BUTDWN : EV_BUTUP;
	ev.time = etimer();
	ev.pos = pd_pos;
	ev.data.info = 0;
	/* stat は「イベント発生時点の状態」。ES_BUT 自身はこのイベントが
	 * 起こした変化なので、enqueue が更新する前の値を載せる。 */
	ev.stat = cur_stat;
	if ((sys_mask & EVMASK(ev.type)) == 0) {
		/* マスクされていてもボタン状態は追う（キーと同じ理由）。 */
		if (down) {
			cur_stat |= ES_BUT;
		} else {
			cur_stat &= ~(UW)ES_BUT;
		}
		hal_int_restore(state);
		return;
	}
	(void)enqueue(&ev, 0);
	hal_int_restore(state);
	notify_req(&ev, 0);
}

EXPORT PNT evt_pd_pos(void)
{
	return pd_pos;
}

EXPORT ERR evt_init(void)
{
	W i;

	for (i = 0; i < EV_QUEUE_MAX; i++) {
		queue[i].used = False;
	}
	for (i = 0; i < EV_REQ_MAX; i++) {
		req[i].pid = 0;
		req[i].mask = 0;
	}
	for (i = 0; i <= EV_MAX; i++) {
		last_time[i] = 0;
	}
	for (i = 0; i < KEYMAX / 8; i++) {
		key_state[i] = 0;
		/* 仕様「システム立ち上げ時には、メタキーを除いたすべてのキーを
		 * 自動リピートの対象とする」。メタキーはキートップコードが
		 * 分からないので、全キーを対象にしておく（メタキーは
		 * そもそもイベントを発生しないので実害が無い）。 */
		rep_mask[i] = 0xFF;
	}
	for (i = 0; i < (W)(sizeof(buz_val) / sizeof(buz_val[0])); i++) {
		buz_val[i] = 0;
	}
	next_seq = 1;
	sys_mask = EM_NULL; /* 仕様: 起動時は 0 */
	pd_pos.x = 0;
	pd_pos.y = 0;
	pd_attr = PD_REL;
	cur_stat = 0;
	rep_offset = 500;  /* 押してから最初のリピートまで */
	rep_interval = 100; /* その後の間隔 */
	last_keytop = 0;
	last_keycode = 0;
	last_keydwn_time = 0;
	last_autkey_time = 0;
	keytab_set = False;
	/* キーボードは接続されていない。仕様の「未定義キーボード」。 */
	kbd_id.kind = (UH)(KB_UNDEF << KB_KIND_SHIFT);
	kbd_id.maker = 0;
	for (i = 0; i < 4; i++) {
		kbd_id.id[i] = 0;
	}
	return ER_OK;
}

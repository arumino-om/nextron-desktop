/*
 * message.c — 周辺核 §1.2「メッセージ管理」
 *
 * snd_msg / rcv_msg / clr_msg / req_tmg / can_tmg / def_msg / ret_msg /
 * brk_msg。プロセスに付属するメッセージキューを実装する。
 *
 * ------------------------------------------------------------------------
 * なぜこれが要るか
 * ------------------------------------------------------------------------
 *
 * §1.1 の `ext_prc` / `ter_prc` の終了コードは**ここを通って親に届く**。
 * 仕様が「子プロセスの終了メッセージ等のシステムでの用途にも使用される」と
 * 書いているとおりで、プロセス管理とメッセージ管理は対になっている。
 *
 * また外殻のマネージャはタイプ 11〜23（外殻予約）を使い、ウィンドウ
 * マネージャの `wget_evt` は `rcv_msg(WAIEVT)` + `brk_msg` で実現される
 * （仕様が明記している）。マネージャ層の土台である。
 *
 * ------------------------------------------------------------------------
 * キューの作り
 * ------------------------------------------------------------------------
 *
 * メッセージはプロセスごとに持たせるのではなく**全体で 1 つのプールに
 * 置き、宛先 PID で絞る**。プロセスごとに固定長の配列を持つと
 * 32 プロセス × 24 通 × 本体 256B で 200KB になってしまう。
 * 本体だけ kmalloc するのも手だが、1 通ごとに確保と解放が走る。
 *
 * FIFO は通し番号 (`seq`) で表す。仕様が「メッセージキューは FIFO であり、
 * 必ず送信順に入れられる」と規定しているので、受信は宛先が一致する中で
 * 最小の `seq` を選ぶ。
 *
 * ------------------------------------------------------------------------
 * システムメッセージは捨てられない
 * ------------------------------------------------------------------------
 *
 * 仕様「これらは、基本的にメッセージキューのオーバーフローの影響を受けず、
 * 捨てられることはないようになっている」。通常の容量 MSG_QUEUE_MAX の上に
 * MSG_RESERVE の予備枠を置き、タイプ 1〜5 だけがそこへ入れる。
 *
 * ------------------------------------------------------------------------
 * メッセージハンドラ (§1.2.3)
 * ------------------------------------------------------------------------
 *
 * 「メインタスクの現在の処理に割り込む形で実行される」。実装は
 * `hal_ctx_inject()` で**止まっているメインタスクのスタックにハンドラ
 * 呼び出しを 1 枚差し込む**。ディスパッチャがそのタスクを選ぶとハンドラが
 * 走り、戻ると割り込んだ位置から再開する。割込みから戻るのと同じ形を
 * タスクのスタックの上で作っている（context.S の btron_msg_trampoline）。
 *
 * 待ち状態のときは、仕様「中断されたシステムコールは ER_MINTR のエラーと
 * して戻る」に従って先に待ちを ER_MINTR で解除してから差し込む。
 * 解除でメインタスクの退避 SP は変わらないので、ハンドラから戻ると
 * `knl_wait()` が ER_MINTR を返すところへ帰る。
 *
 * **自プロセス宛の場合は直接呼ぶ。** 実行状態のタスクは SP を退避して
 * いないので差し込めず、かつ「現在の処理に割り込む」の意味は直接呼び出しと
 * 同じになる。
 *
 * 入れ子は仕様で禁じられている。ハンドラ実行中に届いたものはキューに残し、
 * `msg_tick()` が次の刻みで拾う（遅れは最大 1ms）。
 */

#include <btron/message.h>
#include <btron/proc.h>

#include "../core/knl.h"

/* ------------------------------------------------------------------ */
/* 状態                                                                */
/* ------------------------------------------------------------------ */

typedef struct MsgEnt {
	W to;       /* 宛先 PID。0 なら未使用 */
	W from;     /* 送信元 PID */
	W msg_type;
	W msg_size; /* 本体のバイト数 */
	UW seq;     /* FIFO の通し番号 */
	ID confirm; /* CONFM で待っているタスク。0 なら無し */
	UB body[MSG_MAX_BODY];
} MsgEnt;

LOCAL MsgEnt pool[MSG_POOL];
LOCAL UW next_seq;

typedef struct MsgState {
	FUNCP handler[MS_MAX + 1]; /* 添字 = メッセージタイプ */
	Queue sendq;               /* キューの空き待ち */
	Queue recvq;               /* 受信待ち */
	MESSAGE *hbuf;             /* ハンドラへ渡すメッセージの置き場 */
	Bool in_handler;           /* ハンドラ実行中。入れ子を避ける */
} MsgState;

LOCAL MsgState st[PRC_MAX_PRCID + 1];

/* タイムアウトメッセージ要求 (§1.2.5 req_tmg) */
typedef struct TmgReq {
	W pid;
	W code;
	SYSTIM at; /* 送出する絶対時刻 (knl_otm) */
	Bool used;
} TmgReq;

LOCAL TmgReq tmg[MSG_TMG_MAX];

/* WAIEVT は「システム全体で 1 つのタスクのみ」（仕様）。 */
LOCAL ID waievt_tsk;
LOCAL Bool waievt_pending; /* brk_msg の要求。回数は記録しない（仕様） */

/* ハンドラを起動できずキューに残したものがあるか。msg_tick の空振りを避ける。 */
LOCAL Bool handler_pending;

/* ------------------------------------------------------------------ */
/* 補助                                                                */
/* ------------------------------------------------------------------ */

LOCAL Bool valid_pid(W pid)
{
	return (pid >= 1 && pid <= PRC_MAX_PRCID) ? True : False;
}

/* pid の指定 (>0 任意 / 0 自プロセス / -1 親プロセス) を解く。
 * 解けなければ 0 を返し *er に理由を入れる。 */
LOCAL W resolve_pid(W pid, ER *er)
{
	W me = prc_self();
	W r;

	if (me < 1) {
		*er = ER_CTX; /* プロセスに属さないタスク */
		return 0;
	}
	if (pid == P_SELF) {
		return me;
	}
	if (pid == P_PARENT) {
		r = prc_sts(P_PARENT, 0, 0);
		if (r < 1) {
			*er = ER_NOPRC;
			return 0;
		}
		return r;
	}
	if (!prc_exists(pid)) {
		*er = ER_NOPRC;
		return 0;
	}
	return pid;
}

LOCAL W queued(W pid)
{
	W n = 0;
	W i;

	for (i = 0; i < MSG_POOL; i++) {
		if (pool[i].to == pid) {
			n++;
		}
	}
	return n;
}

/* 宛先 pid の中で最小の seq を持つ、マスクに合うメッセージ。
 * any = True なら t_mask を無視して最も古いものを返す（CHECK 用）。 */
LOCAL MsgEnt *oldest(W pid, W t_mask, Bool any)
{
	MsgEnt *best = 0;
	W i;

	for (i = 0; i < MSG_POOL; i++) {
		MsgEnt *e = &pool[i];

		if (e->to != pid) {
			continue;
		}
		if (!any && (MSGMASK(e->msg_type) & t_mask) == 0) {
			continue;
		}
		if (best == 0 || e->seq < best->seq) {
			best = e;
		}
	}
	return best;
}

/* 待ちを中断するときに入れる値。**待ち要因によってエラーの族が違う。**
 *
 * 中心核の待ち（`tk_slp_tsk` など）には中心核の `E_RLWAI` を入れる。
 * 周辺核のラッパ（`slp_tsk` など）が境界でそれを `ER_MINTR` に直す。
 * 一方、周辺核が自分で張った待ち（ランデブ・メッセージ）には
 * `ER_MINTR` をそのまま入れる — そちらは中心核を経由しないので、
 * 中心核の値を入れると呼び出し側に別族が漏れる。
 *
 * 一律にするとどちらかの層で必ず食い違う（規律 R20）。sched.c の
 * `timeout_result()` が同じ理由で同じ形をしている。 */
LOCAL ER intr_result(CONST TCB *t)
{
	switch (t->waitcause) {
	case TTW_CAL:
	case TTW_ACP:
	case TTW_RDV:
	case TTW_SMSG:
	case TTW_RMSG:
	case TTW_CFM:
		return ER_MINTR; /* 周辺核が張った待ち */
	default:
		return E_RLWAI; /* 中心核の待ち。ラッパが ER_MINTR に直す */
	}
}

/* 確認待ちの送信側を解放する。受信・クリアのどちらでも「受信した」扱い。 */
LOCAL void confirm_done(MsgEnt *e)
{
	if (e->confirm >= 1 && e->confirm <= TK_MAX_TSKID) {
		TCB *t = &knl_tcb[e->confirm];

		if (t->used && (t->state & TTS_WAI) != 0 && t->waitcause == TTW_CFM) {
			knl_wait_release(t, ER_OK);
		}
	}
	e->confirm = 0;
}

/* 枠を空ける。空き待ちの送信側を 1 つ起こす。 */
LOCAL void release_ent(MsgEnt *e)
{
	W to = e->to;

	confirm_done(e);
	e->to = 0;
	e->from = 0;
	e->msg_type = 0;
	e->msg_size = 0;
	e->seq = 0;
	if (valid_pid(to) && !knl_queue_empty(&st[to].sendq)) {
		knl_wait_release(st[to].sendq.head, ER_OK);
	}
}

/* 受信待ちの中から、マスクが噛み合うタスクを 1 つ起こす。 */
LOCAL void wake_receiver(W pid, W msg_type)
{
	TCB *t;

	for (t = st[pid].recvq.head; t != 0; t = t->next) {
		/* wait_ptn に t_mask を入れてある。0 は WAIEVT の「受信しない」指定。 */
		if (t->wait_ptn == 0 ||
		    (t->wait_ptn & (UINT)MSGMASK(msg_type)) != 0) {
			knl_wait_release(t, ER_OK);
			return;
		}
	}
}

/* ------------------------------------------------------------------ */
/* ハンドラの起動 (§1.2.3)                                             */
/* ------------------------------------------------------------------ */

/* ハンドラへ渡す MESSAGE を用意する。入れ子が無いので 1 プロセス 1 枚。 */
LOCAL MESSAGE *handler_buf(W pid, CONST MsgEnt *e)
{
	MESSAGE *m = st[pid].hbuf;
	UB *dst;
	W i;

	if (m == 0) {
		m = (MESSAGE *)kmalloc((UD)MSGSIZE(MSG_MAX_BODY));
		if (m == 0) {
			return 0;
		}
		st[pid].hbuf = m;
	}
	m->msg_type = e->msg_type;
	m->msg_size = e->msg_size;
	dst = (UB *)(VP)&m->msg_body;
	for (i = 0; i < e->msg_size; i++) {
		dst[i] = e->body[i];
	}
	return m;
}

/* ハンドラの扱い。dispatch_handler の戻り値。 */
#define DISP_QUEUE 0  /* ハンドラに回らない。通常どおりキューへ */
#define DISP_DONE 1   /* 処理済み。キューには入れない */
#define DISP_DIRECT 2 /* 呼び出し側が割込みを許してから直接呼ぶこと */

/* 利用者定義のハンドラを起動する。
 *
 * 戻り値 DISP_DIRECT のときは `*direct` 等に呼び出す内容を入れて返す。
 * **自プロセス宛は差し込めない** — 実行状態のタスクは SP を退避していない。
 * その場合は「現在の処理に割り込む」の意味が直接呼び出しと同じになるので、
 * 呼び出し側が割込みを許してから呼ぶ。
 *
 * DISP_QUEUE はいま起動できない（メインタスクが休止・ハンドラ実行中）。
 * 入れ子は仕様で禁じられているので、キューに残して msg_tick に拾わせる。 */
LOCAL W invoke_user(W pid, MsgEnt *e, MSGHANDLER h, Bool allow_direct,
                    MSGHANDLER *direct, W *dfrom, MESSAGE **dmsg)
{
	ID mt = prc_main_task(pid);
	TCB *t;
	MESSAGE *m;
	W from;
	UD newsp;

	if (st[pid].in_handler || mt < 1 || mt > TK_MAX_TSKID) {
		return DISP_QUEUE;
	}
	t = &knl_tcb[mt];
	if (!t->used || t->state == TTS_DMT) {
		return DISP_QUEUE;
	}
	if (t == knl_cur && !allow_direct) {
		/* 割込みハンドラの中（msg_tick）から直接呼ぶとタスク独立部で
		 * ハンドラが走ってしまう。次の機会に回す。 */
		return DISP_QUEUE;
	}
	m = handler_buf(pid, e);
	if (m == 0) {
		return DISP_QUEUE;
	}
	/* 仕様「送信したプロセスの ID（自プロセスの場合は 0）」 */
	from = (e->from == pid) ? 0 : e->from;

	/* 起動の原因になったメッセージはキューから取り出される（仕様）。 */
	release_ent(e);
	st[pid].in_handler = True;

	if (t == knl_cur) {
		*direct = h;
		*dfrom = from;
		*dmsg = m;
		return DISP_DIRECT;
	}

	if ((t->state & TTS_WAI) != 0) {
		/* 仕様「中断されたシステムコールは ER_MINTR のエラーとして戻る」。
		 * 先に待ちを解いておけば、ハンドラから戻った先で knl_wait() が
		 * 中断を返す。退避 SP は待ち解除では変わらない。 */
		knl_wait_release(t, intr_result(t));
	}
	newsp = hal_ctx_inject(t->sp, (void (*)(W, VP))h, from, (VP)m);
	if (newsp == 0) {
		st[pid].in_handler = False;
		return DISP_QUEUE;
	}
	t->sp = newsp;
	return DISP_DONE;
}

/* 届いたメッセージをハンドラに回す。 */
LOCAL W dispatch_handler(W pid, MsgEnt *e, Bool allow_direct,
                         MSGHANDLER *direct, W *dfrom, MESSAGE **dmsg)
{
	FUNCP h = st[pid].handler[e->msg_type];
	ID mt;

	if (h == 0) {
		return DISP_QUEUE; /* ハンドラ未定義 */
	}
	if (h == MH_NONE) {
		/* 仕様「何もせずにメッセージは無視される。この場合は、例外的に
		 * 待ちを含むシステムコールは中断されない」 */
		release_ent(e);
		return DISP_DONE;
	}
	if (h == MH_BREAK) {
		/* 仕様「メッセージは無視される。待ちを含むシステムコールは中断され、
		 * ER_MINTR エラーが戻る」 */
		release_ent(e);
		mt = prc_main_task(pid);
		if (mt >= 1 && mt <= TK_MAX_TSKID) {
			TCB *t = &knl_tcb[mt];

			if (t->used && (t->state & TTS_WAI) != 0) {
				knl_wait_release(t, intr_result(t));
			}
		}
		return DISP_DONE;
	}
	if (h == MH_TERM) {
		/* 仕様「自プロセスを異常終了し、MS_ABORT メッセージ（エラーコードは
		 * 0）を親プロセスに送信する」。「自」は**受信側**のプロセスである。 */
		release_ent(e);
		(void)prc_abort(pid, 0);
		return DISP_DONE;
	}
	{
		/* 仕様の `def_msg` は `FP`（引数も戻り値も不定の関数ポインタ）で
		 * 受ける一方、ハンドラの実体は `VOID msg_hdr(W, MESSAGE*)` である。
		 * 型が違う関数ポインタの変換は定義されないので、いったんデータ
		 * ポインタを経由する（規律 R7 の境界と同じ扱い）。 */
		MSGHANDLER uh;
		VP raw = (VP)h;
		W d;

		uh = (MSGHANDLER)raw;
		d = invoke_user(pid, e, uh, allow_direct, direct, dfrom, dmsg);

		if (d == DISP_QUEUE) {
			handler_pending = True;
		}
		return d;
	}
}

/* ------------------------------------------------------------------ */
/* 送信                                                                */
/* ------------------------------------------------------------------ */

/* プールに 1 通入れる。入らなければ 0。 */
LOCAL MsgEnt *enqueue(W to, W from, CONST MESSAGE *msg)
{
	MsgEnt *e = 0;
	W cap = MSG_QUEUE_MAX;
	CONST UB *src;
	W i;

	/* 仕様: システムメッセージはキューのオーバーフローの影響を受けない。 */
	if (msg->msg_type <= MS_SYSTEM_LAST) {
		cap = MSG_QUEUE_MAX + MSG_RESERVE;
	}
	if (queued(to) >= cap) {
		return 0;
	}
	for (i = 0; i < MSG_POOL; i++) {
		if (pool[i].to == 0) {
			e = &pool[i];
			break;
		}
	}
	if (e == 0) {
		return 0;
	}
	e->to = to;
	e->from = from;
	e->msg_type = msg->msg_type;
	e->msg_size = msg->msg_size;
	e->seq = next_seq++;
	e->confirm = 0;
	src = (CONST UB *)(CONST VP)&msg->msg_body;
	for (i = 0; i < msg->msg_size; i++) {
		e->body[i] = src[i];
	}
	return e;
}

EXPORT ERR snd_msg(W pid, CONST MESSAGE *msg, W opt)
{
	ER er = ER_OK;
	W me = prc_self();
	W to;
	MsgEnt *e;
	UD state;

	if (msg == 0) {
		return ER_ADR;
	}
	if (opt != WAIT && opt != NOWAIT && opt != CONFM) {
		return ER_PAR;
	}
	if (msg->msg_type < MS_MIN || msg->msg_type > MS_MAX) {
		return ER_PAR;
	}
	/* 仕様「メッセージ本体のサイズがシステムの制限を越えた、または 0 以下」 */
	if (msg->msg_size <= 0 || msg->msg_size > MSG_MAX_BODY) {
		return ER_SZOVR;
	}
	to = resolve_pid(pid, &er);
	if (to < 1) {
		return er;
	}
	if (opt == CONFM && to == me) {
		return ER_SELF; /* 仕様「送信先が自プロセスの場合、CONFM はエラー」 */
	}

	state = hal_int_disable();
	for (;;) {
		e = enqueue(to, me, msg);
		if (e != 0) {
			break;
		}
		if (opt == NOWAIT) {
			hal_int_restore(state);
			return ER_NOSPC; /* 送信先のキューがフル */
		}
		/* 空くまで待つ。待っている間に送信先が終了したら ER_NOPRC
		 * （msg_release_process が解除する）。 */
		er = knl_wait(&st[to].sendq, TTW_SMSG, (ID)to, TMO_FEVR);
		if (er != ER_OK) {
			hal_int_restore(state);
			return er;
		}
		if (!prc_exists(to)) {
			hal_int_restore(state);
			return ER_NOPRC;
		}
	}

	/* ハンドラが定義されていれば、キューに積まずにそちらへ回す。 */
	{
		MSGHANDLER dh = 0;
		W dfrom = 0;
		MESSAGE *dmsg = 0;
		W d = dispatch_handler(to, e, True, &dh, &dfrom, &dmsg);

		if (d == DISP_DIRECT) {
			/* **割込みを許してから呼ぶ。** ハンドラは「通常のプロセス
			 * コードの一部として動作し、実行できるシステムコール等の
			 * 制限はない」（仕様）ので、割込み禁止のまま呼べない。 */
			hal_int_restore(state);
			dh(dfrom, dmsg);
			st[to].in_handler = False;
			return ER_OK;
		}
		if (d == DISP_DONE) {
			knl_dispatch();
			hal_int_restore(state);
			return ER_OK;
		}
	}
	wake_receiver(to, msg->msg_type);
	if (opt != CONFM) {
		knl_dispatch();
		hal_int_restore(state);
		return ER_OK;
	}
	/* 仕様「送信先プロセスが送信したメッセージを受信、またはメッセージ
	 * キューからクリアした時点で正常終了する。それまで待つ」 */
	e->confirm = knl_cur->tskid;
	er = knl_wait(0, TTW_CFM, (ID)to, TMO_FEVR);
	hal_int_restore(state);
	return er;
}

/* ------------------------------------------------------------------ */
/* 受信                                                                */
/* ------------------------------------------------------------------ */

/* メッセージを利用者の領域へ写す。入り切らなければ False。 */
LOCAL Bool copy_out(CONST MsgEnt *e, MESSAGE *msg, W msgsz, Bool header_only)
{
	UB *dst;
	W n;
	W i;

	msg->msg_type = e->msg_type;
	msg->msg_size = e->msg_size;
	if (header_only) {
		return True;
	}
	if (msgsz < MSGSIZE(e->msg_size)) {
		/* 仕様「msgsz の範囲内のみ msg の領域に格納されてエラー終了する」。
		 * ヘッダは入れてあるので、呼び出し側は必要な大きさが分かる。 */
		n = msgsz - MSG_HEADER_SIZE;
		dst = (UB *)(VP)&msg->msg_body;
		for (i = 0; i < n; i++) {
			dst[i] = e->body[i];
		}
		return False;
	}
	dst = (UB *)(VP)&msg->msg_body;
	for (i = 0; i < e->msg_size; i++) {
		dst[i] = e->body[i];
	}
	return True;
}

EXPORT WERR rcv_msg(W t_mask, MESSAGE *msg, W msgsz, W opt)
{
	W me = prc_self();
	Bool nowait = ((opt & NOWAIT) != 0) ? True : False;
	Bool waievt = ((opt & WAIEVT) != 0) ? True : False;
	Bool noclr = ((opt & NOCLR) != 0) ? True : False;
	Bool check = ((opt & CHECK) != 0) ? True : False;
	UD state;
	ER er;

	if (msg == 0) {
		return ER_ADR;
	}
	if (me < 1) {
		return ER_CTX;
	}
	/* 仕様「msgsz ≧ 8 でなくてはいけない」 */
	if (msgsz < MSG_HEADER_SIZE) {
		return ER_PAR;
	}
	/* 仕様「WAIEVT 指定時以外 t_mask ≦ 0、WAIEVT 指定時 t_mask < 0」は不正 */
	if (waievt ? (t_mask < 0) : (t_mask <= 0)) {
		return ER_PAR;
	}

	state = hal_int_disable();
	if (waievt) {
		/* 仕様「同時にはシステム全体で 1 つのタスクのみ…最後に呼び出した
		 * タスクの WAIEVT 指定のみが有効」 */
		waievt_tsk = knl_cur->tskid;
	}
	for (;;) {
		MsgEnt *e = (t_mask > 0) ? oldest(me, t_mask, False) : 0;
		W from;

		if (e != 0) {
			from = (e->from == me) ? 0 : e->from;
			if (!copy_out(e, msg, msgsz, False)) {
				/* 入り切らなかった。CLR 指定でもキューに残す（仕様）。 */
				hal_int_restore(state);
				return ER_PAR;
			}
			/* NOCLR でも「受信したとみなされる」（仕様の CONFM の説明）。 */
			confirm_done(e);
			if (!noclr) {
				release_ent(e);
			}
			knl_dispatch();
			hal_int_restore(state);
			return from;
		}

		if (check) {
			/* 仕様「指定したタイプのメッセージが存在せず、それ以外の
			 * メッセージが存在している場合は、メッセージの先頭 8 バイトのみを
			 * msg に格納し正常終了する。CLR 指定があってもキューに残す」 */
			MsgEnt *o = oldest(me, 0, True);

			if (o != 0) {
				from = (o->from == me) ? 0 : o->from;
				(void)copy_out(o, msg, msgsz, True);
				hal_int_restore(state);
				return from;
			}
		}

		if (waievt && waievt_pending && waievt_tsk == knl_cur->tskid) {
			waievt_pending = False;
			hal_int_restore(state);
			return ER_NONE; /* イベント発生で待ちが解けた（仕様） */
		}
		if (nowait) {
			hal_int_restore(state);
			return ER_NONE;
		}
		/* 待つ。マスクは TCB に預けて wake_receiver に選ばせる。 */
		knl_cur->wait_ptn = (UINT)t_mask;
		er = knl_wait(&st[me].recvq, TTW_RMSG, (ID)me, TMO_FEVR);
		if (er != ER_OK) {
			hal_int_restore(state);
			return er; /* ER_MINTR / ER_NONE (brk_msg) など */
		}
	}
}

/* 宛先 pid のメッセージを FIFO 順に並べる。戻り値は個数。 */
LOCAL W collect(W pid, MsgEnt **q, W max)
{
	W n = 0;
	UW base = 0;
	Bool first = True;

	for (;;) {
		MsgEnt *best = 0;
		W i;

		for (i = 0; i < MSG_POOL; i++) {
			MsgEnt *e = &pool[i];

			if (e->to != pid) {
				continue;
			}
			if (!first && e->seq <= base) {
				continue;
			}
			if (best == 0 || e->seq < best->seq) {
				best = e;
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

EXPORT ERR clr_msg(W t_mask, W last_mask)
{
	W me = prc_self();
	MsgEnt *q[MSG_QUEUE_MAX + MSG_RESERVE];
	UD state;
	W n;
	W i;

	if (me < 1) {
		return ER_CTX;
	}
	if (t_mask <= 0 || last_mask < 0) {
		return ER_PAR;
	}
	state = hal_int_disable();
	n = collect(me, q, (W)(sizeof(q) / sizeof(q[0])));

	if (last_mask == MM_ALL) {
		/* 仕様「last_mask = MM_ALL の時は、特別にメッセージを 1 つだけ
		 * クリアすることを意味する」。
		 *
		 * **この特例が無いと何もクリアされない。** 素朴に「last_mask の
		 * タイプの直前まで」と解くと、MM_ALL は全タイプを含むので
		 * 先頭のメッセージで止まってしまう。 */
		for (i = 0; i < n; i++) {
			if ((MSGMASK(q[i]->msg_type) & t_mask) != 0) {
				release_ent(q[i]);
				break;
			}
		}
	} else {
		for (i = 0; i < n; i++) {
			if ((MSGMASK(q[i]->msg_type) & last_mask) != 0) {
				break; /* 終了タイプ。これはクリアしない（仕様） */
			}
			if ((MSGMASK(q[i]->msg_type) & t_mask) != 0) {
				release_ent(q[i]);
			}
		}
	}
	knl_dispatch();
	hal_int_restore(state);
	return ER_OK;
}

/* ------------------------------------------------------------------ */
/* タイムアウトメッセージ (§1.2.5)                                     */
/* ------------------------------------------------------------------ */

EXPORT ERR req_tmg(W time, W code)
{
	W me = prc_self();
	UD state;
	W i;

	if (me < 1) {
		return ER_CTX;
	}
	if (time <= 0) {
		return ER_PAR;
	}
	state = hal_int_disable();
	for (i = 0; i < MSG_TMG_MAX; i++) {
		if (!tmg[i].used) {
			tmg[i].used = True;
			tmg[i].pid = me;
			tmg[i].code = code;
			tmg[i].at = knl_otm + (SYSTIM)time;
			hal_int_restore(state);
			return ER_OK;
		}
	}
	hal_int_restore(state);
	return ER_NOSPC;
}

EXPORT ERR can_tmg(void)
{
	W me = prc_self();
	UD state;
	W i;

	if (me < 1) {
		return ER_CTX;
	}
	/* 仕様「すでに送信されてメッセージキューに入っているタイムアウト
	 * メッセージはクリアされない」ので、要求だけ取り消す。 */
	state = hal_int_disable();
	for (i = 0; i < MSG_TMG_MAX; i++) {
		if (tmg[i].used && tmg[i].pid == me) {
			tmg[i].used = False;
		}
	}
	hal_int_restore(state);
	return ER_OK;
}

/* ------------------------------------------------------------------ */
/* ハンドラの定義 / 終了 / イベント通知                                */
/* ------------------------------------------------------------------ */

EXPORT ERR def_msg(W t_mask, FUNCP msghdr)
{
	W me = prc_self();
	UD state;
	W ty;

	if (me < 1) {
		return ER_CTX;
	}
	if (t_mask <= 0) {
		return ER_PAR;
	}
	state = hal_int_disable();
	for (ty = MS_MIN; ty <= MS_MAX; ty++) {
		if ((MSGMASK(ty) & t_mask) != 0) {
			st[me].handler[ty] = msghdr;
		}
	}
	hal_int_restore(state);
	return ER_OK;
}

EXPORT ERR ret_msg(W ret)
{
	W me = prc_self();

	(void)ret;
	if (me < 1) {
		return ER_CTX;
	}
	/* 仕様は ret = 0 なら「リターンしない」としている。
	 *
	 * 本実装ではハンドラ呼び出しを**スタックに差し込んだ 1 枚のフレーム**
	 * として作っているので、ハンドラが return すれば割り込んだ位置へ帰る
	 * （context.S の btron_msg_trampoline）。したがって ret_msg は普通に
	 * 戻ってよく、その直後にハンドラが return することで仕様の
	 * 「割り込んだ位置から再開」が成立する。
	 *
	 * ret ≠ 0（ハンドラから longjmp する）との差が観測できるのは、
	 * ret_msg の後にハンドラが処理を続ける場合だけで、仕様は
	 * 「メッセージハンドラの最後には必ず本システムコールを実行」と
	 * 定めているのでその形は現れない。
	 *
	 * 仕様はハンドラ外からの発行を「システムエラー」としているが、
	 * エラーコードを 1 つも挙げていない。プロセスを落とすほどの
	 * ことではないので、印を下ろして正常に戻す。 */
	st[me].in_handler = False;
	return ER_OK;
}

EXPORT ERR brk_msg(void)
{
	UD state = hal_int_disable();

	if (waievt_tsk >= 1 && waievt_tsk <= TK_MAX_TSKID) {
		TCB *t = &knl_tcb[waievt_tsk];

		if (t->used && (t->state & TTS_WAI) != 0 && t->waitcause == TTW_RMSG) {
			knl_wait_release(t, ER_NONE);
			knl_dispatch();
			hal_int_restore(state);
			return ER_OK;
		}
	}
	/* 仕様「待ちに入っているタスクが無ければ、待ち解除の要求を記録する。
	 * ただし、待ち解除の要求回数は記録されない」 */
	waievt_pending = True;
	hal_int_restore(state);
	return ER_OK;
}

/* ------------------------------------------------------------------ */
/* 内部の口                                                            */
/* ------------------------------------------------------------------ */

EXPORT void msg_notify_exit(W parent, W pid, W code, W type)
{
	MESSAGE m;
	MsgEnt *e;
	UD state;

	if (!valid_pid(parent)) {
		return; /* 親が無い（初期プロセス、または親が終了済み） */
	}
	m.msg_type = type;
	m.msg_size = 2 * (W)sizeof(W);
	m.msg_body.EXIT.pid = pid;
	m.msg_body.EXIT.code = code;

	/* **ハンドラへは回さない。** プロセスを畳んでいる途中から他プロセスの
	 * スタックへハンドラを差し込むのは危ない。キューに積んでおけば
	 * msg_tick が次の刻みで拾う。 */
	state = hal_int_disable();
	e = enqueue(parent, pid, &m);
	if (e != 0) {
		if (st[parent].handler[type] != 0) {
			handler_pending = True;
		}
		wake_receiver(parent, type);
	}
	hal_int_restore(state);
}

EXPORT void msg_release_process(W pid)
{
	UD state;
	W i;

	if (!valid_pid(pid)) {
		return;
	}
	state = hal_int_disable();
	/* このプロセス宛のメッセージを捨てる。確認待ちは解放される。 */
	for (i = 0; i < MSG_POOL; i++) {
		if (pool[i].to == pid) {
			release_ent(&pool[i]);
		}
	}
	/* このプロセスのキューの空き待ち・確認待ちは ER_NOPRC で解く。
	 * 仕様「待っている間に送信先のプロセスが終了した場合はエラー終了する」 */
	while (!knl_queue_empty(&st[pid].sendq)) {
		knl_wait_release(st[pid].sendq.head, ER_NOPRC);
	}
	while (!knl_queue_empty(&st[pid].recvq)) {
		knl_wait_release(st[pid].recvq.head, ER_NOPRC);
	}
	for (i = 0; i < MSG_POOL; i++) {
		if (pool[i].confirm >= 1 && pool[i].from == pid) {
			pool[i].confirm = 0;
		}
	}
	for (i = 0; i < MSG_TMG_MAX; i++) {
		if (tmg[i].used && tmg[i].pid == pid) {
			tmg[i].used = False;
		}
	}
	for (i = MS_MIN; i <= MS_MAX; i++) {
		st[pid].handler[i] = 0;
	}
	st[pid].in_handler = False;
	hal_int_restore(state);

	if (st[pid].hbuf != 0) {
		kfree(st[pid].hbuf);
		st[pid].hbuf = 0;
	}
}

EXPORT void msg_tick(void)
{
	W i;

	/* タイムアウトメッセージの送出 (§1.2.5 req_tmg) */
	for (i = 0; i < MSG_TMG_MAX; i++) {
		MESSAGE m;
		MsgEnt *e;
		W pid;

		if (!tmg[i].used || knl_otm < tmg[i].at) {
			continue;
		}
		pid = tmg[i].pid;
		tmg[i].used = False;
		/* **ここは割込みハンドラの中。** `prc_sts()` は「自プロセス」を
		 * 必要とするので使えない（タスク独立部では knl_cur がアイドル
		 * タスクで ER_CTX になり、判定が常に偽になる）。 */
		if (!prc_exists(pid)) {
			continue;
		}
		m.msg_type = MS_TMOUT;
		m.msg_size = (W)sizeof(W);
		m.msg_body.TMOUT.code = tmg[i].code;
		e = enqueue(pid, pid, &m);
		if (e != 0) {
			MSGHANDLER dh = 0;
			W dfrom = 0;
			MESSAGE *dmsg = 0;

			/* 割込みハンドラの中なので直接呼び出しは許さない。 */
			if (dispatch_handler(pid, e, False, &dh, &dfrom, &dmsg) ==
			    DISP_QUEUE) {
				wake_receiver(pid, MS_TMOUT);
			}
		}
	}

	/* 入れ子を避けて持ち越したハンドラを拾う。 */
	if (!handler_pending) {
		return;
	}
	handler_pending = False;
	for (i = 0; i < MSG_POOL; i++) {
		MsgEnt *e = &pool[i];
		W to = e->to;

		MSGHANDLER dh = 0;
		W dfrom = 0;
		MESSAGE *dmsg = 0;

		if (!valid_pid(to) || st[to].handler[e->msg_type] == 0) {
			continue;
		}
		(void)dispatch_handler(to, e, False, &dh, &dfrom, &dmsg);
	}
}

EXPORT W msg_count(W pid)
{
	return valid_pid(pid) ? queued(pid) : 0;
}

EXPORT W msg_used(void)
{
	W n = 0;
	W i;

	for (i = 0; i < MSG_POOL; i++) {
		if (pool[i].to != 0) {
			n++;
		}
	}
	return n;
}

EXPORT ERR msg_init(void)
{
	W i;
	W j;

	for (i = 0; i < MSG_POOL; i++) {
		pool[i].to = 0;
		pool[i].confirm = 0;
	}
	for (i = 0; i <= PRC_MAX_PRCID; i++) {
		knl_queue_init(&st[i].sendq, TA_TFIFO);
		knl_queue_init(&st[i].recvq, TA_TFIFO);
		st[i].hbuf = 0;
		st[i].in_handler = False;
		for (j = MS_MIN; j <= MS_MAX; j++) {
			st[i].handler[j] = 0;
		}
	}
	for (i = 0; i < MSG_TMG_MAX; i++) {
		tmg[i].used = False;
	}
	next_seq = 1;
	waievt_tsk = 0;
	waievt_pending = False;
	handler_pending = False;
	return ER_OK;
}

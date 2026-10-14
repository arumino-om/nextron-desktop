/*
 * taskcomm.c — ランデブ (BTRON3 §1.3「プロセス/タスク間同期通信管理」)
 *
 * **これは周辺核の機能である。中心核ではない。**
 * BTRON3 の「OS 仕様 第1章」は周辺核で、§1.1 プロセス/タスク管理、
 * §1.2 メッセージ管理、§1.3 同期通信管理、… と続く。中心核 (ITRON) は
 * 別に規定されていて、その代わりに μT-Kernel 3.0 を基準にしている (D10)。
 *
 * したがって返すのは `ER_*`（周辺核の族）で、名前に `tk_` を付けない。
 * これは例外ではなく**層が違うだけ**で、規律 R20 の言うとおりである。
 *
 * §1.3 は他にセマフォ・イベントフラグ・メッセージバッファも規定していて、
 * **中心核の `tk_cre_sem` 等とは別物**（周辺核の層にある同名機能）。
 * そちらは Phase 2 で中心核の上に載せる。ランデブだけ先に実装したのは、
 * 詳細 E1 で外殻サーバの IPC の土台に選んだからである。
 *
 * 【ランデブとは】呼出側と受付側が出会ったところで同期し、
 * 呼出メッセージを渡し、返答を受け取るまで呼出側が待つ。
 * 要するに**同期 RPC のプリミティブ**で、詳細 E1 でこれを外殻サーバの
 * IPC の土台に選んだ。待ち行列と待ち解除は中心核の knl_* を使う。
 *
 *   cal_por  呼出。成立を待ち、成立後は返答を待つ
 *   acp_por  受付。成立を待つ。成立したらランデブ番号と呼出メッセージを得る
 *   rpl_rdv  返答。ランデブを終える
 *   fwd_por  回送。受け付けたランデブを別のポートへ回す（3 者間要求）
 *
 * 成立条件は `(calptn & acpptn) != 0`。選択条件のビットで相手を選べる。
 *
 * 【待ちの構造】成立前と成立後で待ち要因が変わるが、**待ちは 1 回しか
 * 発生しない**。呼出側は `knl_wait()` に入ったまま、成立時に受付側が
 * 要因を TTW_CAL → TTW_RDV に書き換えてタイムアウトを外す。
 * 仕様の「ランデブ成立後は、ランデブの返答があるまで待つ」と
 * 「回送された呼出タスクの待ちは tmo に関係なく無限待ち」が、
 * どちらもこの一手で表現できる。
 *
 * 【msg バッファの使い回し】仕様の制約の理由がここにある。
 * 呼出側の `msg` は maxrmsz 以上なければならず、回送先の maxrmsz は
 * 回送前より大きくてはならず、回送の cmsz も回送前の maxrmsz 以下。
 * **全部「呼出側が最初に用意した 1 つのバッファを使い回す」ことから来る。**
 *
 * 【未対応】プロセス間（アドレス空間跨ぎ）。いまは同一空間のタスク間のみで、
 * メッセージの受け渡しは単純な複製になっている。空間跨ぎは 詳細 E1 ④ の
 * 「必要な拡張 1」で、周辺核のメモリ管理 (Phase 2) と一緒に入れる。
 */

#include "../core/knl.h"

IMPORT VP memcpy(VP dst, CONST VP src, UD n);

typedef struct PORCB {
	Bool used;
	W maxcmsz;
	W maxrmsz;
	UW opt;
	Queue call;   /* 成立待ちの呼出タスク (TTW_CAL) */
	Queue accept; /* 成立待ちの受付タスク (TTW_ACP) */
} PORCB;

typedef struct RDVCB {
	Bool used;
	UH gen;      /* 世代。番号の使い回しを見分ける */
	TCB *caller;
	TCB *acceptor;
	/* 受け付けたポートの maxrmsz を**複製しておく**。
	 * 成立後にポートが削除されても返答できなければならない（仕様）ので、
	 * ポートを参照したままにはできない。 */
	W maxrmsz;
} RDVCB;

LOCAL PORCB porcb[TK_MAX_PORID + 1];
LOCAL RDVCB rdvcb[TK_MAX_RDV];
LOCAL UH rdv_gen;

/* ランデブ番号の符号化。下位 16bit が添字+1、上位 15bit が世代。
 * 仕様は「>0」を正常としているので最上位ビットは立てない。 */
#define RDV_MAKE(idx, gen) ((W)(((UW)((gen) & 0x7FFFU) << 16) | (UW)((idx) + 1)))
#define RDV_IDX(no) ((W)(((UW)(no) & 0xFFFFU)) - 1)
#define RDV_GEN(no) ((UH)(((UW)(no) >> 16) & 0x7FFFU))

EXPORT void knl_rdv_init(void)
{
	W i;

	for (i = 0; i <= TK_MAX_PORID; i++) {
		porcb[i].used = False;
		knl_queue_init(&porcb[i].call, TA_TFIFO);
		knl_queue_init(&porcb[i].accept, TA_TFIFO);
	}
	for (i = 0; i < TK_MAX_RDV; i++) {
		rdvcb[i].used = False;
		rdvcb[i].gen = 0;
	}
	rdv_gen = 1;
}

LOCAL PORCB *get_por(ID id, ERR *er)
{
	if (id < 1 || id > TK_MAX_PORID) {
		*er = ER_ID;
		return 0;
	}
	if (!porcb[id].used) {
		*er = ER_NOEXS;
		return 0;
	}
	return &porcb[id];
}

/* ランデブ番号から実体を引く。自タスクが受け付けたものでなければ 0。 */
LOCAL RDVCB *get_rdv(W no)
{
	W idx = RDV_IDX(no);

	if (no <= 0 || idx < 0 || idx >= TK_MAX_RDV) {
		return 0;
	}
	if (!rdvcb[idx].used || rdvcb[idx].gen != RDV_GEN(no)) {
		return 0;
	}
	if (rdvcb[idx].acceptor != knl_cur) {
		return 0; /* 他人が受け付けたランデブは操作できない */
	}
	return &rdvcb[idx];
}

LOCAL void free_rdv(RDVCB *e)
{
	e->used = False;
	e->caller = 0;
	e->acceptor = 0;
}

/* ランデブを成立させる。
 *
 * 呼出メッセージを受付側のバッファへ渡し、受付側にランデブ番号を返す。
 * **呼出側と受付側の待ち状態はここでは触らない**（呼び出し元の経路によって
 * どちらが待っているかが違うため）。戻り値は負ならエラー。 */
LOCAL W setup_rdv(PORCB *p, TCB *caller, TCB *acceptor)
{
	W idx;
	W no;

	for (idx = 0; idx < TK_MAX_RDV; idx++) {
		if (!rdvcb[idx].used) {
			break;
		}
	}
	if (idx >= TK_MAX_RDV) {
		return ER_LIMIT;
	}
	rdv_gen = (UH)((rdv_gen + 1) & 0x7FFFU);
	if (rdv_gen == 0) {
		rdv_gen = 1;
	}
	rdvcb[idx].used = True;
	rdvcb[idx].gen = rdv_gen;
	rdvcb[idx].caller = caller;
	rdvcb[idx].acceptor = acceptor;
	rdvcb[idx].maxrmsz = p->maxrmsz;
	no = RDV_MAKE(idx, rdv_gen);

	/* **カーネルの中継どうしで写す。** どちらの文脈で走っているか
	 * 分からないので、ユーザ番地には触らない。受け取る側が自分の
	 * 文脈に戻ったときに `rdv_buf` から取り出す。 */
	memcpy(acceptor->rdv_buf, caller->rdv_buf, (UD)caller->rdv_size);
	acceptor->rdv_pending = no;
	acceptor->rdv_result = caller->rdv_size;
	return no;
}

/* 呼出側を「成立待ち」から「返答待ち」へ移す。
 *
 * 待ち状態のまま要因だけ書き換え、タイムアウトを外す。実行可能にはしない。
 * これが仕様の「成立後は返答があるまで待つ」の実装である。
 * ポート ID も捨てる。成立後のランデブはポートから独立していて、
 * ポートが削除されても返答できなければならない（仕様）。 */
LOCAL void to_reply_wait(TCB *caller)
{
	knl_queue_remove(caller);
	caller->waitcause = TTW_RDV;
	caller->wid = 0;
	caller->timeout = 0;
}

/* ------------------------------------------------------------------ */
/* ポートの生成と削除                                                  */
/* ------------------------------------------------------------------ */

EXPORT WERR cre_por(W maxcmsz, W maxrmsz, UW opt)
{
	UD state;
	ID id = 0;
	W i;

	if (maxcmsz < 1 || maxrmsz < 1) {
		return ER_PAR;
	}
	if ((opt & ~(UW)DELEXIT) != 0) {
		return ER_PAR;
	}
	state = hal_int_disable();
	for (i = 1; i <= TK_MAX_PORID; i++) {
		if (!porcb[i].used) {
			id = i;
			break;
		}
	}
	if (id == 0) {
		hal_int_restore(state);
		return ER_LIMIT;
	}
	porcb[id].used = True;
	porcb[id].maxcmsz = maxcmsz;
	porcb[id].maxrmsz = maxrmsz;
	/* DELEXIT はプロセス終了時の自動削除。プロセスは Phase 2 なので
	 * いまは記録するだけで、まだ効かない。 */
	porcb[id].opt = opt;
	knl_queue_init(&porcb[id].call, TA_TFIFO);
	knl_queue_init(&porcb[id].accept, TA_TFIFO);
	hal_int_restore(state);
	return id;
}

EXPORT ERR del_por(ID id)
{
	UD state;
	ERR er = ER_OK;
	PORCB *p;

	state = hal_int_disable();
	p = get_por(id, &er);
	if (p == 0) {
		hal_int_restore(state);
		return er;
	}
	/* 受け付け待ち・呼び出し待ちのタスクは ER_DLT で起こす（仕様）。 */
	while (!knl_queue_empty(&p->call)) {
		knl_wait_release(p->call.head, ER_DLT);
	}
	while (!knl_queue_empty(&p->accept)) {
		knl_wait_release(p->accept.head, ER_DLT);
	}
	/* **成立済みのランデブは残す。** 仕様が「ランデブ成立後にランデブ
	 * ポートが削除されても、ランデブは成立したままであり、正常に
	 * ランデブの返答ができる」と明記している。そのために RDVCB が
	 * maxrmsz を複製している。 */
	p->used = False;
	knl_dispatch();
	hal_int_restore(state);
	return ER_OK;
}

/* ------------------------------------------------------------------ */
/* 呼出と受付                                                          */
/* ------------------------------------------------------------------ */

EXPORT WERR cal_por(ID id, UW calptn, VP msg, W cmsz, TMOUT tmo)
{
	UD state;
	ERR er = ER_OK;
	PORCB *p;
	TCB *me;
	TCB *acc;
	W r;

	if (calptn == 0 || msg == 0 || cmsz < 1 || tmo < T_FOREVER) {
		return ER_PAR;
	}
	state = hal_int_disable();
	p = get_por(id, &er);
	if (p == 0) {
		hal_int_restore(state);
		return er;
	}
	if (cmsz > p->maxcmsz) {
		hal_int_restore(state);
		return ER_PAR;
	}
	me = knl_cur;
	if (me == &knl_idle) {
		hal_int_restore(state);
		return ER_CTX;
	}
	if (cmsz > RDV_MAX_MSG) {
		hal_int_restore(state);
		return ER_PAR; /* D9 の実装上限 */
	}
	me->rdv_ptn = calptn;
	me->rdv_msg = msg;
	me->rdv_size = cmsz;
	me->rdv_result = 0;
	/* **自分の文脈のうちに写す。** 以後はカーネルの中継だけを使う。 */
	memcpy(me->rdv_buf, msg, (UD)cmsz);

	acc = knl_find_match(&p->accept, calptn);
	if (acc != 0) {
		/* 受付側が待っていた。その場で成立させて相手を起こす。 */
		r = setup_rdv(p, me, acc);
		if (r < 0) {
			hal_int_restore(state);
			return r;
		}
		knl_wait_release(acc, ER_OK);
		/* 自分は返答待ち。タイムアウトは付けない（仕様）。 */
		r = knl_wait(0, TTW_RDV, 0, T_FOREVER);
	} else {
		if (tmo == T_NOWAIT) {
			hal_int_restore(state);
			return ER_NONE;
		}
		/* 成立待ち。成立すると受付側が要因を TTW_RDV に書き換えるので、
		 * この knl_wait から戻るのは**返答が来たとき**になる。 */
		r = knl_wait(&p->call, TTW_CAL, id, (TMO)tmo);
	}
	if (r != ER_OK) {
		hal_int_restore(state);
		/* ER_DLT（ポート削除）か ER_NONE（タイムアウト）。
		 * なお tk_rel_wai() で強制解除されると中心核の族の E_RLWAI が
		 * ここから出てしまう。BTRON3 §1.3 に対応するコードが無いので、
		 * 周辺核が ER_INNER_OF() で畳む想定（規律 R20）。 */
		return r;
	}
	r = me->rdv_result; /* 返答メッセージのバイト数 */
	/* 返答を自分の文脈で取り出す。 */
	if (r > 0) {
		memcpy(me->rdv_msg, me->rdv_buf, (UD)r);
	}
	hal_int_restore(state);
	return r;
}

EXPORT WERR acp_por(ID id, UW acpptn, W *rdv, VP msg, TMOUT tmo)
{
	UD state;
	ERR er = ER_OK;
	PORCB *p;
	TCB *me;
	TCB *caller;
	W r;

	if (acpptn == 0 || rdv == 0 || msg == 0 || tmo < T_FOREVER) {
		return ER_PAR;
	}
	state = hal_int_disable();
	p = get_por(id, &er);
	if (p == 0) {
		hal_int_restore(state);
		return er;
	}
	me = knl_cur;
	if (me == &knl_idle) {
		hal_int_restore(state);
		return ER_CTX;
	}
	me->rdv_ptn = acpptn;
	me->rdv_msg = msg;
	me->rdv_no = rdv;
	me->rdv_result = 0;

	caller = knl_find_match(&p->call, acpptn);
	if (caller != 0) {
		/* 呼出側が待っていた。成立させる。自分は待たない。 */
		r = setup_rdv(p, caller, me);
		if (r < 0) {
			hal_int_restore(state);
			return r;
		}
		to_reply_wait(caller);
		r = me->rdv_result;
		if (r > 0) {
			memcpy(me->rdv_msg, me->rdv_buf, (UD)r);
		}
		*me->rdv_no = me->rdv_pending;
		hal_int_restore(state);
		return r;
	}
	if (tmo == T_NOWAIT) {
		hal_int_restore(state);
		return ER_NONE;
	}
	r = knl_wait(&p->accept, TTW_ACP, id, (TMO)tmo);
	if (r != ER_OK) {
		hal_int_restore(state);
		return r;
	}
	r = me->rdv_result; /* 呼出メッセージのバイト数 */
	if (r > 0) {
		memcpy(me->rdv_msg, me->rdv_buf, (UD)r);
	}
	*me->rdv_no = me->rdv_pending;
	hal_int_restore(state);
	return r;
}

/* ------------------------------------------------------------------ */
/* 返答                                                                */
/* ------------------------------------------------------------------ */

EXPORT ERR rpl_rdv(W rdv, VP msg, W rmsz)
{
	UD state;
	RDVCB *e;
	TCB *caller;

	if (msg == 0 || rmsz < 1) {
		return ER_PAR;
	}
	state = hal_int_disable();
	e = get_rdv(rdv);
	if (e == 0) {
		hal_int_restore(state);
		return ER_OBJ;
	}
	if (rmsz > e->maxrmsz) {
		hal_int_restore(state);
		return ER_PAR;
	}
	caller = e->caller;
	/* 返答は呼出側の msg に入る。呼出側はそこに maxrmsz 以上の領域を
	 * 用意する義務があるので（仕様）、収まることが保証されている。 */
	if (rmsz > RDV_MAX_MSG) {
		hal_int_restore(state);
		return ER_PAR;
	}
	/* **呼出側のユーザ番地には触らない。** 中継へ置いて、
	 * 呼出側が自分の文脈で取り出す。 */
	memcpy(caller->rdv_buf, msg, (UD)rmsz);
	caller->rdv_result = rmsz;
	free_rdv(e);
	knl_wait_release(caller, ER_OK);
	knl_dispatch();
	hal_int_restore(state);
	return ER_OK;
}

/* ------------------------------------------------------------------ */
/* 回送                                                                */
/*                                                                     */
/* 詳細 E1 ④ が E13（3 者間の同期要求）にそのまま当てている機能。        */
/* クライアント → 実身仮身サーバ → 対象クライアント の 3 ホップが、      */
/* 中間のサーバが待たずに済む形で表現できる。                           */
/* ------------------------------------------------------------------ */

EXPORT ERR fwd_por(ID id, UW calptn, W rdv, VP msg, W cmsz)
{
	UD state;
	ERR er = ER_OK;
	PORCB *p;
	RDVCB *e;
	TCB *caller;
	TCB *acc;
	W r;

	if (calptn == 0 || msg == 0 || cmsz < 1) {
		return ER_PAR;
	}
	state = hal_int_disable();
	p = get_por(id, &er);
	if (p == 0) {
		hal_int_restore(state);
		return er;
	}
	e = get_rdv(rdv);
	if (e == 0) {
		hal_int_restore(state);
		return ER_OBJ;
	}
	/* 仕様の制約。どちらも**呼出側の msg バッファを使い回す**ことから来る。
	 * そのバッファは「回送前のポートの maxrmsz 以上」しか保証されていない。 */
	if (p->maxrmsz > e->maxrmsz) {
		hal_int_restore(state);
		return ER_OBJ; /* 回送先の返答が呼出側のバッファに収まらない */
	}
	if (cmsz > e->maxrmsz) {
		hal_int_restore(state);
		return ER_PAR; /* 回送する呼出メッセージが収まらない */
	}
	if (cmsz > p->maxcmsz) {
		hal_int_restore(state);
		return ER_PAR;
	}

	if (cmsz > RDV_MAX_MSG) {
		hal_int_restore(state);
		return ER_PAR;
	}
	caller = e->caller;
	/* 回送メッセージを呼出側の**中継**へ書き、そのまま次の呼出に使う。
	 * ユーザ番地には触らない（回送元の文脈で走っているため）。 */
	memcpy(caller->rdv_buf, msg, (UD)cmsz);
	caller->rdv_size = cmsz;
	caller->rdv_ptn = calptn;
	free_rdv(e); /* 元のランデブは解除される（仕様） */

	acc = knl_find_match(&p->accept, calptn);
	if (acc != 0) {
		r = setup_rdv(p, caller, acc);
		if (r < 0) {
			hal_int_restore(state);
			return r;
		}
		to_reply_wait(caller); /* 引き続き返答待ち */
		knl_wait_release(acc, ER_OK);
	} else {
		/* 回送先で成立しなければ成立待ちに入れる。
		 * **元の tmo に関係なく無限待ち**（仕様）。 */
		caller->waitcause = TTW_CAL;
		caller->wid = id;
		caller->timeout = 0;
		knl_queue_insert(&p->call, caller);
	}
	/* 自タスクは待ちに入らない（仕様）。 */
	knl_dispatch();
	hal_int_restore(state);
	return ER_OK;
}

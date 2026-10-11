/*
 * taskobj.c — 周辺核 §1.3 の同期通信オブジェクト（ランデブ以外）
 *
 * cre_sem / del_sem / sig_sem / wai_sem
 * cre_flg / del_flg / set_flg / clr_flg / wai_flg
 * cre_mbf / del_mbf / snd_mbf / rcv_mbf
 *
 * ランデブは taskcomm.c（先に実装した。詳細 E1 で外殻 IPC の土台に選んだ）。
 *
 * ------------------------------------------------------------------------
 * 中心核のオブジェクトへの写像
 * ------------------------------------------------------------------------
 *
 * **BTRON3 の「排他制御用セマフォ」は μT-Kernel のミューテックスである。**
 * 仕様が SEM_EXCL に課している規則を並べると完全に一致する。
 *
 *   「必ず、獲得〜開放の順で使用しなくてはならない」      = 所有権がある
 *   「すでに獲得している状態で再度獲得 → ER_OBJ」          = 再帰ロック禁止
 *   「獲得していない状態で返却 → ER_OBJ」                  = 所有者のみ解放
 *   「獲得したまま終了した場合には自動的に解放される」      = タスク終了時解放
 *   「初期値は 1 でなければならない」                       = 二値
 *
 * 一方 SEM_SYNC は「獲得・開放の順序に対する制限はない」「タスクが終了した
 * 場合の自動解放は行われない」「初期値に制限はない」で、こちらが本来の
 * 計数セマフォである。したがって写像は
 *
 *   SEM_SYNC → tk_cre_sem（計数セマフォ）
 *   SEM_EXCL → tk_cre_mtx（ミューテックス）
 *
 * となる。1 つの API が中心核の 2 つの別オブジェクトに分かれる。
 *
 * ------------------------------------------------------------------------
 * ID 空間は層ごとに別
 * ------------------------------------------------------------------------
 *
 * ここで返す ID は**この表の添字**で、中心核の ID とは別物である。
 * エラーコードの族が層ごとに別なのと同じ理由（規律 R20）で、
 * 周辺核の `wai_sem(3)` と中心核の `tk_wai_sem(3)` は無関係でよい。
 * 混ぜると、層をまたいで同じ番号が別のオブジェクトを指す事故になる。
 *
 * ------------------------------------------------------------------------
 * DELEXIT
 * ------------------------------------------------------------------------
 *
 * 仕様は 3 つとも「生成プロセス終了時に自動削除」を選べる。
 * 指定が無ければ「生成したオブジェクトはすべてのプロセスから使用可能」の
 * まま残る。中心核のオブジェクトにはこの概念が無いので、ここで持つ。
 */

#include <btron/proc.h>
#include <btron/taskcomm.h>

#include "../core/knl.h"

#define TOBJ_SEM 0
#define TOBJ_FLG 1
#define TOBJ_MBF 2

typedef struct TObj {
	W kind;
	ID kid;    /* 中心核のオブジェクト ID */
	Bool excl; /* SEM_EXCL。中心核ではミューテックス */
	W owner;   /* DELEXIT の対象プロセス。0 なら自動削除しない */
	Bool used;
} TObj;

LOCAL TObj tbl[TK_MAX_TOBJ];

/* ------------------------------------------------------------------ */
/* 補助                                                                */
/* ------------------------------------------------------------------ */

/* 中心核のエラーを周辺核の族へ直す（規律 R20）。
 *
 * **ここが層の境界である。** 中心核の `E_TMOUT` をそのまま返すと、
 * 呼び出し側は `ER_NONE` (-24) を期待しているのに -51 を受け取る。 */
LOCAL ER xlat(ER er)
{
	switch (er) {
	case E_OK:
		return ER_OK;
	case E_TMOUT:
		return ER_NONE; /* 仕様「獲得できずにタイムアウトした」 */
	case E_DLT:
		return ER_DLT;
	case E_RLWAI:
		return ER_MINTR; /* 外から待ちを壊された */
	case E_ID:
	case E_NOEXS:
		return ER_NOEXS;
	case E_PAR:
		return ER_PAR;
	case E_OBJ:
	case E_ILUSE:
		return ER_OBJ;
	case E_QOVR:
	case E_LIMIT:
		return ER_LIMIT;
	case E_NOMEM:
		return ER_NOSPC;
	default:
		return ER_INNER; /* 中心核の都合。族を跨がせない */
	}
}

/* 中心核のタイムアウト指定へ直す。T_NOWAIT / T_FOREVER は同値なので
 * そのまま通るが、負の不正値は弾く。 */
LOCAL Bool to_tmo(TMOUT tmo, TMO *out)
{
	if (tmo < T_FOREVER) {
		return False;
	}
	*out = (TMO)tmo;
	return True;
}

LOCAL TObj *get(ID id, W kind, ER *er)
{
	if (id < 1 || id > TK_MAX_TOBJ) {
		*er = ER_ID;
		return 0;
	}
	if (!tbl[id - 1].used || tbl[id - 1].kind != kind) {
		*er = ER_NOEXS;
		return 0;
	}
	return &tbl[id - 1];
}

/* 表の空き枠を押さえる。戻り値が周辺核の ID (1 起点)。0 なら満杯。 */
LOCAL ID alloc_slot(W kind, UW opt)
{
	UD state = hal_int_disable();
	W i;

	for (i = 0; i < TK_MAX_TOBJ; i++) {
		if (!tbl[i].used) {
			tbl[i].used = True;
			tbl[i].kind = kind;
			tbl[i].kid = 0;
			tbl[i].excl = False;
			tbl[i].owner = ((opt & (UW)DELEXIT) != 0) ? prc_self() : 0;
			hal_int_restore(state);
			return (ID)(i + 1);
		}
	}
	hal_int_restore(state);
	return 0;
}

LOCAL void free_slot(TObj *o)
{
	o->used = False;
	o->kid = 0;
	o->owner = 0;
	o->excl = False;
}

/* ------------------------------------------------------------------ */
/* セマフォ                                                            */
/* ------------------------------------------------------------------ */

EXPORT WERR cre_sem(W cnt, UW opt)
{
	ID id;
	TObj *o;
	ID kid;

	if ((opt & ~(UW)(SEM_EXCL | DELEXIT)) != 0) {
		return ER_PAR;
	}
	if ((opt & SEM_EXCL) != 0) {
		/* 仕様「排他制御用セマフォの場合には cnt = 1 でなければならない」 */
		if (cnt != 1) {
			return ER_PAR;
		}
	} else if (cnt < 0) {
		/* 仕様「同期用セマフォの場合には cnt ≧ 0 でなければならない」 */
		return ER_PAR;
	}
	id = alloc_slot(TOBJ_SEM, opt);
	if (id == 0) {
		return ER_LIMIT;
	}
	o = &tbl[id - 1];

	if ((opt & SEM_EXCL) != 0) {
		T_CMTX c;
		W i;

		/* 排他制御用 = ミューテックス。優先度継承にしておく（仕様は
		 * プロトコルを規定していないが、優先度逆転を放置する理由はない）。 */
		c.exinf = 0;
		c.mtxatr = TA_TFIFO | TA_INHERIT;
		c.ceilpri = 0;
		for (i = 0; i < 8; i++) {
			c.dsname[i] = 0;
		}
		kid = tk_cre_mtx(&c);
		o->excl = True;
	} else {
		T_CSEM c;
		W i;

		c.exinf = 0;
		c.sematr = TA_TFIFO;
		c.isemcnt = cnt;
		c.maxsem = 0x7FFFFFFF;
		for (i = 0; i < 8; i++) {
			c.dsname[i] = 0;
		}
		kid = tk_cre_sem(&c);
	}
	if (kid < 1) {
		free_slot(o);
		return xlat(kid);
	}
	o->kid = kid;
	return id;
}

EXPORT ERR del_sem(ID id)
{
	ER er = ER_OK;
	TObj *o = get(id, TOBJ_SEM, &er);
	ER r;

	if (o == 0) {
		return er;
	}
	r = o->excl ? tk_del_mtx(o->kid) : tk_del_sem(o->kid);
	free_slot(o);
	return xlat(r);
}

EXPORT ERR sig_sem(ID id)
{
	ER er = ER_OK;
	TObj *o = get(id, TOBJ_SEM, &er);

	if (o == 0) {
		return er;
	}
	/* 仕様「獲得していない状態で返却しようとした場合は ER_OBJ」。
	 * 中心核のミューテックスは E_ILUSE を返すので xlat が ER_OBJ に直す。 */
	return xlat(o->excl ? tk_unl_mtx(o->kid) : tk_sig_sem(o->kid, 1));
}

EXPORT ERR wai_sem(ID id, TMOUT tmo)
{
	ER er = ER_OK;
	TObj *o = get(id, TOBJ_SEM, &er);
	TMO t;

	if (o == 0) {
		return er;
	}
	if (!to_tmo(tmo, &t)) {
		return ER_PAR;
	}
	/* 仕様「すでに獲得している状態で再度獲得しようとした場合には ER_OBJ」。
	 * 中心核のミューテックスの E_ILUSE がそれに当たる。 */
	return xlat(o->excl ? tk_loc_mtx(o->kid, t) : tk_wai_sem(o->kid, 1, t));
}

/* ------------------------------------------------------------------ */
/* イベントフラグ                                                      */
/* ------------------------------------------------------------------ */

EXPORT WERR cre_flg(W ptn, UW opt)
{
	ID id;
	TObj *o;
	T_CFLG c;
	ID kid;
	W i;

	if ((opt & ~(UW)DELEXIT) != 0) {
		return ER_PAR;
	}
	if (ptn < 0) {
		return ER_PAR; /* 仕様「初期パターン (≧0)」 */
	}
	id = alloc_slot(TOBJ_FLG, opt);
	if (id == 0) {
		return ER_LIMIT;
	}
	o = &tbl[id - 1];

	c.exinf = 0;
	/* 「生成したイベントフラグは、すべてのプロセスから使用可能」なので
	 * 複数タスクの待ちを許す。 */
	c.flgatr = TA_TFIFO | TA_WMUL;
	c.iflgptn = (UINT)ptn;
	for (i = 0; i < 8; i++) {
		c.dsname[i] = 0;
	}
	kid = tk_cre_flg(&c);
	if (kid < 1) {
		free_slot(o);
		return xlat(kid);
	}
	o->kid = kid;
	return id;
}

EXPORT ERR del_flg(ID id)
{
	ER er = ER_OK;
	TObj *o = get(id, TOBJ_FLG, &er);
	ER r;

	if (o == 0) {
		return er;
	}
	r = tk_del_flg(o->kid);
	free_slot(o);
	return xlat(r);
}

EXPORT ERR set_flg(ID id, W ptn)
{
	ER er = ER_OK;
	TObj *o = get(id, TOBJ_FLG, &er);

	if (o == 0) {
		return er;
	}
	if (ptn < 0) {
		return ER_PAR;
	}
	return xlat(tk_set_flg(o->kid, (UINT)ptn));
}

EXPORT ERR clr_flg(ID id, W ptn)
{
	ER er = ER_OK;
	TObj *o = get(id, TOBJ_FLG, &er);

	if (o == 0) {
		return er;
	}
	/* 仕様「現在の値に ptn が AND される」。**消去パターンではなく
	 * 残すパターンである。** 中心核も同じ規定なのでそのまま渡す。 */
	return xlat(tk_clr_flg(o->kid, (UINT)ptn));
}

EXPORT WERR wai_flg(ID id, W ptn, UW mode, TMOUT tmo)
{
	ER er = ER_OK;
	TObj *o = get(id, TOBJ_FLG, &er);
	UINT wf;
	UINT got = 0;
	TMO t;
	ER r;

	if (o == 0) {
		return er;
	}
	if (ptn <= 0) {
		return ER_PAR; /* 仕様「待ちパターン (>0)」 */
	}
	if ((mode & ~(UW)(WF_OR | NOCLR)) != 0) {
		return ER_PAR;
	}
	if (!to_tmo(tmo, &t)) {
		return ER_PAR;
	}
	wf = ((mode & WF_OR) != 0) ? TWF_ORW : TWF_ANDW;
	/* 仕様「NOCLR の指定がない場合には、待ち解除条件が満たされたとき
	 * イベントフラグの値（全ビット）が 0 クリアされる」。
	 * 中心核の TWF_CLR がちょうどこれ（TWF_BITCLR は条件ビットだけ）。 */
	if ((mode & NOCLR) == 0) {
		wf |= TWF_CLR;
	}
	r = tk_wai_flg(o->kid, (UINT)ptn, wf, &got, t);
	if (r != E_OK) {
		return xlat(r);
	}
	/* 仕様「待ち状態が解除されたときのイベントフラグの値」を返す。
	 * ptn > 0 かつ解除条件が満たされているので必ず正になる。 */
	return (W)(got & 0x7FFFFFFFU);
}

/* ------------------------------------------------------------------ */
/* メッセージバッファ                                                  */
/* ------------------------------------------------------------------ */

EXPORT WERR cre_mbf(W bufsz, W max, UW opt)
{
	ID id;
	TObj *o;
	T_CMBF c;
	ID kid;
	W i;

	if ((opt & ~(UW)DELEXIT) != 0) {
		return ER_PAR;
	}
	/* 仕様「bufsz に 0 を指定することができる。この場合…送信側のタスクと
	 * 受信側のタスクが同時に送信 / 受信を行う（同期通信）」 */
	if (bufsz < 0 || max <= 0) {
		return ER_PAR;
	}
	id = alloc_slot(TOBJ_MBF, opt);
	if (id == 0) {
		return ER_LIMIT;
	}
	o = &tbl[id - 1];

	c.exinf = 0;
	c.mbfatr = TA_TFIFO;
	c.bufsz = bufsz;
	c.maxmsz = max;
	c.bufptr = 0;
	for (i = 0; i < 8; i++) {
		c.dsname[i] = 0;
	}
	kid = tk_cre_mbf(&c);
	if (kid < 1) {
		free_slot(o);
		return xlat(kid);
	}
	o->kid = kid;
	return id;
}

EXPORT ERR del_mbf(ID id)
{
	ER er = ER_OK;
	TObj *o = get(id, TOBJ_MBF, &er);
	ER r;

	if (o == 0) {
		return er;
	}
	r = tk_del_mbf(o->kid);
	free_slot(o);
	return xlat(r);
}

EXPORT ERR snd_mbf(ID id, VP msg, W sz, TMOUT tmo)
{
	ER er = ER_OK;
	TObj *o = get(id, TOBJ_MBF, &er);
	TMO t;

	if (o == 0) {
		return er;
	}
	if (msg == 0) {
		return ER_ADR;
	}
	if (sz <= 0) {
		return ER_PAR;
	}
	if (!to_tmo(tmo, &t)) {
		return ER_PAR;
	}
	return xlat(tk_snd_mbf(o->kid, msg, sz, t));
}

EXPORT WERR rcv_mbf(ID id, VP msg, TMOUT tmo)
{
	ER er = ER_OK;
	TObj *o = get(id, TOBJ_MBF, &er);
	TMO t;
	INT n;

	if (o == 0) {
		return er;
	}
	if (msg == 0) {
		return ER_ADR;
	}
	if (!to_tmo(tmo, &t)) {
		return ER_PAR;
	}
	n = tk_rcv_mbf(o->kid, msg, t);
	/* 仕様「＞0 正常(受信メッセージのバイト数)」 */
	return (n < 0) ? xlat((ER)n) : (W)n;
}

/* ------------------------------------------------------------------ */
/* 内部の口                                                            */
/* ------------------------------------------------------------------ */

EXPORT void tobj_release_process(W pid)
{
	W i;

	if (pid < 1) {
		return;
	}
	for (i = 0; i < TK_MAX_TOBJ; i++) {
		TObj *o = &tbl[i];

		if (!o->used || o->owner != pid) {
			continue;
		}
		switch (o->kind) {
		case TOBJ_SEM:
			(void)(o->excl ? tk_del_mtx(o->kid) : tk_del_sem(o->kid));
			break;
		case TOBJ_FLG:
			(void)tk_del_flg(o->kid);
			break;
		default:
			(void)tk_del_mbf(o->kid);
			break;
		}
		free_slot(o);
	}
}

EXPORT W tobj_count(void)
{
	W n = 0;
	W i;

	for (i = 0; i < TK_MAX_TOBJ; i++) {
		if (tbl[i].used) {
			n++;
		}
	}
	return n;
}

EXPORT ERR tobj_init(void)
{
	W i;

	for (i = 0; i < TK_MAX_TOBJ; i++) {
		tbl[i].used = False;
		tbl[i].owner = 0;
		tbl[i].kid = 0;
		tbl[i].excl = False;
	}
	return ER_OK;
}

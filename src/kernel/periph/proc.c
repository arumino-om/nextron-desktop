/*
 * proc.c — 周辺核 §1.1「プロセス / タスク管理」
 *
 * **周辺核である。中心核ではない。** 中心核 (μT-Kernel 3.0) のタスク管理の
 * 上に、BTRON3 のプロセスという層を載せる。返すのは `ER_*`（規律 R20）。
 *
 * ------------------------------------------------------------------------
 * 優先度の写像 (§1.1.3 → 中心核)
 * ------------------------------------------------------------------------
 *
 * BTRON3 のプロセス優先度 0〜255 は 3 つの群に分かれていて、群ごとに
 * スケジューリングの意味が違う。中心核は優先度 1..130 の
 * プリエンプティブ方式・同一優先度内 FCFS・時分割なしである。
 *
 *   BTRON3 優先度              中心核 優先度   群の中の規則
 *   群A 絶対優先度   0〜127  →  1〜128        厳密に優先度順
 *   群B RR1        128〜191  →  129           群内は全員同順位
 *   群C RR2        192〜255  →  130           群内は全員同順位
 *
 * **群A は 1 対 1 で写す。** 仕様が「厳密に優先度順にスケジューリングされ」
 * と規定しているので、128 段を圧縮すると優先度 0 と 3 が同じ段に落ちて
 * 規定が壊れる。そのために中心核の TK_MAX_TSKPRI を 130 にしてある。
 *
 * **群B / 群C はそれぞれ 1 段に潰す。** 仕様は群内について
 * 「優先度は相対的なスケジューリングの頻度を示す」「優先度が低いタスクでも
 * 必ず実行されることが保証される」と規定している。もし群B の 128 と 191 を
 * 別の中心核優先度に置くと、128 が実行可能な間 191 が全く走らなくなり、
 * この保証が壊れる。だから**同順位に置き、頻度の差はタイムスライスの
 * 長さで表す**（slice_ms）。群B が群C より上の段にあることで、仕様の
 * 「群A → 群B → 群C の順に探す」も自動的に成立する。
 *
 * ------------------------------------------------------------------------
 * ラウンドロビン
 * ------------------------------------------------------------------------
 *
 * 中心核は時分割をしない（仕様が「ラウンドロビンはアプリケーションが
 * システムコールで実現する」としている）。そこで**周辺核が時分割を足す**。
 * prc_tick() が 1 tick ごとにスライスを削り、使い切ったら
 * knl_yield_slice() で自タスクを同一優先度の末尾へ回す。これで
 *
 *   群A  同一優先度内のラウンドロビン（仕様の「平等にスケジューリング」）
 *   群B/C 群全体のラウンドロビン（全員が同順位なので群ごと回る）
 *
 * の両方が同じ仕掛けで出る。
 *
 * ------------------------------------------------------------------------
 * 中心核との境界
 * ------------------------------------------------------------------------
 *
 * 【タスクとプロセスの対応は中心核に持たせない】TCB にプロセス ID を
 * 足せば速いが、中心核はプロセスという概念を持たない層なので、ここで
 * タスク ID を添字にした表 (task_owner) を持つ。ランデブが TCB に
 * rdv_* を足しているのとは逆の判断で、こちらは中心核の層を汚さずに済む。
 *
 * 【Phase 3 まで cre_prc は使えない】実身の実行プログラムレコードを読むには
 * ファイルシステムが必要で、まだ接続されていない。カーネル内の関数を
 * メインタスクにする prc_create() が当面の代わり。
 */

#include <btron/proc.h>

#include "../core/knl.h"

/* 中心核の優先度段数が群A の 128 段 + 群B + 群C を受けられること。
 * kernel.h の TK_MAX_TSKPRI を下げるとここで落ちる。 */
_Static_assert(TK_MAX_TSKPRI == P_ABS_LAST + 3, "優先度の写像 (§1.1.3)");

/* ------------------------------------------------------------------ */
/* プロセス制御ブロック                                                */
/* ------------------------------------------------------------------ */

typedef struct PCB {
	W pid;
	W parpid; /* 0 なら親なし（初期プロセス、または親が終了済み） */
	W pri;    /* BTRON3 のプロセス優先度 0..255。メインタスクの優先度 */

	/* [0] がメインタスク。§1.1.2「プロセスの状態はメインタスクの状態」 */
	ID task[PRC_MAX_TASK];
	W ntask;

	AddrSpace as; /* §1.1.1「1 プロセス = 1 つの独立したアドレス空間」 */
	Bool has_as;

	PRCMAIN entry; /* 形式-A の MAIN() */
	MESSAGE *msg;  /* 起動時メッセージの複製。所有権はこの PCB */

	P_USER usr;
	TC name[PRC_NAMELEN + 1]; /* 実身名の代わり。TNULL 終端 */

	SYSTIM born; /* 生成時刻 (knl_otm)。etime の計算用 */
	UW utime;    /* 累計 CPU 時間 (ミリ秒) */
	UW stime;

	W exitcode;
	Bool used;
} PCB;

LOCAL PCB pcb[PRC_MAX_PRCID + 1]; /* 添字 = プロセス ID。0 は使わない */

/* タスク ID → 所属プロセス ID。0 なら「プロセスに属さないタスク」。
 * 中心核だけを検査するタスクがこれに当たる。 */
LOCAL W task_owner[TK_MAX_TSKID + 1];

/* タスク ID → BTRON3 優先度。
 *
 * 中心核の優先度からは戻せない。群B / 群C が 1 段に潰れているので、
 * 中心核の 129 を見ても 128〜191 のどれだったか分からない。
 * chg_pri の P_REL（相対指定）がこの値を必要とする。 */
LOCAL W task_pri[TK_MAX_TSKID + 1];

/* タスク ID → 残りタイムスライス (ミリ秒)。
 *
 * **プロセス単位ではなくタスク単位で持つ。** §1.1.3 のラウンドロビンは
 * 「同一優先度の**タスク**」の間の話で、1 プロセスが同一優先度の
 * サブタスクを複数持てる。プロセス単位にすると、それらが 1 本の
 * スライスを分け合うことになって意味が変わる。 */
LOCAL W task_slice[TK_MAX_TSKID + 1];

LOCAL W initial_pid;  /* 初期プロセス (§1.1.1) */
LOCAL W cur_space;    /* いま下位半分に入っているプロセス ID。0 なら空 */

/* ------------------------------------------------------------------ */
/* 優先度                                                              */
/* ------------------------------------------------------------------ */

EXPORT W prc_pri_group(W pri)
{
	if (pri < P_PRI_MIN || pri > P_PRI_MAX) {
		return -1;
	}
	if (pri <= P_ABS_LAST) {
		return P_GRP_ABS;
	}
	return (pri <= P_RR1_LAST) ? P_GRP_RR1 : P_GRP_RR2;
}

EXPORT PRI prc_to_tskpri(W pri)
{
	switch (prc_pri_group(pri)) {
	case P_GRP_ABS:
		return (PRI)(pri + 1); /* 0..127 → 1..128 */
	case P_GRP_RR1:
		return (PRI)(P_ABS_LAST + 2); /* 129 */
	case P_GRP_RR2:
		return (PRI)(P_ABS_LAST + 3); /* 130 */
	default:
		return 0;
	}
}

EXPORT W prc_from_tskpri(PRI tskpri)
{
	if (tskpri < 1 || tskpri > (PRI)(P_ABS_LAST + 1)) {
		return -1; /* 群B / 群C は潰れているので戻せない */
	}
	return (W)tskpri - 1;
}

/* タイムスライスの長さ (ミリ秒)。
 *
 * 群A は「同一優先度の場合はラウンドロビン方式で**平等に**」なので一定。
 * 群B / 群C は「優先度は**相対的なスケジューリングの頻度**を示す」なので
 * 優先度が高い（番号が小さい）ほど長くする。64 段を 8 段階に丸めて
 * 群内の幅を 8:1 に収める。段階にするのは、隣り合う優先度のわずかな差で
 * 挙動が変わるより、はっきり差がつく方が使う側に分かりやすいため。 */
LOCAL W slice_ms(W pri)
{
	switch (prc_pri_group(pri)) {
	case P_GRP_RR1:
		return PRC_SLICE_MS * (1 + (P_RR1_LAST - pri) / 8);
	case P_GRP_RR2:
		return PRC_SLICE_MS * (1 + (P_RR2_LAST - pri) / 8);
	default:
		return PRC_SLICE_MS;
	}
}

/* ------------------------------------------------------------------ */
/* 補助                                                                */
/* ------------------------------------------------------------------ */

LOCAL void tc_copy(TC *dst, W len, CONST TC *src)
{
	W i = 0;

	if (src != 0) {
		while (i < len && src[i] != TNULL) {
			dst[i] = src[i];
			i++;
		}
	}
	while (i < len) {
		dst[i] = TNULL; /* 短い名前は 0 で埋める（§1.1.5） */
		i++;
	}
}

/* ASCII の文字列を TC の名前欄に入れる。初期プロセスの既定値用。 */
LOCAL void tc_from_ascii(TC *dst, W len, CONST B *src)
{
	W i = 0;

	while (i < len && src[i] != 0) {
		dst[i] = (TC)(UB)src[i];
		i++;
	}
	while (i < len) {
		dst[i] = TNULL;
		i++;
	}
}

EXPORT W prc_self(void)
{
	TCB *t = knl_cur;

	if (t == 0 || t->tskid < 1 || t->tskid > TK_MAX_TSKID) {
		return 0;
	}
	return task_owner[t->tskid];
}

EXPORT W prc_initial(void)
{
	return initial_pid;
}

EXPORT W prc_count(void)
{
	W n = 0;
	W i;

	for (i = 1; i <= PRC_MAX_PRCID; i++) {
		if (pcb[i].used) {
			n++;
		}
	}
	return n;
}

EXPORT Bool prc_exists(W pid)
{
	if (pid < 1 || pid > PRC_MAX_PRCID) {
		return False;
	}
	return pcb[pid].used;
}

EXPORT ID prc_main_task(W pid)
{
	if (pid < 1 || pid > PRC_MAX_PRCID || !pcb[pid].used) {
		return 0;
	}
	return pcb[pid].task[0];
}

EXPORT W prc_task_owner(ID tskid)
{
	if (tskid < 1 || tskid > TK_MAX_TSKID) {
		return 0;
	}
	return task_owner[tskid];
}

EXPORT AddrSpace *prc_space(W pid)
{
	if (pid < 1 || pid > PRC_MAX_PRCID || !pcb[pid].used || !pcb[pid].has_as) {
		return 0;
	}
	return &pcb[pid].as;
}

/* pid の指定 (>0 任意 / 0 自プロセス / -1 親プロセス) を解く。
 * 解けなければ 0 を返し *er に理由を入れる。 */
LOCAL W resolve_pid(W pid, ER *er)
{
	W me = prc_self();

	if (me <= 0) {
		*er = ER_NOPRC; /* プロセスに属さないタスクからの相対指定 */
		return 0;
	}
	if (pid == P_SELF) {
		return me;
	}
	if (pid == P_PARENT) {
		pid = pcb[me].parpid;
		if (pid <= 0) {
			*er = ER_NOPRC; /* 親が無い（初期プロセス、または親が終了済み） */
			return 0;
		}
		return pid;
	}
	if (pid < 1 || pid > PRC_MAX_PRCID || !pcb[pid].used) {
		*er = ER_NOPRC;
		return 0;
	}
	return pid;
}

/* プロセスのタスク表の空き枠。無ければ -1。 */
LOCAL W free_slot(CONST PCB *p)
{
	W i;

	for (i = 0; i < PRC_MAX_TASK; i++) {
		if (p->task[i] < 1) {
			return i;
		}
	}
	return -1;
}

LOCAL void detach_task(PCB *p, ID tskid)
{
	W i;

	for (i = 0; i < PRC_MAX_TASK; i++) {
		if (p->task[i] == tskid) {
			p->task[i] = 0;
		}
	}
	task_owner[tskid] = 0;
	task_pri[tskid] = -1;
	task_slice[tskid] = 0;
	if (p->ntask > 0) {
		p->ntask--;
	}
}

/* ------------------------------------------------------------------ */
/* 中心核からの呼び出し口                                              */
/* ------------------------------------------------------------------ */

EXPORT void prc_task_switch(ID tskid)
{
	W pid;
	PCB *p;

	if (tskid < 1 || tskid > TK_MAX_TSKID) {
		return;
	}
	pid = task_owner[tskid];
	if (pid < 1) {
		return; /* プロセスに属さないタスク。下位半分はそのままでよい */
	}
	p = &pcb[pid];
	if (!p->has_as || cur_space == pid) {
		return; /* 同じプロセス内のタスク間なら差し替えない (TLB を守る) */
	}
	cur_space = pid;
	hal_vm_switch(&p->as);
}

EXPORT void prc_tick(void)
{
	W ms = (W)hal_timer_tick_ms();
	ID tid = (knl_cur != 0) ? knl_cur->tskid : 0;
	W pid;

	if (tid < 1 || tid > TK_MAX_TSKID) {
		return;
	}
	pid = task_owner[tid];
	if (pid < 1) {
		return; /* プロセスに属さないタスク。課金も時分割もしない */
	}
	/* utime は「プロセスで費した CPU 時間（OS 内での処理時間は除外する）」。
	 * いまは OS 内の時間を切り分けていないので全部ここへ入れる。
	 * 切り分けは特権境界 (EL0 / ring3) を作ってから。 */
	pcb[pid].utime += (UW)ms;
	task_slice[tid] -= ms;
	if (task_slice[tid] > 0) {
		return;
	}
	task_slice[tid] = slice_ms(task_pri[tid]);
	knl_yield_slice();
}

/* ------------------------------------------------------------------ */
/* 生成                                                                */
/* ------------------------------------------------------------------ */

/* 空いている**最小の**プロセス ID を割り当てる（§1.1.1）。 */
LOCAL PCB *alloc_pcb(void)
{
	W i;

	for (i = 1; i <= PRC_MAX_PRCID; i++) {
		if (!pcb[i].used) {
			return &pcb[i];
		}
	}
	return 0;
}

/* 起動時メッセージを PCB の持ち物として複製する。
 * msg = 0 なら msg_type = msg_size = 0 の空メッセージを作る。 */
LOCAL MESSAGE *dup_message(CONST MESSAGE *msg)
{
	W size = (msg != 0) ? msg->msg_size : 0;
	MESSAGE *d;
	W i;

	if (size < 0) {
		size = 0;
	}
	d = (MESSAGE *)kmalloc((UD)sizeof(MESSAGE) + (UD)size);
	if (d == 0) {
		return 0;
	}
	d->msg_type = (msg != 0) ? msg->msg_type : 0;
	d->msg_size = size;
	if (size > 0) {
		/* 本体は共用体として宣言されているが可変長としても使う（仕様の
		 * MSGSIZE / rcv_msg の msgsz がそれを前提にしている）。
		 * 共用体の中の配列の添字では 32 バイトを超えられないので、
		 * 先頭アドレスをバイト列として扱う。 */
		UB *dst = (UB *)(VP)&d->msg_body;
		CONST UB *src = (CONST UB *)(CONST VP)&msg->msg_body;

		for (i = 0; i < size; i++) {
			dst[i] = src[i];
		}
	}
	return d;
}

/* メインタスクの入口。中心核の TASKENTRY と形式-A の MAIN() を繋ぐ。 */
LOCAL void main_trampoline(INT stacd, VP exinf)
{
	PCB *p = (PCB *)exinf;
	W code;

	(void)stacd;
	code = p->entry(p->msg);
	/* 仕様: 「MAIN() および main() からのリターンはプロセスの終了となり、
	 * ext_prc() による終了と同等である」 */
	ext_prc(code);
}

/* サブタスクの入口。仕様の VOID subtask(W arg) へ繋ぐ。 */
LOCAL void sub_trampoline(INT stacd, VP exinf)
{
	SUBTASK f = (SUBTASK)exinf;

	f((W)stacd);
	/* 仕様: 「サブタスクを return で終了することはできない」。
	 * 戻ってきたのは誤りだが、落とすより畳んだ方が被害が小さい。 */
	ext_tsk();
}

EXPORT WERR prc_create(PRCMAIN entry, W pri, CONST TC *name, CONST MESSAGE *msg)
{
	W parent = prc_self();
	PCB *p;
	MESSAGE *m;
	T_CTSK c;
	ID tid;
	UD state;
	W i;

	if (entry == 0) {
		return ER_ADR;
	}
	if (pri == P_PRI_SELF) {
		if (parent < 1) {
			return ER_PPRI; /* 継承元が無い */
		}
		pri = pcb[parent].pri;
	}
	if (prc_pri_group(pri) < 0) {
		return ER_PPRI;
	}
	if (msg != 0 && (msg->msg_size < 0 || msg->msg_size > PRC_MAX_MSGSZ)) {
		return ER_SZOVR;
	}

	state = hal_int_disable();
	p = alloc_pcb();
	if (p != 0) {
		p->used = True; /* 先に押さえる。以降は割込みを許してよい */
	}
	hal_int_restore(state);
	if (p == 0) {
		return ER_NOSPC; /* 仕様: プロセス数が多すぎる */
	}

	for (i = 0; i < PRC_MAX_TASK; i++) {
		p->task[i] = 0;
	}
	p->pid = (W)(p - pcb);
	p->parpid = parent;
	p->pri = pri;
	p->ntask = 0;
	p->has_as = False;
	p->entry = entry;
	p->msg = 0;
	p->born = knl_otm;
	p->utime = 0;
	p->stime = 0;
	p->exitcode = 0;
	tc_copy(p->name, PRC_NAMELEN, name);
	p->name[PRC_NAMELEN] = TNULL;

	/* §1.1.4: ユーザ情報は生成時の親プロセスのものを継承する。
	 * 親が無い（最初のプロセス = 初期プロセス）ときは、仕様の
	 * 「初期プロセスのユーザ情報は、当初はシステムで定義された固定的な
	 * 内容を持つ」に従って既定値を入れる。 */
	if (parent >= 1) {
		p->usr = pcb[parent].usr;
	} else {
		tc_from_ascii(p->usr.usr_name, P_NAMELEN, "SYSTEM");
		tc_from_ascii(p->usr.grp_name1, P_NAMELEN, "SYSTEM");
		tc_copy(p->usr.grp_name2, P_NAMELEN, 0);
		tc_copy(p->usr.grp_name3, P_NAMELEN, 0);
		tc_copy(p->usr.grp_name4, P_NAMELEN, 0);
		p->usr.level = P_LEVEL_MAX; /* 0 = 最高。システムプロセス */
		p->usr.net_level = 1;
	}

	m = dup_message(msg);
	if (m == 0) {
		p->used = False;
		return ER_NOMEM;
	}
	p->msg = m;

	/* §1.1.1「1 つのプロセスは 1 つの独立したアドレス空間を持つ」。
	 * いまは下位半分に何も写像しない（ユーザプログラムを読む経路が
	 * まだ無い）が、空間そのものは作る。§1.5 のローカルメモリと
	 * 実行プログラムレコードの読み込みがここへ入る。 */
	if (hal_vm_create(&p->as) == 0) {
		p->has_as = True;
	}

	c.exinf = (VP)p;
	c.tskatr = TA_HLNG;
	c.task = main_trampoline;
	c.itskpri = prc_to_tskpri(pri);
	c.stksz = TK_MIN_STKSZ * 2;
	c.sstksz = 0;
	c.stkptr = 0;
	c.bufptr = 0;
	for (i = 0; i < 8; i++) {
		c.dsname[i] = 0;
	}
	tid = tk_cre_tsk(&c);
	if (tid < 1) {
		if (p->has_as) {
			hal_vm_destroy(&p->as);
			p->has_as = False;
		}
		kfree(p->msg);
		p->msg = 0;
		p->used = False;
		return (tid == E_LIMIT) ? ER_LIMIT : ER_NOMEM;
	}

	state = hal_int_disable();
	p->task[0] = tid;
	p->ntask = 1;
	task_owner[tid] = p->pid;
	task_pri[tid] = pri;
	task_slice[tid] = slice_ms(pri);
	if (initial_pid == 0) {
		initial_pid = p->pid;
	}
	hal_int_restore(state);

	/* 仕様: 「同時にメインタスクが生成され実行を開始する」 */
	if (tk_sta_tsk(tid, 0) != E_OK) {
		(void)tk_del_tsk(tid);
		detach_task(p, tid);
		if (p->has_as) {
			hal_vm_destroy(&p->as);
			p->has_as = False;
		}
		kfree(p->msg);
		p->msg = 0;
		p->used = False;
		return ER_INNER;
	}
	return p->pid;
}

EXPORT WERR cre_prc(CONST LINK *lnk, W pri, CONST MESSAGE *msg)
{
	if (lnk == 0) {
		return ER_ADR;
	}
	if (pri != P_PRI_SELF && prc_pri_group(pri) < 0) {
		return ER_PPRI;
	}
	if (msg != 0 && msg->msg_size > PRC_MAX_MSGSZ) {
		return ER_SZOVR;
	}
	/* 実身の実行プログラムレコードを読むにはファイルシステムが要る。
	 * まだ 1 つも接続されていないので、仕様の ER_NOFS がそのまま当たる。
	 * Phase 3 (T3-01 以降) でここが本物になる。 */
	return ER_NOFS;
}

EXPORT WERR cre_tsk(SUBTASK entry, W pri, W arg)
{
	W pid = prc_self();
	PCB *p;
	T_CTSK c;
	ID tid;
	UD state;
	W slot;
	W i;

	if (entry == 0) {
		return ER_ADR;
	}
	if (pid < 1) {
		return ER_NOPRC;
	}
	p = &pcb[pid];
	/* 仕様: 「サブタスクの優先度は、自プロセスの優先度グループの範囲内で
	 * のみ指定できる」 */
	if (prc_pri_group(pri) != prc_pri_group(p->pri)) {
		return ER_PPRI;
	}
	state = hal_int_disable();
	slot = free_slot(p);
	hal_int_restore(state);
	if (slot < 0) {
		return ER_LIMIT; /* 仕様: サブタスク数の制限を超えた */
	}

	c.exinf = (VP)entry;
	c.tskatr = TA_HLNG;
	c.task = sub_trampoline;
	c.itskpri = prc_to_tskpri(pri);
	c.stksz = TK_MIN_STKSZ * 2;
	c.sstksz = 0;
	c.stkptr = 0;
	c.bufptr = 0;
	for (i = 0; i < 8; i++) {
		c.dsname[i] = 0;
	}
	tid = tk_cre_tsk(&c);
	if (tid < 1) {
		return (tid == E_LIMIT) ? ER_LIMIT : ER_NOMEM;
	}

	state = hal_int_disable();
	p->task[slot] = tid;
	p->ntask++;
	task_owner[tid] = pid;
	task_pri[tid] = pri;
	task_slice[tid] = slice_ms(pri);
	hal_int_restore(state);

	if (tk_sta_tsk(tid, (INT)arg) != E_OK) {
		(void)tk_del_tsk(tid);
		detach_task(p, tid);
		return ER_INNER;
	}
	return tid;
}

/* ------------------------------------------------------------------ */
/* 終了                                                                */
/* ------------------------------------------------------------------ */

/* プロセスを畳む。
 *
 * self = True なら、呼び出している自タスクがこのプロセスに属している。
 * **その場合は戻らない。** 自タスクだけは最後に残し、休止状態にして
 * ID を中心核に預ける（自分のスタックの上で自分のスタックは返せない）。 */
LOCAL void teardown(W pid, W code, W msgtype, Bool self)
{
	PCB *p = &pcb[pid];
	ID me = (self != False) ? knl_cur->tskid : 0;
	W parent = p->parpid;
	W i;

	p->exitcode = code;

	/* §1.2.2: 終了メッセージを親へ送る。**子プロセスの終了コードは
	 * メッセージ管理を通って親に届く**（仕様が §1.2 でそう定めている）。
	 * 親の付け替えより先に送る — 自分の親が誰かはここでしか分からない。 */
	msg_notify_exit(parent, pid, code, msgtype);

	/* §1.1.1: 「プロセスが終了した場合、そのプロセスの子プロセスの親
	 * プロセスは、終了したプロセスの親プロセスとなる。ただし、初期
	 * プロセスが終了した場合、その子プロセスは親プロセスを持たない」。
	 * 初期プロセスの parpid は 0 なので、引き取り先が 0 = 親なしになる。 */
	for (i = 1; i <= PRC_MAX_PRCID; i++) {
		if (pcb[i].used && pcb[i].parpid == pid) {
			pcb[i].parpid = p->parpid;
		}
	}

	/* §1.1.2: 「メインタスクが終了した場合は、そのプロセス内のすべての
	 * サブタスクも強制終了され、プロセスも終了する」。畳むときは
	 * メインもサブも区別せず落とす。 */
	for (i = 0; i < PRC_MAX_TASK; i++) {
		ID t = p->task[i];

		if (t < 1 || t == me) {
			continue;
		}
		(void)tk_ter_tsk(t);
		(void)tk_del_tsk(t);
		p->task[i] = 0;
		task_owner[t] = 0;
		task_pri[t] = -1;
		task_slice[t] = 0;
	}

	if (p->msg != 0) {
		kfree(p->msg);
		p->msg = 0;
	}

	/* メッセージキュー・タイムアウト要求・ハンドラを捨てる (§1.2)。
	 * このプロセス宛に空き待ち・確認待ちをしている送信側は ER_NOPRC で
	 * 解ける（仕様「待っている間に送信先のプロセスが終了した場合は
	 * エラー終了する」）。 */
	msg_release_process(pid);

	/* DELEXIT 指定のグローバル名データを捨てる (§1.4.1)。 */
	gnm_release_process(pid);

	/* DELEXIT 指定の同期通信オブジェクトを捨てる (§1.3)。 */
	tobj_release_process(pid);

	/* §1.5 のメモリブロックを返す。**アドレス空間を壊す前に呼ぶ。**
	 * ローカルメモリの物理ページはプロセスのページテーブルを辿らないと
	 * 分からないので、表を解放した後では回収できない。 */
	mbk_release_process(pid);

	/* **アドレス空間を外してから破棄する。** 下位半分に入ったまま
	 * ページテーブルを返すと、MMU が解放済みのページを辿り続ける。 */
	if (p->has_as) {
		if (cur_space == pid) {
			hal_vm_drop_identity(); /* 下位半分を空の表に向け、TLB を流す */
			cur_space = 0;
		}
		hal_vm_destroy(&p->as);
		p->has_as = False;
	}
	p->ntask = 0;
	p->used = False;
	if (initial_pid == pid) {
		initial_pid = 0;
	}

	if (me >= 1) {
		task_owner[me] = 0;
		task_pri[me] = -1;
		task_slice[me] = 0;
		/* TCB とスタックは自分では返せない。ID を預けて休止する。 */
		knl_defer_del(me);
		tk_ext_tsk(); /* 戻らない */
	}
}

EXPORT void ext_prc(W code)
{
	W pid = prc_self();

	if (pid >= 1) {
		/* 仕様 §1.2.2: ext_prc による終了は MS_EXIT で親へ通知される。 */
		teardown(pid, code, MS_EXIT, True); /* 戻らない */
	}
	/* プロセスに属さないタスクが呼んだ。中心核のタスクとしてだけ畳む。 */
	tk_ext_tsk();
}

EXPORT ERR prc_abort(W pid, W code)
{
	W me = prc_self();

	if (pid < 1 || pid > PRC_MAX_PRCID || !pcb[pid].used) {
		return ER_NOPRC;
	}
	/* §1.2.2 の MS_ABORT。「プロセスがシステムエラーにより異常終了させられた
	 * 場合」と、メッセージハンドラ MH_TERM による終了がこれに当たる。 */
	if (pid == me) {
		teardown(pid, code, MS_ABORT, True); /* 戻らない */
	}
	teardown(pid, code, MS_ABORT, False);
	return ER_OK;
}

/* pid が anc の子孫か。anc 自身は含まない。 */
LOCAL Bool is_descendant(W pid, W anc)
{
	W n = 0;

	while (pid >= 1 && pid <= PRC_MAX_PRCID && n <= PRC_MAX_PRCID) {
		pid = pcb[pid].parpid;
		if (pid == anc) {
			return True;
		}
		n++; /* 木が壊れて輪になっていても止まるように数える */
	}
	return False;
}

EXPORT ERR ter_prc(W pid, W code, W opt)
{
	W me = prc_self();
	W target;
	Bool kill_self = False;
	W i;

	if (opt != TERM_NRM && opt != TERM_ALL) {
		return ER_PAR;
	}
	if (me < 1) {
		return ER_NOPRC;
	}
	if (pid == P_SELF) {
		return ER_SELF; /* 仕様: 自プロセスは指定不可 */
	}
	target = (pid == P_PARENT) ? pcb[me].parpid : pid;
	if (target < 1 || target > PRC_MAX_PRCID || !pcb[target].used) {
		return ER_NOPRC;
	}
	if (target == me) {
		return ER_SELF;
	}
	/* 仕様: 「自プロセスより高いユーザレベルのプロセスは強制終了できない」。
	 * ユーザレベルは **0 が最高**なので、番号が小さい方が高い。 */
	if (pcb[target].usr.level < pcb[me].usr.level) {
		return ER_LEVEL;
	}

	if (opt == TERM_NRM) {
		/* 仕様 §1.2.2: ter_prc による強制終了は MS_TERM で親へ通知される。 */
		teardown(target, code, MS_TERM, False);
		return ER_OK;
	}

	/* TERM_ALL。子孫も落とす。
	 *
	 * target を最後に畳むのが要点。先に target を畳むと、まだ残っている
	 * 子孫が target の親に引き取られてしまい、子孫だと分からなくなる。
	 *
	 * 仕様: 「親プロセスまたはさらにその親のプロセスを指定した場合には、
	 * 自プロセスも強制終了させられる」。自分は最後に畳む。 */
	for (i = 1; i <= PRC_MAX_PRCID; i++) {
		if (!pcb[i].used || !is_descendant(i, target)) {
			continue;
		}
		if (i == me) {
			kill_self = True;
			continue;
		}
		teardown(i, code, MS_TERM, False);
	}
	teardown(target, code, MS_TERM, False);
	if (kill_self) {
		teardown(me, code, MS_TERM, True); /* 戻らない */
	}
	return ER_OK;
}

EXPORT void ext_tsk(void)
{
	W pid = prc_self();
	ID me = knl_cur->tskid;

	if (pid >= 1) {
		PCB *p = &pcb[pid];

		/* 仕様: 「メインタスクが終了した場合はプロセスも終了する」 */
		if (p->task[0] == me) {
			teardown(pid, p->exitcode, MS_EXIT, True); /* 戻らない */
		}
		detach_task(p, me);
		knl_defer_del(me);
	}
	tk_ext_tsk();
}

EXPORT ERR ter_tsk(W tskid)
{
	W pid = prc_self();
	PCB *p;
	ID me;

	if (pid < 1) {
		return ER_NOPRC;
	}
	p = &pcb[pid];
	me = knl_cur->tskid;
	/* 仕様: 「指定できるタスクは、自プロセス内のサブタスクのみである。
	 * 自タスクおよびメインタスクを指定することはできない」 */
	if (tskid < 1 || tskid > TK_MAX_TSKID || task_owner[tskid] != pid) {
		return ER_ID;
	}
	if (tskid == me || tskid == p->task[0]) {
		return ER_ID;
	}
	(void)tk_ter_tsk((ID)tskid);
	(void)tk_del_tsk((ID)tskid);
	detach_task(p, (ID)tskid);
	return ER_OK;
}

/* ------------------------------------------------------------------ */
/* 優先度変更                                                          */
/* ------------------------------------------------------------------ */

EXPORT WERR chg_pri(W id, W pri, W opt)
{
	W me = prc_self();
	W grp;
	PCB *p;
	ER er = ER_OK;
	W pid;
	W i;

	if ((opt & ~(W)(P_REL | P_TASK)) != 0) {
		return ER_PAR;
	}
	if (me < 1) {
		return ER_NOPRC;
	}

	if ((opt & P_TASK) != 0) {
		/* 仕様: 「指定できるタスクは、自プロセス内のタスクのみである」 */
		ID tid = (id == P_SELF) ? knl_cur->tskid : (ID)id;
		W newpri;

		if (id < 0 || tid < 1 || tid > TK_MAX_TSKID ||
		    task_owner[tid] != me) {
			return ER_ID;
		}
		p = &pcb[me];
		newpri = ((opt & P_REL) != 0) ? task_pri[tid] + pri : pri;
		/* 仕様: 「指定できる優先度の範囲は、対象タスクが属するプロセスの
		 * 現在の優先度グループ内となる」 */
		if (prc_pri_group(newpri) != prc_pri_group(p->pri)) {
			return ER_PPRI;
		}
		if (tk_chg_pri(tid, prc_to_tskpri(newpri)) != E_OK) {
			return ER_INNER;
		}
		task_pri[tid] = newpri;
		task_slice[tid] = slice_ms(newpri);
		if (tid == p->task[0]) {
			p->pri = newpri; /* プロセスの優先度はメインタスクの優先度 */
		}
		return newpri;
	}

	pid = resolve_pid(id, &er);
	if (pid < 1) {
		return er;
	}
	p = &pcb[pid];
	grp = prc_pri_group(p->pri);

	/* 仕様: 「id で指定したプロセス ID のプロセス内の**全タスク**の
	 * 優先度を変更する」。
	 *
	 * P_REL（相対指定）はタスクごとに現在値へ加える。こうするとサブタスクと
	 * メインタスクの優先度差が保たれ、戻り値が「メインタスクの優先度」と
	 * 規定されていることの意味も通る（全部同じ値にするなら戻り値は
	 * 指定値そのものになり、わざわざメインタスクと書く理由が無い）。
	 *
	 * **検査を先に全部通す。** 途中で失敗して一部だけ変わった状態にすると、
	 * 呼び出し側から復元できない。 */
	for (i = 0; i < PRC_MAX_TASK; i++) {
		ID t = p->task[i];
		W newpri;

		if (t < 1) {
			continue;
		}
		newpri = ((opt & P_REL) != 0) ? task_pri[t] + pri : pri;
		if (prc_pri_group(newpri) != grp) {
			return ER_PPRI;
		}
	}
	for (i = 0; i < PRC_MAX_TASK; i++) {
		ID t = p->task[i];
		W newpri;

		if (t < 1) {
			continue;
		}
		newpri = ((opt & P_REL) != 0) ? task_pri[t] + pri : pri;
		if (tk_chg_pri(t, prc_to_tskpri(newpri)) != E_OK) {
			return ER_INNER;
		}
		task_pri[t] = newpri;
		task_slice[t] = slice_ms(newpri);
	}
	p->pri = task_pri[p->task[0]];
	return p->pri;
}

/* ------------------------------------------------------------------ */
/* 状態と情報の取得                                                    */
/* ------------------------------------------------------------------ */

/* §1.1.2「プロセスの状態は、そのメインタスクの状態となる」 */
LOCAL UW state_of(CONST PCB *p)
{
	ID t = p->task[0];
	UINT s;

	if (t < 1 || t > TK_MAX_TSKID) {
		return P_WAIT;
	}
	s = knl_tcb[t].state;
	if (s == TTS_RUN) {
		return P_RUN;
	}
	if (s == TTS_RDY) {
		return P_READY;
	}
	return P_WAIT; /* 待ち・強制待ち・休止はすべて待ち状態として見せる */
}

EXPORT WERR prc_sts(W pid, P_STATE *buff, TC *name)
{
	ER er = ER_OK;
	CONST PCB *p;
	W target = resolve_pid(pid, &er);
	W i;

	if (target < 1) {
		return er;
	}
	p = &pcb[target];
	if (buff != 0) {
		buff->state = state_of(p);
		buff->priority = p->pri;
		buff->parpid = p->parpid;
	}
	if (name != 0) {
		for (i = 0; i <= PRC_NAMELEN; i++) {
			name[i] = p->name[i];
		}
	}
	return target;
}

EXPORT ERR chg_usr(CONST P_USER *buff)
{
	W me = prc_self();

	if (buff == 0) {
		return ER_ADR;
	}
	if (me < 1) {
		return ER_NOPRC;
	}
	/* 仕様: 「本システムコールはシステムプロセスでのみ実行可能で、
	 * 一般のアプリケーションプロセスでは実行できない」。
	 *
	 * **仕様はこの場合のエラーコードを挙げていない。** システムプロセスの
	 * 判定基準も書かれていないので、ユーザレベル 0（最高）をシステム
	 * プロセスとし、満たさない場合は ER_LEVEL を返すことにする。
	 * ter_prc の ER_LEVEL と同じ意味（権限が足りない）なので整合する。 */
	if (pcb[me].usr.level != P_LEVEL_MAX) {
		return ER_LEVEL;
	}
	if (buff->level < P_LEVEL_MAX || buff->level > P_LEVEL_MIN) {
		return ER_PAR;
	}
	if (buff->net_level < 1 || buff->net_level > P_LEVEL_MIN) {
		return ER_PAR;
	}
	/* 仕様: 「変更した内容は、それ以後生成した子プロセスに継承される」。
	 * 継承は prc_create() が親の usr を複製することで成立する。 */
	pcb[me].usr = *buff;
	return ER_OK;
}

EXPORT WERR get_usr(W pid, P_USER *buff)
{
	ER er = ER_OK;
	W target = resolve_pid(pid, &er);
	W i;

	if (target < 1) {
		return er;
	}
	if (buff != 0) {
		*buff = pcb[target].usr;
		/* 仕様: 「隠し名を取り出すことはできず、隠し名の部分には常に 0 が
		 * 得られる」。12 文字目以降が隠し名。 */
		for (i = P_VISLEN; i < P_NAMELEN; i++) {
			buff->usr_name[i] = TNULL;
			buff->grp_name1[i] = TNULL;
			buff->grp_name2[i] = TNULL;
			buff->grp_name3[i] = TNULL;
			buff->grp_name4[i] = TNULL;
		}
	}
	return target;
}

EXPORT ERR get_inf(W pid, P_INFO *buff)
{
	ER er = ER_OK;
	CONST PCB *p;
	W target = resolve_pid(pid, &er);
	W mem = 0;
	W i;

	if (target < 1) {
		return er;
	}
	if (buff == 0) {
		return ER_ADR;
	}
	p = &pcb[target];
	for (i = 0; i < 11; i++) {
		buff->resv[i] = 0;
	}
	buff->etime = (UW)((knl_otm - p->born) / 1000); /* ミリ秒 → 秒 */
	buff->utime = p->utime;
	buff->stime = p->stime;

	/* いま数えられるのはタスクのスタックと起動メッセージだけ。
	 * 実行プログラムレコードとローカルメモリ (§1.5 / T2-12) が入ると
	 * ここが本来の「実行に必要とするメモリサイズ」になる。 */
	for (i = 0; i < PRC_MAX_TASK; i++) {
		ID t = p->task[i];

		if (t >= 1 && t <= TK_MAX_TSKID) {
			mem += (W)knl_tcb[t].stksz;
		}
	}
	if (p->msg != 0) {
		mem += (W)sizeof(MESSAGE) + p->msg->msg_size;
	}
	buff->tmem = mem;
	buff->wmem = mem;
	return ER_OK;
}

/* ------------------------------------------------------------------ */
/* タスク付属同期 (§1.1.9)                                             */
/*                                                                     */
/* 中心核の tk_* を包むが、**エラーコードの族が違う**（規律 R20）ので    */
/* 境界で必ず変換する。中心核の E_TMOUT をそのまま返してはならない。     */
/* ------------------------------------------------------------------ */

/* 中心核の待ち結果を周辺核の族に直す。 */
LOCAL ER wait_result(ER er)
{
	switch (er) {
	case E_OK:
		return ER_OK;
	case E_TMOUT:
		/* 仕様: 「タイムアウト時間が経過したが、起床されなかった」 */
		return ER_NONE;
	case E_RLWAI:
		/* 仕様が挙げるのは ER_MINTR「メッセージハンドラが起動されたため、
		 * 待ちが中断された」だけ。メッセージ管理 (§1.2) はまだ無いが、
		 * 「外から待ちを壊された」という意味はこれに当たる。 */
		return ER_MINTR;
	default:
		return ER_INNER; /* 中心核の都合で失敗した。族を跨がせない */
	}
}

EXPORT ERR slp_tsk(W time)
{
	/* 仕様: time > 0 で指定時間 (ミリ秒) 待つ、-1 で無限に待つ。
	 * 0 は規定が無いので ER_PAR。 */
	if (time == 0 || time < T_FOREVER) {
		return ER_PAR;
	}
	return wait_result(tk_slp_tsk((TMO)time));
}

EXPORT ERR wup_tsk(W tskid)
{
	W me = prc_self();

	if (me < 1) {
		return ER_NOPRC;
	}
	/* 仕様: 「指定するタスクは自プロセス内のタスクでなくてはいけない。
	 * 他プロセスのタスクを起床することはできない。また、自タスクを
	 * 指定することはできない」 */
	if (tskid < 1 || tskid > TK_MAX_TSKID || task_owner[tskid] != me) {
		return ER_ID;
	}
	if (tskid == knl_cur->tskid) {
		return ER_ID;
	}
	switch (tk_wup_tsk((ID)tskid)) {
	case E_OK:
		return ER_OK;
	case E_QOVR:
		return ER_LIMIT; /* 起床要求のキューイング数が制限を超えた */
	default:
		return ER_ID;
	}
}

EXPORT WERR can_wup(W tskid)
{
	W me = prc_self();
	ID tid;
	INT n;

	if (me < 1) {
		return ER_NOPRC;
	}
	tid = (tskid == P_SELF) ? knl_cur->tskid : (ID)tskid;
	if (tskid < 0 || tid < 1 || tid > TK_MAX_TSKID || task_owner[tid] != me) {
		return ER_ID;
	}
	n = tk_can_wup(tid);
	return (n < 0) ? ER_ID : (W)n;
}

EXPORT ERR dly_tsk(W time)
{
	if (time < 0) {
		return ER_PAR;
	}
	if (time == 0) {
		/* 仕様: 「time = 0 を指定した場合は、実行を中断して再スケジュールを
		 * 行うことを意味する。つまり、タスクの状態を実行状態から、実行可能
		 * 状態へ移行させることになる」。
		 *
		 * 中心核の tk_dly_tsk(0) は何もせずに戻る（μT-Kernel の規定）。
		 * 譲らせるには同一優先度のレディキューを回す方を呼ぶ。 */
		return (tk_rot_rdq(0) == E_OK) ? ER_OK : ER_INNER;
	}
	return wait_result(tk_dly_tsk((RELTIM)time));
}

EXPORT W get_tid(void)
{
	return (W)tk_get_tid();
}

/* ------------------------------------------------------------------ */
/* 初期化                                                              */
/* ------------------------------------------------------------------ */

EXPORT ERR prc_init(void)
{
	W i;

	for (i = 0; i <= PRC_MAX_PRCID; i++) {
		pcb[i].used = False;
		pcb[i].pid = i;
		pcb[i].parpid = 0;
		pcb[i].ntask = 0;
		pcb[i].has_as = False;
		pcb[i].msg = 0;
	}
	for (i = 0; i <= TK_MAX_TSKID; i++) {
		task_owner[i] = 0;
		task_pri[i] = -1;
		task_slice[i] = 0;
	}
	initial_pid = 0;
	cur_space = 0;
	return ER_OK;
}

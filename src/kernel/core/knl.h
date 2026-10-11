/*
 * knl.h — 中心核の内部定義（カーネル内部。公開しない）
 *
 * 公開 API は include/btron/kernel.h。こちらは TCB と待ち行列の実装詳細。
 *
 * 【割込みの規律】状態を触る区間はすべて割込み禁止で囲む。
 * `knl_dispatch()` は**割込み禁止で呼び、割込み禁止で戻る**。
 * 呼び出し元が `hal_int_restore()` で元に戻す。タスクが切り替わっても、
 * それぞれのタスクが自分の退避点で同じ約束を守っているので成立する。
 *
 * 【SMP】いまは単一 CPU 前提。詳細 D3 の半分割スケジューリングは T1-10 で、
 * そのときここが per-CPU 構造に分かれる。
 */

#ifndef __BTRON_KERNEL_KNL_H__
#define __BTRON_KERNEL_KNL_H__

#include <btron/hal.h>
#include <btron/clock.h>
#include <btron/gname.h>
#include <btron/intc.h>
#include <btron/kernel.h>
#include <btron/memory.h>
#include <btron/message.h>
#include <btron/proc.h>
#include <btron/taskcomm.h>
#include <btron/vmem.h>

typedef struct TCB TCB;
typedef struct MTXCB MTXCB; /* 実体は core/mtx.c */

/* 双方向リスト。レディキューと待ち行列の両方に使う。 */
typedef struct Queue {
	TCB *head;
	TCB *tail;
	ATR order; /* TA_TFIFO か TA_TPRI。待ち行列の並び方 */
} Queue;

struct TCB {
	TCB *next;
	TCB *prev;
	Queue *queue; /* いま並んでいる行列。0 なら並んでいない */

	ID tskid;
	VP exinf;
	ATR tskatr;
	TASKENTRY entry;
	PRI itskpri; /* 起動時優先度 */
	PRI bpri;    /* ベース優先度 */
	PRI pri;     /* 現在優先度。ミューテックスが無い間は bpri と一致 */
	UINT state;  /* TTS_* */

	UW waitcause; /* TTW_*。待っていなければ 0 */
	ID wid;       /* 待ちオブジェクト ID */
	ER waitresult; /* 待ち解除の結果。起こした側が入れる */
	INT wupcnt;   /* 起床要求キューイング数 */
	INT suscnt;   /* 強制待ち要求ネスト数 */

	/* 待ち条件。待ち要因ごとに使う欄が違う。 */
	INT wait_cnt;      /* セマフォ: 要求資源数 */
	UINT wait_ptn;     /* イベントフラグ: 待ちビットパターン */
	UINT wait_mode;    /* イベントフラグ: TWF_* */
	UINT *wait_flgptn; /* イベントフラグ: 結果の格納先 */

	/* ランデブ (BTRON3 §1.3)。呼出側と受付側で意味が対応している。 */
	UW rdv_ptn;   /* calptn / acpptn */
	VP rdv_msg;   /* メッセージバッファ。呼出側は入出力兼用 */
	W rdv_size;   /* 呼出メッセージのバイト数 */
	W *rdv_no;    /* acp_por の rdv 格納先 */
	W rdv_result; /* 成立・返答で得たバイト数 */

	/* タイムアウトの絶対時刻 (起動からのミリ秒)。0 なら無期限。 */
	SYSTIM timeout;

	UD sp;    /* 退避した SP */
	UD stack; /* スタック領域の下端（仮想アドレス） */
	SZ stksz;

	/* このタスクがロックしているミューテックスの連鎖。
	 * 簡略化した優先度制御規則で「ロックしているものが無くなったか」を
	 * 判定するのに使う。タスク終了時の自動解除にも使う。 */
	MTXCB *mtxlist;

	UB dsname[8];
	Bool used; /* この TCB が生成済みか */
};

/* ------------------------------------------------------------------ */
/* 大域状態                                                            */
/* ------------------------------------------------------------------ */

IMPORT TCB *knl_cur;        /* 実行状態のタスク。常に非 0（アイドルを含む） */
IMPORT TCB knl_idle;        /* アイドルタスク。レディキューには入らない */
IMPORT TCB knl_tcb[TK_MAX_TSKID + 1]; /* 添字 = タスク ID。0 は使わない */
IMPORT Bool knl_on;         /* 中心核が動いているか */
IMPORT W knl_dsp_disabled;  /* ディスパッチ禁止のネスト数 */
IMPORT SYSTIM knl_otm;      /* 起動からの経過ミリ秒 */
IMPORT SYSTIM knl_systim_offset; /* tk_set_tim で設定された分 */

/* ------------------------------------------------------------------ */
/* 待ち行列                                                            */
/* ------------------------------------------------------------------ */

/* 待ち行列の並び。**生の属性をそのまま渡してはならない。**
 *
 * 同じビットの意味がオブジェクトによって違う。例えば 0x02 は
 * セマフォでは TA_CNT、メールボックスでは TA_MPRI、ミューテックスでは
 * TA_INHERIT（並びは優先度順）である。生成時にここで正規化して渡す。 */
#define KNL_ORDER(is_pri) ((is_pri) ? TA_TPRI : TA_TFIFO)

IMPORT void knl_queue_init(Queue *q, ATR order);
IMPORT void knl_queue_insert(Queue *q, TCB *t);
IMPORT void knl_queue_insert_tail(Queue *q, TCB *t);
IMPORT void knl_queue_remove(TCB *t);
IMPORT Bool knl_queue_empty(CONST Queue *q);

/* ------------------------------------------------------------------ */
/* スケジューラ                                                        */
/* ------------------------------------------------------------------ */

IMPORT void knl_sched_init(void);

/* 実行可能状態にする。
 *
 * 仕様の細則（忘れやすい）:
 *   head = True   プリエンプトされて実行可能に戻る場合。同一優先度で最高
 *   head = False  待ち解除・強制待ち解除・起動の場合。同一優先度で最低 */
IMPORT void knl_make_ready(TCB *t, Bool head);
IMPORT void knl_unready(TCB *t);
IMPORT TCB *knl_highest(void);

/* タスクの**現在優先度**を変える。ベース優先度は触らない。
 *
 * 実行可能ならレディキューを移し、優先度順の待ち行列にいるなら並び直す。
 * ディスパッチはしないので、呼び出し側が適切な場所で knl_dispatch() する。
 * ミューテックスの優先度継承と tk_chg_pri が共用する。 */
IMPORT void knl_set_pri(TCB *t, PRI newpri);

/* 割込み禁止で呼び、割込み禁止で戻る。必要ならタスクを切り替える。 */
IMPORT void knl_dispatch(void);

/* タイムスライスを使い切ったので同一優先度の末尾へ回る。
 * **切替はしない。** 周辺核の prc_tick() から呼ぶ（詳細は sched.c）。 */
IMPORT void knl_yield_slice(void);

/* ------------------------------------------------------------------ */
/* 自タスクの後片付け                                                  */
/*                                                                     */
/* **自分のスタックの上で自分のスタックは返せない。** tk_exd_tsk 相当の     */
/* 「終了して TCB ごと消える」を安全に行うため、ID をいったん預けて       */
/* 休止状態になり、次に別のタスクへ切り替わった後で削除する。            */
/* ------------------------------------------------------------------ */

IMPORT void knl_defer_del(ID tskid);

/* 預かったタスクを削除する。knl_dispatch() の入口から呼ばれる。 */
IMPORT void knl_reclaim(void);

/* ------------------------------------------------------------------ */
/* 待ちと解除                                                          */
/* ------------------------------------------------------------------ */

/* 自タスクを待ち状態にする。q が 0 なら行列に並ばない（起床待ち・遅延）。
 * 戻り値は待ち解除の結果。割込み禁止で呼ぶこと。 */
IMPORT ER knl_wait(Queue *q, UW cause, ID wid, TMO tmout);

/* 待ち状態のタスクを解除する。二重待ちなら強制待ちへ落とす。 */
IMPORT void knl_wait_release(TCB *t, ER result);

/* タイムアウトの確認。tick ごとに呼ぶ。 */
IMPORT void knl_timeout_check(void);

/* ------------------------------------------------------------------ */
/* 補助                                                                */
/* ------------------------------------------------------------------ */

/* tskid の検査。TSK_SELF なら自タスク。不正なら 0 を返し *er に理由を入れる。 */
IMPORT TCB *knl_get_tcb(ID tskid, ER *er);

/* ページを確保して直接マップ上の仮想アドレスを返す。失敗で 0。
 *
 * 物理ページアロケータが立っていればそちらから取り、**解放できる**。
 * 立っていない（メモリマップが無い直接起動）ときだけ、起動初期の
 * バンプアロケータから取る。その場合 knl_free_pages() は何もしない。 */
IMPORT UD knl_alloc_pages(SZ size);
IMPORT void knl_free_pages(UD va, SZ size);

/* スタックを確保する。戻り値はスタックの**上端**（仮想アドレス）。失敗で 0。 */
IMPORT UD knl_alloc_stack(SZ size);

/* タイマ割込みのハンドラ。*/
IMPORT void knl_timer_handler(UW intid, VP arg);

/* 待ち行列から、選択条件が噛み合う最初のタスクを探す。
 * 条件は (t->rdv_ptn & ptn) != 0（BTRON3 §1.3 のランデブ成立条件）。 */
IMPORT TCB *knl_find_match(Queue *q, UW ptn);

IMPORT void knl_rdv_init(void);
IMPORT void knl_mtx_init(void);
IMPORT void knl_mbx_init(void);
IMPORT void knl_mbf_init(void);
IMPORT void knl_mp_init(void);
IMPORT void knl_cyc_init(void);

/* タスクが終了するときに、そのタスクがロックしているミューテックスを
 * すべて解除する（仕様が要求している）。task.c から呼ぶ。 */
IMPORT void knl_mtx_release_all(TCB *t);

/* TA_CEILING のミューテックスに関わっているタスクのベース優先度を
 * newpri に変えてよいか。駄目なら True（tk_chg_pri が E_ILUSE を返す）。 */
IMPORT Bool knl_mtx_ceiling_violation(CONST TCB *t, PRI newpri);

/* 周期ハンドラ・アラームハンドラの時刻を進める。tick ごとに呼ぶ。 */
IMPORT void knl_cyc_tick(void);

#endif /* __BTRON_KERNEL_KNL_H__ */

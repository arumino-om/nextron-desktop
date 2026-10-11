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
#include <btron/intc.h>
#include <btron/kernel.h>
#include <btron/vmem.h>

typedef struct TCB TCB;

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

/* 割込み禁止で呼び、割込み禁止で戻る。必要ならタスクを切り替える。 */
IMPORT void knl_dispatch(void);

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

/* スタックを確保する。戻り値はスタックの上端（仮想アドレス）。失敗で 0。 */
IMPORT UD knl_alloc_stack(SZ size);

/* タイマ割込みのハンドラ。*/
IMPORT void knl_timer_handler(UW intid, VP arg);

/* 待ち行列から、選択条件が噛み合う最初のタスクを探す。
 * 条件は (t->rdv_ptn & ptn) != 0（BTRON3 §1.3 のランデブ成立条件）。 */
IMPORT TCB *knl_find_match(Queue *q, UW ptn);

IMPORT void knl_rdv_init(void);

#endif /* __BTRON_KERNEL_KNL_H__ */

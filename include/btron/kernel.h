/*
 * kernel.h — 中心核 (μT-Kernel 3.0 準拠) の API
 *
 * docs/design-issues.md D10: 中心核の基準仕様は μT-Kernel 3.0 (IEEE 2050-2018)。
 * ランデブは μT-Kernel 3.0 の仕様には無いので **BTRON3 §1.3** の規定を使う。
 *
 * 【規律 R15】T-License 2.2 のコード（μT-Kernel 3.0 の実装）を取り込んでは
 * ならない。ヘッダも含む。**仕様書を読んで独自実装している。**
 * 本ファイルは仕様書に書かれたインタフェース（関数名・引数・定数値）を
 * 自分で書き起こしたものである。
 *
 * 【スケジューリング】優先度ベースのプリエンプティブ方式。同一優先度内は
 * FCFS で、**時分割はしない**（仕様: 優先順位の高いタスクが実行できる状態に
 * ある限り、それより低いタスクは全く実行されない）。ラウンドロビンは
 * アプリが `tk_rot_rdq` を呼んで実現する。
 *
 * 詳細 D3 の「絶対優先度群 / RR 群」は**周辺核のプロセススケジューリング**の
 * 話で、この層ではない。周辺核がプロセス優先度をここへ写像する。
 *
 * 【優先順位の細則】仕様が明示している、忘れやすい規則:
 *
 *   実行状態 → 実行可能状態（プリエンプト）  同一優先度で**最高**の優先順位
 *   実行状態 → 待ち状態 → 待ち解除            同一優先度で**最低**の優先順位
 *   強制待ち状態 → 実行できる状態             同一優先度で**最低**の優先順位
 *
 * つまりプリエンプトは待ち行列の先頭に戻し、待ち解除は末尾に付ける。
 */

#ifndef __BTRON_KERNEL_H__
#define __BTRON_KERNEL_H__

#include <btron/errcode.h>
#include <btron/typedef.h>

/* ------------------------------------------------------------------ */
/* データ型 (μT-Kernel 共通規定)                                       */
/*                                                                     */
/* ID / FP / VP は BTRON3 の typedef.h に既にあるのでそれを使う。        */
/* INT / UINT は「プロセッサのビット幅」だが、A1 で W = 32bit と決めた   */
/* ので 32bit にする。64bit にすると公開 ABI が仕様とずれる。           */
/* ------------------------------------------------------------------ */

typedef W INT;   /* 符号付き整数 */
typedef UW UINT; /* 符号無し整数 */
typedef W PRI;   /* タスク優先度。1 以上。小さい方が高い */
typedef W SZ;    /* サイズ一般 */
typedef UW ATR;  /* オブジェクト属性 */
typedef W TMO;   /* タイムアウト値 (ミリ秒) */
typedef UW RELTIM;   /* 相対時間 (ミリ秒) */
typedef UD RELTIM_U; /* 相対時間 (マイクロ秒) */

/* システム時刻。
 *
 * 仕様の T_SYSTIM は {W hi; UW lo} の構造体だが、A5 で「64bit 化する」と
 * 決めたので単一の 64bit 整数にする。μT-Kernel 3.0 も 64bit 環境では
 * `typedef D SYSTIM` を認めている。単位はミリ秒。 */
typedef D SYSTIM;
typedef D SYSTIM_U; /* マイクロ秒単位 */

/* タスク起動アドレス。
 * 仕様の T_CTSK.task の型は汎用の FP だが、実際の signature は固定されている。 */
typedef void (*TASKENTRY)(INT stacd, VP exinf);

/* ------------------------------------------------------------------ */
/* 定数                                                                */
/* ------------------------------------------------------------------ */

#define TA_NULL 0U

/* 共通属性 */
#define TA_ASM 0x00000000U      /* アセンブリ言語（未サポート → E_NOSPT） */
#define TA_HLNG 0x00000001U     /* 高級言語 */
#define TA_SSTKSZ 0x00000002U   /* システムスタックサイズを指定 */
#define TA_USERSTACK 0x00000004U /* ユーザスタックポインタを指定 */
#define TA_USERBUF 0x00000020U  /* バッファをユーザが指定 */
#define TA_DSNAME 0x00000040U   /* DS オブジェクト名称を指定 */
#define TA_NODISWAI 0x00000080U /* 待ち禁止拒否 */
#define TA_RNG0 0x00000000U     /* 保護レベル 0 */
#define TA_RNG1 0x00000100U
#define TA_RNG2 0x00000200U
#define TA_RNG3 0x00000300U
#define TA_FPU 0x00001000U

/* 待ち行列の並び */
#define TA_TFIFO 0x00000000U /* FIFO */
#define TA_TPRI 0x00000001U  /* タスク優先度順 */

/* セマフォ固有 */
#define TA_FIRST 0x00000000U /* 待ち行列先頭のタスクを優先 */
#define TA_CNT 0x00000002U   /* 要求数を獲得できるタスクから順に割り当てる */

/* イベントフラグ固有 */
#define TA_WSGL 0x00000000U /* 複数タスクの待ちを許さない */
#define TA_WMUL 0x00000008U /* 複数タスクの待ちを許す */

/* イベントフラグの待ちモード */
#define TWF_ANDW 0x00U    /* AND 待ち */
#define TWF_ORW 0x01U     /* OR 待ち */
#define TWF_CLR 0x10U     /* 待ち解除時に全ビットをクリア */
#define TWF_BITCLR 0x20U  /* 待ち解除時に条件ビットのみクリア */

/* タイムアウト */
#define TMO_POL 0   /* ポーリング */
#define TMO_FEVR (-1) /* 永久待ち */

/* タスク指定 */
#define TSK_SELF 0 /* 自タスク */

/* タスク状態 (T_RTSK.tskstat)。和集合の判定ができるようビット対応。 */
#define TTS_RUN 0x0001U
#define TTS_RDY 0x0002U
#define TTS_WAI 0x0004U
#define TTS_SUS 0x0008U
#define TTS_WAS 0x000CU /* TTS_WAI | TTS_SUS */
#define TTS_DMT 0x0010U
#define TTS_NODISWAI 0x0080U

/* 待ち要因 (T_RTSK.tskwait) */
#define TTW_SLP 0x00000001U /* tk_slp_tsk による待ち */
#define TTW_DLY 0x00000002U /* tk_dly_tsk による待ち */
#define TTW_SEM 0x00000004U /* セマフォ待ち */
#define TTW_FLG 0x00000008U /* イベントフラグ待ち */
/* ランデブ (BTRON3 §1.3)。μT-Kernel が「(予約)」として空けている値を使う。 */
#define TTW_CAL 0x00000400U /* ランデブ成立待ち（呼出側） */
#define TTW_ACP 0x00000800U /* ランデブ成立待ち（受付側） */
#define TTW_RDV 0x00001000U /* ランデブ返答待ち */

/* ------------------------------------------------------------------ */
/* 実装の制限値 (D9。暫定)                                             */
/* ------------------------------------------------------------------ */

#define TK_MAX_TSKPRI 32    /* 指定可能な最大タスク優先度。仕様の下限は 16 */
#define TK_WAKEUP_MAXCNT 65535 /* 起床要求キューイング数の上限 */
#define TK_SUSPEND_MAXCNT 65535 /* 強制待ち要求ネスト数の上限 */

#define TK_MAX_TSKID 64 /* タスク数 */
#define TK_MAX_SEMID 64 /* セマフォ数 */
#define TK_MAX_FLGID 64 /* イベントフラグ数 */
#define TK_MAX_PORID 32 /* ランデブポート数 */
#define TK_MAX_RDV 32   /* 同時に成立しているランデブの数 */

#define TK_MIN_STKSZ 4096 /* スタックサイズの下限 */

/* ------------------------------------------------------------------ */
/* パケット                                                            */
/* ------------------------------------------------------------------ */

typedef struct t_ctsk {
	VP exinf;      /* 拡張情報。カーネルは内容に関知しない */
	ATR tskatr;    /* タスク属性 */
	TASKENTRY task; /* タスク起動アドレス */
	PRI itskpri;   /* タスク起動時優先度 */
	SZ stksz;      /* スタックサイズ (バイト) */
	SZ sstksz;     /* システムスタックサイズ (TA_SSTKSZ。未サポート) */
	VP stkptr;     /* ユーザスタックポインタ (TA_USERSTACK) */
	UB dsname[8];  /* DS オブジェクト名称 (TA_DSNAME) */
	VP bufptr;     /* ユーザバッファ (TA_USERBUF) */
} T_CTSK;

typedef struct t_rtsk {
	VP exinf;
	PRI tskpri;    /* 現在優先度 */
	PRI tskbpri;   /* ベース優先度 */
	UINT tskstat;  /* TTS_* */
	UW tskwait;    /* TTW_* */
	ID wid;        /* 待ちオブジェクト ID */
	INT wupcnt;    /* 起床要求キューイング数 */
	INT suscnt;    /* 強制待ち要求ネスト数 */
} T_RTSK;

typedef struct t_csem {
	VP exinf;
	ATR sematr;
	INT isemcnt; /* 初期資源数 */
	INT maxsem;  /* 最大資源数 */
	UB dsname[8];
} T_CSEM;

typedef struct t_rsem {
	VP exinf;
	ID wtsk;     /* 待ちタスクの ID。無ければ 0 */
	INT semcnt;  /* 現在の資源数 */
} T_RSEM;

typedef struct t_cflg {
	VP exinf;
	ATR flgatr;
	UINT iflgptn; /* 初期ビットパターン */
	UB dsname[8];
} T_CFLG;

typedef struct t_rflg {
	VP exinf;
	ID wtsk;
	UINT flgptn;
} T_RFLG;

/* ------------------------------------------------------------------ */
/* タスク管理機能                                                      */
/* ------------------------------------------------------------------ */

IMPORT ID tk_cre_tsk(CONST T_CTSK *pk_ctsk);
IMPORT ER tk_del_tsk(ID tskid);
IMPORT ER tk_sta_tsk(ID tskid, INT stacd);
IMPORT void tk_ext_tsk(void) __attribute__((noreturn));
IMPORT void tk_exd_tsk(void) __attribute__((noreturn));
IMPORT ER tk_ter_tsk(ID tskid);
IMPORT ER tk_chg_pri(ID tskid, PRI tskpri);
IMPORT ID tk_get_tid(void);
IMPORT ER tk_ref_tsk(ID tskid, T_RTSK *pk_rtsk);

/* ------------------------------------------------------------------ */
/* タスク付属同期機能                                                  */
/* ------------------------------------------------------------------ */

IMPORT ER tk_slp_tsk(TMO tmout);
IMPORT ER tk_wup_tsk(ID tskid);
IMPORT INT tk_can_wup(ID tskid);
IMPORT ER tk_rel_wai(ID tskid);
IMPORT ER tk_sus_tsk(ID tskid);
IMPORT ER tk_rsm_tsk(ID tskid);
IMPORT ER tk_frsm_tsk(ID tskid);
IMPORT ER tk_dly_tsk(RELTIM dlytim);

/* ------------------------------------------------------------------ */
/* 同期・通信機能                                                      */
/* ------------------------------------------------------------------ */

IMPORT ID tk_cre_sem(CONST T_CSEM *pk_csem);
IMPORT ER tk_del_sem(ID semid);
IMPORT ER tk_sig_sem(ID semid, INT cnt);
IMPORT ER tk_wai_sem(ID semid, INT cnt, TMO tmout);
IMPORT ER tk_ref_sem(ID semid, T_RSEM *pk_rsem);

IMPORT ID tk_cre_flg(CONST T_CFLG *pk_cflg);
IMPORT ER tk_del_flg(ID flgid);
IMPORT ER tk_set_flg(ID flgid, UINT setptn);
IMPORT ER tk_clr_flg(ID flgid, UINT clrptn);
IMPORT ER tk_wai_flg(ID flgid, UINT waiptn, UINT wfmode, UINT *p_flgptn, TMO tmout);
IMPORT ER tk_ref_flg(ID flgid, T_RFLG *pk_rflg);

/* ------------------------------------------------------------------ */
/* スケジューリング操作                                                */
/* ------------------------------------------------------------------ */

/* 指定優先度のレディキューを回転させる。同一優先度内のラウンドロビンは
 * これで実現する（仕様が時分割を規定していないため）。
 * tskpri = 0 なら自タスクの現在優先度。 */
IMPORT ER tk_rot_rdq(PRI tskpri);

/* ------------------------------------------------------------------ */
/* 時間管理機能                                                        */
/* ------------------------------------------------------------------ */

IMPORT ER tk_set_tim(CONST SYSTIM *pk_tim);
IMPORT ER tk_get_tim(SYSTIM *pk_tim);
IMPORT ER tk_get_otm(SYSTIM *pk_tim); /* 起動からの経過時間 */

/* ------------------------------------------------------------------ */
/* ディスパッチ禁止・割込み禁止                                        */
/* ------------------------------------------------------------------ */

IMPORT ER tk_dis_dsp(void);
IMPORT ER tk_ena_dsp(void);

/* ------------------------------------------------------------------ */
/* ランデブ (BTRON3 §1.3「プロセス/タスク間同期通信管理」)              */
/*                                                                     */
/* **ここだけ μT-Kernel ではなく BTRON3 の API である。** μT-Kernel 3.0 の */
/* 仕様にランデブは無い（実装の拡張であって標準仕様ではない）。一方       */
/* BTRON3 §1.3 は完全なシグネチャで規定しているので、そちらに従う（D10）。 */
/*                                                                     */
/* **したがって返すのは `ER_*`（周辺核の族）であって `E_*` ではない。**    */
/* 名前も `tk_` を付けない。規律 R20 の「どの API を呼んだかで族が決まる」  */
/* がそのまま効いている。E1 の IPC はこの上に載る（詳細 E1 ④）。          */
/* ------------------------------------------------------------------ */

typedef W TMOUT;      /* タイムアウト指定 (ミリ秒)。BTRON3 §1.3.1 */
#define T_NOWAIT 0    /* 待たない。μT-Kernel の TMO_POL と同値 */
#define T_FOREVER (-1) /* 永久待ち。μT-Kernel の TMO_FEVR と同値 */

/* ポート属性 opt は DELEXIT（生成プロセス終了時に自動削除）のみ。
 * DELEXIT は BTRON3 共通の属性なので typedef.h にある。 */

/* ランデブポートの生成。戻り値 >0 がポート ID。
 * 生成したポートはすべてのプロセスから使用可能（仕様）。 */
IMPORT WERR cre_por(W maxcmsz, W maxrmsz, UW opt);

/* ランデブポートの削除。
 * 受付待ち・呼出待ちのタスクは ER_DLT で待ちが解除される。
 * **既に成立しているランデブは成立したまま残り、正常に返答できる**（仕様）。 */
IMPORT ERR del_por(ID id);

/* ランデブ呼出。戻り値 >0 が返答メッセージのバイト数。
 *
 * msg は呼出メッセージの入力と返答メッセージの出力を兼ねる。
 * **maxrmsz 以上の領域が必要**（仕様）。cmsz は 1..maxcmsz。
 * tmo は**成立までの待ち時間**で、成立後は返答まで無限に待つ（仕様）。 */
IMPORT WERR cal_por(ID id, UW calptn, VP msg, W cmsz, TMOUT tmo);

/* ランデブ受付。戻り値 >0 が呼出メッセージのバイト数。
 * msg は maxcmsz 以上の領域が必要。*rdv にランデブ番号が入る。 */
IMPORT WERR acp_por(ID id, UW acpptn, W *rdv, VP msg, TMOUT tmo);

/* ランデブ回送。受け付けたランデブを別のポートへ回す。
 *
 * 呼出タスクと自タスクの間のランデブを解除し、**呼出タスクが新たに回送先へ
 * 呼出を行った状態にする**。呼出タスクの待ちは元の tmo に関係なく無限待ち
 * になり、自タスクは待たない（仕様）。詳細 E1 ④ が E13 の 3 者間要求に
 * これをそのまま当てている。
 *
 * 制約（仕様）: 回送先の maxrmsz は回送前の maxrmsz より大きくてはならない
 * (ER_OBJ)。cmsz は回送前の maxrmsz より大きくてはならない (ER_PAR)。
 * どちらも**呼出タスクの msg バッファを使い回す**ことから来ている。 */
IMPORT ERR fwd_por(ID id, UW calptn, W rdv, VP msg, W cmsz);

/* ランデブ返答。相手に返答してランデブを終了する。rmsz は 1..maxrmsz。 */
IMPORT ERR rpl_rdv(W rdv, VP msg, W rmsz);

/* ------------------------------------------------------------------ */
/* 中心核の起動                                                        */
/*                                                                     */
/* knl_init() で構造体を用意し、タスクを作ってから knl_start() を呼ぶ。  */
/* knl_start() は戻らない。呼び出した文脈はアイドルタスクになる。        */
/* ------------------------------------------------------------------ */

IMPORT ER knl_init(void);
IMPORT void knl_start(void) __attribute__((noreturn));

/* 中心核が動いているか。テストと緊急時の判定に使う。 */
IMPORT Bool knl_running(void);

#endif /* __BTRON_KERNEL_H__ */

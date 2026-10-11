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

/* ミューテックス固有。**下位 2 ビットの列挙であってビットフラグではない。**
 * TA_INHERIT / TA_CEILING は待ち行列を優先度順にする。 */
#define TA_INHERIT 0x00000002U /* 優先度継承プロトコル */
#define TA_CEILING 0x00000003U /* 優先度上限プロトコル */
#define TA_MTXPROTO 0x00000003U /* プロトコルの選択ビット */

/* メールボックス固有 */
#define TA_MFIFO 0x00000000U /* メッセージを FIFO で管理 */
#define TA_MPRI 0x00000002U  /* メッセージを優先度順で管理 */

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
#define TTW_MBX 0x00000040U /* メールボックス待ち */
#define TTW_MTX 0x00000080U /* ミューテックス待ち */
#define TTW_SMBF 0x00000100U /* メッセージバッファ送信待ち */
#define TTW_RMBF 0x00000200U /* メッセージバッファ受信待ち */
#define TTW_MPF 0x00002000U /* 固定長メモリプール待ち */
#define TTW_MPL 0x00004000U /* 可変長メモリプール待ち */
/* ランデブ (周辺核 §1.3)。μT-Kernel が「(予約)」として空けている値を使う。 */
#define TTW_CAL 0x00000400U /* ランデブ成立待ち（呼出側） */
#define TTW_ACP 0x00000800U /* ランデブ成立待ち（受付側） */
#define TTW_RDV 0x00001000U /* ランデブ返答待ち */

/* 周辺核 §1.2 メッセージ管理の待ち */
#define TTW_SMSG 0x00002000U /* メッセージキューの空き待ち */
#define TTW_RMSG 0x00004000U /* メッセージ受信待ち */
#define TTW_CFM 0x00008000U  /* メッセージ受信確認待ち (CONFM) */

/* ------------------------------------------------------------------ */
/* 実装の制限値 (D9。暫定)                                             */
/* ------------------------------------------------------------------ */

/* 指定可能な最大タスク優先度。仕様の下限は 16。
 *
 * **130 なのは周辺核のプロセス優先度を 1 対 1 で受けるため。**
 * BTRON3 §1.1.3 のプロセス優先度 0〜255 は 3 つの群に分かれている。
 *
 *   群A 絶対優先度     0〜127  厳密に優先度順。同一優先度内はラウンドロビン
 *   群B RR グループ1  128〜191 群全体でラウンドロビン。優先度は相対的な頻度
 *   群C RR グループ2  192〜255 同上
 *
 * 群A は「厳密に優先度順」なので 128 段をそのまま持たないと潰れる。
 * 一方、群B / 群C は**群の中では全員が同順位**（低い優先度のプロセスでも
 * 必ず実行される）なので、群ごとに 1 段あればよい。よって 128 + 1 + 1 = 130。
 *
 * ここを 32 のままにして 0〜255 を圧縮すると、優先度 0 と 3 が同じ段に
 * 落ちて群A の「厳密に優先度順」が壊れる。写像の表は periph/proc.c。 */
#define TK_MAX_TSKPRI 130
#define TK_WAKEUP_MAXCNT 65535 /* 起床要求キューイング数の上限 */
#define TK_SUSPEND_MAXCNT 65535 /* 強制待ち要求ネスト数の上限 */

#define TK_MAX_TSKID 64 /* タスク数 */
#define TK_MAX_SEMID 64 /* セマフォ数 */
#define TK_MAX_FLGID 64 /* イベントフラグ数 */
#define TK_MAX_MTXID 32 /* ミューテックス数 */
#define TK_MAX_MBXID 32 /* メールボックス数 */
#define TK_MAX_MBFID 16 /* メッセージバッファ数 */
#define TK_MAX_MPFID 16 /* 固定長メモリプール数 */
#define TK_MAX_MPLID 16 /* 可変長メモリプール数 */
#define TK_MAX_CYCID 16 /* 周期ハンドラ数 */
#define TK_MAX_ALMID 16 /* アラームハンドラ数 */

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

typedef struct t_cmtx {
	VP exinf;
	ATR mtxatr;
	PRI ceilpri; /* TA_CEILING のときだけ有効。上限優先度 */
	UB dsname[8];
} T_CMTX;

typedef struct t_rmtx {
	VP exinf;
	ID htsk;  /* ロックしているタスク。無ければ 0 */
	ID wtsk;  /* ロック待ちの先頭タスク。無ければ 0 */
} T_RMTX;

/* メールボックスのメッセージヘッダ。
 *
 * **メッセージの本体はアプリが用意し、カーネルはこのヘッダで繋ぐだけ。**
 * 内容の複製はしない（規律 R10 の「大きなデータを IPC で運ばない」と
 * 同じ考え方が仕様の側に既に入っている）。
 * TA_MPRI のときは T_MSG_PRI を先頭に置く。 */
typedef struct t_msg {
	struct t_msg *msgque; /* カーネルが使う。アプリは触らない */
} T_MSG;

typedef struct t_msg_pri {
	T_MSG msgque;
	PRI msgpri; /* 1 が最高。同一優先度は FIFO */
} T_MSG_PRI;

typedef struct t_cmbx {
	VP exinf;
	ATR mbxatr;
	UB dsname[8];
} T_CMBX;

typedef struct t_rmbx {
	VP exinf;
	ID wtsk;      /* 受信待ちの先頭タスク */
	T_MSG *pk_msg; /* 待っているメッセージの先頭 */
} T_RMBX;

typedef struct t_cmbf {
	VP exinf;
	ATR mbfatr;
	W bufsz;  /* メッセージバッファ領域のバイト数。**0 でもよい**（同期通信） */
	W maxmsz; /* 送信できるメッセージの最大バイト数 */
	VP bufptr; /* TA_USERBUF のときのバッファ。未サポート */
	UB dsname[8];
} T_CMBF;

typedef struct t_rmbf {
	VP exinf;
	ID wtsk;  /* 受信待ちの先頭タスク */
	ID stsk;  /* 送信待ちの先頭タスク */
	W msgsz;  /* 次に受信されるメッセージのバイト数。無ければ 0 */
	W frbufsz; /* 空きバッファのバイト数 */
	W maxmsz;
} T_RMBF;

typedef struct t_cmpf {
	VP exinf;
	ATR mpfatr;
	W mpfcnt;  /* 獲得できるメモリブロックの数 */
	W blfsz;   /* メモリブロックのバイト数 */
	VP bufptr; /* TA_USERBUF のときのバッファ。未サポート */
	UB dsname[8];
} T_CMPF;

typedef struct t_rmpf {
	VP exinf;
	ID wtsk;
	W frbcnt; /* 空きブロック数 */
} T_RMPF;

typedef struct t_cmpl {
	VP exinf;
	ATR mplatr;
	W mplsz;   /* メモリプールのバイト数 */
	VP bufptr; /* TA_USERBUF のときのバッファ。未サポート */
	UB dsname[8];
} T_CMPL;

typedef struct t_rmpl {
	VP exinf;
	ID wtsk;
	W frsz;    /* 空きの総バイト数 */
	W maxsz;   /* 連続して獲得できる最大バイト数 */
} T_RMPL;

/* 周期ハンドラ・アラームハンドラ。
 * ハンドラはタスク独立部で走る。自タスクを待たせる API は呼べない。 */
typedef void (*CYCHANDLER)(VP exinf);
typedef void (*ALMHANDLER)(VP exinf);

typedef struct t_ccyc {
	VP exinf;
	ATR cycatr;
	CYCHANDLER cychdr;
	RELTIM cyctim; /* 起動周期 (ミリ秒)。>0 */
	RELTIM cycphs; /* 起動位相 (ミリ秒) */
	UB dsname[8];
} T_CCYC;

typedef struct t_rcyc {
	VP exinf;
	RELTIM lfttim; /* 次に起動するまでの時間 */
	UINT cycstat;  /* TCYC_* */
} T_RCYC;

typedef struct t_calm {
	VP exinf;
	ATR almatr;
	ALMHANDLER almhdr;
	UB dsname[8];
} T_CALM;

typedef struct t_ralm {
	VP exinf;
	RELTIM lfttim; /* 起動までの残り時間。動作していなければ 0 */
	UINT almstat;  /* TALM_* */
} T_RALM;

/* 周期ハンドラ属性・状態 */
#define TA_STA 0x00000002U /* 生成時から動作させる */
#define TA_PHS 0x00000004U /* 起動位相を保存する */

#define TCYC_STOP 0x00U /* 停止中 */
#define TCYC_STA 0x01U  /* 動作中 */
#define TALM_STOP 0x00U
#define TALM_STA 0x01U

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

IMPORT ID tk_cre_mtx(CONST T_CMTX *pk_cmtx);
IMPORT ER tk_del_mtx(ID mtxid);
IMPORT ER tk_loc_mtx(ID mtxid, TMO tmout);
IMPORT ER tk_unl_mtx(ID mtxid);
IMPORT ER tk_ref_mtx(ID mtxid, T_RMTX *pk_rmtx);

IMPORT ID tk_cre_mbx(CONST T_CMBX *pk_cmbx);
IMPORT ER tk_del_mbx(ID mbxid);
IMPORT ER tk_snd_mbx(ID mbxid, T_MSG *pk_msg);
IMPORT ER tk_rcv_mbx(ID mbxid, T_MSG **ppk_msg, TMO tmout);
IMPORT ER tk_ref_mbx(ID mbxid, T_RMBX *pk_rmbx);

IMPORT ID tk_cre_mbf(CONST T_CMBF *pk_cmbf);
IMPORT ER tk_del_mbf(ID mbfid);
IMPORT ER tk_snd_mbf(ID mbfid, CONST VP msg, INT msgsz, TMO tmout);
IMPORT INT tk_rcv_mbf(ID mbfid, VP msg, TMO tmout);
IMPORT ER tk_ref_mbf(ID mbfid, T_RMBF *pk_rmbf);

/* ------------------------------------------------------------------ */
/* メモリプール管理機能                                                */
/* ------------------------------------------------------------------ */

IMPORT ID tk_cre_mpf(CONST T_CMPF *pk_cmpf);
IMPORT ER tk_del_mpf(ID mpfid);
IMPORT ER tk_get_mpf(ID mpfid, VP *p_blf, TMO tmout);
IMPORT ER tk_rel_mpf(ID mpfid, VP blf);
IMPORT ER tk_ref_mpf(ID mpfid, T_RMPF *pk_rmpf);

IMPORT ID tk_cre_mpl(CONST T_CMPL *pk_cmpl);
IMPORT ER tk_del_mpl(ID mplid);
IMPORT ER tk_get_mpl(ID mplid, W blksz, VP *p_blk, TMO tmout);
IMPORT ER tk_rel_mpl(ID mplid, VP blk);
IMPORT ER tk_ref_mpl(ID mplid, T_RMPL *pk_rmpl);

/* ------------------------------------------------------------------ */
/* 周期ハンドラ・アラームハンドラ                                      */
/* ------------------------------------------------------------------ */

IMPORT ID tk_cre_cyc(CONST T_CCYC *pk_ccyc);
IMPORT ER tk_del_cyc(ID cycid);
IMPORT ER tk_sta_cyc(ID cycid);
IMPORT ER tk_stp_cyc(ID cycid);
IMPORT ER tk_ref_cyc(ID cycid, T_RCYC *pk_rcyc);

IMPORT ID tk_cre_alm(CONST T_CALM *pk_calm);
IMPORT ER tk_del_alm(ID almid);
IMPORT ER tk_sta_alm(ID almid, RELTIM almtim);
IMPORT ER tk_stp_alm(ID almid);
IMPORT ER tk_ref_alm(ID almid, T_RALM *pk_ralm);

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

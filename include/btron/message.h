/*
 * message.h — 周辺核 §1.2「メッセージ管理」
 *
 * **周辺核の API である。** 返すのは `ER_*`（規律 R20）。
 * 中心核のメールボックス (`tk_snd_mbx`) やメッセージバッファ
 * (`tk_snd_mbf`) とは別の層で、こちらは**プロセスに付属する**。
 *
 * 【§1.2.1 の要点】
 *   - プロセスにメッセージキューが付属し、宛先はプロセス ID で指定する
 *   - キューは **FIFO**。必ず送信順に入る
 *   - キューが満杯のとき、空くまで待つかエラーで返るかを選べる
 *   - **子プロセスの終了メッセージもこのキューに入る**。つまり §1.1 の
 *     `ext_prc` / `ter_prc` の終了コードは、ここを通って親に届く
 *   - メッセージハンドラを定義すると非同期に処理できる（一種の割込み）
 *
 * 【§1.2.2 の要点】タイプは 1〜31 で、用途が層ごとに分かれている。
 *
 *     1〜5    システムが自動発生させる（終了・タイムアウト）
 *     6〜10   システム予約
 *     11〜23  **外殻予約**（マネージャがここを使う）
 *     24〜31  アプリケーション
 *
 * システムメッセージは「基本的にメッセージキューのオーバーフローの
 * 影響を受けず、捨てられることはない」（仕様）。実装では通常の容量とは
 * 別に予備枠を置いて満たしている。
 */

#ifndef __BTRON_MESSAGE_H__
#define __BTRON_MESSAGE_H__

#include <btron/errcode.h>
#include <btron/typedef.h>

/* ------------------------------------------------------------------ */
/* メッセージタイプ (§1.2.2 / §1.2.4)                                  */
/* ------------------------------------------------------------------ */

#define MS_ABORT 1   /* プロセス異常終了 */
#define MS_EXIT 2    /* プロセス正常終了 */
#define MS_TERM 3    /* プロセス強制終了 */
#define MS_TMOUT 4   /* タイムアウト */
#define MS_SYSEVT 5  /* システムイベント（強制終了） */
#define MS_SYS1 6    /* システム予約 */
#define MS_SYS2 7
#define MS_SYS3 8
#define MS_SYS4 9
#define MS_SYS5 10
#define MS_MNG0 11 /* 外殻予約。マネージャがここを使う */
#define MS_MNG1 12
#define MS_MNG2 13
#define MS_MNG3 14
#define MS_MNG4 15
#define MS_MNG5 16
#define MS_MNG6 17
#define MS_MNG7 18
#define MS_MNG8 19
#define MS_MNG9 20
#define MS_MNG10 21
#define MS_MNG11 22
#define MS_MNG12 23
#define MS_TYPE0 24 /* アプリケーションメッセージ */
#define MS_TYPE1 25
#define MS_TYPE2 26
#define MS_TYPE3 27
#define MS_TYPE4 28
#define MS_TYPE5 29
#define MS_TYPE6 30
#define MS_TYPE7 31

#define MS_MIN 1
#define MS_MAX 31

/* システムが自動発生させる範囲。この範囲は予備枠を使える。 */
#define MS_SYSTEM_LAST MS_SYSEVT

/* ------------------------------------------------------------------ */
/* メッセージタイプマスク (§1.2.4)                                     */
/* ------------------------------------------------------------------ */

#define MSGMASK(msgtype) (1 << ((msgtype) - 1))

#define MM_ABORT MSGMASK(MS_ABORT)
#define MM_EXIT MSGMASK(MS_EXIT)
#define MM_TERM MSGMASK(MS_TERM)
#define MM_TMOUT MSGMASK(MS_TMOUT)
#define MM_SYSEVT MSGMASK(MS_SYSEVT)
#define MM_SYS1 MSGMASK(MS_SYS1)
#define MM_SYS2 MSGMASK(MS_SYS2)
#define MM_SYS3 MSGMASK(MS_SYS3)
#define MM_SYS4 MSGMASK(MS_SYS4)
#define MM_SYS5 MSGMASK(MS_SYS5)
#define MM_MNG0 MSGMASK(MS_MNG0)
#define MM_MNG1 MSGMASK(MS_MNG1)
#define MM_MNG2 MSGMASK(MS_MNG2)
#define MM_MNG3 MSGMASK(MS_MNG3)
#define MM_MNG4 MSGMASK(MS_MNG4)
#define MM_MNG5 MSGMASK(MS_MNG5)
#define MM_MNG6 MSGMASK(MS_MNG6)
#define MM_MNG7 MSGMASK(MS_MNG7)
#define MM_MNG8 MSGMASK(MS_MNG8)
#define MM_MNG9 MSGMASK(MS_MNG9)
#define MM_MNG10 MSGMASK(MS_MNG10)
#define MM_MNG11 MSGMASK(MS_MNG11)
#define MM_MNG12 MSGMASK(MS_MNG12)
#define MM_TYPE0 MSGMASK(MS_TYPE0)
#define MM_TYPE1 MSGMASK(MS_TYPE1)
#define MM_TYPE2 MSGMASK(MS_TYPE2)
#define MM_TYPE3 MSGMASK(MS_TYPE3)
#define MM_TYPE4 MSGMASK(MS_TYPE4)
#define MM_TYPE5 MSGMASK(MS_TYPE5)
#define MM_TYPE6 MSGMASK(MS_TYPE6)
#define MM_TYPE7 MSGMASK(MS_TYPE7)

#define MM_ALL 0x7fffffff
#define MM_NULL 0

/* ------------------------------------------------------------------ */
/* メッセージ (§1.2.1 / §1.2.4)                                        */
/*                                                                     */
/* **これが MESSAGE の正式な定義である。** §1.1.6 のプロセス起動        */
/* メッセージも「プロセス間メッセージと同一の構造を持つ」（仕様）ので    */
/* 同じ型を使う。                                                      */
/*                                                                     */
/* `msg_body` は共用体として宣言されているが、`MSGSIZE()` と            */
/* `rcv_msg()` の `msgsz` 引数が示すとおり**可変長としても使える**。     */
/* 32 バイトを超える本体は、この構造体より大きい領域を確保して使う。    */
/* ------------------------------------------------------------------ */

typedef union {
	struct { /* MS_ABORT */
		W pid;
		W code;
	} ABORT;
	struct { /* MS_EXIT */
		W pid;
		W code;
	} EXIT;
	struct { /* MS_TERM */
		W pid;
		W code;
	} TERM;
	struct { /* MS_TMOUT */
		W code;
	} TMOUT;
	struct { /* MS_SYSEVT */
		W code;
	} SYSEVT;
	struct { /* その他一般 */
		UB msg_str[32];
	} ANYMSG;
} MSGBODY;

typedef struct message {
	W msg_type;      /* メッセージタイプ (MS_*) */
	W msg_size;      /* メッセージ本体サイズ（バイト） */
	MSGBODY msg_body; /* メッセージ本体 */
} MESSAGE;

/* ヘッダ部を含むメッセージ全体のバイト数。仕様の定義そのまま。
 * `offsetof` は使えないので同じ値を直接書く（_Static_assert で固定）。 */
#define MSG_HEADER_SIZE 8
#define MSGSIZE(msgsz) (MSG_HEADER_SIZE + (msgsz))

_Static_assert(MSG_HEADER_SIZE == 2 * sizeof(W), "MESSAGE のヘッダ部");
_Static_assert(sizeof(MSGBODY) == 32, "MSGBODY は 32 バイト (§1.2.4)");

/* ------------------------------------------------------------------ */
/* メッセージオプション (§1.2.4)                                       */
/*                                                                     */
/* 【注意】**0x0002 は呼ぶシステムコールによって意味が違う。**          */
/* `snd_msg` では CONFM（受信確認待ち）、`rcv_msg` では CHECK           */
/* （メッセージの有無を調べる）。仕様がそう定義している。               */
/* 規律 R22 と同じ構図で、ビットの意味は文脈で決まる。                  */
/* ------------------------------------------------------------------ */

#define WAIT 0x0000   /* 待つ */
#define NOWAIT 0x0001 /* 待たない */
#define CONFM 0x0002  /* snd_msg: 受信（確認）待ち */
#define CHECK 0x0002  /* rcv_msg: メッセージチェック */
#define WAIEVT 0x0004 /* rcv_msg: メッセージおよびイベント待ち */
/* CLR (0x0000) / NOCLR (0x0008) は typedef.h にある */

/* ------------------------------------------------------------------ */
/* メッセージハンドラ (§1.2.3)                                         */
/* ------------------------------------------------------------------ */

#define MH_NONE ((FUNCP)1)  /* 無視（待ちも中断しない） */
#define MH_BREAK ((FUNCP)2) /* 無視して待ちを中断（ER_MINTR） */
#define MH_TERM ((FUNCP)3)  /* 自プロセスを異常終了し MS_ABORT を送る */

/* メッセージハンドラの形。
 *
 *   pid   送信したプロセスの ID（自プロセスの場合は 0）
 *   r_msg 受信したメッセージへのポインタ
 *
 * 最後に必ず `ret_msg()` を呼ぶこと（仕様）。 */
typedef void (*MSGHANDLER)(W pid, MESSAGE *r_msg);

/* ------------------------------------------------------------------ */
/* 実装の制限値 (D9。暫定)                                             */
/* ------------------------------------------------------------------ */

#define MSG_MAX_BODY 256  /* メッセージ本体の上限。超えると ER_SZOVR */
#define MSG_QUEUE_MAX 16  /* 1 プロセスのキューの深さ（通常メッセージ） */
#define MSG_RESERVE 8     /* システムメッセージ用の予備枠 */
#define MSG_POOL 160      /* 全プロセス合計のメッセージ数 */
#define MSG_TMG_MAX 32    /* 同時に要求できるタイムアウトメッセージ数 */

/* ------------------------------------------------------------------ */
/* システムコール (§1.2.5)                                             */
/* ------------------------------------------------------------------ */

/* メッセージ送信。pid: >0 任意 / 0 自プロセス / -1 親プロセス。
 * opt: NOWAIT ‖ WAIT ‖ CONFM。 */
IMPORT ERR snd_msg(W pid, CONST MESSAGE *msg, W opt);

/* メッセージ受信。戻り値 >0 が送信元 PID、0 なら送信元は自プロセス。
 * opt: (WAIT ‖ NOWAIT ‖ WAIEVT) | (CLR ‖ NOCLR) | (CHECK)。
 *
 * `msgsz` は格納領域全体のバイト数で、ヘッダを含むので 8 以上必要。
 * 入り切らない場合は msgsz の範囲だけ格納してエラーで返り、
 * **CLR 指定でもメッセージはキューに残る**（仕様）。 */
IMPORT WERR rcv_msg(W t_mask, MESSAGE *msg, W msgsz, W opt);

/* メッセージクリア。`last_mask` で指定したタイプの直前までクリアする。
 * `last_mask = MM_NULL` でキューの最後まで、`MM_ALL` で 1 つだけ。 */
IMPORT ERR clr_msg(W t_mask, W last_mask);

/* 自プロセス宛に `time` ミリ秒後の MS_TMOUT を要求する。 */
IMPORT ERR req_tmg(W time, W code);

/* 自プロセスのタイムアウトメッセージ要求をすべて取り消す。
 * **既にキューに入っているものはクリアしない**（仕様）。 */
IMPORT ERR can_tmg(void);

/* メッセージハンドラ定義。`msghdr` は関数・NULL・MH_* のいずれか。 */
IMPORT ERR def_msg(W t_mask, FUNCP msghdr);

/* メッセージハンドラ終了。ハンドラの中からのみ呼べる。
 * ret = 0 なら割り込んだ位置から再開、≠0 ならここから続行する。 */
IMPORT ERR ret_msg(W ret);

/* WAIEVT 指定の `rcv_msg()` の待ちを解除する。
 * 待っているタスクが無ければ要求を記録する（回数は記録しない）。
 * **外殻（ウィンドウマネージャ）の `wget_evt` のために用意された**（仕様）。 */
IMPORT ERR brk_msg(void);

/* ------------------------------------------------------------------ */
/* 以下は BTRON3 の API ではない。本OS 内部の口                         */
/* ------------------------------------------------------------------ */

IMPORT ERR msg_init(void);

/* プロセスの終了を親へ知らせる (§1.2.2)。type は MS_EXIT / MS_TERM /
 * MS_ABORT。§1.1 の teardown から呼ぶ。 */
IMPORT void msg_notify_exit(W parent, W pid, W code, W type);

/* プロセスが消えるときの後片付け。キューと要求とハンドラを捨て、
 * 空き待ち・確認待ちのタスクを ER_NOPRC で解除する。 */
IMPORT void msg_release_process(W pid);

/* 1 tick ごとに呼ぶ。タイムアウトメッセージの送出と、
 * 入れ子を避けて持ち越したメッセージハンドラの起動。 */
IMPORT void msg_tick(void);

IMPORT W msg_count(W pid);   /* 検証用。pid のキューに入っている数 */
IMPORT W msg_used(void);     /* 検証用。プール全体の使用数 */

#endif /* __BTRON_MESSAGE_H__ */

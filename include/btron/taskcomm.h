/*
 * taskcomm.h — 周辺核 §1.3「プロセス/タスク間同期通信管理」
 *
 * **周辺核の API である。中心核ではない。**
 * BTRON3 の「OS 仕様 第1章」は周辺核で、§1.3 がこれ。中心核 (ITRON) は
 * 別に規定されていて、その代わりに μT-Kernel 3.0 を基準にしている (D10)。
 *
 * したがって返すのは `ER_*`（周辺核の族）で、名前に `tk_` を付けない。
 * 中心核の `tk_cre_sem` 等と**同名の機能が層ごとに 2 つある**ことになるが、
 * 両仕様がそう作られている。規律 R20 のとおり、どの API を呼んだかで
 * エラーコードの族が決まる。
 *
 * §1.3 の全システムコール:
 *
 *   セマフォ          cre_sem / del_sem / sig_sem / wai_sem        実装済み
 *   イベントフラグ    cre_flg / del_flg / set_flg / clr_flg / wai_flg  実装済み
 *   メッセージバッファ cre_mbf / del_mbf / snd_mbf / rcv_mbf       実装済み
 *   ランデブ          cre_por / del_por / cal_por / acp_por /
 *                     fwd_por / rpl_rdv                           実装済み
 *
 * ランデブだけ先に実装したのは、詳細 E1 で外殻サーバの IPC の土台に
 * 選んだからである。残りは中心核のオブジェクトの上に載せた（taskobj.c）。
 *
 * 【仕様の注記】「メッセージによる通信と一番大きく異なる点は、メッセージが
 * プロセス間の通信であるのに対し、タスク間の通信であることである」。
 * §1.2 がプロセス間、ここがタスク間。ただし「生成したオブジェクトは
 * すべてのプロセスから使用可能」なのでプロセス間にも使える。
 */

#ifndef __BTRON_TASKCOMM_H__
#define __BTRON_TASKCOMM_H__

#include <btron/errcode.h>
#include <btron/kernel.h>
#include <btron/typedef.h>

typedef W TMOUT;       /* タイムアウト指定 (ミリ秒)。§1.3.1 */
#define T_NOWAIT 0     /* 待たない。中心核の TMO_POL と同値 */
#define T_FOREVER (-1) /* 永久待ち。中心核の TMO_FEVR と同値 */

/* 実装の制限値 (D9。暫定) */
#define TK_MAX_PORID 32 /* ランデブポート数 */
#define TK_MAX_RDV 32   /* 同時に成立しているランデブの数 */
#define TK_MAX_TOBJ 64  /* セマフォ + イベントフラグ + メッセージバッファ */

/* ------------------------------------------------------------------ */
/* セマフォ (§1.3.1 / §1.3.2)                                          */
/*                                                                     */
/* **SEM_EXCL は中心核のミューテックスに写る。** 仕様が課している規則    */
/* （獲得〜開放の順、再帰禁止、所有者のみ解放、タスク終了時の自動解放、  */
/* 初期値 1）が μT-Kernel のミューテックスと完全に一致する。            */
/* SEM_SYNC の方が本来の計数セマフォ。詳細は periph/taskobj.c。          */
/* ------------------------------------------------------------------ */

#define SEM_SYNC 0x0000U /* 同期用セマフォ（計数セマフォ） */
#define SEM_EXCL 0x4000U /* 排他制御用セマフォ（＝ミューテックス） */

IMPORT WERR cre_sem(W cnt, UW opt);
IMPORT ERR del_sem(ID id);
IMPORT ERR sig_sem(ID id);
IMPORT ERR wai_sem(ID id, TMOUT tmo);

/* ------------------------------------------------------------------ */
/* イベントフラグ (§1.3.1 / §1.3.2)                                    */
/* ------------------------------------------------------------------ */

#define WF_AND 0x0000U /* ptn で 1 のビットがすべてセットされるまで待つ */
#define WF_OR 0x0002U  /* ptn で 1 のビットのいずれかがセットされるまで待つ */
/* NOCLR (0x0008) は typedef.h にある */

IMPORT WERR cre_flg(W ptn, UW opt);
IMPORT ERR del_flg(ID id);
IMPORT ERR set_flg(ID id, W ptn);

/* **消去パターンではなく「残すパターン」。** 仕様は「現在の値に ptn が
 * AND される」と規定している。`clr_flg(id, 0)` で全クリア。 */
IMPORT ERR clr_flg(ID id, W ptn);

/* 戻り値 >0 が待ち解除時のイベントフラグの値。
 * mode = (WF_AND ‖ WF_OR) | [NOCLR]。NOCLR が無ければ**全ビット**が
 * 0 クリアされる（条件ビットだけではない）。 */
IMPORT WERR wai_flg(ID id, W ptn, UW mode, TMOUT tmo);

/* ------------------------------------------------------------------ */
/* メッセージバッファ (§1.3.2)                                         */
/* ------------------------------------------------------------------ */

/* bufsz = 0 で同期通信（送信側と受信側が同時に揃う）。 */
IMPORT WERR cre_mbf(W bufsz, W max, UW opt);
IMPORT ERR del_mbf(ID id);
IMPORT ERR snd_mbf(ID id, VP msg, W sz, TMOUT tmo);
IMPORT WERR rcv_mbf(ID id, VP msg, TMOUT tmo);

/* ------------------------------------------------------------------ */
/* 内部の口（BTRON3 の API ではない）                                   */
/* ------------------------------------------------------------------ */

IMPORT ERR tobj_init(void);
IMPORT void tobj_release_process(W pid); /* DELEXIT の自動削除 */
IMPORT W tobj_count(void);               /* 検証用 */

/* ------------------------------------------------------------------ */
/* ランデブ                                                            */
/*                                                                     */
/* 呼出側と受付側が出会ったところで同期し、呼出メッセージを渡し、        */
/* 返答を受け取るまで呼出側が待つ。同期 RPC のプリミティブ。            */
/* 成立条件は (calptn & acpptn) != 0。                                  */
/* ------------------------------------------------------------------ */

/* ランデブポートの生成。戻り値 >0 がポート ID。
 * opt は DELEXIT（生成プロセス終了時に自動削除）のみ。
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

/* 成立しているランデブの**呼出側のプロセス ID**。
 *
 * **BTRON3 の API ではない。** 外殻のサーバが「誰からの要求か」で権限を
 * 判断するのに要る。要求の中に入れさせると偽れるので、ランデブの記録から
 * 引けるようにしてある。 */
IMPORT W rdv_caller(W rdv);

#endif /* __BTRON_TASKCOMM_H__ */

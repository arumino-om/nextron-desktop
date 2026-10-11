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
 *   セマフォ          cre_sem / del_sem / sig_sem / wai_sem        未実装
 *   イベントフラグ    cre_flg / del_flg / set_flg / clr_flg / wai_flg  未実装
 *   メッセージバッファ cre_mbf / del_mbf / snd_mbf / rcv_mbf       未実装
 *   ランデブ          cre_por / del_por / cal_por / acp_por /
 *                     fwd_por / rpl_rdv                           **実装済み**
 *
 * ランデブだけ先に実装したのは、詳細 E1 で外殻サーバの IPC の土台に
 * 選んだからである。残りは Phase 2 で中心核の上に載せる。
 */

#ifndef __BTRON_TASKCOMM_H__
#define __BTRON_TASKCOMM_H__

#include <btron/errcode.h>
#include <btron/typedef.h>

typedef W TMOUT;       /* タイムアウト指定 (ミリ秒)。§1.3.1 */
#define T_NOWAIT 0     /* 待たない。中心核の TMO_POL と同値 */
#define T_FOREVER (-1) /* 永久待ち。中心核の TMO_FEVR と同値 */

/* 実装の制限値 (D9。暫定) */
#define TK_MAX_PORID 32 /* ランデブポート数 */
#define TK_MAX_RDV 32   /* 同時に成立しているランデブの数 */

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

#endif /* __BTRON_TASKCOMM_H__ */

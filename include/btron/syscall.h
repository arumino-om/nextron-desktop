/*
 * syscall.h — ユーザ特権 (EL0) から周辺核を呼ぶための約束
 *
 * ------------------------------------------------------------------------
 * これは BTRON3 仕様の一部ではない
 * ------------------------------------------------------------------------
 *
 * 仕様はシステムコールを**関数として**定義しているだけで、どうやって
 * 特権境界を越えるかは規定していない（当然で、CPU ごとに違う）。
 * ここにあるのは本OS が決めた呼び出し規約である。
 *
 * アプリケーションが書くのは `opn_dev(...)` のような普通の関数呼び出しで、
 * `src/lib/sys/` のスタブがこの規約に直す。**アプリはこのヘッダを
 * 直接使わない。**
 *
 * ------------------------------------------------------------------------
 * 規約
 * ------------------------------------------------------------------------
 *
 *   arm64    x8 = 番号、x0〜x5 = 引数、`svc #0`、戻り値は x0
 *   x86_64   rax = 番号、rdi/rsi/rdx/r10/r8/r9 = 引数、`syscall`（予定）
 *
 * ------------------------------------------------------------------------
 * ユーザから来たポインタは信用しない
 * ------------------------------------------------------------------------
 *
 * **ここが保護の要。** ユーザがカーネルの番地を渡してきたら、カーネルは
 * 自分の権限でそこを読み書きしてしまう。番号ごとに、ポインタ引数は
 * 必ず `usr_ok()` で
 *
 *   - 下位半分（プロセス固有の空間）に収まっているか
 *   - 実際に写像されているか
 *   - 書き込むなら書き込み可能か
 *
 * を確かめてから触る。PAN（特権アクセス禁止）に頼らず明示的に検める
 * 方を選んだ。PAN は「触れない」だけで「どこを触ろうとしたか」を
 * 教えてくれないので、誤りが例外になって落ちるだけになる。
 */

#ifndef __BTRON_SYSCALL_H__
#define __BTRON_SYSCALL_H__

#include <btron/typedef.h>

/* ------------------------------------------------------------------ */
/* システムコール番号                                                  */
/*                                                                     */
/* 章ごとに 10 番刻み。**一度決めた番号は変えない** — ユーザプログラムは */
/* 番号で呼ぶので、ずらすと古いプログラムが別の呼び出しに化ける。       */
/* ------------------------------------------------------------------ */

/* §1.1 プロセス / タスク管理 */
#define SYS_ext_prc 1
#define SYS_ter_prc 2
#define SYS_chg_pri 3
#define SYS_prc_sts 4
#define SYS_get_usr 5
#define SYS_slp_tsk 6
#define SYS_wup_tsk 7
#define SYS_dly_tsk 8
#define SYS_get_tid 9
#define SYS_cre_prc 10

/* §1.2 メッセージ管理 */
#define SYS_snd_msg 20
#define SYS_rcv_msg 21
#define SYS_clr_msg 22

/* §1.5 メモリ管理 */
#define SYS_get_mbk 30
#define SYS_rel_mbk 31
#define SYS_mbk_sts 32

/* §1.9 時計管理 / §1.10 システム管理 */
#define SYS_get_tim 40
#define SYS_set_tim 41
#define SYS_get_ver 42
#define SYS_get_cnf 43

/* §1.8 デバイス管理 */
#define SYS_opn_dev 50
#define SYS_cls_dev 51
#define SYS_rea_dev 52
#define SYS_wri_dev 53

/* §1.6 ファイル管理 */
#define SYS_get_lnk 60
#define SYS_chg_wrk 61
#define SYS_cre_fil 62
#define SYS_del_fil 63
#define SYS_opn_fil 64
#define SYS_cls_fil 65
#define SYS_see_rec 66
#define SYS_fnd_rec 67
#define SYS_rea_rec 68
#define SYS_wri_rec 69
#define SYS_ins_rec 70
#define SYS_apd_rec 71
#define SYS_del_rec 72
#define SYS_fil_sts 73

/* §1.7 イベント管理 */
#define SYS_get_evt 80
#define SYS_put_evt 81
#define SYS_chg_emk 82
#define SYS_get_pdp 83

#define SYS_MAX 95

/* ------------------------------------------------------------------ */
/* カーネル側の入口                                                    */
/* ------------------------------------------------------------------ */

/* 例外ベクタ (vectors.S) から呼ばれる。引数と番号の並びは                */
/* ベクタ側と合わせること。                                             */
IMPORT D btron_syscall_entry(UD a0, UD a1, UD a2, UD a3, UD a4, UD a5,
                             UD num);

/* ユーザから来たポインタを検める。触ってよければ True。
 * `size` が 0 なら「NULL でないこと」だけを見る。 */
IMPORT Bool usr_ok(UD p, W size, Bool write);

/* EL0 からの、システムコール以外の同期例外。ベクタから呼ばれる。
 * そのプロセスだけを落として戻らない。落とせなければ戻る。 */
IMPORT void hal_user_fault(UD esr, UD far, UD elr);

#endif /* __BTRON_SYSCALL_H__ */

/*
 * testsuite.h — 検査区分の登録と、区分をまたいで使う道具
 *
 * **なぜ登録制にしたか（W0-1）。**
 * 検査は 1 つの selftest.c に書き足し続けていて 7,500 行になった。
 * 1 つのタスクが 1 つのコミットになる作業の進め方では、どの作業も
 * 必ずこのファイルの末尾を触る。並列に進めると全員がそこでぶつかる。
 *
 * そこで区分ごとにファイルを分け、**登録をリンカのセクションに集めた**。
 * 新しい区分を足すときに触るのは自分の新しいファイルだけで、
 * 既存のファイルは 1 行も変わらない。
 *
 * 実行順は order で決める。リンク順に依存させると、ビルドの都合で
 * 検査の順番が変わってしまう（検査には順番に意味があるものがある
 * — フォントマネージャを立ててから止める、ファイルシステムを
 * 接続してから切り離す）。order の帯は docs/registry.md で予約する。
 *
 * 書き方:
 *
 *     BTRON_TEST_GROUP("周辺核 なにか管理 (§1.x)", 400, TST_PHASE_PROC,
 *                      tst_nanika)
 *     {
 *             BTRON_TEST("...", { BTRON_CHECK("...", expr); });
 *     }
 */

#ifndef __BTRON_TESTSUITE_H__
#define __BTRON_TESTSUITE_H__

#include <btron/dp.h>
#include <btron/file.h>
#include <btron/kernel.h>
#include <btron/message.h>
#include <btron/test.h>
#include <btron/typedef.h>

/* 走らせる文脈。
 *
 *   TST_PHASE_KERNEL  プロセスに属さないカーネルタスク。中心核の検査は
 *                     「同一優先度内の実行順」を 1 文字ずつ比べるので、
 *                     タイムスライスで回されるプロセス内では走らせない
 *   TST_PHASE_PROC    初期プロセスの中。周辺核より上はこちら
 */
#define TST_PHASE_KERNEL 0
#define TST_PHASE_PROC 1
/*   TST_PHASE_SELF    本来の起動列 (D8 / startup.c) を通したあと。
 *                     ESP に \BTRON\SELFBOOT があるときだけ走る。
 *                     検査の経路（ファイルシステムが 1 つも接続されて
 *                     いない状態から始まる）と前提が違うので分けてある */
#define TST_PHASE_SELF 2

typedef struct {
	CONST B *name; /* 区分の名前。失敗したときに場所が分かるように出す */
	W order;       /* 実行順。小さい方が先 */
	W phase;       /* TST_PHASE_* */
	void (*run)(void);
} TestGroup;

/* 登録の実体は**ポインタの配列**にする。記述子そのものを並べると、
 * リンカが構造体の間に詰め物を入れたときに歩幅が狂う。 */
#define BTRON_TEST_GROUP(nm, ord, ph, fn)                                  \
	LOCAL void fn(void);                                                   \
	LOCAL CONST TestGroup tst_desc_##fn = {(nm), (ord), (ph), (fn)};       \
	LOCAL CONST TestGroup *CONST tst_ent_##fn                              \
	    __attribute__((used, section("btron_tests"))) = &tst_desc_##fn;    \
	LOCAL void fn(void)

/* 登録された区分のうち、文脈の合うものを order 順に走らせる。 */
IMPORT void tst_run_phase(W phase);

/* ------------------------------------------------------------------ */
/* 区分をまたいで使う道具（実体は common.c）                           */
/* ------------------------------------------------------------------ */

/* 実行順の記録。タスクが走った順に 1 文字ずつ積む。 */
IMPORT B tst_seq_log[];
IMPORT W tst_seq_n;
IMPORT void tst_seq_reset(void);
IMPORT void tst_seq_put(B c);
IMPORT Bool tst_seq_eq(CONST B *s);
IMPORT void tst_seq_show(CONST B *expect);
IMPORT W tst_seq_transitions(void);

IMPORT void tst_put_dec(W v);
IMPORT ID tst_make_task(TASKENTRY entry, PRI pri, VP exinf);
IMPORT void tst_spin_ms(W ms);
IMPORT UD tst_free_pages_settled(void);
IMPORT Bool tst_scr_solid(GID g, W l, W t, W r, W b, UW val);

/* 記録を取るだけのタスク（複数の区分から使う） */
IMPORT void tst_t_mark_b(INT stacd, VP exinf);
IMPORT void tst_t_rel_a(INT stacd, VP exinf);
IMPORT void tst_t_tag(INT stacd, VP exinf);

/* 起こされるまで寝ているプロセス */
IMPORT W tst_p_sleeper(MESSAGE *msg);

/* フォントマネージャ。mgr が立て、window が使い、mgr が止める。 */
IMPORT W tst_fontman_pid;
IMPORT W tst_fontman_por;

/* ファイルシステムの仕掛け。file が接続し、exec が使い、
 * file_detach が切り離す。 */
IMPORT CONST TC tst_nm_conn[];
IMPORT CONST TC tst_nm_f3[];
IMPORT CONST TC tst_path_abs[];
IMPORT LINK tst_fl_root;
IMPORT LINK tst_fl_a;
IMPORT LINK tst_fl_b;
IMPORT Bool tst_fs_up;

IMPORT CONST TC tst_nm_screen[];
IMPORT CONST TC tst_nm_test[];
IMPORT CONST TC tst_nm_rda0[];
IMPORT CONST TC tst_nm_rda1[];
IMPORT CONST TC tst_nm_rda2[];

#endif /* __BTRON_TESTSUITE_H__ */

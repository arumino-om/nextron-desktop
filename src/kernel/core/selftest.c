/*
 * selftest.c — 自己検査の入口
 *
 * 検査の中身はここには無い。**区分ごとに src/kernel/core/tests/ に
 * 分かれていて、リンカのセクションに登録される**（W0-1。testsuite.h）。
 * ここに残っているのは走らせる文脈を用意する部分だけである。
 *
 * 文脈が 2 つあるのが要点。
 *
 *   TST_PHASE_KERNEL  プロセスに属さないカーネルタスク。
 *                     中心核の検査は「同一優先度内の実行順」を 1 文字ずつ
 *                     比べているので、10ms ごとに同一優先度の末尾へ回される
 *                     プロセス内のタスクでは落ちてしまう（Phase 2 ㉞）
 *   TST_PHASE_PROC    初期プロセスの中。周辺核より上の層はこちら
 *
 * どちらもタスクの中で走らせなければならない。待ちに入るシステムコールは
 * タスク独立部（起動文脈や割込みハンドラ）から呼ぶと E_CTX になる。
 */

#include <btron/bootinfo.h>
#include <btron/proc.h>
#include <btron/startup.h>
#include <btron/test.h>
#include <btron/window.h>

#include "knl.h"
#include "tests/testsuite.h"

/* ------------------------------------------------------------------ */
/* 初期プロセスの中で走らせる分                                        */
/* ------------------------------------------------------------------ */

LOCAL ID proc_test_waiter;

/* 初期プロセスのメインタスク。形式-A の MAIN()。 */
LOCAL W p_test_main(MESSAGE *msg)
{
	if ((bootinfo()->flags & BI_F_SELFBOOT) != 0) {
		/* **本来の起動列を通す**（D8 / startup.c）。
		 * ドライバを登録し、BTRON 区画を接続し（未構築なら構築し）、
		 * ディスク上の実行プログラムから外殻を起こす。 */
		(void)sys_startup();
		tst_run_phase(TST_PHASE_SELF);
		(void)tk_wup_tsk(proc_test_waiter);
		return 0;
	}
	BTRON_TEST("起動時メッセージ (§1.1.6)", {
		BTRON_CHECK("空メッセージが渡る", msg != 0);
		if (msg != 0) {
			BTRON_CHECK("タイプ", msg->msg_type == 0);
			BTRON_CHECK("大きさ", msg->msg_size == 0);
		}
	});
	tst_run_phase(TST_PHASE_PROC);
	/* 検査タスクを起こす。周辺核の wup_tsk は自プロセス内に限るので、
	 * 中心核の方を直に呼ぶ（ここはカーネル内部の検査コード）。 */
	(void)tk_wup_tsk(proc_test_waiter);
	return 0;
}

/* 周辺核以上の検査を初期プロセスの中で走らせる。
 *
 * **検査タスクは待ちに入る。** 単に優先度を下げて譲るだけにすると、
 * 初期プロセスが chg_pri で自分の優先度を動かす検査の最中に、
 * 検査タスクが実行可能なまま横から選ばれてしまい、§1.1 の検査が
 * 途中で打ち切られる。待ち状態にして、初期プロセスが終わるときに
 * 起こしてもらう。 */
LOCAL void run_proc_phase(void)
{
	CONST TC name[] = {(TC)'I', (TC)'N', (TC)'I', (TC)'T', TNULL};
	W pid;

	proc_test_waiter = tk_get_tid();
	/* 優先度 2（群A）は中心核優先度 3 に写る。検査タスク（中心核 5）より
	 * 高いので、生成した瞬間に初期プロセスへ切り替わる。 */
	pid = prc_create(p_test_main, 2, name, 0);
	if (pid < 1) {
		btron_test_begin("初期プロセスの生成");
		btron_test_fail("初期プロセスの生成", "prc_create() >= 1",
		                __FILE__, __LINE__);
		btron_test_end("初期プロセスの生成");
		return;
	}
	/* 初期プロセスが検査を終えて tk_wup_tsk するまで待つ。 */
	(void)tk_slp_tsk(TMO_FEVR);
}

/* ------------------------------------------------------------------ */
/* 入口                                                                */
/* ------------------------------------------------------------------ */

LOCAL void test_task(INT stacd, VP exinf)
{
	W code;

	(void)stacd;
	(void)exinf;
	/* SELFBOOT の印があるときは中心核の検査を飛ばす。
	 * 起動列は初期プロセスの中で走らせる（ファイル管理を使う）。 */
	if ((bootinfo()->flags & BI_F_SELFBOOT) == 0) {
		tst_run_phase(TST_PHASE_KERNEL);
	}
	run_proc_phase();
	code = btron_test_report();

	/* ESP に HOLD の印があれば、電源を落とさず画面を残す。
	 * 手で画面を見るための経路（run_qemu.py --gui）。 */
	if ((bootinfo()->flags & BI_F_HOLD) != 0) {
		hal_console_puts("\n  HOLD の印があるので、電源を落とさず画面を"
		                 "残します\n");
		win_demo_hold(); /* 戻らない */
	}

	/* ESP に印があれば、ここでわざと例外を起こす（T4-02）。
	 *
	 * **緊急画面は「例外から呼ばれても描けること」が要件**なので、
	 * 描画だけ確かめても足りない。例外ベクタ → hal_exception_report →
	 * screen_panic_begin という本物の経路を 1 度通す。
	 *
	 * 検査が全部終わってから落とすので、`[ DONE ] n/n` は先に出ている。
	 * 終了コードは hal_exit(2)。ctest 側は --expect-exit 2 で受ける。 */
	if ((bootinfo()->flags & BI_F_PANIC_TEST) != 0) {
		volatile UW *bad;

		hal_console_puts("\n  PANICTEST の印があるので、ここでわざと例外を"
		                 "起こします\n");
		hal_console_puts("  （この直後の例外が画面に出れば成功です）\n");
		/* 上位半分の、何も写像していない番地を読む。
		 * 変換フォルトになり、同期例外の経路へ入る。 */
		bad = (volatile UW *)(VP)(BTRON_RESERVED_BASE + 0x1000ULL);
		code = (W)*bad;
	}
	hal_exit(code);
}

EXPORT void knl_selftest_start(void)
{
	ID id;

	/* 検査タスクは中くらいの優先度にする。上にも下にもタスクを置けるように。 */
	id = tst_make_task(test_task, 5, 0);
	if (id < 1) {
		hal_console_puts("  検査タスクを作れない\n");
		hal_exit(1);
	}
	if (tk_sta_tsk(id, 0) != E_OK) {
		hal_console_puts("  検査タスクを起動できない\n");
		hal_exit(1);
	}
	knl_start(); /* 戻らない */
}

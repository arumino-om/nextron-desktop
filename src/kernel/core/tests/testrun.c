/*
 * testrun.c — 登録された検査区分を order 順に走らせる
 *
 * 登録はリンカが `btron_tests` セクションに集める（testsuite.h）。
 * 範囲を示す __btron_tests_start / __btron_tests_end はリンカスクリプトが
 * 作る（アーキごとの kernel.ld）。**セクションを拾い忘れると検査が 0 件に
 * なって静かに通ってしまう**ので、1 件も無いことは失敗として報告する。
 */

#include <btron/hal.h>
#include <btron/test.h>

#include "testsuite.h"

IMPORT CONST TestGroup *CONST __btron_tests_start[];
IMPORT CONST TestGroup *CONST __btron_tests_end[];

#define TST_MAX_GROUPS 64

EXPORT void tst_run_phase(W phase)
{
	CONST TestGroup *sel[TST_MAX_GROUPS];
	W n = 0;
	W total;
	W i;
	W j;

	total = (W)(__btron_tests_end - __btron_tests_start);
	if (total <= 0) {
		btron_test_begin("検査区分の登録");
		btron_test_fail("検査区分の登録", "btron_tests が空ではない",
		                __FILE__, __LINE__);
		btron_test_end("検査区分の登録");
		return;
	}

	for (i = 0; i < total; i++) {
		CONST TestGroup *g = __btron_tests_start[i];

		if (g == 0 || g->phase != phase) {
			continue;
		}
		if (n >= TST_MAX_GROUPS) {
			btron_test_begin("検査区分の登録");
			btron_test_fail("検査区分の登録", "区分の数が TST_MAX_GROUPS 以内",
			                __FILE__, __LINE__);
			btron_test_end("検査区分の登録");
			break;
		}
		/* order の昇順に挿入する。リンク順に依存させない。 */
		for (j = n; j > 0 && sel[j - 1]->order > g->order; j--) {
			sel[j] = sel[j - 1];
		}
		sel[j] = g;
		n++;
	}

	for (i = 0; i < n; i++) {
		hal_console_puts("--- ");
		hal_console_puts(sel[i]->name);
		hal_console_puts(" ---\n");
		sel[i]->run();
	}
}

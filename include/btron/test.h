/*
 * test.h — 極小のテストハーネス
 *
 * freestanding でも使える。結果は HAL のコンソールに出し、
 * 終了コードで合否を返す。tools/run_qemu.py がこれを判定する。
 *
 * 出力の約束 (tools/run_qemu.py が解釈する):
 *   "[ RUN  ] <名前>"
 *   "[ FAIL ] <名前>: <式>  (<ファイル>:<行>)"
 *   "[ PASS ] <名前>"
 *   "[ DONE ] <合格数>/<総数>"
 */

#ifndef __BTRON_TEST_H__
#define __BTRON_TEST_H__

#include <btron/hal.h>

IMPORT W btron_test_total;
IMPORT W btron_test_failed;

IMPORT void btron_test_begin(CONST B *name);
IMPORT void btron_test_fail(CONST B *name, CONST B *expr, CONST B *file, W line);
IMPORT void btron_test_end(CONST B *name);
IMPORT W btron_test_report(void);

/* 1 つの検査。失敗してもそのテストは続行する。 */
#define BTRON_CHECK(name, expr)                                            \
	do {                                                                   \
		if (!(expr)) {                                                     \
			btron_test_fail((name), #expr, __FILE__, __LINE__);            \
		}                                                                  \
	} while (0)

#define BTRON_TEST(name, body)                                             \
	do {                                                                   \
		CONST B *btron__n = (name);                                        \
		btron_test_begin(btron__n);                                        \
		{ body }                                                           \
		btron_test_end(btron__n);                                          \
	} while (0)

#endif /* __BTRON_TEST_H__ */

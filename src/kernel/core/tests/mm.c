/*
 * mm.c — 物理メモリとカーネルヒープ
 *
 * selftest.c から切り出したもの（W0-1）。区分の登録は
 * testsuite.h の BTRON_TEST_GROUP で行い、実行順は order で決まる。
 * order の帯は docs/registry.md で予約している。
 */

#include <btron/bootinfo.h>
#include <btron/device.h>
#include <btron/dp.h>
#include <btron/event.h>
#include <btron/file.h>
#include <btron/fontman.h>
#include <btron/machdesc.h>
#include <btron/manager.h>
#include <btron/objfmt.h>
#include <btron/screen.h>
#include <btron/system.h>
#include <btron/test.h>
#include <btron/virtio.h>
#include <btron/window.h>
#include <btron/winmsg.h>

#include "../knl.h"
#include "testsuite.h"

BTRON_TEST_GROUP("物理メモリとカーネルヒープ", 170, TST_PHASE_KERNEL, tst_mm)
{
	BTRON_TEST("削除すると物理メモリが戻る (Phase 2 の土台)", {
		if (pmem_total_pages() == 0) {
			/* メモリマップが無い構成。アロケータが立たないので返せない。 */
			BTRON_CHECK("アロケータ無し", pmem_free_pages() == 0);
		} else {
			UD free0 = pmem_free_pages();
			ID id;
			T_CMPF cf;
			T_CMPL cl;
			W i;

			for (i = 0; i < 8; i++) {
				cf.dsname[i] = 0;
				cl.dsname[i] = 0;
			}

			/* ① タスクのスタック。生成で減り、削除で戻る。
			 * ここが戻らないとタスクを作っては消すたびに漏れる。 */
			id = tst_make_task(tst_t_mark_b, 20, 0);
			BTRON_CHECK("生成で減った", pmem_free_pages() < free0);
			BTRON_CHECK("削除", tk_del_tsk(id) == E_OK);
			BTRON_CHECK("スタックが戻った", pmem_free_pages() == free0);

			/* ② 固定長メモリプール */
			cf.exinf = 0;
			cf.mpfatr = TA_TFIFO;
			cf.mpfcnt = 16;
			cf.blfsz = 256;
			cf.bufptr = 0;
			id = tk_cre_mpf(&cf);
			BTRON_CHECK("生成", id >= 1);
			BTRON_CHECK("減った", pmem_free_pages() < free0);
			BTRON_CHECK("削除", tk_del_mpf(id) == E_OK);
			BTRON_CHECK("戻った", pmem_free_pages() == free0);

			/* ③ 可変長メモリプール */
			cl.exinf = 0;
			cl.mplatr = TA_TFIFO;
			cl.mplsz = 8192;
			cl.bufptr = 0;
			id = tk_cre_mpl(&cl);
			BTRON_CHECK("生成", id >= 1);
			BTRON_CHECK("減った", pmem_free_pages() < free0);
			BTRON_CHECK("削除", tk_del_mpl(id) == E_OK);
			BTRON_CHECK("戻った", pmem_free_pages() == free0);

			/* ④ 繰り返しても漏れない。**これが本題。**
			 * 1 回だけなら偶然同じ数になることもあるが、
			 * 100 回繰り返して変わらなければ漏れていない。 */
			for (i = 0; i < 100; i++) {
				ID t = tst_make_task(tst_t_mark_b, 20, 0);

				if (t < 1 || tk_del_tsk(t) != E_OK) {
					break;
				}
			}
			BTRON_CHECK("100 回繰り返しても漏れない",
			            i == 100 && pmem_free_pages() == free0);
		}
	});

	BTRON_TEST("中心核が動いている", {
		BTRON_CHECK("起動済み", knl_running() == True);
		BTRON_CHECK("自タスクがいる", tk_get_tid() >= 1);
		BTRON_CHECK("アイドルではない", knl_cur != &knl_idle);
	});
}

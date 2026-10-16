/*
 * screen.c — 緊急画面 (E24)
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

BTRON_TEST_GROUP("緊急画面 (E24)", 160, TST_PHASE_KERNEL, tst_screen)
{
	BTRON_TEST("緊急画面 (E24 / T4-02 / 規律 R18)", {
		CONST Screen *sc = screen();

		if (sc == 0) {
			/* 画面が無い構成。begin は False を返し、何も起きない。 */
			BTRON_CHECK("画面が無ければ何もしない",
			            screen_panic_begin("test") == False);
			BTRON_CHECK("占有していない", screen_panic_active() == False);
		} else {
			UW before = screen_get(10, 300);
			UW after;

			BTRON_CHECK("占有できた", screen_panic_begin("*** 検査 ***") == True);
			BTRON_CHECK("占有中", screen_panic_active() == True);
			/* 画面が塗り替わったこと。**画素を読み返して確かめる**ので
			 * スクリーンショット比較が要らない（Phase 1 ㉗）。 */
			after = screen_get(10, 300);
			BTRON_CHECK("面が変わった", after != before || before == 0);
			BTRON_CHECK("緊急画面の地", after == screen_panic_bg());
			/* 普通のコンソールと地の色が違うこと。一目で区別できる必要がある。 */
			BTRON_CHECK("普段と違う地", screen_panic_bg() != screen_rgb(0x10, 0x14, 0x20));
			/* 見出しの下の罫が引かれていること */
			BTRON_CHECK("罫がある",
			            screen_get(400, 2 * 16 + 6) != screen_panic_bg());

			/* 以後の hal_console_puts() が緊急画面へ流れる。
			 * 文字が描かれたかは、行の中に前景色の画素があるかで見る。 */
			hal_console_puts("緊急画面の検査中\n");
			{
				W x;
				Bool drew = False;

				for (x = 16; x < 400; x++) {
					W y;

					for (y = 3 * 16 + 4; y < 4 * 16 + 4; y++) {
						if (screen_get(x, y) == screen_panic_fg()) {
							drew = True;
						}
					}
				}
				BTRON_CHECK("文字が描かれた", drew == True);
			}

			/* 畳んで普通の画面コンソールに戻す。
			 * 本物のパニックでは戻らないので、ここは検査のためだけ。 */
			screen_panic_end();
			BTRON_CHECK("畳んだ", screen_panic_active() == False);
			screen_clear(screen_rgb(0x10, 0x14, 0x20));
			hal_console_puts("  （緊急画面の検査が終わり、画面を戻しました）\n");
		}
	});

}

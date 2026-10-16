/*
 * input.c — 入力装置ドライバ (virtio-input)
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

BTRON_TEST_GROUP("入力装置ドライバ (virtio-input)", 330, TST_PHASE_PROC, tst_input)
{
	BTRON_TEST("イベント管理のドライバ経路", {
		KeyTab *kt;
		VP mem = 0;
		EVENT ev;
		PNT p;

		(void)chg_emk(EM_ALL);
		(void)clr_evt(EM_ALL, EM_NULL);

		/* ドライバが登録する形の変換表を手で作る。
		 * キートップ 30 (evdev の KEY_A) を素で 'a'、
		 * 左シフトで 'A'、命令シフトで 0x01 にする。 */
		if (get_mbk(&mem, 4, 0) == ER_OK) {
			W i;

			kt = (KeyTab *)mem;
			kt->keymax = 256;
			kt->kctmax = 3;
			for (i = 0; i < KCTSEL; i++) {
				if ((i & KM_C) != 0) {
					kt->kctsel[i] = 2;
				} else if ((i & (KM_L | KM_R)) != 0) {
					kt->kctsel[i] = 1;
				} else {
					kt->kctsel[i] = 0;
				}
			}
			for (i = 0; i < KCTMAX; i++) {
				kt->kct[i] = 0;
			}
			kt->kct[0 * 256 + 30] = (UH)'a';
			kt->kct[1 * 256 + 30] = (UH)'A';
			kt->kct[2 * 256 + 30] = 0x01;
			evt_set_kbd(0, kt);
			(void)rel_mbk(mem);
		}

		/* メタキーはイベントを発生しない（仕様 §1.7.2）。 */
		evt_meta(ES_LSHFT, ES_LSHFT);
		BTRON_CHECK("メタキーではイベントが起きない", evt_count() == 0);

		/* シフト中に押すと表 1 が引かれる。 */
		evt_key(30, True);
		BTRON_CHECK("押し下げが積まれた", evt_count() == 1);
		BTRON_CHECK("取り出し",
		            get_evt(EM_KEYDWN, &ev, CLR) == EV_KEYDWN);
		BTRON_CHECK("キートップコードはそのまま", ev.data.key.keytop == 30);
		BTRON_CHECK("左シフトで 'A'", ev.data.key.code == (TC)'A');
		BTRON_CHECK("stat に左シフト", (ev.stat & ES_LSHFT) != 0);

		/* キー状態配列はドライバ経路でも更新される。 */
		{
			KeyMap km;

			BTRON_CHECK("キー状態が取れる", get_kmp(km) == ER_OK);
			BTRON_CHECK("30 番が押されている",
			            (km[30 / 8] & (1U << (30 % 8))) != 0);
		}

		evt_key(30, False);
		BTRON_CHECK("離した", get_evt(EM_KEYUP, &ev, CLR) == EV_KEYUP);
		evt_meta(0, ES_LSHFT);

		/* 命令シフトは表 2。**ビットの取り違えがここで出る。** */
		evt_meta(ES_CMD, ES_CMD);
		evt_key(30, True);
		BTRON_CHECK("命令シフトで 0x01",
		            get_evt(EM_KEYDWN, &ev, CLR) == EV_KEYDWN &&
		                ev.data.key.code == 0x01);
		evt_key(30, False);
		(void)get_evt(EM_KEYUP, &ev, CLR);
		evt_meta(0, ES_CMD);

		/* シフト無しは表 0。 */
		evt_key(30, True);
		BTRON_CHECK("素で 'a'",
		            get_evt(EM_KEYDWN, &ev, CLR) == EV_KEYDWN &&
		                ev.data.key.code == (TC)'a');
		evt_key(30, False);
		(void)get_evt(EM_KEYUP, &ev, CLR);

		/* **マスクで消されてもキー状態配列は更新される。**
		 *
		 * システムイベントマスクはイベントの話で、キーが物理的に
		 * 押されていることとは別。ここを混ぜると、マスクを絞った
		 * アプリから get_kmp が嘘を返す。 */
		{
			KeyMap km;

			(void)chg_emk(EM_ALL & ~(W)EM_KEYDWN);
			evt_key(31, True);
			BTRON_CHECK("マスク中はイベントにならない", evt_count() == 0);
			BTRON_CHECK("それでもキー状態は立つ",
			            get_kmp(km) == ER_OK &&
			                (km[31 / 8] & (1U << (31 % 8))) != 0);
			evt_key(31, False);
			BTRON_CHECK("離すと落ちる",
			            get_kmp(km) == ER_OK &&
			                (km[31 / 8] & (1U << (31 % 8))) == 0);
			(void)chg_emk(EM_ALL);
			(void)clr_evt(EM_ALL, EM_NULL);
		}

		/* PD の移動はイベントを起こさない（そういうタイプが無い）。 */
		evt_set_pda(PD_REL);
		evt_pd_moved((PNT){40, 50});
		BTRON_CHECK("移動ではイベントが起きない", evt_count() == 0);
		BTRON_CHECK("位置が入った",
		            get_pdp(&p) == ER_OK && p.x == 40 && p.y == 50);
		evt_pd_delta(5, -10);
		BTRON_CHECK("相対移動", get_pdp(&p) == ER_OK && p.x == 45 &&
		                            p.y == 40);
		/* 画面の外へは出ない。負の側は 0 で止まる。 */
		evt_pd_delta(-1000, -1000);
		BTRON_CHECK("左上で止まる",
		            get_pdp(&p) == ER_OK && p.x == 0 && p.y == 0);

		/* メインボタンはイベントになり、サブボタンはならない。
		 *
		 * **先に離しておく。** §1.7 の検査が put_evt(EV_BUTDWN) を
		 * 使っていて ES_BUT が立ったままになっているので、
		 * ここで初期状態を揃えないと下の確認が意味を失う。 */
		evt_pd_button(False, False);
		(void)clr_evt(EM_ALL, EM_NULL);
		evt_pd_button(False, True);
		BTRON_CHECK("ボタン押しが来た",
		            get_evt(EM_BUTDWN, &ev, CLR) == EV_BUTDWN);
		BTRON_CHECK("発生時の stat には ES_BUT が無い",
		            (ev.stat & ES_BUT) == 0);
		/* ボタンを押した**後**の状態には ES_BUT が立っている。
		 * 次のイベントの stat に載ることで確かめる。 */
		evt_key(30, True);
		BTRON_CHECK("押した後の stat には ES_BUT がある",
		            get_evt(EM_KEYDWN, &ev, CLR) == EV_KEYDWN &&
		                (ev.stat & ES_BUT) != 0);
		evt_key(30, False);
		(void)get_evt(EM_KEYUP, &ev, CLR);
		evt_pd_button(False, False);
		(void)get_evt(EM_BUTUP, &ev, CLR);

		evt_pd_button(True, True);
		BTRON_CHECK("サブボタンではイベントが起きない", evt_count() == 0);
		evt_pd_button(True, False);

		/* 絶対動作タイプにすると set_pdp が断られる（仕様 §1.7.3）。 */
		evt_set_pda(PD_ABS);
		BTRON_CHECK("絶対動作では位置設定不可",
		            set_pdp((PNT){0, 0}) == ER_OBJ);
		evt_set_pda(PD_REL);

		(void)chg_emk(EM_NULL);
		(void)clr_evt(EM_ALL, EM_NULL);
	});

	BTRON_TEST("virtio-input の接続 (R19: 記述から取る)", {
		CONST MachDesc *md = machdesc();
		ERR er;
		W n;
		W i;
		W probed = 0;

		hal_console_puts("    virtio 枠 : ");
		tst_put_dec((W)md->virtio_count);
		hal_console_puts(" 個\n");

		/* 枠に挿さっている virtio-input を先に数えて見せる。
		 * **これと「使えた台数」が食い違ったら、見つけたのに
		 * 動かせなかったということ**で、原因の切り分けに効く。
		 * （実際に version 1 = 旧式インタフェースで引っかかった。） */
		{
			VirtioDev f[VIN_PROBE_MAX];
			W k = virtio_mmio_probe(VIRTIO_ID_INPUT, f, VIN_PROBE_MAX);
			W j;

			hal_console_puts("    入力枠    : ");
			tst_put_dec(k);
			hal_console_puts(" 台");
			for (j = 0; j < k; j++) {
				hal_console_puts(" (版 ");
				tst_put_dec((W)f[j].version);
				hal_console_puts(" INTID ");
				tst_put_dec((W)f[j].intid);
				hal_console_puts(")");
			}
			hal_console_puts("\n");
			probed = k;
		}
		er = vinput_register();
		n = vinput_count();
		hal_console_puts("    入力装置  : ");
		tst_put_dec(n);
		hal_console_puts(" 台");
		for (i = 0; i < n; i++) {
			switch (vinput_kind(i)) {
			case VIN_KIND_KBD:
				hal_console_puts(" キーボード");
				break;
			case VIN_KIND_ABS:
				hal_console_puts(" PD(絶対)");
				break;
			case VIN_KIND_REL:
				hal_console_puts(" PD(相対)");
				break;
			default:
				hal_console_puts(" 不明");
				break;
			}
		}
		hal_console_puts("\n");

		if (md->virtio_count == 0) {
			/* ACPI 経路では枠の記述が AML の中にあり、まだ読めない。
			 * **「装置が無い」と「読めていない」は違う**ので、
			 * ここで区別して記録しておく。 */
			BTRON_CHECK("枠が無ければ装置も無い",
			            er == ER_NOEXS && n == 0);
		} else {
			/* 枠はあるが装置を挿していない構成もありうる。
			 * 挿してあれば ER_OK、無ければ ER_NOEXS。 */
			BTRON_CHECK("結果は OK か未存在",
			            er == ER_OK || er == ER_NOEXS);
			BTRON_CHECK("台数と結果が一致",
			            (er == ER_OK) == (n > 0));
			/* 見つけた台数と動かせた台数は一致していなければならない。
			 * ここが食い違うのは「旧式インタフェースで断った」場合で、
			 * 黙って 0 台になるのを防ぐための検査。 */
			BTRON_CHECK("見つけた分だけ動かせた", probed == n);
		}

		if (n > 0) {
			KBD_ID kid;
			Bool kbd = False;
			Bool pd = False;
			TC nm[L_DEVNM + 1];
			W d;

			for (i = 0; i < n; i++) {
				if (vinput_kind(i) == VIN_KIND_KBD) {
					kbd = True;
				} else {
					pd = True;
				}
			}
			Bool dev_kbd = False;
			Bool dev_pd = False;
			EVENT ev2;

			if (kbd) {
				BTRON_CHECK("キーボード ID が入った",
				            get_kid(&kid) == ER_OK &&
				                kid.kind ==
				                    (UH)(KB_IBM101 << KB_KIND_SHIFT));
				/* ドライバが入れた既定の変換表が引けること。
				 * evdev の KEY_A = 30 が素で 'a' になる。
				 * **ここが通れば配列表とキートップコードの約束が
				 * 両側で一致している。** */
				(void)chg_emk(EM_ALL);
				(void)clr_evt(EM_ALL, EM_NULL);
				evt_key(30, True);
				BTRON_CHECK("既定の変換表で 'a'",
				            get_evt(EM_KEYDWN, &ev2, CLR) ==
				                    EV_KEYDWN &&
				                ev2.data.key.code == (TC)'a');
				evt_key(30, False);
				(void)get_evt(EM_KEYUP, &ev2, CLR);
				(void)chg_emk(EM_NULL);
				(void)clr_evt(EM_ALL, EM_NULL);
			}
			/* デバイス管理にも名前が出ていること (§1.8)。 */
			for (d = 1; d <= DEV_MAX_UNIT; d++) {
				if (get_dev(nm, d) < 0) {
					continue;
				}
				if (nm[0] == (TC)'k' && nm[1] == (TC)'b' &&
				    nm[2] == (TC)'d') {
					dev_kbd = True;
				}
				if (nm[0] == (TC)'p' && nm[1] == (TC)'d' &&
				    nm[2] == TNULL) {
					dev_pd = True;
				}
			}
			BTRON_CHECK("キーボードは \"kbd\" で登録される",
			            kbd == dev_kbd);
			BTRON_CHECK("PD は \"pd\" で登録される", pd == dev_pd);
		}
	});

	/* -------------------------------------------------------------- */
	/* 外殻 §3.1 ウィンドウマネージャ                                 */
	/* -------------------------------------------------------------- */

}

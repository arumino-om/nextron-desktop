/*
 * dp_ptr.c — ディスプレイプリミティブ ポインタ制御 (§2.6)
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

LOCAL PTRIMG ptr_img; /* ポインタイメージ。1KB 超なのでスタックに置かない */

BTRON_TEST_GROUP("ディスプレイプリミティブ ポインタ制御 (§2.6)", 270, TST_PHASE_PROC, tst_dp_ptr)
{
	BTRON_TEST("ポインタの状態遷移 (§2.6.3)", {
		PTRSTS ps;

		BTRON_CHECK("初期化", gini_ptr() == EG_OK);
		BTRON_CHECK("初期状態",
		            gget_ptr(&ps, 0) == 16 && ps.style == PS_SELECT &&
		                ps.motion == 1 && ps.hidden == 0);

		/* 仕様「消去要求では、ポインタの消去要求数を＋１して消去する。
		 * 表示要求では、表示要求数が 0 でない場合、−１して 0 になった
		 * 場合に表示を行なう」。**カウンタであることが肝。**
		 * 単なる真偽値にすると、入れ子になった描画で早く出てしまう。 */
		BTRON_CHECK("消去で 1", gdsp_ptr(0) == 1);
		BTRON_CHECK("もう一度消去で 2", gdsp_ptr(0) == 2);
		BTRON_CHECK("表示で 1", gdsp_ptr(1) == 1);
		BTRON_CHECK("まだ消えている", gget_ptr(&ps, 0) == 16 &&
		                                  ps.hidden == 1);
		BTRON_CHECK("表示で 0", gdsp_ptr(1) == 0);
		/* 0 のときに表示要求を出しても負にはならない。 */
		BTRON_CHECK("0 から下がらない", gdsp_ptr(1) == 0);
		BTRON_CHECK("消去を 3 回", gdsp_ptr(0) == 1 && gdsp_ptr(0) == 2 &&
		                               gdsp_ptr(0) == 3);
		/* 仕様「強制表示要求では、表示要求数を 0 としてポインタの表示を
		 * 行なう」。入れ子を数え損ねたときの逃げ道。 */
		BTRON_CHECK("強制表示で 0", gdsp_ptr(-1) == 0);

		/* 追従 / 非追従。 */
		BTRON_CHECK("非追従にする", gmot_ptr(0) == EG_OK);
		BTRON_CHECK("状態が変わった",
		            gget_ptr(&ps, 0) == 16 && ps.motion == 0);
		/* 仕様「非追従の時に非追従指定を行なった場合は何もしない」 */
		BTRON_CHECK("もう一度非追従", gmot_ptr(0) == EG_OK);
		BTRON_CHECK("追従に戻す", gmot_ptr(1) == EG_OK);
		/* 仕様「追従状態の場合は、何もしない。関数値は 1」 */
		{
			PNT z;

			z.x = 0;
			z.y = 0;
			BTRON_CHECK("追従中は動かせない", gmov_ptr(z) == 1);
		}
	});

	BTRON_TEST("ポインタ形状の指定 (§2.6.3 gset_ptr)", {
		PTRSTS ps;
		W i;

		for (i = 0; i < PS_STD_COUNT; i++) {
			if (gset_ptr(i, 0, COL_BLACK, COL_WHITE) != EG_OK) {
				BTRON_CHECK("標準形状の設定", False);
			}
		}
		BTRON_CHECK("形状が入った",
		            gget_ptr(&ps, 0) == 16 && ps.style == PS_MENU);
		/* 仕様「= 16 〜 : 予約」 */
		BTRON_CHECK("予約された形状",
		            gset_ptr(PS_STD_COUNT, 0, COL_BLACK, COL_WHITE) ==
		                EG_PAR);
		/* 仕様「style<0 の場合は、img で指定したポインタイメージが有効」。
		 * アドレスを渡さなければ EG_ADR。 */
		BTRON_CHECK("イメージが無い",
		            gset_ptr(-1, 0, COL_BLACK, COL_WHITE) == EG_ADR);

		ptr_img.hotpt.x = 0;
		ptr_img.hotpt.y = 0;
		ptr_img.size.h = 0;
		ptr_img.size.v = 0;
		BTRON_CHECK("大きさ 0",
		            gset_ptr(-1, &ptr_img, COL_BLACK, COL_WHITE) == EG_PAR);
		ptr_img.size.h = MAX_PTR_SIZE + 1;
		ptr_img.size.v = MAX_PTR_SIZE + 1;
		BTRON_CHECK("大きさが上限超え",
		            gset_ptr(-1, &ptr_img, COL_BLACK, COL_WHITE) == EG_PAR);
		/* ポインタ形状は正方形として定義されている。 */
		ptr_img.size.h = 8;
		ptr_img.size.v = 16;
		BTRON_CHECK("正方形でない",
		            gset_ptr(-1, &ptr_img, COL_BLACK, COL_WHITE) == EG_PAR);
		ptr_img.size.h = 8;
		ptr_img.size.v = 8;
		ptr_img.hotpt.x = 8;
		BTRON_CHECK("ホットポイントが外",
		            gset_ptr(-1, &ptr_img, COL_BLACK, COL_WHITE) == EG_PAR);
		ptr_img.hotpt.x = 0;
		for (i = 0; i < P_SIZE; i++) {
			ptr_img.data[i] = 0;
			ptr_img.mask[i] = 0;
		}
		BTRON_CHECK("8x8 のイメージを設定",
		            gset_ptr(-1, &ptr_img, COL_BLACK, COL_WHITE) == EG_OK);
		BTRON_CHECK("サイズが追従する",
		            gget_ptr(&ps, 0) == 8 && ps.style == -1);
		/* 片付け。以後の検査は標準形状で動かす。 */
		BTRON_CHECK("標準形状に戻す",
		            gset_ptr(PS_SELECT, 0, COL_BLACK, COL_WHITE) == EG_OK);
	});

	BTRON_TEST("ポインタは下地を退避しない (§2.6.2 / 詳細 E2)", {
		GID sg = gopn_dev(tst_nm_screen, 0);
		RECT bnd;
		RECT bg;
		PAT pat;
		UB pm[1];
		PNT at;
		W i;

		if (sg >= 1 && gget_bnd(sg, &bnd) == EG_OK && bnd.c.right > 200 &&
		    bnd.c.bottom > 200) {
			/* **ポインタは一番上のレイヤである**（詳細 E2）。
			 * 下地を退避して書き戻すのではなく、合成する側が下地を
			 * 作り直してから重ねる。ここでは検査が合成する側を演じる
			 * —— 机を塗る → `gptr_paint` で重ねる、の順である。
			 *
			 * 退避が無いので「消し残し」も「消し過ぎ」も原理的に
			 * 起きない。確かめるのは**重ねたものが正しいか**だけ。 */
			pm[0] = 0x80;
			pat.spat.kind = PAT_SPAT;
			pat.spat.hsize = 1;
			pat.spat.vsize = 1;
			/* crep = 0 (COL_PIXVAL) なのでピクセル値がそのまま入る。 */
			pat.spat.fgcol = 0x00123456;
			pat.spat.bgcol = 0x00123456;
			pat.spat.mask = pm;
			bg.c.left = 96;
			bg.c.top = 96;
			bg.c.right = 128;
			bg.c.bottom = 128;

			BTRON_CHECK("非追従にする", gmot_ptr(0) == EG_OK);
			BTRON_CHECK("強制表示", gdsp_ptr(-1) == 0);

			/* 8x8 を全部前景色で埋めたイメージ。重なりが見分けやすい。 */
			ptr_img.hotpt.x = 0;
			ptr_img.hotpt.y = 0;
			ptr_img.size.h = 8;
			ptr_img.size.v = 8;
			for (i = 0; i < P_SIZE; i++) {
				ptr_img.data[i] = 0xFF;
				ptr_img.mask[i] = 0xFF;
			}
			BTRON_CHECK("塗り潰しイメージを設定",
			            gset_ptr(-1, &ptr_img, COL_BLACK, COL_WHITE) ==
			                EG_OK);

			at.x = 100;
			at.y = 100;
			BTRON_CHECK("置く", gmov_ptr(at) == 0);
			BTRON_CHECK("下地を塗る",
			            gfil_rec(sg, bg, &pat, 0, G_STORE) == EG_OK);
			gptr_paint(0);
			BTRON_CHECK("重なった（黒 = 0）",
			            tst_scr_solid(sg, 100, 100, 108, 108, 0));

			/* 動かす。**合成し直すと前の場所は下地に戻る** —— 退避を
			 * 書き戻しているのではなく、机を塗り直しているだけである。 */
			at.x = 104;
			at.y = 104;
			BTRON_CHECK("動かす", gmov_ptr(at) == 0);
			BTRON_CHECK("下地を作り直す",
			            gfil_rec(sg, bg, &pat, 0, G_STORE) == EG_OK);
			gptr_paint(0);
			BTRON_CHECK("前の場所は下地",
			            tst_scr_solid(sg, 100, 100, 104, 108, 0x00123456));
			BTRON_CHECK("新しい場所は前景色",
			            tst_scr_solid(sg, 104, 104, 112, 112, 0));

			/* 消すと重ねない。下地はそのまま残る。 */
			BTRON_CHECK("消す", gdsp_ptr(0) == 1);
			BTRON_CHECK("下地を作り直す",
			            gfil_rec(sg, bg, &pat, 0, G_STORE) == EG_OK);
			gptr_paint(0);
			BTRON_CHECK("消えている",
			            tst_scr_solid(sg, 96, 96, 128, 128, 0x00123456));

			BTRON_CHECK("標準形状に戻す",
			            gset_ptr(PS_SELECT, 0, COL_BLACK, COL_WHITE) ==
			                EG_OK);
			BTRON_CHECK("追従に戻す", gmot_ptr(1) == EG_OK);
			BTRON_CHECK("閉じる", gcls_env(sg) == EG_OK);
		} else if (sg >= 1) {
			BTRON_CHECK("閉じる", gcls_env(sg) == EG_OK);
		}
	});

	BTRON_TEST("ポインタの 4 通りの重ね方 (§2.6.2)", {
		GID sg = gopn_dev(tst_nm_screen, 0);
		RECT bnd;
		PIXVAL mark = 0x00123456;
		PIXVAL v;
		PNT at;
		PNT pfg;
		PNT pbg;
		PNT ptr;
		W i;

		/* 画面が無い構成では何も確かめられない。**その場合でも
		 * 上の状態遷移の検査は走っている**（規律 R27: 構成に依る
		 * 検査と依らない検査を混ぜない）。 */
		if (sg >= 1 && gget_bnd(sg, &bnd) == EG_OK &&
		    bnd.c.right > 200 && bnd.c.bottom > 200) {
			/* 選択指 (PS_SELECT) の先端は (0,0) で、絵は
			 *   行 0 = "X..............."
			 *   行 1 = "XX.............."
			 * なので、ホットポイントを (100,100) に置くと
			 *   (100,100)  data=1 mask=1 → 前景色
			 *   (102,101)  data=0 mask=1 → 背景色（自動の縁取り）
			 *   (115,100)  data=0 mask=0 → 透明（下地のまま）
			 * になる。3 通りすべてを 1 度に確かめられる。
			 *
			 * **重ねるのは `gptr_paint`**（詳細 E2）。本来は合成する側
			 * が呼ぶが、ここでは検査がその役を演じる。 */
			at.x = 100;
			at.y = 100;
			pfg.x = 100;
			pfg.y = 100;
			pbg.x = 102;
			pbg.y = 101;
			ptr.x = 115;
			ptr.y = 100;

			BTRON_CHECK("非追従にする", gmot_ptr(0) == EG_OK);
			BTRON_CHECK("強制表示", gdsp_ptr(-1) == 0);
			BTRON_CHECK("そこへ動かす", gmov_ptr(at) == 0);
			BTRON_CHECK("下地を置く",
			            gdra_pnt(sg, pfg, mark, G_STORE) == EG_OK &&
			                gdra_pnt(sg, pbg, mark, G_STORE) == EG_OK &&
			                gdra_pnt(sg, ptr, mark, G_STORE) == EG_OK);
			gptr_paint(0);
			BTRON_CHECK("先端は前景色（黒 = 0）",
			            gtst_pnt(sg, pfg, &v) == 0 && v == 0);
			BTRON_CHECK("縁は背景色（白）",
			            gtst_pnt(sg, pbg, &v) == 0 &&
			                (UW)v == 0x00FFFFFFU);
			BTRON_CHECK("透明な所は下地のまま",
			            gtst_pnt(sg, ptr, &v) == 0 && v == mark);

			/* 消すと重ねない（下地を作り直した状態がそのまま残る）。 */
			BTRON_CHECK("消す", gdsp_ptr(0) == 1);
			BTRON_CHECK("下地を作り直す",
			            gdra_pnt(sg, pfg, mark, G_STORE) == EG_OK &&
			                gdra_pnt(sg, pbg, mark, G_STORE) == EG_OK);
			gptr_paint(0);
			BTRON_CHECK("先端は下地",
			            gtst_pnt(sg, pfg, &v) == 0 && v == mark);
			BTRON_CHECK("縁も下地",
			            gtst_pnt(sg, pbg, &v) == 0 && v == mark);

			/* data=1 mask=0 は反転 (XOR)。標準形状には現れないので
			 * アプリ定義イメージで確かめる。**反転するのは合成し直した
			 * 下地**なので、退避が要らない。 */
			ptr_img.hotpt.x = 0;
			ptr_img.hotpt.y = 0;
			ptr_img.size.h = 8;
			ptr_img.size.v = 8;
			for (i = 0; i < P_SIZE; i++) {
				ptr_img.data[i] = 0xFF; /* 全部 1 */
				ptr_img.mask[i] = 0;    /* 全部 0 */
			}
			BTRON_CHECK("反転イメージを設定",
			            gset_ptr(-1, &ptr_img, COL_BLACK, COL_WHITE) ==
			                EG_OK);
			BTRON_CHECK("出す", gdsp_ptr(-1) == 0);
			BTRON_CHECK("下地を作り直す",
			            gdra_pnt(sg, pfg, mark, G_STORE) == EG_OK);
			gptr_paint(0);
			BTRON_CHECK("下地が反転した",
			            gtst_pnt(sg, pfg, &v) == 0 &&
			                (UW)v == ((UW)mark ^ 0x00FFFFFFU));

			BTRON_CHECK("標準形状に戻す",
			            gset_ptr(PS_SELECT, 0, COL_BLACK, COL_WHITE) ==
			                EG_OK);
			/* 画面の外は EG_PAR。 */
			{
				PNT out;

				out.x = (H)bnd.c.right;
				out.y = 0;
				BTRON_CHECK("画面の外", gmov_ptr(out) == EG_PAR);
			}
			BTRON_CHECK("追従に戻す", gmot_ptr(1) == EG_OK);
			BTRON_CHECK("閉じる", gcls_env(sg) == EG_OK);
		} else if (sg >= 1) {
			BTRON_CHECK("閉じる", gcls_env(sg) == EG_OK);
		}
	});

}

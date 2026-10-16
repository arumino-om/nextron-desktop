/*
 * file_detach.c — 周辺核 ファイルシステムの切り離し (§1.6.2)
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

BTRON_TEST_GROUP("周辺核 ファイルシステムの切り離し (§1.6.2)", 320, TST_PHASE_PROC, tst_file_detach)
{
	BTRON_TEST("切り離しと後始末 (§1.6.2 / §1.6.5)", {
		if (!tst_fs_up) {
			BTRON_CHECK("ファイルシステムが無いので飛ばす", True);
		} else {
			W fd;

			/* 開いているファイルがあると切り離せない。 */
			fd = opn_fil(&tst_fl_b, F_READ, 0);
			BTRON_CHECK("開く", fd > 0);
			BTRON_CHECK("開いていると切り離せない",
			            det_fls(tst_nm_rda1, 0) == ER_BUSY);
			BTRON_CHECK("閉じる", cls_fil(fd) == ER_OK);

			BTRON_CHECK("同期", syn_fls() == ER_OK);
			BTRON_CHECK("切り離す", det_fls(tst_nm_rda1, 0) == ER_OK);
			BTRON_CHECK("もう無い", det_fls(tst_nm_rda1, 0) == ER_NOEXS);
			BTRON_CHECK("0 個に戻った", lst_fls(0, 0) == 0);

			/* 切断状態のリンクは使えない（仕様の虚身）。 */
			BTRON_CHECK("切断後は使えない",
			            opn_fil(&tst_fl_b, F_READ, 0) == ER_NOFS);

			/* 接続し直すと中身が残っている。 */
			BTRON_CHECK("再接続",
			            att_fls(tst_nm_rda1, tst_nm_conn, 0, FS_SYNC) == ER_OK);
			{
				LINK l;

				BTRON_CHECK("ファイルが残っている",
				            get_lnk(tst_path_abs, &l, F_NORM) == 0 &&
				                l.f_id == tst_fl_a.f_id);
			}
			BTRON_CHECK("片付け", det_fls(tst_nm_rda1, 0) == ER_OK);
			tst_fs_up = False;
		}
	});

	/* -------------------------------------------------------------- */
	/* 入力装置 (virtio-input) とイベント管理のドライバ経路            */
	/*                                                                */
	/* 【規律 R27】ここは 2 段に分かれている。                         */
	/*                                                                */
	/*   (a) ドライバの口（evt_key / evt_meta / evt_pd_*）の検査       */
	/*       … 装置が無くても必ず走る。エンコード・メタキー・          */
	/*          キュー・ボタン状態はここで全部確かめる。               */
	/*   (b) 本物の virtio-input の検査                                */
	/*       … Device Tree に枠があるときだけ走る。                    */
	/*          ctest の input-arm64 構成が担保する。                  */
	/*                                                                */
	/* (a) を (b) に混ぜると、既定の ACPI 構成では何も検査されない      */
	/* のに通ってしまう（前に 2 度踏んだ穴）。                         */
	/* -------------------------------------------------------------- */

}

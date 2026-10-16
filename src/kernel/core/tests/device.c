/*
 * device.c — 周辺核 デバイス管理 (§1.8)
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

LOCAL CONST TC nm_com[] = {(TC)'c', (TC)'o', (TC)'m', TNULL};

LOCAL CONST TC nm_rda[] = {(TC)'r', (TC)'d', (TC)'a', TNULL};

LOCAL CONST TC nm_rda01[] = {(TC)'r', (TC)'d', (TC)'a', (TC)'0', (TC)'1',
                             TNULL};

LOCAL CONST TC nm_rda255[] = {(TC)'r', (TC)'d', (TC)'a', (TC)'2', (TC)'5',
                              (TC)'5', TNULL};

LOCAL W dev_child_dd; /* 子プロセスが開いたデバイスディスクリプタ */

/* デバイスを開いたまま終わる子プロセス。自動クローズの検査用。 */
LOCAL W p_devopen(MESSAGE *msg)
{
	(void)msg;
	dev_child_dd = opn_dev(tst_nm_rda0, D_READ, 0);
	return 0; /* 閉じずに終わる */
}

BTRON_TEST_GROUP("周辺核 デバイス管理 (§1.8)", 290, TST_PHASE_PROC, tst_device)
{
	BTRON_TEST("デバイスの登録と一覧 (§1.8)", {
		DEV_INFO info[4];
		W n;
		TC nm[L_DEVNM + 1];
		DEV_STATE st;

		BTRON_CHECK("シリアルの登録", serdev_register() == ER_OK);
		BTRON_CHECK("RAM ディスクの登録", ramdisk_register() == ER_OK);

		n = lst_dev(0, 0);
		BTRON_CHECK("2 つ登録されている", n == 2);
		BTRON_CHECK("情報が取れる", lst_dev(info, 4) == 2);
		/* 仕様「ndev より多い場合は ndev 個のみ格納」 */
		BTRON_CHECK("要素数で切られる", lst_dev(info, 1) == 2);
		BTRON_CHECK("要素数が負", lst_dev(info, -1) == ER_PAR);

		/* デバイス番号 → ユニット名。戻り値はサブユニット数。 */
		BTRON_CHECK("1 番はシリアル", get_dev(nm, 1) == 0);
		BTRON_CHECK("名前", nm[0] == (TC)'c' && nm[1] == (TC)'o' &&
		                        nm[2] == (TC)'m');
		BTRON_CHECK("2 番は RAM ディスク", get_dev(nm, 2) == 2);
		BTRON_CHECK("存在しない番号", get_dev(nm, 99) == ER_NOEXS);

		BTRON_CHECK("状態", dev_sts(nm_rda, &st) == ER_OK);
		/* ブロック型なので DA_CHARDEV が立っていない */
		BTRON_CHECK("ブロック型", (st.attr & DA_CHARDEV) == 0);
		BTRON_CHECK("RAM ディスク",
		            (st.attr & DA_DEVKIND) == DK_DISK_RAM);
		BTRON_CHECK("ブロックサイズ", st.blksz == 512);
		BTRON_CHECK("書き込める", st.wprt == 0);
		BTRON_CHECK("シリアルは文字型",
		            dev_sts(nm_com, &st) == ER_OK &&
		                (st.attr & DA_CHARDEV) != 0);
		BTRON_CHECK("シリアルは読めない", st.wprt == 0);
	});

	BTRON_TEST("論理デバイス名の切り分け (§1.8.2)", {
		W dd;

		/* "rda" はユニット名（物理デバイス名）、"rda0"/"rda1" はサブユニット。
		 * **末尾の連続する数字がサブユニット番号**である。 */
		dd = opn_dev(nm_rda, D_UPDATE, 0);
		BTRON_CHECK("ユニット名で開ける", dd >= 1);
		BTRON_CHECK("閉じる", cls_dev(dd, 0, 0) == ER_OK);
		dd = opn_dev(tst_nm_rda0, D_UPDATE, 0);
		BTRON_CHECK("サブユニット 0", dd >= 1);
		BTRON_CHECK("閉じる", cls_dev(dd, 0, 0) == ER_OK);
		dd = opn_dev(tst_nm_rda1, D_UPDATE, 0);
		BTRON_CHECK("サブユニット 1", dd >= 1);
		BTRON_CHECK("閉じる", cls_dev(dd, 0, 0) == ER_OK);
		/* サブユニット 2 は無い（nsub = 2 なので 0 と 1 だけ）。 */
		BTRON_CHECK("無いサブユニット",
		            opn_dev(tst_nm_rda2, D_UPDATE, 0) == ER_NOEXS);
		/* 仕様「前に 0 の付かない 3 桁までの数字」 */
		BTRON_CHECK("先頭 0 は不正",
		            opn_dev(nm_rda01, D_UPDATE, 0) == ER_PAR);
		BTRON_CHECK("255 以上は不正",
		            opn_dev(nm_rda255, D_UPDATE, 0) == ER_PAR);
		BTRON_CHECK("登録されていない名前",
		            opn_dev(tst_nm_test, D_UPDATE, 0) == ER_NOEXS);
		BTRON_CHECK("属性不正", opn_dev(nm_rda, 0, 0) == ER_PAR);
		BTRON_CHECK("アドレス不正", opn_dev(0, D_READ, 0) == ER_ADR);
	});

	BTRON_TEST("RAM ディスクの読み書き (§1.8.4)", {
		W dd;
		VP mem = 0;
		B *buf;
		W asz = 0;
		W nblk_rda = 0;
		W i;

		if (get_mbk(&mem, 1, 0) == ER_OK) {
			buf = (B *)mem;
			dd = opn_dev(tst_nm_rda0, D_UPDATE, 0);
			BTRON_CHECK("開く", dd >= 1);
			/* 仕様「size = 0 の時は実際の読み込みは行わずに、a_size に
			 * 読み込み可能なデータサイズを戻す」 */
			/* 仕様「size = 0 の時は…a_size に読み込み可能な
			 * データサイズを戻す」。**ブロック数は構成で変わる**ので
			 * 値そのものではなく「正で、範囲外の判定と一致すること」
			 * を確かめる。 */
			BTRON_CHECK("読める量",
			            rea_dev(dd, 0, buf, 0, &asz, 0) == ER_OK &&
			                asz > 0);
			nblk_rda = asz;
			for (i = 0; i < 512; i++) {
				buf[i] = (B)(i & 0x7F);
			}
			BTRON_CHECK("書き込み",
			            wri_dev(dd, 3, buf, 1, &asz, 0) == ER_OK &&
			                asz == 1);
			for (i = 0; i < 512; i++) {
				buf[i] = 0;
			}
			BTRON_CHECK("読み込み",
			            rea_dev(dd, 3, buf, 1, &asz, 0) == ER_OK &&
			                asz == 1);
			BTRON_CHECK("中身が合っている",
			            buf[0] == 0 && buf[5] == 5 && buf[127] == 127);
			/* 属性データは持っていない */
			BTRON_CHECK("属性データ",
			            rea_dev(dd, -1, buf, 1, &asz, 0) == ER_DEV);
			/* **ブロック数は読んだ時点の値を覚えておく。**
			 * asz は後の読み書きで上書きされるので、そのまま使うと
			 * 範囲内の番号を渡してしまって検査にならない。 */
			BTRON_CHECK("範囲外",
			            wri_dev(dd, nblk_rda, buf, 1, &asz, 0) == ER_PAR);
			BTRON_CHECK("閉じる", cls_dev(dd, 0, 0) == ER_OK);

			/* **サブユニットは別の領域。** rda1 の同じブロックは 0 のまま。 */
			dd = opn_dev(tst_nm_rda1, D_READ, 0);
			BTRON_CHECK("開く", dd >= 1);
			for (i = 0; i < 512; i++) {
				buf[i] = (B)0xFF;
			}
			BTRON_CHECK("読み込み",
			            rea_dev(dd, 3, buf, 1, &asz, 0) == ER_OK);
			BTRON_CHECK("別の領域を指している", buf[5] == 0);
			/* 読み込み専用で開いたので書けない */
			BTRON_CHECK("書けない",
			            wri_dev(dd, 0, buf, 1, &asz, 0) == ER_DEV);
			BTRON_CHECK("閉じる", cls_dev(dd, 0, 0) == ER_OK);

			/* シリアルは書けるが読めない */
			dd = opn_dev(nm_com, D_WRITE, 0);
			BTRON_CHECK("シリアルを開く", dd >= 1);
			BTRON_CHECK("書ける",
			            wri_dev(dd, 0, "", 0, &asz, 0) == ER_OK);
			BTRON_CHECK("閉じる", cls_dev(dd, 0, 0) == ER_OK);
			dd = opn_dev(nm_com, D_READ, 0);
			BTRON_CHECK("読み用に開ける", dd >= 1);
			BTRON_CHECK("読めない",
			            rea_dev(dd, 0, buf, 1, &asz, 0) == ER_DEV);
			BTRON_CHECK("閉じる", cls_dev(dd, 0, 0) == ER_OK);
			(void)rel_mbk(mem);
		}
	});

	BTRON_TEST("排他制御は物理デバイス名まで効く (§1.8.4)", {
		W d1;
		W d2;

		/* 仕様「"hda" の D_EXCL オープンは、"hda#" が 1 つも
		 * オープンされていない場合のみ可能であり、逆に "hda" を
		 * D_EXCL オープンした後は、どの "hda#" のオープンも不可」 */
		d1 = opn_dev(nm_rda, D_UPDATE | D_EXCL, 0);
		BTRON_CHECK("ユニット全体を排他で開く", d1 >= 1);
		BTRON_CHECK("サブユニットは開けない",
		            opn_dev(tst_nm_rda0, D_READ, 0) == ER_BUSY);
		BTRON_CHECK("ユニットも開けない",
		            opn_dev(nm_rda, D_READ, 0) == ER_BUSY);
		BTRON_CHECK("閉じる", cls_dev(d1, 0, 0) == ER_OK);

		/* 逆向き。サブユニットが開いていると物理デバイス名の排他は不可。 */
		d1 = opn_dev(tst_nm_rda0, D_READ, 0);
		BTRON_CHECK("サブユニットを開く", d1 >= 1);
		BTRON_CHECK("ユニット全体の排他は不可",
		            opn_dev(nm_rda, D_UPDATE | D_EXCL, 0) == ER_BUSY);
		/* 別のサブユニットは影響を受けない。 */
		d2 = opn_dev(tst_nm_rda1, D_READ, 0);
		BTRON_CHECK("別のサブユニットは開ける", d2 >= 1);
		BTRON_CHECK("片付け",
		            cls_dev(d1, 0, 0) == ER_OK && cls_dev(d2, 0, 0) == ER_OK);
		BTRON_CHECK("全部閉じた", dev_open_count() == 0);
	});

	BTRON_TEST("プロセス終了でデバイスが閉じる (§1.8.4)", {
		W n0 = dev_open_count();
		/* **1 と決め打ちできない。** フォントマネージャがまだ動いている。 */
		W p0 = prc_count();
		W child;

		dev_child_dd = 0;
		child = prc_create(p_devopen, 1, 0, 0);
		BTRON_CHECK("子は走って終わった", child >= 1 && prc_count() == p0);
		BTRON_CHECK("子は開いていた", dev_child_dd >= 1);
		/* 仕様「デバイスをオープンしたプロセスが終了したときには、
		 * 自動的にそのデバイスはクローズされる」 */
		BTRON_CHECK("自動的に閉じられた", dev_open_count() == n0);
		(void)clr_msg(MM_ALL, MM_NULL);
	});

	BTRON_TEST("サスペンド要求 (§1.8.4 sus_dev)", {
		BTRON_CHECK("禁止カウントは 0", sus_dev(D_CHECK) == 0);
		BTRON_CHECK("禁止する", sus_dev(D_DISSUS) == ER_OK);
		BTRON_CHECK("カウントが増えた", sus_dev(D_CHECK) == 1);
		BTRON_CHECK("禁止中は断る", sus_dev(D_SUSPEND) == ER_BUSY);
		BTRON_CHECK("許可する", sus_dev(D_ENASUS) == ER_OK);
		BTRON_CHECK("カウントが戻った", sus_dev(D_CHECK) == 0);
		/* 省電力の仕掛けがまだ無い */
		BTRON_CHECK("サスペンドは未サポート",
		            sus_dev(D_SUSPEND) == ER_NOSPT);
		BTRON_CHECK("不正なモード", sus_dev(0) == ER_PAR);
	});

	/* -------------------------------------------------------------- */
	/* §1.6 ファイル管理                                              */
	/*                                                                */
	/* ディレクトリは無い。実身 / 仮身モデルなので、                   */
	/* 「ファイルの中にリンクレコードを置く」ことが唯一の参照手段。     */
	/* -------------------------------------------------------------- */

}

/*
 * file.c — 周辺核 ファイル管理 (§1.6)
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

/* ファイル管理 (§1.6) の検査で使う名前。**マクロの中にカンマ付きの
 * 初期化子を書けない**（BTRON_TEST は関数形式マクロ）のでここに置く。 */
LOCAL CONST TC nm_fsname[] = {(TC)'T', (TC)'E', (TC)'S', (TC)'T', (TC)'F',
                              (TC)'S', TNULL};

LOCAL CONST TC nm_fsloc[] = {(TC)'R', (TC)'A', (TC)'M', TNULL};

LOCAL CONST TC nm_conn2[] = {(TC)'t', (TC)'s', (TC)'2', TNULL};

LOCAL CONST TC nm_f1[] = {(TC)'a', (TC)'l', (TC)'p', (TC)'h', (TC)'a', TNULL};

LOCAL CONST TC nm_f2[] = {(TC)'b', (TC)'e', (TC)'t', (TC)'a', TNULL};

LOCAL CONST TC nm_bad[] = {(TC)'x', (TC)TC_FDLM, (TC)'y', TNULL};

/* ／tst／alpha／beta */
LOCAL CONST TC path_deep[] = {(TC)TC_FDLM, (TC)'t', (TC)'s', (TC)'t',
                              (TC)TC_FDLM, (TC)'a', (TC)'l', (TC)'p',
                              (TC)'h', (TC)'a', (TC)TC_FDLM, (TC)'b',
                              (TC)'e', (TC)'t', (TC)'a', TNULL};

/* ／tst／alpha：1  （同名の 2 つめ） */
LOCAL CONST TC path_nth[] = {(TC)TC_FDLM, (TC)'t', (TC)'s', (TC)'t',
                             (TC)TC_FDLM, (TC)'a', (TC)'l', (TC)'p',
                             (TC)'h', (TC)'a', (TC)TC_FSEP, (TC)'1', TNULL};

/* ≡  （作業ファイル） */
LOCAL CONST TC path_own[] = {(TC)TC_FOWN, TNULL};

/* ／nope */
LOCAL CONST TC path_nofs[] = {(TC)TC_FDLM, (TC)'n', (TC)'o', (TC)'p',
                              (TC)'e', TNULL};

BTRON_TEST_GROUP("周辺核 ファイル管理 (§1.6)", 300, TST_PHASE_PROC, tst_file)
{
	BTRON_TEST("ファイルシステムの生成と接続 (§1.6.2)", {
		LINK root;
		FS_STATE fst;
		F_ATTACH at[4];
		W n;

		/* 媒体を作る。rda1 を使う（rda0 は §1.8 の検査で汚してある）。 */
		BTRON_CHECK("生成",
		            fil_format(tst_nm_rda1, nm_fsname, nm_fsloc, F_LEVEL1,
		                       F_JPN) == ER_OK);
		/* 仕様「システムのスタートアップ時には、ファイルシステムは
		 * 1 つも存在していない状態」 */
		BTRON_CHECK("接続前は 0 個", lst_fls(0, 0) == 0);
		BTRON_CHECK("接続", att_fls(tst_nm_rda1, tst_nm_conn, &root, FS_SYNC) ==
		                        ER_OK);
		tst_fs_up = True;
		tst_fl_root = root;
		/* 仕様「接続時には接続したファイルシステムのルートファイルへの
		 * リンクが得られる」「ルートファイルは常に 0 のファイル ID」 */
		BTRON_CHECK("ルートのファイル ID は 0", root.f_id == 0);
		BTRON_CHECK("ファイルシステム名が入る",
		            root.fs_name[0] == (TC)'T' && root.fs_name[5] == (TC)'S');

		/* 二重接続はできない。 */
		BTRON_CHECK("同じデバイスは二度接続できない",
		            att_fls(tst_nm_rda1, nm_conn2, 0, FS_SYNC) == ER_EXS);
		BTRON_CHECK("接続名の重複", att_fls(tst_nm_rda0, tst_nm_conn, 0, FS_SYNC) ==
		                                ER_EXS);
		/* 形式が違う媒体。rda0 は §1.8 の検査で適当な値を書いてある。 */
		BTRON_CHECK("形式が違う",
		            att_fls(tst_nm_rda0, nm_conn2, 0, FS_SYNC) == ER_TRON);

		BTRON_CHECK("一覧", lst_fls(at, 4) == 1);
		BTRON_CHECK("接続名", at[0].a_name[0] == (TC)'t');
		n = fls_sts(tst_nm_rda1, &fst);
		BTRON_CHECK("管理情報", n > 0);
		BTRON_CHECK("ブロックサイズ", fst.fs_bsize == 512);
		BTRON_CHECK("アクセス管理レベル", fst.fs_level == F_LEVEL1);
		BTRON_CHECK("使用言語", fst.fs_lang == F_JPN);
		BTRON_CHECK("空きがある", fst.fs_nfree > 0 &&
		                              fst.fs_nfree < fst.fs_nblk);
		BTRON_CHECK("存在しないデバイス", fls_sts(tst_nm_rda2, &fst) == ER_NOEXS);
	});

	BTRON_TEST("ファイルの生成とレコード (§1.6.3)", {
		if (!tst_fs_up) {
			BTRON_CHECK("ファイルシステムが無いので飛ばす", True);
		} else {
			LINK a;
			W fd;
			W fid;
			B buf[64];
			W rsz = 0;
			UH sub = 0;
			W t;
			W i;

			a = tst_fl_root;
			fid = cre_fil(&a, nm_f1, 0, 0x1234, F_FLOAT);
			BTRON_CHECK("生成", fid > 0);
			tst_fl_a = a;
			/* 仕様「参照カウントはファイルを生成した時点では 0」 */
			BTRON_CHECK("参照カウントは 0", get_nlk(&a) == 0);

			fd = opn_fil(&a, F_UPDATE, 0);
			BTRON_CHECK("開く", fd > 0);
			/* 仕様「レコードが 1 つも存在しないときは終端レコードが
			 * 現在レコードとなる」。読むと ER_ENDR。 */
			BTRON_CHECK("空なら終端レコード",
			            rea_rec(fd, 0, buf, 64, &rsz, &sub) == ER_ENDR);

			for (i = 0; i < 16; i++) {
				buf[i] = (B)('A' + i);
			}
			BTRON_CHECK("追加", apd_rec(fd, buf, 16, 5, 0x55, 0) == ER_OK);
			BTRON_CHECK("先頭へ", see_rec(fd, 0, 1, 0) == ER_OK);
			for (i = 0; i < 64; i++) {
				buf[i] = 0;
			}
			t = rea_rec(fd, 0, buf, 64, &rsz, &sub);
			BTRON_CHECK("読める", t == 5 && rsz == 16 && sub == 0x55);
			BTRON_CHECK("中身", buf[0] == 'A' && buf[15] == 'P');
			/* 仕様「offset ≧ レコードサイズ…のときは buf には何も
			 * 格納されずに *r_size、*subtype に対応する値が格納される」 */
			BTRON_CHECK("大きさだけ取る",
			            rea_rec(fd, 0, 0, 0, &rsz, &sub) == 5 && rsz == 16);
			BTRON_CHECK("途中から",
			            rea_rec(fd, 4, buf, 64, &rsz, 0) == 5 && rsz == 12 &&
			                buf[0] == 'E');

			/* 書き込みでレコードが伸びる。 */
			buf[0] = 'z';
			BTRON_CHECK("末尾へ追記",
			            wri_rec(fd, -1, buf, 1, &rsz, 0, 0) == ER_OK &&
			                rsz == 1);
			BTRON_CHECK("伸びた",
			            rea_rec(fd, 0, buf, 64, &rsz, 0) == 5 && rsz == 17 &&
			                buf[16] == 'z');
			/* 縮小。 */
			BTRON_CHECK("縮小", trc_rec(fd, 8) == ER_OK);
			BTRON_CHECK("縮んだ",
			            rea_rec(fd, 0, buf, 64, &rsz, 0) == 5 && rsz == 8);
			/* 仕様「レコードサイズ≦ size のときは何もしない」 */
			BTRON_CHECK("大きい値なら何もしない", trc_rec(fd, 99) == ER_OK);

			/* 複数レコードと検索。 */
			BTRON_CHECK("2 つめ", apd_rec(fd, buf, 4, 7, 0x77, 0) == ER_OK);
			BTRON_CHECK("3 つめ", apd_rec(fd, buf, 2, 5, 0x11, 0) == ER_OK);
			BTRON_CHECK("先頭へ", see_rec(fd, 0, 1, 0) == ER_OK);
			{
				W rn = -1;

				BTRON_CHECK("タイプ 7 を探す",
				            fnd_rec(fd, F_TOPEND, F_TYPEMASK(7), 0, &rn) == 7 &&
				                rn == 1);
				BTRON_CHECK("サブタイプ指定",
				            fnd_rec(fd, F_TOPEND, F_TYPEALL, 0x11, &rn) == 5 &&
				                rn == 2);
				/* 仕様「typemask=0 の場合も含む」 */
				BTRON_CHECK("マスク 0", fnd_rec(fd, F_TOPEND, 0, 0, 0) ==
				                            ER_REC);
				BTRON_CHECK("見つからない",
				            fnd_rec(fd, F_TOPEND, F_TYPEMASK(9), 0, 0) ==
				                ER_REC);
				/* 後向き。 */
				BTRON_CHECK("終端から先頭へ",
				            fnd_rec(fd, F_ENDTOP, F_TYPEMASK(5), 0, &rn) == 5 &&
				                rn == 2);
			}

			/* 挿入。現在レコードの直前に入る。 */
			BTRON_CHECK("先頭へ", see_rec(fd, 1, 1, 0) == ER_OK);
			BTRON_CHECK("挿入", ins_rec(fd, buf, 3, 9, 0, 0) == ER_OK);
			{
				W rn = -1;

				BTRON_CHECK("4 つになった",
				            see_rec(fd, 0, -1, &rn) == ER_OK && rn == 4);
				BTRON_CHECK("入った場所",
				            fnd_rec(fd, F_TOPEND, F_TYPEMASK(9), 0, &rn) == 9 &&
				                rn == 1);
			}
			/* 削除。 */
			BTRON_CHECK("削除", del_rec(fd) == 0);
			{
				W rn = -1;

				BTRON_CHECK("3 つに戻った",
				            see_rec(fd, 0, -1, &rn) == ER_OK && rn == 3);
			}

			BTRON_CHECK("閉じる", cls_fil(fd) == ER_OK);

			/* **閉じて開き直しても中身が残る。**ここが通れば、
			 * メモリの写しがブロック連鎖へ書き戻されている。 */
			fd = opn_fil(&a, F_READ, 0);
			BTRON_CHECK("開き直す", fd > 0);
			BTRON_CHECK("レコード数",
			            see_rec(fd, 0, -1, &rsz) == ER_OK && rsz == 3);
			BTRON_CHECK("先頭へ", see_rec(fd, 0, 1, 0) == ER_OK);
			BTRON_CHECK("中身が残っている",
			            rea_rec(fd, 0, buf, 64, &rsz, &sub) == 5 &&
			                rsz == 8 && sub == 0x55 && buf[0] == 'A');
			/* 読み込み専用では書けない（仕様: ER_FD）。 */
			BTRON_CHECK("読み込み専用では書けない",
			            wri_rec(fd, 0, buf, 1, 0, 0, 0) == ER_FD);
			BTRON_CHECK("閉じる", cls_fil(fd) == ER_OK);
		}
	});

	BTRON_TEST("リンクレコードと参照カウント (§1.6.5)", {
		if (!tst_fs_up) {
			BTRON_CHECK("ファイルシステムが無いので飛ばす", True);
		} else {
			LINK b;
			LINK tmp;
			W fd;
			B buf[64];
			W i;

			b = tst_fl_root;
			BTRON_CHECK("beta を生成", cre_fil(&b, nm_f2, 0, 0, F_FLOAT) > 0);
			tst_fl_b = b;

			/* alpha の中に beta へのリンクレコードを置く。 */
			fd = opn_fil(&tst_fl_a, F_UPDATE, 0);
			BTRON_CHECK("alpha を開く", fd > 0);
			BTRON_CHECK("リンクレコードを追加",
			            apd_rec(fd, (CONST B *)(CONST VP)&b, L_LNKSZ,
			                    F_LINKREC, 0, 0) == ER_OK);
			/* 仕様「リンクレコードの挿入により、そのリンクの示すファイルの
			 * 参照カウントは +1 される」 */
			BTRON_CHECK("参照カウントが 1", get_nlk(&b) == 1);
			/* 仕様「size = LINK 構造体のサイズ…でなくてはいけない」 */
			BTRON_CHECK("大きさが違えば断る",
			            apd_rec(fd, (CONST B *)(CONST VP)&b, 4, F_LINKREC, 0,
			                    0) == ER_PAR);

			/* 読み出すと LINK になる。 */
			BTRON_CHECK("末尾へ", see_rec(fd, -1, -1, 0) == ER_OK);
			{
				W rsz = 0;

				BTRON_CHECK("リンクレコード",
				            rea_rec(fd, 0, buf, 64, &rsz, 0) == F_LINKREC &&
				                rsz == L_LNKSZ);
				for (i = 0; i < (W)sizeof(LINK); i++) {
					((B *)(VP)&tmp)[i] = buf[i];
				}
				BTRON_CHECK("中身", tmp.f_id == b.f_id);
				/* 仕様「固定リンクはファイルシステム名を持たない…
				 * ファイルから固定リンクを取り出した時点で、そのファイルの
				 * 属するファイルシステム名が設定される」 */
				BTRON_CHECK("ファイルシステム名が埋まる",
				            tmp.fs_name[0] == (TC)'T');
				/* 仕様「offset = 0、size ≧ LINK 構造体のサイズ」 */
				BTRON_CHECK("半端な offset",
				            rea_rec(fd, 1, buf, 64, &rsz, 0) == ER_PAR);
			}
			/* fnd_lnk で引ける。 */
			{
				LINK q = b;
				W rn = -1;

				BTRON_CHECK("先頭へ", see_rec(fd, 0, 1, 0) == ER_OK);
				BTRON_CHECK("同一ファイルで検索",
				            fnd_lnk(fd, F_TOPEND | F_SFILE, &q, 0, &rn) ==
				                    ER_OK &&
				                rn == 3);
			}
			BTRON_CHECK("閉じる", cls_fil(fd) == ER_OK);

			/* 参照カウントが 0 でなければ消せない（仕様）。 */
			BTRON_CHECK("参照されていると消せない",
			            del_fil(0, &b, 0) == ER_BUSY);

			/* alpha はリンクレコードを含むので force が要る。 */
			BTRON_CHECK("リンクを含むと断る", del_fil(0, &tst_fl_a, 0) == ER_REC);
		}
	});

	BTRON_TEST("パス名はリンクの列 (§1.6.2)", {
		if (!tst_fs_up) {
			BTRON_CHECK("ファイルシステムが無いので飛ばす", True);
		} else {
			LINK l;
			W fd;
			LINK dup;

			/* ルートの中に alpha へのリンクを置く。これでパス名で
			 * たどれるようになる。**ディレクトリではなくリンクである。** */
			fd = opn_fil(&tst_fl_root, F_UPDATE, 0);
			BTRON_CHECK("ルートを開く", fd > 0);
			BTRON_CHECK("alpha へのリンク",
			            apd_rec(fd, (CONST B *)(CONST VP)&tst_fl_a, L_LNKSZ,
			                    F_LINKREC, 0, 0) == ER_OK);
			BTRON_CHECK("閉じる", cls_fil(fd) == ER_OK);

			BTRON_CHECK("絶対パス", get_lnk(tst_path_abs, &l, F_NORM) == 0);
			BTRON_CHECK("alpha に当たった", l.f_id == tst_fl_a.f_id);
			/* alpha の中の beta へもたどれる。 */
			BTRON_CHECK("2 段", get_lnk(path_deep, &l, F_NORM) == 0);
			BTRON_CHECK("beta に当たった", l.f_id == tst_fl_b.f_id);
			/* 接続していないファイルシステム。 */
			BTRON_CHECK("接続名が無い",
			            get_lnk(path_nofs, &l, F_NORM) == ER_NOFS);

			/* **同じ名前のファイルを 2 つ置いて、出現順で区別する。**
			 * ファイル名は一意でないという仕様の肝。 */
			dup = tst_fl_root;
			BTRON_CHECK("同名の 2 つめを生成",
			            cre_fil(&dup, nm_f1, 0, 0, F_FLOAT) > 0);
			fd = opn_fil(&tst_fl_root, F_UPDATE, 0);
			BTRON_CHECK("ルートを開く", fd > 0);
			BTRON_CHECK("2 つめへのリンク",
			            apd_rec(fd, (CONST B *)(CONST VP)&dup, L_LNKSZ,
			                    F_LINKREC, 0, 0) == ER_OK);
			BTRON_CHECK("閉じる", cls_fil(fd) == ER_OK);
			BTRON_CHECK("出現順 0 は 1 つめ",
			            get_lnk(tst_path_abs, &l, F_NORM) == 0 &&
			                l.f_id == tst_fl_a.f_id);
			BTRON_CHECK("出現順 1 は 2 つめ",
			            get_lnk(path_nth, &l, F_NORM) == 0 &&
			                l.f_id == dup.f_id);

			/* 作業ファイル。 */
			BTRON_CHECK("作業ファイルは未定義",
			            get_lnk(0, &l, F_NORM) == ER_NOEXS);
			BTRON_CHECK("設定", chg_wrk(&tst_fl_a) == ER_OK);
			BTRON_CHECK("取得",
			            get_lnk(0, &l, F_NORM) == 0 && l.f_id == tst_fl_a.f_id);
			BTRON_CHECK("≡ でも同じ",
			            get_lnk(path_own, &l, F_NORM) == 0 &&
			                l.f_id == tst_fl_a.f_id);
			/* 相対パス名は作業ファイルから。 */
			BTRON_CHECK("相対パス",
			            get_lnk(nm_f2, &l, F_NORM) == 0 &&
			                l.f_id == tst_fl_b.f_id);
			BTRON_CHECK("解除", chg_wrk(0) == ER_OK);

			BTRON_CHECK("空のパス名", get_lnk(nm_fsname + 6, &l, F_NORM) ==
			                              ER_FNAME);
			BTRON_CHECK("モードが不正", get_lnk(tst_path_abs, &l, 0x40) == ER_PAR);
		}
	});

	BTRON_TEST("共有とアクセス管理 (§1.6.4 / §1.6.5)", {
		if (!tst_fs_up) {
			BTRON_CHECK("ファイルシステムが無いので飛ばす", True);
		} else {
			W fd1;
			W fd2;
			A_MODE am;
			F_STATE fst;
			TC nm[L_FNM + 1];

			/* 共有モードどうしは同時に開ける。 */
			fd1 = opn_fil(&tst_fl_b, F_READ, 0);
			fd2 = opn_fil(&tst_fl_b, F_READ, 0);
			BTRON_CHECK("共有で 2 つ開ける", fd1 > 0 && fd2 > 0);
			BTRON_CHECK("同時オープンは 1 つに数える", fil_open_count() == 1);
			BTRON_CHECK("閉じる", cls_fil(fd2) == ER_OK);
			/* 排他モードは他が開いていると断られる。 */
			BTRON_CHECK("排他は断られる",
			            opn_fil(&tst_fl_b, F_READ | F_EXCL, 0) == ER_BUSY);
			BTRON_CHECK("閉じる", cls_fil(fd1) == ER_OK);
			fd1 = opn_fil(&tst_fl_b, F_UPDATE | F_EXCL, 0);
			BTRON_CHECK("今度は開ける", fd1 > 0);
			/* 仕様「排他モード: 他からのいかなる同時オープンを一切禁止」 */
			BTRON_CHECK("排他中は開けない",
			            opn_fil(&tst_fl_b, F_READ, 0) == ER_BUSY);
			BTRON_CHECK("閉じる", cls_fil(fd1) == ER_OK);
			/* 排他書き込みモード。読みは通るが書きは断られる。 */
			fd1 = opn_fil(&tst_fl_b, F_UPDATE | F_WEXCL, 0);
			BTRON_CHECK("排他書き込みで開く", fd1 > 0);
			fd2 = opn_fil(&tst_fl_b, F_READ, 0);
			BTRON_CHECK("読みは通る", fd2 > 0);
			BTRON_CHECK("書きは断られる",
			            opn_fil(&tst_fl_b, F_WRITE, 0) == ER_BUSY);
			BTRON_CHECK("閉じる", cls_fil(fd2) == ER_OK);
			BTRON_CHECK("閉じる", cls_fil(fd1) == ER_OK);
			BTRON_CHECK("全部閉じた", fil_open_count() == 0);

			/* 書込不可属性。所有者でも書けなくなる（仕様）。 */
			BTRON_CHECK("書込不可にする",
			            chg_fat(&tst_fl_b, F_SETRONLY) == ER_OK);
			BTRON_CHECK("書き込みで開けない",
			            opn_fil(&tst_fl_b, F_WRITE, 0) == ER_RONLY);
			/* **読みは通る。** 書込不可は変更を禁じるだけで、
			 * 読み出しは妨げない（仕様）。 */
			fd1 = opn_fil(&tst_fl_b, F_READ, 0);
			BTRON_CHECK("読み込みでは開ける", fd1 > 0);
			if (fd1 > 0) {
				BTRON_CHECK("閉じる", cls_fil(fd1) == ER_OK);
			}
			BTRON_CHECK("戻す", chg_fat(&tst_fl_b, F_RSTRONLY) == ER_OK);
			fd1 = opn_fil(&tst_fl_b, F_WRITE, 0);
			BTRON_CHECK("書き込みで開ける", fd1 > 0);
			if (fd1 > 0) {
				BTRON_CHECK("閉じる", cls_fil(fd1) == ER_OK);
			}

			/* 削除不可属性。 */
			BTRON_CHECK("削除不可にする", chg_fat(&tst_fl_b, F_SETPERM) == ER_OK);
			BTRON_CHECK("消せない", del_fil(0, &tst_fl_b, 0) == ER_PERM);
			BTRON_CHECK("戻す", chg_fat(&tst_fl_b, F_RSTPERM) == ER_OK);
			BTRON_CHECK("不正な属性", chg_fat(&tst_fl_b, 99) == ER_PAR);

			/* アクセスモードの変更と確認。 */
			am.f_ownacc = F_OWNACR; /* 読みだけ */
			am.f_grpacc = 0;
			am.f_pubacc = 0;
			am.f_grpno = 0;
			BTRON_CHECK("モード変更", chg_fmd(&tst_fl_b, &am) == ER_OK);
			BTRON_CHECK("状態が取れる", fil_sts(&tst_fl_b, nm, &fst, 0) == 0);
			BTRON_CHECK("所有者は読みのみ",
			            (fst.f_type & F_OWNACC) == F_OWNACR);
			BTRON_CHECK("名前", nm[0] == (TC)'b');
			/* 所有者名が入っている（レベル 1 なので隠し名は 0）。 */
			BTRON_CHECK("所有者", fst.f_owner[0] == (TC)'S');
			am.f_ownacc = F_OWNACR | F_OWNACW | F_OWNACE;
			BTRON_CHECK("戻す", chg_fmd(&tst_fl_b, &am) == ER_OK);

			/* 名前の変更。特殊記号は使えない。 */
			BTRON_CHECK("特殊記号は使えない",
			            chg_fnm(&tst_fl_b, nm_bad) == ER_FNAME);
			BTRON_CHECK("名前を変える", chg_fnm(&tst_fl_b, tst_nm_f3) == ER_OK);
			BTRON_CHECK("変わった",
			            fil_sts(&tst_fl_b, nm, 0, 0) == 0 && nm[0] == (TC)'g');
			BTRON_CHECK("戻す", chg_fnm(&tst_fl_b, nm_f2) == ER_OK);

			/* 存在検査。 */
			BTRON_CHECK("存在する", chk_fil(&tst_fl_b, F_EXIST, 0) == 0);
			BTRON_CHECK("読める", chk_fil(&tst_fl_b, F_READ, 0) == 0);
		}
	});

}

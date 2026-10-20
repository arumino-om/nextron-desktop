/*
 * exec.c — 実身からのプロセス起動とユーザ特権 (§1.1.6)
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

/* ビルド時に埋め込んだ実行プログラムレコード (tools/mkobj.py)。 */
IMPORT CONST UB btron_user_hello[];

IMPORT CONST UW btron_user_hello_size;

IMPORT CONST UB btron_user_shell[];

IMPORT CONST UW btron_user_shell_size;

LOCAL CONST TC nm_prog[] = {(TC)'h', (TC)'e', (TC)'l', (TC)'l', (TC)'o',
                            TNULL};

LOCAL CONST TC nm_shell[] = {(TC)'s', (TC)'h', (TC)'e', (TC)'l', (TC)'l',
                             TNULL};

LOCAL LINK fl_prog;

BTRON_TEST_GROUP("実身からのプロセス起動とユーザ特権 (§1.1.6)", 310, TST_PHASE_PROC, tst_exec)
{
	BTRON_TEST("実身からプロセスを起こす (§1.1.6)", {
		if (!tst_fs_up) {
			BTRON_CHECK("ファイルシステムが無いので飛ばす", True);
		} else {
			LINK p;
			W fd;
			W pid;
			MESSAGE m;
			W i;
			UD before = tst_free_pages_settled();

			/* 実身を作り、**先頭レコード**に実行プログラムレコードを
			 * 置く。仕様「実行プログラムレコードは、先頭レコードで
			 * なければならない」。 */
			p = tst_fl_root;
			BTRON_CHECK("実身を生成", cre_fil(&p, nm_prog, 0, 0,
			                                  F_FLOAT) > 0);
			fl_prog = p;
			fd = opn_fil(&p, F_UPDATE, 0);
			BTRON_CHECK("開く", fd > 0);
			/* **副種別はこの機械の CPU**（objfmt.h の OBJ_CPU_NATIVE）。
			 * cre_prc は副種別が自分の CPU と違う像を断るので、
			 * ここに arm64 を直書きすると x86_64 で必ず失敗する。 */
			BTRON_CHECK("プログラムを書く",
			            apd_rec(fd, (CONST B *)btron_user_hello,
			                    (W)btron_user_hello_size, 9,
			                    (UH)OBJ_CPU_NATIVE, 0) == ER_OK);
			BTRON_CHECK("閉じる", cls_fil(fd) == ER_OK);

			/* 起動メッセージ。先頭バイトがそのまま終了コードになる
			 * ようにプログラムを書いてある。 */
			m.msg_type = 0;
			m.msg_size = 5;
			for (i = 0; i < 5; i++) {
				m.msg_body.ANYMSG.msg_str[i] = (UB)("BTRON"[i]);
			}
			(void)clr_msg(MM_ALL, MM_NULL);

			pid = cre_prc(&p, 2, &m);
			BTRON_CHECK("プロセスが起きた", pid > 0);
			if (pid > 0) {
				MESSAGE r;
				W got;

				/* 子が終わると正常終了メッセージが届く (§1.2)。
				 *
				 * **異常終了も受け付けて型で判定する。** 片方だけを
				 * 待つと、落ち方を間違えたときに検査が落ちるのでは
				 * なく永久に待ってしまう（逆検査で踏んだ）。 */
				got = rcv_msg(MM_EXIT | MM_ABORT, &r, (W)sizeof(r),
				              WAIT);
				BTRON_CHECK("終了メッセージが届く", got >= 0);
				BTRON_CHECK("正常終了メッセージ",
				            r.msg_type == MS_EXIT);
				BTRON_CHECK("送り主は子プロセス",
				            r.msg_body.EXIT.pid == pid);
				/* 'B' = 0x42 = 66。メッセージがユーザ空間まで
				 * 届いていなければ 0 になる。 */
				BTRON_CHECK("終了コードはメッセージの先頭バイト",
				            r.msg_body.EXIT.code == 'B');
			}

			/* **特権が本当に落ちているかを確かめる。**
			 *
			 * 'F' で始まるメッセージを渡すと、プログラムは
			 * カーネルの番地をシステムコールを通さず直接読む。
			 * EL0 なら変換フォルトでこのプロセスだけが落ち、
			 * 親には異常終了が届く。EL1 のままなら読めてしまう。 */
			(void)clr_msg(MM_ALL, MM_NULL);
			m.msg_body.ANYMSG.msg_str[0] = (UB)'F';
			pid = cre_prc(&p, 2, &m);
			BTRON_CHECK("もう一度起こす", pid > 0);
			if (pid > 0) {
				MESSAGE r;

				BTRON_CHECK("異常終了が届く",
				            rcv_msg(MM_ABORT | MM_EXIT, &r,
				                    (W)sizeof(r), WAIT) >= 0);
				/* 仕様 §1.2.2「プロセスがシステムエラーにより
				 * 異常終了させられた場合」= MS_ABORT。
				 * **MS_EXIT が来たら読めてしまっている。** */
				BTRON_CHECK("異常終了メッセージ",
				            r.msg_type == MS_ABORT);
				BTRON_CHECK("落ちたのは子プロセス",
				            r.msg_body.ABORT.pid == pid);
			}
			(void)clr_msg(MM_ALL, MM_NULL);

			/* **像のページが返っていること。** プロセスごとに
			 * 数ページずつ失うと、すぐ枯れる。 */
			BTRON_CHECK("ページが戻った",
			            tst_free_pages_settled() + 2 >= before);

			/* **ヘッダの中身が壊れていたら走らせない。**
			 *
			 * レコードのサブタイプ（外側）とヘッダの CPU（内側）は
			 * 別の場所にある。片方だけ見ていると、もう片方が
			 * 壊れていても通ってしまう。両方を別々に試す。 */
			fd = opn_fil(&p, F_UPDATE, 0);
			if (fd > 0) {
				B bad[4];

				BTRON_CHECK("先頭へ", see_rec(fd, 0, 1, 0) == ER_OK);
				/* ヘッダの CPU（本体の 12 バイト目）だけを壊す。
				 * レコードのサブタイプは正しいままにする。 */
				bad[0] = 0x42; /* 68000 */
				bad[1] = 0;
				bad[2] = 0;
				bad[3] = 0;
				BTRON_CHECK("ヘッダの CPU を壊す",
				            wri_rec(fd, OBJ_CPU, bad, 4, 0, 0, 0) == ER_OK);
				BTRON_CHECK("閉じる", cls_fil(fd) == ER_OK);
				BTRON_CHECK("ヘッダの CPU が違えば走らせない",
				            cre_prc(&p, 2, 0) == ER_REC);
			}
			/* 印（マジック）を壊す。 */
			fd = opn_fil(&p, F_UPDATE, 0);
			if (fd > 0) {
				B bad[1];

				bad[0] = 'X';
				BTRON_CHECK("先頭へ", see_rec(fd, 0, 1, 0) == ER_OK);
				BTRON_CHECK("印を壊す",
				            wri_rec(fd, 0, bad, 1, 0, 0, 0) == ER_OK);
				BTRON_CHECK("閉じる", cls_fil(fd) == ER_OK);
				BTRON_CHECK("印が違えば走らせない",
				            cre_prc(&p, 2, 0) == ER_REC);
			}
			/* レコードのサブタイプ（外側）を壊す。 */
			fd = opn_fil(&p, F_UPDATE, 0);
			if (fd > 0) {
				UH wrong = 0x0042; /* 68000 */

				BTRON_CHECK("先頭へ", see_rec(fd, 0, 1, 0) == ER_OK);
				BTRON_CHECK("サブタイプを変える",
				            wri_rec(fd, 0, 0, 0, 0, &wrong, 0) == ER_OK);
				BTRON_CHECK("閉じる", cls_fil(fd) == ER_OK);
				BTRON_CHECK("サブタイプが違えば走らせない",
				            cre_prc(&p, 2, 0) == ER_REC);
			}
			/* レコードタイプが違う実身も断る。 */
			{
				LINK q = tst_fl_root;

				BTRON_CHECK("別の実身", cre_fil(&q, tst_nm_f3, 0, 0,
				                                F_FLOAT) > 0);
				BTRON_CHECK("プログラムレコードが無い",
				            cre_prc(&q, 2, 0) == ER_REC);
			}
			BTRON_CHECK("アドレス不正", cre_prc(0, 2, 0) == ER_ADR);
			BTRON_CHECK("優先度範囲外", cre_prc(&p, 300, 0) == ER_PPRI);
		}
	});

	BTRON_TEST("外殻をユーザ特権で動かす (詳細 E22 / E1)", {
		if (!tst_fs_up) {
			BTRON_CHECK("ファイルシステムが無いので飛ばす", True);
		} else {
			LINK sh;
			W fd;
			W pid;
			W oldmask;

			/* **ここで確かめたいこと。**
			 *
			 * 外殻（ウィンドウマネージャやその下の描画）は、
			 * 詳細 E22「1 プロセス 1 サーバ」のとおりユーザランドの
			 * プロセスであるべきもの。そこへ移せるかどうかは
			 *
			 *   - 画面をユーザ特権のプロセスへ貸し出せるか
			 *   - ディスプレイプリミティブがその上で動くか
			 *     （**システムコールではなくライブラリとして**）
			 *   - フォントマネージャの共有メモリを読めるか
			 *   - イベントを取れるか
			 *
			 * で決まる。全部まとめて 1 つのプログラムで試す。 */
			sh = tst_fl_root;
			BTRON_CHECK("実身を生成",
			            cre_fil(&sh, nm_shell, 0, 0, F_FLOAT) > 0);
			fd = opn_fil(&sh, F_UPDATE, 0);
			BTRON_CHECK("開く", fd > 0);
			BTRON_CHECK("外殻を書く",
			            apd_rec(fd, (CONST B *)btron_user_shell,
			                    (W)btron_user_shell_size, 9,
			                    (UH)OBJ_CPU_NATIVE, 0) == ER_OK);
			BTRON_CHECK("閉じる", cls_fil(fd) == ER_OK);

			oldmask = chg_emk(EM_NULL);
			(void)chg_emk(oldmask);
			(void)clr_msg(MM_ALL, MM_NULL);
			pid = cre_prc(&sh, 2, 0);
			BTRON_CHECK("外殻が起きた", pid > 0);
			if (pid > 0) {
				MESSAGE r;

				BTRON_CHECK("終了メッセージ",
				            rcv_msg(MM_EXIT | MM_ABORT, &r,
				                    (W)sizeof(r), WAIT) >= 0);
				/* **異常終了なら、どこかで特権境界に当たっている。** */
				BTRON_CHECK("正常終了", r.msg_type == MS_EXIT);
				/* 戻り値 0 = 画素の読み返しまで通った。
				 * 画面が無い構成でも 0 を返す（その場合は描画を
				 * 飛ばす）ので、どちらでも 0 でなければならない。 */
				BTRON_CHECK("戻り値", r.msg_body.EXIT.code == 0);
			}
			(void)chg_emk(oldmask);
			(void)clr_msg(MM_ALL, MM_NULL);
		}
	});


	BTRON_TEST("cre_prc は接続されていないファイルシステムを断る (§1.1.6)", {
		LINK lnk;
		W i;

		for (i = 0; i < L_FSNM; i++) {
			lnk.fs_name[i] = (TC)'z';
		}
		lnk.fs_name[L_FSNM - 1] = TNULL;
		lnk.f_id = 1;
		lnk.atr1 = 0;
		lnk.atr2 = 0;
		lnk.atr3 = 0;
		lnk.atr4 = 0;
		lnk.atr5 = 0;
		BTRON_CHECK("ファイルシステム未接続", cre_prc(&lnk, 0, 0) == ER_NOFS);
	});
}

/*
 * selfboot.c — 自立起動 (D8 / startup.c) の検査
 *
 * **ESP に `\BTRON\SELFBOOT` があるときだけ走る**（TST_PHASE_SELF）。
 * 他の検査は「ファイルシステムは 1 つも接続されていない状態から始まる」
 * ことを前提に書かれているので、本来の起動列を通した状態とは分ける。
 *
 * ここで確かめたいこと。
 *
 *   1. ディスクが見つかり、GPT の区画表が読めて、BTRON 区画が分かる
 *   2. 未構築なら構築し、接続できる
 *   3. 実行プログラムが実身として置かれ、**ディスクから起動できる**
 *   4. **2 回目の起動で前回の内容が残っている**（これが自立起動の芯）
 *
 * 4 は 1 回の起動では確かめられない。`tools/run_selfboot.py` が
 * 同じイメージで 2 度起動し、1 回目に「構築しました」、2 回目に
 * 「既に構築済み」と「前回の印が残っている」が出ることを見る。
 */

#include <btron/device.h>
#include <btron/file.h>
#include <btron/message.h>
#include <btron/proc.h>
#include <btron/startup.h>
#include <btron/test.h>

#include "../knl.h"
#include "testsuite.h"

/* 前回の起動が残した印。中身はこの文字列で、2 回目はこれを読み返す。 */
LOCAL CONST TC nm_mark[] = {(TC)'m', (TC)'a', (TC)'r', (TC)'k', TNULL};
LOCAL CONST B mark_text[] = "BTRON SELFBOOT MARK";
#define MARK_LEN ((W)sizeof(mark_text) - 1)

LOCAL CONST TC nm_shell[] = {(TC)'s', (TC)'h', (TC)'e', (TC)'l', (TC)'l',
                             TNULL};

/* "／sys／shell" */
LOCAL CONST TC path_shell[] = {(TC)TC_FDLM, (TC)'s', (TC)'y', (TC)'s',
                               (TC)TC_FDLM, (TC)'s', (TC)'h', (TC)'e',
                               (TC)'l', (TC)'l', TNULL};

/* "／sys／mark" */
LOCAL CONST TC path_mark[] = {(TC)TC_FDLM, (TC)'s', (TC)'y', (TC)'s',
                              (TC)TC_FDLM, (TC)'m', (TC)'a', (TC)'r', (TC)'k',
                              TNULL};

BTRON_TEST_GROUP("自立起動 (D8 / startup.c)", 400, TST_PHASE_SELF, tst_selfboot)
{
	CONST SysStartup *st = sys_state_get();

	BTRON_TEST("起動列がディスクを見つける (§1.8 / D8)", {
		BTRON_CHECK("起動列を通した", st->done == True);
		BTRON_CHECK("シリアルが登録されている", st->serial == True);
		if (st->disk != True) {
			hal_console_puts("    ディスク登録の結果: ");
			tst_put_dec((W)st->disk_err);
			hal_console_puts("\n");
		}
		BTRON_CHECK("ディスクが見つかった", st->disk == True);
		if (st->disk) {
			hal_console_puts("    ディスク  : ");
			tst_put_dec((W)(st->disk_blocks / 2048U));
			hal_console_puts("MiB / 区画 ");
			tst_put_dec(st->parts);
			hal_console_puts(" 件 / BTRON 区画 = vda");
			tst_put_dec(st->btron_part);
			hal_console_puts("\n");
			/* ESP と BTRON 区画の 2 つ（tools/mkimage.py）。 */
			BTRON_CHECK("GPT の区画が 2 件", st->parts == 2);
			BTRON_CHECK("BTRON 区画がある", st->btron_part >= 0);
			BTRON_CHECK("容量が入っている", st->disk_blocks > 0);
			BTRON_CHECK("BTRON 区画に大きさがある",
			            vblk_part_blocks(st->btron_part) > 0);
		}
	});

	BTRON_TEST("ファイルシステムが接続されている (§1.6.2)", {
		F_ATTACH at[2];

		BTRON_CHECK("接続できた", st->attached == True);
		BTRON_CHECK("接続は 1 件", lst_fls(at, 2) == 1);
		BTRON_CHECK("ルートのファイル ID は 0", st->root.f_id == 0);
		/* **構築したのか、残っていたのか。** どちらでも検査は通るが、
		 * 出力で区別できるようにする（run_selfboot.py が見る）。 */
		if (st->placed) {
			hal_console_puts("    状態      : 初回（構築した）\n");
		} else {
			hal_console_puts("    状態      : 2 回目以降"
			                 "（前回の内容が残っている）\n");
		}
		BTRON_CHECK("実行プログラムが在る", st->programs == True);
	});

	BTRON_TEST("前回の内容が残る（永続性の芯）", {
		LINK lnk;
		W fd;
		B buf[32];
		W got = 0;

		if (get_lnk(path_mark, &lnk, F_NORM) >= 0) {
			/* 2 回目以降。**前回書いた印が読めなければならない。** */
			fd = opn_fil(&lnk, F_READ, 0);
			BTRON_CHECK("印を開く", fd > 0);
			if (fd > 0) {
				(void)see_rec(fd, 0, F_TOPEND, 0);
				BTRON_CHECK("印を読む",
				            rea_rec(fd, 0, buf, (W)sizeof(buf), &got,
				                    0) >= 0);
				BTRON_CHECK("長さが合う", got == MARK_LEN);
				if (got == MARK_LEN) {
					W i;
					Bool same = True;

					for (i = 0; i < MARK_LEN; i++) {
						if (buf[i] != mark_text[i]) {
							same = False;
						}
					}
					BTRON_CHECK("中身が合う（前回の印が残っている）", same);
				}
				(void)cls_fil(fd);
			}
			hal_console_puts("    印        : 前回の印が残っている\n");
		} else {
			LINK p = st->root;
			LINK root = st->root;

			/* 初回。次の起動で読み返すための印を置く。 */
			BTRON_CHECK("印を作る", cre_fil(&p, nm_mark, 0, 0, F_FLOAT) > 0);
			/* ルートに仮身を置く。これでパス名でたどれる。 */
			BTRON_CHECK("ルートに仮身を置く",
			            sys_put_link(&root, &p) == ER_OK);
			fd = opn_fil(&p, F_UPDATE, 0);
			BTRON_CHECK("開く", fd > 0);
			if (fd > 0) {
				BTRON_CHECK("書く",
				            apd_rec(fd, mark_text, MARK_LEN, 1, 0, 0) ==
				                ER_OK);
				BTRON_CHECK("閉じる", cls_fil(fd) == ER_OK);
			}
			BTRON_CHECK("同期", syn_fls() == ER_OK);
			hal_console_puts("    印        : 今回の起動で置いた\n");
		}
	});

	BTRON_TEST("ディスク上の実行プログラムから外殻が起きる (§1.1.6)", {
		W pid;
		MESSAGE r;

		(void)clr_msg(MM_ALL, MM_NULL);
		/* **埋め込みではなく実身から読む。** 2 回目の起動では
		 * 前回置いたものがそのまま使われる。 */
		pid = sys_run_program(nm_shell, 2, 0);
		if (pid <= 0) {
			LINK dbg;
			W r;

			hal_console_puts("    cre_prc の結果: ");
			tst_put_dec(pid);
			r = get_lnk(path_shell, &dbg, F_NORM);
			hal_console_puts(" / get_lnk: ");
			tst_put_dec(r);
			hal_console_puts("\n");
		}
		BTRON_CHECK("起きた", pid > 0);
		if (pid > 0) {
			BTRON_CHECK("終了メッセージ",
			            rcv_msg(MM_EXIT | MM_ABORT, &r, (W)sizeof(r),
			                    WAIT) >= 0);
			/* 異常終了なら、どこかで特権境界に当たっている。 */
			BTRON_CHECK("正常終了", r.msg_type == MS_EXIT);
			BTRON_CHECK("戻り値", r.msg_body.EXIT.code == 0);
		}
		(void)clr_msg(MM_ALL, MM_NULL);
	});
}

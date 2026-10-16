/*
 * startup.c — システムの起動列 (D8 の残作業 ②③)
 *
 * ------------------------------------------------------------------------
 * なぜこれが要るのか
 * ------------------------------------------------------------------------
 *
 * ここまで、デバイスドライバの登録もファイルシステムの接続も**検査
 * コードの中**で行っていた。つまり「電源を入れると OS が立ち上がる」
 * 経路が存在せず、検査が全部を駆動していた。ファイルシステムも
 * RAM ディスクの上にしか無いので電源を切ると消える。
 *
 * この節はその 2 つを埋める。
 *
 *   1. ドライバを登録する（シリアル / RAM ディスク / 入力 / ブロック）
 *   2. **永続する記憶装置を探し、無ければ構築し、接続する**
 *   3. 実行プログラムを実身として置く（2 回目以降は既にある）
 *
 * 仕様 (D8) は `/SYS/DEVCONF`・初期プロセス・実行環境ファイルの仕組みを
 * 踏襲すると決めてあるが、**それらの意味論は仕様原文が必要**な部分が
 * 残っている（`DEVCONF` の書式、起動時のユーザ環境の決め方）。ここでは
 * 原文を要しない範囲 —— 記憶装置の発見・構築・接続と、実行プログラムの
 * 配置 —— だけを実装し、残りは論点として `design-issues.md` §4 に
 * 差し戻してある（規律: 決まっていない設計を勝手に決めない）。
 *
 * ------------------------------------------------------------------------
 * 構築 (fil_format) を起動時にやってよいのか
 * ------------------------------------------------------------------------
 *
 * **BTRON 区画のタイプ GUID が付いている区画に限る。** GPT の区画表で
 * 自分のものだと分かる区画だけを触るので、他の OS の区画を壊さない。
 * 中身が本OS の形式でなければ `att_fls` が `ER_TRON` を返すので、
 * それを「まだ構築していない」と解釈して 1 度だけ構築する。
 */

#include <btron/device.h>
#include <btron/errcode.h>
#include <btron/file.h>
#include <btron/hal.h>
#include <btron/objfmt.h>
#include <btron/proc.h>
#include <btron/startup.h>
#include <btron/virtio.h>

IMPORT CONST UB btron_user_shell[];
IMPORT CONST UW btron_user_shell_size;
IMPORT CONST UB btron_user_winman[];
IMPORT CONST UW btron_user_winman_size;

/* 接続名とファイルシステム名。接続名はパス名の第 1 要素になる
 * （"／sys／…"）。 */
LOCAL CONST TC nm_attach[] = {(TC)'s', (TC)'y', (TC)'s', TNULL};
LOCAL CONST TC nm_fsname[] = {(TC)'B', (TC)'T', (TC)'R', (TC)'O', (TC)'N',
                              TNULL};
LOCAL CONST TC nm_fsloc[] = {(TC)'v', (TC)'d', (TC)'a', TNULL};

/* 置く実行プログラムの名前。 */
LOCAL CONST TC nm_shell[] = {(TC)'s', (TC)'h', (TC)'e', (TC)'l', (TC)'l',
                             TNULL};
LOCAL CONST TC nm_winman[] = {(TC)'w', (TC)'i', (TC)'n', (TC)'m', (TC)'a',
                              (TC)'n', TNULL};

LOCAL SysStartup sys_state;

/* ------------------------------------------------------------------ */
/* 補助                                                                */
/* ------------------------------------------------------------------ */

/* "vda" + サブユニット番号の論理デバイス名を作る。番号は 1 桁で足りる
 * （区画は 4 つまでしか見ない）。 */
LOCAL void dev_name(TC *out, W sub)
{
	out[0] = (TC)'v';
	out[1] = (TC)'d';
	out[2] = (TC)'a';
	out[3] = (TC)('0' + sub);
	out[4] = TNULL;
}

/* 親の中に仮身（リンクレコード）を置く。
 *
 * **これが無いとパス名でたどれない。** 仕様 §1.6.1 のとおり
 * ディレクトリは存在せず、参照関係は「ファイルの中のリンクレコード」が
 * 作る。`cre_fil` は実身を作ってリンクを返すだけなので、親から辿れる
 * ようにするには親にリンクレコードを足す必要がある。 */
LOCAL ERR put_link(LINK *parent, CONST LINK *child)
{
	W fd;
	ERR er;

	fd = opn_fil(parent, F_UPDATE, 0);
	if (fd < 1) {
		return ER_ACCES;
	}
	er = apd_rec(fd, (CONST B *)(CONST VP)child, L_LNKSZ, F_LINKREC, 0, 0);
	(void)cls_fil(fd);
	return er;
}

/* 実行プログラムレコードを持つ実身を 1 つ作る。
 * 仕様「実行プログラムレコードは、先頭レコードでなければならない」。 */
LOCAL ERR put_program(LINK *root, CONST TC *name, CONST UB *img, UW size)
{
	LINK lnk = *root;
	W fd;
	ERR er;

	if (cre_fil(&lnk, name, 0, 0, F_FLOAT) < 0) {
		return ER_NOSPC;
	}
	fd = opn_fil(&lnk, F_UPDATE, 0);
	if (fd < 1) {
		return ER_ACCES;
	}
	er = apd_rec(fd, (CONST B *)img, (W)size, OBJ_REC_PROGRAM,
	             (UH)OBJ_CPU_NATIVE, 0);
	(void)cls_fil(fd);
	if (er != ER_OK) {
		return er;
	}
	return put_link(root, &lnk);
}

/* パス名 "／sys／<name>" を組む。 */
LOCAL void sys_path(TC *out, CONST TC *name)
{
	W i = 0;
	W k;

	out[i++] = (TC)TC_FDLM;
	for (k = 0; nm_attach[k] != TNULL; k++) {
		out[i++] = nm_attach[k];
	}
	out[i++] = (TC)TC_FDLM;
	for (k = 0; name[k] != TNULL; k++) {
		out[i++] = name[k];
	}
	out[i] = TNULL;
}

/* ------------------------------------------------------------------ */
/* 起動列                                                              */
/* ------------------------------------------------------------------ */

LOCAL void register_drivers(void)
{
	ERR er;

	/* 既に登録されていても害が無いように、戻り値は数えるだけにする。
	 * **装置が無いことは異常ではない**（構成によって付いていない）。 */
	sys_state.serial = (serdev_register() == ER_OK) ? True : False;
	sys_state.ramdisk = (ramdisk_register() == ER_OK) ? True : False;

	er = vinput_register();
	sys_state.input_devs = (er == ER_OK) ? vinput_count() : 0;

	er = vblk_register();
	sys_state.disk = (er == ER_OK) ? True : False;
	sys_state.disk_err = er;
	if (sys_state.disk) {
		sys_state.disk_blocks = vblk_capacity();
		sys_state.parts = vblk_parts();
		sys_state.btron_part = vblk_btron_part();
	} else {
		sys_state.btron_part = -1;
	}
}

/* BTRON 区画を接続する。無ければ構築してから接続する。 */
LOCAL ERR attach_disk(void)
{
	TC dev[8];
	LINK root;
	ERR er;

	if (!sys_state.disk || sys_state.btron_part < 0) {
		return ER_NOEXS;
	}
	dev_name(dev, sys_state.btron_part);

	er = att_fls(dev, nm_attach, &root, FS_SYNC);
	if (er == ER_TRON) {
		/* 本OS の形式になっていない。**この区画は GPT のタイプ GUID で
		 * 本OS のものだと分かっている**ので、1 度だけ構築する。 */
		hal_console_puts("  [起動] BTRON 区画が未構築なので構築します\n");
		er = fil_format(dev, nm_fsname, nm_fsloc, F_LEVEL1, F_JPN);
		if (er != ER_OK) {
			return er;
		}
		er = att_fls(dev, nm_attach, &root, FS_SYNC);
		sys_state.formatted = True;
	}
	if (er != ER_OK) {
		return er;
	}
	sys_state.attached = True;
	sys_state.root = root;
	return ER_OK;
}

/* 実行プログラムを実身として置く。既にあれば何もしない。 */
LOCAL ERR place_programs(void)
{
	TC path[L_PATHNM];
	LINK lnk;

	if (!sys_state.attached) {
		return ER_NOEXS;
	}
	sys_path(path, nm_shell);
	if (get_lnk(path, &lnk, F_NORM) >= 0) {
		sys_state.programs = True;
		return ER_OK; /* 前回置いたものが残っている */
	}
	if (put_program(&sys_state.root, nm_winman, btron_user_winman,
	                btron_user_winman_size) != ER_OK) {
		return ER_NOSPC;
	}
	if (put_program(&sys_state.root, nm_shell, btron_user_shell,
	                btron_user_shell_size) != ER_OK) {
		return ER_NOSPC;
	}
	(void)syn_fls();
	sys_state.programs = True;
	sys_state.placed = True;
	return ER_OK;
}

EXPORT CONST SysStartup *sys_startup(void)
{
	ERR er;

	if (sys_state.done) {
		return &sys_state;
	}
	sys_state.done = True;

	register_drivers();
	hal_console_puts("  [起動] ドライバ: シリアル");
	if (sys_state.ramdisk) {
		hal_console_puts(" / RAM ディスク");
	}
	if (sys_state.input_devs > 0) {
		hal_console_puts(" / 入力装置");
	}
	if (sys_state.disk) {
		hal_console_puts(" / ディスク (vda)");
	}
	hal_console_puts("\n");

	if (!sys_state.disk) {
		hal_console_puts("  [起動] 永続する記憶装置がありません"
		                 "（RAM ディスクだけで動きます）\n");
		return &sys_state;
	}

	er = attach_disk();
	if (er != ER_OK) {
		hal_console_puts("  [起動] BTRON 区画を接続できません\n");
		return &sys_state;
	}
	er = place_programs();
	if (er != ER_OK) {
		hal_console_puts("  [起動] 実行プログラムを置けません\n");
		return &sys_state;
	}
	if (sys_state.placed) {
		hal_console_puts("  [起動] ファイルシステムを構築しました"
		                 "（実行プログラムを置きました）\n");
	} else {
		hal_console_puts("  [起動] ファイルシステムは既に構築済みです"
		                 "（前回の内容が残っています）\n");
	}
	return &sys_state;
}

EXPORT ERR sys_put_link(LINK *parent, CONST LINK *child)
{
	return put_link(parent, child);
}

EXPORT CONST SysStartup *sys_state_get(void)
{
	return &sys_state;
}

EXPORT WERR sys_run_program(CONST TC *name, W pri, CONST MESSAGE *msg)
{
	TC path[L_PATHNM];
	LINK lnk;

	if (!sys_state.programs) {
		return ER_NOFS;
	}
	sys_path(path, name);
	if (get_lnk(path, &lnk, F_NORM) < 0) {
		return ER_NOEXS;
	}
	return cre_prc(&lnk, pri, (MESSAGE *)msg);
}

EXPORT WERR sys_run_shell(W pri, CONST MESSAGE *msg)
{
	return sys_run_program(nm_shell, pri, msg);
}

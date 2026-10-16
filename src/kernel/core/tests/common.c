/*
 * common.c — 検査が共有する道具と仕掛け
 *
 * selftest.c から切り出したもの（W0-1）。区分をまたいで使うものだけを
 * ここに置き、宣言は testsuite.h にある。**区分ごとの道具はここに
 * 置かない** — 置くと並列に作業したときにこのファイルが衝突点になる。
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

/* ------------------------------------------------------------------ */
/* 実行順の記録                                                        */
/* ------------------------------------------------------------------ */

#define TST_LOG_MAX 64

EXPORT B tst_seq_log[TST_LOG_MAX + 1];

EXPORT W tst_seq_n;

/* 数を 10 進で出す。検査の中で「何台見つかったか」を見せるのに使う。
 * ハーネスは合否しか扱わないので、値は普通の出力として出す。 */
EXPORT void tst_put_dec(W v)
{
	B buf[12];
	W i = 0;
	UW u = (v < 0) ? (UW)(-v) : (UW)v;

	if (v < 0) {
		hal_console_putc('-');
	}
	do {
		buf[i++] = (B)('0' + (u % 10U));
		u /= 10U;
	} while (u > 0 && i < (W)sizeof(buf));
	while (i > 0) {
		hal_console_putc(buf[--i]);
	}
}

EXPORT void tst_seq_reset(void)
{
	tst_seq_n = 0;
	tst_seq_log[0] = '\0';
}

EXPORT void tst_seq_put(B c)
{
	if (tst_seq_n < TST_LOG_MAX) {
		tst_seq_log[tst_seq_n++] = c;
		tst_seq_log[tst_seq_n] = '\0';
	}
}

EXPORT Bool tst_seq_eq(CONST B *s)
{
	W i;

	for (i = 0; s[i] != '\0'; i++) {
		if (i >= tst_seq_n || tst_seq_log[i] != s[i]) {
			return False;
		}
	}
	return (i == tst_seq_n) ? True : False;
}

/* 失敗したときに何が起きたか分かるように実測の並びを出す。 */
EXPORT void tst_seq_show(CONST B *expect)
{
	hal_console_puts("         期待 \"");
	hal_console_puts(expect);
	hal_console_puts("\"  実測 \"");
	hal_console_puts(tst_seq_log);
	hal_console_puts("\"\n");
}

/* ------------------------------------------------------------------ */
/* 補助                                                                */
/* ------------------------------------------------------------------ */

EXPORT ID tst_make_task(TASKENTRY entry, PRI pri, VP exinf)
{
	T_CTSK ctsk;
	W i;

	ctsk.exinf = exinf;
	ctsk.tskatr = TA_HLNG | TA_RNG0;
	ctsk.task = entry;
	ctsk.itskpri = pri;
	/* **ファイル管理は何段も重なる。** 4KB (TK_MIN_STKSZ) では
	 * ブロック 1 つ (512B) とインデックスエントリ (256B) を持つ関数が
	 * 数段重なっただけで溢れ、黙ってページテーブルを壊した。 */
	ctsk.stksz = TK_MIN_STKSZ * 4;
	ctsk.sstksz = 0;
	ctsk.stkptr = 0;
	ctsk.bufptr = 0;
	for (i = 0; i < 8; i++) {
		ctsk.dsname[i] = 0;
	}
	return tk_cre_tsk(&ctsk);
}

EXPORT void tst_t_mark_b(INT stacd, VP exinf)
{
	(void)stacd;
	(void)exinf;
	tst_seq_put('b');
	tk_ext_tsk();
}

/* ------------------------------------------------------------------ */
/* ② 待ち解除されたタスクは同一優先度の「末尾」に付く                  */
/*                                                                     */
/*   A B C すべて pri 6。レディキューは A, B, C の順。                  */
/*   A が寝る → B が A を起こす（A は**末尾**へ）→ B が終わる。         */
/*   次は C、その後に A。                                              */
/* ------------------------------------------------------------------ */

EXPORT void tst_t_rel_a(INT stacd, VP exinf)
{
	(void)stacd;
	(void)exinf;
	tst_seq_put('a');
	(void)tk_slp_tsk(TMO_FEVR);
	tst_seq_put('A');
	tk_ext_tsk();
}

/* ------------------------------------------------------------------ */
/* ③ 優先度順。番号の小さい方が先に走る                                */
/* ------------------------------------------------------------------ */

EXPORT void tst_t_tag(INT stacd, VP exinf)
{
	(void)exinf;
	tst_seq_put((B)stacd);
	tk_ext_tsk();
}

/* knl_otm を基準に ms だけ回る。タイムスライスを必ず使い切るために使う。
 * 待ちに入ってはいけない（待つと譲ってしまい、スライスの検査にならない）。 */
EXPORT void tst_spin_ms(W ms)
{
	SYSTIM until = knl_otm + (SYSTIM)ms;

	while (knl_otm < until) {
		/* 何もしない。volatile 越しに時刻を読むためにループだけ回す。 */
	}
}

/* 起こされるまで寝ているプロセス。木構造と限界値の検査に使う。 */
EXPORT W tst_p_sleeper(MESSAGE *msg)
{
	(void)msg;
	(void)slp_tsk(T_FOREVER);
	return 0;
}

/* ------------------------------------------------------------------ */
/* 周辺核 §1.2 / §1.4 / §1.3 の検査用のプロセスとハンドラ               */
/* ------------------------------------------------------------------ */

EXPORT W tst_fontman_pid;  /* 立てたフォントマネージャ。最後に止める */

EXPORT W tst_fontman_por;

EXPORT CONST TC tst_nm_conn[] = {(TC)'t', (TC)'s', (TC)'t', TNULL};

EXPORT CONST TC tst_nm_f3[] = {(TC)'g', (TC)'a', (TC)'m', (TC)'m', (TC)'a', TNULL};

/* ／tst／alpha */
EXPORT CONST TC tst_path_abs[] = {(TC)TC_FDLM, (TC)'t', (TC)'s', (TC)'t',
                             (TC)TC_FDLM, (TC)'a', (TC)'l', (TC)'p',
                             (TC)'h', (TC)'a', TNULL};

EXPORT LINK tst_fl_root;

EXPORT LINK tst_fl_a;

EXPORT LINK tst_fl_b;

EXPORT Bool tst_fs_up;

EXPORT CONST TC tst_nm_screen[] = {(TC)'S', (TC)'C', (TC)'R', (TC)'E', (TC)'E',
                              (TC)'N', TNULL};

EXPORT CONST TC tst_nm_rda0[] = {(TC)'r', (TC)'d', (TC)'a', (TC)'0', TNULL};

EXPORT CONST TC tst_nm_rda1[] = {(TC)'r', (TC)'d', (TC)'a', (TC)'1', TNULL};

EXPORT CONST TC tst_nm_rda2[] = {(TC)'r', (TC)'d', (TC)'a', (TC)'2', TNULL};

/* 画面の領域が 1 色で埋まっているか。画面は dpt_bmp ではないので
 * 描画環境を通して読む。 */
EXPORT Bool tst_scr_solid(GID g, W l, W t, W r, W b, UW val)
{
	W x;
	W y;

	for (y = t; y < b; y++) {
		for (x = l; x < r; x++) {
			PNT p;
			PIXVAL v = 0;

			p.x = (H)x;
			p.y = (H)y;
			if (gtst_pnt(g, p, &v) != 0 || (UW)v != val) {
				return False;
			}
		}
	}
	return True;
}

/* 検査で使う TC の名前。
 *
 * **BTRON_TEST の中に書けない。** マクロの引数は括弧しか見ないので、
 * 配列初期化子の `,` でマクロの引数が割れてしまう（波括弧は効かない）。 */
EXPORT CONST TC tst_nm_test[] = {(TC)'T', (TC)'E', (TC)'S', (TC)'T', TNULL};

/* 預かっている後片付けを流してから空きページ数を数える。
 *
 * 終了したタスクの TCB とスタックは**別のタスクへ切り替わってから**返る
 * （knl_defer_del / knl_reclaim）。流さずに数えると、基準を取った後で
 * それが返って空きページが増え、漏れていないのに一致しなくなる。
 * 1 度譲ればディスパッチャの入口を通るので片付く（Phase 2 ㊱）。 */
EXPORT UD tst_free_pages_settled(void)
{
	(void)dly_tsk(0);
	return pmem_free_pages();
}

/* 記録の中で文字が変わった回数。時分割が効いていないと 1 回しかない。 */
EXPORT W tst_seq_transitions(void)
{
	W n = 0;
	W i;

	for (i = 1; i < tst_seq_n; i++) {
		if (tst_seq_log[i] != tst_seq_log[i - 1]) {
			n++;
		}
	}
	return n;
}

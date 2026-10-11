/*
 * demo.c — 画面を手で見るための常駐デモ
 *
 * **これは BTRON3 の仕様ではない。** 起動時検査が終わった後、電源を
 * 落とさずに画面を残して「GUI らしい状態」を見せるためだけのもの。
 * ESP に `\BTRON\HOLD` があるときだけ走る（`run_qemu.py --gui`）。
 *
 * ------------------------------------------------------------------------
 * なぜ要るか
 * ------------------------------------------------------------------------
 *
 * ここまでウィンドウマネージャは**検査の中でしか動いていなかった**。
 * 開いて、確かめて、閉じる。しかも画面コンソールが同じ画面に文字を
 * 流し続けるので、窓はすぐ上書きされて消える。
 *
 * つまり「部品はあるが画面として組み上がっていない」状態だった。
 * このデモは最小の組み上げで、3 つのことをする。
 *
 *   1. 画面の持ち主をコンソールからウィンドウマネージャへ渡す
 *      （コンソールの複製を外し、画面を背景で塗り直す）
 *   2. ウィンドウを 2 枚開いて中身を描く
 *   3. イベントを待ち続ける（電源を落とさない）
 *
 * 入力デバイスのドライバが入ったら、3 がそのまま本物の主ループになる。
 */

#include <btron/dp.h>
#include <btron/event.h>
#include <btron/fontman.h>
#include <btron/hal.h>
#include <btron/proc.h>
#include <btron/window.h>

/* 作業領域に中身を描く。 */
LOCAL void paint(WID wid, CONST TC *line1, CONST TC *line2)
{
	GID g = wget_gid(wid);
	RECT r;
	RECT box;

	if (g < 1) {
		return;
	}
	(void)gget_fra(g, &r);
	/* 背景 */
	(void)gfil_rec(g, r, (CONST PAT *)WHITE0, 0, G_STORE);
	/* 枠の内側に細い罫 */
	box = r;
	box.c.left = (H)(r.c.left + 6);
	box.c.top = (H)(r.c.top + 6);
	box.c.right = (H)(r.c.right - 6);
	box.c.bottom = (H)(r.c.bottom - 6);
	if (box.c.right > box.c.left && box.c.bottom > box.c.top) {
		(void)gfra_rec(g, box, LATTR_MAKE(LN_SOLID, 1),
		               (CONST PAT *)BLACK50, 0, G_STORE);
	}
	/* 文字。前景は黒、背景は透明。 */
	(void)gset_chc(g, COL_BLACK, COL_TRANS);
	(void)gdra_stp(g, 16, 32, line1, 64, G_STORE);
	(void)gdra_stp(g, 16, 54, line2, 64, G_STORE);

	/* 塗り分けの見本。標準パターンの濃度が並ぶ。 */
	{
		CONST PAT *pats[5];
		W i;

		pats[0] = (CONST PAT *)BLACK12;
		pats[1] = (CONST PAT *)BLACK25;
		pats[2] = (CONST PAT *)BLACK50;
		pats[3] = (CONST PAT *)BLACK75;
		pats[4] = (CONST PAT *)BLACK100;
		for (i = 0; i < 5; i++) {
			RECT s;

			s.c.left = (H)(16 + i * 26);
			s.c.top = 70;
			s.c.right = (H)(s.c.left + 22);
			s.c.bottom = 94;
			if (s.c.right < box.c.right) {
				(void)gfil_rec(g, s, pats[i], 0, G_STORE);
			}
		}
	}
}

EXPORT void win_demo_hold(void)
{
	CONST TC t1[] = {(TC)'B', (TC)'T', (TC)'R', (TC)'O', (TC)'N', (TC)'3',
	                 TNULL};
	CONST TC t2[] = {(TC)0x30A6, (TC)0x30A3, (TC)0x30F3, (TC)0x30C9,
	                 (TC)0x30A6, TNULL}; /* ウィンドウ */
	CONST TC l1[] = {(TC)0x753B, (TC)0x9762, (TC)0x306F, (TC)0x30A6,
	                 (TC)0x30A3, (TC)0x30F3, (TC)0x30C9, (TC)0x30A6,
	                 (TC)0x30DE, (TC)0x30CD, (TC)0x30FC, (TC)0x30B8,
	                 (TC)0x30E3, TNULL}; /* 画面はウィンドウマネージャ */
	CONST TC l2[] = {(TC)0x304C, (TC)0x6301, (TC)0x3063, (TC)0x3066,
	                 (TC)0x3044, (TC)0x307E, (TC)0x3059, TNULL};
	                 /* が持っています */
	CONST TC m1[] = {(TC)0x91CD, (TC)0x306A, (TC)0x308A, (TC)0x306F,
	                 (TC)0x524D, (TC)0x7F6E, (TC)0x9577, (TC)0x65B9,
	                 (TC)0x5F62, TNULL}; /* 重なりは前置長方形 */
	CONST TC m2[] = {(TC)0x30EA, (TC)0x30B9, (TC)0x30C8, (TC)0x3067,
	                 TNULL}; /* リストで */
	RECT r;
	WID w1;
	WID w2;
	WEVENT ev;

	/* **画面の持ち主をコンソールから取り上げる。**
	 * これをしないと、以後の出力が窓の上に流れてくる。 */
	hal_console_tee(0);

	/* フォントマネージャを立てる。タイトルと本文の字形に要る。 */
	(void)fontman_start(1);

	/* 机の上を塗る。**これをしないとコンソールの文字が残る。**
	 * ウィンドウを動かしたときに現れる背景 (winman の expose) と
	 * 同じパターンにしておく。 */
	{
		CONST TC dev[] = {(TC)'S', (TC)'C', (TC)'R', (TC)'E', (TC)'E',
		                  (TC)'N', TNULL};
		GID sg = gopn_dev(dev, 0);

		if (sg >= 1) {
			RECT b;

			(void)gget_bnd(sg, &b);
			(void)gfil_rec(sg, b, (CONST PAT *)BLACK50, 0, G_STORE);
			(void)gcls_env(sg);
		}
	}

	if (win_init() != EX_OK) {
		/* 画面が無い構成。電源を落とさずに止まるだけにする
		 * （HOLD の印は「落とすな」という指示なので従う）。 */
		for (;;) {
			(void)dly_tsk(1000);
		}
	}

	r.c.left = 40;
	r.c.top = 60;
	r.c.right = 420;
	r.c.bottom = 260;
	w1 = wopn_wnd(WA_NORMAL, 0, &r, 0, 0, t1, 0, 0);

	r.c.left = 240;
	r.c.top = 180;
	r.c.right = 660;
	r.c.bottom = 420;
	w2 = wopn_wnd(WA_NORMAL, 0, &r, 0, 0, t2, 0, 0);

	if (w1 >= 1) {
		paint(w1, l1, l2);
	}
	if (w2 >= 1) {
		paint(w2, m1, m2);
	}

	/* 主ループ。入力デバイスのドライバが入れば、ここがそのまま
	 * 本物のループになる。いまは誰もイベントを起こさないので待ち続ける。 */
	for (;;) {
		W t = wget_evt(&ev, W_NOWAIT);

		if (t == EV_SWITCH && ev.s.cmd == W_SWITCH) {
			(void)wswi_wnd(ev.s.wid, &ev);
		}
		(void)dly_tsk(50);
	}
}

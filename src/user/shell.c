/*
 * shell.c — ユーザ特権 (EL0) で動く外殻の最小形
 *
 * **これは BTRON3 仕様の一部ではない。** 詳細 E22「1 プロセス 1 サーバ」
 * のとおり外殻をユーザランドへ移せることを、実際に動かして確かめる
 * ためのもの。
 *
 * ------------------------------------------------------------------------
 * ここで何が起きているか
 * ------------------------------------------------------------------------
 *
 * このプログラムはカーネルの外、保護されたユーザ特権で走っている。
 * それでいて
 *
 *   - 画面に直接描いている（`gopn_dev("SCREEN")` = 画面の貸し出し）
 *   - 文字を描いている（フォントマネージャの共有メモリを読む）
 *   - 入力を受けている（イベント管理のシステムコール）
 *
 * 描画は**システムコールではない**。ディスプレイプリミティブは
 * ライブラリとしてこのプログラムに結合されていて、1 画素ごとに
 * 特権境界を越えたりしない（詳細 E1）。越えるのは
 *
 *   1. 画面を借りるとき（1 度だけ）
 *   2. フォントマネージャの置き場を引くとき（1 度だけ）
 *   3. イベントを取り出すとき
 *
 * の 3 か所だけである。
 */

#include <btron/device.h>
#include <btron/dp.h>
#include <btron/event.h>
#include <btron/fontman.h>
#include <btron/message.h>
#include <btron/proc.h>

LOCAL CONST TC dev_com[] = {(TC)'c', (TC)'o', (TC)'m', TNULL};
LOCAL CONST TC dev_screen[] = {(TC)'S', (TC)'C', (TC)'R', (TC)'E', (TC)'E',
                               (TC)'N', TNULL};

/* 「外殻がユーザ特権で動いています」 */
LOCAL CONST TC msg1[] = {(TC)0x5916, (TC)0x6BBB, (TC)0x304C, (TC)0x30E6,
                         (TC)0x30FC, (TC)0x30B6, (TC)0x7279, (TC)0x6A29,
                         (TC)0x3067, (TC)0x52D5, (TC)0x3044, (TC)0x3066,
                         (TC)0x3044, (TC)0x307E, (TC)0x3059, TNULL};
/* 「描画はシステムコールではありません」 */
LOCAL CONST TC msg2[] = {(TC)0x63CF, (TC)0x753B, (TC)0x306F, (TC)0x30B7,
                         (TC)0x30B9, (TC)0x30C6, (TC)0x30E0, (TC)0x30B3,
                         (TC)0x30FC, (TC)0x30EB, (TC)0x3067, (TC)0x306F,
                         (TC)0x3042, (TC)0x308A, (TC)0x307E, (TC)0x305B,
                         (TC)0x3093, TNULL};

LOCAL W dd;

LOCAL void say(CONST B *s)
{
	W n = 0;

	while (s[n] != 0) {
		n++;
	}
	(void)wri_dev(dd, 0, s, n, 0, 0);
}

/* 場面を描く。**ポインタが動いた跡を直すのにも使う**ので、何度呼んでも
 * 同じ絵になるようにしてある（クリッピングを掛けて呼べば一部だけ直る）。
 * ここはウィンドウマネージャの居ない単体の外殻なので、画面を持っている
 * 自分が合成する側になる（詳細 E2）。 */
LOCAL void scene(GID g, CONST RECT *b, CONST RECT *r)
{
	W i;

	/* 机の上。 */
	(void)gfil_rec(g, *b, (CONST PAT *)BLACK50, 0, G_STORE);

	/* 枠と中身。 */
	(void)gfil_rec(g, *r, (CONST PAT *)WHITE0, 0, G_STORE);
	(void)gfra_rec(g, *r, LATTR_MAKE(LN_SOLID, 2), (CONST PAT *)BLACK100, 0,
	               G_STORE);

	/* 文字。字形はフォントマネージャの共有メモリから引く。
	 * **サーバの置き場を 1 度引くだけで、文字ごとの IPC は無い。** */
	(void)gset_chc(g, COL_BLACK, COL_TRANS);
	(void)gdra_stp(g, (W)(r->c.left + 20), (W)(r->c.top + 40), msg1, 64,
	               G_STORE);
	(void)gdra_stp(g, (W)(r->c.left + 20), (W)(r->c.top + 70), msg2, 64,
	               G_STORE);

	/* 塗り分けの見本。 */
	{
		CONST PAT *pats[5];

		pats[0] = (CONST PAT *)BLACK12;
		pats[1] = (CONST PAT *)BLACK25;
		pats[2] = (CONST PAT *)BLACK50;
		pats[3] = (CONST PAT *)BLACK75;
		pats[4] = (CONST PAT *)BLACK100;
		for (i = 0; i < 5; i++) {
			RECT s;

			s.c.left = (H)(r->c.left + 20 + i * 30);
			s.c.top = (H)(r->c.top + 100);
			s.c.right = (H)(s.c.left + 26);
			s.c.bottom = (H)(s.c.top + 30);
			(void)gfil_rec(g, s, pats[i], 0, G_STORE);
		}
	}
}

EXPORT W MAIN(MESSAGE *msg)
{
	GID g;
	RECT b;
	RECT r;
	RECT was;  /* 前のコマでポインタが居た所 */
	Bool had;
	W i;

	(void)msg;
	dd = opn_dev(dev_com, D_WRITE, 0);
	if (dd < 1) {
		return -1;
	}
	say("  [外殻] ユーザ特権で起動しました\n");

	/* 1. 画面を借りる。ここだけがシステムコール。 */
	g = gopn_dev(dev_screen, 0);
	if (g < 1) {
		say("  [外殻] 画面がありません（シリアルだけの構成）\n");
		(void)cls_dev(dd, 0, 0);
		return 0;
	}
	say("  [外殻] 画面を借りました\n");

	if (gget_bnd(g, &b) != EG_OK) {
		say("  [外殻] 画面の大きさが取れません\n");
		(void)cls_dev(dd, 0, 0);
		return -1;
	}

	/* 2〜5. 机・枠・文字・見本をまとめて描く。
	 * **ここから先はシステムコールを呼んでいない。** */
	r.c.left = (H)(b.c.left + 40);
	r.c.top = (H)(b.c.top + 40);
	r.c.right = (H)(r.c.left + 520);
	r.c.bottom = (H)(r.c.top + 180);
	scene(g, &b, &r);

	/* 6. 描けたことを画素で確かめる。
	 *    **目で見るだけでは検査にならない**ので、読み返す。 */
	{
		PNT p;
		PIXVAL v = 0;

		p.x = (H)(r.c.left + 10);
		p.y = (H)(r.c.top + 10);
		if (gtst_pnt(g, p, &v) == 0 && (UW)v == 0x00FFFFFFU) {
			say("  [外殻] 画素を読み返して確かめました\n");
		} else {
			say("  [外殻] *** 画素が合いません ***\n");
			(void)cls_dev(dd, 0, 0);
			return -2;
		}
	}

	/* 7. ポインタを出し、入力を少し待つ。 */
	(void)chg_emk(EM_ALL);
	(void)gini_ptr();
	had = False;
	/* 1.2 秒ほど回す。**画面を撮るのに要る長さ**でもある
	 * （tools/run_qemu.py --screenshot は変化したコマだけを残す）。 */
	for (i = 0; i < 60; i++) {
		EVENT ev;

		RECT now;
		Bool has;

		gptr_follow();
		/* **画面を持っている者が合成する**（詳細 E2）。
		 * ポインタが居た所と行った所だけ場面を描き直してから重ねる。
		 * 退避も書き戻しも要らない。 */
		has = (gptr_rect(&now) != 0) ? True : False;
		if (had || has) {
			GRGN cl;

			cl.ns = 0;
			cl.r = had ? was : now;
			if (had && has) {
				if (now.c.left < cl.r.c.left) {
					cl.r.c.left = now.c.left;
				}
				if (now.c.top < cl.r.c.top) {
					cl.r.c.top = now.c.top;
				}
				if (now.c.right > cl.r.c.right) {
					cl.r.c.right = now.c.right;
				}
				if (now.c.bottom > cl.r.c.bottom) {
					cl.r.c.bottom = now.c.bottom;
				}
			}
			(void)gset_clp(g, &cl);
			scene(g, &b, &r);
			(void)gset_clp(g, 0);
			gptr_paint(0);
		}
		was = now;
		had = has;
		if (get_evt(EM_ALL, &ev, CLR) == EV_KEYDWN) {
			say("  [外殻] キーを受け取りました\n");
		}
		(void)dly_tsk(20);
	}

	say("  [外殻] 終わります\n");
	(void)gcls_env(g);
	(void)cls_dev(dd, 0, 0);
	return 0;
}

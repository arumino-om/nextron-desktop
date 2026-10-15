/*
 * demo.c — 画面を手で触るための常駐デモ
 *
 * **これは BTRON3 の仕様ではない。** 起動時検査が終わった後、電源を
 * 落とさずに画面を残して「GUI らしい状態」を見せるためだけのもの。
 * ESP に `\BTRON\HOLD` があるときだけ走る（`run_qemu.py --gui`）。
 *
 * ------------------------------------------------------------------------
 * なぜ要るか
 * ------------------------------------------------------------------------
 *
 * ウィンドウマネージャは**検査の中でしか動いていなかった**。開いて、
 * 確かめて、閉じる。しかも画面コンソールが同じ画面に文字を流し続けるので、
 * 窓はすぐ上書きされて消える。つまり「部品はあるが画面として組み上がって
 * いない」状態だった。このデモは最小の組み上げで、
 *
 *   1. 画面の持ち主をコンソールからウィンドウマネージャへ渡す
 *   2. ウィンドウを 2 枚開いて中身を描く
 *   3. ポインタを出し、入力を受け取り続ける
 *
 * をする。3 が本物の主ループで、キーボードと PD のドライバが入ったので
 * **実際に触れる**ようになった。
 *
 * ------------------------------------------------------------------------
 * ポインタを気にしなくてよくなった
 * ------------------------------------------------------------------------
 *
 * 描画先は**自分のウィンドウの裏画面**（詳細 E2）なので、ポインタに
 * 触れようがない。直描きの頃は `gdsp_ptr(0)` … `gdsp_ptr(1)` で囲む
 * 必要があったが、もう要らない。囲みを忘れたときに出る書き残しも
 * 起きない。
 *
 * ------------------------------------------------------------------------
 * 隠れた部分は自分で描き直す
 * ------------------------------------------------------------------------
 *
 * 書いた点は配列に覚えておき、変形されたら描き直す。重なりで隠れた分は
 * 裏画面に残っているので描き直さなくてよいが、**変形すると裏画面を
 * 取り直す**ので中身は失われる。そこは仕様どおり再表示要求で描き直す。
 */

#include <btron/dp.h>
#include <btron/event.h>
#include <btron/fontman.h>
#include <btron/message.h>
#include <btron/hal.h>
#include <btron/proc.h>
#include <btron/window.h>

/* エラーコードを 10 進で出す。原因を黙らせないため。 */
LOCAL void put_err(W v)
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

/* 書いた点の記録。作業領域の相対座標で持つ。 */
#define DEMO_MAX_DOT 512
LOCAL PNT dot[DEMO_MAX_DOT];
LOCAL W ndot;

/* 打った文字の記録。 */
#define DEMO_MAX_TYPED 24
LOCAL TC typed[DEMO_MAX_TYPED + 1];
LOCAL W ntyped;

/* ------------------------------------------------------------------ */
/* 小さな道具                                                          */
/* ------------------------------------------------------------------ */

/* 10 進を TC 列に足す。戻り値は次の位置。 */
LOCAL W put_num(TC *buf, W at, W max, W v)
{
	B tmp[8];
	W n = 0;
	UW u = (v < 0) ? (UW)(-v) : (UW)v;

	if (v < 0 && at < max) {
		buf[at++] = (TC)'-';
	}
	do {
		tmp[n++] = (B)('0' + (u % 10U));
		u /= 10U;
	} while (u > 0 && n < (W)sizeof(tmp));
	while (n > 0 && at < max) {
		buf[at++] = (TC)(UB)tmp[--n];
	}
	return at;
}

LOCAL W put_ascii(TC *buf, W at, W max, CONST B *s)
{
	while (*s != 0 && at < max) {
		buf[at++] = (TC)(UB)*s++;
	}
	return at;
}

/* ------------------------------------------------------------------ */
/* 描画                                                                */
/* ------------------------------------------------------------------ */

/* 状態行。PD の位置とボタン、最後に打った文字を出す。
 *
 * 毎回全部描き直すと点滅するので、呼ぶ側が「変わったとき」だけ呼ぶ。 */
LOCAL void status(WID wid, PNT p, UW stat)
{
	GID g = wget_gid(wid);
	TC buf[80];
	W n = 0;
	W w;
	RECT fr;
	RECT box;

	if (g < 1) {
		return;
	}
	(void)gget_fra(g, &fr);
	box.c.left = (H)(fr.c.left + 8);
	box.c.top = (H)(fr.c.bottom - 28);
	box.c.right = (H)(fr.c.right - 8);
	box.c.bottom = (H)(fr.c.bottom - 8);
	if (box.c.right <= box.c.left || box.c.bottom <= box.c.top) {
		return;
	}

	n = put_ascii(buf, n, 72, "PD ");
	n = put_num(buf, n, 72, p.x);
	n = put_ascii(buf, n, 72, ", ");
	n = put_num(buf, n, 72, p.y);
	n = put_ascii(buf, n, 72, ((stat & ES_BUT) != 0) ? "  [BUT]" : "       ");
	n = put_ascii(buf, n, 72, "  ");
	{
		W i;

		for (i = 0; i < ntyped && n < 72; i++) {
			buf[n++] = typed[i];
		}
	}
	buf[n] = TNULL;

	/* **消してから書くと、消えている間が見える。** 状態行は PD を
	 * 動かすたびに書き替わるので、そこがちらつきとして目に付く。
	 * 文字の背景を不透明にすれば文字のセルは背景ごと 1 回で書き替わる
	 * ので、白で消すのは**文字が覆わない余りだけ**にする。 */
	w = (n > 0) ? gget_stw(g, buf, n, 0, 0) : 0;
	if (w > 0) {
		RECT s;

		(void)gset_chc(g, COL_BLACK, COL_WHITE);
		(void)gdra_stp(g, (W)(box.c.left + 2), (W)(box.c.top + 15), buf,
		               n, G_STORE);
		/* 文字の左の 2 画素 */
		s = box;
		s.c.right = (H)(box.c.left + 2);
		(void)gfil_rec(g, s, (CONST PAT *)WHITE0, 0, G_STORE);
		/* 文字列より右の余り */
		s = box;
		s.c.left = (H)(box.c.left + 2 + w);
		if (s.c.left < s.c.right) {
			(void)gfil_rec(g, s, (CONST PAT *)WHITE0, 0, G_STORE);
		}
		/* 文字枠（高さ 16）より下の余り */
		s = box;
		s.c.left = (H)(box.c.left + 2);
		s.c.right = (H)(box.c.left + 2 + w);
		s.c.top = (H)(box.c.top + 16);
		if (s.c.top < s.c.bottom && s.c.left < s.c.right) {
			(void)gfil_rec(g, s, (CONST PAT *)WHITE0, 0, G_STORE);
		}
		return;
	}
	(void)gfil_rec(g, box, (CONST PAT *)WHITE0, 0, G_STORE);
}

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

/* 記録した点を描く。 */
LOCAL void draw_dot(GID g, PNT p)
{
	RECT s;

	s.c.left = (H)(p.x - 1);
	s.c.top = (H)(p.y - 1);
	s.c.right = (H)(p.x + 2);
	s.c.bottom = (H)(p.y + 2);
	(void)gfil_rec(g, s, (CONST PAT *)BLACK100, 0, G_STORE);
}

LOCAL void paint_dots(WID wid)
{
	GID g = wget_gid(wid);
	W i;

	if (g < 1) {
		return;
	}
	for (i = 0; i < ndot; i++) {
		draw_dot(g, dot[i]);
	}
}

/* 再表示要求が出ていれば描き直す。描き直したら True。
 *
 * 仕様 §3.1.2.3 の `wsta_dsp` 〜 `wend_dsp` で囲む。囲みの中では
 * マネージャが再表示領域をクリッピングに入れてくれるので、
 * 全部描き直しても外へはみ出さない。 */
LOCAL Bool redisplay(WID wid, CONST TC *line1, CONST TC *line2, Bool dots)
{
	RECT r;

	if (wid < 1 || wchk_dsp(wid) <= 0) {
		return False;
	}
	if (wsta_dsp(wid, &r, 0) > 0) {
		paint(wid, line1, line2);
		if (dots) {
			paint_dots(wid);
		}
	}
	(void)wend_dsp(wid);
	return True;
}

/* 枠やハンドルを掴んだらドラッグに入る。
 *
 * 仕様 §3.1.4: `wmov_drg` の位置コードは `W_FRAM` と `W_LTHD`〜`W_RBHD`、
 * `wrsz_drg` は `W_LTHD`〜`W_RBHD`。**つまり四隅は移動にも変形にも
 * 使える。** ここでは四隅を変形、枠を移動に割り当てる。
 * タイトル部は移動に使えない（仕様の位置コードに入っていない）。 */
LOCAL void drag(WEVENT *evt)
{
	W code = evt->s.cmd;
	RECT nr;

	if (code >= W_LTHD && code <= W_RBHD) {
		(void)wrsz_drg(evt, 0, &nr);
	} else if (code == W_FRAM) {
		(void)wmov_drg(evt, &nr);
	}
	/* 動かした窓自身の再表示は戻り値 1 で知らされるが、
	 * 再表示領域は設定されているので、下の再表示処理が拾う。 */
}

/* ------------------------------------------------------------------ */
/* 本体                                                                */
/* ------------------------------------------------------------------ */

/* デモ本体。**プロセスのメインタスクとして走る。**
 *
 * 外殻はプロセスである（詳細 E22）。ウィンドウマネージャをサーバに
 * した時点で、それを立てる側もプロセスでなければならなくなった
 * （ポートもグローバル名もタスクも、プロセスに属するものだから）。 */
LOCAL W demo_main(MESSAGE *msg)
{
	CONST TC t1[] = {(TC)'B', (TC)'T', (TC)'R', (TC)'O', (TC)'N', (TC)'3',
	                 TNULL};
	CONST TC t2[] = {(TC)0x30A6, (TC)0x30A3, (TC)0x30F3, (TC)0x30C9,
	                 (TC)0x30A6, TNULL}; /* ウィンドウ */
	CONST TC l1[] = {(TC)0x30AD, (TC)0x30FC, (TC)0x3092, (TC)0x6253,
	                 (TC)0x3064, (TC)0x3068, (TC)0x4E0B, (TC)0x306B,
	                 (TC)0x51FA, (TC)0x307E, (TC)0x3059, TNULL};
	                 /* キーを打つと下に出ます */
	CONST TC l2[] = {(TC)0x5074, (TC)0x306E, (TC)0x7A93, (TC)0x3067,
	                 (TC)0x7D75, (TC)0x304C, (TC)0x63CF, (TC)0x3051,
	                 (TC)0x307E, (TC)0x3059, TNULL};
	                 /* 側の窓で絵が描けます */
	CONST TC m1[] = {(TC)0x3053, (TC)0x3053, (TC)0x3092, (TC)0x62BC,
	                 (TC)0x3059, (TC)0x3068, (TC)0x70B9, (TC)0x304C,
	                 (TC)0x6253, (TC)0x3066, (TC)0x307E, (TC)0x3059,
	                 TNULL}; /* ここを押すと点が打てます */
	CONST TC m2[] = {(TC)0x91CD, (TC)0x306A, (TC)0x308A, (TC)0x306F,
	                 (TC)0x524D, (TC)0x7F6E, (TC)0x9577, (TC)0x65B9,
	                 (TC)0x5F62, (TC)0x30EA, (TC)0x30B9, (TC)0x30C8,
	                 TNULL}; /* 重なりは前置長方形リスト */
	RECT r;
	WID w1;
	WID w2;
	WEVENT ev;
	Bool win_ok;
	PNT last;
	UW last_stat = 0;
	Bool drawing = False;

	(void)msg;

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

	{
		WERR wer = win_launch();

		if (wer < 0) {
			/* **黙って止まらない。** 画面に机だけが出て「なぜ窓が
			 * 無いのか」が分からない、という状態を作らない。 */
			hal_console_tee(0);
			hal_console_puts("  ウィンドウマネージャを立てられません (");
			put_err(wer);
			hal_console_puts(")\n");
		}
		win_ok = (wer >= 0);
	}
	if (!win_ok) {
		/* 画面が無い構成。電源を落とさずに止まるだけにする
		 * （HOLD の印は「落とすな」という指示なので従う）。 */
		for (;;) {
			(void)dly_tsk(1000);
		}
	}

	r.c.left = 40;
	r.c.top = 60;
	r.c.right = 420;
	r.c.bottom = 300;
	w1 = wopn_wnd(WA_NORMAL, 0, &r, 0, 0, t1, 0, 0);

	r.c.left = 240;
	r.c.top = 200;
	r.c.right = 680;
	r.c.bottom = 440;
	w2 = wopn_wnd(WA_NORMAL, 0, &r, 0, 0, t2, 0, 0);

	if (w1 >= 1) {
		paint(w1, l1, l2);
	}
	if (w2 >= 1) {
		paint(w2, m1, m2);
	}

	/* ポインタを初期状態にする。**出す順を気にしなくてよい** ——
	 * 画面に重ねるのはマネージャで、窓は裏画面に描かれる（詳細 E2）。 */
	(void)gini_ptr();
	(void)get_pdp(&last);
	if (w1 >= 1) {
		status(w1, last, 0);
	}

	for (;;) {
		W t = wget_evt(&ev, W_NOWAIT);
		PNT now;
		Bool need_status = False;

		switch (t) {
		case EV_SWITCH:
			/* 入力受付状態でない窓が押された。
			 * **切換はマネージャがもう済ませている**（仕様
			 * §3.1.2.2「このイベントを取り出した時点では、既に wid で
			 * 示されるウィンドウに入力受付状態は切り換わっており」）。
			 * 切り換えついでに枠を掴んでいたらドラッグへ入る。 */
			drag(&ev);
			need_status = True;
			break;

		case EV_BUTDWN:
			/* 入力受付状態の窓への押し下げ。どこを押したかは
			 * 自分で引く（仕様はイベントに位置コードを載せない）。 */
			{
				PNT lp;
				W hit = 0;
				W code = wfnd_wnd(&ev.e.pos, &lp, &hit);

				if (code > W_WORK && hit >= 1) {
					/* 枠やハンドル。SEVENT の形に組み直して渡す。 */
					WEVENT de;

					de.s.type = EV_BUTDWN;
					de.s.time = ev.e.time;
					de.s.pos = ev.e.pos;
					de.s.cmd = (H)code;
					de.s.wid = (H)hit;
					de.s.stat = ev.e.stat;
					drag(&de);
					need_status = True;
					break;
				}
			}
			drawing = True;
			last_stat = ev.e.stat | ES_BUT;
			need_status = True;
			break;

		case EV_BUTUP:
			drawing = False;
			last_stat = ev.e.stat & ~(UW)ES_BUT;
			need_status = True;
			break;

		case EV_KEYDWN:
		case EV_AUTKEY:
			if (ev.e.data.key.code != TNULL) {
				TC c = ev.e.data.key.code;

				if (c == 0x0008) {
					/* 後退。1 文字消す。 */
					if (ntyped > 0) {
						ntyped--;
					}
				} else if (c == 0x000A || c == 0x000D) {
					ntyped = 0; /* 改段落で消す */
				} else if (c >= 0x20) {
					if (ntyped >= DEMO_MAX_TYPED) {
						ntyped = 0;
					}
					typed[ntyped++] = c;
				}
				need_status = True;
			}
			break;

		default:
			break;
		}

		/* マネージャからの通知を引き取る。
		 * **溜めるとメッセージキューが詰まる。** いまは EV_INACT
		 * （入力不可状態になった）だけで、描き直しは下の再表示要求で
		 * 分かるので、中身は見ずに捨てる。 */
		{
			MESSAGE m;

			while (rcv_msg(MM_MNG0, &m, (W)sizeof(m), NOWAIT) >= 0) {
				/* 捨てる */
			}
		}

		/* 再表示要求 (§3.1.2.3)。**これが BTRON の再表示モデル。**
		 * マネージャは隠れた中身を保存しないので、前後が変わったら
		 * アプリが描き直す。`wsta_dsp` 〜 `wend_dsp` で囲む。 */
		if (redisplay(w1, l1, l2, False) || redisplay(w2, m1, m2, True)) {
			need_status = True;
		}

		/* ポインタを PD に追従させる。**ここが「システムが動かす」の
		 * 実体。**割込みから画面を書くのは避けたいので、主ループが呼ぶ。 */

		(void)get_pdp(&now);
		if (now.x != last.x || now.y != last.y) {
			last = now;
			need_status = True;
		}

		/* 押しながら動かしたら点を打つ。 */
		if (drawing && w2 >= 1) {
			PNT lp;
			W hit = 0;
			W code = wfnd_wnd(&now, &lp, &hit);

			if (code == W_WORK && hit == w2 && ndot < DEMO_MAX_DOT) {
				dot[ndot++] = lp;
							draw_dot(wget_gid(w2), lp);
						}
		}

		if (need_status && w1 >= 1) {
					status(w1, last, last_stat);
				}
		(void)dly_tsk(10);
	}
}

EXPORT void win_demo_hold(void)
{
	CONST TC name[] = {(TC)'D', (TC)'E', (TC)'M', (TC)'O', TNULL};
	WERR pid;

	/* **デモをプロセスとして起こす。**
	 *
	 * ウィンドウマネージャをサーバにした時点で、それを立てる側も
	 * プロセスでなければならなくなった。ポートもグローバル名も
	 * サブタスクも、プロセスに属するものだからである。
	 * 検査タスクはプロセスに属していないので、ここで 1 つ作る。 */
	pid = prc_create(demo_main, 2, name, 0);
	if (pid < 1) {
		hal_console_puts("  デモのプロセスを作れません\n");
	}
	/* 親は待つだけ。HOLD の印は「電源を落とすな」という指示なので、
	 * 戻らずに居続ける。 */
	for (;;) {
		(void)dly_tsk(1000);
	}
}

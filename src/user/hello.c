/*
 * hello.c — ユーザ特権 (EL0) で走る最初のプログラム
 *
 * **これは BTRON3 仕様の一部ではない。** `cre_prc` が実身から
 * 実行プログラムレコードを読んでプロセスを起こせることを、実際に
 * 動かして確かめるためのもの。
 *
 * 仕様 §1.1.6 の形式-A でメッセージを受け取る。
 *
 *   W MAIN (MESSAGE *msg)
 *
 * やることは 3 つ。
 *
 *   1. シリアル ("com") を開いて文字を書く
 *      — システムコールが特権境界を越えて届いていることの証拠
 *   2. 起動メッセージを読む
 *      — 仕様の「生成時に指定したメッセージを直接的に得る」経路
 *   3. 終了コードを返す
 *      — 親プロセスに正常終了メッセージが届く
 */

#include <btron/device.h>
#include <btron/message.h>
#include <btron/proc.h>

LOCAL CONST TC dev_com[] = {(TC)'c', (TC)'o', (TC)'m', TNULL};

LOCAL void say(W dd, CONST B *s)
{
	W n = 0;
	W asz = 0;

	while (s[n] != 0) {
		n++;
	}
	(void)wri_dev(dd, 0, s, n, &asz, 0);
}

EXPORT W MAIN(MESSAGE *msg)
{
	W dd;
	W code = 0;

	dd = opn_dev(dev_com, D_WRITE, 0);
	if (dd < 1) {
		return -1;
	}
	say(dd, "  [EL0] ユーザプログラムが動いています\n");

	/* **特権が本当に落ちているかを確かめる経路。**
	 *
	 * 起動メッセージが 'F' で始まっていたら、カーネルの番地を
	 * システムコールを通さずに直接読む。EL0 なら変換フォルトになって
	 * このプロセスだけが落ち、親には異常終了が届く。EL1 のままなら
	 * 読めてしまい、下の行まで進んでしまう。
	 *
	 * **「断られた」ことを報告する側では確かめられない。**
	 * usr_ok() はアドレスを見るだけなので、特権が落ちていなくても
	 * 同じ答えを返す。実際に触るしかない。 */
	if (msg != 0 && msg->msg_size > 0 &&
	    msg->msg_body.ANYMSG.msg_str[0] == (UB)'F') {
		volatile CONST UW *k = (volatile CONST UW *)(CONST VP)
		    0xFFFFFF0000000000ULL;
		UW v;

		say(dd, "  [EL0] カーネル番地を直接読みます\n");
		v = *k;
		say(dd, "  [EL0] *** 読めてしまいました（EL1 のまま） ***\n");
		(void)cls_dev(dd, 0, 0);
		return (W)(v | 1U);
	}

	/* 起動メッセージ。cre_prc に渡した内容がそのまま来る。 */
	if (msg != 0 && msg->msg_size > 0) {
		say(dd, "  [EL0] 起動メッセージ: ");
		(void)wri_dev(dd, 0, (CONST B *)(CONST VP)msg->msg_body.ANYMSG.msg_str,
		              msg->msg_size, 0, 0);
		say(dd, "\n");
		/* 終了コードはメッセージの先頭バイトから作る。
		 * 親が「渡した値が届いた」ことを確かめられるようにする。 */
		code = (W)msg->msg_body.ANYMSG.msg_str[0];
	}

	/* **ここでカーネルの番地を渡してみる。**保護が効いていれば
	 * ER_ADR で断られ、効いていなければカーネルが読んでしまう。 */
	{
		CONST B *kaddr = (CONST B *)(CONST VP)0xFFFFFF0000000000ULL;
		W asz = 0;

		if (wri_dev(dd, 0, kaddr, 8, &asz, 0) == ER_ADR) {
			say(dd, "  [EL0] カーネル番地は断られました\n");
		} else {
			say(dd, "  [EL0] *** カーネル番地が通った ***\n");
			code = -2;
		}
	}

	(void)cls_dev(dd, 0, 0);
	return code;
}

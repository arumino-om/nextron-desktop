/*
 * panic.c — 緊急画面
 *
 * docs/design-issues.md E24 / 詳細 E24 / 規律 R18。
 *
 * **外殻（ウィンドウマネージャ）の協力を一切必要としない。**
 * 画面はカーネルが保有していて、上位半分の写像は貸出と独立に存在し続ける
 * （規律 R18）。だから WM が落ちていようと、まだ立ち上がっていなかろうと、
 * 例外の中からでも描ける。これが E24 を採った理由そのものである。
 *
 * BTRON3 自身の「外殻が落ちたとき」の答えは IMS/BMS = シリアルのモニタ
 * だった（詳細 D8）。これはその画面版で、実機でシリアルが取れないときの
 * 命綱になる。仕様を置き換えるものではない。
 *
 * 【壊れた状態で動かねばならない】ので、次を守る。
 *
 *   - 動的確保をしない。行バッファも静的
 *   - 待たない・ロックを取らない（割込み禁止のまま呼ばれる）
 *   - 画面コンソールのカーソル状態を使わない。絶対位置に描く。
 *     画面コンソールの途中で例外が起きても壊れない
 *   - 画面が無ければ何もしない。シリアルだけで進む
 *
 * 【既存の出力をそのまま流用する】`hal_console_tee()` の複製先を自分に
 * 差し替えるので、`hal_exception_report()` が `hal_console_puts()` で
 * 出している内容がそのまま緊急画面にも出る。報告側は 1 行だけ増える。
 */

#include <btron/hal.h>
#include <btron/screen.h>

#define LINE_MAX 120
#define TOP_ROW 3  /* 表題帯の下から本文を書き始める */
#define LEFT_COL 2

LOCAL B line[LINE_MAX + 1];
LOCAL W len;
LOCAL W row;
LOCAL Bool active = False;
LOCAL UW fg;
LOCAL UW bg;

/* 配色。
 *
 * **赤一色にしない。** 画面全体が赤いと、復旧できる不具合でも
 * 「壊れた」という印象を与えて必要以上に不安にさせる。
 * 落ち着いた黒地にして、見出しだけ琥珀色の細い罫で示す。
 * 注意は引くが、警告色で画面を埋めない。
 *
 * 普通の画面コンソール（濃紺地）とは地の色が違うので、
 * 緊急画面であることは一目で分かる。 */
#define PANIC_BG 0x0A, 0x0A, 0x0E   /* ほぼ黒。わずかに青み */
#define PANIC_FG 0xD8, 0xDC, 0xE4   /* 本文。明るい灰 */
#define PANIC_TITLE 0xF2, 0xC0, 0x60 /* 見出し。琥珀 */
#define PANIC_SUB 0x98, 0xA0, 0xAC   /* 補足。くすんだ灰 */
#define PANIC_RULE 0xC8, 0x90, 0x30  /* 見出しの下の細い罫 */

/* いまの行を描く。advance なら次の行へ送る。
 *
 * 途中までの行も毎回描き直す。例外の報告は「ラベルを出して値を出して
 * 改行」という順で来るので、改行まで待つと何も見えないまま止まる
 * 可能性がある。**止まる直前の 1 行が一番知りたい。** */
LOCAL void flush(Bool advance)
{
	if (row >= (W)screen_console_rows() + TOP_ROW) {
		return; /* 画面からあふれた。シリアルには出ているので黙って捨てる */
	}
	line[len] = '\0';
	if (len > 0) {
		screen_text_at(LEFT_COL, row, line, fg, bg);
	}
	if (advance) {
		row++;
		len = 0;
	}
}

LOCAL void panic_write(CONST B *s)
{
	if (!active) {
		return;
	}
	for (; *s != '\0'; s++) {
		if (*s == '\n') {
			flush(True);
		} else if (*s != '\r') {
			if (len < LINE_MAX) {
				line[len++] = *s;
			}
		}
	}
	flush(False);
}

EXPORT UW screen_panic_bg(void)
{
	return screen_rgb(PANIC_BG);
}

EXPORT UW screen_panic_fg(void)
{
	return screen_rgb(PANIC_FG);
}

EXPORT Bool screen_panic_begin(CONST B *title)
{
	CONST Screen *s = screen();

	if (s == 0 || active) {
		return False;
	}
	active = True;
	len = 0;
	row = TOP_ROW;
	bg = screen_rgb(PANIC_BG);
	fg = screen_rgb(PANIC_FG);

	/* 切り替えたことをシリアルにだけ残す。
	 *
	 * **画面には出さない。** 利用者に見せるのは「何が起きたか」であって
	 * カーネルの内部事情ではない。一方で「緊急画面が本当に出たか」を
	 * 機械的に確かめる手がかりは要る（検査 panic-screen-arm64）。
	 * 複製先をいったん外してから書けば、シリアルにだけ残る。
	 *
	 * **見出しも一緒に出す。** 自己検査も緊急画面を 1 度開くので、
	 * 見出しが無いと「検査が開いた分」と「本物の例外が開いた分」を
	 * 区別できず、本物の経路が壊れても検査が通ってしまう。 */
	hal_console_tee(0);
	hal_console_puts("  緊急画面へ切り替えました: ");
	hal_console_puts(title);
	hal_console_puts("（外殻の協力なし。E24 / R18）\n");

	/* 画面を占有する。貸出先が何を描いていようと上書きする。 */
	screen_clear(bg);
	screen_text_at(LEFT_COL, 0, title, screen_rgb(PANIC_TITLE), bg);
	screen_text_at(LEFT_COL, 1, "OSで致命的なエラーが発生したため，動作を停止しました．",
	               screen_rgb(PANIC_SUB), bg);
	/* 見出しと本文の区切り。細い罫 1 本だけにする。 */
	screen_fill(0, 2 * 16 + 6, (W)s->width, 2, screen_rgb(PANIC_RULE));

	/* 以後 hal_console_puts() の出力がここに来る。 */
	hal_console_tee(panic_write);
	return True;
}

/* 緊急画面を畳む。
 *
 * **本物のパニックでは戻らないので呼ばれない。** これがあるのは
 * 自己検査のためで、わざと例外を起こさずに描画だけ確かめられるようにしている
 * （T4-02 の受入条件「画素を読み返して検査する」）。 */
EXPORT void screen_panic_end(void)
{
	active = False;
	hal_console_tee(screen_console_puts);
}

EXPORT Bool screen_panic_active(void)
{
	return active;
}

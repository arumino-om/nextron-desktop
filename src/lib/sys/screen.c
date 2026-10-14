/*
 * screen.c — ユーザ特権側の画面（貸し出しを受ける側）
 *
 * **カーネル側の `src/kernel/screen/fb.c` と同じ名前の関数を持つが、
 * 中身は別物である。** カーネルに結合されるのは fb.c、ユーザプログラムに
 * 結合されるのはこちら。両方が同じ `screen()` を提供するので、
 * ディスプレイプリミティブ (src/lib/dp) はどちらの側でも同じまま動く。
 *
 * これが詳細 E1 の「DP はライブラリであってサーバではない」という
 * 設計判断の実利である。外殻をユーザ特権へ移しても、描画の呼び出しは
 * 関数呼び出しのままで、1 画素ごとにシステムコールを往復しない。
 */

#include <btron/screen.h>

#include <btron/syscall.h>

LOCAL Screen scr;
LOCAL Bool ready;
LOCAL Bool tried;

IMPORT D btron_sys_lend_screen(Screen *out);

EXPORT CONST Screen *screen(void)
{
	if (!tried) {
		tried = True;
		/* **1 度だけ借りる。** 写像はプロセスに残るので、
		 * 以後はシステムコールを呼ばずに済む。 */
		if (btron_sys_lend_screen(&scr) >= 0) {
			ready = True;
		}
	}
	return ready ? &scr : 0;
}

EXPORT ERR screen_init(void)
{
	return (screen() != 0) ? 0 : ER_NOEXS;
}

/*
 * winboot.c — ウィンドウマネージャを起こす
 *
 * ウィンドウマネージャはユーザ特権のプロセス（詳細 E22）なので、
 * 誰かが起こさなければならない。実行プログラムレコードはビルド時に
 * カーネルへ埋め込んである（tools/mkobj.py）。
 *
 * **ファイルを通さない。** 仕様の `cre_prc` は実身から読むが、外殻の
 * サーバはファイルシステムが立つより先に起きていなければならない
 * （画面が無ければ何も見えない）。埋め込みから直接起こす。
 *
 * ファイルシステムが整えば、同じ像を実身に書いて `cre_prc` で起こす
 * 形に移せる。実際、検査ではその経路も通してある。
 */

#include <btron/window.h>

#include <btron/manager.h>
#include <btron/proc.h>
#include <btron/winmsg.h>

IMPORT CONST UB btron_user_winman[];
IMPORT CONST UW btron_user_winman_size;

LOCAL CONST TC wm_name[] = {(TC)'W', (TC)'I', (TC)'N', (TC)'M', (TC)'A',
                            (TC)'N', TNULL};

EXPORT WERR win_launch(void)
{
	WERR pid;
	W i;

	if (mgr_lookup(MGR_WINDOW) >= 1) {
		return 0; /* もう立っている */
	}
	/* **要求を出す側より高い優先度にする。** 同じだと、要求を出した
	 * プロセスが走り続けてサーバが動かない。 */
	pid = prc_create_obj(btron_user_winman, (W)btron_user_winman_size, 1,
	                     wm_name, 0);
	if (pid < 1) {
		return pid;
	}
	/* 名前が出るまで待つ。**出たということは画面も取れている。** */
	for (i = 0; i < 400; i++) {
		if (mgr_lookup(MGR_WINDOW) >= 1) {
			return pid;
		}
		(void)dly_tsk(5);
	}
	return EX_NOSPT;
}

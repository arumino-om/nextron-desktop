/*
 * winmsg.h — ウィンドウマネージャの要求形式
 *
 * ------------------------------------------------------------------------
 * これは BTRON3 仕様の一部ではない
 * ------------------------------------------------------------------------
 *
 * 仕様 §3.1 はウィンドウマネージャの**関数**を定めているだけで、
 * アプリとマネージャがどうやって話すかは書いていない。詳細 E22
 * 「1 プロセス 1 サーバ」に従ってマネージャを別プロセスにする以上、
 * 関数呼び出しは要求と返答に直さなければならない。その形式がこれ。
 *
 * アプリケーションが書くのは `wopn_wnd(...)` のような普通の関数呼び出しで、
 * `src/lib/appl/window/wincli.c` のスタブがここへ直す（規律 R11）。
 *
 * ------------------------------------------------------------------------
 * ポインタは運べない
 * ------------------------------------------------------------------------
 *
 * **サーバは別のアドレス空間にいる。** `RECT *` を渡しても、その番地は
 * サーバにとって意味を持たない。だから中身を複写して運ぶ。
 *
 *   a[0..3]  小さい引数（ウィンドウ ID、モードなど）
 *   data     入力の中身を詰めたもの。返答では出力の中身が入る
 *
 * 何をどう詰めるかは関数ごとに決まっていて、利用側とサーバで対になって
 * いる。**片方だけ変えると黙って壊れる**ので、両方を並べて直すこと。
 *
 * ------------------------------------------------------------------------
 * 描画環境は利用側が持つ
 * ------------------------------------------------------------------------
 *
 * `wget_gid` が返すのは**呼んだプロセスの中で有効な描画環境 ID** である。
 * ディスプレイプリミティブはプロセスごとのライブラリ（詳細 E1）なので、
 * サーバの GID を渡しても使えない。
 *
 * そこでサーバは「作業領域の絶対座標」と「手前にあるウィンドウの矩形」
 * （`WinEnv`）だけを返し、利用側が自分の描画環境を組み立てる。前後関係が
 * 変わるとサーバの世代番号 `gen` が進むので、利用側はそれを見て組み直す。
 */

#ifndef __BTRON_WINMSG_H__
#define __BTRON_WINMSG_H__

#include <btron/dp.h>
#include <btron/typedef.h>

#define MGR_WINDOW "WINMAN" /* グローバル名 */

/* 要求の種類。**一度決めた番号は変えない。** */
#define WF_cnv_rec 1
#define WF_opn_wnd 2
#define WF_opn_pwd 3
#define WF_cls_wnd 4
#define WF_get_sts 5
#define WF_get_tit 6
#define WF_set_tit 7
#define WF_set_bgp 8
#define WF_get_bgp 9
#define WF_get_dat 10
#define WF_set_dat 11
#define WF_get_wrk 12
#define WF_set_wrk 13
#define WF_get_org 14
#define WF_set_org 15
#define WF_get_bar 16
#define WF_mov_wnd 17
#define WF_rsz_wnd 18
#define WF_chg_wnd 19
#define WF_scr_wnd 20
#define WF_era_wnd 21
#define WF_req_dsp 22
#define WF_chk_dsp 23
#define WF_sta_dsp 24
#define WF_end_dsp 25
#define WF_swi_wnd 26
#define WF_get_act 27
#define WF_fnd_wnd 28
#define WF_lst_wnd 29
#define WF_get_evt 30
#define WF_snd_evt 31
#define WF_sta_drg 32
#define WF_end_drg 33
#define WF_get_drg 34
#define WF_mov_drg 35
#define WF_rsz_drg 36
#define WF_get_env 37

/* 利用側が描画環境を組み立てるのに要る情報。 */
#define WIN_MAX_ENVFORE 8

typedef struct WinEnv {
	W gen;     /* 前後関係の世代。変わったら組み直す */
	RECT work; /* 作業領域（絶対座標） */
	W nfore;   /* 手前にあるウィンドウの数 */
	RECT fore[WIN_MAX_ENVFORE]; /* その矩形（絶対座標） */
} WinEnv;

/* 要求と返答。ランデブ 1 往復で運ぶ。
 * 大きさは中心核の `RDV_MAX_MSG` に収まっていなければならない。 */
#define WIN_MSG_DATA 200

typedef struct WinMsg {
	W func;   /* WF_* */
	W result; /* 返答。< 0 はエラー */
	W a[4];   /* 小さい引数 */
	W inlen;  /* data に詰めた入力のバイト数 */
	W outlen; /* 返答で data に入れた出力のバイト数 */
	UB data[WIN_MSG_DATA];
} WinMsg;

#endif /* __BTRON_WINMSG_H__ */

/*
 * winsrv.h — ウィンドウマネージャのサーバ内部（公開しない）
 *
 * 公開 API は include/btron/window.h。そちらを実装するのは
 * `src/lib/appl/window/wincli.c`（利用側）で、要求はランデブで
 * ここへ届く。
 *
 * **名前を `wsv_` に変えてあるのは、利用側と同居させないため。**
 * 同じ名前だと、サーバ本体を結合したプログラムで利用側のスタブが
 * 隠れてしまい、どちらを呼んでいるのか分からなくなる。
 */

#ifndef __BTRON_WINSRV_H__
#define __BTRON_WINSRV_H__

#include <btron/window.h>
#include <btron/winmsg.h>

/* 要求元のプロセス ID。サーバが配る直前に設定する。
 * **`prc_self()` ではサーバ自身が返ってしまう。** */
IMPORT W win_req_pid;

IMPORT ERR wsv_cnv_rec(UW attr, RECT *r, UW opt);
IMPORT WID wsv_opn_wnd(UW attr, UW par, RECT *r, CONST RECT *org, W pict,
                       CONST TC *tit, CONST PAT *bgpat, CONST WDDISP *atr);
IMPORT WID wsv_opn_pwd(CONST RECT *r);
IMPORT ERR wsv_cls_wnd(WID wid, W opt);
IMPORT GID wsv_get_gid(WID wid);
IMPORT W wsv_get_sts(WID wid, WDSTAT *stat, WDDISP *atr);
IMPORT W wsv_get_tit(WID wid, W *pict, TC *title);
IMPORT ERR wsv_set_tit(WID wid, W pict, CONST TC *title, W mode);
IMPORT ERR wsv_set_bgp(WID wid, CONST PAT *pat);
IMPORT W wsv_get_bgp(WID wid, PAT *pat, W size);
IMPORT ERR wsv_get_dat(WID wid, W *dat);
IMPORT ERR wsv_set_dat(WID wid, W dat);
IMPORT ERR wsv_get_wrk(WID wid, RECT *r);
IMPORT ERR wsv_set_wrk(WID wid, CONST RECT *r);
IMPORT ERR wsv_get_org(WID wid, W *parent, RECT *org);
IMPORT ERR wsv_set_org(WID wid, W parent, CONST RECT *org);
IMPORT ERR wsv_get_bar(WID wid, RECT *rbar, RECT *bbar, RECT *lbar);
IMPORT W wsv_mov_wnd(WID wid, RECT *newr);
IMPORT W wsv_rsz_wnd(WID wid, RECT *newr);
IMPORT W wsv_chg_wnd(WID wid, RECT *r, W mode);
IMPORT W wsv_scr_wnd(WID wid, CONST RECT *r, W dh, W dv, W mode);
IMPORT ERR wsv_era_wnd(WID wid, CONST RECT *r);
IMPORT ERR wsv_req_dsp(WID wid);
IMPORT W wsv_chk_dsp(WID wid);
IMPORT W wsv_sta_dsp(WID wid, RECT *r, RLIST *rlst);
IMPORT W wsv_end_dsp(WID wid);
IMPORT ERR wsv_swi_wnd(WID wid, CONST WEVENT *evt);
IMPORT WID wsv_get_act(W *pid);
IMPORT W wsv_fnd_wnd(CONST PNT *gpos, PNT *lpos, W *wid);
IMPORT W wsv_lst_wnd(WID wid, W size, W *wids);
IMPORT W wsv_get_evt(WEVENT *evt, W mode);
IMPORT W wsv_snd_evt(CONST WEVENT *evt);
IMPORT GID wsv_sta_drg(WID wid, W lock);
IMPORT ERR wsv_end_drg(void);
IMPORT W wsv_get_drg(PNT *pos, WEVENT *evt);
IMPORT W wsv_mov_drg(WEVENT *evt, RECT *newr);
IMPORT W wsv_rsz_drg(WEVENT *evt, CONST RECT *limit, RECT *newr);

/* 利用側が自分の描画環境を組み立てるのに要る情報。
 * **BTRON3 の API ではない。** 詳細は winmsg.h を見ること。 */
IMPORT W wsv_get_env(WID wid, WinEnv *env);

/* サーバを立てる。`win_init` から呼ぶ。 */
/* 口（ポートとグローバル名）を開く。 */
IMPORT ERR winsrv_open(void);

/* 要求を配り続ける。戻るのはポートが消えたときだけ。
 * **ユーザランドのサーバではこれが MAIN になる。** */
IMPORT ERR winsrv_loop(void);

IMPORT void winsrv_stop(void);

#endif /* __BTRON_WINSRV_H__ */

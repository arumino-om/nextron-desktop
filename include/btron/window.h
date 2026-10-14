/*
 * window.h — ウィンドウマネージャ (BTRON3 第3編 §3.1)
 *
 * 画面上の独立した矩形領域を管理し、入力受付状態を 1 つのウィンドウに
 * 与え、イベントを配送する。返すのは `EX_*`（外殻の族。規律 R20）。
 *
 * ------------------------------------------------------------------------
 * ウィンドウの 4 種類と前後関係 (§3.1.1.2)
 * ------------------------------------------------------------------------
 *
 *   通常ウィンドウ   一般のアプリが使う。主ウィンドウと従属ウィンドウ
 *   前面ウィンドウ   フロントエンド機能用。**常に最前面**
 *   パネルウィンドウ ダイアローグ用。入力受付状態を横取りして占有する
 *   内部ウィンドウ   独立した描画環境をウィンドウとして扱う擬似ウィンドウ
 *
 * 前後関係は最前面から
 *
 *   (1) 前面ウィンドウ
 *   (2) パネルウィンドウ
 *   (3) 入力受付状態の従属ウィンドウ
 *   (4) 入力受付状態の主ウィンドウ
 *   (5) 入力不可状態の主ウィンドウ
 *
 * **主ウィンドウが入力不可になると、その従属ウィンドウは表示を消去されて
 * 見えなくなる**（不可視状態）。従属ウィンドウは主ウィンドウが入力受付
 * 状態のときだけ「見える」存在である（仕様）。
 *
 * ------------------------------------------------------------------------
 * 入力受付状態 (§3.1.1.2)
 * ------------------------------------------------------------------------
 *
 * ある時点で**1 つの主ウィンドウのみ**が入力受付状態になれる。
 * 新しく主ウィンドウを開くとそこへ移り、以前のウィンドウの管理プロセスへ
 * `EV_INACT` が送られる。イベント管理 (§1.7) 自身はこの規則を保証しない
 * ので、ここで保証する。
 *
 * ------------------------------------------------------------------------
 * いま実装していないもの
 * ------------------------------------------------------------------------
 *
 *   ドラッグ系 (wmov_drg / wrsz_drg / wsta_drg / wend_drg / wget_drg)
 *     実際のポインティングデバイスのドライバ待ち
 *   要求応答の対話 (wugt_evt / wpas_evt / wrsp_evt / wwai_rsp)
 *   メニュー (wget_dmn / wexe_dmn)・FEP (wdef_fep)・ダブルクリック判定
 *   内部ウィンドウ (wopn_iwd)   実身/仮身マネージャ待ち
 *   表示属性の既定値変更 (wchg_dsp)・全面モード (wchg_ful)
 *   wget_inf / wset_inf
 */

#ifndef __BTRON_WINDOW_H__
#define __BTRON_WINDOW_H__

#include <btron/dp.h>
#include <btron/errcode.h>
#include <btron/event.h>
#include <btron/typedef.h>

/* ------------------------------------------------------------------ */
/* ウィンドウ属性 (§3.1.3)                                             */
/* ------------------------------------------------------------------ */

#define WA_FRONT 0x0001  /* 前面ウィンドウ */
#define WA_SUBW 0x0002   /* 従属ウィンドウ (WA_FRONT = 0 のときのみ有効) */
#define WA_SIZE 0x0004   /* ドラッグ変形可 */
#define WA_HHDL 0x0008   /* 左右変形可 */
#define WA_VHDL 0x0010   /* 上下変形可 */
#define WA_RBAR 0x0020   /* 右スクロールバー有 */
#define WA_BBAR 0x0040   /* 下スクロールバー有 */
#define WA_LBAR 0x0080   /* 左スクロールバー有 */
#define WA_TITL 0x0100   /* **タイトルバー無**（0 で有。反転していることに注意） */
#define WA_BGDSP 0x0200  /* 入力不可状態の時も表示を更新 */
#define WA_FULL 0x0400   /* 全面モード */
#define WA_SFULL 0x0800  /* フルスクリーン */
#define WA_STD 0x0000    /* 正規座標でウィンドウ枠を指定 */
#define WA_WORK 0x1000   /* 作業領域座標で指定 */
#define WA_FRAME 0x2000  /* 外枠座標で指定 */
#define WA_FMOVE 0x4000  /* オープン時、画面内に強制移動 */

#define WA_NORMAL 0x007c /* 通常（変形可、右/下スクロールバー） */

/* ------------------------------------------------------------------ */
/* ウィンドウイベント (§3.1.3)                                         */
/*                                                                     */
/* 核イベント (EVENT) と同じ先頭 2 語を持つ 4 つの形式の共用体。        */
/* **先頭が必ず `type`** なので、どの形式でもタイプだけは先に見られる。 */
/* ------------------------------------------------------------------ */

typedef struct SEVENT { /* 切換イベント形式 */
	W type;
	UW time;
	PNT pos;
	H cmd;  /* 位置コード / コマンド (W_*) */
	H wid;
	UW stat;
} SEVENT;

typedef struct REVENT { /* 要求イベント形式 */
	W type;
	RECT r; /* 対象矩形領域（相対座標） */
	H cmd;
	H wid;
	W src; /* ソース PID ほか */
} REVENT;

typedef struct GEVENT { /* 一般イベント形式 */
	W type;
	H data[4];
	H cmd;
	H wid;
	W src;
} GEVENT;

typedef union _WEVENT {
	EVENT e;  /* 生イベント（核イベント） */
	SEVENT s; /* 切換イベント形式 */
	REVENT r; /* 要求イベント形式 */
	GEVENT g; /* 一般イベント形式 */
} WEVENT;

/* ウィンドウイベントタイプ。**核イベントのタイプ空間の続き**である。
 * EV_APPL1 (8) 以降をウィンドウマネージャが使うので、アプリケーション
 * イベントと番号が重なる — 外殻が意味を定義すると仕様が書いている通り。 */
#define EV_SWITCH 8   /* 切換イベント */
#define EV_REQUEST 9  /* 要求 / 応答イベント */
#define EV_MENU 10    /* メニューイベント */
#define EV_RSWITCH 16 /* 切換イベント（再表示要） */
#define EV_INACT 17   /* 入力不可状態に切換わった */
#define EV_MSG 18     /* 他のメッセージが得られた */
#define EV_NOMSG 19   /* メッセージは得られなかった */

/* 位置コード / コマンド */
#define W_WORK 0     /* 作業領域内 */
#define W_FRAM 1     /* ウィンドウ枠 */
#define W_PICT 2     /* ピクトグラム */
#define W_TITL 3     /* タイトル部 */
#define W_LTHD 4     /* ハンドル（左上） */
#define W_RTHD 5     /* ハンドル（右上） */
#define W_LBHD 6     /* ハンドル（左下） */
#define W_RBHD 7     /* ハンドル（右下） */
#define W_RBAR 8     /* 右スクロールバー */
#define W_BBAR 9     /* 下スクロールバー */
#define W_LBAR 10    /* 左スクロールバー */
#define W_SWITCH 128 /* 入力受付状態の移行 */
#define W_CLOSED 129 /* ウィンドウのクローズ */

/* 要求イベントのコマンド */
#define W_REDISP 0 /* ウィンドウの再表示要求 */
#define W_PASTE 1  /* データの貼り込み要求 */
#define W_DELETE 2 /* ウィンドウのクローズ要求 */
#define W_FINISH 3 /* 処理の終了要求 */
#define W_OPENED 5 /* 子ウィンドウのオープン通知 */
#define W_ACK 0x40 /* 要求に対する ACK 応答 */
#define W_NAK 0x80 /* 要求に対する NACK 応答 */

/* ウィンドウイベント取得モード。
 * **WAIT / NOWAIT は §1.2 のメッセージオプションと同じ値**（0 / 1）。 */
#define W_WAIT 0x0000
#define W_NOWAIT 0x0001
#define W_NOMSG 0x0010  /* メッセージは取り出さない */
#define W_RAWEVT 0x0020 /* メニュー用（絶対座標） */
#define W_DRGEVT 0x0030 /* パーツ用（相対座標） */

/* スクロール / 全面モード */
#define W_NORM 0  /* 通常モード */
#define W_FULL 1  /* 全面モード */
#define W_MOVE 0  /* 座標系を固定する（イメージを移動） */
#define W_MOVEC 1
#define W_HOLD 2  /* 座標系を移動する（イメージを固定） */
#define W_SCRL 1
#define W_VALID 1 /* スクロール表示は正当 */
#define W_NOOVL 2 /* オーバーラップ時はスクロールしない */
#define W_RDSET 4 /* 再表示領域を設定する */

/* PD の押し方 */
#define W_PRESS 0
#define W_QPRESS 1
#define W_CLICK 2
#define W_DCLICK 3

/* ------------------------------------------------------------------ */
/* ウィンドウの状態 / 表示属性 (§3.1.3)                                */
/* ------------------------------------------------------------------ */

typedef struct wdstat {
	UW attr;  /* ウィンドウの属性 */
	W parent; /* 親ウィンドウ ID / 主ウィンドウ ID */
	W pid;    /* 管理プロセス ID (0: 無し) */
	RECT r;   /* ウィンドウの表示位置（絶対座標） */
	RECT wr;  /* 作業領域の位置（相対座標） */
	RECT org; /* 生成元の矩形領域 ((0,0,0,0): 無し) */
} WDSTAT;

typedef struct {
	UW frame;    /* 境界線幅 / パターンデータ番号 */
	UW tlbg;     /* タイトル背景パターンデータ番号 */
	UW barpat;   /* スクロールバーのパターン部 */
	UW barbg;    /* スクロールバーの白部分 */
	COLOR tlcol; /* タイトル文字色（負のときは既定） */
} WDDISP;

typedef W WID; /* ウィンドウ ID */

/* ------------------------------------------------------------------ */
/* 実装の制限値 (D9。暫定)                                             */
/* ------------------------------------------------------------------ */

#define W_MAX_WND 16   /* 同時に開けるウィンドウ数 */
#define W_MAX_TITLE 32 /* タイトル文字列の長さ */

/* ------------------------------------------------------------------ */
/* ウィンドウマネージャの関数 (§3.1.4 の実装済み分)                    */
/* ------------------------------------------------------------------ */

/* ウィンドウの生成。戻り値 >0 がウィンドウ ID。
 * `r` は入力が指定座標系での枠、**出力が作業領域の相対座標**（左上は
 * 常に (0,0)）になる。 */
IMPORT WID wopn_wnd(UW attr, UW par, RECT *r, CONST RECT *org, W pict,
                    CONST TC *tit, CONST PAT *bgpat, CONST WDDISP *atr);

/* パネルウィンドウの生成。入力受付状態を横取りする。 */
IMPORT WID wopn_pwd(CONST RECT *r);

/* ウィンドウの削除。opt は CLR（表示をクリア）か NOCLR。 */
IMPORT ERR wcls_wnd(WID wid, W opt);

/* 作業領域の描画環境。**アプリはここへ描く。** */
IMPORT GID wget_gid(WID wid);

/* 状態と表示属性の取得。 */
IMPORT W wget_sts(WID wid, WDSTAT *stat, WDDISP *atr);

/* タイトルとピクトグラム。 */
IMPORT W wget_tit(WID wid, W *pict, TC *title);
IMPORT ERR wset_tit(WID wid, W pict, CONST TC *title, W mode);

/* 作業領域の背景パターン。 */
IMPORT ERR wset_bgp(WID wid, CONST PAT *pat);
IMPORT W wget_bgp(WID wid, PAT *pat, W size);

/* 任意データ（アプリが自由に使う 1 語）。 */
IMPORT ERR wget_dat(WID wid, W *dat);
IMPORT ERR wset_dat(WID wid, W dat);

/* 作業用矩形（アプリが自由に使う）。 */
IMPORT ERR wget_wrk(WID wid, RECT *r);
IMPORT ERR wset_wrk(WID wid, CONST RECT *r);

/* 生成元の矩形領域。 */
IMPORT ERR wget_org(WID wid, W *parent, RECT *org);
IMPORT ERR wset_org(WID wid, W parent, CONST RECT *org);

/* 位置と大きさの変更。
 *
 * **`newr` は入出力。** 仕様「最終的な新しいウィンドウの外枠が newr で
 * 指定した領域に絶対座標値で格納される」。
 * 戻り値は 0（再表示不要）か 1（再表示必要）。 */
IMPORT W wmov_wnd(WID wid, RECT *newr);
IMPORT W wrsz_wnd(WID wid, RECT *newr);
IMPORT W wchg_wnd(WID wid, RECT *r, W mode);

/* 作業領域のスクロール。 */
IMPORT W wscr_wnd(WID wid, CONST RECT *r, W dh, W dv, W mode);

/* 作業領域を背景パターンで消去する。`r = NULL` で全域。 */
IMPORT ERR wera_wnd(WID wid, CONST RECT *r);

/* 再表示処理。`wsta_dsp` 〜 `wend_dsp` の間に再表示を行う。
 * `wsta_dsp` の戻り値が 0 なら再表示すべき領域が無い。 */
IMPORT W wsta_dsp(WID wid, RECT *r, RLIST *rlst);
IMPORT W wend_dsp(WID wid);
IMPORT W wchk_dsp(WID wid);
IMPORT ERR wreq_dsp(WID wid);

/* 入力受付状態の切り換え。 */
IMPORT ERR wswi_wnd(WID wid, CONST WEVENT *evt);

/* 入力受付状態のウィンドウ。`pid` に管理プロセスが入る。 */
IMPORT WID wget_act(W *pid);

/* 座標からウィンドウを探す。戻り値は位置コード (W_*)。 */
IMPORT W wfnd_wnd(CONST PNT *gpos, PNT *lpos, W *wid);

/* 自プロセスのウィンドウの一覧。戻り値は個数。 */
IMPORT W wlst_wnd(WID wid, W size, W *wids);

/* スクロールバーの矩形（相対座標）。無いバーには空の矩形が入る。 */
IMPORT ERR wget_bar(WID wid, RECT *rbar, RECT *bbar, RECT *lbar);

/* ウィンドウイベントの取得。**アプリの主ループがこれを回す。** */
IMPORT W wget_evt(WEVENT *evt, W mode);

/* ウィンドウイベントの送信。 */
IMPORT W wsnd_evt(CONST WEVENT *evt);

/* ------------------------------------------------------------------ */
/* ドラッグ (§3.1.2.5 / §3.1.4)                                        */
/*                                                                     */
/* ドラッグ「影」は作業領域をはみ出して描くので、ウィンドウの描画環境   */
/* では描けない。専用の描画環境を借りる。仕様 §3.1.2.5 によれば、       */
/* 元のウィンドウの描画環境と違うのは                                   */
/*                                                                     */
/*   フレーム長方形   画面全体                                         */
/*   表示長方形       元のウィンドウの作業領域（相対座標）              */
/*   前置長方形リスト 元のウィンドウと同じ                              */
/*                                                                     */
/* の 3 点だけで、座標系は元のウィンドウと同じ。だから影の座標を         */
/* 計算し直さなくてよい。                                              */
/* ------------------------------------------------------------------ */

/* ドラッグ描画の開始。戻り値は影を描くための描画環境 ID。
 * `lock` が 0 でなければ他のウィンドウの同時描画を禁止する
 * （元のウィンドウをはみ出して描くなら必須）。
 * 既に他がドラッグ中なら `EX_DRAG`。 */
IMPORT GID wsta_drg(WID wid, W lock);

/* ドラッグ描画の終了。描画環境を解放する。 */
IMPORT ERR wend_drg(void);

/* ドラッグ中の PD 位置（ドラッグ描画環境の相対座標）とイベント。
 * `EV_REQUEST` と `EV_SWITCH` はキューに残す。 */
IMPORT W wget_drg(PNT *pos, WEVENT *evt);

/* ドラッグによる移動。`evt` は契機となったイベントで、位置コードは
 * `W_FRAM` か `W_LTHD`〜`W_RBHD` でなければならない。
 * 戻り値は 0（再表示不要）か 1（再表示必要）。 */
IMPORT W wmov_drg(WEVENT *evt, RECT *newr);

/* ドラッグによる変形。位置コードは `W_LTHD`〜`W_RBHD`。
 * `limit->lefttop` が最小、`limit->rightbot` が最大の大きさ
 * （`h` が横、`v` が縦）。`limit` が NULL なら既定値。 */
IMPORT W wrsz_drg(WEVENT *evt, CONST RECT *limit, RECT *newr);

/* 枠座標 ←→ 作業領域座標の変換。 */
IMPORT ERR wcnv_rec(UW attr, RECT *r, UW opt);

/* ------------------------------------------------------------------ */
/* 以下は BTRON3 の API ではない。本OS 内部の口                         */
/* ------------------------------------------------------------------ */

/* ウィンドウマネージャを立てる。画面の描画環境を押さえる。 */
IMPORT ERR win_init(void);
IMPORT void win_finish(void);

/* プロセス終了時にそのプロセスのウィンドウを閉じる。 */
IMPORT void win_release_process(W pid);

IMPORT W win_count(void); /* 検証用 */

/* 画面を手で見るための常駐デモ。**仕様ではない。**
 * ESP に `\BTRON\HOLD` があるときだけ、起動時検査の後に呼ばれる。
 * 戻らない（電源を落とさずに画面を残す）。 */
IMPORT void win_demo_hold(void) __attribute__((noreturn));

#endif /* __BTRON_WINDOW_H__ */

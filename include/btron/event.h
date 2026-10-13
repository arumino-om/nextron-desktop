/*
 * event.h — 周辺核 §1.7「イベント管理」
 *
 * キーボード (KB) とポインティングデバイス (PD) の操作を「イベント」として
 * 統一的に扱う。返すのは `ER_*`（周辺核の族。規律 R20）。
 *
 * 【§1.7.1 の要点】
 *   - 操作は**システムに唯 1 つのイベントキュー**に発生順に格納される
 *   - アプリは順次取り出して対応する動作をする（イベント駆動形式）
 *   - **ある時点で入力受付状態のプロセスは必ず 1 つ**で、複数のプロセス間で
 *     受け渡していく。**イベント管理自体はこの規則を保証しない** —
 *     「外殻レベルで提供することになる」（仕様）。ウィンドウマネージャの
 *     仕事である
 *
 * 【起動直後は動いていない】仕様「システムのスタートアップ時点では、
 * イベントキューは空、システムイベントマスクは 0 となっており、イベント
 * 管理機能は事実上動作していない状態であるため、**必ずシステムイベント
 * マスクを適当な値に設定する必要がある**」。`chg_emk()` を呼ばないと
 * 何も溜まらない。
 *
 * 【入力デバイスのドライバはまだ無い】QEMU の virt マシンでは
 * ExitBootServices の後にキーボードもマウスも自前で叩く必要があり、
 * §1.8 のデバイス管理とドライバ (T?-??) 待ちである。それまでイベントは
 * `put_evt()` で注入する。仕様自身が「ソフトキーボードのようなフロント
 * エンドプロセスで、ボタンイベントが適当なキーイベントに変換されて
 * アプリケーションに渡される」という経路を認めているので、
 * イベント管理の側は完成していてよい。
 */

#ifndef __BTRON_EVENT_H__
#define __BTRON_EVENT_H__

#include <btron/errcode.h>
#include <btron/typedef.h>

/* ------------------------------------------------------------------ */
/* イベントタイプとマスク (§1.7.2)                                     */
/* ------------------------------------------------------------------ */

#define EV_NULL 0    /* ヌルイベント（擬似的。キューには入らない） */
#define EV_BUTDWN 1  /* PD のボタンが押された */
#define EV_BUTUP 2   /* PD のボタンが離された */
#define EV_KEYDWN 3  /* 通常キーが押された */
#define EV_KEYUP 4   /* 通常キーが離された */
#define EV_AUTKEY 5  /* 自動リピート（擬似的。キューには入らない） */
#define EV_DEVICE 6  /* デバイスイベント */
#define EV_RSV 7     /* 予約 */
#define EV_APPL1 8   /* アプリケーションイベント */
#define EV_APPL2 9
#define EV_APPL3 10
#define EV_APPL4 11
#define EV_APPL5 12
#define EV_APPL6 13
#define EV_APPL7 14
#define EV_APPL8 15

#define EV_MIN 0
#define EV_MAX 15

#define EM_BUTDWN 0x0001
#define EM_BUTUP 0x0002
#define EM_KEYDWN 0x0004
#define EM_KEYUP 0x0008
#define EM_AUTKEY 0x0010
#define EM_DEVICE 0x0020
#define EM_RSV 0x0040
#define EM_APPL1 0x0080
#define EM_APPL2 0x0100
#define EM_APPL3 0x0200
#define EM_APPL4 0x0400
#define EM_APPL5 0x0800
#define EM_APPL6 0x1000
#define EM_APPL7 0x2000
#define EM_APPL8 0x4000

#define EM_NULL 0x0000
#define EM_ALL 0x7fff

/* タイプ → マスク。**ヌルイベントにはマスクが定義されない**（仕様）ので、
 * EV_NULL に対して使ってはならない。 */
#define EVMASK(type) (1 << ((type) - 1))

/* ------------------------------------------------------------------ */
/* イベント (§1.7.2 / §1.7.5)                                          */
/* ------------------------------------------------------------------ */

typedef union {
	struct { /* EV_KEYUP / EV_KEYDWN / EV_AUTKEY */
		UH keytop; /* キートップコード（キーの物理的位置） */
		TC code;   /* エンコードされた文字コード */
	} key;
	struct { /* EV_DEVICE */
		H kind;  /* デバイスイベント種別 (DE_*) */
		H devno; /* デバイス番号 */
	} dev;
	W info; /* その他のイベント用データ */
} EVDATA;

typedef struct {
	W type;      /* イベントタイプ (EV_*) */
	UW time;     /* イベント発生時刻（ミリ秒。相対的。絶対時刻ではない） */
	PNT pos;     /* 発生時の PD 位置（スクリーン左上を (0,0) とした絶対座標） */
	EVDATA data; /* タイプに依存した固有データ */
	UW stat;     /* メタキー / PD ボタン状態 (ES_*) */
} EVENT;

/* デバイスイベントの種別 (EVDATA.dev.kind) */
#define DE_unknown 0
#define DE_MOUNT 0x01       /* メディア挿入 */
#define DE_EJECT 0x02       /* メディア排出 */
#define DE_ILLMOUNT 0x03    /* メディア不正挿入 */
#define DE_ILLEJECT 0x04    /* メディア不正排出 */
#define DE_REMOUNT 0x05     /* メディア再挿入 */
#define DE_CARDBATLOW 0x06  /* カードバッテリ残量警告 */
#define DE_CARDBATFAIL 0x07 /* カードバッテリ異常 */
#define DE_REQEJECT 0x08    /* メディア排出要求 */

/* ------------------------------------------------------------------ */
/* メタキー / PD ボタン状態 (§1.7.2)                                   */
/*                                                                     */
/* **メタキーは押されてもイベントを発生しない。** 状態だけが `stat` に  */
/* 載り、文字コードのエンコードに使われる。PD のサブボタン              */
/* （メニューボタン）も同様にイベントを発生しない。                     */
/* ------------------------------------------------------------------ */

#define ES_BUT 0x0001   /* PD のメインボタン */
#define ES_BUT2 0x0002  /* PD のサブボタン（メニューボタン） */
#define ES_ALPH 0x0004  /* 英語ロックキー */
#define ES_KANA 0x0008  /* カタカナロックキー */
#define ES_LSHFT 0x0010 /* 左シフト */
#define ES_RSHFT 0x0020 /* 右シフト */
#define ES_EXT 0x0040   /* 拡張シフト */
#define ES_CMD 0x0080   /* 命令シフト */

#define ES_LLSHFT 0x00000100 /* 左シフト簡易ロック */
#define ES_LRSHFT 0x00000200
#define ES_LEXT 0x00000400
#define ES_LCMD 0x00000800
#define ES_TLSHFT 0x00001000 /* 左シフト一時シフト */
#define ES_TRSHFT 0x00002000
#define ES_TEXT 0x00004000
#define ES_TCMD 0x00008000
#define ES_HAN 0x00010000   /* 半角キー */
#define ES_NODSP 0x00200000 /* ポインタ非表示 */
#define ES_PDSIM 0x00C00000 /* PD シミュレーション */

/* 入力モード */
#define IM_HIRA 0x0000
#define IM_ALPH ES_ALPH
#define IM_KATA ES_KANA
#define IM_CAPS (ES_ALPH | ES_KANA)
#define IM_MASK (ES_ALPH | ES_KANA)

#define KIN_KANA 0x0000  /* かな入力モード */
#define KIN_ROMAN 0x0001 /* ローマ字入力モード */

/* ------------------------------------------------------------------ */
/* put_evt の属性 (§1.7.5)                                             */
/* ------------------------------------------------------------------ */

#define EP_NONE 0x0000 /* time, pos, stat は evt の内容のまま */
#define EP_POS 0x0001  /* pos に現在の PD 位置を設定 */
#define EP_STAT 0x0002 /* stat に現在のメタキー / ボタン状態を設定 */
#define EP_TIME 0x0004 /* time に現在のイベントタイマーの値を設定 */
#define EP_ALL 0x0007

/* ------------------------------------------------------------------ */
/* ポインティングデバイスの属性 (§1.7.5 chg_pda)                       */
/* ------------------------------------------------------------------ */

#define PD_REV 0x1000   /* 左右反転 */
#define PD_ABS 0x0100   /* 絶対座標タイプ */
#define PD_REL 0x0000   /* 相対座標タイプ（差動タイプ） */
#define PD_SCMSK 0x00f0 /* スキャン速度 */
#define PD_SNMSK 0x000f /* 感度 */

/* ------------------------------------------------------------------ */
/* キーボード (§1.7.3 / §1.7.5)                                        */
/* ------------------------------------------------------------------ */

#define KEYMAX 256
typedef UB KeyMap[KEYMAX / 8]; /* キー状態配列。1 キー = 1 ビット */

typedef struct {
	UH kind;  /* キーボードタイプ。0000 000T TTTT TTMP */
	UH maker; /* メーカー ID。get_ver() の maker と同じ */
	UB id[4]; /* メーカー依存キーボード ID */
} KBD_ID;

#define KB_PD 0x0001       /* P: PD がキーボードに内蔵されている */
#define KB_MECHLOCK 0x0002 /* M: ロックキーはメカニカルロック方式 */
#define KB_KIND_SHIFT 2
#define KB_UNDEF 0x00  /* 未定義キーボード */
#define KB_TRONJP 0x01 /* TRON 日本語キーボード */
#define KB_IBM101 0x40 /* IBM 101 系英語キーボード */
#define KB_IBM106 0x41 /* IBM 106 系日本語キーボード */

/* 文字コード変換表。
 *
 * `kct[kctsel[メタキー状態]][キートップコード]` が文字コード。
 * メタキー状態は 6 ビット **CERLKA** の並びで、
 *   A = 日本語/英語、K = ひらがな/カタカナ、L = 左シフト、
 *   R = 右シフト、E = 拡張シフト、C = 命令シフト
 * という順。ビットの順番を取り違えると全く別の文字が出る。 */
#define KCTSEL 64
#define KCTMAX 4000

typedef struct {
	W keymax;          /* 実際の最大キー数 (1〜256) */
	W kctmax;          /* 実際の変換表の数 (1〜64) */
	UH kctsel[KCTSEL]; /* メタキー状態 → 変換表の番号 */
	UH kct[KCTMAX];    /* 変換表本体。keymax × kctmax 個 */
} KeyTab;

/* メタキー状態 (CERLKA) をビット位置で表す。`kctsel` の添字になる。 */
#define KM_A 0x01 /* 日本語(0) / 英語(1) */
#define KM_K 0x02 /* ひらがな(0) / カタカナ(1) */
#define KM_L 0x04 /* 左シフト */
#define KM_R 0x08 /* 右シフト */
#define KM_E 0x10 /* 拡張シフト */
#define KM_C 0x20 /* 命令シフト */

/* ------------------------------------------------------------------ */
/* ブザー (§1.7.5 ctl_buz)                                             */
/* ------------------------------------------------------------------ */

#define GET_BUZ_BEEP 0x0000
#define GET_BUZ_PDON 0x0001
#define GET_BUZ_PDOFF 0x0002
#define GET_BUZ_KEYON 0x0003
#define GET_BUZ_KEYOFF 0x0004
#define GET_BUZ_VOLUME 0x0010
#define SET_BUZ_BEEP 0x0100
#define SET_BUZ_PDON 0x0101
#define SET_BUZ_PDOFF 0x0102
#define SET_BUZ_KEYON 0x0103
#define SET_BUZ_KEYOFF 0x0104
#define SET_BUZ_VOLUME 0x0110

/* ------------------------------------------------------------------ */
/* 実装の制限値 (D9。暫定)                                             */
/* ------------------------------------------------------------------ */

#define EV_QUEUE_MAX 64 /* イベントキューの深さ */
#define EV_REQ_MAX 8    /* イベントメッセージ要求を出せるプロセス数 */

/* ------------------------------------------------------------------ */
/* システムコール (§1.7.6)                                             */
/* ------------------------------------------------------------------ */

/* イベントの取得。戻り値は得られたイベントのタイプ。
 * opt は CLR（キューから取り除く）か NOCLR。
 * 指定したタイプが発生していなければ `EV_NULL` が得られる（エラーではない）。 */
IMPORT WERR get_evt(W t_mask, EVENT *evt, W opt);

/* イベントの発生。**常にキューの最後に入る**（time の値に関係なく）。
 * `EV_NULL` と `EV_AUTKEY` は指定できない（擬似イベントなので）。 */
IMPORT ERR put_evt(CONST EVENT *evt, W opt);

/* イベントのクリア。`last_mask` の意味は §1.2 の `clr_msg` と同じ。 */
IMPORT ERR clr_evt(W t_mask, W last_mask);

/* PD の位置。相対動作タイプなら設定もできる（絶対動作タイプは不可）。 */
IMPORT WERR get_pdp(PNT *pos);
IMPORT WERR set_pdp(PNT pos);

/* イベントタイマー（ミリ秒単位で加算される 32 ビットのタイマー）。 */
IMPORT ERR get_etm(UW *time);

/* キー状態配列。 */
IMPORT ERR get_kmp(KeyMap keymap);

/* システムイベントマスクの変更。戻り値は変更前の値。
 * **これを呼ぶまでイベント管理は事実上動いていない**（仕様）。 */
IMPORT WERR chg_emk(W mask);

/* 自動リピートのオフセット時間とインターバル時間（ミリ秒）。 */
IMPORT ERR set_krp(W offset, W interval);
IMPORT ERR get_krp(W *offset, W *interval);

/* 自動リピートの対象キー。"1" のビットのキーが対象。 */
IMPORT ERR set_krm(CONST UB *keymap);
IMPORT ERR get_krm(KeyMap keymap);

/* キーボード ID。 */
IMPORT ERR get_kid(KBD_ID *id);

/* 文字コード変換表。戻り値は実際に必要なバイト数。 */
IMPORT WERR get_ktb(KeyTab *keytab);
IMPORT ERR set_ktb(CONST KeyTab *keytab);

/* PD の属性の変更。戻り値は変更前の属性。 */
IMPORT WERR chg_pda(W atr);

/* ブザー。音源が無い構成では鳴らないが、設定は保持される。 */
IMPORT ERR sig_buz(W type);
IMPORT ERR ctl_buz(W kind, UW *val);

/* イベントメッセージ要求。指定タイプのイベントを `MS_SYS5` の
 * メッセージとして自プロセスへ送らせる。`EM_NULL` で解除。
 * **メッセージにしてもイベントはキューから取り除かれない**（仕様）。 */
IMPORT ERR req_evt(W t_mask);

/* 最終イベント発生からの経過時間。 */
IMPORT WERR las_evt(W t_mask);

/* ------------------------------------------------------------------ */
/* 以下は BTRON3 の API ではない。本OS 内部の口                         */
/* ------------------------------------------------------------------ */

IMPORT ERR evt_init(void);

/* --- ドライバからの口 ---
 *
 * キーボードの**配列**（どのキートップがシフトか、どの文字になるか）を
 * 知っているのはドライバだけなので、配列依存はドライバが持ち、
 * エンコード・キュー・自動リピート・ボタン状態は周辺核が持つ。 */

/* キーボード ID と既定の文字コード変換表を登録する。 */
IMPORT void evt_set_kbd(CONST KBD_ID *id, CONST KeyTab *tab);

/* PD の属性 (PD_ABS / PD_REL など) を登録する。chg_pda の内部版で、
 * **絶対動作タイプかどうかは装置の性質**なのでドライバが決める。 */
IMPORT void evt_set_pda(W atr);

/* メタキー状態の更新。mask のビットを bits の値にする。
 * 仕様によりイベントは発生しない。 */
IMPORT void evt_meta(UW bits, UW mask);

/* キーの上下。文字コードへのエンコードは周辺核が変換表で行う。 */
IMPORT void evt_key(UH keytop, Bool down);

/* PD の移動。絶対動作タイプは evt_pd_moved、相対動作タイプは
 * evt_pd_delta を使う。どちらも画面の範囲に収められる。 */
IMPORT void evt_pd_moved(PNT pos);
IMPORT void evt_pd_delta(W dx, W dy);

/* PD のボタン。sub が真ならサブボタン（メニューボタン）。
 * サブボタンは仕様によりイベントを発生せず、状態だけが変わる。 */
IMPORT void evt_pd_button(Bool sub, Bool down);

/* 現在の PD 位置。ポインタ描画のために割込み文脈からも読む。 */
IMPORT PNT evt_pd_pos(void);

/* プロセス終了時にイベントメッセージ要求を外す（仕様）。 */
IMPORT void evt_release_process(W pid);

IMPORT W evt_count(void); /* 検証用。キューに入っている数 */

#endif /* __BTRON_EVENT_H__ */

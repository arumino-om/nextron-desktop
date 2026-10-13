/*
 * device.h — 周辺核 §1.8「デバイス管理」
 *
 * アプリケーションが各種のデバイスを統一的に扱うためのインタフェースと、
 * デバイスドライバの登録／管理。返すのは `ER_*`（規律 R20）。
 *
 * 【§1.8.1 の要点 — 3 つのものがここを通らない】
 *
 *   ビットマップディスプレイ  **デバイス管理とは独立**。ディスプレイ
 *                             プリミティブ (第2章) として提供される。
 *                             「ドライバと一体となっているためドライバ
 *                             インタフェースも適用されない」（仕様）
 *   キーボード / PD           アプリ向けはイベント管理 (§1.7)。
 *                             **ドライバ側はデバイス管理が面倒を見る**
 *   ディスク装置              通常はファイルとして使うが、デバイスとして
 *                             直接扱う口も用意される
 *
 * つまりデバイス管理は「ファイル管理とイベント管理が内部で通る土台」で
 * あって、アプリが直接叩くのは限られた場合だけである。
 *
 * 【論理デバイス名 (§1.8.2)】「種別 + ユニット + サブユニット番号」で
 * 最大 8 文字。
 *
 *   種別        "hd" / "fd" など、デバイスの種類
 *   ユニット    同種が複数あるときの識別。通常 "a" 〜 の 1 文字
 *   サブユニット 1 ユニットを論理分割したときの番号。"0"〜"254"
 *
 * **「種別 + ユニット」をユニット名と呼び、OS が識別する単位はこれ。**
 * OS 側は種別とユニットを区別しない。ユニット名の最後の文字は数字で
 * あってはならない — 最後が数字ならサブユニットと見なされるため。
 * ドライバはユニット単位で登録される。
 */

#ifndef __BTRON_DEVICE_H__
#define __BTRON_DEVICE_H__

#include <btron/errcode.h>
#include <btron/typedef.h>

/* ------------------------------------------------------------------ */
/* オープンモード (§1.8.3)                                             */
/* ------------------------------------------------------------------ */

#define D_READ 0x0001   /* 読み込み専用（R アクセス権が必要） */
#define D_WRITE 0x0002  /* 書き込み専用（W アクセス権が必要） */
#define D_UPDATE 0x0003 /* 更新用（RW アクセス権が必要） */
#define D_EXCL 0x0100   /* 排他モード */
#define D_WEXCL 0x0200  /* 排他書き込みモード */
#define D_NOWAIT 0x8000 /* 待ち無し */

/* ------------------------------------------------------------------ */
/* デバイス管理情報 / デバイス情報 (§1.8.3)                            */
/* ------------------------------------------------------------------ */

typedef struct dev_state {
	UW attr;  /* デバイスの属性 (DA_* / DK_*) */
	UW mode;  /* アクセスモード */
	W blksz;  /* 物理ブロックサイズ (-1: 不明) */
	W wprt;   /* 書き込み可否 (0:許可 1:禁止 -1:不明) */
} DEV_STATE;

#define L_DEVNM 8 /* デバイス名の長さ（文字数） */

typedef struct {
	UW attr;           /* デバイスの属性 */
	W nsub;            /* サブユニット数 */
	TC name[L_DEVNM];  /* ユニット名 */
} DEV_INFO;

/* デバイスの属性。IIII IIII IIII IIII CRxx xxxx KKKK KKKK */
#define DA_DEVINFO 0xFFFF0000U /* デバイス / メディア依存情報 */
#define DA_CHARDEV 0x8000U     /* C: 文字型デバイス */
#define DA_REMOVABLE 0x4000U   /* R: 取り外し可能 */
#define DA_DEVKIND 0xFFU       /* K: デバイス / メディア種別 */
#define DA_DEVTYPE 0xF0U       /* タイプマスク */

#define DK_UNDEF 0x00      /* 未定義 / 不明 */
#define DK_DISK 0x10       /* ディスクタイプ */
#define DK_DISK_UNDEF 0x10 /* その他のディスク */
#define DK_DISK_RAM 0x11   /* RAM ディスク */
#define DK_DISK_ROM 0x12   /* ROM ディスク */
#define DK_DISK_FLA 0x13   /* FLASH ROM / SS ディスク */
#define DK_DISK_FD 0x14    /* フロッピーディスク */
#define DK_DISK_HD 0x15    /* ハードディスク */
#define DK_DISK_CDROM 0x16 /* CD-ROM */

/* ------------------------------------------------------------------ */
/* サスペンド要求モード (§1.8.3)                                       */
/* ------------------------------------------------------------------ */

#define D_SUSPEND 0x0001 /* サスペンドする */
#define D_DISSUS 0x0002  /* サスペンドを禁止する */
#define D_ENASUS 0x0003  /* サスペンドを許可する */
#define D_CHECK 0x0004   /* サスペンド禁止カウントを調べる */
#define D_FORCE 0x8000U  /* 強制サスペンド */

/* ------------------------------------------------------------------ */
/* 実装の制限値 (D9。暫定)                                             */
/* ------------------------------------------------------------------ */

#define DEV_MAX_UNIT 16 /* 登録できるユニット数 */
#define DEV_MAX_OPEN 32 /* 同時にオープンできるデバイス数 */

/* ------------------------------------------------------------------ */
/* ドライバインタフェース                                              */
/*                                                                     */
/* **これは BTRON3 が規定するドライバインタフェースそのものではない。** */
/* 仕様は §1.8.1 で「ここでは、アプリケーションインタフェースのみに      */
/* ついて記述する」としていて、ドライバ側の規定は本文に無い。            */
/* よって本OS が必要な形で定める（規律 R13: ドライバは原則ユーザランド   */
/* だが、シリアルのようにカーネル内に置くものもある）。                  */
/* ------------------------------------------------------------------ */

typedef struct DevDriver {
	/* ユニット名。ASCII で書く（内部で TC へ広げる）。
	 * **最後の文字が数字であってはならない**（仕様）。 */
	CONST B *unit;
	UW attr;  /* DA_* / DK_* */
	W nsub;   /* サブユニット数。0 なら分割なし */
	W blksz;  /* 物理ブロックサイズ。文字型なら -1 */

	/* 開く / 閉じる。0 なら「常に成功」とみなす。 */
	ERR (*open)(W sub, W o_mode, W *error);
	ERR (*close)(W sub, W eject, W *error);

	/* 読む / 書く。戻り値は実際に処理した量（負ならエラー）。
	 * `start` は仕様どおり「≧0 固有データ、<0 属性データ」。 */
	W (*read)(W sub, W start, B *buf, W size, W *error);
	W (*write)(W sub, W start, CONST B *buf, W size, W *error);
} DevDriver;

/* ドライバを登録する。起動時に呼ぶ。
 * `drv` は静的な寿命を持つこと（表はポインタを保持する）。 */
IMPORT ERR dev_register(CONST DevDriver *drv);

/* ------------------------------------------------------------------ */
/* システムコール (§1.8.4)                                             */
/* ------------------------------------------------------------------ */

/* デバイスのオープン。戻り値 >0 がデバイスディスクリプタ。
 * `error` にはドライバが返した詳細エラーが入る（NULL なら格納しない）。 */
IMPORT WERR opn_dev(CONST TC *dev, W o_mode, W *error);

/* クローズ。**オープンしたプロセスが終了すると自動的に閉じられる**（仕様）。 */
IMPORT ERR cls_dev(W dd, W eject, W *error);

/* 読み / 書き。`size = 0` なら実際には行わず、`a_size` に可能な量を返す。 */
IMPORT ERR rea_dev(W dd, W start, B *buf, W size, W *a_size, W *error);
IMPORT ERR wri_dev(W dd, W start, CONST B *buf, W size, W *a_size, W *error);

/* アクセスモードの変更。 */
IMPORT ERR chg_dmd(CONST TC *dev, W mode);

/* デバイス管理情報の取得。 */
IMPORT ERR dev_sts(CONST TC *dev, DEV_STATE *buf);

/* デバイス番号からユニット名を引く。戻り値はサブユニット数。 */
IMPORT WERR get_dev(TC *dev, W devno);

/* 登録済みデバイスの一覧。戻り値は登録数。
 * `dev = NULL` または `ndev = 0` で数だけ取れる。 */
IMPORT WERR lst_dev(DEV_INFO *dev, W ndev);

/* サスペンド要求。 */
IMPORT WERR sus_dev(UW mode);

/* ------------------------------------------------------------------ */
/* 以下は BTRON3 の API ではない。本OS 内部の口                         */
/* ------------------------------------------------------------------ */

IMPORT ERR dev_init(void);

/* プロセス終了時にそのプロセスが開いたデバイスを閉じる（仕様）。 */
IMPORT void dev_release_process(W pid);

IMPORT W dev_open_count(void); /* 検証用 */

/* ドライバの登録口。起動時に呼ぶ。 */
IMPORT ERR serdev_register(void);  /* "com" シリアル（文字型） */
IMPORT ERR ramdisk_register(void); /* "rda" RAM ディスク（ブロック型） */

/* virtio-input。"kbd" と "pd" を登録し、イベント管理に実体を与える。
 * 入力装置が 1 台も無ければ ER_NOEXS を返す（異常ではない）。 */
IMPORT ERR vinput_register(void);

/* 見つかった入力装置の数と種類。検証とログ用。 */
#define VIN_KIND_NONE 0
#define VIN_KIND_KBD 1 /* キーボード */
#define VIN_KIND_ABS 2 /* 絶対座標の PD（タブレット） */
#define VIN_KIND_REL 3 /* 相対座標の PD（マウス） */

IMPORT W vinput_count(void);
IMPORT W vinput_kind(W i);

#define VIN_PROBE_MAX 4 /* 同時に扱う入力装置の上限 */

#endif /* __BTRON_DEVICE_H__ */

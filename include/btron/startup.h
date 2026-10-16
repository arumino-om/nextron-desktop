/*
 * startup.h — システムの起動列 (D8)
 *
 * 【これは BTRON3 仕様の一部ではない】仕様 (D8) は `/SYS/DEVCONF`・
 * 初期プロセス・実行環境ファイルの仕組みを規定しているが、その書式と
 * 意味論は仕様原文を要する部分が残っている。ここにあるのは原文を
 * 要しない範囲 —— **ドライバの登録と、永続する記憶装置の発見・構築・
 * 接続、実行プログラムの配置** —— だけである。
 */

#ifndef __BTRON_STARTUP_H__
#define __BTRON_STARTUP_H__

#include <btron/file.h>
#include <btron/message.h>
#include <btron/typedef.h>

/* 起動列が何をしたか。検査とログが読む。 */
typedef struct {
	Bool done;      /* 起動列を 1 度通したか */
	Bool serial;    /* "com" を登録した */
	Bool ramdisk;   /* "rda" を登録した */
	W input_devs;   /* 見つけた入力装置の数 */
	Bool disk;      /* "vda" を登録した（virtio-blk があった） */
	ERR disk_err;   /* 登録の結果。見つからないときの理由 */
	UD disk_blocks; /* ディスクの 512B セクタ数 */
	W parts;        /* GPT の区画数 */
	W btron_part;   /* BTRON 区画のサブユニット番号。無ければ -1 */
	Bool formatted; /* この起動で構築した（＝前回の内容は無かった） */
	Bool attached;  /* ファイルシステムを接続できた */
	Bool programs;  /* 実行プログラムが実身として在る */
	Bool placed;    /* この起動で置いた（＝初回） */
	LINK root;      /* 接続したファイルシステムのルート */
} SysStartup;

/* 起動列を通す。**2 度呼んでも 1 度しか走らない。**
 * プロセスの文脈から呼ぶこと（ファイル管理のシステムコールを使う）。 */
IMPORT CONST SysStartup *sys_startup(void);

/* 親の中に仮身（リンクレコード）を置く。**これが無いとパス名で
 * たどれない** — 仕様 §1.6.1 のとおりディレクトリは存在せず、
 * 参照関係はファイルの中のリンクレコードが作る。 */
IMPORT ERR sys_put_link(LINK *parent, CONST LINK *child);

/* 直前の結果を見る（起動列を走らせない）。 */
IMPORT CONST SysStartup *sys_state_get(void);

/* ディスク上の実行プログラムからプロセスを起こす。
 * **埋め込みからではなく実身から読む**ので、2 回目以降の起動では
 * 前回置いたものがそのまま使われる。 */
IMPORT WERR sys_run_program(CONST TC *name, W pri, CONST MESSAGE *msg);
IMPORT WERR sys_run_shell(W pri, CONST MESSAGE *msg);

#endif /* __BTRON_STARTUP_H__ */

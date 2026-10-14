/*
 * fs.h — 本OS のファイルシステム形式（内部定義。公開しない）
 *
 * 公開 API は include/btron/file.h（周辺核 §1.6）。
 *
 * ------------------------------------------------------------------------
 * なぜ独自形式か
 * ------------------------------------------------------------------------
 *
 * 仕様 §1.6.1 は
 *
 *   「ファイルシステムを実現する上での各種の物理構造やデータ構造は、
 *     インプリメントに依存するが、交換用媒体としてのフロッピーディスクに
 *     関しては互換性を保つために物理構造の詳細が規定されている」
 *
 * と言っている。つまり**ディスク形式は実装の自由**で、規定があるのは
 * 交換用フロッピーだけ。そちらは詳細 C1 の標準形式で、T3-01 として
 * 別に用意する。ここにあるのは本OS が固定媒体に使う形式である。
 *
 * 【規律 R1】メモリ表現（file.h の構造体）とディスク表現（ここ）は
 * 別物である。構造体をそのまま I/O してはならない。だからこのファイルの
 * 読み書きはすべてバイト単位の詰め込み／取り出しで行う。
 * **こうしておくと、パディングやエンディアンの違いで形式が壊れない。**
 *
 * ------------------------------------------------------------------------
 * 配置
 * ------------------------------------------------------------------------
 *
 *   ブロック 0            スーパーブロック
 *   ブロック 1..          ブロックビットマップ（1 ビット 1 ブロック）
 *   その後                ファイルインデックス（1 エントリ 256 バイト）
 *   その後                データブロック
 *
 * データブロックは先頭 4 バイトが次ブロック番号の連鎖で、残り 508 バイトが
 * レコードストリームの一部になる。ファイル 1 つのレコードストリームは
 *
 *   UH タイプ / UH サブタイプ / UW サイズ / 本体 / 4 バイト境界への詰め物
 *
 * の並びである。リンクレコード（タイプ 0）の本体は**固定リンクの形**で、
 * ファイルシステム名を持たない（仕様 §1.6.2「固定リンクはファイルシステム
 * 名を持たないため、同一のファイルシステム内のファイルの参照のみ可能」）。
 * 取り出すときに、属しているファイルシステムの名前を埋める。
 */

#ifndef __BTRON_KERNEL_FS_H__
#define __BTRON_KERNEL_FS_H__

#include <btron/file.h>

#define FSB_MAGIC0 'B'
#define FSB_MAGIC1 'T'
#define FSB_MAGIC2 'F'
#define FSB_MAGIC3 '1'
#define FSB_VERSION 1

#define FSB_BSIZE 512    /* 論理ブロックのバイト数 */
#define FSB_ENTSZ 256    /* ファイルインデックス 1 エントリ */
#define FSB_ENTPB (FSB_BSIZE / FSB_ENTSZ) /* 1 ブロックのエントリ数 */
#define FSB_DATASZ (FSB_BSIZE - 4)        /* データブロックの正味 */

/* スーパーブロックの項目オフセット */
#define SB_MAGIC 0
#define SB_VERSION 4
#define SB_BSIZE 8
#define SB_NBLK 12
#define SB_NFILE 16
#define SB_BMAP_BLK 20
#define SB_BMAP_CNT 24
#define SB_IDX_BLK 28
#define SB_IDX_CNT 32
#define SB_DATA_BLK 36
#define SB_NFREE 40
#define SB_LANG 44
#define SB_LEVEL 46
#define SB_CTIME 48
#define SB_MTIME 56
#define SB_FSNAME 64   /* TC[20] = 40 バイト */
#define SB_FSLOCAT 104 /* TC[20] = 40 バイト */

/* ファイルインデックスエントリの項目オフセット */
#define FE_FLAGS 0
#define FE_TYPE 4
#define FE_ATYPE 6
#define FE_GRPACC 8
#define FE_PUBACC 10
#define FE_NLINK 12
#define FE_NREC 16
#define FE_SIZE 20
#define FE_NBLK 24
#define FE_FIRST 28
#define FE_CTIME 32
#define FE_MTIME 40
#define FE_ATIME 48
#define FE_LTIME 56
#define FE_NAME 64    /* TC[20] */
#define FE_OWNER 104  /* TC[14] = 28 バイト */
#define FE_GROUP 132  /* TC[14] */
#define FE_PWD 160    /* TC[8]  = 16 バイト */

#define FE_F_USED 0x0001  /* 使用中 */
#define FE_F_LINKF 0x0002 /* リンクファイル */

/* 参照カウントの上限。仕様 ins_rec の ER_LIMIT「参照カウントが
 * システムの制限(255)を越えた」。 */
#define FSB_MAX_NLINK 255

/* レコードストリームの詰め方 */
#define REC_HDR 8 /* UH タイプ + UH サブタイプ + UW サイズ */
#define REC_ALIGN 4
#define REC_PAD(n) (((n) + (REC_ALIGN - 1)) & ~(REC_ALIGN - 1))

/* 固定リンクの本体。UW f_id + UH atr1..5 = 14 バイト。 */
#define FIXLNK_SIZE 14

/* 接続中のファイルシステム 1 つ。 */
typedef struct Fs {
	Bool used;
	W dd;                 /* デバイスディスクリプタ（システム所有） */
	TC dev[L_DEVNM + 1];  /* 論理デバイス名 */
	TC conn[L_CONNM + 1]; /* 接続名 */
	Bool ronly;           /* 書き込み禁止で接続した */
	Bool async;           /* 非同期指定 */

	/* スーパーブロックの写し。 */
	UW nblk;
	UW nfile;
	UW bmap_blk;
	UW bmap_cnt;
	UW idx_blk;
	UW idx_cnt;
	UW data_blk;
	UW nfree;
	W lang;
	W level;
	STIME ctime;
	STIME mtime;
	TC fs_name[L_FSNM + 1];
	TC fs_locat[L_DLNM + 1];
	Bool sb_dirty;
} Fs;

/* --- バイト列の詰め込み／取り出し（R1: 構造体を直接 I/O しない） --- */
IMPORT UW fsb_get32(CONST UB *p, W off);
IMPORT void fsb_put32(UB *p, W off, UW v);
IMPORT UH fsb_get16(CONST UB *p, W off);
IMPORT void fsb_put16(UB *p, W off, UH v);
IMPORT UD fsb_get64(CONST UB *p, W off);
IMPORT void fsb_put64(UB *p, W off, UD v);
IMPORT void fsb_get_tc(CONST UB *p, W off, TC *dst, W n);
IMPORT void fsb_put_tc(UB *p, W off, CONST TC *src, W n);

/* --- ブロック入出力 --- */
IMPORT ERR fsb_read(Fs *fs, UW blk, UB *buf);
IMPORT ERR fsb_write(Fs *fs, UW blk, CONST UB *buf);

/* --- ブロックの確保と解放 --- */
IMPORT W fsb_alloc(Fs *fs);       /* 戻り値はブロック番号。0 なら空き無し */
IMPORT ERR fsb_free(Fs *fs, UW blk);

/* --- ファイルインデックス --- */
IMPORT ERR fsb_ent_read(Fs *fs, UW fid, UB *ent);
IMPORT ERR fsb_ent_write(Fs *fs, UW fid, CONST UB *ent);
IMPORT W fsb_ent_alloc(Fs *fs, UW want); /* want != 0 ならその ID を要求 */

/* --- レコードストリーム --- */
IMPORT W fsb_stream_read(Fs *fs, UW first, UB *buf, W len);
IMPORT ERR fsb_stream_write(Fs *fs, UB *ent, CONST UB *buf, W len);
IMPORT ERR fsb_stream_free(Fs *fs, UB *ent);

/* --- 生成と接続 --- */
IMPORT ERR fsb_format(W dd, CONST TC *fs_name, CONST TC *fs_locat, W level,
                      W lang);
IMPORT ERR fsb_mount(Fs *fs);
IMPORT ERR fsb_sync(Fs *fs);

#endif /* __BTRON_KERNEL_FS_H__ */

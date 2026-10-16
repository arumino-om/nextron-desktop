/*
 * objfmt.h — 実行プログラムレコードの形式
 *
 * ------------------------------------------------------------------------
 * 仕様はここを規定していない
 * ------------------------------------------------------------------------
 *
 * 共有データ仕様 3.2 はレコードタイプ 9 を「実行プログラムレコード」とし、
 * 「実行プログラムの標準オブジェクト形式であり、サブタイプは CPU のタイプ
 * を示す」と書いている。**サブタイプの値は表で与えられているが、
 * オブジェクト形式そのものの中身は手元の仕様書にない**（§5 未読箇所）。
 * したがって形式は本OS が決める。
 *
 * サブタイプ（CPU タイプ）は仕様の表に従う。表は
 *
 *   0x100〜0x13F TRONCHIP 系   0x00 TRONCHIP32
 *   0x140〜0x14F 68000 系      0x40 68000 / 0x41 68010 / 0x42 68020
 *   0x150〜0x15F NS32000 系    0x50 32032
 *   0x160〜0x16F 86 系         0x60 8086 … 0x63 80386
 *   0x170〜0x17F V シリーズ
 *
 * で、64 ビットの CPU が無い。**A17 として拡張する。**
 * 86 系は連番の続きが自然なので x86-64 を 0x64 とし、ARM 系は空いている
 * 0x180〜0x18F を新設して AArch64 を 0x80 とする。
 *
 * ------------------------------------------------------------------------
 * 配置
 * ------------------------------------------------------------------------
 *
 * 再配置はしない。プログラムは `BTRON_PROC_TEXT_BASE` に固定で結合する。
 * レコードの中身は
 *
 *   ヘッダ (64 バイト) ＋ 本体
 *
 * で、本体は base から始まる 1 枚の像である。先頭 `text_size` バイトが
 * 読み出し＋実行、その後ろ `data_size` バイトが読み書き、さらに
 * `bss_size` バイトが 0 で埋められる。
 *
 * **text と data の境はページ境界に合わせる。** 保護属性をページ単位で
 * しか付けられないので、またがると書けるコードか実行できるデータが
 * できてしまう。
 */

#ifndef __BTRON_OBJFMT_H__
#define __BTRON_OBJFMT_H__

#include <btron/typedef.h>

#define OBJ_MAGIC0 'B'
#define OBJ_MAGIC1 'T'
#define OBJ_MAGIC2 'O'
#define OBJ_MAGIC3 'B'
#define OBJ_MAGIC4 'J'
#define OBJ_MAGIC5 '1'

#define OBJ_VERSION 1
#define OBJ_HDRSZ 64

/* レコードタイプ。共有データ仕様 3.2 が実行プログラムレコードに 9 を
 * 割り当てている。 */
#define OBJ_REC_PROGRAM 9

/* このビルドの CPU タイプ。レコードのサブタイプに入れる。 */
#if defined(__aarch64__)
#define OBJ_CPU_NATIVE 0x80 /* OBJ_CPU_ARM64 */
#elif defined(__x86_64__)
#define OBJ_CPU_NATIVE 0x64 /* OBJ_CPU_X86_64 */
#else
#error "OBJ_CPU_NATIVE が未定義のアーキテクチャ"
#endif

/* CPU タイプ（レコードのサブタイプ）。A17 の拡張を含む。 */
#define OBJ_CPU_TRON32 0x00
#define OBJ_CPU_68000 0x40
#define OBJ_CPU_8086 0x60
#define OBJ_CPU_80386 0x63
#define OBJ_CPU_X86_64 0x64 /* A17: 86 系の続き */
#define OBJ_CPU_ARM64 0x80  /* A17: ARM 系 (0x180〜0x18F) を新設 */

/* ヘッダ内のオフセット。**構造体を直接 I/O しない**（規律 R1）。 */
#define OBJ_MAGIC 0      /* 6 バイト */
#define OBJ_VER 8
#define OBJ_CPU 12
#define OBJ_ENTRY 16     /* base からのオフセット */
#define OBJ_TEXT_SIZE 20 /* 読み出し＋実行の長さ */
#define OBJ_DATA_SIZE 24 /* 読み書きの長さ（ファイルにある分） */
#define OBJ_BSS_SIZE 28  /* data の後ろ。0 で埋める */
#define OBJ_STACK 32     /* 要求するスタック。0 なら既定 */
#define OBJ_BASE 36      /* 結合した先頭アドレス（確認用） */

/* 本OS が用意する既定のユーザスタック。 */
#define OBJ_DEF_STACK (64 * 1024)

#endif /* __BTRON_OBJFMT_H__ */

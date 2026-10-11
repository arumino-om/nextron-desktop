/*
 * machine.h — プラットフォーム情報
 *
 * BTRON3 の慣例 (B-right/V 第3編 1.2.3) に従い、プログラムを実行する
 * ハードウェアや OS に関する情報を定義する。移植性の高いプログラムを
 * 書くための補助情報として使う。
 *
 * 決定は docs/design-issues.md 詳細 H2 を参照。
 *
 * 注意 (規律 R2):
 *   ページサイズをここに定数として置いてはならない。
 *   A13 で 4KB と決めたが、「HAL が公開する値を参照する」規律を同時に
 *   設けている。将来 16KB granule へ移行する余地を残すため。
 */

#ifndef __BTRON_MACHINE_H__
#define __BTRON_MACHINE_H__

/* ------------------------------------------------------------------ */
/* TRON 仕様種別 (μITRON3.0 標準ハンドブックより。B-right/V と共通)     */
/* ------------------------------------------------------------------ */

#define SPEC_TRON 0x0000   /* TRON 共通 (TAD 等) */
#define SPEC_ITRON 0x1000  /* ITRON */
#define SPEC_BTRON 0x2000  /* BTRON */
#define SPEC_CTRON 0x3000  /* CTRON */
#define SPEC_mITRON 0x5000 /* μITRON */
#define SPEC_mBTRON 0x6000 /* μBTRON */

#define SPEC_MAGIC(n) ((n) & 0xf000)
#define SPEC_VER(n) ((n) & 0x0fff)

/*
 * 準拠する BTRON 仕様のバージョン。
 *
 * B-right/V は SPEC_BTRON|0x302 (Ver 3.02) と宣言しているが、
 * 準拠対象の仕様書は Ver 3.20.00 である。本OS は仕様書に合わせて
 * 0x320 を宣言する。意図的な選択であり、B-right/V のデータと
 * 突き合わせる場面ではこの差に注意する。
 */
#define BTRON_SPEC (SPEC_BTRON | 0x320) /* BTRON Ver 3.20 */

/* ------------------------------------------------------------------ */
/* 機種 ID (ビットマスク)                                              */
/* ------------------------------------------------------------------ */

#define PCAT 0x00000010       /* PC-AT 互換機 (B-right/V 既定義) */
#define MACH_X64PC 0x00000020 /* x86_64 汎用 PC (UEFI)  — 本OS */
#define MACH_ARM64 0x00000040 /* arm64 汎用     (UEFI)  — 本OS */

/* ------------------------------------------------------------------ */
/* CPU ID (ビットマスク)                                               */
/* ------------------------------------------------------------------ */

#define CPU_TRON 0x00000001  /* TRON 仕様      (B-right/V 既定義) */
#define CPU_I386 0x00000010  /* i386 互換      (B-right/V 既定義) */
#define CPU_X64 0x00000020   /* x86-64         — 本OS */
#define CPU_ARM64 0x00000040 /* AArch64        — 本OS */
#define CPU_V810 0x00001000  /* V810           (B-right/V 既定義) */

/* ------------------------------------------------------------------ */
/* ターゲット別の定義                                                  */
/* ------------------------------------------------------------------ */

#if defined(__x86_64__)

#define MACHINE_ID MACH_X64PC
#define CPU_ID CPU_X64

#elif defined(__aarch64__)

#define MACHINE_ID MACH_ARM64
#define CPU_ID CPU_ARM64

#else
#error "サポートされていないアーキテクチャです (x86_64 / aarch64 のみ)"
#endif

/* ------------------------------------------------------------------ */
/* プラットフォーム属性（両アーキで共通）                              */
/* ------------------------------------------------------------------ */

#define ALLOW_MISALIGN 1  /* 通常のロード/ストアは未アライメント可      */
                          /* ただしアトミック操作と一部 SIMD は不可     */
#define BIGENDIAN 0       /* A4: 内部はリトルエンディアン               */
#define VIRTUAL_ADDRESS 1 /* 仮想記憶システム                           */
#define USE_PIC_PID 1     /* D2 動的リンカ / I3 ASLR のため PIC を使う  */
#define USE_ROM_MODULE 0  /* ROM 化しない                               */
#define POWER_SAVE 1      /* 省電力関連機能あり                         */

#endif /* __BTRON_MACHINE_H__ */

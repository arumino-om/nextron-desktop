/*
 * header_check.c — 公開ヘッダの自己検査
 *
 * include/btron 以下のヘッダの _Static_assert を両ターゲットで評価させるための
 * 翻訳単位。決定事項が崩れたらビルドが落ちる。
 *
 *   A1  W = 32bit     A2  LINK = 56 byte   A3  TC = 16bit
 *   A4  リトルエンディアン                  A5  STIME = 64bit
 *   A8  D / UD = 64bit                      H2  BTRON_SPEC = 3.20
 */

#include <btron/machine.h>
#include <btron/typedef.h>

_Static_assert(BTRON_SPEC == (SPEC_BTRON | 0x320), "H2: BTRON_SPEC は Ver 3.20");
_Static_assert(BIGENDIAN == 0, "A4: 内部はリトルエンディアン");
_Static_assert(VIRTUAL_ADDRESS == 1, "仮想記憶");

/* A2: fs_name は TC[20] = 40 バイト。+ f_id(UW) 4 + atr1-5 10 = 54。
 * f_id が UW なのでアライメント 4 で 56 に丸められる。
 * 仕様の 52 バイト (f_id が UH のとき) から変わる。 */
_Static_assert(sizeof(LINK) == 56, "A2: LINK = 56 byte");
_Static_assert(_Alignof(LINK) == 4, "A2: LINK の境界");

/* アーキごとに正しい ID が選ばれているか */
#if defined(__aarch64__)
_Static_assert(CPU_ID == CPU_ARM64 && MACHINE_ID == MACH_ARM64, "H2: arm64");
#elif defined(__x86_64__)
_Static_assert(CPU_ID == CPU_X64 && MACHINE_ID == MACH_X64PC, "H2: x86_64");
#endif

/* R5 の確認: 縮小変換は明示キャストでのみ通る。
 * ここから (D) を外すと -Wconversion -Werror でビルドが落ちる。 */
EXPORT W btron_header_check_truncate(D v) { return (W)v; }

/* R8 の形: 「文字」は (TLANG, TC) の対である */
EXPORT WTC btron_header_check_wtc(TLANG lang, TC ch) { return towtc(lang, ch); }

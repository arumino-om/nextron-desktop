/*
 * apic.h — Local APIC の内部の口 (x86_64 の HAL の中だけ)
 *
 * **公開ヘッダではない。** 同じディレクトリの中（vectors.S の受け口である
 * exception.c と apic.c）だけで使う。`hal_intc_*` の契約は
 * `include/btron/intc.h` の方にあり、そこに x86 固有の口を足さないために
 * ここを置いてある（アーキの差を HAL の中に閉じる。規律 R3 と同じ姿勢）。
 */

#ifndef __BTRON_HAL_X86_64_APIC_H__
#define __BTRON_HAL_X86_64_APIC_H__

#include <btron/typedef.h>

/* ゲートが受け取ったベクタを預ける。`hal_intc_acknowledge()` が 1 度だけ
 * これを返す（apic.c の冒頭「どの割込みが起きたかの聞き方が逆」）。 */
IMPORT void apic_take_vector(UD vec);

/* 較正で求めた APIC タイマの周波数。ログと検査用。0 なら未較正。 */
IMPORT UD hal_apic_hz(void);

#endif /* __BTRON_HAL_X86_64_APIC_H__ */

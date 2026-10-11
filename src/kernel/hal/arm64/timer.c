/*
 * timer.c — generic timer の読み出し (arm64)
 *
 * ARMv8 の generic timer はシステムレジスタで読める。
 *
 *   CNTFRQ_EL0   周波数 (Hz)。ファームウェアが設定する
 *   CNTPCT_EL0   物理カウンタ。単調増加
 *
 * **周波数も値もレジスタから読めるので、ファームウェアの記述 (DT / ACPI) に
 * 依存しない。** 記述が必要になるのは割込み（EL1 物理タイマの INTID）の方で、
 * それは MachDesc.timer_intid に入っている。中心核の時間管理（BTRON3 §1.3）で
 * GIC と一緒に使う。
 */

#include <btron/hal.h>

EXPORT UD hal_timer_freq(void)
{
	UD v;

	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
	return v;
}

EXPORT UD hal_timer_count(void)
{
	UD v;

	/* isb を挟まないと投機実行で古い値を読みうる */
	__asm__ volatile(
	    "isb\n"
	    "mrs %0, cntpct_el0\n"
	    : "=r"(v)::"memory");
	return v;
}

EXPORT void hal_timer_wait_us(UD usec)
{
	UD freq = hal_timer_freq();
	UD ticks;
	UD start;

	if (freq == 0) {
		return;
	}
	/* freq は普通 1〜100MHz。usec * freq が溢れないよう先に割る。 */
	ticks = (freq / 1000000UL) * usec;
	if (ticks == 0) {
		ticks = 1;
	}
	start = hal_timer_count();
	while (hal_timer_count() - start < ticks) {
		__asm__ volatile("yield" ::: "memory");
	}
}

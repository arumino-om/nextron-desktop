/*
 * timer.c — 時間の読み出し (x86_64)
 *
 * TSC は読めるが、**周波数はレジスタから直接は分からない**。
 * CPUID leaf 0x15 (TSC/Core Crystal Clock) が使えれば計算でき、
 * 使えなければ ACPI PM タイマか HPET で校正する必要がある。
 *
 * ここが arm64 との構造差。arm64 は CNTFRQ_EL0 に周波数が入っているので
 * 校正が要らない。
 *
 * 【状態】ブート経路が無いため未検証。CPUID 0x15 だけ実装し、
 * 取れなければ 0 を返す（呼び側は 0 を「不明」として扱う契約）。
 */

#include <btron/hal.h>

LOCAL void cpuid(UW leaf, UW sub, UW out[4])
{
	__asm__ volatile("cpuid"
	                 : "=a"(out[0]), "=b"(out[1]), "=c"(out[2]), "=d"(out[3])
	                 : "a"(leaf), "c"(sub));
}

EXPORT UD hal_timer_freq(void)
{
	UW r[4];

	cpuid(0, 0, r);
	if (r[0] < 0x15) {
		return 0; /* leaf 0x15 が無い。校正が必要 */
	}
	cpuid(0x15, 0, r);
	/* EAX = 分母, EBX = 分子, ECX = コア水晶発振器の周波数 (Hz) */
	if (r[0] == 0 || r[1] == 0 || r[2] == 0) {
		return 0;
	}
	return (UD)r[2] * r[1] / r[0];
}

EXPORT UD hal_timer_count(void)
{
	UW lo;
	UW hi;

	__asm__ volatile(
	    "lfence\n"
	    "rdtsc\n"
	    : "=a"(lo), "=d"(hi)::"memory");
	return ((UD)hi << 32) | lo;
}

EXPORT void hal_timer_wait_us(UD usec)
{
	UD freq = hal_timer_freq();
	UD ticks;
	UD start;

	if (freq == 0) {
		return;
	}
	ticks = (freq / 1000000UL) * usec;
	if (ticks == 0) {
		ticks = 1;
	}
	start = hal_timer_count();
	while (hal_timer_count() - start < ticks) {
		__asm__ volatile("pause" ::: "memory");
	}
}

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
 * CPUID 0x15 が使えない機種が普通にあるので（QEMU の既定の CPU モデルも
 * そう）、**PIT (8254) で校正する**。チャネル 2 を一発モードで走らせ、
 * ポート 0x61 bit5 の立ち上がりまでに TSC が何回進んだかを数える。
 * ACPI の PM タイマや HPET を使う手もあるが、PIT はポート I/O だけで
 * 済み、ACPI の解釈も写像も要らない — 起動のいちばん早い段階で使える。
 *
 * **校正は 1 度だけ行い、結果を覚える。** 10ms を測るので、毎回やると
 * 起動が目に見えて遅くなる。
 */

#include <btron/hal.h>

LOCAL void cpuid(UW leaf, UW sub, UW out[4])
{
	__asm__ volatile("cpuid"
	                 : "=a"(out[0]), "=b"(out[1]), "=c"(out[2]), "=d"(out[3])
	                 : "a"(leaf), "c"(sub));
}

LOCAL inline void outb(UH port, UB v)
{
	__asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

LOCAL inline UB inb(UH port)
{
	UB v;

	__asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
	return v;
}

/* PIT (8254) のポート。 */
#define PIT_CH2 0x42U
#define PIT_CMD 0x43U
#define PIT_GATE 0x61U /* bit0 = チャネル 2 のゲート, bit5 = 出力 */

#define PIT_HZ 1193182U /* 水晶の周波数。規定値 */

/* PIT で 10ms を測り、その間の TSC の進みから周波数を出す。 */
LOCAL UD calibrate_with_pit(void)
{
	UW ticks = PIT_HZ / 100U; /* 10ms */
	UD t0;
	UD t1;
	UB gate;

	/* チャネル 2 を「一発」（モード 0）、2 バイトでカウンタを書く。 */
	gate = inb(PIT_GATE);
	outb(PIT_GATE, (UB)(gate & 0xFCU)); /* ゲートを落とし、スピーカは切る */
	outb(PIT_CMD, 0xB0U);               /* ch2 / lobyte+hibyte / mode 0 */
	outb(PIT_CH2, (UB)(ticks & 0xFFU));
	outb(PIT_CH2, (UB)((ticks >> 8) & 0xFFU));

	/* ゲートを上げるとカウントが始まる。 */
	t0 = hal_timer_count();
	outb(PIT_GATE, (UB)((gate & 0xFCU) | 0x01U));

	/* 出力 (bit5) が立つまで待つ。カウンタが 0 になった印。 */
	while ((inb(PIT_GATE) & 0x20U) == 0) {
		/* 待つだけ */
	}
	t1 = hal_timer_count();
	outb(PIT_GATE, gate);

	if (t1 <= t0) {
		return 0;
	}
	/* 10ms 分なので 100 倍して Hz にする。 */
	return (t1 - t0) * 100ULL;
}

EXPORT UD hal_timer_freq(void)
{
	/* 覚えておく。校正には 10ms かかるので毎回はやらない。 */
	LOCAL UD cached;
	UW r[4];

	if (cached != 0) {
		return cached;
	}

	cpuid(0, 0, r);
	if (r[0] >= 0x15) {
		cpuid(0x15, 0, r);
		/* EAX = 分母, EBX = 分子, ECX = コア水晶発振器の周波数 (Hz) */
		if (r[0] != 0 && r[1] != 0 && r[2] != 0) {
			cached = (UD)r[2] * r[1] / r[0];
			return cached;
		}
	}
	/* leaf 0x15 が使えない。PIT で校正する。 */
	cached = calibrate_with_pit();
	return cached;
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

/* DMA の順序づけ (hal.h)。
 *
 * x86_64 は TSO なので「書き込み同士」の順序は保たれ、本来 `mfence` は
 * 不要である。それでも置くのは、virtqueue のあとにレジスタへ通知する
 * 経路で WC 書き込みが混じった場合に備えるため。コストは arm64 側に
 * 比べて無視できる。 */
EXPORT void hal_dma_barrier(void)
{
	__asm__ volatile("mfence" ::: "memory");
}

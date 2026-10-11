/*
 * irq.c — 割込みの禁止・許可とタイマ割込み (arm64)
 *
 * DAIF の I ビットが IRQ のマスク。立っていると禁止。
 * アイドルは WFI で止める（WFE ではない。WFI は割込みで必ず起きる）。
 *
 * タイマは generic timer の EL1 物理タイマ (CNTP_*)。
 * 割込み番号 (INTID) は MachDesc から取る（規律 R19）。
 */

#include <btron/hal.h>
#include <btron/intc.h>
#include <btron/machdesc.h>

#define DAIF_I (1ULL << 7)

#define CNTP_CTL_ENABLE (1ULL << 0)
#define CNTP_CTL_IMASK (1ULL << 1)

LOCAL UD tick_interval; /* 1 tick のカウンタ値 */
LOCAL UW tick_ms = 1;   /* 1 tick のミリ秒 */

EXPORT UD hal_int_disable(void)
{
	UD daif;

	__asm__ volatile("mrs %0, daif" : "=r"(daif));
	__asm__ volatile("msr daifset, #2" ::: "memory"); /* I ビットを立てる */
	return daif;
}

EXPORT void hal_int_restore(UD state)
{
	__asm__ volatile("msr daif, %0" : : "r"(state) : "memory");
}

EXPORT void hal_int_enable(void)
{
	__asm__ volatile("msr daifclr, #2" ::: "memory");
}

EXPORT Bool hal_int_enabled(void)
{
	UD daif;

	__asm__ volatile("mrs %0, daif" : "=r"(daif));
	return ((daif & DAIF_I) == 0) ? True : False;
}

EXPORT void hal_cpu_idle(void)
{
	/* WFI は DAIF.I でマスクされていても割込みの到来で起きる。
	 * 割込みを禁止したままアイドルに入っても死なないようにこれを使う。 */
	__asm__ volatile(
	    "dsb sy\n"
	    "wfi\n" ::: "memory");
}

/* ------------------------------------------------------------------ */
/* タイマ割込み                                                        */
/* ------------------------------------------------------------------ */

/* 次の tick を仕込む。ハンドラの先頭で呼ぶ。 */
EXPORT void hal_timer_tick_reload(void)
{
	__asm__ volatile("msr cntp_tval_el0, %0" : : "r"(tick_interval) : "memory");
}

EXPORT UW hal_timer_tick_ms(void)
{
	return tick_ms;
}

EXPORT ER hal_timer_tick_start(UW hz)
{
	CONST MachDesc *md = machdesc();
	UD freq = hal_timer_freq();
	UW intid = md->timer_intid[MD_TIMER_NONSECURE];
	ER er;

	if (hz == 0 || freq == 0) {
		return E_PAR;
	}
	if (intid < 16 || intid >= 32) {
		/* EL1 物理タイマは PPI なので INTID 16..31 でなければおかしい。
		 * FDT の PPI 番号を +16 し忘れるとここで気づく (Phase 1 ⑫)。 */
		return E_SYS;
	}
	tick_interval = freq / hz;
	tick_ms = 1000U / hz;
	if (tick_ms == 0) {
		tick_ms = 1;
	}

	er = intc_set_priority(intid, 0x80);
	if (er != E_OK) {
		return er;
	}
	hal_timer_tick_reload();
	__asm__ volatile("msr cntp_ctl_el0, %0" : : "r"(CNTP_CTL_ENABLE) : "memory");

	return intc_enable(intid);
}

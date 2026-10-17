/*
 * apic.c — Local APIC と タイマ割込み (x86_64)
 *
 * arm64 の gic.c + irq.c のタイマ部分に当たる。アドレスは記述（ACPI の
 * MADT）から取る（規律 R19）。
 *
 * ------------------------------------------------------------------------
 * GIC との構造差 — 「どの割込みが起きたか」の聞き方が逆
 * ------------------------------------------------------------------------
 *
 * GIC は**レジスタに聞く**（GICC_IAR を読むと INTID が返り、読んだことが
 * 受領になる）。だから `hal_intc_acknowledge()` が自然に書ける。
 *
 * x86 は**ベクタごとに別のゲート**で、番号はハードウェアがソフトウェアへ
 * 「制御を渡す先」として教える。割込みが起きた後にレジスタへ聞く手段は無い
 * （ISR を走査すれば分かるが、それは「いま処理中のもの」ではなく
 * 「保留中のもの全部」である）。
 *
 * そこで**ゲートが受け取ったベクタを 1 つだけ預かる**形にした。
 * vectors.S → btron_exception_entry() → apic_take_vector(vec) で預け、
 * `hal_intc_acknowledge()` が 1 度だけ返して空にする。2 度目は
 * 1023（spurious）を返すので、`intc_dispatch()` のループが 1 周で終わる。
 *
 * **INTID = ベクタ番号**として扱う。中心核の表は 256 個あり、x86 の
 * 外部割込みは 32..255 なので、そのまま添字になる。
 *
 * ------------------------------------------------------------------------
 * タイマ — 周波数が記述に無い
 * ------------------------------------------------------------------------
 *
 * arm64 は `CNTFRQ_EL0` に周波数が入っているので割れば済む。Local APIC の
 * タイマは**バス / コア水晶の周波数で動き、その値はどこにも書いていない**。
 * CPUID 0x15 で取れる機種もあるが QEMU では 0 を返す。
 *
 * だから**較正する**。TSC は timer.c が PIT で較正済みなので、それを
 * 物差しにして「一定の TSC 時間に APIC タイマがいくつ減ったか」を測る。
 * PIT をもう一度使わないのは、較正の二重実装を避けるため。
 */

#include <btron/hal.h>
#include <btron/intc.h>
#include <btron/machdesc.h>
#include <btron/vmem.h>

#include "apic.h"

/* Local APIC のレジスタ（xAPIC。MMIO の先頭からのオフセット）。 */
#define LAPIC_ID 0x020
#define LAPIC_VER 0x030
#define LAPIC_TPR 0x080
#define LAPIC_EOI 0x0B0
#define LAPIC_SVR 0x0F0   /* Spurious Interrupt Vector Register */
#define LAPIC_ICR_LO 0x300
#define LAPIC_LVT_TIMER 0x320
#define LAPIC_TIMER_ICR 0x380 /* Initial Count */
#define LAPIC_TIMER_CCR 0x390 /* Current Count */
#define LAPIC_TIMER_DCR 0x3E0 /* Divide Configuration */

#define SVR_ENABLE 0x100U /* bit8: APIC を有効にする */

#define LVT_MASKED 0x10000U
#define LVT_PERIODIC 0x20000U

#define DCR_DIV16 0x3U

/* 使うベクタ。**記述から来るのではなくこちらが決める**（intc.h の
 * `hal_timer_tick_intid` の注記）。
 *
 *   0x20  tick。中心核の時間管理が使う
 *   0x21- MSI / MSI-X の宛先に配る（詳細 F8）
 *   0xFF  spurious。APIC が原因不明の割込みを上げるときのベクタ
 *
 * 0x00-0x1F は例外に予約されているので使えない。 */
#define VEC_TICK 0x20U
#define VEC_MSI_FIRST 0x21U
#define VEC_MSI_LAST 0xEFU
#define VEC_SPURIOUS 0xFFU

LOCAL UD lapic;         /* MMIO の別名。0 なら未初期化 */
LOCAL UD tick_interval; /* 1 tick の APIC タイマカウント */
LOCAL UW tick_ms = 1;
LOCAL UD apic_hz;       /* 較正で求めた APIC タイマの周波数 */
LOCAL UW msi_next = VEC_MSI_FIRST;

/* ゲートが預けたベクタ。0 なら空。 */
LOCAL UW pending;

LOCAL inline void wr(UW off, UW v)
{
	*(volatile UW *)(VP)(lapic + off) = v;
}

LOCAL inline UW rd(UW off)
{
	return *(volatile UW *)(VP)(lapic + off);
}

/* ------------------------------------------------------------------ */
/* ベクタの受け渡し（vectors.S → exception.c → ここ）                 */
/* ------------------------------------------------------------------ */

EXPORT void apic_take_vector(UD vec)
{
	pending = (UW)vec;
}

/* ------------------------------------------------------------------ */
/* hal_intc_* — GIC と同じ口を APIC で                                 */
/* ------------------------------------------------------------------ */

EXPORT ER hal_intc_init(void)
{
	CONST MachDesc *md = machdesc();
	UD pa = md->intc_cpu_pa;

	if (md->intc_type != MD_INTC_APIC || pa == 0) {
		/* 記述に APIC が無い。**既定値 0xFEE0_0000 を決め打ちしない**
		 * （規律 R19 / R31）。割込みの要る機能はここで止まる。 */
		return E_NOSPT;
	}
	lapic = hal_vm_map_device(pa, 0x1000);
	if (lapic == 0) {
		return E_NOMEM;
	}

	/* spurious ベクタを置いて APIC を有効にする。**これを書かないと
	 * 以後の EOI も LVT も効かない**（ソフト無効のまま）。 */
	wr(LAPIC_SVR, VEC_SPURIOUS | SVR_ENABLE);

	/* TPR = 0。どの優先度の割込みも通す。GIC の優先度マスクに当たる。 */
	wr(LAPIC_TPR, 0);

	/* タイマは止めた状態から始める（hal_timer_tick_start で開ける）。 */
	wr(LAPIC_LVT_TIMER, LVT_MASKED);
	return E_OK;
}

EXPORT ER hal_intc_enable(UW intid)
{
	if (lapic == 0) {
		return E_SYS;
	}
	/* **Local APIC には「有効にする」表が無い。** GIC の GICD_ISENABLER に
	 * 当たるものはベクタごとの LVT（タイマ・サーマル等）か、IOAPIC の
	 * リダイレクション表（レガシ割込み。F9 で後回し）にある。
	 * ここで扱うのは LVT タイマだけなので、それ以外は何もしないで成功を
	 * 返す —— MSI は装置側が送るので、受ける側に設定は要らない。 */
	if (intid == VEC_TICK) {
		UW v = rd(LAPIC_LVT_TIMER);

		wr(LAPIC_LVT_TIMER, v & ~LVT_MASKED);
	}
	return E_OK;
}

EXPORT ER hal_intc_disable(UW intid)
{
	if (lapic == 0) {
		return E_SYS;
	}
	if (intid == VEC_TICK) {
		wr(LAPIC_LVT_TIMER, rd(LAPIC_LVT_TIMER) | LVT_MASKED);
	}
	return E_OK;
}

EXPORT ER hal_intc_set_priority(UW intid, UW prio)
{
	(void)intid;
	(void)prio;
	/* **x86 の割込み優先度はベクタ番号そのもの**（上位 4bit がクラス）で、
	 * GIC のような優先度レジスタは無い。番号の割り当てで決まっているので
	 * ここでは何もしない。嘘の失敗を返すと中心核が止まるので E_OK。 */
	return E_OK;
}

EXPORT UW hal_intc_acknowledge(void)
{
	UW v = pending;

	if (v == 0) {
		return 1023U; /* 空。intc_dispatch のループを終わらせる */
	}
	pending = 0;
	return v;
}

EXPORT void hal_intc_end(UW intid)
{
	(void)intid;
	if (lapic != 0) {
		/* EOI は**番号を書かない**（GIC は INTID を書く）。
		 * いま処理中のものを APIC が知っているため。 */
		wr(LAPIC_EOI, 0);
	}
}

EXPORT UW hal_intc_version(void)
{
	/* ログと検査用。GIC の版に当たるものとして APIC の版レジスタの
	 * 下位 8bit（xAPIC なら 0x10 以上）を返す。 */
	return (lapic != 0) ? (rd(LAPIC_VER) & 0xFFU) : 0;
}

/* ------------------------------------------------------------------ */
/* MSI の宛先 (詳細 F8)                                                */
/* ------------------------------------------------------------------ */

EXPORT ER hal_msi_alloc(UW *intid, UD *addr, UW *data)
{
	CONST MachDesc *md = machdesc();

	if (intid == 0 || addr == 0 || data == 0) {
		return E_PAR;
	}
	if (lapic == 0) {
		return E_SYS;
	}
	if (msi_next > VEC_MSI_LAST) {
		return E_LIMIT;
	}

	/* **x86 の MSI は「LAPIC の番地へベクタ番号を書く」こと**である
	 * （詳細 F8）。宛先アドレスの bits[19:12] が宛先 APIC ID で、
	 * ここでは起動した CPU (BSP) に固定する。翻訳器 (arm64 の ITS) に
	 * 当たるものが要らないのはこの形のおかげ。 */
	*addr = md->intc_cpu_pa | ((UD)(rd(LAPIC_ID) >> 24) << 12);
	*data = msi_next; /* 送出モードは固定 (edge / fixed) なので番号だけ */
	*intid = msi_next;
	msi_next++;
	return E_OK;
}

/* ------------------------------------------------------------------ */
/* タイマ割込み                                                        */
/* ------------------------------------------------------------------ */

EXPORT UW hal_timer_tick_intid(void)
{
	return VEC_TICK;
}

EXPORT UW hal_timer_tick_ms(void)
{
	return tick_ms;
}

EXPORT void hal_timer_tick_reload(void)
{
	/* 周期モードなので APIC が自分で入れ直す。arm64 の CNTP_TVAL のような
	 * 仕込み直しは要らない。**何もしないのが正しい。** */
}

/* APIC タイマの周波数を較正する。
 *
 * TSC を物差しにする（timer.c が PIT で較正済み）。測る時間が短いと
 * 分母が小さくて誤差が乗るので、1ms 相当を測る。 */
LOCAL UD calibrate(void)
{
	UD tsc_hz = hal_timer_freq();
	UD t0;
	UD t1;
	UW c0;
	UW c1;
	UD want;

	if (tsc_hz == 0) {
		return 0;
	}
	wr(LAPIC_TIMER_DCR, DCR_DIV16);
	wr(LAPIC_LVT_TIMER, LVT_MASKED); /* 割込みは出さずに走らせる */
	wr(LAPIC_TIMER_ICR, 0xFFFFFFFFU);

	want = tsc_hz / 1000U; /* 1ms */
	t0 = hal_timer_count();
	c0 = rd(LAPIC_TIMER_CCR);
	do {
		t1 = hal_timer_count();
	} while (t1 - t0 < want);
	c1 = rd(LAPIC_TIMER_CCR);

	wr(LAPIC_TIMER_ICR, 0); /* 止める */

	if (c0 <= c1) {
		return 0; /* 減っていない = タイマが動いていない */
	}
	/* (減った数) / (経過秒) = 分周後の周波数。分周は 16。 */
	return (UD)(c0 - c1) * 1000UL * 16UL;
}

EXPORT ER hal_timer_tick_start(UW hz)
{
	if (lapic == 0) {
		return E_SYS;
	}
	if (hz == 0) {
		return E_PAR;
	}
	apic_hz = calibrate();
	if (apic_hz == 0) {
		return E_SYS;
	}
	tick_ms = 1000U / hz;
	if (tick_ms == 0) {
		tick_ms = 1;
	}
	/* 分周は 16 のまま使う。較正値も分周前の値に直してあるので割って戻す。 */
	tick_interval = (apic_hz / 16UL) / hz;
	if (tick_interval == 0) {
		tick_interval = 1;
	}

	wr(LAPIC_TIMER_DCR, DCR_DIV16);
	wr(LAPIC_LVT_TIMER, VEC_TICK | LVT_PERIODIC);
	wr(LAPIC_TIMER_ICR, (UW)tick_interval);
	return intc_enable(VEC_TICK);
}

EXPORT UD hal_apic_hz(void)
{
	return apic_hz;
}

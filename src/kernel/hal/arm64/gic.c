/*
 * gic.c — GIC (Generic Interrupt Controller) v2 / v3
 *
 * docs/design-issues.md 詳細 D8 / 規律 R19。
 * アドレスも版も `MachDesc` から来る。ここに直書きしない。
 *
 * 【v2 と v3 の違い】
 *
 *   v2  distributor (GICD) + CPU インタフェース (GICC) の 2 つの MMIO。
 *       割込みの受付と完了は GICC のレジスタを読み書きする。
 *   v3  distributor (GICD) + コアごとの再配布器 (GICR) の MMIO に加え、
 *       **CPU インタフェースがシステムレジスタになった** (ICC_*_EL1)。
 *       MMIO ではなくレジスタを叩くので、まず ICC_SRE_EL1.SRE を立てて
 *       システムレジスタ経路を有効にする必要がある。
 *
 * この差を吸収して intc.h の 1 つのインタフェースにする。
 */

#include <btron/hal.h>
#include <btron/intc.h>
#include <btron/machdesc.h>
#include <btron/vmem.h>

/* ------------------------------------------------------------------ */
/* レジスタ                                                            */
/* ------------------------------------------------------------------ */

/* distributor (v2 / v3 共通の部分) */
#define GICD_CTLR 0x000
#define GICD_TYPER 0x004
#define GICD_IGROUPR 0x080  /* +4*n */
#define GICD_ISENABLER 0x100
#define GICD_ICENABLER 0x180
#define GICD_IPRIORITYR 0x400 /* +n (バイト単位) */
#define GICD_ICFGR 0xC00

#define GICD_CTLR_ENABLE_G0 (1U << 0)
#define GICD_CTLR_ENABLE_G1 (1U << 1)
/* v3 の GICD_CTLR */
#define GICD_CTLR_ARE_NS (1U << 4)
#define GICD_CTLR_ENABLE_G1NS (1U << 1)
#define GICD_CTLR_RWP (1U << 31) /* Register Write Pending */

/* CPU インタフェース (v2 のみ) */
#define GICC_CTLR 0x0000
#define GICC_PMR 0x0004
#define GICC_BPR 0x0008
#define GICC_IAR 0x000C
#define GICC_EOIR 0x0010

/* 再配布器 (v3 のみ) */
#define GICR_CTLR 0x0000
#define GICR_WAKER 0x0014
#define GICR_SGI_BASE 0x10000 /* SGI / PPI 用のフレーム */
#define GICR_IGROUPR0 (GICR_SGI_BASE + 0x0080)
#define GICR_ISENABLER0 (GICR_SGI_BASE + 0x0100)
#define GICR_ICENABLER0 (GICR_SGI_BASE + 0x0180)
#define GICR_IPRIORITYR (GICR_SGI_BASE + 0x0400)

#define GICR_WAKER_PROCESSOR_SLEEP (1U << 1)
#define GICR_WAKER_CHILDREN_ASLEEP (1U << 2)

#define INTID_SPURIOUS 1023U
#define PPI_FIRST 16U
#define SPI_FIRST 32U

LOCAL UD gicd;        /* distributor の仮想アドレス */
LOCAL UD giccpu;      /* v2: GICC / v3: このコアの GICR */
LOCAL UW gic_version; /* 2 or 3 */

/* ------------------------------------------------------------------ */
/* MMIO                                                                */
/* ------------------------------------------------------------------ */

LOCAL inline UW rd32(UD base, UD off)
{
	return *(volatile UW *)(VP)(base + off);
}

LOCAL inline void wr32(UD base, UD off, UW v)
{
	*(volatile UW *)(VP)(base + off) = v;
}

LOCAL inline void wr8(UD base, UD off, UB v)
{
	*(volatile UB *)(VP)(base + off) = v;
}

/* ------------------------------------------------------------------ */
/* v3 のシステムレジスタ                                               */
/*                                                                     */
/* ICC_* は GICv3 で追加されたので、アセンブラに名前を知らせるために     */
/* S3_<op1>_<Cn>_<Cm>_<op2> の形で書く。                               */
/* ------------------------------------------------------------------ */

#define ICC_SRE_EL1 "S3_0_C12_C12_5"
#define ICC_PMR_EL1 "S3_0_C4_C6_0"
#define ICC_IGRPEN1_EL1 "S3_0_C12_C12_7"
#define ICC_IAR1_EL1 "S3_0_C12_C12_0"
#define ICC_EOIR1_EL1 "S3_0_C12_C12_1"
#define ICC_BPR1_EL1 "S3_0_C12_C12_3"
#define ICC_CTLR_EL1 "S3_0_C12_C12_4"

#define SYSREG_WRITE(name, val)                                                          \
	__asm__ volatile("msr " name ", %0" : : "r"((UD)(val)) : "memory")
#define SYSREG_READ(name, out)                                                           \
	__asm__ volatile("mrs %0, " name : "=r"(out)::"memory")

/* ------------------------------------------------------------------ */
/* 初期化                                                              */
/* ------------------------------------------------------------------ */

LOCAL void gicd_wait_rwp(void)
{
	W spin = 1000000;

	while ((rd32(gicd, GICD_CTLR) & GICD_CTLR_RWP) != 0 && spin-- > 0) {
		/* 書き込みが行き渡るのを待つ */
	}
}

LOCAL ER init_v2(CONST MachDesc *md)
{
	UW n;
	UW i;

	giccpu = hal_vm_map_device(md->intc_cpu_pa,
	                           md->intc_cpu_size != 0 ? md->intc_cpu_size : 0x2000);
	if (giccpu == 0) {
		return E_SYS;
	}

	/* distributor を止めてから設定する */
	wr32(gicd, GICD_CTLR, 0);

	n = (rd32(gicd, GICD_TYPER) & 0x1FU) + 1U; /* 32 本単位の個数 */
	for (i = 0; i < n; i++) {
		/* 全部無効・優先度は中間値・グループ 0 */
		wr32(gicd, GICD_ICENABLER + i * 4, 0xFFFFFFFFU);
		wr32(gicd, GICD_IGROUPR + i * 4, 0);
	}
	for (i = 0; i < n * 32U; i++) {
		wr8(gicd, GICD_IPRIORITYR + i, 0xA0);
	}

	wr32(gicd, GICD_CTLR, GICD_CTLR_ENABLE_G0 | GICD_CTLR_ENABLE_G1);

	/* CPU インタフェース。PMR は「この値より小さい優先度だけ通す」ので、
	 * 全部通すために最大値を書く。 */
	wr32(giccpu, GICC_PMR, 0xFF);
	wr32(giccpu, GICC_BPR, 0);
	wr32(giccpu, GICC_CTLR, 1);
	return E_OK;
}

LOCAL ER init_v3(CONST MachDesc *md)
{
	UD sre;
	UW n;
	UW i;
	W spin;

	/* 再配布器。MachDesc が持っているのはこのコアのフレームの先頭。
	 * SGI/PPI 用フレームを含むので 0x20000 取る。 */
	giccpu = hal_vm_map_device(md->intc_cpu_pa,
	                           md->intc_cpu_size >= 0x20000 ? md->intc_cpu_size : 0x20000);
	if (giccpu == 0) {
		return E_SYS;
	}

	/* 再配布器を起こす。ProcessorSleep を落として ChildrenAsleep が
	 * 下りるのを待つ。これをしないと PPI が届かない。 */
	wr32(giccpu, GICR_WAKER, rd32(giccpu, GICR_WAKER) & ~GICR_WAKER_PROCESSOR_SLEEP);
	spin = 1000000;
	while ((rd32(giccpu, GICR_WAKER) & GICR_WAKER_CHILDREN_ASLEEP) != 0 && spin-- > 0) {
		/* 起きるのを待つ */
	}

	/* distributor。ARE_NS を立てないと v3 の親和性ルーティングが働かない。 */
	wr32(gicd, GICD_CTLR, 0);
	gicd_wait_rwp();
	wr32(gicd, GICD_CTLR, GICD_CTLR_ARE_NS);
	gicd_wait_rwp();

	n = (rd32(gicd, GICD_TYPER) & 0x1FU) + 1U;
	for (i = 1; i < n; i++) { /* SPI は 32 番から = i=1 以降 */
		wr32(gicd, GICD_ICENABLER + i * 4, 0xFFFFFFFFU);
		wr32(gicd, GICD_IGROUPR + i * 4, 0xFFFFFFFFU); /* グループ 1 NS */
	}
	for (i = SPI_FIRST; i < n * 32U; i++) {
		wr8(gicd, GICD_IPRIORITYR + i, 0xA0);
	}
	wr32(gicd, GICD_CTLR, GICD_CTLR_ARE_NS | GICD_CTLR_ENABLE_G1NS);
	gicd_wait_rwp();

	/* SGI / PPI は再配布器側で設定する */
	wr32(giccpu, GICR_ICENABLER0, 0xFFFFFFFFU);
	wr32(giccpu, GICR_IGROUPR0, 0xFFFFFFFFU);
	for (i = 0; i < SPI_FIRST; i++) {
		wr8(giccpu, GICR_IPRIORITYR + i, 0xA0);
	}

	/* CPU インタフェースをシステムレジスタ経路にする。
	 * SRE を立てないと ICC_* は使えない（UNDEFINED になる）。 */
	SYSREG_READ(ICC_SRE_EL1, sre);
	SYSREG_WRITE(ICC_SRE_EL1, sre | 1ULL);
	__asm__ volatile("isb" ::: "memory");

	SYSREG_WRITE(ICC_PMR_EL1, 0xFFULL);
	SYSREG_WRITE(ICC_BPR1_EL1, 0ULL);
	SYSREG_WRITE(ICC_CTLR_EL1, 0ULL);
	SYSREG_WRITE(ICC_IGRPEN1_EL1, 1ULL);
	__asm__ volatile("isb" ::: "memory");
	return E_OK;
}

EXPORT ER hal_intc_init(void)
{
	CONST MachDesc *md = machdesc();

	if (md->intc_type != MD_INTC_GICV2 && md->intc_type != MD_INTC_GICV3) {
		return E_NOSPT;
	}
	if (md->intc_dist_pa == 0 || md->intc_cpu_pa == 0) {
		return E_SYS;
	}
	gic_version = (md->intc_type == MD_INTC_GICV3) ? 3U : 2U;

	gicd = hal_vm_map_device(md->intc_dist_pa,
	                         md->intc_dist_size != 0 ? md->intc_dist_size : 0x10000);
	if (gicd == 0) {
		return E_SYS;
	}
	return (gic_version == 3) ? init_v3(md) : init_v2(md);
}

/* ------------------------------------------------------------------ */
/* 個別の割込み                                                        */
/* ------------------------------------------------------------------ */

/* SGI / PPI (INTID < 32) は v3 では再配布器側、v2 では distributor 側。 */
LOCAL inline Bool on_redist(UW intid)
{
	return (gic_version == 3 && intid < SPI_FIRST) ? True : False;
}

EXPORT ER hal_intc_enable(UW intid)
{
	UW reg = intid / 32U;
	UW bit = 1U << (intid % 32U);

	if (intid >= INTC_MAX_INTID) {
		return E_PAR;
	}
	if (on_redist(intid)) {
		wr32(giccpu, GICR_ISENABLER0, bit);
	} else {
		wr32(gicd, GICD_ISENABLER + reg * 4, bit);
		gicd_wait_rwp();
	}
	return E_OK;
}

EXPORT ER hal_intc_disable(UW intid)
{
	UW reg = intid / 32U;
	UW bit = 1U << (intid % 32U);

	if (intid >= INTC_MAX_INTID) {
		return E_PAR;
	}
	if (on_redist(intid)) {
		wr32(giccpu, GICR_ICENABLER0, bit);
	} else {
		wr32(gicd, GICD_ICENABLER + reg * 4, bit);
		gicd_wait_rwp();
	}
	return E_OK;
}

EXPORT ER hal_intc_set_priority(UW intid, UW prio)
{
	if (intid >= INTC_MAX_INTID || prio > 0xFF) {
		return E_PAR;
	}
	if (on_redist(intid)) {
		wr8(giccpu, GICR_IPRIORITYR + intid, (UB)prio);
	} else {
		wr8(gicd, GICD_IPRIORITYR + intid, (UB)prio);
	}
	return E_OK;
}

/* ------------------------------------------------------------------ */
/* 受付と完了                                                          */
/* ------------------------------------------------------------------ */

/* 発生した割込みの INTID を取る。無ければ INTID_SPURIOUS。 */
EXPORT UW hal_intc_acknowledge(void)
{
	UD v;

	if (gic_version == 3) {
		SYSREG_READ(ICC_IAR1_EL1, v);
		return (UW)(v & 0xFFFFFFU);
	}
	return rd32(giccpu, GICC_IAR) & 0x3FFU;
}

EXPORT void hal_intc_end(UW intid)
{
	if (gic_version == 3) {
		SYSREG_WRITE(ICC_EOIR1_EL1, intid);
	} else {
		wr32(giccpu, GICC_EOIR, intid);
	}
}

EXPORT UW hal_intc_version(void)
{
	return gic_version;
}

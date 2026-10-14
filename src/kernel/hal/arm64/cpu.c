/*
 * cpu.c — CPU の起動時設定 (arm64)
 *
 * 起動第 1 段のいちばん最初に呼ぶ。MMU より前でよい。
 */

#include <btron/hal.h>

#define CPACR_FPEN_NOTRAP (3ULL << 20)

/* SCTLR_EL1.SPAN。1 なら、EL0 からの例外で PSTATE.PAN を立てない。 */
#define SCTLR_SPAN (1ULL << 23)

EXPORT void hal_cpu_init(void)
{
	/* Advanced SIMD / 浮動小数点を EL1 で使えるようにする。
	 *
	 * CPACR_EL1.FPEN の既定値は「トラップする」で、その状態で FP/SIMD の
	 * 命令を実行すると同期例外 (EC 0x07) になる。
	 *
	 * **カーネルは明示的に浮動小数点を使わないが、これが必要になる。**
	 * コンテキスト切替 (context.S) が呼び出され側保存である d8-d15 を
	 * 退避するからで、clang が構造体のコピーに SIMD を使う可能性に
	 * 備えてのものである。
	 *
	 * UEFI 経由ではファームウェアが既に許可しているので気づかない。
	 * QEMU の -kernel 直接起動では既定値のままなので、ここを通さないと
	 * 最初のタスク切替で落ちる。**2 つの起動経路を両方試す価値がここにある。** */
	__asm__ volatile(
	    "msr cpacr_el1, %0\n"
	    "isb\n"
	    :
	    : "r"(CPACR_FPEN_NOTRAP)
	    : "memory");

	/* PAN (Privileged Access Never) を立てないようにする。
	 *
	 * FEAT_PAN がある CPU では、EL0 からの例外で PSTATE.PAN が 1 になり、
	 * **カーネルがユーザのメモリを読めなくなる**。システムコールの引数に
	 * ポインタを取る以上、読めなければ何もできない。
	 *
	 * もちろん「読めてしまう」ことこそが危ないので、ユーザから来た
	 * ポインタは必ず usr_ok()（syscall.c）で範囲と写像を確かめてから
	 * 触る。PAN に頼らず、明示的に検める方を選んだ。 */
	{
		UD sctlr;

		__asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
		sctlr |= SCTLR_SPAN;
		__asm__ volatile(
		    "msr sctlr_el1, %0\n"
		    "isb\n"
		    :
		    : "r"(sctlr)
		    : "memory");
	}
}

/* DMA の順序づけ (hal.h)。
 *
 * `dsb sy` にしているのは、virtqueue は普通メモリ（内部共有）に置くが
 * レジスタへの通知は Device メモリで、両者の間の順序も保証したいため。
 * `ish` だと Device アクセスとの順序は保証されない。 */
EXPORT void hal_dma_barrier(void)
{
	__asm__ volatile("dsb sy" ::: "memory");
}

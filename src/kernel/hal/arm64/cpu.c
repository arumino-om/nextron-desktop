/*
 * cpu.c — CPU の起動時設定 (arm64)
 *
 * 起動第 1 段のいちばん最初に呼ぶ。MMU より前でよい。
 */

#include <btron/hal.h>

#define CPACR_FPEN_NOTRAP (3ULL << 20)

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
}

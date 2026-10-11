/*
 * exit.c — 実行の終了 (arm64)
 *
 * ARM セミホスティングで QEMU を終了させる。テストの合否判定に使う。
 * QEMU は -semihosting 付きで起動する必要がある。
 * セミホスティングが無い実機では停止ループになる。
 */

#include <btron/hal.h>

#define SYS_EXIT 0x18
#define ADP_STOPPED_APPLICATION_EXIT 0x20026UL

EXPORT void hal_exit(W code)
{
	UD block[2] = { ADP_STOPPED_APPLICATION_EXIT, (UD)(UW)code };

	register UD x0 __asm__("x0") = SYS_EXIT;
	register UD x1 __asm__("x1") = (UD)(VP)block;
	__asm__ volatile("hlt #0xF000" : : "r"(x0), "r"(x1) : "memory");

	for (;;) {
		__asm__ volatile("wfe");
	}
}

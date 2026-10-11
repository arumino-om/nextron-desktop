/*
 * exit.c — 実行の終了 (x86_64)
 *
 * QEMU の isa-debug-exit デバイスで終了する。テストの合否判定に使う。
 * QEMU は -device isa-debug-exit,iobase=0xf4,iosize=0x04 付きで起動する。
 * 終了コードは (値 << 1) | 1 になるので、tools/run_qemu.py 側で 0 と 1 を
 * 正常として扱っている。
 *
 * 【状態】ブート経路が無いため未検証。
 */

#include <btron/hal.h>

#define DEBUG_EXIT_PORT 0xF4U

EXPORT void hal_exit(W code)
{
	__asm__ volatile("outl %0, %1" : : "a"((UW)code), "Nd"((UH)DEBUG_EXIT_PORT));

	for (;;) {
		__asm__ volatile("cli; hlt");
	}
}

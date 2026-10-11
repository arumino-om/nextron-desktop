/*
 * boot.c — arm64 の C 側入口
 *
 * Phase 1 の最初のマイルストーン。起動して、シリアルに出し、
 * 実行時にも設計上の不変条件を検査して終了する。
 */

#include <btron/hal.h>
#include <btron/machine.h>
#include <btron/test.h>

EXPORT void boot_main(UD dtb);

EXPORT void boot_main(UD dtb)
{
	hal_console_init();

	hal_console_puts("\n");
	hal_console_puts("BTRON3 準拠 OS — arm64\n");
	hal_console_puts("  DTB   : ");
	hal_console_puts(dtb != 0 ? "あり\n" : "なし\n");

	/* コンパイル時だけでなく実行時にも確かめる。
	 * 実機で型の幅やアライメントが崩れていないことの確認になる。 */
	BTRON_TEST("基本データタイプの幅", {
		BTRON_CHECK("幅", sizeof(B) == 1);
		BTRON_CHECK("幅", sizeof(H) == 2);
		BTRON_CHECK("幅", sizeof(W) == 4);
		BTRON_CHECK("幅", sizeof(D) == 8);
		BTRON_CHECK("幅", sizeof(UD) == 8);
		BTRON_CHECK("幅", sizeof(VP) == 8);
	});

	BTRON_TEST("文字コード (A3 / B1)", {
		BTRON_CHECK("TC", sizeof(TC) == 2);
		BTRON_CHECK("WTC", sizeof(WTC) == 4);
		BTRON_CHECK("pack", wtclang(towtc(TSC_SYS, (TC)0x2121)) == TSC_SYS);
		BTRON_CHECK("pack", wtcchar(towtc(TSC_SYS, (TC)0x2121)) == 0x2121);
	});

	BTRON_TEST("STIME の 64bit 化 (A5)", {
		BTRON_CHECK("幅", sizeof(STIME) == 8);
		/* 2053 年を越えても溢れないこと。1985 起点で 100 年分の秒数。 */
		STIME far_future = (STIME)100 * 365 * 24 * 60 * 60;
		BTRON_CHECK("範囲", far_future > 0);
	});

	BTRON_TEST("LINK = 56 byte (A2)", {
		BTRON_CHECK("サイズ", sizeof(LINK) == 56);
		BTRON_CHECK("f_id の幅", sizeof(((LINK *)0)->f_id) == 4);
	});

	BTRON_TEST("エンディアン (A4)", {
		UW v = 0x01020304U;
		UB *p = (UB *)&v;
		BTRON_CHECK("リトル", p[0] == 0x04);
		BTRON_CHECK("BIGENDIAN", BIGENDIAN == 0);
	});

	BTRON_TEST("ページサイズは HAL 経由 (A13 / R2)", {
		BTRON_CHECK("4KB", hal_page_size() == 4096);
		BTRON_CHECK("shift", hal_page_shift() == 12);
		BTRON_CHECK("整合", (1U << hal_page_shift()) == hal_page_size());
	});

	BTRON_TEST("プラットフォーム ID (H2)", {
		BTRON_CHECK("CPU", CPU_ID == CPU_ARM64);
		BTRON_CHECK("機種", MACHINE_ID == MACH_ARM64);
		BTRON_CHECK("仕様", BTRON_SPEC == (SPEC_BTRON | 0x320));
	});

	hal_exit(btron_test_report());
}

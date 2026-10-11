/*
 * boot.c — arm64 の C 側入口
 *
 * Phase 1 の最初のマイルストーン。起動して、シリアルに出し、
 * 実行時にも設計上の不変条件を検査して終了する。
 */

#include <btron/hal.h>
#include <btron/machine.h>
#include <btron/test.h>
#include <btron/vmem.h>

EXPORT void boot_main(UD dtb);

IMPORT B __kernel_start[];
IMPORT B __kernel_end[];

/* QEMU virt の RAM は 0x4000_0000 から。-kernel で ELF を直接起動すると
 * QEMU は x0 に DTB を渡してこないため、いまは決め打ちにしている。
 * D8 で決めた UEFI 経由にすればメモリマップを正しく取得できる。 */
#define EARLY_POOL_SIZE (16UL * 1024 * 1024)

EXPORT void boot_main(UD dtb)
{
	hal_console_init();
	hal_exception_init(); /* これより後のフォルトは無言のハングではなく報告になる */

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

	/* ---- ページングの立ち上げ (§3.2 / 規律 R3・R4) ---- */
	{
		UD ps = (UD)hal_page_size();
		UD pool = ((UD)(VP)__kernel_end + ps - 1) & ~(ps - 1);

		pmem_early_init(pool, EARLY_POOL_SIZE);

		BTRON_TEST("MMU を有効にする (§3.2)", {
			BTRON_CHECK("有効化前", hal_vm_enabled() == False);
			BTRON_CHECK("hal_vm_init", hal_vm_init() == 0);
			/* ここに到達した時点で、MMU 有効のまま命令が流れている */
			BTRON_CHECK("有効化後", hal_vm_enabled() == True);
		});

		hal_console_puts("  ページテーブルに使った量: ");
		{
			UD used = pmem_early_used() / 1024;
			B buf[8];
			W i = 0;
			do {
				buf[i++] = (B)('0' + (used % 10));
				used /= 10;
			} while (used > 0 && i < 8);
			while (i > 0) {
				hal_console_putc(buf[--i]);
			}
		}
		hal_console_puts(" KB\n");
	}

	BTRON_TEST("アドレス空間レイアウト (§3.2-1 / BTRON3 §1.5)", {
		/* BTRON3 §1.5「共有・システムメモリは負、ローカルは正」 */
		BTRON_CHECK("共有は負", (D)BTRON_SHARED_BASE < 0);
		BTRON_CHECK("固有は正", (D)(BTRON_USER_LIMIT - 1) > 0);
		BTRON_CHECK("各半分 128TB", BTRON_USER_LIMIT == (1ULL << BTRON_VA_BITS));
	});

	BTRON_TEST("上位半分の区画は 512GB 境界 (§3.2-3)", {
		CONST UD g = 512ULL * 1024 * 1024 * 1024;
		BTRON_CHECK("directmap", (BTRON_DIRECTMAP_BASE % g) == 0);
		BTRON_CHECK("common", (BTRON_COMMON_BASE % g) == 0);
		BTRON_CHECK("system", (BTRON_SYSTEM_BASE % g) == 0);
		BTRON_CHECK("maprec", (BTRON_MAPREC_BASE % g) == 0);
		BTRON_CHECK("mmio", (BTRON_MMIO_BASE % g) == 0);
		BTRON_CHECK("kernel", (BTRON_KERNEL_BASE % g) == 0);
	});

	BTRON_TEST("直接マップが引ける", {
		CONST AddrSpace *ks = hal_vm_kernel_space();
		UD kpa = (UD)(VP)__kernel_start & ~((UD)hal_page_size() - 1);
		BTRON_CHECK("往復", hal_vm_translate(ks, BTRON_PA_TO_VA(kpa)) == kpa);
		/* 直接マップ経由で自分のコードが読めること */
		BTRON_CHECK("読める", *(volatile UW *)(VP)BTRON_PA_TO_VA(kpa) != 0);
	});

	BTRON_TEST("アドレス空間の生成と切替 (規律 R3)", {
		AddrSpace as;
		UD pa = pmem_early_alloc_page();
		CONST UD va = 0x0000400000000000ULL; /* 下位半分のどこか */

		BTRON_CHECK("ページ確保", pa != 0);
		BTRON_CHECK("生成", hal_vm_create(&as) == 0);
		BTRON_CHECK("ASID", as.asid != 0);
		BTRON_CHECK("マップ", hal_vm_map(&as, va, pa, (UD)hal_page_size(),
		                                 BTRON_PG_READ | BTRON_PG_WRITE) == 0);
		BTRON_CHECK("変換", hal_vm_translate(&as, va) == pa);
		BTRON_CHECK("未マップは 0", hal_vm_translate(&as, va + 0x10000000ULL) == 0);

		/* 【足場】いまカーネルは低位アドレスで走っているので、TTBR0 を
		 * 差し替える前に逃げ道を張る。上位半分へ移したら不要になる。 */
		BTRON_CHECK("足場", hal_vm_identity_kernel(&as) == 0);

		/* 切り替えて、新しい空間でしか見えないページに書いて読む */
		hal_vm_switch(&as);
		*(volatile UW *)(VP)va = 0xB7120000U;
		BTRON_CHECK("書いて読める", *(volatile UW *)(VP)va == 0xB7120000U);
		/* 直接マップ（上位半分）からも同じ物理ページが見える */
		BTRON_CHECK("共有側から同一視",
		            *(volatile UW *)(VP)BTRON_PA_TO_VA(pa) == 0xB7120000U);
	});

	hal_exit(btron_test_report());
}

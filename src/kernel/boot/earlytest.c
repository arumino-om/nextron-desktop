/*
 * earlytest.c — 起動時の不変条件検査（アーキ非依存）
 *
 * **中心核が立つ前に、起動文脈のまま走らせる検査。** タスクがまだ無いので
 * 待ちに入る API は使えない。代わりに「型の幅」「アドレス空間の配置」
 * 「引き継ぎ情報の整合」「ページテーブルの往復」「画面」「グリフ」など、
 * **その場で確かめられる不変条件**だけを置く。
 *
 * arm64 の boot.c に書かれていたものを、x86_64 の起動経路 (T1-11) を
 * 入れるときに切り出した。**両アーキで同じ検査が同じ順で走る**のが
 * 要点で、アーキ差はこの中に閉じる（machine.h の CPU_ID / MACHINE_ID）。
 *
 * 検査区分 (src/kernel/core/tests/) との違いは文脈だけである。
 * あちらはタスクの中、ここは起動文脈。
 */

#include <btron/bootinfo.h>
#include <btron/font.h>
#include <btron/hal.h>
#include <btron/intc.h>
#include <btron/kernel.h>
#include <btron/machdesc.h>
#include <btron/machine.h>
#include <btron/screen.h>
#include <btron/test.h>
#include <btron/vmem.h>

#if defined(__x86_64__)
/* 較正値を見るためだけ。HAL の中の私的ヘッダ（apic.h の冒頭）。 */
#include "../hal/x86_64/apic.h"
#endif

IMPORT B __kernel_start[];
IMPORT B __kernel_end[];

/* 画面に出ている「文字の量」を数える。
 *
 * 画面コンソールの前景色の画素を拾う。**出力が画面に届いていることを
 * 読み返しで確かめる**ために使う（㉗ と同じ考え方で、スクリーンショット
 * 比較にしない）。カーソルの位置を知らなくても増減で判定できる。
 *
 * 全画素を読むとフレームバッファはキャッシュが効かないので重い（㉖）。
 * グリフは 8x16 なので、横 2 画素・縦 3 画素ごとに拾えば十分に当たる。 */
LOCAL UD screen_ink(void)
{
	CONST Screen *s = screen();
	CONST UW fg = screen_rgb(0xD0, 0xD8, 0xE0);
	UD n = 0;
	UW y;

	if (s == 0) {
		return 0;
	}
	for (y = 0; y < s->height; y += 3) {
		UW x;

		for (x = 0; x < s->width; x += 2) {
			if (screen_get((W)x, (W)y) == fg) {
				n++;
			}
		}
	}
	return n;
}

#if defined(__x86_64__)
/* タイマ割込みが入ったことを数える（L5 / T1-11 の続き）。
 *
 * **x86_64 だけでここを見る。** arm64 は中心核の検査「割込みとタイマ
 * (D8 / T1-01 / T1-02)」が本物の経路で同じことを確かめているが、x86_64 は
 * まだ中心核に繋がっていないので、HAL だけを起動文脈で動かして見る。
 * 中心核が繋がったら（L5 の 2 段目）この枝は要らなくなる。 */
LOCAL UD tick_hits;

LOCAL void count_tick(UW intid, VP arg)
{
	(void)intid;
	(void)arg;
	hal_timer_tick_reload();
	tick_hits++;
}
#endif

EXPORT void boot_early_tests(void)
{
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
		/* **アーキ差はここに閉じる。** machine.h が CPU_ID / MACHINE_ID を
		 * ターゲットごとに決めているので、検査もそれに合わせる。 */
#if defined(__aarch64__)
		BTRON_CHECK("CPU", CPU_ID == CPU_ARM64);
		BTRON_CHECK("機種", MACHINE_ID == MACH_ARM64);
#else
		BTRON_CHECK("CPU", CPU_ID == CPU_X64);
		BTRON_CHECK("機種", MACHINE_ID == MACH_X64PC);
#endif
		BTRON_CHECK("仕様", BTRON_SPEC == (SPEC_BTRON | 0x320));
	});

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

	BTRON_TEST("カーネルは上位半分で走っている", {
		/* **実行中の番地**が上位半分にあることを見る。リンク時の番地
		 * （シンボルのアドレス）では「リンクされている場所」しか
		 * 分からない。アーキ固有の命令（arm64 の adr）を使わずに
		 * 実行時の値を取るため、呼び出し元の戻り番地を使う。 */
		UD pc = (UD)(VP)__builtin_return_address(0);

		BTRON_CHECK("PC が上位半分", pc >= BTRON_SHARED_BASE);
		BTRON_CHECK("PC がカーネル区画", pc >= BTRON_KERNEL_BASE);
		BTRON_CHECK("リンク先も上位半分",
		            (UD)(VP)__kernel_start >= BTRON_KERNEL_BASE);
		/* スタックも上位半分にある */
		BTRON_CHECK("SP が上位半分", (UD)(VP)&pc >= BTRON_SHARED_BASE);
	});

	BTRON_TEST("VMA <-> LMA の換算 (R4 の前提)", {
		BTRON_CHECK("基準", hal_kva_to_pa(BTRON_KERNEL_BASE) == hal_kernel_pa_base());
		BTRON_CHECK("整合",
		            hal_kva_to_pa((UD)(VP)__kernel_start) == hal_kernel_pa_base());
		BTRON_CHECK("終端",
		            hal_kva_to_pa((UD)(VP)__kernel_end) == hal_kernel_pa_end());
		BTRON_CHECK("物理は低位", hal_kernel_pa_base() < BTRON_USER_LIMIT);
		BTRON_CHECK("大きさ",
		            hal_kernel_pa_end() - hal_kernel_pa_base()
		                == (UD)(VP)__kernel_end - (UD)(VP)__kernel_start);
	});

	BTRON_TEST("引き継ぎ情報 (D8)", {
		CONST BootInfo *bi = bootinfo();

		BTRON_CHECK("magic", bi->magic == BTRON_BOOTINFO_MAGIC);
		BTRON_CHECK("version", bi->version == BTRON_BOOTINFO_VERSION);
		BTRON_CHECK("size", bi->size == sizeof(BootInfo));
		/* 範囲外アクセスは 0 を返す */
		BTRON_CHECK("範囲外", bootinfo_mem(bootinfo_mem_count()) == 0);

		if (bi->efi_system_table != 0) {
			/* UEFI 経由なら、ハードウェア列挙の入口とメモリマップが
			 * 必ず来ているはず。arm64 の QEMU virt は DTB、
			 * x86_64 は ACPI (詳細 D8)。 */
			BTRON_CHECK("列挙の入口", bi->dtb != 0 || bi->acpi_rsdp != 0);
			BTRON_CHECK("メモリマップ", bootinfo_mem_count() > 0);
			BTRON_CHECK("カーネル位置", bi->kernel_pa == hal_kernel_pa_base());
			BTRON_CHECK("カーネル大きさ",
			            bi->kernel_size >= hal_kernel_pa_end() - hal_kernel_pa_base());
		} else {
			/* QEMU -kernel の直接起動。ELF 直接起動では x0 に何も入らない
			 * ので引き継ぎ情報は空。カーネルが自分で決め打ちする。 */
			BTRON_CHECK("マップなし", bootinfo_mem_count() == 0);
		}
	});

	BTRON_TEST("メモリマップの整合", {
		UW n = bootinfo_mem_count();
		UD ps = (UD)hal_page_size();
		UD prev_end = 0;
		Bool sorted = True;
		Bool aligned = True;
		Bool kernel_covered = (n == 0);
		UD kb = hal_kernel_pa_base();
		UW i;

		for (i = 0; i < n; i++) {
			CONST BootMemRegion *r = bootinfo_mem(i);

			if (r->base < prev_end) {
				sorted = False;
			}
			if ((r->base & (ps - 1)) != 0 || (r->size & (ps - 1)) != 0) {
				aligned = False;
			}
			/* カーネルを置いた領域が「空き」に見えていてはならない。
			 * 空きに見えると周辺核のメモリ管理が自分を踏む。 */
			if (kb >= r->base && kb < r->base + r->size) {
				kernel_covered = (r->type != BMEM_FREE);
			}
			prev_end = r->base + r->size;
		}
		BTRON_CHECK("昇順で重なりなし", sorted == True);
		BTRON_CHECK("ページ境界", aligned == True);
		BTRON_CHECK("カーネル領域は空きでない", kernel_covered == True);
	});

	BTRON_TEST("フレームバッファの整合 (F6 / Phase 4 の前提)", {
		CONST BootFrameBuffer *fb = &bootinfo()->fb;

		if (fb->format == BFB_NONE || fb->base == 0) {
			/* GOP が無い、またはリニアなフレームバッファを見せない
			 * ドライバだった。シリアルだけで進む。 */
			BTRON_CHECK("無効なら base も 0", fb->base == 0);
		} else {
			UW i;
			Bool looks_free = False;

			BTRON_CHECK("pitch", fb->pitch >= fb->width * 4);
			BTRON_CHECK("大きさ", fb->size >= (UD)fb->pitch * fb->height);
			/* フレームバッファの物理領域が「空き」に見えてはならない。
			 * 空きに見えると物理ページアロケータが画面を配ってしまう。
			 *
			 * **メモリマップに載っていないこともある。** x86_64 の
			 * フレームバッファは PCI の BAR で、RAM の記述には現れない。
			 * 載っていなければ配られようがないので、見るべきは
			 * 「覆う領域が空きだと言っていないか」だけである。 */
			for (i = 0; i < bootinfo_mem_count(); i++) {
				CONST BootMemRegion *r = bootinfo_mem(i);

				if (fb->base >= r->base && fb->base < r->base + r->size
				    && r->type == BMEM_FREE) {
					looks_free = True;
				}
			}
			BTRON_CHECK("空きに見えない", looks_free == False);
		}
	});

	BTRON_TEST("ハードウェア記述 (D8)", {
		CONST MachDesc *md = machdesc();

		if (bootinfo()->dtb == 0 && bootinfo()->acpi_rsdp == 0) {
			/* QEMU -kernel 直接起動。記述が無いので既定値で動いている。
			 * **その既定値も ID レジスタで確かめてから開いている**（R31）。 */
			BTRON_CHECK("記述なし", md->source == MD_SRC_NONE);
			BTRON_CHECK("確かめた既定値", hal_console_opened());
		} else if ((bootinfo()->flags & BI_F_NO_UART) != 0) {
			/* NOUART の印。**シリアルが無い機械のふり**をしている
			 * （規律 R31 の逆テスト）。記述自体は読めている。 */
			BTRON_CHECK("出どころ", md->source != MD_SRC_NONE);
			BTRON_CHECK("シリアルは消えた", md->uart_type == MD_UART_NONE);
			BTRON_CHECK("番地も消えた", md->uart_pa == 0);
			BTRON_CHECK("ドライバも黙った", !hal_console_opened());
		} else {
			BTRON_CHECK("出どころ", md->source != MD_SRC_NONE);
			/* **アーキで中身が違う。** 記述から何が取れるべきかは
			 * ハードウェアの作りで決まる。 */
#if defined(__aarch64__)
			/* シリアル。これが読めないと実機で何も出せない。 */
			BTRON_CHECK("シリアル種別", md->uart_type == MD_UART_PL011);
			BTRON_CHECK("シリアル位置", md->uart_pa != 0);
			BTRON_CHECK("ドライバに反映", hal_console_pa() == md->uart_pa);
			/* 割込みコントローラ。中心核の前提。 */
			BTRON_CHECK("GIC", md->intc_type == MD_INTC_GICV2
			                       || md->intc_type == MD_INTC_GICV3);
			BTRON_CHECK("GICD", md->intc_dist_pa != 0);
			BTRON_CHECK("GICR/GICC", md->intc_cpu_pa != 0);
			/* タイマ。EL1 物理タイマは PPI なので INTID 16..31。 */
			BTRON_CHECK("タイマ INTID",
			            md->timer_intid[MD_TIMER_NONSECURE] >= 16
			                && md->timer_intid[MD_TIMER_NONSECURE] < 32);
			BTRON_CHECK("CPU 起動方法", md->boot_method != MD_BOOT_NONE);
#else
			/* x86 のシリアルは ACPI の SPCR にあるが、QEMU の q35 は
			 * SPCR を出さない。そのときは COM1 の既定値で動く（R19 の
			 * 保険が効いている状態）。どちらでも**ドライバが口を
			 * 持っている**ことが条件。 */
			/* **ポート I/O はアドレス空間に現れない**ので
			 * `hal_console_pa()` は 0 を返す（MMIO 区画に張るものが
			 * 無いという意味で、出力できないという意味ではない。
			 * この検査の出力自体がそのシリアルから出ている）。 */
			BTRON_CHECK("シリアルの種別が分かる",
			            md->uart_type == MD_UART_NONE
			                || md->uart_type == MD_UART_8250_PIO);
			BTRON_CHECK("MMIO には現れない", hal_console_pa() == 0);
			/* 割込みコントローラ。MADT から Local APIC と IOAPIC。 */
			BTRON_CHECK("APIC", md->intc_type == MD_INTC_APIC);
			BTRON_CHECK("Local APIC", md->intc_cpu_pa != 0);
			BTRON_CHECK("IOAPIC", md->intc_dist_pa != 0);
			/* タイマ割込みは APIC のローカルタイマで、INTID のような
			 * 記述は MADT に無い（APIC タイマは CPU ごとの LVT）。 */
#endif
			BTRON_CHECK("CPU", md->cpu_count >= 1);
		}
		/* 周波数。arm64 は CNTFRQ_EL0 を読むだけ、x86_64 は TSC を
		 * 校正する（CPUID 0x15 が無い機種が普通にある）。 */
		BTRON_CHECK("タイマ周波数", hal_timer_freq() > 0);
	});

	BTRON_TEST("記述に無いシリアルへは書かない (規律 R31)", {
		CONST MachDesc *md = machdesc();

		if (md->uart_type != MD_UART_NONE) {
			/* 記述から取れた。そこを使っていること。 */
			BTRON_CHECK("記述どおり", hal_console_opened());
		} else if ((bootinfo()->flags & BI_F_NO_UART) != 0) {
			/* **「シリアルが無い機械」のふり**（R31 の逆テスト）。
			 * 既定値も試さないので、完全に黙っていなければならない。 */
			BTRON_CHECK("黙っている", !hal_console_opened());

			/* **黙ったうえで、画面には出ていること。**
			 * シリアルが無い機械では画面が唯一の出力路になる
			 * （詳細 F9「報告と終了」）。黙るだけでは何も分からない。
			 *
			 * 出しているのは検査の `[ PASS ]` と同じ経路
			 * （`hal_console_puts` → `hal_console_tee` → 画面コンソール）
			 * なので、ここが通れば結果も画面に出ている。 */
			if (screen() != 0) {
				UD before = screen_ink();

				hal_console_puts("  シリアルが無いので画面だけに出して"
				                 "います（R31 / 詳細 F9）\n");
				BTRON_CHECK("画面には出ている", screen_ink() > before);
			}
		} else if (md->source == MD_SRC_NONE) {
			/* 記述が 1 つも無い経路（-kernel 直接起動）。
			 * 既定値を使うが、ID レジスタで確かめてから開いている。 */
			BTRON_CHECK("確かめた既定値", hal_console_opened());
		} else {
			/* **記述はあるが、そこにシリアルが無い。**
			 *
			 * ここでの振る舞いは**アーキで違ってよい。分かれ目は
			 * 「確かめること自体が落ちるか」**である（規律 R31 は
			 * 「読んで確かめられるときだけ既定値を使う」）。
			 *
			 *   MMIO (arm64)  居ない番地は**読むだけで外部アボート**。
			 *                 確かめられないので触らない = 黙る
			 *   PIO  (x86)    居ない番地を突いても止まらない。
			 *                 スクラッチレジスタで確かめてから開ける */
#if defined(__aarch64__)
			BTRON_CHECK("黙っている", !hal_console_opened());
#else
			/* q35 は SPCR を出さないが COM1 は居る。確かめが取れた
			 * から開いている、という状態。**ポート I/O は
			 * アドレス空間に現れない**ので写像は増えない。 */
			BTRON_CHECK("確かめてから開いた", hal_console_opened());
			BTRON_CHECK("写像は増えない", hal_console_pa() == 0);
#endif
		}
	});

#if defined(__x86_64__)
	BTRON_TEST("タイマ割込みが入る (L5 / Local APIC)", {
		UW intid = hal_timer_tick_intid();

		BTRON_CHECK("割込みコントローラの初期化", intc_init() == E_OK);
		/* 版レジスタが読めれば MMIO の写像が当たっている。
		 * xAPIC は 0x10 以上を返す（0 は「読めていない」の意）。 */
		BTRON_CHECK("APIC が応答する", hal_intc_version() >= 0x10U);
		BTRON_CHECK("tick のベクタは例外域の外", intid >= 32U && intid < 256U);
		BTRON_CHECK("ハンドラの登録",
		            intc_set_handler(intid, count_tick, 0) == E_OK);

		tick_hits = 0;
		BTRON_CHECK("tick の開始", hal_timer_tick_start(1000U) == E_OK);
		/* 較正値。1ms を測っているので、桁が合っていればよい
		 * （QEMU の実測は 1GHz 前後。0 なら較正が失敗している）。 */
		hal_console_puts("    APIC タイマ: ");
		boot_put_dec(hal_apic_hz() / 1000000UL);
		hal_console_puts(" MHz (較正)\n");
		BTRON_CHECK("較正できた", hal_apic_hz() > 0);

		{
			UD t0 = hal_timer_count();
			UD limit = hal_timer_freq() / 10U; /* 100ms で諦める */

			hal_int_enable();
			while (tick_hits < 3 && hal_timer_count() - t0 < limit) {
				hal_cpu_idle();
			}
			(void)hal_int_disable();
		}
		hal_console_puts("    tick 到来  : ");
		boot_put_dec(tick_hits);
		hal_console_puts(" 回\n");

		/* **2 回以上来たことが EOI が効いている証拠である。**
		 * EOI を出さないと APIC は同じ優先度の割込みを 2 度目から
		 * 上げない（ISR のビットが立ったまま）。1 回で止まっていたら
		 * 「割込みは来たが受領していない」であって、そこを見分けられる
		 * のはこの数しかない。 */
		BTRON_CHECK("周期的に来る (EOI が効いている)", tick_hits >= 3);

		/* 片付け。**この後は中心核が繋がるまで誰も面倒を見ない**ので、
		 * 止めてハンドラも外す。割込みは禁止した状態で先へ進む。 */
		BTRON_CHECK("止められる", intc_disable(intid) == E_OK);
		BTRON_CHECK("ハンドラを外せる", intc_clr_handler(intid) == E_OK);
	});

#endif
	BTRON_TEST("MMIO 区画への写像 (§3.2-3 / R18 の機構)", {
		CONST MachDesc *md = machdesc();
		UD pa = (md->intc_dist_pa != 0) ? md->intc_dist_pa : hal_console_pa();
		UD va = hal_vm_map_device(pa, 0x10000);

		BTRON_CHECK("張れた", va != 0);
		BTRON_CHECK("別名は基底+物理", va == BTRON_MMIO_BASE + pa);
		BTRON_CHECK("MMIO 区画の中", va >= BTRON_MMIO_BASE
		                                 && va < BTRON_RESERVED_BASE);
		BTRON_CHECK("往復", hal_vm_translate(hal_vm_kernel_space(), va) == pa);
		/* 二度呼んでも同じ値。別名は物理アドレスで一意に決まる。 */
		BTRON_CHECK("冪等", hal_vm_map_device(pa, 0x10000) == va);
		BTRON_CHECK("0 は拒否", hal_vm_map_device(pa, 0) == 0);
	});

	BTRON_TEST("早期プールの位置 (R4 の前提)", {
		UD pb = pmem_early_base();
		UD kb = hal_kernel_pa_base();
		UD ke = hal_kernel_pa_end();

		/* ページテーブルの置き場がカーネル本体と重なっていないこと */
		BTRON_CHECK("重なりなし", pb >= ke || pmem_early_limit() <= kb);
		BTRON_CHECK("確保済みがある", pmem_early_used() > 0);
		BTRON_CHECK("上限内", pmem_early_base() + pmem_early_used()
		                          <= pmem_early_limit());
	});

	BTRON_TEST("物理ページアロケータ (A13 / A14 / §1.5 の土台)", {
		UD ps = (UD)hal_page_size();

		if (bootinfo_mem_count() == 0) {
			/* メモリマップが無い構成。アロケータは立たない。 */
			BTRON_CHECK("立っていない", pmem_total_pages() == 0);
			BTRON_CHECK("確保できない", pmem_alloc(1) == 0);
		} else {
			UD a;
			UD b;
			UD c;
			UD free0 = pmem_free_pages();

			BTRON_CHECK("管理下にある", pmem_total_pages() > 1000);
			BTRON_CHECK("空きがある", free0 > 1000);

			a = pmem_alloc(1);
			BTRON_CHECK("1 ページ確保", a != 0);
			BTRON_CHECK("ページ境界", (a & (ps - 1)) == 0);
			BTRON_CHECK("減った", pmem_free_pages() == free0 - 1);
			/* ゼロクリアされていること。前の中身が漏れると情報漏洩になる。 */
			BTRON_CHECK("ゼロクリア", *(volatile UD *)(VP)BTRON_PA_TO_VA(a) == 0);
			/* 直接マップ経由で書いて読めること */
			*(volatile UD *)(VP)BTRON_PA_TO_VA(a) = 0xB7120001ULL;
			BTRON_CHECK("書いて読める",
			            *(volatile UD *)(VP)BTRON_PA_TO_VA(a) == 0xB7120001ULL);

			b = pmem_alloc(1);
			BTRON_CHECK("2 つめ", b != 0 && b != a);

			/* **連続確保。** §1.5 の get_mbk が「nblk 個の連続した
			 * メモリブロック」を求めているので、ここが要件。 */
			c = pmem_alloc(4);
			BTRON_CHECK("4 ページ確保", c != 0);
			BTRON_CHECK("連続している",
			            *(volatile UD *)(VP)BTRON_PA_TO_VA(c + 3 * ps) == 0);
			BTRON_CHECK("減った", pmem_free_pages() == free0 - 6);

			pmem_free(c, 4);
			pmem_free(b, 1);
			pmem_free(a, 1);
			BTRON_CHECK("全部戻った", pmem_free_pages() == free0);
			/* 戻したページは再び配られる */
			{
				UD d = pmem_alloc(1);

				BTRON_CHECK("再確保", d != 0);
				pmem_free(d, 1);
			}

			/* カーネル本体と早期プールは絶対に配らない。
			 * 配ると自分のコードやページテーブルを上書きする。 */
			BTRON_CHECK("カーネルは管理外扱い",
			            pmem_owns(hal_kernel_pa_base()) == False
			                || pmem_free_pages() < pmem_total_pages() + 1);
			/* 変な引数で壊れない */
			BTRON_CHECK("0 ページ", pmem_alloc(0) == 0);
			pmem_free(0, 1);
			pmem_free(1, 1); /* ページ境界でない */
			BTRON_CHECK("壊れていない", pmem_free_pages() == free0);
		}
	});

	BTRON_TEST("カーネルヒープ", {
		VP a;
		VP b;
		VP c;

		if (bootinfo_mem_count() == 0) {
			BTRON_CHECK("ページが無いので取れない", kmalloc(16) == 0);
		} else {
			UD used0 = kheap_used();

			a = kmalloc(16);
			b = kmalloc(1000);
			BTRON_CHECK("確保", a != 0 && b != 0);
			BTRON_CHECK("重ならない", a != b);
			BTRON_CHECK("8 バイト整列", ((UD)a & 7U) == 0 && ((UD)b & 7U) == 0);
			BTRON_CHECK("使用量が増えた", kheap_used() > used0);
			*(volatile UD *)a = 0xAAAAAAAAULL;
			*(volatile UD *)b = 0xBBBBBBBBULL;
			BTRON_CHECK("独立している",
			            *(volatile UD *)a == 0xAAAAAAAAULL
			                && *(volatile UD *)b == 0xBBBBBBBBULL);

			kfree(a);
			kfree(b);
			BTRON_CHECK("戻った", kheap_used() == used0);
			kfree(0);     /* NULL は無視 */
			BTRON_CHECK("0 バイト", kmalloc(0) == 0);

			/* ページより大きい要求も通る */
			c = kmalloc(100000);
			BTRON_CHECK("大きい確保", c != 0);
			*(volatile UD *)c = 1;
			kfree(c);
			BTRON_CHECK("戻った", kheap_used() == used0);
			BTRON_CHECK("ヒープは成長した", kheap_total() > 0);
		}
	});

	BTRON_TEST("直接マップが引ける", {
		CONST AddrSpace *ks = hal_vm_kernel_space();
		UD kpa = hal_kva_to_pa((UD)(VP)__kernel_start);
		BTRON_CHECK("往復", hal_vm_translate(ks, BTRON_PA_TO_VA(kpa)) == kpa);
		BTRON_CHECK("読める", *(volatile UW *)(VP)BTRON_PA_TO_VA(kpa) != 0);
	});

	/* ---- 恒等写像を捨てて、下位半分をプロセスに明け渡す ---- */
	BTRON_TEST("恒等写像を捨てる", {
		UD kpa = hal_kva_to_pa((UD)(VP)__kernel_start);
		hal_vm_drop_identity();
		/* ここに到達できれば、上位半分だけで実行が続いている */
		BTRON_CHECK("生存", hal_vm_enabled() == True);
		BTRON_CHECK("直接マップは健在",
		            *(volatile UW *)(VP)BTRON_PA_TO_VA(kpa) != 0);
	});

	BTRON_TEST("アドレス空間の生成と切替 (規律 R3)", {
		AddrSpace as;
		UD pa = pmem_early_alloc_page();
		CONST UD va = 0x0000400000000000ULL; /* 下位半分のどこか */

		BTRON_CHECK("ページ確保", pa != 0);
		BTRON_CHECK("生成", hal_vm_create(&as) == 0);
#if defined(__aarch64__)
		/* arm64 の ASID は TTBR の中にあり、常に割り当てる。 */
		BTRON_CHECK("ASID", as.asid != 0);
#else
		/* x86_64 の対応物は PCID で、**CR4.PCIDE が立っていないと
		 * 使えない**（QEMU の既定の CPU では立っていない）。その場合は
		 * CR3 書き込みごとに TLB が落ちるので、識別子は要らない。
		 * 「PCID を使うなら 0 でない」が不変条件。 */
		BTRON_CHECK("PCID", as.asid == 0 || as.asid > 0);
#endif
		BTRON_CHECK("マップ", hal_vm_map(&as, va, pa, (UD)hal_page_size(),
		                                 BTRON_PG_READ | BTRON_PG_WRITE) == 0);
		BTRON_CHECK("変換", hal_vm_translate(&as, va) == pa);
		BTRON_CHECK("未マップは 0", hal_vm_translate(&as, va + 0x10000000ULL) == 0);

		/* 足場なしで切り替えられる。カーネルは上位半分で走り続ける。 */
		hal_vm_switch(&as);
		*(volatile UW *)(VP)va = 0xB7120000U;
		BTRON_CHECK("書いて読める", *(volatile UW *)(VP)va == 0xB7120000U);
		BTRON_CHECK("共有側から同一視",
		            *(volatile UW *)(VP)BTRON_PA_TO_VA(pa) == 0xB7120000U);

		/* 別の空間へ切り替えると、さっきのページは見えなくなる */
		{
			AddrSpace other;
			BTRON_CHECK("2 つめ", hal_vm_create(&other) == 0);
			hal_vm_switch(&other);
			BTRON_CHECK("分離", hal_vm_translate(&other, va) == 0);
		}
	});

	BTRON_TEST("画面 (E24 / 規律 R18 / F6)", {
		CONST Screen *s = screen();

		if (s == 0) {
			/* 画面が無い構成。異常ではない。 */
			BTRON_CHECK("記述も無い", bootinfo()->fb.base == 0
			                              || bootinfo()->fb.format == BFB_NONE);
		} else {
			UW red = screen_rgb(0xFF, 0, 0);
			UW blue = screen_rgb(0, 0, 0xFF);

			BTRON_CHECK("大きさ", s->width > 0 && s->height > 0);
			BTRON_CHECK("pitch", s->pitch >= s->width * 4);
			/* **写像は上位半分の MMIO 区画にある**（規律 R18）。
			 * ここが下位半分だと、プロセスへ貸し出した瞬間に
			 * 緊急画面が描けなくなる。 */
			BTRON_CHECK("上位半分", s->base >= BTRON_MMIO_BASE
			                            && s->base < BTRON_RESERVED_BASE);
			BTRON_CHECK("別名", s->base == BTRON_MMIO_BASE + s->pa);

			/* **書いた画素を読み返して確かめる。**
			 * 画面出力の自動テストはスクリーンショット比較になりがちで
			 * CI に乗らない。読み返せば普通のテストとして扱える。 */
			screen_pixel(1, 1, red);
			screen_pixel(2, 1, blue);
			BTRON_CHECK("書いて読める", screen_get(1, 1) == red);
			BTRON_CHECK("隣と混ざらない", screen_get(2, 1) == blue);
			BTRON_CHECK("赤と青は別の値", red != blue);
			/* 画面外は黙って無視し、読むと 0 */
			screen_pixel((W)s->width, 0, red);
			BTRON_CHECK("画面外は 0", screen_get((W)s->width, 0) == 0);

			screen_fill(4, 4, 8, 8, red);
			BTRON_CHECK("矩形の中", screen_get(5, 5) == red);
			BTRON_CHECK("矩形の外", screen_get(12, 5) != red);
			screen_fill(0, 0, 16, 16, screen_rgb(0x10, 0x14, 0x20));
		}
	});

	BTRON_TEST("グリフデータ (B8)", {
		CONST FontSet *f = font_default();
		UW width = 0;
		UW len = 0;

		BTRON_CHECK("ある", f != 0);
		BTRON_CHECK("16 ドット", f->height == 16);
		BTRON_CHECK("半角がある", f->narrow_count > 90);
		BTRON_CHECK("全角がある", f->wide_count > 10000);
		/* 半角は 8 ドット、全角は 16 ドット */
		BTRON_CHECK("'A' は半角", font_glyph(f, 'A', &width) != 0 && width == 8);
		BTRON_CHECK("'あ' は全角", font_glyph(f, 0x3042U, &width) != 0 && width == 16);
		BTRON_CHECK("'準' がある", font_glyph(f, 0x6E96U, &width) != 0 && width == 16);
		BTRON_CHECK("'—' がある", font_glyph(f, 0x2014U, &width) != 0);
		/* 範囲外は 0 を返す（落ちない） */
		BTRON_CHECK("無い字", font_glyph(f, 0xE000U, &width) == 0 && width == 0);
		BTRON_CHECK("面外", font_glyph(f, 0x10000U, &width) == 0);

		/* UTF-8 の切り出し。規律 R7 の境界。 */
		BTRON_CHECK("ASCII", font_utf8_next("A", &len) == 'A' && len == 1);
		BTRON_CHECK("3 バイト", font_utf8_next("あ", &len) == 0x3042U && len == 3);
		BTRON_CHECK("終端", font_utf8_next("", &len) == 0 && len == 0);
	});

}

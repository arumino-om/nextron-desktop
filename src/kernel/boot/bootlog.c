/*
 * bootlog.c — 起動時の表示（アーキ非依存）
 *
 * 引き継ぎ情報・ハードウェア記述・起動画面の表示。**出力の形は
 * ctest の `--expect` が見ている**ので、両アーキで同じものを出すために
 * 1 か所に置く。arm64 の boot.c にあったものを、x86_64 の起動経路
 * (T1-11) を入れるときに切り出した。
 */

#include <btron/bootinfo.h>
#include <btron/font.h>
#include <btron/hal.h>
#include <btron/machdesc.h>
#include <btron/machine.h>
#include <btron/screen.h>
#include <btron/vmem.h>

EXPORT void boot_put_dec(UD v)
{
	B buf[24];
	W i = 0;

	do {
		buf[i++] = (B)('0' + (v % 10));
		v /= 10;
	} while (v > 0 && i < (W)sizeof(buf));
	while (i > 0) {
		hal_console_putc(buf[--i]);
	}
}

EXPORT void boot_put_hex(UD v)
{
	CONST B *digits = "0123456789abcdef";
	W i;

	hal_console_puts("0x");
	for (i = 60; i >= 0; i -= 4) {
		hal_console_putc(digits[(v >> (UW)i) & 0xF]);
	}
}

/* ------------------------------------------------------------------ */
/* 第 1 段: 物理アドレスで走る                                         */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* 第 2 段: 上位半分で走る                                             */
/* ------------------------------------------------------------------ */

LOCAL CONST B *mem_type_name(UW t)
{
	switch (t) {
	case BMEM_FREE:
		return "空き    ";
	case BMEM_LOADER:
		return "ローダ  ";
	case BMEM_FIRMWARE:
		return "FW 予約 ";
	default:
		return "使用不可";
	}
}

/* 起動画面。画面が立ったことを目で見て分かるようにする。
 *
 * 文字が出れば B8（グリフデータ）と UTF-8 → グリフの経路まで一度に
 * 確かめられる。色は BGRA / RGBA の差を screen_rgb() が吸収している。 */
EXPORT void boot_draw_splash(void)
{
	CONST Screen *s = screen();
	UW w = s->width;
	UW i;

	CONST UW band = screen_rgb(0x1E, 0x3A, 0x5F);

	/* 上端に帯を引き、その中に表題を置く。
	 * この 3 行は固定領域にするので、以後の出力でスクロールしない。 */
	screen_fill(0, 0, (W)w, 3 * 16 + 4, band);
	screen_text_at(1, 0, "BTRON3 準拠 OS — 画面コンソール", screen_rgb(0xF0, 0xF4, 0xFF),
	               band);
	screen_text_at(1, 1, "かな・漢字も出ます（GNU Unifont / B8）",
	               screen_rgb(0xA8, 0xC0, 0xE0), band);

	/* 色の帯。画素形式 (BGRA / RGBA) を取り違えていれば色が入れ替わるので、
	 * 目で見て分かる。赤・緑・青の順に明るくなっていれば正しい。 */
	for (i = 0; i < 8; i++) {
		UW r = ((i & 1U) != 0) ? 0xFFU : 0x20U;
		UW g = ((i & 2U) != 0) ? 0xFFU : 0x20U;
		UW b = ((i & 4U) != 0) ? 0xFFU : 0x20U;

		screen_fill((W)(i * (w / 8U)), 2 * 16 + 2, (W)(w / 8U), 14,
		            screen_rgb(r, g, b));
	}

	screen_console_reserve(3);
	screen_console_color(screen_rgb(0xD0, 0xD8, 0xE0), screen_rgb(0x10, 0x14, 0x20));
	hal_console_puts("  画面コンソール: ");
	boot_put_dec(screen_console_cols());
	hal_console_puts(" 桁 x ");
	boot_put_dec(screen_console_rows());
	hal_console_puts(" 行\n");
}

EXPORT void boot_show_machdesc(void)
{
	CONST MachDesc *md = machdesc();
	UW i;

	hal_console_puts("  ハード記述: ");
	hal_console_puts(md_source_name(md->source));
	if (md->source == MD_SRC_NONE) {
		hal_console_puts("（ハードウェアは既定値で動かしている）\n");
		return;
	}
	hal_console_puts("\n    シリアル  : ");
	hal_console_puts(md_uart_name(md->uart_type));
	hal_console_puts(" @ ");
	boot_put_hex(md->uart_pa);
	hal_console_puts("\n    割込み    : ");
	hal_console_puts(md_intc_name(md->intc_type));
	hal_console_puts(" dist ");
	boot_put_hex(md->intc_dist_pa);
	hal_console_puts(" cpu ");
	boot_put_hex(md->intc_cpu_pa);
	hal_console_puts("\n    タイマ    : INTID");
	for (i = 0; i < MD_TIMER_COUNT; i++) {
		hal_console_puts(" ");
		boot_put_dec(md->timer_intid[i]);
	}
	hal_console_puts(" (EL1 物理 = ");
	boot_put_dec(md->timer_intid[MD_TIMER_NONSECURE]);
	hal_console_puts(", ");
	boot_put_dec(hal_timer_freq() / 1000000);
	hal_console_puts("MHz)\n    CPU       : ");
	boot_put_dec(md->cpu_count);
	hal_console_puts(" 個  id");
	for (i = 0; i < md->cpu_count && i < MD_MAX_CPUS; i++) {
		hal_console_puts(" ");
		boot_put_hex(md->cpu_id[i]);
	}
	hal_console_puts("\n    CPU 起動  : ");
	hal_console_puts(md_boot_name(md->boot_method));
	/* virtio-mmio の枠。入力装置を探す先なので、0 なら触れない画面になる。 */
	hal_console_puts("\n    virtio    : ");
	boot_put_dec(md->virtio_count);
	hal_console_puts(" 枠");
	if (md->virtio_count > 0) {
		hal_console_puts(" @ ");
		boot_put_hex(md->virtio[0].pa);
		hal_console_puts(" INTID ");
		boot_put_dec(md->virtio[0].intid);
	}
	/* PCIe の ECAM 窓（詳細 F8）。**この行は DT 経路と ACPI 経路で一字一句
	 * 同じになるはずで、それが 2 つのパーサの相互検証になる**（Phase 1 ⑫ と
	 * 同じ形。ctest が両方の構成で同じ文字列を要求している）。 */
	for (i = 0; i < md->ecam_count && i < MD_MAX_ECAM; i++) {
		CONST MdEcam *e = &md->ecam[i];

		hal_console_puts("\n    PCIe ECAM : seg ");
		boot_put_dec(e->segment);
		hal_console_puts(" bus ");
		boot_put_dec(e->bus_start);
		hal_console_puts("-");
		boot_put_dec(e->bus_end);
		hal_console_puts(" @ ");
		boot_put_hex(e->pa);
		hal_console_puts(" size ");
		boot_put_hex(e->size);
	}
	if (md->ecam_count == 0) {
		hal_console_puts("\n    PCIe ECAM : なし");
	}
	hal_console_puts("\n");
}

EXPORT void boot_show_bootinfo(void)
{
	CONST BootInfo *bi = bootinfo();
	UD total = 0;
	UW i;

	hal_console_puts("  ACPI RSDP : ");
	if (bi->acpi_rsdp != 0) {
		boot_put_hex(bi->acpi_rsdp);
	} else {
		hal_console_puts("なし");
	}
	hal_console_puts("\n  DTB       : ");
	if (bi->dtb != 0) {
		boot_put_hex(bi->dtb);
	} else {
		hal_console_puts("なし");
	}
	hal_console_puts("\n  画面      : ");
	if (bi->fb.format != BFB_NONE && bi->fb.base != 0) {
		boot_put_dec(bi->fb.width);
		hal_console_puts("x");
		boot_put_dec(bi->fb.height);
		hal_console_puts(" pitch ");
		boot_put_dec(bi->fb.pitch);
		hal_console_puts(" @ ");
		boot_put_hex(bi->fb.base);
	} else {
		hal_console_puts("なし（シリアルのみ）");
	}
	hal_console_puts("\n");

	if (bootinfo_mem_count() == 0) {
		hal_console_puts("  メモリマップ: なし\n");
		return;
	}
	hal_console_puts("  メモリマップ ");
	boot_put_dec(bootinfo_mem_count());
	hal_console_puts(" 領域\n");
	for (i = 0; i < bootinfo_mem_count(); i++) {
		CONST BootMemRegion *r = bootinfo_mem(i);

		if (r->type == BMEM_FREE) {
			total += r->size;
		}
		/* 全部出すと長いので、1MB 以上の領域だけ出す */
		if (r->size < 1024 * 1024) {
			continue;
		}
		hal_console_puts("    ");
		boot_put_hex(r->base);
		hal_console_puts(" + ");
		boot_put_dec(r->size / 1024 / 1024);
		hal_console_puts("MB  ");
		hal_console_puts(mem_type_name(r->type));
		hal_console_puts("\n");
	}
	hal_console_puts("  空き合計  : ");
	boot_put_dec(total / 1024 / 1024);
	hal_console_puts("MB\n");
}


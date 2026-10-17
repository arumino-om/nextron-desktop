/*
 * machdesc.c — ハードウェア構成の読み取り（アーキ中立）
 *
 * 詳細 D8 の「ハードウェア列挙 = ACPI (x86_64) / Device Tree (arm64)」を
 * 実際にどう選ぶか。方針は**両方読み、アーキの本線を優先する**。
 *
 *   arm64   Device Tree を優先し、無ければ ACPI
 *   x86_64  ACPI を優先し、無ければ Device Tree
 *
 * 「本線を優先」としたのは詳細 D8 の方針に従うためだが、実測では
 * QEMU + ArmVirtQemu は既定で ACPI しか置かない（`virt,acpi=off` で DTB）。
 * つまり arm64 でも ACPI 経路が実際に使われる。片方しか書かないと
 * 動かない構成が出るので、両方を実装して取れた方を使う。
 *
 * 両方あった場合、優先側で埋まらなかった項目だけをもう一方で補う
 * （片方に SPCR が無い、片方に PSCI の記述が無い、といった実例がある）。
 */

#include <btron/bootinfo.h>
#include <btron/machdesc.h>

LOCAL MachDesc desc;

/* 2 つ目の記述で「まだ空いている欄」だけを埋める */
LOCAL void merge(MachDesc *dst, CONST MachDesc *src)
{
	UW i;

	if (dst->uart_type == MD_UART_NONE && src->uart_type != MD_UART_NONE) {
		dst->uart_type = src->uart_type;
		dst->uart_pa = src->uart_pa;
	}
	if (dst->intc_type == MD_INTC_NONE && src->intc_type != MD_INTC_NONE) {
		dst->intc_type = src->intc_type;
		dst->intc_dist_pa = src->intc_dist_pa;
		dst->intc_dist_size = src->intc_dist_size;
		dst->intc_cpu_pa = src->intc_cpu_pa;
		dst->intc_cpu_size = src->intc_cpu_size;
	}
	for (i = 0; i < MD_TIMER_COUNT; i++) {
		if (dst->timer_intid[i] == 0) {
			dst->timer_intid[i] = src->timer_intid[i];
		}
	}
	if (dst->boot_method == MD_BOOT_NONE) {
		dst->boot_method = src->boot_method;
	}
	if (dst->pm1_kind == MD_PM1_NONE && src->pm1_kind != MD_PM1_NONE) {
		dst->pm1_kind = src->pm1_kind;
		dst->pm1a_cnt = src->pm1a_cnt;
		dst->pm1b_cnt = src->pm1b_cnt;
	}
	if (!dst->s5_known && src->s5_known) {
		dst->s5_known = True;
		dst->slp_typ_a = src->slp_typ_a;
		dst->slp_typ_b = src->slp_typ_b;
	}
	if (dst->cpu_count == 0 && src->cpu_count != 0) {
		UW n = (src->cpu_count < MD_MAX_CPUS) ? src->cpu_count : MD_MAX_CPUS;

		for (i = 0; i < n; i++) {
			dst->cpu_id[i] = src->cpu_id[i];
		}
		dst->cpu_count = src->cpu_count;
	}
	/* ECAM 窓（詳細 F8）。**片方にしか無いことが普通にある** —
	 * arm64 の DT 経路では `pci-host-ecam-generic` が立つが、同じ機種の
	 * ACPI 経路では MCFG から来る。どちらか一方でも取れれば PCIe が使える。 */
	if (dst->ecam_count == 0 && src->ecam_count != 0) {
		UW n = (src->ecam_count < MD_MAX_ECAM) ? src->ecam_count : MD_MAX_ECAM;

		for (i = 0; i < n; i++) {
			dst->ecam[i] = src->ecam[i];
		}
		dst->ecam_count = src->ecam_count;
	}
}

LOCAL void clear(MachDesc *md)
{
	UW i;

	md->source = MD_SRC_NONE;
	md->uart_type = MD_UART_NONE;
	md->uart_pa = 0;
	md->intc_type = MD_INTC_NONE;
	md->intc_dist_pa = 0;
	md->intc_dist_size = 0;
	md->intc_cpu_pa = 0;
	md->intc_cpu_size = 0;
	for (i = 0; i < MD_TIMER_COUNT; i++) {
		md->timer_intid[i] = 0;
	}
	md->pm1_kind = MD_PM1_NONE;
	md->pm1a_cnt = 0;
	md->pm1b_cnt = 0;
	md->s5_known = False;
	md->slp_typ_a = 0;
	md->slp_typ_b = 0;
	md->boot_method = MD_BOOT_NONE;
	md->cpu_count = 0;
	for (i = 0; i < MD_MAX_CPUS; i++) {
		md->cpu_id[i] = 0;
	}
	md->ecam_count = 0;
	for (i = 0; i < MD_MAX_ECAM; i++) {
		md->ecam[i].pa = 0;
		md->ecam[i].size = 0;
		md->ecam[i].segment = 0;
		md->ecam[i].bus_start = 0;
		md->ecam[i].bus_end = 0;
	}
}

EXPORT Bool machdesc_probe(void)
{
	CONST BootInfo *bi = bootinfo();
	MachDesc other;
	Bool primary_ok = False;
	Bool other_ok = False;

	clear(&desc);
	clear(&other);

#if defined(__aarch64__)
	primary_ok = fdt_parse(bi->dtb, &desc);
	other_ok = acpi_parse(bi->acpi_rsdp, &other);
#else
	primary_ok = acpi_parse(bi->acpi_rsdp, &desc);
	other_ok = fdt_parse(bi->dtb, &other);
#endif

	if (!primary_ok && other_ok) {
		/* 本線が無かった。もう一方をそのまま使う。 */
		desc = other;
		primary_ok = True;
	} else if (primary_ok && other_ok) {
		merge(&desc, &other);
	}

	/* ESP に NOUART の印があれば、読めたシリアルを捨てる。
	 *
	 * **実機で「シリアルが無い機械」を再現する唯一の手段**（R31 の逆テスト）。
	 * QEMU の virt も q35 も記述から UART を消せないので、ここで捨てる。
	 * 捨てる場所をパーサの後にしてあるのが要点で、**パーサは普通に動いた
	 * うえで、利用側が記述に UART が無い機械として振る舞う**。
	 * 既定アドレスへ書いていれば、この構成で外部アボートになる。 */
	if ((bi->flags & BI_F_NO_UART) != 0) {
		desc.uart_type = MD_UART_NONE;
		desc.uart_pa = 0;
	}
	return primary_ok;
}

EXPORT CONST MachDesc *machdesc(void)
{
	return &desc;
}

/* ------------------------------------------------------------------ */
/* ログ用の名前                                                        */
/* ------------------------------------------------------------------ */

EXPORT CONST B *md_source_name(UW v)
{
	switch (v) {
	case MD_SRC_FDT:
		return "Device Tree";
	case MD_SRC_ACPI:
		return "ACPI";
	default:
		return "なし";
	}
}

EXPORT CONST B *md_uart_name(UW v)
{
	switch (v) {
	case MD_UART_PL011:
		return "PL011";
	case MD_UART_8250:
		return "16550 (MMIO)";
	case MD_UART_8250_PIO:
		return "16550 (ポート I/O)";
	default:
		return "なし";
	}
}

EXPORT CONST B *md_intc_name(UW v)
{
	switch (v) {
	case MD_INTC_GICV2:
		return "GICv2";
	case MD_INTC_GICV3:
		return "GICv3";
	case MD_INTC_APIC:
		return "APIC";
	default:
		return "なし";
	}
}

EXPORT CONST B *md_boot_name(UW v)
{
	switch (v) {
	case MD_BOOT_PSCI_HVC:
		return "PSCI (HVC)";
	case MD_BOOT_PSCI_SMC:
		return "PSCI (SMC)";
	default:
		return "なし";
	}
}

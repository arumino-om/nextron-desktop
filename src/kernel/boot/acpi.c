/*
 * acpi.c — ACPI テーブルのパース
 *
 * 詳細 D8: x86_64 のハードウェア列挙は ACPI。
 * ただし arm64 でも**既定では ACPI しか来ない**（実測: QEMU + ArmVirtQemu は
 * `virt,acpi=off` を付けないと DTB を置かない）。したがって arm64 でも必要。
 *
 * 経路:
 *
 *   RSDP (UEFI のコンフィグレーションテーブルから。BootInfo.acpi_rsdp)
 *     └ XSDT  テーブルへの 64bit ポインタの配列
 *         ├ APIC (MADT)  割込みコントローラと CPU
 *         ├ GTDT         arm64 の generic timer
 *         ├ SPCR         デバッグコンソールのシリアル
 *         ├ MCFG         PCIe の ECAM 窓 (詳細 F8)
 *         └ FACP (FADT)  arm64 の PSCI の有無と呼び方
 *             └ DSDT     `\_S5` の SLP_TYP だけを拾う（x86_64 の電源断。F7）
 *
 * 【規律 R7】すべてリトルエンディアン。MADT のサブテーブルは 1 バイト境界から
 * 始まり中に 8 バイト値が入るので、構造体を被せず bytes.h のバイト単位
 * アクセサで読む（未整列アクセス回避）。
 *
 * 【規律】チェックサムを必ず検証する。ファームウェアのテーブルは外部入力
 * であり、壊れたものを信じるとアドレスのゴミを MMIO として触ることになる。
 */

#include <btron/machdesc.h>

#include "bytes.h"

/* --- RSDP --- */
#define RSDP_SIG 0        /* "RSD PTR " 8 バイト */
#define RSDP_CHECKSUM 8   /* 先頭 20 バイトの総和が 0 */
#define RSDP_REVISION 15
#define RSDP_RSDT 16      /* 32bit。revision 0 のみ */
#define RSDP_LENGTH 20
#define RSDP_XSDT 24      /* 64bit。revision 2 以降 */
#define RSDP_EXT_CHECKSUM 32

/* --- 共通テーブルヘッダ (36 バイト) --- */
#define SDT_SIG 0
#define SDT_LENGTH 4
#define SDT_REVISION 8
#define SDT_CHECKSUM 9
#define SDT_HEADER_SIZE 36

/* --- MADT ("APIC") --- */
#define MADT_LAPIC_ADDR 36
#define MADT_FLAGS 40
#define MADT_FIRST 44

#define MADT_LAPIC 0x00
#define MADT_IOAPIC 0x01
#define MADT_X2APIC 0x09
#define MADT_GICC 0x0B
#define MADT_GICD 0x0C
#define MADT_GICR 0x0E

/* GICC サブテーブル内のオフセット */
#define GICC_FLAGS 12
#define GICC_PHYS_BASE 32  /* GICv2 の CPU インタフェース */
#define GICC_GICR_BASE 60
#define GICC_MPIDR 68

/* GICD サブテーブル */
#define GICD_PHYS_BASE 8
#define GICD_VERSION 20

/* GICR サブテーブル */
#define GICR_BASE 4
#define GICR_LENGTH 12

/* LAPIC / x2APIC サブテーブル */
#define LAPIC_APIC_ID 3
#define LAPIC_FLAGS 4
#define X2APIC_ID 4
#define X2APIC_FLAGS 8
#define IOAPIC_ADDR 4

#define MADT_ENABLED 0x1

/* --- GTDT --- */
#define GTDT_SECURE_EL1_GSIV 48
#define GTDT_NONSECURE_EL1_GSIV 56
#define GTDT_VIRTUAL_GSIV 64
#define GTDT_NONSECURE_EL2_GSIV 72
#define GTDT_MIN_SIZE 80

/* --- SPCR --- */
#define SPCR_INTERFACE_TYPE 36
#define SPCR_BASE_ADDR 44 /* GAS の中の Address フィールド */
#define SPCR_MIN_SIZE 52

/* SPCR / DBG2 の InterfaceType */
#define SPCR_IF_16550_FULL 0x00
#define SPCR_IF_16550_SUBSET 0x01
#define SPCR_IF_PL011 0x03
#define SPCR_IF_SBSA_32 0x0D
#define SPCR_IF_SBSA 0x0E

/* GAS の AddressSpaceId */
#define GAS_SPACE_MEM 0
#define GAS_SPACE_IO 1
#define SPCR_GAS 40

/* --- MCFG (詳細 F8) ---
 *
 * ヘッダの後に 8 バイトの予約があり、そこから 16 バイトの割り当て
 * エントリが並ぶ。**窓の大きさはどこにも無い**のでバス番号の範囲から
 * 導く（PCIe は 1 バスに 256 関数 x 4KB = 1MB）。 */
#define MCFG_FIRST 44
#define MCFG_ENTRY_SIZE 16
#define MCFG_E_BASE 0
#define MCFG_E_SEGMENT 8
#define MCFG_E_BUS_START 10
#define MCFG_E_BUS_END 11

#define ECAM_BUS_SHIFT 20 /* 1 バス = 1MB */

/* --- FADT ("FACP") --- */
#define FADT_DSDT 40          /* 32bit。ACPI 1.0 から */
#define FADT_PM1A_CNT_BLK 64  /* 32bit。ポート I/O のアドレス */
#define FADT_PM1B_CNT_BLK 68
#define FADT_PM1_CNT_LEN 89   /* 8bit。バイト数（2 のはず） */
#define FADT_ARM_BOOT_ARCH 129
#define FADT_MIN_SIZE 131
#define FADT_X_DSDT 140       /* 64bit。ACPI 2.0 以降。優先する */
#define FADT_X_PM1A_CNT 172   /* GAS 12 バイト */
#define FADT_X_PM1B_CNT 184
#define FADT_X_MIN_SIZE 196   /* X_PM1b_CNT の末尾まで */

#define ARM_BOOT_PSCI_COMPLIANT 0x1
#define ARM_BOOT_PSCI_USE_HVC 0x2

/* --- AML（`\_S5` だけを探す。解釈器ではない） --- */
#define AML_NAME_OP 0x08
#define AML_PACKAGE_OP 0x12
#define AML_ZERO_OP 0x00
#define AML_ONE_OP 0x01
#define AML_BYTE_PREFIX 0x0A
#define AML_WORD_PREFIX 0x0B

/* ------------------------------------------------------------------ */
/* 検証                                                                */
/* ------------------------------------------------------------------ */

LOCAL Bool checksum_ok(BytePtr p, UD len)
{
	UB sum = 0;
	UD i;

	for (i = 0; i < len; i++) {
		sum = (UB)(sum + p[i]);
	}
	return (sum == 0) ? True : False;
}

/* ------------------------------------------------------------------ */
/* 各テーブル                                                          */
/* ------------------------------------------------------------------ */

LOCAL void take_madt(BytePtr t, UD len, MachDesc *md)
{
	UD at = MADT_FIRST;

	while (at + 2 <= len) {
		UW type = rd_u8(t, at);
		UW sub = rd_u8(t, at + 1);

		if (sub < 2 || at + sub > len) {
			break; /* 壊れている。これ以上は信用しない */
		}
		switch (type) {
		case MADT_GICD:
			if (sub >= 24) {
				UW ver = rd_u8(t, at + GICD_VERSION);

				md->intc_dist_pa = rd_le64(t, at + GICD_PHYS_BASE);
				/* GICv3 の GICD は 64KB、GICv2 は 4KB */
				md->intc_type = (ver >= 3) ? MD_INTC_GICV3 : MD_INTC_GICV2;
				md->intc_dist_size = (ver >= 3) ? 0x10000ULL : 0x1000ULL;
			}
			break;

		case MADT_GICR:
			if (sub >= 16) {
				md->intc_cpu_pa = rd_le64(t, at + GICR_BASE);
				md->intc_cpu_size = rd_le32(t, at + GICR_LENGTH);
			}
			break;

		case MADT_GICC:
			if (sub >= 76 && (rd_le32(t, at + GICC_FLAGS) & MADT_ENABLED) != 0) {
				if (md->cpu_count < MD_MAX_CPUS) {
					md->cpu_id[md->cpu_count] = rd_le64(t, at + GICC_MPIDR);
				}
				md->cpu_count++;
				/* GICR が独立したサブテーブルで来ない機種では、
				 * GICC の中の GICRBaseAddress に入っている。 */
				if (md->intc_cpu_pa == 0) {
					md->intc_cpu_pa = rd_le64(t, at + GICC_GICR_BASE);
				}
				/* GICv2 の CPU インタフェースはこちら */
				if (md->intc_type == MD_INTC_GICV2 && md->intc_cpu_pa == 0) {
					md->intc_cpu_pa = rd_le64(t, at + GICC_PHYS_BASE);
					md->intc_cpu_size = 0x2000ULL;
				}
			}
			break;

		case MADT_LAPIC:
			if (sub >= 8 && (rd_le32(t, at + LAPIC_FLAGS) & MADT_ENABLED) != 0) {
				if (md->cpu_count < MD_MAX_CPUS) {
					md->cpu_id[md->cpu_count] = rd_u8(t, at + LAPIC_APIC_ID);
				}
				md->cpu_count++;
				md->intc_type = MD_INTC_APIC;
				md->intc_cpu_pa = rd_le32(t, MADT_LAPIC_ADDR);
				md->intc_cpu_size = 0x1000ULL;
			}
			break;

		case MADT_X2APIC:
			if (sub >= 16 && (rd_le32(t, at + X2APIC_FLAGS) & MADT_ENABLED) != 0) {
				if (md->cpu_count < MD_MAX_CPUS) {
					md->cpu_id[md->cpu_count] = rd_le32(t, at + X2APIC_ID);
				}
				md->cpu_count++;
				md->intc_type = MD_INTC_APIC;
			}
			break;

		case MADT_IOAPIC:
			if (sub >= 12 && md->intc_dist_pa == 0) {
				md->intc_dist_pa = rd_le32(t, at + IOAPIC_ADDR);
				md->intc_dist_size = 0x1000ULL;
			}
			break;

		default:
			break;
		}
		at += sub;
	}
}

LOCAL void take_gtdt(BytePtr t, UD len, MachDesc *md)
{
	if (len < GTDT_MIN_SIZE) {
		return;
	}
	/* GTDT は GSIV（= GIC の INTID）をそのまま持っている。
	 * FDT の PPI 番号と違い +16 は不要。 */
	md->timer_intid[MD_TIMER_SECURE] = rd_le32(t, GTDT_SECURE_EL1_GSIV);
	md->timer_intid[MD_TIMER_NONSECURE] = rd_le32(t, GTDT_NONSECURE_EL1_GSIV);
	md->timer_intid[MD_TIMER_VIRTUAL] = rd_le32(t, GTDT_VIRTUAL_GSIV);
	md->timer_intid[MD_TIMER_HYP] = rd_le32(t, GTDT_NONSECURE_EL2_GSIV);
}

LOCAL void take_spcr(BytePtr t, UD len, MachDesc *md)
{
	UW iface;
	UW space;
	UD addr;

	if (len < SPCR_MIN_SIZE) {
		return;
	}
	iface = rd_u8(t, SPCR_INTERFACE_TYPE);
	space = rd_u8(t, SPCR_GAS);
	addr = rd_le64(t, SPCR_BASE_ADDR);
	if (addr == 0) {
		return;
	}

	switch (iface) {
	case SPCR_IF_PL011:
	case SPCR_IF_SBSA:
	case SPCR_IF_SBSA_32:
		md->uart_type = MD_UART_PL011;
		break;
	case SPCR_IF_16550_FULL:
	case SPCR_IF_16550_SUBSET:
		md->uart_type = (space == GAS_SPACE_IO) ? MD_UART_8250_PIO : MD_UART_8250;
		break;
	default:
		return; /* 知らない種類。既定値のままにする */
	}
	md->uart_pa = addr;
}

LOCAL void take_mcfg(BytePtr t, UD len, MachDesc *md)
{
	UD at;

	for (at = MCFG_FIRST; at + MCFG_ENTRY_SIZE <= len; at += MCFG_ENTRY_SIZE) {
		UD base = rd_le64(t, at + MCFG_E_BASE);
		UW first = rd_u8(t, at + MCFG_E_BUS_START);
		UW last = rd_u8(t, at + MCFG_E_BUS_END);
		MdEcam *e;

		/* 壊れたエントリは捨てる。信じると MMIO としてゴミを叩く。 */
		if (base == 0 || last < first) {
			continue;
		}
		if (md->ecam_count >= MD_MAX_ECAM) {
			md->ecam_count++; /* 数だけ数える */
			continue;
		}
		e = &md->ecam[md->ecam_count];
		e->pa = base;
		e->size = (UD)(last - first + 1) << ECAM_BUS_SHIFT;
		e->segment = rd_le16(t, at + MCFG_E_SEGMENT);
		e->bus_start = first;
		e->bus_end = last;
		md->ecam_count++;
	}
}

/* AML の整数オブジェクト 1 個を読む。読めた長さを *adv に返す。
 *
 * `\_S5` のパッケージ要素に現れるのは小さな整数だけなので、
 * ZeroOp / OneOp / BytePrefix / WordPrefix の 4 つで足りる。 */
LOCAL Bool aml_int(BytePtr t, UD at, UD end, UW *out, UD *adv)
{
	UB op;

	if (at >= end) {
		return False;
	}
	op = rd_u8(t, at);
	switch (op) {
	case AML_ZERO_OP:
		*out = 0;
		*adv = 1;
		return True;
	case AML_ONE_OP:
		*out = 1;
		*adv = 1;
		return True;
	case AML_BYTE_PREFIX:
		if (at + 1 >= end) {
			return False;
		}
		*out = rd_u8(t, at + 1);
		*adv = 2;
		return True;
	case AML_WORD_PREFIX:
		if (at + 2 >= end) {
			return False;
		}
		*out = rd_le16(t, at + 1);
		*adv = 3;
		return True;
	default:
		return False;
	}
}

/* DSDT の中から `Name(_S5, Package{ SLP_TYPa, SLP_TYPb, ... })` を探す。
 *
 * **これは AML 解釈器ではない。** 名前空間も評価も持たず、
 *
 *   08 5F 53 35 5F 12 <PkgLength> <要素数> <要素...>
 *   ^NameOp  "_S5_"  ^PackageOp
 *
 * という 1 つのバイト並びを走査するだけである。S5 の SLP_TYP が FADT に
 * 無く DSDT にしかないせいで、**AML を 1 行も解釈せずに電源を落とす
 * には、この形を直に探す以外の手が無い**（詳細 F8 は AML 解釈器 =
 * T1-15 を実機ブートの前提から外すと決めている）。
 *
 * NameOp と PackageOp で挟まれた形まで一致を要求しているので、
 * `_S5_` という名前が他の場所（参照や文字列）に現れても拾わない。 */
EXPORT Bool acpi_s5_parse(UD dsdt_pa, UD len, UW *slp_typ_a, UW *slp_typ_b)
{
	BytePtr t = (BytePtr)(VP)dsdt_pa;
	UD at;

	if (dsdt_pa == 0 || len < SDT_HEADER_SIZE) {
		return False;
	}
	/* ヘッダの後ろが AML。末尾 8 バイトは最短の一致にも足りない。 */
	for (at = SDT_HEADER_SIZE; at + 8 <= len; at++) {
		UD p;
		UD adv;
		UW n;
		UW a;
		UW b;

		if (rd_u8(t, at) != AML_NAME_OP || !bstr_eqn(t, at + 1, "_S5_", 4)
		    || rd_u8(t, at + 5) != AML_PACKAGE_OP) {
			continue;
		}
		/* PkgLength は先頭バイトの上位 2bit が「後続バイト数」。
		 * 長さ自体は使わない（要素は直後に並ぶ）ので読み飛ばす。 */
		p = at + 6;
		p += 1 + (UD)(rd_u8(t, p) >> 6);
		if (p >= len) {
			return False;
		}
		n = rd_u8(t, p); /* 要素数。PackageOp は ByteData で持つ */
		p++;
		if (n < 2) {
			return False; /* SLP_TYPa / SLP_TYPb が揃っていない */
		}
		if (!aml_int(t, p, len, &a, &adv)) {
			return False;
		}
		p += adv;
		if (!aml_int(t, p, len, &b, &adv)) {
			return False;
		}
		*slp_typ_a = a;
		*slp_typ_b = b;
		return True;
	}
	return False;
}

LOCAL void take_fadt(BytePtr t, UD len, MachDesc *md)
{
	UH flags;
	UD dsdt;

	if (len < FADT_MIN_SIZE) {
		return;
	}

	/* --- 電源断 (F7)。PM1 制御レジスタと SLP_TYP ---
	 *
	 * 64bit の X_ 側があればそちらを使う。ACPI 2.0 以降は X_ が正で、
	 * 32bit 側は互換のために残されているだけ。 */
	if (len >= FADT_X_MIN_SIZE && rd_le64(t, FADT_X_PM1A_CNT + 4) != 0) {
		UW space = rd_u8(t, FADT_X_PM1A_CNT);

		md->pm1_kind = (space == GAS_SPACE_IO) ? MD_PM1_PIO : MD_PM1_MMIO;
		md->pm1a_cnt = rd_le64(t, FADT_X_PM1A_CNT + 4);
		md->pm1b_cnt = rd_le64(t, FADT_X_PM1B_CNT + 4);
	} else if (rd_le32(t, FADT_PM1A_CNT_BLK) != 0) {
		/* 32bit 側はポート I/O と規定されている */
		md->pm1_kind = MD_PM1_PIO;
		md->pm1a_cnt = rd_le32(t, FADT_PM1A_CNT_BLK);
		md->pm1b_cnt = rd_le32(t, FADT_PM1B_CNT_BLK);
	}

	dsdt = (len >= FADT_X_DSDT + 8) ? rd_le64(t, FADT_X_DSDT) : 0;
	if (dsdt == 0) {
		dsdt = rd_le32(t, FADT_DSDT);
	}
	if (dsdt != 0) {
		BytePtr d = (BytePtr)(VP)dsdt;
		UD dlen = rd_le32(d, SDT_LENGTH);

		/* DSDT もファームウェアの外部入力。長さとチェックサムを見る。 */
		if (dlen >= SDT_HEADER_SIZE && dlen <= 0x100000ULL
		    && bstr_eqn(d, SDT_SIG, "DSDT", 4) && checksum_ok(d, dlen)) {
			md->s5_known =
			    acpi_s5_parse(dsdt, dlen, &md->slp_typ_a, &md->slp_typ_b);
		}
	}

	/* --- CPU の起動方法（= arm64 の電源断の呼び口） --- */
	flags = rd_le16(t, FADT_ARM_BOOT_ARCH);
	if ((flags & ARM_BOOT_PSCI_COMPLIANT) == 0) {
		return;
	}
	md->boot_method =
	    ((flags & ARM_BOOT_PSCI_USE_HVC) != 0) ? MD_BOOT_PSCI_HVC : MD_BOOT_PSCI_SMC;
}

/* ------------------------------------------------------------------ */
/* 入口                                                                */
/* ------------------------------------------------------------------ */

LOCAL void take_table(UD pa, MachDesc *md)
{
	BytePtr t = (BytePtr)(VP)pa;
	UD len;

	if (pa == 0) {
		return;
	}
	len = rd_le32(t, SDT_LENGTH);
	if (len < SDT_HEADER_SIZE || len > 0x100000ULL) {
		return; /* 1MB を超えるテーブルは想定しない */
	}
	if (!checksum_ok(t, len)) {
		return;
	}

	if (bstr_eqn(t, SDT_SIG, "APIC", 4)) {
		take_madt(t, len, md);
	} else if (bstr_eqn(t, SDT_SIG, "GTDT", 4)) {
		take_gtdt(t, len, md);
	} else if (bstr_eqn(t, SDT_SIG, "SPCR", 4)) {
		take_spcr(t, len, md);
	} else if (bstr_eqn(t, SDT_SIG, "MCFG", 4)) {
		take_mcfg(t, len, md);
	} else if (bstr_eqn(t, SDT_SIG, "FACP", 4)) {
		take_fadt(t, len, md);
	}
}

EXPORT Bool acpi_parse(UD rsdp_pa, MachDesc *md)
{
	BytePtr r = (BytePtr)(VP)rsdp_pa;
	UW rev;
	UD root;
	UD entry_size;
	UD len;
	UD n;
	UD i;
	BytePtr root_t;

	if (rsdp_pa == 0) {
		return False;
	}
	if (!bstr_eqn(r, RSDP_SIG, "RSD PTR ", 8)) {
		return False;
	}
	/* ACPI 1.0 部分（先頭 20 バイト）のチェックサム */
	if (!checksum_ok(r, 20)) {
		return False;
	}

	rev = rd_u8(r, RSDP_REVISION);
	if (rev >= 2) {
		UD total = rd_le32(r, RSDP_LENGTH);

		if (total < 36 || !checksum_ok(r, total)) {
			return False;
		}
		root = rd_le64(r, RSDP_XSDT);
		entry_size = 8;
	} else {
		root = rd_le32(r, RSDP_RSDT);
		entry_size = 4;
	}
	if (root == 0) {
		return False;
	}

	root_t = (BytePtr)(VP)root;
	len = rd_le32(root_t, SDT_LENGTH);
	if (len < SDT_HEADER_SIZE || len > 0x100000ULL || !checksum_ok(root_t, len)) {
		return False;
	}
	n = (len - SDT_HEADER_SIZE) / entry_size;

	/* FADT / GTDT / SPCR は MADT とは独立なので、順序は問わない。
	 * ただし MADT の中は GICD → GICC の順に依存する部分があるため、
	 * サブテーブルの処理順はテーブル内の並び（= ファームウェアの出力順）に従う。 */
	for (i = 0; i < n; i++) {
		UD at = SDT_HEADER_SIZE + i * entry_size;
		UD pa = (entry_size == 8) ? rd_le64(root_t, at) : (UD)rd_le32(root_t, at);

		take_table(pa, md);
	}

	md->source = MD_SRC_ACPI;
	return True;
}

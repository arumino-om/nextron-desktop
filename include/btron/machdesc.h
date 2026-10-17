/*
 * machdesc.h — ファームウェアから読み取ったハードウェア構成
 *
 * docs/design-issues.md 詳細 D8「本OS での方針」:
 *
 *   ハードウェア列挙  PC/AT 固定マップ (BV) → **ACPI (x86_64) / Device Tree (arm64)**
 *
 * BV は `hwresmgr` が PC/AT の固定 I/O マップを無条件登録していた。本OS は
 * ファームウェアの記述を読む。どちらの経路で来るかは機種とファームウェアの
 * 設定次第で、実測では QEMU + ArmVirtQemu は既定で ACPI、`virt,acpi=off` で
 * Device Tree を渡してくる。**両方に対応し、取れた方を使う。**
 *
 * 【規律 R2 と同じ考え方】ハードウェアのアドレスを定数で直書きしない。
 * ここに集めた値を参照する。唯一の例外は「この構造体を埋める前に使う
 * 起動初期コンソール」で、それはアーキごとの既定値から始めて、
 * 読み取れたら差し替える。
 *
 * 【規律 R7】FDT はビッグエンディアン、ACPI はリトルエンディアン。
 * 変換はパーサの中だけで行い、この構造体に入った後は内部表現
 * （リトルエンディアン, A4）として扱う。
 */

#ifndef __BTRON_MACHDESC_H__
#define __BTRON_MACHDESC_H__

#include <btron/typedef.h>

/* 記述の出どころ */
#define MD_SRC_NONE 0
#define MD_SRC_FDT 1  /* Device Tree */
#define MD_SRC_ACPI 2 /* ACPI */

/* シリアル */
#define MD_UART_NONE 0
#define MD_UART_PL011 1 /* ARM PrimeCell PL011 / SBSA Generic UART */
#define MD_UART_8250 2  /* 16550 互換 (MMIO) */
#define MD_UART_8250_PIO 3 /* 16550 互換 (x86 のポート I/O) */

/* 割込みコントローラ */
#define MD_INTC_NONE 0
#define MD_INTC_GICV2 1
#define MD_INTC_GICV3 2
#define MD_INTC_APIC 3

/* CPU の起動方法。**電源を落とす方法でもある** — PSCI は CPU_ON と
 * SYSTEM_OFF を同じ呼び口で提供するので、arm64 の F7（電源管理）の
 * 終了経路はこの欄がそのまま答えになる。 */
#define MD_BOOT_NONE 0
#define MD_BOOT_PSCI_HVC 1 /* PSCI を HVC 命令で呼ぶ */
#define MD_BOOT_PSCI_SMC 2 /* PSCI を SMC 命令で呼ぶ */

/* ACPI PM1 制御レジスタの在り方（x86_64 の電源断。F7） */
#define MD_PM1_NONE 0
#define MD_PM1_PIO 1  /* ポート I/O（PC の既定） */
#define MD_PM1_MMIO 2 /* メモリ写像 */

/* タイマの割込み。添字はこの順で固定する。 */
#define MD_TIMER_SECURE 0
#define MD_TIMER_NONSECURE 1 /* EL1 物理タイマ。これを使う */
#define MD_TIMER_VIRTUAL 2
#define MD_TIMER_HYP 3
#define MD_TIMER_COUNT 4

/* D9（各種上限値）が決まるまでの暫定値。溢れたら切り捨てて数だけ数える。 */
#define MD_MAX_CPUS 64

/* virtio-mmio の枠。QEMU の virt 機種は 32 枠を常に用意し、
 * 何も挿していない枠は DeviceID 0 を返す（枠の存在と中身は別）。 */
#define MD_MAX_VIRTIO 32

/* PCIe の ECAM 窓。1 窓 = 1 ホストブリッジ = 1 PCI セグメント。
 * QEMU は 1 つしか置かないが、実機では複数ある（詳細 F8）。
 * MD_MAX_CPUS と同じく D9 が決まるまでの暫定値で、溢れたら切り捨てて
 * 数だけ数える。 */
#define MD_MAX_ECAM 4

/* MMIO 1 枠の記述。アドレスと割込みの対。 */
typedef struct MdSlot {
	UD pa;
	UD size;
	UW intid; /* GIC の INTID。0 なら割込みが分からなかった */
} MdSlot;

/* ECAM 窓 1 つの記述（詳細 F8）。
 *
 * **大きさの出どころが 2 つの記述で違う。** DT の `reg` は窓の大きさを
 * 直接持っているが、ACPI の MCFG は持っておらず、バス番号の範囲から
 * 導くしかない（1 バス = 1MB）。同じ機種なら両者が一致するはずで、
 * それが Phase 1 ⑫ と同じ相互検証になる。
 *
 * `pa` は **`bus_start` のバス**の先頭である。bus 番号 b の構成空間は
 * `pa + ((b - bus_start) << 20)` から 1MB 分。 */
typedef struct MdEcam {
	UD pa;
	UD size;
	UW segment;   /* PCI セグメント（DT の linux,pci-domain / MCFG の PciSegment）*/
	UW bus_start;
	UW bus_end;
} MdEcam;

typedef struct MachDesc {
	UW source; /* MD_SRC_*。NONE なら何も読めなかった */

	/* --- シリアル（デバッグコンソール。詳細 D8 より Phase 1 から必要）--- */
	UW uart_type; /* MD_UART_* */
	UD uart_pa;   /* MMIO の物理アドレス。PIO なら I/O ポート番号 */

	/* --- 割込みコントローラ --- */
	UW intc_type;      /* MD_INTC_* */
	UD intc_dist_pa;   /* GICD / IOAPIC */
	UD intc_dist_size;
	UD intc_cpu_pa;    /* GICv3: GICR の先頭 / GICv2: GICC / APIC: LAPIC */
	UD intc_cpu_size;

	/* --- タイマ ---
	 * 値は GIC の割込み ID (INTID)。FDT の PPI 番号は +16 して格納する。
	 * 周波数は CNTFRQ_EL0 から読むのでここには入れない。 */
	UW timer_intid[MD_TIMER_COUNT];

	/* --- 電源断 (F7 / 詳細 F9) ---
	 *
	 * arm64 は上の boot_method（PSCI）がそのまま終了経路になるので、
	 * ここは x86_64 の ACPI PM1 制御レジスタ専用である。
	 *
	 * **SLP_TYP は FADT に無い。** ACPI 規格は S5 の SLP_TYP 値を
	 * DSDT の `\_S5` パッケージに置いており、機種によって違う。そのため
	 * 値を決め打ちできず、`\_S5` を読まないと電源が落ちない
	 * （T1-15 の AML 解釈器までは要らない。詳細 F8 の「AML を読まずに
	 * 実機を起動する」方針はそのまま保てる）。 */
	UW pm1_kind;  /* MD_PM1_* */
	UD pm1a_cnt;  /* PM1a_CNT。pm1_kind が NONE なら無効 */
	UD pm1b_cnt;  /* PM1b_CNT。0 なら片側だけ */
	Bool s5_known; /* \_S5 が読めた */
	UW slp_typ_a;
	UW slp_typ_b;

	/* --- CPU (D3: SMP) --- */
	UW boot_method; /* MD_BOOT_* */
	UW cpu_count;   /* 記述に現れた CPU の総数。MD_MAX_CPUS を超えても数える */
	UD cpu_id[MD_MAX_CPUS]; /* arm64: MPIDR / x86_64: APIC ID */

	/* --- virtio-mmio の枠 (規律 R19: アドレスも割込みも記述から取る) ---
	 *
	 * ここに入るのは**枠**であって装置ではない。どの枠に何が挿さって
	 * いるかはレジスタの DeviceID を読むまで分からないので、
	 * 中身の判定はドライバ側 (virtio_mmio_probe) の仕事。 */
	UW virtio_count;
	MdSlot virtio[MD_MAX_VIRTIO];

	/* --- PCIe の ECAM 窓 (詳細 F8 / 規律 R19) ---
	 *
	 * virtio の枠と同じで、ここに入るのは**窓**であって装置ではない。
	 * 窓の中に何が挿さっているかは構成空間を読むまで分からないので、
	 * 列挙は `src/drivers/pci` の仕事（詳細 F8「この層が持たない知識」）。
	 *
	 * 窓が無い機種もある（QEMU の `-M virt` を ACPI で動かすと MCFG は
	 * 来るが、DT 経路しか無い機種では窓が 0 個になる）。
	 * **0 個と「読めていない」は区別できない**ので、使う側は
	 * 「窓が無ければ PCIe 装置も無い」として扱う。 */
	UW ecam_count;
	MdEcam ecam[MD_MAX_ECAM];
} MachDesc;

/* ------------------------------------------------------------------ */
/* 読み取り                                                            */
/*                                                                     */
/* 起動第 1 段（MMU 無効・物理アドレス）で 1 度だけ呼ぶ。                */
/* ファームウェアのテーブルはカーネルのページテーブルに写像されないので、 */
/* bootinfo_take() と同じ理由で、この時点で読んで複製しておく。          */
/* ------------------------------------------------------------------ */

IMPORT Bool machdesc_probe(void);
IMPORT CONST MachDesc *machdesc(void); /* 常に非 0。読めなければ source=NONE */

/* 人が読める名前。ログ用。 */
IMPORT CONST B *md_source_name(UW v);
IMPORT CONST B *md_uart_name(UW v);
IMPORT CONST B *md_intc_name(UW v);
IMPORT CONST B *md_boot_name(UW v);

/* --- パーサ本体。machdesc_probe() から呼ぶ。単体でも試せるよう公開する ---
 * どちらも「物理アドレスに置かれたテーブル」を読み、md を埋める。
 * 読めた項目だけを上書きし、読めなければ md を触らない。 */
IMPORT Bool fdt_parse(UD dtb_pa, MachDesc *md);
IMPORT Bool acpi_parse(UD rsdp_pa, MachDesc *md);

/* DSDT（AML）の中から `\_S5` の SLP_TYP だけを取り出す。
 *
 * **AML 解釈器ではない。** `Name(_S5, Package{...})` という 1 つの形を
 * バイト列として探すだけで、評価も名前空間も持たない。T1-15（AML）を
 * 実機ブートの前提にしないための最小限（詳細 F8）。
 *
 * 実機でしか通らない経路なので、合成した AML を食わせて検査している
 * （x86_64 のブート経路が無いあいだ、ここだけは単体で確かめられる）。 */
IMPORT Bool acpi_s5_parse(UD dsdt_pa, UD len, UW *slp_typ_a, UW *slp_typ_b);

#endif /* __BTRON_MACHDESC_H__ */

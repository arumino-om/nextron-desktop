/*
 * ecam.c — PCIe 構成空間 (ECAM) のアクセスと装置の列挙
 *
 * 方針と根拠は include/btron/pci.h の冒頭（詳細 F8）。
 *
 * 【ECAM のアドレス計算】
 *
 *   構成空間の番地 = 窓の先頭 + ((bus - bus_start) << 20)
 *                            + (dev  << 15)
 *                            + (func << 12)
 *                            + オフセット
 *
 * つまり 1 関数が 4KB、1 装置が 8 関数 = 32KB、1 バスが 32 装置 = 1MB。
 * **窓の先頭は `bus_start` のバスに対応する**（MCFG の StartBusNumber /
 * DT の `bus-range` の下限）。QEMU はどちらも 0 から始めるので差が出ない
 * が、引かないと分割された窓を持つ実機で丸ごとずれる。
 *
 * 【写すのはバス単位】
 *
 * 窓は 256MB（256 バス分）あるが、全部張るとページテーブルだけで 512KB
 * 要る。実際に触るバスは 1〜数本なので、**触るときに 1MB ずつ張る**。
 * `hal_vm_map_device()` の別名は物理アドレスで一意に決まるので、同じバスを
 * 二度要求しても同じ番地が返り、張り直しも起きない。
 */

#include <btron/pci.h>

#include <btron/hal.h>
#include <btron/machdesc.h>
#include <btron/vmem.h>

#define ECAM_BUS_SHIFT 20
#define ECAM_DEV_SHIFT 15
#define ECAM_FUNC_SHIFT 12

#define PCI_MAX_BUS 256
#define PCI_MAX_DEV 32
#define PCI_MAX_FUNC 8

/* ブリッジを辿る深さの上限。構成空間はファームウェア（外部入力）が
 * 作ったものなので、secondary bus が自分を指す壊れた機械でも止まるように
 * 打ち切る。ACPI のチェックサムを検証するのと同じ姿勢（Phase 1 ⑭）。 */
#define PCI_MAX_DEPTH 8

LOCAL Bool ready;

/* ------------------------------------------------------------------ */
/* 窓                                                                  */
/* ------------------------------------------------------------------ */

/* seg が属する窓を探す。bus が範囲内のものを選ぶ。 */
LOCAL CONST MdEcam *window(UW seg, UW bus)
{
	CONST MachDesc *md = machdesc();
	UW i;

	for (i = 0; i < md->ecam_count && i < MD_MAX_ECAM; i++) {
		CONST MdEcam *e = &md->ecam[i];

		if (e->pa == 0 || e->segment != seg) {
			continue;
		}
		if (bus >= e->bus_start && bus <= e->bus_end) {
			return e;
		}
	}
	return 0;
}

/* バス 1 本 (1MB) を写し、その先頭の仮想アドレスを返す。失敗時 0。 */
LOCAL UD map_bus(CONST MdEcam *e, UW bus)
{
	UD off = (UD)(bus - e->bus_start) << ECAM_BUS_SHIFT;
	UD span = (UD)1 << ECAM_BUS_SHIFT;

	if (off + span > e->size) {
		return 0;
	}
	return hal_vm_map_device(e->pa + off, span);
}

/* seg:bus:dev:func の構成空間の仮想アドレス。失敗時 0。 */
LOCAL UD cfg_of(UW seg, UW bus, UW dev, UW func)
{
	CONST MdEcam *e = window(seg, bus);
	UD base;

	if (e == 0 || dev >= PCI_MAX_DEV || func >= PCI_MAX_FUNC) {
		return 0;
	}
	base = map_bus(e, bus);
	if (base == 0) {
		return 0;
	}
	return base + ((UD)dev << ECAM_DEV_SHIFT) + ((UD)func << ECAM_FUNC_SHIFT);
}

EXPORT ERR pci_init(void)
{
	CONST MachDesc *md = machdesc();

	if (md->ecam_count == 0) {
		return ER_NOEXS;
	}
	/* バス 0 が写せるかで「使える」を判断する。列挙はここから始まる。 */
	if (cfg_of(md->ecam[0].segment, md->ecam[0].bus_start, 0, 0) == 0) {
		return ER_NOSPC;
	}
	ready = True;
	return ER_OK;
}

/* ------------------------------------------------------------------ */
/* 構成空間アクセス                                                    */
/*                                                                     */
/* ECAM は構成空間をそのままメモリに見せるので、普通の MMIO として読む。 */
/* **自然境界に揃った 1 / 2 / 4 バイトでなければならない**（PCIe の規定）。 */
/* 揃っていない要求は「居ない」と同じ扱いにして断る。                   */
/* ------------------------------------------------------------------ */

LOCAL Bool cfg_ok(CONST PciDev *d, UW off, UW width)
{
	if (d == 0 || d->cfg == 0) {
		return False;
	}
	if (off + width > PCI_CFG_SIZE) {
		return False;
	}
	return ((off & (width - 1)) == 0) ? True : False;
}

EXPORT UW pci_cfg_rd8(CONST PciDev *d, UW off)
{
	if (!cfg_ok(d, off, 1)) {
		return 0xFFU;
	}
	return *(volatile UB *)(d->cfg + off);
}

EXPORT UW pci_cfg_rd16(CONST PciDev *d, UW off)
{
	if (!cfg_ok(d, off, 2)) {
		return 0xFFFFU;
	}
	return *(volatile UH *)(d->cfg + off);
}

EXPORT UW pci_cfg_rd32(CONST PciDev *d, UW off)
{
	if (!cfg_ok(d, off, 4)) {
		return 0xFFFFFFFFU;
	}
	return *(volatile UW *)(d->cfg + off);
}

EXPORT void pci_cfg_wr8(CONST PciDev *d, UW off, UW v)
{
	if (cfg_ok(d, off, 1)) {
		*(volatile UB *)(d->cfg + off) = (UB)v;
	}
}

EXPORT void pci_cfg_wr16(CONST PciDev *d, UW off, UW v)
{
	if (cfg_ok(d, off, 2)) {
		*(volatile UH *)(d->cfg + off) = (UH)v;
	}
}

EXPORT void pci_cfg_wr32(CONST PciDev *d, UW off, UW v)
{
	if (cfg_ok(d, off, 4)) {
		*(volatile UW *)(d->cfg + off) = v;
	}
}

/* ------------------------------------------------------------------ */
/* 1 関数の読み取り                                                    */
/* ------------------------------------------------------------------ */

EXPORT Bool pci_at(UW seg, UW bus, UW dev, UW func, PciDev *out)
{
	PciDev t;
	UW id;

	if (out == 0) {
		return False;
	}
	t.cfg = cfg_of(seg, bus, dev, func);
	if (t.cfg == 0) {
		return False;
	}
	t.seg = seg;
	t.bus = bus;
	t.dev = dev;
	t.func = func;
	/* **居ない関数は全ビット 1 を返す。** これが PCI の「居ない」の作法で、
	 * 窓が張られていても装置が無ければこうなる（枠の存在と中身は別）。 */
	id = pci_cfg_rd16(&t, PCI_VENDOR_ID);
	if (id == PCI_NO_DEVICE || id == 0) {
		return False;
	}
	t.vendor = id;
	t.device = pci_cfg_rd16(&t, PCI_DEVICE_ID);
	t.classcode = pci_cfg_rd8(&t, PCI_CLASS);
	t.subclass = pci_cfg_rd8(&t, PCI_SUBCLASS);
	t.prog_if = pci_cfg_rd8(&t, PCI_PROG_IF);
	t.header = pci_cfg_rd8(&t, PCI_HEADER_TYPE);
	*out = t;
	return True;
}

/* ------------------------------------------------------------------ */
/* 列挙                                                                */
/* ------------------------------------------------------------------ */

/* 列挙の状態。見つけた総数と、入れ先。 */
typedef struct Walk {
	PciDev *out;
	W max;
	W found; /* 見つけた総数。max を超えても数える */
	UW seg;
	/* 絞り込み。PCI_ANY なら問わない。 */
	UW want_class;
	UW want_subclass;
	UW want_progif;
} Walk;

LOCAL Bool matches(CONST Walk *w, CONST PciDev *d)
{
	if (w->want_class != PCI_ANY && d->classcode != w->want_class) {
		return False;
	}
	if (w->want_subclass != PCI_ANY && d->subclass != w->want_subclass) {
		return False;
	}
	if (w->want_progif != PCI_ANY && d->prog_if != w->want_progif) {
		return False;
	}
	return True;
}

LOCAL void scan_bus(Walk *w, UW bus, UW depth);

LOCAL void scan_func(Walk *w, UW bus, UW dev, UW func, UW depth)
{
	PciDev d;

	if (!pci_at(w->seg, bus, dev, func, &d)) {
		return;
	}
	if (matches(w, &d)) {
		if (w->out != 0 && w->found < w->max) {
			w->out[w->found] = d;
		}
		w->found++;
	}
	/* ブリッジなら下のバスへ降りる。**絞り込みに合わなくても降りる** —
	 * 探しているのはブリッジの向こうに居るかもしれない。 */
	if ((d.header & PCI_HDR_TYPE_MASK) == PCI_HDR_BRIDGE) {
		UW sec = pci_cfg_rd8(&d, PCI_SECONDARY_BUS);

		/* 自分以下のバス番号を指すブリッジは壊れている（または
		 * ファームウェアが構成していない）。辿ると無限に回る。 */
		if (sec > bus && sec < PCI_MAX_BUS) {
			scan_bus(w, sec, depth + 1);
		}
	}
}

LOCAL void scan_bus(Walk *w, UW bus, UW depth)
{
	UW dev;

	if (depth >= PCI_MAX_DEPTH) {
		return;
	}
	for (dev = 0; dev < PCI_MAX_DEV; dev++) {
		PciDev d;
		UW func;

		/* 関数 0 が居なければその装置は居ない（PCI の規定）。
		 * 全関数を舐めると窓の中を 8 倍読むことになる。 */
		if (!pci_at(w->seg, bus, dev, 0, &d)) {
			continue;
		}
		scan_func(w, bus, dev, 0, depth);
		if ((d.header & PCI_HDR_MULTIFUNC) == 0) {
			continue;
		}
		for (func = 1; func < PCI_MAX_FUNC; func++) {
			scan_func(w, bus, dev, func, depth);
		}
	}
}

LOCAL W walk_all(UW cls, UW sub, UW pif, PciDev *out, W max)
{
	CONST MachDesc *md = machdesc();
	Walk w;
	UW i;

	if (max < 0) {
		return ER_PAR;
	}
	if (!ready && pci_init() != ER_OK) {
		return 0;
	}
	w.out = out;
	w.max = max;
	w.found = 0;
	w.want_class = cls;
	w.want_subclass = sub;
	w.want_progif = pif;

	/* 窓ごと（= セグメントごと）に、その窓の先頭のバスから降りる。 */
	for (i = 0; i < md->ecam_count && i < MD_MAX_ECAM; i++) {
		CONST MdEcam *e = &md->ecam[i];

		if (e->pa == 0) {
			continue;
		}
		w.seg = e->segment;
		scan_bus(&w, e->bus_start, 0);
	}
	return w.found;
}

EXPORT W pci_enum(PciDev *out, W max)
{
	return walk_all(PCI_ANY, PCI_ANY, PCI_ANY, out, max);
}

EXPORT W pci_find_class(UW classcode, UW subclass, UW prog_if, PciDev *out,
                        W max)
{
	return walk_all(classcode, subclass, prog_if, out, max);
}

/* ------------------------------------------------------------------ */
/* BAR                                                                 */
/*                                                                     */
/* 【大きさの測り方】PCI には全ビット 1 を書いて読み返す以外の方法が      */
/* 無い。読み返した値の下位の 0 の並びが、そのアドレス空間の大きさを     */
/* 表す（書けないビットが 0 で返る）。                                  */
/*                                                                     */
/* 測っている間、BAR には 0xFFFFFFFF が入っている。そのまま応答させると  */
/* 巨大な範囲を占有するので、**先に Command の MEM / IO ビットを落として */
/* 装置の応答を止め、終わったら元に戻す**。pci.h に書いたとおり、        */
/* いま画面を描いている装置に対して呼んではならない。                   */
/* ------------------------------------------------------------------ */

#define BAR_IO 0x1U
#define BAR_TYPE_MASK 0x6U
#define BAR_TYPE_64 0x4U
#define BAR_MEM_MASK 0xFFFFFFF0U
#define BAR_IO_MASK 0xFFFFFFFCU

#define PCI_MAX_BAR 6
#define PCI_BRIDGE_BAR 2

EXPORT Bool pci_bar(CONST PciDev *d, UW idx, PciBar *out)
{
	UW nbar = PCI_MAX_BAR;
	UW off;
	UW lo;
	UW cmd;
	UW size_lo;
	UW size_hi = 0xFFFFFFFFU;
	UD pa;
	UD size;

	if (d == 0 || d->cfg == 0 || out == 0) {
		return False;
	}
	if ((d->header & PCI_HDR_TYPE_MASK) == PCI_HDR_BRIDGE) {
		nbar = PCI_BRIDGE_BAR;
	}
	if (idx >= nbar) {
		return False;
	}
	off = PCI_BAR0 + idx * 4U;
	lo = pci_cfg_rd32(d, off);
	if (lo == 0 || lo == 0xFFFFFFFFU) {
		return False; /* 使われていない BAR */
	}

	out->is_io = ((lo & BAR_IO) != 0) ? True : False;
	out->is_64 = (!out->is_io && (lo & BAR_TYPE_MASK) == BAR_TYPE_64) ? True
	                                                                  : False;
	if (out->is_io) {
		pa = lo & BAR_IO_MASK;
	} else {
		pa = lo & BAR_MEM_MASK;
	}
	if (out->is_64) {
		if (idx + 1 >= nbar) {
			return False; /* 64bit なのに上位が無い。構成が壊れている */
		}
		pa |= (UD)pci_cfg_rd32(d, off + 4U) << 32;
	}

	/* ここから測定。応答を止める。 */
	cmd = pci_cfg_rd16(d, PCI_COMMAND);
	pci_cfg_wr16(d, PCI_COMMAND, cmd & ~(UW)(PCI_CMD_MEM | PCI_CMD_IO));

	pci_cfg_wr32(d, off, 0xFFFFFFFFU);
	size_lo = pci_cfg_rd32(d, off);
	pci_cfg_wr32(d, off, lo);
	if (out->is_64) {
		UW hi = pci_cfg_rd32(d, off + 4U);

		pci_cfg_wr32(d, off + 4U, 0xFFFFFFFFU);
		size_hi = pci_cfg_rd32(d, off + 4U);
		pci_cfg_wr32(d, off + 4U, hi);
	}

	pci_cfg_wr16(d, PCI_COMMAND, cmd);

	/* 読み返した値からアドレスのビットだけを残し、反転して +1 する。 */
	if (out->is_io) {
		size = (UD)(size_lo & BAR_IO_MASK);
	} else {
		size = (UD)(size_lo & BAR_MEM_MASK);
	}
	if (out->is_64) {
		size |= (UD)size_hi << 32;
	} else {
		size |= (UD)0xFFFFFFFFU << 32; /* 上位は「書けない」扱い */
	}
	size = ~size + 1;
	if (size == 0) {
		return False;
	}
	out->pa = pa;
	out->size = size;
	return True;
}

EXPORT ERR pci_enable(CONST PciDev *d, Bool master)
{
	UW cmd;

	if (d == 0 || d->cfg == 0) {
		return ER_ADR;
	}
	cmd = pci_cfg_rd16(d, PCI_COMMAND);
	cmd |= PCI_CMD_MEM;
	if (master) {
		cmd |= PCI_CMD_MASTER;
	}
	pci_cfg_wr16(d, PCI_COMMAND, cmd);
	return ER_OK;
}

EXPORT UD pci_bar_map(CONST PciDev *d, UW idx, UD *size)
{
	PciBar b;
	UD va;

	if (!pci_bar(d, idx, &b)) {
		return 0;
	}
	/* I/O 空間の BAR は MMIO として写せない。x86 のポート I/O であり、
	 * ECAM を選んだ時点でこちらは使わないと決めている（詳細 F8）。 */
	if (b.is_io) {
		return 0;
	}
	va = hal_vm_map_device(b.pa, b.size);
	if (va == 0) {
		return 0;
	}
	if (size != 0) {
		*size = b.size;
	}
	return va;
}

/* ------------------------------------------------------------------ */
/* ケーパビリティ                                                      */
/* ------------------------------------------------------------------ */

#define CAP_ID 0
#define CAP_NEXT 1

/* ケーパビリティは構成空間の 0x40〜0xFF に鎖で並ぶ。
 * **外部入力なので鎖が輪になっていることを想定する**（Phase 1 ⑭ と
 * 同じ姿勢）。領域の大きさから上限を決めて打ち切る。 */
#define CAP_MIN 0x40U
#define CAP_MAX 0xFCU
#define CAP_STEPS 48

EXPORT UW pci_cap_find(CONST PciDev *d, UW capid)
{
	UW at;
	UW i;

	if (d == 0 || d->cfg == 0) {
		return 0;
	}
	if ((pci_cfg_rd16(d, PCI_STATUS) & PCI_STATUS_CAPLIST) == 0) {
		return 0;
	}
	at = pci_cfg_rd8(d, PCI_CAP_PTR) & ~3U;
	for (i = 0; i < CAP_STEPS; i++) {
		if (at < CAP_MIN || at > CAP_MAX) {
			return 0;
		}
		if (pci_cfg_rd8(d, at + CAP_ID) == capid) {
			return at;
		}
		at = pci_cfg_rd8(d, at + CAP_NEXT) & ~3U;
	}
	return 0;
}

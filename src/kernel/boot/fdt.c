/*
 * fdt.c — Flattened Device Tree のパース
 *
 * 詳細 D8: arm64 のハードウェア列挙は Device Tree。
 *
 * DTB の構造は 3 ブロック。
 *
 *   ヘッダ        40 バイト。すべてビッグエンディアンの 32bit (規律 R7)
 *   struct ブロック  トークン列。節点の入れ子とプロパティ
 *   strings ブロック  プロパティ名の文字列プール
 *
 * トークンは BEGIN_NODE / END_NODE / PROP / NOP / END の 5 種で、
 * **あるノードのプロパティは必ずそのノードの BEGIN_NODE の直後に連続して
 * 並ぶ**（子ノードより前）。この性質を使い、ノードのプロパティ領域が
 * 確定した時点（最初の子、または END_NODE）で 1 ノード分をまとめて処理する。
 * プロパティの出現順は規定されていないので、1 パスで「compatible を見て
 * から reg を読む」はできない。領域が確定してから名前で引く。
 *
 * ここでは汎用の FDT API は作らない。必要なのは MachDesc を埋めることだけで、
 * ドライバ向けに木を公開するのは Phase 3（F1 のユーザランドドライバ）の話。
 * そのときは DTB そのものを渡す（物理アドレスは BootInfo.dtb にある）。
 */

#include <btron/machdesc.h>

#include "bytes.h"

#define FDT_MAGIC 0xD00DFEEDU

#define FDT_BEGIN_NODE 1U
#define FDT_END_NODE 2U
#define FDT_PROP 3U
#define FDT_NOP 4U
#define FDT_END 9U

/* ヘッダ内のオフセット */
#define FDTH_MAGIC 0
#define FDTH_TOTALSIZE 4
#define FDTH_OFF_DT_STRUCT 8
#define FDTH_OFF_DT_STRINGS 12
#define FDTH_OFF_MEM_RSVMAP 16
#define FDTH_VERSION 20
#define FDTH_LAST_COMP_VERSION 24
#define FDTH_BOOT_CPUID 28
#define FDTH_SIZE_DT_STRINGS 32
#define FDTH_SIZE_DT_STRUCT 36

/* 入れ子の深さの上限。実機の DT は 5〜6 段。溢れたらそのノードを飛ばす。 */
#define FDT_MAX_DEPTH 24

/* DT 仕様の既定値（親が宣言していない場合） */
#define DEFAULT_ADDR_CELLS 2U
#define DEFAULT_SIZE_CELLS 1U

/* GIC の割込み指定の第 1 セル */
#define GIC_SPI 0U
#define GIC_PPI 1U

typedef struct Node {
	UD props;     /* プロパティ列の先頭オフセット（DTB 先頭から） */
	UD props_end; /* 終端 */
	UD name;      /* ノード名のオフセット */
	UW addr_cells; /* このノードの子に適用する #address-cells */
	UW size_cells;
	Bool done; /* 処理済みか */
} Node;

/* ------------------------------------------------------------------ */
/* プロパティの検索                                                    */
/* ------------------------------------------------------------------ */

typedef struct Fdt {
	BytePtr base;
	UD strings; /* strings ブロックのオフセット */
} Fdt;

/* ノードのプロパティ領域から name を探す。見つかれば値のオフセットを返し
 * *len に長さを入れる。無ければ 0。 */
LOCAL UD prop_find(CONST Fdt *f, CONST Node *n, CONST B *name, UW *len)
{
	UD p = n->props;

	while (p + 4 <= n->props_end) {
		UW tok = rd_be32(f->base, p);
		UW plen;
		UW noff;

		if (tok == FDT_NOP) {
			p += 4;
			continue;
		}
		if (tok != FDT_PROP) {
			break;
		}
		plen = rd_be32(f->base, p + 4);
		noff = rd_be32(f->base, p + 8);
		if (bstr_eq(f->base, f->strings + noff, name)) {
			*len = plen;
			return p + 12;
		}
		p = p + 12 + ((plen + 3U) & ~3U);
	}
	return 0;
}

/* compatible は NUL 区切りの文字列リスト。s を含むか。 */
LOCAL Bool compat_has(CONST Fdt *f, CONST Node *n, CONST B *s)
{
	UW len = 0;
	UD at = prop_find(f, n, "compatible", &len);
	UD end;

	if (at == 0) {
		return False;
	}
	end = at + len;
	while (at < end) {
		UD l = 0;

		if (bstr_eq(f->base, at, s)) {
			return True;
		}
		while (at + l < end && rd_u8(f->base, at + l) != 0) {
			l++;
		}
		at += l + 1;
	}
	return False;
}

/* cells 個の 32bit を 1 つの値として読む。3 セル以上は下位 2 セルを使う。 */
LOCAL UD read_cells(CONST Fdt *f, UD at, UW cells)
{
	if (cells == 0) {
		return 0;
	}
	if (cells == 1) {
		return (UD)rd_be32(f->base, at);
	}
	/* 上位は捨てる。64bit を超える物理アドレスは扱わない */
	return rd_be64(f->base, at + (UD)(cells - 2) * 4);
}

/* reg の i 番目の (アドレス, 大きさ) を取り出す */
LOCAL Bool reg_entry(CONST Fdt *f, CONST Node *n, UW parent_ac, UW parent_sc, UW i,
                     UD *addr, UD *size)
{
	UW len = 0;
	UD at = prop_find(f, n, "reg", &len);
	UD stride = ((UD)parent_ac + parent_sc) * 4;

	if (at == 0 || stride == 0 || (UD)(i + 1) * stride > len) {
		return False;
	}
	at += (UD)i * stride;
	*addr = read_cells(f, at, parent_ac);
	*size = read_cells(f, at + (UD)parent_ac * 4, parent_sc);
	return True;
}

/* ------------------------------------------------------------------ */
/* ノードごとの処理                                                    */
/* ------------------------------------------------------------------ */

LOCAL void take_uart(CONST Fdt *f, CONST Node *n, UW pac, UW psc, MachDesc *md)
{
	UD addr = 0;
	UD size = 0;
	UW type;

	if (compat_has(f, n, "arm,pl011") || compat_has(f, n, "arm,sbsa-uart")) {
		type = MD_UART_PL011;
	} else if (compat_has(f, n, "ns16550a") || compat_has(f, n, "ns16550") ||
	           compat_has(f, n, "snps,dw-apb-uart")) {
		type = MD_UART_8250;
	} else {
		return;
	}
	if (md->uart_type != MD_UART_NONE) {
		return; /* 最初に見つけたものを使う */
	}
	if (!reg_entry(f, n, pac, psc, 0, &addr, &size)) {
		return;
	}
	md->uart_type = type;
	md->uart_pa = addr;
}

LOCAL void take_intc(CONST Fdt *f, CONST Node *n, UW pac, UW psc, MachDesc *md)
{
	UD a0 = 0;
	UD s0 = 0;
	UD a1 = 0;
	UD s1 = 0;

	if (md->intc_type != MD_INTC_NONE) {
		return;
	}
	if (compat_has(f, n, "arm,gic-v3")) {
		/* reg[0] = GICD, reg[1] = GICR（再配布器の先頭） */
		if (!reg_entry(f, n, pac, psc, 0, &a0, &s0)) {
			return;
		}
		(void)reg_entry(f, n, pac, psc, 1, &a1, &s1);
		md->intc_type = MD_INTC_GICV3;
	} else if (compat_has(f, n, "arm,cortex-a15-gic") ||
	           compat_has(f, n, "arm,cortex-a9-gic") || compat_has(f, n, "arm,gic-400")) {
		/* reg[0] = GICD, reg[1] = GICC（CPU インタフェース） */
		if (!reg_entry(f, n, pac, psc, 0, &a0, &s0)) {
			return;
		}
		(void)reg_entry(f, n, pac, psc, 1, &a1, &s1);
		md->intc_type = MD_INTC_GICV2;
	} else {
		return;
	}
	md->intc_dist_pa = a0;
	md->intc_dist_size = s0;
	md->intc_cpu_pa = a1;
	md->intc_cpu_size = s1;
}

LOCAL void take_timer(CONST Fdt *f, CONST Node *n, MachDesc *md)
{
	UW len = 0;
	UD at;
	UW i;

	if (!compat_has(f, n, "arm,armv8-timer") && !compat_has(f, n, "arm,armv7-timer")) {
		return;
	}
	at = prop_find(f, n, "interrupts", &len);
	if (at == 0) {
		return;
	}
	/* GIC の割込み指定は 3 セル: <種別 番号 フラグ>。
	 * 並びは secure, non-secure, virtual, hypervisor の順で固定。 */
	for (i = 0; i < MD_TIMER_COUNT; i++) {
		UD e = at + (UD)i * 12;
		UW kind;
		UW num;

		if ((UD)(i + 1) * 12 > len) {
			break;
		}
		kind = rd_be32(f->base, e);
		num = rd_be32(f->base, e + 4);
		/* PPI は 16 番から、SPI は 32 番から始まる */
		md->timer_intid[i] = (kind == GIC_PPI) ? num + 16U : num + 32U;
	}
}

LOCAL void take_cpu(CONST Fdt *f, CONST Node *n, UW pac, UW psc, MachDesc *md)
{
	UW len = 0;
	UD at = prop_find(f, n, "device_type", &len);

	(void)psc;
	if (at == 0 || !bstr_eq(f->base, at, "cpu")) {
		return;
	}
	at = prop_find(f, n, "reg", &len);
	if (at == 0) {
		return;
	}
	if (md->cpu_count < MD_MAX_CPUS) {
		md->cpu_id[md->cpu_count] = read_cells(f, at, pac);
	}
	md->cpu_count++;
}

/* virtio-mmio の枠。QEMU の virt は 32 枠を常に並べるので、
 * 空いているかどうかはここでは分からない（DeviceID を読むまで）。
 * 枠の数が多いので、溢れたら切り捨てる。 */
LOCAL void take_virtio(CONST Fdt *f, CONST Node *n, UW pac, UW psc, MachDesc *md)
{
	UD addr = 0;
	UD size = 0;
	UW len = 0;
	UD at;
	MdSlot *s;

	if (!compat_has(f, n, "virtio,mmio")) {
		return;
	}
	if (!reg_entry(f, n, pac, psc, 0, &addr, &size)) {
		return;
	}
	if (md->virtio_count >= MD_MAX_VIRTIO) {
		md->virtio_count++; /* 数だけ数える */
		return;
	}
	s = &md->virtio[md->virtio_count];
	s->pa = addr;
	s->size = size;
	s->intid = 0;
	/* 割込みは 3 セル <種別 番号 フラグ>。virtio-mmio は SPI。 */
	at = prop_find(f, n, "interrupts", &len);
	if (at != 0 && len >= 12) {
		UW kind = rd_be32(f->base, at);
		UW num = rd_be32(f->base, at + 4);

		s->intid = (kind == GIC_PPI) ? num + 16U : num + 32U;
	}
	md->virtio_count++;
}

LOCAL void take_psci(CONST Fdt *f, CONST Node *n, MachDesc *md)
{
	UW len = 0;
	UD at;

	if (!compat_has(f, n, "arm,psci") && !compat_has(f, n, "arm,psci-0.2") &&
	    !compat_has(f, n, "arm,psci-1.0")) {
		return;
	}
	at = prop_find(f, n, "method", &len);
	if (at == 0) {
		return;
	}
	if (bstr_eq(f->base, at, "hvc")) {
		md->boot_method = MD_BOOT_PSCI_HVC;
	} else if (bstr_eq(f->base, at, "smc")) {
		md->boot_method = MD_BOOT_PSCI_SMC;
	}
}

LOCAL void visit(CONST Fdt *f, CONST Node *n, UW pac, UW psc, MachDesc *md)
{
	take_uart(f, n, pac, psc, md);
	take_intc(f, n, pac, psc, md);
	take_timer(f, n, md);
	take_cpu(f, n, pac, psc, md);
	take_psci(f, n, md);
	take_virtio(f, n, pac, psc, md);
}

/* ------------------------------------------------------------------ */
/* 木の走査                                                            */
/* ------------------------------------------------------------------ */

/* ノードのプロパティ領域が確定したので処理する */
LOCAL void finish(CONST Fdt *f, Node *stack, UW depth, UD end, MachDesc *md)
{
	Node *n = &stack[depth];
	UW len = 0;
	UD at;
	UW pac = DEFAULT_ADDR_CELLS;
	UW psc = DEFAULT_SIZE_CELLS;

	n->props_end = end;
	n->done = True;

	/* このノードが子に課す cells。宣言が無ければ DT 仕様の既定値。 */
	at = prop_find(f, n, "#address-cells", &len);
	n->addr_cells = (at != 0 && len >= 4) ? rd_be32(f->base, at) : DEFAULT_ADDR_CELLS;
	at = prop_find(f, n, "#size-cells", &len);
	n->size_cells = (at != 0 && len >= 4) ? rd_be32(f->base, at) : DEFAULT_SIZE_CELLS;

	/* 自分の reg を解釈するのに使うのは**親の** cells */
	if (depth > 0) {
		pac = stack[depth - 1].addr_cells;
		psc = stack[depth - 1].size_cells;
	}
	visit(f, n, pac, psc, md);
}

EXPORT Bool fdt_parse(UD dtb_pa, MachDesc *md)
{
	Fdt f;
	Node stack[FDT_MAX_DEPTH];
	UD p;
	UD end;
	UW depth = 0;
	UD total;

	if (dtb_pa == 0) {
		return False;
	}
	f.base = (BytePtr)(VP)dtb_pa;
	if (rd_be32(f.base, FDTH_MAGIC) != FDT_MAGIC) {
		return False;
	}
	total = rd_be32(f.base, FDTH_TOTALSIZE);
	if (total < 40) {
		return False;
	}
	f.strings = rd_be32(f.base, FDTH_OFF_DT_STRINGS);
	p = rd_be32(f.base, FDTH_OFF_DT_STRUCT);
	end = p + rd_be32(f.base, FDTH_SIZE_DT_STRUCT);
	if (end > total || f.strings >= total) {
		return False;
	}

	while (p + 4 <= end) {
		UW tok = rd_be32(f.base, p);

		if (tok == FDT_END) {
			break;
		}
		if (tok == FDT_NOP) {
			p += 4;
			continue;
		}
		if (tok == FDT_PROP) {
			UW plen = rd_be32(f.base, p + 4);
			p = p + 12 + ((plen + 3U) & ~3U);
			continue;
		}
		if (tok == FDT_BEGIN_NODE) {
			UD name = p + 4;
			UD l = 0;

			/* 親のプロパティ領域はここで終わり */
			if (depth > 0 && !stack[depth - 1].done) {
				finish(&f, stack, depth - 1, p, md);
			}
			while (name + l < end && rd_u8(f.base, name + l) != 0) {
				l++;
			}
			p = name + ((l + 1 + 3U) & ~3U);

			if (depth >= FDT_MAX_DEPTH) {
				return False; /* 想定外に深い。信用しない */
			}
			stack[depth].name = name;
			stack[depth].props = p;
			stack[depth].props_end = p;
			stack[depth].addr_cells = DEFAULT_ADDR_CELLS;
			stack[depth].size_cells = DEFAULT_SIZE_CELLS;
			stack[depth].done = False;
			depth++;
			continue;
		}
		if (tok == FDT_END_NODE) {
			if (depth == 0) {
				return False; /* 対応しない END_NODE。壊れている */
			}
			if (!stack[depth - 1].done) {
				finish(&f, stack, depth - 1, p, md);
			}
			depth--;
			p += 4;
			continue;
		}
		return False; /* 未知のトークン */
	}

	md->source = MD_SRC_FDT;
	return True;
}

/*
 * msi.c — MSI / MSI-X の設定
 *
 * 方針と根拠は include/btron/pci.h の冒頭（詳細 F8）。
 *
 * 【なぜ MSI が実機ブートの鍵なのか】
 *
 * レガシ割込み (INTx) は「装置のピンがどの割込み入力に繋がっているか」を
 * 引く必要があり、その表は ACPI の `_PRT` = DSDT の AML の中にある。
 * AML の解釈器 (T1-15) は大きい。
 *
 * MSI は**装置が自分で割込みを送る**方式で、送り先はソフトウェアが
 * 構成空間に書く。つまり経路の情報がファームウェアの記述に依存しない。
 * これで AML が実機ブートの前提から外れる。
 *
 * 【MSI-X を優先する理由】
 *
 * MSI はベクタ番号が連続した 1 組（しかも 2 のべき乗）しか持てず、
 * マスクも装置によっては無い。MSI-X はテーブルが BAR の中にあるので
 * ベクタごとに宛先とマスクを持てる。NVMe はキューごと、xHCI は
 * イベントリングごとに別の宛先を張りたいので、こちらが要る。
 *
 * 【有効にする順番を守る】
 *
 *   1. テーブルに宛先を書く（マスクしたまま）
 *   2. INTx を止め、バスマスタを許す
 *   3. MSI-X を有効にする
 *   4. ベクタのマスクを下げる
 *
 * 1 の前に 3 をやると、装置が「まだ何も書いていない宛先」へ割込みを
 * 送る。実機ではそれが何処でもない番地への書き込みになり、症状は
 * 無言の停止として出る（規律 R31 と同じ形）。だから `pci_msix_open()`
 * は有効化しない。
 */

#include <btron/pci.h>

#include <btron/hal.h>

/* ------------------------------------------------------------------ */
/* MSI-X ケーパビリティ (PCIe Base Spec §7.7.2)                         */
/* ------------------------------------------------------------------ */

#define MSIX_CTRL 0x02   /* 16bit */
#define MSIX_TABLE 0x04  /* 32bit: 下位 3bit が BIR、上位がオフセット */
#define MSIX_PBA 0x08    /* 32bit: 同じ形 */

#define MSIX_CTRL_SIZE_MASK 0x07FFU /* テーブル要素数 - 1 */
#define MSIX_CTRL_FUNC_MASK 0x4000U /* 全ベクタを一括マスク */
#define MSIX_CTRL_ENABLE 0x8000U

#define MSIX_BIR_MASK 0x07U
#define MSIX_OFF_MASK 0xFFFFFFF8U

/* ベクタテーブル 1 要素 = 16 バイト */
#define MSIX_ENT_SIZE 16
#define MSIX_ENT_ADDR_LO 0
#define MSIX_ENT_ADDR_HI 4
#define MSIX_ENT_DATA 8
#define MSIX_ENT_CTRL 12
#define MSIX_ENT_CTRL_MASK 0x1U

#define MSIX_MAX_VECTORS 2048

/* ------------------------------------------------------------------ */
/* MSI ケーパビリティ (PCIe Base Spec §7.7.1)                           */
/* ------------------------------------------------------------------ */

#define MSI_CTRL 0x02    /* 16bit */
#define MSI_ADDR_LO 0x04 /* 32bit */
/* 以下は 64bit アドレス対応かどうかでずれる。 */
#define MSI_ADDR_HI 0x08
#define MSI_DATA_32 0x08 /* 16bit */
#define MSI_DATA_64 0x0C /* 16bit */

#define MSI_CTRL_ENABLE 0x0001U
#define MSI_CTRL_MMC_SHIFT 1 /* 装置が要求できるベクタ数 (log2) */
#define MSI_CTRL_MMC_MASK 0x000EU
#define MSI_CTRL_MME_MASK 0x0070U /* 実際に配る数 (log2) */
#define MSI_CTRL_ADDR64 0x0080U
#define MSI_CTRL_PVM 0x0100U /* ベクタごとのマスク */

/* ------------------------------------------------------------------ */
/* テーブルへのアクセス                                                */
/* ------------------------------------------------------------------ */

LOCAL UD ent_of(CONST PciMsix *mx, UW vec)
{
	if (mx == 0 || mx->table == 0 || vec >= mx->count) {
		return 0;
	}
	return mx->table + (UD)vec * MSIX_ENT_SIZE;
}

LOCAL UW tbl_rd(UD at)
{
	return *(volatile UW *)at;
}

LOCAL void tbl_wr(UD at, UW v)
{
	*(volatile UW *)at = v;
}

/* ------------------------------------------------------------------ */
/* MSI-X                                                               */
/* ------------------------------------------------------------------ */

EXPORT ERR pci_msix_open(CONST PciDev *d, PciMsix *mx)
{
	UW cap;
	UW ctrl;
	UW tbl;
	UW pba;
	UD tbl_va;
	UD pba_va;
	UD bar_size = 0;
	UD tbl_off;
	UD pba_off;
	UW count;

	if (d == 0 || mx == 0) {
		return ER_ADR;
	}
	cap = pci_cap_find(d, PCI_CAP_MSIX);
	if (cap == 0) {
		return ER_NOSPT;
	}
	ctrl = pci_cfg_rd16(d, cap + MSIX_CTRL);
	count = (ctrl & MSIX_CTRL_SIZE_MASK) + 1U;
	if (count > MSIX_MAX_VECTORS) {
		return ER_NOSPT;
	}
	tbl = pci_cfg_rd32(d, cap + MSIX_TABLE);
	pba = pci_cfg_rd32(d, cap + MSIX_PBA);

	/* テーブルと PBA は BAR の中にある。同じ BAR に両方載っているのが
	 * 普通だが、別々でもよいので各々写す（別名は物理アドレスで一意に
	 * 決まるので、同じ BAR なら 2 度目は同じ番地が返るだけ）。
	 *
	 * 写す順番に注意 — `pci_bar_map` は大きさを測るために一度応答を
	 * 落とすので、**応答を許すのは写し終わってから**。逆にすると
	 * 測定が自分で落としたものを元に戻して終わる。 */
	tbl_va = pci_bar_map(d, tbl & MSIX_BIR_MASK, &bar_size);
	if (tbl_va == 0) {
		return ER_NOSPC;
	}
	tbl_off = tbl & MSIX_OFF_MASK;
	if (tbl_off + (UD)count * MSIX_ENT_SIZE > bar_size) {
		return ER_NOSPT; /* 構成が壊れている。触ると BAR の外を叩く */
	}
	pba_va = pci_bar_map(d, pba & MSIX_BIR_MASK, 0);
	if (pba_va == 0) {
		return ER_NOSPC;
	}
	pba_off = pba & MSIX_OFF_MASK;

	/* **ここでテーブルが読み書きできるようになる。** ファームウェアは
	 * 使い終わった装置の MEM 応答を落として渡してくるので、許すまでは
	 * 書いた値がどこにも届かず、読むと全ビット 1 が返る（pci_enable の
	 * 注記）。バスマスタはまだ許さない — 宛先も書いていない装置に
	 * DMA を許す理由が無い。 */
	if (pci_enable(d, False) != ER_OK) {
		return ER_ADR;
	}

	mx->cap = cap;
	mx->count = count;
	mx->table = tbl_va + tbl_off;
	mx->pba = pba_va + pba_off;
	return ER_OK;
}

EXPORT ERR pci_msix_set(CONST PciMsix *mx, UW vec, UD addr, UW data)
{
	UD at = ent_of(mx, vec);

	if (at == 0) {
		return ER_PAR;
	}
	/* **マスクを先に上げる。** 宛先を書き換えている途中に割込みが来ると、
	 * 下位 32bit だけ新しいという中途半端な番地へ飛ぶ。 */
	tbl_wr(at + MSIX_ENT_CTRL, MSIX_ENT_CTRL_MASK);
	tbl_wr(at + MSIX_ENT_ADDR_LO, (UW)(addr & 0xFFFFFFFFU));
	tbl_wr(at + MSIX_ENT_ADDR_HI, (UW)(addr >> 32));
	tbl_wr(at + MSIX_ENT_DATA, data);
	return ER_OK;
}

EXPORT Bool pci_msix_get(CONST PciMsix *mx, UW vec, UD *addr, UW *data)
{
	UD at = ent_of(mx, vec);

	if (at == 0) {
		return False;
	}
	if (addr != 0) {
		*addr = (UD)tbl_rd(at + MSIX_ENT_ADDR_LO) |
		        ((UD)tbl_rd(at + MSIX_ENT_ADDR_HI) << 32);
	}
	if (data != 0) {
		*data = tbl_rd(at + MSIX_ENT_DATA);
	}
	return True;
}

EXPORT ERR pci_msix_mask(CONST PciMsix *mx, UW vec, Bool on)
{
	UD at = ent_of(mx, vec);
	UW v;

	if (at == 0) {
		return ER_PAR;
	}
	v = tbl_rd(at + MSIX_ENT_CTRL);
	if (on) {
		v |= MSIX_ENT_CTRL_MASK;
	} else {
		v &= ~(UW)MSIX_ENT_CTRL_MASK;
	}
	tbl_wr(at + MSIX_ENT_CTRL, v);
	return ER_OK;
}

EXPORT ERR pci_msix_enable(CONST PciDev *d, CONST PciMsix *mx, Bool on)
{
	UW ctrl;
	UW cmd;

	if (d == 0 || mx == 0 || mx->cap == 0) {
		return ER_ADR;
	}
	cmd = pci_cfg_rd16(d, PCI_COMMAND);
	ctrl = pci_cfg_rd16(d, mx->cap + MSIX_CTRL);
	if (on) {
		/* INTx を止める。MSI-X と同時に有効だと二重に届く。
		 * バスマスタは MSI そのものがメモリ書き込みなので必要。 */
		pci_cfg_wr16(d, PCI_COMMAND,
		             (cmd | PCI_CMD_INTX_DISABLE | PCI_CMD_MASTER));
		ctrl |= MSIX_CTRL_ENABLE;
		ctrl &= ~MSIX_CTRL_FUNC_MASK;
	} else {
		ctrl &= ~MSIX_CTRL_ENABLE;
	}
	pci_cfg_wr16(d, mx->cap + MSIX_CTRL, ctrl);
	return ER_OK;
}

EXPORT Bool pci_msix_enabled(CONST PciDev *d, CONST PciMsix *mx)
{
	if (d == 0 || mx == 0 || mx->cap == 0) {
		return False;
	}
	return ((pci_cfg_rd16(d, mx->cap + MSIX_CTRL) & MSIX_CTRL_ENABLE) != 0)
	           ? True
	           : False;
}

/* ------------------------------------------------------------------ */
/* MSI（MSI-X が無い装置向け）                                         */
/* ------------------------------------------------------------------ */

EXPORT ERR pci_msi_open(CONST PciDev *d, PciMsi *ms)
{
	UW cap;
	UW ctrl;

	if (d == 0 || ms == 0) {
		return ER_ADR;
	}
	cap = pci_cap_find(d, PCI_CAP_MSI);
	if (cap == 0) {
		return ER_NOSPT;
	}
	ctrl = pci_cfg_rd16(d, cap + MSI_CTRL);
	ms->cap = cap;
	ms->count = 1U << ((ctrl & MSI_CTRL_MMC_MASK) >> MSI_CTRL_MMC_SHIFT);
	ms->is_64 = ((ctrl & MSI_CTRL_ADDR64) != 0) ? True : False;
	ms->per_vector_mask = ((ctrl & MSI_CTRL_PVM) != 0) ? True : False;
	return ER_OK;
}

EXPORT ERR pci_msi_set(CONST PciDev *d, CONST PciMsi *ms, UD addr, UW data)
{
	if (d == 0 || ms == 0 || ms->cap == 0) {
		return ER_ADR;
	}
	/* 32bit しか書けない装置に上位が立った宛先を渡すのは誤り。
	 * 黙って下位だけ書くと、装置は別の番地へ送る。 */
	if (!ms->is_64 && (addr >> 32) != 0) {
		return ER_PAR;
	}
	pci_cfg_wr32(d, ms->cap + MSI_ADDR_LO, (UW)(addr & 0xFFFFFFFFU));
	if (ms->is_64) {
		pci_cfg_wr32(d, ms->cap + MSI_ADDR_HI, (UW)(addr >> 32));
		pci_cfg_wr16(d, ms->cap + MSI_DATA_64, data);
	} else {
		pci_cfg_wr16(d, ms->cap + MSI_DATA_32, data);
	}
	return ER_OK;
}

EXPORT ERR pci_msi_enable(CONST PciDev *d, CONST PciMsi *ms, Bool on)
{
	UW ctrl;
	UW cmd;

	if (d == 0 || ms == 0 || ms->cap == 0) {
		return ER_ADR;
	}
	cmd = pci_cfg_rd16(d, PCI_COMMAND);
	ctrl = pci_cfg_rd16(d, ms->cap + MSI_CTRL);
	if (on) {
		pci_cfg_wr16(d, PCI_COMMAND,
		             (cmd | PCI_CMD_INTX_DISABLE | PCI_CMD_MASTER));
		/* 配るのは 1 本だけ（MME = 0）。複数要るなら MSI-X を使う。 */
		ctrl &= ~MSI_CTRL_MME_MASK;
		ctrl |= MSI_CTRL_ENABLE;
	} else {
		ctrl &= ~MSI_CTRL_ENABLE;
	}
	pci_cfg_wr16(d, ms->cap + MSI_CTRL, ctrl);
	return ER_OK;
}

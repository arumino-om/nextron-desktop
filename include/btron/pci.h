/*
 * pci.h — PCIe の列挙と MSI / MSI-X
 *
 * 【これは BTRON3 仕様の一部ではない】仕様書はデバイスドライバの
 * インタフェースを意図的に規定していない（周辺核 §1.8 はアプリから見た
 * デバイス管理だけを定める）。virtio.h と同じく、ここにあるのは
 * 本OS が選んだ土台である。
 *
 * 根拠は docs/design-issues.md **詳細 F8**。要点だけ写すと:
 *
 *   - **ECAM だけを使う。** I/O ポート 0xCF8/0xCFC は x86 専用で、しかも
 *     PCIe の拡張構成空間 (256〜4095) に届かない。ECAM は構成空間が
 *     そのままメモリに見えるので、両アーキで同じコードが動く
 *   - **窓の場所は記述から取る**（規律 R19）。x86_64 は ACPI の MCFG、
 *     arm64 は DT の `pci-host-ecam-generic`。`MachDesc.ecam` に入っている
 *   - **UEFI は構成を済ませて渡してくる。** バス番号は振られ、BAR には
 *     アドレスが割り当たっている。だからここがやるのは「列挙する」
 *     「構成空間を読む」「BAR を写す」「割込みを張る」の 4 つだけで、
 *     資源の割り当て直しはしない
 *   - **割込みは MSI / MSI-X を本線にする。** レガシ割込み (INTx) の経路は
 *     ACPI の `_PRT` = DSDT の AML の中にあるので、MSI にすれば AML の
 *     解釈器 (T1-15) を実機ブートの前提から外せる
 *
 * 【この層が持たない知識】(詳細 F8)
 *
 * PCIe 層は「見つける・読む・写す・張る」だけで、**装置ごとの知識を
 * 持たない**。クラスコードで候補を絞るところまでが仕事で、NVMe / xHCI の
 * 中身はドライバ側。`virtio_mmio_probe()` が「枠の存在と中身は別」と
 * したのと同じ形である。
 *
 * **MSI の宛先も持たない。** 宛先（アドレスとデータ）を決めるのは
 * アーキの割込みコントローラなので `hal_msi_alloc()`（intc.h）にある。
 * ここは与えられた値をテーブルへ書くだけ。
 *
 * 【規律 R13】置き場はカーネル内。詳細 F1 の表で「ドライバより先に
 * 装置を見つける層で、これが無いとどのドライバも始まらないので初期
 * シリアルと同じ扱いにする」と理由付きで例外にしてある。
 *
 * 【規律 R15】規格は PCI Express Base Specification と PCI Local Bus
 * Specification の公開情報から書いた。他の OS のコードは見ていない。
 */

#ifndef __BTRON_PCI_H__
#define __BTRON_PCI_H__

#include <btron/errcode.h>
#include <btron/typedef.h>

/* ------------------------------------------------------------------ */
/* 構成空間 (PCI Local Bus Spec §6.1 / PCIe の拡張は 0x100 以降)        */
/* ------------------------------------------------------------------ */

#define PCI_VENDOR_ID 0x00   /* 16bit。0xFFFF なら関数が居ない */
#define PCI_DEVICE_ID 0x02   /* 16bit */
#define PCI_COMMAND 0x04     /* 16bit */
#define PCI_STATUS 0x06      /* 16bit */
#define PCI_REVISION 0x08    /* 8bit */
#define PCI_PROG_IF 0x09     /* 8bit */
#define PCI_SUBCLASS 0x0A    /* 8bit */
#define PCI_CLASS 0x0B       /* 8bit */
#define PCI_CACHE_LINE 0x0C  /* 8bit。読み書きできる唯一の無害なレジスタ */
#define PCI_HEADER_TYPE 0x0E /* 8bit */
#define PCI_BAR0 0x10        /* 32bit x 6 (ブリッジは x2) */
#define PCI_SECONDARY_BUS 0x19   /* 8bit。ブリッジのみ */
#define PCI_SUBORDINATE_BUS 0x1A /* 8bit。ブリッジのみ */
#define PCI_CAP_PTR 0x34         /* 8bit。ケーパビリティ列の先頭 */

/* 構成空間の大きさ。PCIe の 1 関数あたり 4KB。 */
#define PCI_CFG_SIZE 4096

/* Command レジスタ */
#define PCI_CMD_IO 0x0001
#define PCI_CMD_MEM 0x0002
#define PCI_CMD_MASTER 0x0004 /* バスマスタ。MSI を送るのに必要 */
#define PCI_CMD_INTX_DISABLE 0x0400

/* Status レジスタ */
#define PCI_STATUS_CAPLIST 0x0010

/* Header Type */
#define PCI_HDR_TYPE_MASK 0x7F
#define PCI_HDR_MULTIFUNC 0x80 /* 関数 1〜7 も見に行く必要がある */
#define PCI_HDR_NORMAL 0x00
#define PCI_HDR_BRIDGE 0x01 /* PCI-to-PCI ブリッジ。secondary bus を辿る */

/* クラスコード（ベースクラスのみ。詳細 F9 で要るものだけ名前を付ける）*/
#define PCI_CLASS_STORAGE 0x01
#define PCI_CLASS_NETWORK 0x02
#define PCI_CLASS_DISPLAY 0x03
#define PCI_CLASS_BRIDGE 0x06
#define PCI_CLASS_SERIAL_BUS 0x0C

#define PCI_SUBCLASS_NVM 0x08  /* storage: NVMe は prog_if 0x02 */
#define PCI_SUBCLASS_USB 0x03  /* serial bus: xHCI は prog_if 0x30 */

/* クラスコードでの検索で「問わない」を表す。 */
#define PCI_ANY 0xFFFFU

/* 居ない関数を読むと全ビット 1 が返る。 */
#define PCI_NO_DEVICE 0xFFFFU

/* ------------------------------------------------------------------ */
/* ケーパビリティ                                                      */
/* ------------------------------------------------------------------ */

#define PCI_CAP_MSI 0x05
/* PCI Express ケーパビリティ。**「PCIe なら必ずある」とは限らない** —
 * QEMU の arm64 virt の virtio-pci は GPEX のルートバスに直付けされ、
 * 従来型 PCI 装置として見えるので持っていない（実測。鎖は
 * MSI-X 1 つとベンダ固有 5 つだけだった）。有無で経路を分けないこと。 */
#define PCI_CAP_EXPRESS 0x10
#define PCI_CAP_MSIX 0x11

/* ------------------------------------------------------------------ */
/* 1 つの関数                                                          */
/*                                                                     */
/* `cfg` はその関数の構成空間 4KB を写した仮想アドレス。写像は上位半分の  */
/* MMIO 区画にあり（規律 R18）、デバイスメモリ属性で張られている。        */
/* ------------------------------------------------------------------ */

typedef struct PciDev {
	UD cfg; /* 構成空間の仮想アドレス。0 なら無効 */
	UW seg;
	UW bus;
	UW dev;
	UW func;
	UW vendor;
	UW device;
	UW classcode; /* ベースクラス */
	UW subclass;
	UW prog_if;
	UW header; /* PCI_HDR_*。種別は PCI_HDR_TYPE_MASK で取り出す */
} PciDev;

/* BAR 1 本。 */
typedef struct PciBar {
	UD pa;     /* 割り当てられているアドレス（ファームウェアが決めた）*/
	UD size;   /* 大きさ。0 なら BAR が使われていない */
	Bool is_io; /* I/O 空間の BAR。MMIO として写せない */
	Bool is_64; /* 64bit BAR。次の 1 本を消費している */
} PciBar;

/* MSI-X の記述。`pci_msix_open()` で埋める。 */
typedef struct PciMsix {
	UD table; /* ベクタテーブルの仮想アドレス */
	UD pba;   /* Pending Bit Array の仮想アドレス */
	UW cap;   /* ケーパビリティのオフセット */
	UW count; /* テーブルの要素数 (1〜2048) */
} PciMsix;

/* MSI の記述。テーブルを持たず構成空間の中で完結する。 */
typedef struct PciMsi {
	UW cap;
	UW count;  /* 割り当てられる最大ベクタ数（2 のべき乗）*/
	Bool is_64; /* 64bit アドレスを書けるか */
	Bool per_vector_mask;
} PciMsi;

/* ------------------------------------------------------------------ */
/* 初期化と列挙                                                        */
/* ------------------------------------------------------------------ */

/* ECAM 窓を使えるようにする。何度呼んでもよい。
 *
 *   ER_OK      窓があり、使える
 *   ER_NOEXS   記述に窓が無い（PCIe が無い機種。または記述を読めていない）
 *   ER_NOSPC   窓を写せなかった */
IMPORT ERR pci_init(void);

/* バス 0 から深さ優先で列挙する。ブリッジの secondary bus を辿り、
 * マルチファンクションの関数 1〜7 も見る。
 *
 * 戻り値は**見つけた関数の総数**で、`out` には先頭の `max` 個だけを入れる
 * （`lst_dev` と同じ意味論。切り捨てても数は数える）。
 * `out` が 0 なら数えるだけ。負のエラーを返すことがある。
 *
 * **副作用を持たない。** BAR の大きさを測ると装置の応答を一時的に
 * 止める必要があるので（`pci_bar` の注意書き）、列挙では測らない。 */
IMPORT W pci_enum(PciDev *out, W max);

/* クラスコードで絞って列挙する。`PCI_ANY` は「問わない」。
 * 戻り値の意味論は `pci_enum` と同じ。 */
IMPORT W pci_find_class(UW classcode, UW subclass, UW prog_if, PciDev *out,
                        W max);

/* bus:dev:func を直に指す。居なければ False（`vendor` が 0xFFFF）。 */
IMPORT Bool pci_at(UW seg, UW bus, UW dev, UW func, PciDev *out);

/* ------------------------------------------------------------------ */
/* 構成空間アクセス                                                    */
/*                                                                     */
/* オフセットは 0〜4095。**拡張構成空間 (256〜4095) まで届く**のが       */
/* ECAM を選んだ理由の 1 つ（詳細 F8）。範囲外や無効な装置を読むと      */
/* 全ビット 1 が返り、書き込みは捨てられる（PCI の「居ない」の作法）。  */
/* ------------------------------------------------------------------ */

IMPORT UW pci_cfg_rd8(CONST PciDev *d, UW off);
IMPORT UW pci_cfg_rd16(CONST PciDev *d, UW off);
IMPORT UW pci_cfg_rd32(CONST PciDev *d, UW off);
IMPORT void pci_cfg_wr8(CONST PciDev *d, UW off, UW v);
IMPORT void pci_cfg_wr16(CONST PciDev *d, UW off, UW v);
IMPORT void pci_cfg_wr32(CONST PciDev *d, UW off, UW v);

/* ------------------------------------------------------------------ */
/* BAR                                                                 */
/* ------------------------------------------------------------------ */

/* BAR `idx` (0〜5) を読む。
 *
 * 【注意】大きさを知る方法は PCI には 1 つしか無い — **BAR に全ビット 1 を
 * 書いて読み返す**。その間この関数は装置の応答（Command の MEM / IO ビット）
 * を落とし、終わったら元に戻す。つまり
 *
 *   **いま画面を描いている装置に対して呼んではならない。**
 *
 * QEMU の arm64 virt では画面は ramfb（PCIe ではない）なので当たらないが、
 * 実機 (F9) では GOP のフレームバッファが表示装置の BAR そのものである。
 * 表示装置 (`PCI_CLASS_DISPLAY`) の BAR を測るのは、画面を手放した後に
 * 限ること。 */
IMPORT Bool pci_bar(CONST PciDev *d, UW idx, PciBar *out);

/* BAR を上位半分の MMIO 区画へ写し、仮想アドレスを返す。失敗時 0。
 * `pci_bar` と同じ注意が効く。`size` が 0 でなければ大きさを返す。
 *
 * **写しただけでは読めない。** 続けて `pci_enable()` を呼ぶこと。 */
IMPORT UD pci_bar_map(CONST PciDev *d, UW idx, UD *size);

/* メモリ空間への応答を許す。`master` を真にするとバスマスタも許す。
 *
 * **BAR を写して仮想アドレスを得ても、それだけでは読めない。** Command の
 * MEM ビットが立っていない装置は、割り当てられた番地に一切応答しない
 * （読むと全ビット 1 が返る）。ファームウェアは使い終わった装置の応答を
 * 落として渡してくる — 実測で QEMU + ArmVirtQemu の virtio-pci が
 * すべて MEM = 0 で来た。**症状は「BAR は写せたのに中身が全部 0xFF」**
 * という分かりにくい形で出る。
 *
 * バスマスタは装置が自分でメモリを読み書きするのに必要で、MSI もその
 * 一種（装置がメモリ書き込みとして割込みを送る）。**まだ初期化して
 * いない装置に許すと、キューのゴミを読み始めうる**ので、本当に使う
 * 段になってから許す。 */
IMPORT ERR pci_enable(CONST PciDev *d, Bool master);

/* ------------------------------------------------------------------ */
/* ケーパビリティ                                                      */
/* ------------------------------------------------------------------ */

/* ケーパビリティ列から `capid` を探す。構成空間でのオフセットを返し、
 * 無ければ 0。壊れた装置で無限に回らないよう打ち切る。 */
IMPORT UW pci_cap_find(CONST PciDev *d, UW capid);

/* ------------------------------------------------------------------ */
/* MSI-X / MSI (詳細 F8)                                               */
/*                                                                     */
/* **MSI-X を優先する。** テーブルが BAR の中にあるので複数ベクタを      */
/* 素直に配れて、1 本しか無い MSI よりキューごとの割り当てに向く。       */
/* NVMe も xHCI も MSI-X を持っている。                                 */
/*                                                                     */
/* 宛先（アドレスとデータ）はここでは決めない。`hal_msi_alloc()`        */
/* (intc.h) が割り当てたものを `pci_msix_set` に渡す。                  */
/* ------------------------------------------------------------------ */

/* MSI-X を使えるようにする。テーブルの載っている BAR を写す。
 *
 *   ER_OK      使える。`mx` が埋まる
 *   ER_NOSPT   MSI-X を持っていない
 *   ER_NOSPC   テーブルの BAR を写せなかった
 *
 * **この時点では MSI-X を有効にしない。** 宛先を書く前に有効にすると、
 * 装置が何処でもない番地へ割込みを送ることになる。
 *
 * ただし**テーブルの載っている BAR の応答は許す**（`pci_enable(d, False)`
 * 相当）。テーブルは BAR の中にあるので、これをしないと書いた値が
 * どこにも届かない。バスマスタは許さない。 */
IMPORT ERR pci_msix_open(CONST PciDev *d, PciMsix *mx);

/* ベクタ `vec` の宛先を書く。書いた時点ではマスクされたまま。 */
IMPORT ERR pci_msix_set(CONST PciMsix *mx, UW vec, UD addr, UW data);

/* ベクタ `vec` の宛先を読み返す。検証用。 */
IMPORT Bool pci_msix_get(CONST PciMsix *mx, UW vec, UD *addr, UW *data);

/* ベクタ `vec` のマスクを上げ下げする。 */
IMPORT ERR pci_msix_mask(CONST PciMsix *mx, UW vec, Bool on);

/* MSI-X 全体の有効・無効。有効にすると同時に INTx を止め、
 * バスマスタを許す（MSI は装置が自分で送るので必要）。 */
IMPORT ERR pci_msix_enable(CONST PciDev *d, CONST PciMsix *mx, Bool on);

/* MSI-X が有効か。検証用。 */
IMPORT Bool pci_msix_enabled(CONST PciDev *d, CONST PciMsix *mx);

/* --- MSI（MSI-X が無い装置向け）--- */

IMPORT ERR pci_msi_open(CONST PciDev *d, PciMsi *ms);
IMPORT ERR pci_msi_set(CONST PciDev *d, CONST PciMsi *ms, UD addr, UW data);
IMPORT ERR pci_msi_enable(CONST PciDev *d, CONST PciMsi *ms, Bool on);

#endif /* __BTRON_PCI_H__ */

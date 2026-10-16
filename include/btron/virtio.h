/*
 * virtio.h — virtio-mmio トランスポートと split virtqueue
 *
 * 【これは BTRON3 仕様の一部ではない】仕様書はデバイスドライバの
 * インタフェースを意図的に規定していない（周辺核 §1.8 はアプリから見た
 * デバイス管理だけを定める）。ここにあるのは本OS が選んだ土台である。
 *
 * 【なぜ virtio-mmio か】
 * arm64 の QEMU virt 機種には PS/2 が無い。入力装置を得る道は
 *
 *   (a) virtio-mmio … Device Tree に 32 枠が並び、割込みは普通の SPI
 *   (b) virtio-pci  … PCIe の列挙が要る。割込みは INTx か MSI-X で、
 *                     どちらも ACPI の _PRT か GIC の ITS/v2m を要する
 *   (c) USB (xHCI)  … 一番重い
 *
 * (a) が圧倒的に軽く、しかも**全部が記述から取れる**（規律 R19）。
 * ただし枠の記述は Device Tree にしか無い。ACPI では DSDT の AML の中
 * （_HID "LNRO0005"）にあり、AML を読む仕掛けが必要になる。だから
 * 入力装置を使うときは `virt,acpi=off` で動かす。ACPI 経路での装置列挙は
 * 別の課題 (T1-15) に分けた。
 *
 * 【規格の出どころ】Virtual I/O Device (VIRTIO) Version 1.1 以降、
 * OASIS の公開仕様。レジスタ配置と virtqueue の構造は仕様書から書いた。
 * Linux や QEMU のコードは見ていない（規律 R15 と同じ姿勢）。
 *
 * 【旧式インタフェースは扱わない】
 * MMIO の Version レジスタが 1 を返すものは virtio 0.9.5 相当の
 * 「legacy interface」で、キューの渡し方が QueuePFN（ページ番号 1 個）と
 * 根本的に違う。本OS は Version 2 だけを相手にし、1 なら `ER_NOSPT` で
 * はっきり断る。理由は
 *
 *   - virtio-input は 1.0 で入った装置で、旧式に対応する規定が無い
 *   - 旧式はリングの endianness がゲスト依存で、移植のたびに罠になる
 *
 * **ただし QEMU の virt 機種は既定で旧式を出す。**
 * `-global virtio-mmio.force-legacy=false` を付けると Version 2 になる。
 * tools/run_qemu.py の `--input` がこれを面倒見る。
 */

#ifndef __BTRON_VIRTIO_H__
#define __BTRON_VIRTIO_H__

#include <btron/typedef.h>

/* ------------------------------------------------------------------ */
/* MMIO レジスタ (VIRTIO 1.1 §4.2.2)                                   */
/* ------------------------------------------------------------------ */

#define VIRTIO_MMIO_MAGIC 0x000        /* R  0x74726976 = "virt" */
#define VIRTIO_MMIO_VERSION 0x004      /* R  1=legacy, 2=modern */
#define VIRTIO_MMIO_DEVICE_ID 0x008    /* R  0 なら枠が空 */
#define VIRTIO_MMIO_VENDOR_ID 0x00c    /* R */
#define VIRTIO_MMIO_DEV_FEAT 0x010     /* R */
#define VIRTIO_MMIO_DEV_FEAT_SEL 0x014 /* W  32 ビットずつ窓を切り替える */
#define VIRTIO_MMIO_DRV_FEAT 0x020     /* W */
#define VIRTIO_MMIO_DRV_FEAT_SEL 0x024 /* W */
#define VIRTIO_MMIO_QUEUE_SEL 0x030    /* W */
#define VIRTIO_MMIO_QUEUE_NUM_MAX 0x034 /* R  0 ならその番号のキューは無い */
#define VIRTIO_MMIO_QUEUE_NUM 0x038    /* W */
#define VIRTIO_MMIO_QUEUE_READY 0x044  /* RW */
#define VIRTIO_MMIO_QUEUE_NOTIFY 0x050 /* W */
#define VIRTIO_MMIO_INT_STATUS 0x060   /* R */
#define VIRTIO_MMIO_INT_ACK 0x064      /* W */
#define VIRTIO_MMIO_STATUS 0x070       /* RW */
#define VIRTIO_MMIO_QUEUE_DESC_LO 0x080
#define VIRTIO_MMIO_QUEUE_DESC_HI 0x084
#define VIRTIO_MMIO_QUEUE_DRV_LO 0x090 /* available リング */
#define VIRTIO_MMIO_QUEUE_DRV_HI 0x094
#define VIRTIO_MMIO_QUEUE_DEV_LO 0x0a0 /* used リング */
#define VIRTIO_MMIO_QUEUE_DEV_HI 0x0a4
#define VIRTIO_MMIO_CONFIG_GEN 0x0fc   /* R */
#define VIRTIO_MMIO_CONFIG 0x100       /* 装置固有の設定空間 */

#define VIRTIO_MAGIC 0x74726976U

/* Status レジスタのビット (§2.1) */
#define VIRTIO_S_ACKNOWLEDGE 0x01
#define VIRTIO_S_DRIVER 0x02
#define VIRTIO_S_DRIVER_OK 0x04
#define VIRTIO_S_FEATURES_OK 0x08
#define VIRTIO_S_NEEDS_RESET 0x40
#define VIRTIO_S_FAILED 0x80

/* InterruptStatus のビット (§4.2.2) */
#define VIRTIO_INT_USED 0x01   /* used リングが進んだ */
#define VIRTIO_INT_CONFIG 0x02 /* 設定空間が変わった */

/* 機能ビット (§6)。64 ビット空間の 32 番。 */
#define VIRTIO_F_VERSION_1 32

/* 装置の種類 (§5)。使うものだけ。 */
#define VIRTIO_ID_BLOCK 2
#define VIRTIO_ID_INPUT 18

/* ------------------------------------------------------------------ */
/* virtio-blk (§5.2)                                                   */
/* ------------------------------------------------------------------ */

/* 設定空間。capacity は**512 バイトセクタ単位**（§5.2.4）。
 * 論理ブロックサイズとは別物で、blk_size 機能が無ければ 512 と考える。 */
#define VIRTIO_BLK_CFG_CAPACITY 0x00 /* le64 */

/* 要求の型 (§5.2.6)。 */
#define VIRTIO_BLK_T_IN 0   /* 装置 → こちら（読み） */
#define VIRTIO_BLK_T_OUT 1  /* こちら → 装置（書き） */
#define VIRTIO_BLK_T_FLUSH 4

/* 要求ヘッダは 16 バイト。type(le32) reserved(le32) sector(le64)。
 * **構造体を直接渡さない**（規律 R1）。バイト単位で詰める。 */
#define VIRTIO_BLK_REQ_HDR 16

/* 応答の 1 バイト (§5.2.6)。 */
#define VIRTIO_BLK_S_OK 0
#define VIRTIO_BLK_S_IOERR 1
#define VIRTIO_BLK_S_UNSUPP 2

/* セクタの大きさ。capacity の単位であり、GPT の LBA の単位でもある。 */
#define VIRTIO_BLK_SECTOR 512

/* ------------------------------------------------------------------ */
/* split virtqueue (§2.6)                                              */
/* ------------------------------------------------------------------ */

/* 記述子。16 バイト固定。リトルエンディアン。 */
typedef struct VqDesc {
	UD addr; /* バッファの**物理**アドレス */
	UW len;
	UH flags;
	UH next;
} VqDesc;

#define VQ_DESC_F_NEXT 0x1
#define VQ_DESC_F_WRITE 0x2 /* 装置が書く（＝こちらは読む） */

/* avail リングのフラグ (§2.6.6)。立てると装置は割込みを上げない。
 * ブロック装置は同期に使う（要求を出して待つ）ので、割込みを
 * 使わずに used リングを見張る。 */
#define VQ_AVAIL_F_NO_INTERRUPT 0x1

/* used リングの 1 要素。8 バイト。 */
typedef struct VqUsedElem {
	UW id;  /* 使い終わった記述子の番号 */
	UW len; /* 装置が書いた長さ */
} VqUsedElem;

/* 1 本のキュー。
 *
 * desc / avail / used は仕様上は別々に置けるが、1 ページに詰めている。
 * バッファ領域も固定長 × 個数で確保し、記述子 i がバッファ i を指すよう
 * 最初に組んでしまう。入力装置のように「同じ大きさの箱を回し続ける」
 * 使い方ならこれで足りる。 */
typedef struct VirtQ {
	UW index; /* キュー番号 */
	UW qsz;   /* 記述子の数。2 の冪 */
	UW bufsz; /* 1 バッファのバイト数 */

	/* 以下は仮想アドレス。装置に渡すのは物理アドレス。 */
	VqDesc *desc;
	UH *avail;          /* [0]=flags [1]=idx [2..]=ring */
	UH *used;           /* [0]=flags [1]=idx 以降 VqUsedElem */
	VqUsedElem *used_ring;
	UB *buf;            /* qsz × bufsz */

	UD ring_pa; /* 確保したページの先頭物理アドレス */
	UW ring_pages;
	UD buf_pa;
	UW buf_pages;

	UH last_used; /* こちらが見た used.idx */
	UH avail_idx; /* こちらが進めた avail.idx の控え */
} VirtQ;

/* 1 台の装置。 */
typedef struct VirtioDev {
	UD mmio;  /* レジスタの仮想アドレス。0 なら未使用 */
	UD pa;    /* 物理アドレス。ログ用 */
	UW devid;
	UW version;
	UW vendor;
	UW intid; /* GIC の INTID */
} VirtioDev;

/* ------------------------------------------------------------------ */
/* トランスポート                                                      */
/* ------------------------------------------------------------------ */

/* machdesc の virtio 枠を順に調べ、DeviceID が devid の装置を out に集める。
 * 戻り値は見つかった台数（max まで）。負ならエラー。
 * **レジスタ空間の写像もここで行う。** */
IMPORT W virtio_mmio_probe(UW devid, VirtioDev *out, W max);

/* 初期化手順 (§3.1) の前半。リセット → ACKNOWLEDGE → DRIVER →
 * 機能交渉 → FEATURES_OK。want は「これがあれば使う」機能ビットの集合で、
 * VIRTIO_F_VERSION_1 は常に要求する。 */
IMPORT ERR virtio_setup(VirtioDev *d, UD want);

/* キューを 1 本用意する。want は欲しい記述子の数（装置の上限で切る）。 */
IMPORT ERR virtio_queue_setup(VirtioDev *d, VirtQ *q, UW index, UW want, UW bufsz,
                              Bool device_writes);

/* 初期化手順の後半。DRIVER_OK を立てる。 */
IMPORT void virtio_ready(VirtioDev *d);

/* 設定空間の読み書き。 */
IMPORT UW virtio_cfg_rd8(CONST VirtioDev *d, UW off);
IMPORT UW virtio_cfg_rd32(CONST VirtioDev *d, UW off);
IMPORT void virtio_cfg_wr8(CONST VirtioDev *d, UW off, UW v);

/* 割込み。 */
IMPORT UW virtio_int_status(CONST VirtioDev *d);
IMPORT void virtio_int_ack(CONST VirtioDev *d, UW bits);

/* 記述子 idx を装置に渡す。avail リングに載せるだけで通知はしない。 */
IMPORT void vq_offer(VirtQ *q, UW idx);

/* 渡した分を装置に知らせる。 */
IMPORT void vq_kick(CONST VirtioDev *d, VirtQ *q);

/* used リングから 1 つ取り出す。取れたら True。 */
IMPORT Bool vq_take(VirtQ *q, UW *idx, UW *len);

/* 記述子 idx のバッファ先頭。 */
#define VQ_BUF(q, idx) ((q)->buf + (UD)(idx) * (q)->bufsz)

#endif /* __BTRON_VIRTIO_H__ */

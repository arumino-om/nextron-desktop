/*
 * blk.c — virtio-blk のデバイスドライバ (周辺核 §1.8)
 *
 * 論理デバイス名 "vda"。ブロック型。**サブユニットが GPT の区画**になる。
 *
 * ------------------------------------------------------------------------
 * なぜこれが要るのか
 * ------------------------------------------------------------------------
 *
 * ここまでファイルシステムは RAM ディスク ("rda") の上にしか無く、
 * 電源を切ると消えていた。**自立起動（ディスクから起動して、前回の
 * 内容が残っている）には永続する記憶装置が要る。**
 *
 * 装置は選ぶ必要がない。UEFI 経路では QEMU に
 * `-drive format=raw,if=virtio,file=<image>` を渡していて、
 * **ファームウェアが起動に使っているその同じディスクが virtio-blk として
 * 見えている**。つまりカーネルは自分が入っているディスクを読み書きできる。
 *
 * ------------------------------------------------------------------------
 * サブユニット = 区画
 * ------------------------------------------------------------------------
 *
 * 仕様 §1.8.2 は「1 つのユニットに含まれるすべてのサブユニットは 1 つの
 * デバイスドライバで処理される」としか言っておらず、サブユニットの
 * 意味は実装に委ねられている。RAM ディスクは領域を機械的に 2 等分して
 * いたが、実機のディスクでは**区画 1 つがサブユニット 1 つ**が自然で、
 * 仕様の「物理デバイス名による排他」もそのまま効く。
 *
 *   "vda"   ユニット全体（GPT ごと。区画表を読むのに使う）
 *   "vda0"  第 1 区画（ESP。ローダとカーネルが入っている）
 *   "vda1"  第 2 区画（BTRON 区画。ファイルシステムを載せる）
 *
 * 区画表は GPT を自分で読む。**構造体を直接 I/O してはならない**（規律 R1）
 * ので、バイトの位置を明示して組み立てる。
 *
 * ------------------------------------------------------------------------
 * 割込みを使わない
 * ------------------------------------------------------------------------
 *
 * 要求を出してから結果が要るまでの間に他にやることが無い（ファイル
 * システムが同期的に呼ぶ）ので、used リングを見張る。avail リングの
 * フラグに VQ_AVAIL_F_NO_INTERRUPT を立て、GIC 側も有効にしない。
 * **ハンドラの無い割込みが上がり続けると、EOI だけ出して回り続ける**
 * （intc_dispatch）ので、上げさせないのが一番安全である。
 */

#include <btron/device.h>
#include <btron/hal.h>
#include <btron/kernel.h>
#include <btron/virtio.h>

#define VBLK_MAX_PARTS 4
/* 1 要求で運ぶ上限。記述子 0 がヘッダ、1 がデータ、2 が応答。 */
#define VBLK_BUFSZ 4096
#define VBLK_MAX_BLK (VBLK_BUFSZ / VIRTIO_BLK_SECTOR) /* 8 ブロック */
#define VBLK_QSZ 4
/* 応答を待つ回数の上限。QEMU は即座に返すが、無限に回ると
 * 「無言のハング」になるので必ず上限を置く（Phase 1 の教訓）。 */
#define VBLK_SPIN 100000000

typedef struct {
	UD first; /* 先頭 LBA */
	UD count; /* ブロック数 */
	Bool btron; /* BTRON 区画のタイプ GUID か */
} BlkPart;

LOCAL VirtioDev blk_d;
LOCAL VirtQ blk_q;
LOCAL Bool blk_up;
LOCAL UD blk_capacity; /* 512B セクタ数 */
LOCAL BlkPart blk_part[VBLK_MAX_PARTS];
LOCAL W blk_nparts;
LOCAL ID blk_mtx; /* 要求は 1 本ずつ。記述子を使い回すため */

/* BTRON 区画のタイプ GUID（詳細 D8。tools/mkimage.py と同じ値）。
 * 文字列 "B7120000-0000-4254-524F-4E4653303300" を GPT の並び
 * （前半 3 群はリトルエンディアン、後半 2 群はそのまま）で置いたもの。 */
LOCAL CONST UB btron_type_guid[16] = {
	0x00, 0x00, 0x12, 0xB7, 0x00, 0x00, 0x54, 0x42,
	0x52, 0x4F, 0x4E, 0x46, 0x53, 0x30, 0x33, 0x00};

/* ------------------------------------------------------------------ */
/* バイト列から読む（規律 R1 / R7）                                    */
/* ------------------------------------------------------------------ */

LOCAL UW ld32(CONST UB *p)
{
	return (UW)p[0] | ((UW)p[1] << 8) | ((UW)p[2] << 16) | ((UW)p[3] << 24);
}

LOCAL UD ld64(CONST UB *p)
{
	return (UD)ld32(p) | ((UD)ld32(p + 4) << 32);
}

LOCAL void st32(UB *p, UW v)
{
	p[0] = (UB)(v & 0xFFU);
	p[1] = (UB)((v >> 8) & 0xFFU);
	p[2] = (UB)((v >> 16) & 0xFFU);
	p[3] = (UB)((v >> 24) & 0xFFU);
}

LOCAL void st64(UB *p, UD v)
{
	st32(p, (UW)(v & 0xFFFFFFFFULL));
	st32(p + 4, (UW)(v >> 32));
}

/* ------------------------------------------------------------------ */
/* 要求 1 件                                                           */
/* ------------------------------------------------------------------ */

/* sector から nblk ブロックを読む / 書く。成功なら nblk、失敗なら負。
 * **呼ぶ側は blk_mtx を取っていること。** */
LOCAL W blk_request(UW type, UD sector, B *buf, W nblk)
{
	UB *hdr = VQ_BUF(&blk_q, 0);
	UB *data = VQ_BUF(&blk_q, 1);
	UB *stat = VQ_BUF(&blk_q, 2);
	UD base = blk_q.buf_pa;
	UW len = (UW)nblk * VIRTIO_BLK_SECTOR;
	UD spin;
	W i;

	st32(hdr, type);
	st32(hdr + 4, 0);
	st64(hdr + 8, sector);
	*stat = 0xFFU; /* 装置が書いたことが分かるように潰しておく */

	if (type == VIRTIO_BLK_T_OUT) {
		for (i = 0; i < (W)len; i++) {
			data[i] = (UB)buf[i];
		}
	}

	/* 記述子 3 本の連鎖。ヘッダ（装置が読む）→ データ → 応答（装置が書く）。 */
	blk_q.desc[0].addr = base;
	blk_q.desc[0].len = VIRTIO_BLK_REQ_HDR;
	blk_q.desc[0].flags = VQ_DESC_F_NEXT;
	blk_q.desc[0].next = 1;

	blk_q.desc[1].addr = base + VBLK_BUFSZ;
	blk_q.desc[1].len = len;
	blk_q.desc[1].flags = (UH)(VQ_DESC_F_NEXT |
	                           ((type == VIRTIO_BLK_T_IN) ? VQ_DESC_F_WRITE : 0));
	blk_q.desc[1].next = 2;

	blk_q.desc[2].addr = base + 2U * VBLK_BUFSZ;
	blk_q.desc[2].len = 1;
	blk_q.desc[2].flags = VQ_DESC_F_WRITE;
	blk_q.desc[2].next = 0;

	vq_offer(&blk_q, 0);
	vq_kick(&blk_d, &blk_q);

	for (spin = 0; spin < VBLK_SPIN; spin++) {
		UW idx = 0;
		UW got = 0;

		if (vq_take(&blk_q, &idx, &got)) {
			virtio_int_ack(&blk_d, virtio_int_status(&blk_d));
			hal_dma_barrier();
			if (*stat != VIRTIO_BLK_S_OK) {
				return ER_IO;
			}
			if (type == VIRTIO_BLK_T_IN) {
				for (i = 0; i < (W)len; i++) {
					buf[i] = (B)data[i];
				}
			}
			return nblk;
		}
	}
	return ER_IO; /* 応答が来ない */
}

/* ------------------------------------------------------------------ */
/* GPT の区画表                                                        */
/* ------------------------------------------------------------------ */

/* LBA1 のヘッダと区画エントリを読んで blk_part[] を埋める。
 * GPT が無ければ区画 0 件（ユニット全体だけが使える）。 */
LOCAL void blk_scan_gpt(void)
{
	B sec[VIRTIO_BLK_SECTOR];
	CONST UB *u = (CONST UB *)sec;
	UD ent_lba;
	UW nent;
	UW entsz;
	UW i;

	blk_nparts = 0;
	if (blk_request(VIRTIO_BLK_T_IN, 1, sec, 1) < 0) {
		return;
	}
	/* 署名 "EFI PART"（8 バイト。offset 0） */
	if (u[0] != 'E' || u[1] != 'F' || u[2] != 'I' || u[3] != ' ' ||
	    u[4] != 'P' || u[5] != 'A' || u[6] != 'R' || u[7] != 'T') {
		return;
	}
	ent_lba = ld64(u + 72);
	nent = ld32(u + 80);
	entsz = ld32(u + 84);
	if (entsz < 128 || entsz > VIRTIO_BLK_SECTOR) {
		return;
	}
	if (nent > 128U) {
		nent = 128U; /* 読み過ぎない。先頭の方しか使わない */
	}

	for (i = 0; i < nent && blk_nparts < VBLK_MAX_PARTS; i++) {
		UW per = VIRTIO_BLK_SECTOR / entsz;
		UD lba = ent_lba + (i / per);
		UW off = (i % per) * entsz;
		CONST UB *e;
		UD first;
		UD last;
		W k;
		Bool zero = True;
		Bool is_btron = True;

		if (blk_request(VIRTIO_BLK_T_IN, lba, sec, 1) < 0) {
			return;
		}
		e = (CONST UB *)sec + off;
		for (k = 0; k < 16; k++) {
			if (e[k] != 0) {
				zero = False;
			}
			if (e[k] != btron_type_guid[k]) {
				is_btron = False;
			}
		}
		if (zero) {
			continue; /* 未使用のエントリ */
		}
		first = ld64(e + 32);
		last = ld64(e + 40);
		if (last < first) {
			continue;
		}
		blk_part[blk_nparts].first = first;
		blk_part[blk_nparts].count = last - first + 1;
		blk_part[blk_nparts].btron = is_btron;
		blk_nparts++;
	}
}

/* ------------------------------------------------------------------ */
/* デバイス管理の口                                                    */
/* ------------------------------------------------------------------ */

/* サブユニットの範囲。sub < 0 ならユニット全体。 */
LOCAL Bool range_of(W sub, UD *base, UD *limit)
{
	if (sub < 0) {
		*base = 0;
		*limit = blk_capacity;
		return True;
	}
	if (sub >= blk_nparts) {
		return False;
	}
	*base = blk_part[sub].first;
	*limit = blk_part[sub].count;
	return True;
}

LOCAL W blk_rw(Bool write, W sub, W start, B *buf, W size, W *error)
{
	UD base;
	UD limit;
	W done = 0;
	ERR er;

	if (error != 0) {
		*error = 0;
	}
	if (start < 0) {
		return ER_DEV; /* 属性データは持っていない */
	}
	if (!blk_up) {
		return ER_NOMDA;
	}
	if (!range_of(sub, &base, &limit)) {
		return ER_PAR;
	}
	if ((UD)start >= limit) {
		return ER_PAR;
	}
	/* 仕様「size = 0 の時は実際の読み込みは行わずに、a_size に読み込み
	 * 可能なデータサイズを戻す」 */
	if (size == 0) {
		return (W)(limit - (UD)start);
	}
	if ((UD)start + (UD)size > limit) {
		size = (W)(limit - (UD)start);
	}

	er = (ERR)tk_loc_mtx(blk_mtx, TMO_FEVR);
	if (er != E_OK) {
		return ER_IO;
	}
	while (done < size) {
		W n = size - done;
		W r;

		if (n > VBLK_MAX_BLK) {
			n = VBLK_MAX_BLK;
		}
		r = blk_request(write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN,
		                base + (UD)start + (UD)done,
		                buf + (UD)done * VIRTIO_BLK_SECTOR, n);
		if (r < 0) {
			(void)tk_unl_mtx(blk_mtx);
			return (done > 0) ? done : r;
		}
		done += r;
	}
	(void)tk_unl_mtx(blk_mtx);
	return done;
}

LOCAL W blk_read(W sub, W start, B *buf, W size, W *error)
{
	return blk_rw(False, sub, start, buf, size, error);
}

LOCAL W blk_write(W sub, W start, CONST B *buf, W size, W *error)
{
	/* 書き込みの経路も同じ関数を通す。buf は読むだけなので
	 * const を外して渡す（blk_rw は write のとき書かない）。 */
	return blk_rw(True, sub, start, (B *)buf, size, error);
}

LOCAL DevDriver blk_drv = {
	"vda",
	DK_DISK_HD, /* ブロック型、取り外し不可 */
	0,            /* サブユニット数は区画を読んでから入れる */
	VIRTIO_BLK_SECTOR,
	0,
	0,
	blk_read,
	blk_write,
};

/* ------------------------------------------------------------------ */
/* 登録                                                                */
/* ------------------------------------------------------------------ */

EXPORT ERR vblk_register(void)
{
	T_CMTX cmtx;
	ERR er;
	W n;

	if (blk_up) {
		return ER_OK; /* 二度呼ばれても害が無いようにする */
	}
	n = virtio_mmio_probe(VIRTIO_ID_BLOCK, &blk_d, 1);
	if (n <= 0) {
		return ER_NOEXS; /* ディスクが無い構成。異常ではない */
	}
	er = virtio_setup(&blk_d, 0);
	if (er != ER_OK) {
		return er;
	}
	er = virtio_queue_setup(&blk_d, &blk_q, 0, VBLK_QSZ, VBLK_BUFSZ, False);
	if (er != ER_OK) {
		return er;
	}
	/* 割込みは使わない。used リングを見張る。 */
	blk_q.avail[0] = VQ_AVAIL_F_NO_INTERRUPT;
	virtio_ready(&blk_d);

	blk_capacity = (UD)virtio_cfg_rd32(&blk_d, VIRTIO_BLK_CFG_CAPACITY) |
	               ((UD)virtio_cfg_rd32(&blk_d, VIRTIO_BLK_CFG_CAPACITY + 4) << 32);
	if (blk_capacity == 0) {
		return ER_NOMDA;
	}

	cmtx.exinf = 0;
	cmtx.mtxatr = TA_TFIFO;
	cmtx.ceilpri = 1;
	blk_mtx = tk_cre_mtx(&cmtx);
	if (blk_mtx < 1) {
		return ER_NOMEM;
	}

	blk_up = True;
	blk_scan_gpt();
	blk_drv.nsub = blk_nparts;
	er = dev_register(&blk_drv);
	if (er != ER_OK) {
		blk_up = False;
		return er;
	}
	return ER_OK;
}

/* 検証とログ用。 */
EXPORT UD vblk_capacity(void)
{
	return blk_capacity;
}

EXPORT W vblk_parts(void)
{
	return blk_nparts;
}

/* BTRON 区画のサブユニット番号。無ければ -1。 */
EXPORT W vblk_btron_part(void)
{
	W i;

	for (i = 0; i < blk_nparts; i++) {
		if (blk_part[i].btron) {
			return i;
		}
	}
	return -1;
}

EXPORT UD vblk_part_blocks(W sub)
{
	if (sub < 0 || sub >= blk_nparts) {
		return 0;
	}
	return blk_part[sub].count;
}

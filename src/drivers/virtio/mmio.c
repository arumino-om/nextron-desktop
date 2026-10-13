/*
 * mmio.c — virtio-mmio トランスポートと split virtqueue
 *
 * 規格の出どころと方針は include/btron/virtio.h の冒頭に書いた。
 *
 * 【規律 R13】ここはカーネル内に置く。詳細 F1 の例外基準のうち
 * 「割込みに直結し、イベント管理より先に立っていなければならないもの」に
 * 当たる。ポインタとキーボードが無ければ外殻は操作できないので、
 * 外殻のサーバに依存させられない（鶏と卵になる）。
 */

#include <btron/virtio.h>

#include <btron/errcode.h>
#include <btron/hal.h>
#include <btron/machdesc.h>
#include <btron/vmem.h>

/* ------------------------------------------------------------------ */
/* レジスタアクセス                                                    */
/* ------------------------------------------------------------------ */

LOCAL inline UW rd32(UD base, UW off)
{
	return *(volatile UW *)(base + off);
}

LOCAL inline void wr32(UD base, UW off, UW v)
{
	*(volatile UW *)(base + off) = v;
}

EXPORT UW virtio_cfg_rd8(CONST VirtioDev *d, UW off)
{
	return *(volatile UB *)(d->mmio + VIRTIO_MMIO_CONFIG + off);
}

EXPORT UW virtio_cfg_rd32(CONST VirtioDev *d, UW off)
{
	return rd32(d->mmio, VIRTIO_MMIO_CONFIG + off);
}

EXPORT void virtio_cfg_wr8(CONST VirtioDev *d, UW off, UW v)
{
	*(volatile UB *)(d->mmio + VIRTIO_MMIO_CONFIG + off) = (UB)v;
}

EXPORT UW virtio_int_status(CONST VirtioDev *d)
{
	return rd32(d->mmio, VIRTIO_MMIO_INT_STATUS);
}

EXPORT void virtio_int_ack(CONST VirtioDev *d, UW bits)
{
	wr32(d->mmio, VIRTIO_MMIO_INT_ACK, bits);
}

/* ------------------------------------------------------------------ */
/* 枠の走査                                                            */
/* ------------------------------------------------------------------ */

EXPORT W virtio_mmio_probe(UW devid, VirtioDev *out, W max)
{
	CONST MachDesc *md = machdesc();
	W found = 0;
	UW i;

	if (out == 0 || max <= 0) {
		return ER_ADR;
	}
	for (i = 0; i < md->virtio_count && i < MD_MAX_VIRTIO; i++) {
		CONST MdSlot *s = &md->virtio[i];
		UD va;
		UW id;

		if (s->pa == 0 || s->size < 0x100) {
			continue;
		}
		va = hal_vm_map_device(s->pa, s->size);
		if (va == 0) {
			continue;
		}
		/* 枠が生きているかは MagicValue で分かる。
		 * **空の枠も Magic は返す。**挿さっているかは DeviceID 0 で見る。 */
		if (rd32(va, VIRTIO_MMIO_MAGIC) != VIRTIO_MAGIC) {
			continue;
		}
		id = rd32(va, VIRTIO_MMIO_DEVICE_ID);
		if (id != devid) {
			continue;
		}
		out[found].mmio = va;
		out[found].pa = s->pa;
		out[found].devid = id;
		out[found].version = rd32(va, VIRTIO_MMIO_VERSION);
		out[found].vendor = rd32(va, VIRTIO_MMIO_VENDOR_ID);
		out[found].intid = s->intid;
		found++;
		if (found >= max) {
			break;
		}
	}
	return found;
}

/* ------------------------------------------------------------------ */
/* 初期化手順 (§3.1)                                                   */
/* ------------------------------------------------------------------ */

EXPORT ERR virtio_setup(VirtioDev *d, UD want)
{
	UD have;
	UD use;

	if (d == 0 || d->mmio == 0) {
		return ER_ADR;
	}
	/* legacy (version 1) はキューの渡し方が違う（QueuePFN とページ単位）。
	 * QEMU は既定で version 2 を出すので、legacy は扱わないと決める。
	 * 黙って誤動作させるより、はっきり断る。 */
	if (d->version != 2) {
		return ER_NOSPT;
	}

	/* 1. リセット。UEFI が触っていた場合もこれで初期状態に戻る。 */
	wr32(d->mmio, VIRTIO_MMIO_STATUS, 0);
	/* 2. ACKNOWLEDGE  3. DRIVER */
	wr32(d->mmio, VIRTIO_MMIO_STATUS, VIRTIO_S_ACKNOWLEDGE);
	wr32(d->mmio, VIRTIO_MMIO_STATUS, VIRTIO_S_ACKNOWLEDGE | VIRTIO_S_DRIVER);

	/* 4. 機能交渉。64 ビットを 32 ビットの窓 2 枚で読む。 */
	wr32(d->mmio, VIRTIO_MMIO_DEV_FEAT_SEL, 0);
	have = (UD)rd32(d->mmio, VIRTIO_MMIO_DEV_FEAT);
	wr32(d->mmio, VIRTIO_MMIO_DEV_FEAT_SEL, 1);
	have |= (UD)rd32(d->mmio, VIRTIO_MMIO_DEV_FEAT) << 32;

	/* VERSION_1 は modern の必須条件。装置が出していなければ諦める。 */
	if ((have & (1ULL << VIRTIO_F_VERSION_1)) == 0) {
		wr32(d->mmio, VIRTIO_MMIO_STATUS, VIRTIO_S_FAILED);
		return ER_NOSPT;
	}
	use = (want | (1ULL << VIRTIO_F_VERSION_1)) & have;

	wr32(d->mmio, VIRTIO_MMIO_DRV_FEAT_SEL, 0);
	wr32(d->mmio, VIRTIO_MMIO_DRV_FEAT, (UW)(use & 0xFFFFFFFFULL));
	wr32(d->mmio, VIRTIO_MMIO_DRV_FEAT_SEL, 1);
	wr32(d->mmio, VIRTIO_MMIO_DRV_FEAT, (UW)(use >> 32));

	/* 5. FEATURES_OK を立て、6. 読み直して**装置が受けたか確かめる**。
	 * ここを省くと、受け入れられていない機能を前提に動いてしまう。 */
	wr32(d->mmio, VIRTIO_MMIO_STATUS,
	     VIRTIO_S_ACKNOWLEDGE | VIRTIO_S_DRIVER | VIRTIO_S_FEATURES_OK);
	if ((rd32(d->mmio, VIRTIO_MMIO_STATUS) & VIRTIO_S_FEATURES_OK) == 0) {
		wr32(d->mmio, VIRTIO_MMIO_STATUS, VIRTIO_S_FAILED);
		return ER_NOSPT;
	}
	return ER_OK;
}

EXPORT void virtio_ready(VirtioDev *d)
{
	wr32(d->mmio, VIRTIO_MMIO_STATUS,
	     VIRTIO_S_ACKNOWLEDGE | VIRTIO_S_DRIVER | VIRTIO_S_FEATURES_OK |
	         VIRTIO_S_DRIVER_OK);
}

/* ------------------------------------------------------------------ */
/* キューの用意                                                        */
/* ------------------------------------------------------------------ */

/* desc / avail / used を 1 つの領域に並べたときの大きさ。
 *
 * 仕様 §2.6 の並びと最小アラインは
 *   記述子表  16 × qsz   16 バイト境界
 *   avail     6 + 2×qsz   2 バイト境界
 *   used      6 + 8×qsz   4 バイト境界
 * で、間に詰め物を入れて境界を合わせる。 */
#define VQ_ALIGN(v, a) (((v) + ((a) - 1U)) & ~((a) - 1U))

LOCAL UW ring_bytes(UW qsz, UW *avail_off, UW *used_off)
{
	UW off = 16U * qsz;

	*avail_off = off;
	off += 6U + 2U * qsz;
	off = VQ_ALIGN(off, 4U);
	*used_off = off;
	off += 6U + 8U * qsz;
	return off;
}

LOCAL UW pages_for(UD bytes)
{
	return (UW)((bytes + (UD)hal_page_size() - 1) / hal_page_size());
}

EXPORT ERR virtio_queue_setup(VirtioDev *d, VirtQ *q, UW index, UW want, UW bufsz,
                              Bool device_writes)
{
	UW qmax;
	UW qsz;
	UW avail_off = 0;
	UW used_off = 0;
	UW bytes;
	UD ring_pa;
	UD buf_pa;
	UW i;

	if (d == 0 || q == 0 || bufsz == 0) {
		return ER_ADR;
	}
	wr32(d->mmio, VIRTIO_MMIO_QUEUE_SEL, index);
	/* 既に用意済みのキューを上書きしない（リセット直後なら 0 のはず）。 */
	if (rd32(d->mmio, VIRTIO_MMIO_QUEUE_READY) != 0) {
		return ER_BUSY;
	}
	qmax = rd32(d->mmio, VIRTIO_MMIO_QUEUE_NUM_MAX);
	if (qmax == 0) {
		return ER_NOEXS; /* その番号のキューは無い */
	}
	qsz = (want < qmax) ? want : qmax;
	/* 2 の冪に丸める（仕様の要求）。 */
	while ((qsz & (qsz - 1U)) != 0) {
		qsz &= qsz - 1U;
	}
	if (qsz == 0) {
		return ER_PAR;
	}

	bytes = ring_bytes(qsz, &avail_off, &used_off);
	ring_pa = pmem_alloc(pages_for(bytes));
	if (ring_pa == 0) {
		return ER_NOMEM;
	}
	buf_pa = pmem_alloc(pages_for((UD)qsz * bufsz));
	if (buf_pa == 0) {
		pmem_free(ring_pa, pages_for(bytes));
		return ER_NOMEM;
	}

	q->index = index;
	q->qsz = qsz;
	q->bufsz = bufsz;
	q->ring_pa = ring_pa;
	q->ring_pages = pages_for(bytes);
	q->buf_pa = buf_pa;
	q->buf_pages = pages_for((UD)qsz * bufsz);
	q->desc = (VqDesc *)BTRON_PA_TO_VA(ring_pa);
	q->avail = (UH *)(BTRON_PA_TO_VA(ring_pa) + avail_off);
	q->used = (UH *)(BTRON_PA_TO_VA(ring_pa) + used_off);
	q->used_ring = (VqUsedElem *)(q->used + 2);
	q->buf = (UB *)BTRON_PA_TO_VA(buf_pa);
	q->last_used = 0;
	q->avail_idx = 0;

	/* 記述子 i をバッファ i に結びつけてしまう。
	 * 連鎖は使わないので next は触らない。 */
	for (i = 0; i < qsz; i++) {
		q->desc[i].addr = buf_pa + (UD)i * bufsz;
		q->desc[i].len = bufsz;
		q->desc[i].flags = device_writes ? VQ_DESC_F_WRITE : 0;
		q->desc[i].next = 0;
	}

	wr32(d->mmio, VIRTIO_MMIO_QUEUE_NUM, qsz);
	wr32(d->mmio, VIRTIO_MMIO_QUEUE_DESC_LO, (UW)(ring_pa & 0xFFFFFFFFULL));
	wr32(d->mmio, VIRTIO_MMIO_QUEUE_DESC_HI, (UW)(ring_pa >> 32));
	wr32(d->mmio, VIRTIO_MMIO_QUEUE_DRV_LO,
	     (UW)((ring_pa + avail_off) & 0xFFFFFFFFULL));
	wr32(d->mmio, VIRTIO_MMIO_QUEUE_DRV_HI, (UW)((ring_pa + avail_off) >> 32));
	wr32(d->mmio, VIRTIO_MMIO_QUEUE_DEV_LO,
	     (UW)((ring_pa + used_off) & 0xFFFFFFFFULL));
	wr32(d->mmio, VIRTIO_MMIO_QUEUE_DEV_HI, (UW)((ring_pa + used_off) >> 32));
	hal_dma_barrier();
	wr32(d->mmio, VIRTIO_MMIO_QUEUE_READY, 1);
	return ER_OK;
}

/* ------------------------------------------------------------------ */
/* リングの出し入れ                                                    */
/* ------------------------------------------------------------------ */

EXPORT void vq_offer(VirtQ *q, UW idx)
{
	/* avail の ring は [2] から始まる。添字は qsz の剰余（リングなので
	 * idx 自体は 65536 で回り、剰余を取って位置を決める）。 */
	q->avail[2 + (q->avail_idx % q->qsz)] = (UH)idx;
	q->avail_idx++;
	/* 記述子とリング要素を書いてから idx を見せる。順序が逆だと
	 * 装置が中身の無い要素を読む。 */
	hal_dma_barrier();
	q->avail[1] = q->avail_idx;
}

EXPORT void vq_kick(CONST VirtioDev *d, VirtQ *q)
{
	hal_dma_barrier();
	wr32(d->mmio, VIRTIO_MMIO_QUEUE_NOTIFY, q->index);
}

EXPORT Bool vq_take(VirtQ *q, UW *idx, UW *len)
{
	UH devidx = q->used[1];
	CONST VqUsedElem *e;

	if (devidx == q->last_used) {
		return False;
	}
	/* idx を読んでから要素を読む。 */
	hal_dma_barrier();
	e = &q->used_ring[q->last_used % q->qsz];
	if (idx != 0) {
		*idx = e->id;
	}
	if (len != 0) {
		*len = e->len;
	}
	q->last_used++;
	return True;
}

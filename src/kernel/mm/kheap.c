/*
 * kheap.c — カーネルヒープ（可変長の内部確保）
 *
 * **これはカーネル自身のためのもので、アプリ向けではない。**
 * BTRON3 §1.5 はこう書いている。
 *
 *   「基本的にはブロック単位 ( 例えば 4KB 単位 ) でメモリを割り当てる
 *     機能のみを OS は提供する。さらに細かく内部を分割するのは
 *     ライブラリ、またはアプリケーション自身で行う」
 *
 * アプリ側は `get_mbk` でブロックを取り、ライブラリが細かく刻む。
 * カーネルも同じ構図で、ページアロケータからページを取って自分で刻む。
 * これがその「ライブラリ」側に当たる。
 *
 * 【方式】ページの塊（アリーナ）を必要に応じて増やし、各アリーナの中を
 * 双方向リストの first fit で刻む。解放時に前後の空きと融合する。
 * 断片化はするが、カーネル内部の確保は数が少なく寿命も長いので十分。
 *
 * 【アリーナを跨がない】1 つの確保が 2 つのアリーナにまたがることはない。
 * 連続した物理ページで取っているので論理的には繋げられるが、
 * 解放の判定が面倒になるだけで得が無い。
 */

#include <btron/errcode.h>
#include <btron/hal.h>
#include <btron/intc.h>
#include <btron/vmem.h>

typedef struct Chunk {
	struct Chunk *next;
	struct Chunk *prev;
	UD size;  /* ヘッダを含まない利用可能バイト数 */
	UD inuse; /* 0 / 1。8 バイト整列を保つため UD にしてある */
} Chunk;

typedef struct Arena {
	struct Arena *next;
	UD pa;     /* 物理先頭。返すときに要る */
	UW npages;
	UW resv;
	Chunk *head;
} Arena;

#define HDRSZ ((UD)sizeof(Chunk))
#define MIN_SPLIT 32U

LOCAL Arena *arenas;
LOCAL UD used_bytes;
LOCAL UD total_bytes;

/* 新しいアリーナを足す。need は利用者が欲しいバイト数。 */
LOCAL Arena *grow(UD need)
{
	UD ps = (UD)hal_page_size();
	UD want = (UD)sizeof(Arena) + HDRSZ + need;
	UW npages;
	UD pa;
	Arena *a;
	Chunk *c;

	/* 最低 4 ページ。小さく刻んで何度も伸ばすより良い。 */
	npages = (UW)((want + ps - 1) / ps);
	if (npages < 4) {
		npages = 4;
	}
	pa = pmem_alloc(npages);
	if (pa == 0) {
		return 0;
	}
	a = (Arena *)(VP)BTRON_PA_TO_VA(pa);
	a->pa = pa;
	a->npages = npages;
	a->resv = 0;

	c = (Chunk *)(VP)((UD)(VP)a + sizeof(Arena));
	c->next = 0;
	c->prev = 0;
	c->size = (UD)npages * ps - sizeof(Arena) - HDRSZ;
	c->inuse = 0;
	a->head = c;

	a->next = arenas;
	arenas = a;
	total_bytes += (UD)npages * ps;
	return a;
}

LOCAL VP carve(Arena *a, UD size)
{
	Chunk *c;

	for (c = a->head; c != 0; c = c->next) {
		if (c->inuse != 0 || c->size < size) {
			continue;
		}
		if (c->size >= size + HDRSZ + MIN_SPLIT) {
			Chunk *n = (Chunk *)(VP)((UD)(VP)c + HDRSZ + size);

			n->size = c->size - size - HDRSZ;
			n->inuse = 0;
			n->next = c->next;
			n->prev = c;
			if (c->next != 0) {
				c->next->prev = n;
			}
			c->next = n;
			c->size = size;
		}
		c->inuse = 1;
		used_bytes += c->size;
		return (VP)((UD)(VP)c + HDRSZ);
	}
	return 0;
}

EXPORT VP kmalloc(UD size)
{
	UD state;
	Arena *a;
	VP p = 0;

	if (size == 0) {
		return 0;
	}
	size = (size + 7U) & ~7U;

	state = hal_int_disable();
	for (a = arenas; a != 0 && p == 0; a = a->next) {
		p = carve(a, size);
	}
	if (p == 0) {
		a = grow(size);
		if (a != 0) {
			p = carve(a, size);
		}
	}
	hal_int_restore(state);
	return p;
}

EXPORT void kfree(VP ptr)
{
	UD state;
	Chunk *c;

	if (ptr == 0) {
		return;
	}
	state = hal_int_disable();
	c = (Chunk *)(VP)((UD)ptr - HDRSZ);
	if (c->inuse == 0) {
		hal_int_restore(state); /* 二重解放。黙って無視する */
		return;
	}
	c->inuse = 0;
	used_bytes -= c->size;

	/* 後ろと融合 */
	if (c->next != 0 && c->next->inuse == 0) {
		Chunk *n = c->next;

		c->size += HDRSZ + n->size;
		c->next = n->next;
		if (n->next != 0) {
			n->next->prev = c;
		}
	}
	/* 前と融合 */
	if (c->prev != 0 && c->prev->inuse == 0) {
		Chunk *pv = c->prev;

		pv->size += HDRSZ + c->size;
		pv->next = c->next;
		if (c->next != 0) {
			c->next->prev = pv;
		}
	}
	/* アリーナ自体は返さない。
	 * 全部空いたかの判定にアリーナの走査が要るうえ、すぐまた要求が来る。
	 * 返すべきかは使用実態を見てから決める（D9 と一緒に見直す）。 */
	hal_int_restore(state);
}

EXPORT UD kheap_used(void)
{
	return used_bytes;
}

EXPORT UD kheap_total(void)
{
	return total_bytes;
}

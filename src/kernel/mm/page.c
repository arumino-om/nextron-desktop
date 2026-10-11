/*
 * page.c — 物理ページの管理（アーキ中立）
 *
 * docs/design-issues.md A13 / A14 / §1.5 の土台。
 *
 * 起動初期のバンプアロケータ (pmem.c) と違い**解放できる**。
 * BootInfo のメモリマップから `BMEM_FREE` の領域を取り込む。
 *
 * 【ビットマップにした理由】
 * 連続した複数ページを配れる必要がある。BTRON3 §1.5 の
 * `get_mbk(adr, nblk, atr)` が「nblk 個の**連続した**メモリブロック」を
 * 求めていて、獲得したブロックは連続した論理アドレスを持つと規定している。
 * 空きリストだけでは連続を探せないので、1 ページ 1 ビットの表を持つ。
 *
 * 512MB・4KB ページなら 16KB の表で済む。64GB でも 2MB。
 * バディにするのは断片化が問題になってからでよい（D9 と一緒に見直す）。
 *
 * 【穴がある】物理アドレス空間は連続していない。MMIO やファームウェアの
 * 領域が挟まる。最小〜最大の全域に表を張り、使えない部分は最初から
 * 「使用中」にしておく。表は少し無駄になるが、添字の計算が
 * `(pa - base) >> shift` だけで済む。
 */

#include <btron/bootinfo.h>
#include <btron/errcode.h>
#include <btron/hal.h>
#include <btron/intc.h>
#include <btron/vmem.h>

LOCAL UD pm_base;   /* 表が覆う物理範囲の先頭（ページ境界） */
LOCAL UD pm_pages;  /* 表が覆うページ数 */
LOCAL UW *pm_map;   /* 1 ページ 1 ビット。1 = 使用中 */
LOCAL UD pm_total;  /* 使える総ページ数 */
LOCAL UD pm_free;   /* 空きページ数 */
LOCAL UD pm_hint;   /* 次に探し始める添字 */
LOCAL Bool pm_ready = False;

#define BITS_PER_WORD 32U

LOCAL inline Bool bit_get(UD i)
{
	return ((pm_map[i / BITS_PER_WORD] >> (UW)(i % BITS_PER_WORD)) & 1U) != 0 ? True
	                                                                         : False;
}

LOCAL inline void bit_set(UD i)
{
	pm_map[i / BITS_PER_WORD] |= 1U << (UW)(i % BITS_PER_WORD);
}

LOCAL inline void bit_clr(UD i)
{
	pm_map[i / BITS_PER_WORD] &= ~(1U << (UW)(i % BITS_PER_WORD));
}

LOCAL inline UD idx_of(UD pa)
{
	return (pa - pm_base) >> hal_page_shift();
}

LOCAL inline UD pa_of(UD i)
{
	return pm_base + (i << hal_page_shift());
}

/* ------------------------------------------------------------------ */
/* 立ち上げ                                                            */
/* ------------------------------------------------------------------ */

/* [lo, hi) を空きとして開放する。範囲は表の中に収める。 */
LOCAL void release_range(UD lo, UD hi)
{
	UD i;

	if (lo < pm_base) {
		lo = pm_base;
	}
	if (hi > pm_base + (pm_pages << hal_page_shift())) {
		hi = pm_base + (pm_pages << hal_page_shift());
	}
	for (i = idx_of(lo); i < idx_of(hi) && i < pm_pages; i++) {
		if (bit_get(i)) {
			bit_clr(i);
			pm_total++;
			pm_free++;
		}
	}
}

/* [lo, hi) を使用中にする。既に使用中なら数えない。 */
LOCAL void reserve_range(UD lo, UD hi)
{
	UD i;

	if (lo < pm_base) {
		lo = pm_base;
	}
	if (hi > pm_base + (pm_pages << hal_page_shift())) {
		hi = pm_base + (pm_pages << hal_page_shift());
	}
	if (hi <= lo) {
		return;
	}
	for (i = idx_of(lo); i < idx_of(hi + (UD)hal_page_size() - 1) && i < pm_pages;
	     i++) {
		if (!bit_get(i)) {
			bit_set(i);
			if (pm_total > 0) {
				pm_total--;
			}
			if (pm_free > 0) {
				pm_free--;
			}
		}
	}
}

EXPORT ERR pmem_init(void)
{
	UD ps = (UD)hal_page_size();
	UD lo = ~0ULL;
	UD hi = 0;
	UD map_bytes;
	UD i;
	UW n;

	if (pm_ready) {
		return E_OK;
	}
	if (bootinfo_mem_count() == 0) {
		/* メモリマップが無い（QEMU の -kernel 直接起動）。
		 * どこまで RAM があるか分からないので本アロケータは立てない。
		 * 起動初期のバンプアロケータだけで動き続ける。 */
		return E_NOEXS;
	}

	/* 表が覆う範囲を決める。使える領域の最小〜最大。 */
	for (n = 0; n < bootinfo_mem_count(); n++) {
		CONST BootMemRegion *r = bootinfo_mem(n);

		if (r->type == BMEM_UNUSABLE) {
			continue;
		}
		if (r->base < lo) {
			lo = r->base;
		}
		if (r->base + r->size > hi) {
			hi = r->base + r->size;
		}
	}
	if (lo >= hi) {
		return E_NOEXS;
	}
	pm_base = lo & ~(ps - 1);
	pm_pages = (hi - pm_base + ps - 1) / ps;

	/* 表そのものは起動初期のプールから取る。解放しないので構わない。 */
	map_bytes = (pm_pages + 7) / 8;
	map_bytes = (map_bytes + ps - 1) & ~(ps - 1);
	{
		UD first = 0;
		UD pages = map_bytes / ps;

		for (i = 0; i < pages; i++) {
			UD pa = pmem_early_alloc_page();

			if (pa == 0) {
				return E_NOMEM;
			}
			if (i == 0) {
				first = pa;
			} else if (pa != first + i * ps) {
				return E_SYS; /* バンプアロケータは連続を返すはず */
			}
		}
		pm_map = (UW *)(VP)BTRON_PA_TO_VA(first);
	}
	/* 最初は全部「使用中」。空き領域だけを開放していく。 */
	for (i = 0; i < map_bytes / 4; i++) {
		pm_map[i] = 0xFFFFFFFFU;
	}
	pm_total = 0;
	pm_free = 0;

	for (n = 0; n < bootinfo_mem_count(); n++) {
		CONST BootMemRegion *r = bootinfo_mem(n);

		if (r->type == BMEM_FREE) {
			release_range(r->base, r->base + r->size);
		}
	}

	/* 自分が載っている場所を取り返す。
	 *
	 * **カーネル本体と起動初期のプールは BMEM_FREE に入っていることがある。**
	 * ローダは自分が確保した分を BMEM_LOADER にするが、カーネルが
	 * 直接起動で自分の後ろを勝手にプールにした場合などは印が付かない。
	 * 二重に配らないよう、必ずここで押さえる。 */
	reserve_range(hal_kernel_pa_base(), hal_kernel_pa_end());
	reserve_range(pmem_early_base(), pmem_early_limit());

	pm_hint = 0;
	pm_ready = True;
	return E_OK;
}

/* ------------------------------------------------------------------ */
/* 確保と解放                                                          */
/* ------------------------------------------------------------------ */

LOCAL void zero_pages(UD pa, UW npages)
{
	UD words = ((UD)npages << hal_page_shift()) / 8;
	UD *p = (UD *)(VP)BTRON_PA_TO_VA(pa);
	UD i;

	for (i = 0; i < words; i++) {
		p[i] = 0;
	}
}

EXPORT UD pmem_alloc(UW npages)
{
	UD state;
	UD start;
	UD i;
	UD run;
	UD pa = 0;
	W pass;

	if (!pm_ready || npages == 0) {
		return 0;
	}
	state = hal_int_disable();

	/* ヒントの位置から 1 周する。2 周目は先頭から。 */
	for (pass = 0; pass < 2 && pa == 0; pass++) {
		start = (pass == 0) ? pm_hint : 0;
		run = 0;
		for (i = start; i < pm_pages; i++) {
			if (bit_get(i)) {
				run = 0;
				continue;
			}
			run++;
			if (run < npages) {
				continue;
			}
			{
				UD first = i + 1 - npages;
				UD k;

				for (k = 0; k < npages; k++) {
					bit_set(first + k);
				}
				pm_free -= npages;
				pm_hint = i + 1;
				pa = pa_of(first);
			}
			break;
		}
	}
	hal_int_restore(state);
	if (pa != 0) {
		zero_pages(pa, npages);
	}
	return pa;
}

EXPORT void pmem_free(UD pa, UW npages)
{
	UD state;
	UD first;
	UD k;

	if (!pm_ready || npages == 0 || !pmem_owns(pa)) {
		return;
	}
	state = hal_int_disable();
	first = idx_of(pa);
	for (k = 0; k < npages && first + k < pm_pages; k++) {
		if (bit_get(first + k)) {
			bit_clr(first + k);
			pm_free++;
		}
	}
	if (first < pm_hint) {
		pm_hint = first; /* 空いたところから探し直す */
	}
	hal_int_restore(state);
}

EXPORT Bool pmem_owns(UD pa)
{
	UD ps = (UD)hal_page_size();

	if (!pm_ready || pa < pm_base || (pa & (ps - 1)) != 0) {
		return False;
	}
	return (idx_of(pa) < pm_pages) ? True : False;
}

EXPORT UD pmem_total_pages(void)
{
	return pm_total;
}

EXPORT UD pmem_free_pages(void)
{
	return pm_free;
}

/*
 * mmu.c — 仮想メモリ (x86_64)
 *
 * docs/design-issues.md §3.2 のアドレス空間レイアウトを x86_64 で実装する。
 *
 * arm64 と違い CR3 は 1 本しかないので、共有側（上位半分）を全プロセスの
 * PML4 に持たせる必要がある。ただし 1 PML4 エントリが 512GB を支配するため、
 * 上位 256 エントリ分の PDPT を起動時に全数確保しておけば、以後
 * 最上段エントリの値は変化しない (§3.2-4 / 規律 R4)。
 * プロセス生成時に複製するのは 2KB のポインタ値だけで、その下の
 * PDPT / PD / PT と実メモリは 1 組しか存在しない。
 *
 * 4 段ページング。4KB ページ (A13)。
 *   PML4 bits[47:39]  PDPT bits[38:30]  PD bits[29:21]  PT bits[20:12]
 *
 * 【状態】ロジックは完全だが、x86_64 のブート経路（UEFI / multiboot）が
 *         まだ無いため未検証。arm64 と同一のインタフェースで型検査のみ
 *         通してある。ブート経路ができ次第 QEMU で確認する。
 */

#include <btron/hal.h>
#include <btron/vmem.h>

/* ------------------------------------------------------------------ */
/* エントリ                                                            */
/* ------------------------------------------------------------------ */

#define E_P (1ULL << 0)   /* Present */
#define E_RW (1ULL << 1)  /* 書き込み可 */
#define E_US (1ULL << 2)  /* ユーザ特権からアクセス可 */
#define E_PWT (1ULL << 3) /* Write-Through */
#define E_PCD (1ULL << 4) /* Cache Disable */
#define E_A (1ULL << 5)   /* Accessed */
#define E_D (1ULL << 6)   /* Dirty */
#define E_PS (1ULL << 7)  /* Page Size (PDPT/PD で大ページ) */
#define E_G (1ULL << 8)   /* Global */
#define E_NX (1ULL << 63) /* 実行禁止 */

#define E_ADDR_MASK 0x000FFFFFFFFFF000ULL

#define PAGE_SHIFT 12U
#define PTE_PER_TABLE 512U

/* 上位半分 = PML4[256..511]。512GB × 256 = 128TB (§3.2-1) */
#define PML4_SHARED_FIRST 256U
#define PML4_ENTRIES 512U

/* CR4.PCIDE が有効なときだけ PCID を使う */
#define CR4_PCIDE (1ULL << 17)

/* arm64 と同じ形でリンカスクリプトが .rodata の中に値として置く。
 * x86_64 のブート経路を作るときに同じ約束で用意すること。 */
IMPORT CONST UD __kernel_vma_base;
IMPORT CONST UD __kernel_lma_base;

IMPORT B __kernel_start[];
IMPORT B __kernel_end[];

LOCAL AddrSpace kernel_space;
LOCAL Bool vm_on = False;
LOCAL Bool pcid_on = False;
LOCAL UW next_pcid = 1;

/* ------------------------------------------------------------------ */
/* 補助                                                                */
/* ------------------------------------------------------------------ */

LOCAL inline void write_cr3(UD v)
{
	__asm__ volatile("mov %0, %%cr3" : : "r"(v) : "memory");
}

LOCAL inline UD read_cr4(void)
{
	UD v;
	__asm__ volatile("mov %%cr4, %0" : "=r"(v));
	return v;
}

/* カーネルイメージの大きさ。
 *
 * 【重要】C から取ったシンボルのアドレスは実行している段によって変わる。
 * 物理で走っている第 1 段では物理アドレス、上位半分へ移った第 2 段では
 * VMA になる。差分だけは段によらず一定。詳細は arm64 側の同名関数。 */
LOCAL inline UD kernel_size(void)
{
	return (UD)(VP)__kernel_end - (UD)(VP)__kernel_start;
}

EXPORT UD hal_kernel_pa_base(void)
{
	return __kernel_lma_base;
}

EXPORT UD hal_kernel_pa_end(void)
{
	return __kernel_lma_base + kernel_size();
}

EXPORT UD hal_kva_to_pa(UD kva)
{
	return kva - __kernel_vma_base + __kernel_lma_base;
}

LOCAL inline UD *table_ptr(UD pa)
{
	/* UEFI から渡された時点で恒等写像が張られている。自前のテーブルに
	 * 切り替えた後は直接マップ上から見る。arm64 と同じ扱い。 */
	return (UD *)(VP)(vm_on ? BTRON_PA_TO_VA(pa) : pa);
}

LOCAL inline UW idx_of(UD va, UW level)
{
	/* level 0 = PML4, 3 = PT */
	UW shift = PAGE_SHIFT + (3U - level) * 9U;
	return (UW)((va >> shift) & 0x1FFULL);
}

LOCAL UD attr_to_entry(UW attr)
{
	UD e = E_P | E_A;

	if (attr & BTRON_PG_WRITE) {
		e |= E_RW | E_D;
	}
	if (attr & BTRON_PG_USER) {
		e |= E_US;
	}
	if (attr & BTRON_PG_GLOBAL) {
		e |= E_G;
	}
	if (attr & BTRON_PG_DEVICE) {
		e |= E_PCD | E_PWT; /* キャッシュ無効 */
	}
	if (!(attr & BTRON_PG_EXEC)) {
		e |= E_NX;
	}
	return e;
}

LOCAL UD *walk(UD root_pa, UD va, Bool create)
{
	UD *tbl = table_ptr(root_pa);
	UW level;

	for (level = 0; level < 3; level++) {
		UD *e = &tbl[idx_of(va, level)];

		if (!(*e & E_P)) {
			UD next;
			if (!create) {
				return 0;
			}
			next = pmem_early_alloc_page();
			if (next == 0) {
				return 0;
			}
			/* 中間段は許可を絞らない。最終段の権限が実効値になる。 */
			*e = (next & E_ADDR_MASK) | E_P | E_RW | E_US;
		}
		tbl = table_ptr(*e & E_ADDR_MASK);
	}
	return &tbl[idx_of(va, 3)];
}

/* ------------------------------------------------------------------ */
/* 公開インタフェース                                                  */
/* ------------------------------------------------------------------ */

EXPORT ERR hal_vm_map(AddrSpace *as, UD va, UD pa, UD size, UW attr)
{
	UD ps = (UD)hal_page_size();
	UD ent = attr_to_entry(attr);
	UD off;

	if ((va | pa | size) & (ps - 1)) {
		return -1;
	}
	for (off = 0; off < size; off += ps) {
		UD *e = walk(as->root_pa, va + off, True);
		if (e == 0) {
			return -1;
		}
		*e = ((pa + off) & E_ADDR_MASK) | ent;
	}
	return 0;
}

EXPORT UD hal_vm_translate(CONST AddrSpace *as, UD va)
{
	UD ps = (UD)hal_page_size();
	UD *e = walk(as->root_pa, va, False);

	if (e == 0 || !(*e & E_P)) {
		return 0;
	}
	return (*e & E_ADDR_MASK) | (va & (ps - 1));
}

EXPORT ERR hal_vm_create(AddrSpace *as)
{
	UD *dst;
	CONST UD *src;
	UW i;

	as->root_pa = pmem_early_alloc_page();
	if (as->root_pa == 0) {
		return -1;
	}

	/* §3.2-4: 共有側（上位 256 エントリ = 2KB）を複製する。
	 * 複製されるのは最上段のポインタ値だけで、その下の PDPT / PD / PT と
	 * 実メモリは 1 組しか存在しない。したがって共有領域への追加マップは
	 * 全プロセスに即座に見え、伝播作業は発生しない。
	 *
	 * これが成立するのは hal_vm_init() が上位 256 エントリを起動時に
	 * 全数確保し、以後その値を変えないから（規律 R4）。 */
	dst = table_ptr(as->root_pa);
	src = table_ptr(kernel_space.root_pa);
	for (i = PML4_SHARED_FIRST; i < PML4_ENTRIES; i++) {
		dst[i] = src[i];
	}

	as->asid = pcid_on ? next_pcid++ : 0;
	as->resv = 0;
	return 0;
}

EXPORT void hal_vm_destroy(AddrSpace *as)
{
	as->root_pa = 0;
	as->asid = 0;
}

EXPORT void hal_vm_drop_identity(void)
{
	/* CR3 は 1 本しかないので、恒等写像は共有側と同じ木の下位半分に
	 * 入っている。下位 256 エントリを空にすれば下位半分は丸ごと空く。
	 *
	 * arm64 は TTBR0 を空の表に向け替えるだけで済む。ここが構造差。 */
	UD *pml4 = table_ptr(kernel_space.root_pa);
	UW i;

	for (i = 0; i < PML4_SHARED_FIRST; i++) {
		pml4[i] = 0;
	}
	/* PCID を使っていても、この木の TLB は捨てなければならない。
	 * bit63 を立てずに書くと当該 PCID のエントリが無効化される。 */
	write_cr3(kernel_space.root_pa & E_ADDR_MASK);
}

EXPORT void hal_vm_switch(CONST AddrSpace *as)
{
	UD cr3 = as->root_pa & E_ADDR_MASK;

	if (pcid_on) {
		/* PCID を付けると TLB を流さずに切り替えられる (§3.2-4)。
		 * bit63 を立てると既存の TLB エントリを保持する。 */
		cr3 |= ((UD)as->asid & 0xFFFULL) | (1ULL << 63);
	}
	write_cr3(cr3);
}

EXPORT Bool hal_vm_enabled(void)
{
	return vm_on;
}

EXPORT CONST AddrSpace *hal_vm_kernel_space(void)
{
	return &kernel_space;
}

/* ------------------------------------------------------------------ */
/* 起動時の初期化                                                      */
/*                                                                     */
/* 未検証。x86_64 のブート経路（UEFI / multiboot）ができてから確認する。 */
/* ------------------------------------------------------------------ */

EXPORT ERR hal_vm_init(void)
{
	UD ps = (UD)hal_page_size();
	/* この関数は物理で走る（第 1 段）。シンボルのアドレスは当てにできない。 */
	UD ksize = (kernel_size() + ps - 1) & ~(ps - 1);
	UD kvma = __kernel_vma_base;
	UD kstart = __kernel_lma_base;
	UD *pml4;
	UW i;

	if (kvma != BTRON_KERNEL_BASE) {
		return -1;
	}

	pcid_on = (read_cr4() & CR4_PCIDE) != 0;

	kernel_space.root_pa = pmem_early_alloc_page();
	if (kernel_space.root_pa == 0) {
		return -1;
	}
	kernel_space.asid = 0;

	/* 規律 R4: 上位 256 エントリ分の PDPT を起動時に全数確保する。
	 * 256 × 4KB = 1MB を払って、以後の伝播問題を買い取る。
	 * これをやらないと、共有領域が新しい 512GB 区画に初めて届いたときに
	 * 全プロセスの PML4 を走査して書き換える必要が生じる。 */
	pml4 = table_ptr(kernel_space.root_pa);
	for (i = PML4_SHARED_FIRST; i < PML4_ENTRIES; i++) {
		UD t = pmem_early_alloc_page();
		if (t == 0) {
			return -1;
		}
		pml4[i] = (t & E_ADDR_MASK) | E_P | E_RW;
	}

	/* 物理メモリの直接マップ。ページテーブル自身が置かれている早期プールを
	 * 必ず含めること。切替後 table_ptr() は直接マップ経由でテーブルを読む。 */
	{
		UD dm_start = kstart;
		UD dm_end = pmem_early_limit();

		if (pmem_early_base() < dm_start) {
			dm_start = pmem_early_base();
		}
		if (kstart + ksize > dm_end) {
			dm_end = kstart + ksize;
		}
		dm_start &= ~(ps - 1);
		dm_end = (dm_end + ps - 1) & ~(ps - 1);

		if (hal_vm_map(&kernel_space, BTRON_PA_TO_VA(dm_start), dm_start,
		               dm_end - dm_start,
		               BTRON_PG_READ | BTRON_PG_WRITE | BTRON_PG_EXEC
		                   | BTRON_PG_GLOBAL) != 0) {
			return -1;
		}
	}
	/* カーネル本体を上位半分 (BTRON_KERNEL_BASE) に写像する。 */
	if (hal_vm_map(&kernel_space, kvma, kstart, ksize,
	               BTRON_PG_READ | BTRON_PG_WRITE | BTRON_PG_EXEC
	                   | BTRON_PG_GLOBAL) != 0) {
		return -1;
	}
	/* CR3 を切り替えた瞬間に PC が無効にならないよう恒等写像も張る。
	 * 上位半分へジャンプした後 hal_vm_drop_identity() で捨てる。 */
	if (hal_vm_map(&kernel_space, kstart, kstart, ksize,
	               BTRON_PG_READ | BTRON_PG_WRITE | BTRON_PG_EXEC) != 0) {
		return -1;
	}

	write_cr3(kernel_space.root_pa & E_ADDR_MASK);
	vm_on = True;
	return 0;
}

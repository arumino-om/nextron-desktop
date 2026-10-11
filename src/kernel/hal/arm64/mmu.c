/*
 * mmu.c — 仮想メモリ (arm64)
 *
 * docs/design-issues.md §3.2 のアドレス空間レイアウトを arm64 で実装する。
 *
 * arm64 は TTBR0_EL1 / TTBR1_EL1 の 2 本を持つので、
 * 「下位半分 = プロセス固有、上位半分 = 共有」がハードウェアで分かれる。
 * 共有側は複製も同期も不要で、切替時は TTBR0 だけを書けばよい。
 *
 * TCR_EL1.T0SZ = T1SZ = 17 とすることで、各半分が 2^47 = 128TB となり、
 * x86_64 のカノニカル分割（上位半分の基底 0xFFFF_8000_0000_0000）と
 * 完全に一致する (§3.2-2)。アドレス定数の #ifdef 分岐が要らなくなる。
 *
 * 4KB granule (A13)。4 段。
 *   L0 bits[46:39]  L1 bits[38:30]  L2 bits[29:21]  L3 bits[20:12]
 * T0SZ=17 なので L0 の有効エントリ数は 512 ではなく 256 になる。
 */

#include <btron/hal.h>
#include <btron/vmem.h>

/* ------------------------------------------------------------------ */
/* 記述子                                                              */
/* ------------------------------------------------------------------ */

#define D_VALID (1ULL << 0)
#define D_TABLE (1ULL << 1) /* L0-L2 で立てるとテーブル、L3 では常に 1 */
#define D_PAGE (1ULL << 1)  /* L3 のページ記述子 */

#define D_ATTRIDX(n) (((UD)(n) & 7ULL) << 2)
#define D_NS (1ULL << 5)
#define D_AP_RW_EL1 (0ULL << 6)  /* EL1 のみ RW */
#define D_AP_RW_ALL (1ULL << 6)  /* EL0 も RW */
#define D_AP_RO_EL1 (2ULL << 6)  /* EL1 のみ RO */
#define D_AP_RO_ALL (3ULL << 6)  /* EL0 も RO */
#define D_SH_INNER (3ULL << 8)
#define D_AF (1ULL << 10) /* アクセスフラグ。立てないとアクセス時に例外 */
#define D_PXN (1ULL << 53)
#define D_UXN (1ULL << 54)

#define D_ADDR_MASK 0x0000FFFFFFFFF000ULL

/* MAIR_EL1 のインデックス */
#define MAIR_NORMAL 0 /* Normal, Inner/Outer Write-Back non-transient */
#define MAIR_DEVICE 1 /* Device-nGnRnE */
#define MAIR_VALUE ((0xFFULL << (8 * MAIR_NORMAL)) | (0x00ULL << (8 * MAIR_DEVICE)))

#define T0SZ 17ULL /* §3.2-2 */
#define T1SZ 17ULL

#define PAGE_SHIFT 12U
#define PTE_PER_TABLE 512U

/* L0 は T0SZ=17 により 256 エントリ。上位半分の確保量 (規律 R4) もこれで決まる。 */
#define L0_ENTRIES (PTE_PER_TABLE >> (T0SZ - 16ULL))

/* リンカスクリプトが提供する。__kernel_* は VMA（上位半分）。 */
IMPORT B __kernel_start[];
IMPORT B __kernel_end[];
/* リンカスクリプトが .rodata 内に置いた 64bit の値。
 * 絶対シンボルではなくデータなので adrp の射程に入る。 */
IMPORT CONST UD __kernel_vma_base;
IMPORT CONST UD __kernel_lma_base;

#define PL011_PA 0x09000000ULL

LOCAL AddrSpace kernel_space; /* 上位半分 (TTBR1) */
LOCAL UD empty_root;          /* 恒等写像を捨てた後に TTBR0 が指す空の表 */
LOCAL Bool vm_on = False;
LOCAL UW next_asid = 1;

/* ------------------------------------------------------------------ */
/* 補助                                                                */
/* ------------------------------------------------------------------ */

/* カーネルイメージの大きさ。
 *
 * 【重要】C から取ったシンボルのアドレスは、実行している段によって値が
 * 変わる。clang は adrp/add で求めるので、物理で走っている第 1 段では
 * 物理アドレス、上位半分へ移った第 2 段では VMA になる。
 * 差分だけは段によらず一定なので、大きさはこれで求める。
 * 基準アドレスの方はリンカが .rodata に置いた値を読む。 */
LOCAL inline UD kernel_size(void)
{
	return (UD)(VP)__kernel_end - (UD)(VP)__kernel_start;
}

/* カーネルイメージが実際に置かれた物理先頭。0 なら未設定で、
 * リンカスクリプトの LMA を使う。 */
LOCAL UD kernel_pa = 0;

EXPORT void hal_kernel_set_pa_base(UD pa)
{
	kernel_pa = (pa != 0) ? pa : __kernel_lma_base;
}

EXPORT UD hal_kernel_pa_base(void)
{
	return (kernel_pa != 0) ? kernel_pa : __kernel_lma_base;
}

EXPORT UD hal_kernel_pa_end(void)
{
	return hal_kernel_pa_base() + kernel_size();
}

EXPORT UD hal_kva_to_pa(UD kva)
{
	return kva - __kernel_vma_base + hal_kernel_pa_base();
}

LOCAL inline UD *table_ptr(UD pa)
{
	/* MMU 有効化の前後でテーブルへの到達方法が変わる。
	 * 有効化前は恒等写像、有効化後は直接マップ上にある。 */
	return (UD *)(VP)(vm_on ? BTRON_PA_TO_VA(pa) : pa);
}

LOCAL inline UW idx_of(UD va, UW level)
{
	UW shift = PAGE_SHIFT + (3U - level) * 9U;
	/* L0 のインデックス幅は T0SZ / T1SZ で決まる。T*SZ=17 なら bits[46:39] の
	 * 8 ビットで、エントリ数は 256。bit47 まで含めて 9 ビットで取ると
	 * 上位半分（bit47=1）で 256 以上になり、表を飛び出してレベル 1 の
	 * 変換フォルトになる。
	 *
	 * ここが x86_64 との構造差。x86_64 の PML4 は bits[47:39] の 9 ビットで、
	 * 1 本の木の上位半分が 256..511 を使う。arm64 は TTBR0 / TTBR1 で木が
	 * 分かれるので、上位半分も自分の木の 0 番から始まる。 */
	UD mask = (level == 0) ? ((UD)L0_ENTRIES - 1) : 0x1FFULL;
	return (UW)((va >> shift) & mask);
}

LOCAL UD attr_to_desc(UW attr)
{
	UD d = D_VALID | D_PAGE | D_AF;

	if (attr & BTRON_PG_DEVICE) {
		d |= D_ATTRIDX(MAIR_DEVICE);
		/* デバイスメモリは投機実行させない */
		d |= D_PXN | D_UXN;
	} else {
		d |= D_ATTRIDX(MAIR_NORMAL) | D_SH_INNER;
	}

	if (attr & BTRON_PG_USER) {
		d |= (attr & BTRON_PG_WRITE) ? D_AP_RW_ALL : D_AP_RO_ALL;
	} else {
		d |= (attr & BTRON_PG_WRITE) ? D_AP_RW_EL1 : D_AP_RO_EL1;
		d |= D_UXN; /* EL1 専用のページを EL0 から実行させない */
	}

	if (!(attr & BTRON_PG_EXEC)) {
		d |= D_PXN | D_UXN;
	}
	return d;
}

/* va に対応する L3 エントリへのポインタを得る。途中のテーブルは必要なら作る。 */
LOCAL UD *walk(UD root_pa, UD va, Bool create)
{
	UD *tbl = table_ptr(root_pa);
	UW level;

	for (level = 0; level < 3; level++) {
		UD *e = &tbl[idx_of(va, level)];

		if (!(*e & D_VALID)) {
			UD next;
			if (!create) {
				return 0;
			}
			next = pmem_early_alloc_page();
			if (next == 0) {
				return 0;
			}
			*e = (next & D_ADDR_MASK) | D_VALID | D_TABLE;
		}
		tbl = table_ptr(*e & D_ADDR_MASK);
	}
	return &tbl[idx_of(va, 3)];
}

/* ------------------------------------------------------------------ */
/* 公開インタフェース                                                  */
/* ------------------------------------------------------------------ */

EXPORT ERR hal_vm_map(AddrSpace *as, UD va, UD pa, UD size, UW attr)
{
	UD ps = (UD)hal_page_size();
	UD desc = attr_to_desc(attr);
	UD off;

	if ((va | pa | size) & (ps - 1)) {
		return -1;
	}
	for (off = 0; off < size; off += ps) {
		UD *e = walk(as->root_pa, va + off, True);
		if (e == 0) {
			return -1;
		}
		*e = ((pa + off) & D_ADDR_MASK) | desc;
	}
	__asm__ volatile("dsb ishst" ::: "memory");
	return 0;
}

EXPORT UD hal_vm_translate(CONST AddrSpace *as, UD va)
{
	UD ps = (UD)hal_page_size();
	UD *e = walk(as->root_pa, va, False);

	if (e == 0 || !(*e & D_VALID)) {
		return 0;
	}
	return (*e & D_ADDR_MASK) | (va & (ps - 1));
}

EXPORT ERR hal_vm_create(AddrSpace *as)
{
	/* 下位半分 (TTBR0) のツリーを確保するだけでよい。
	 * 上位半分は TTBR1 が別に指しているので複製も同期も要らない。
	 * x86_64 ではここで上位 256 PML4 エントリ 2KB の複製が要る (§3.2-4)。 */
	as->root_pa = pmem_early_alloc_page();
	if (as->root_pa == 0) {
		return -1;
	}
	as->asid = next_asid++;
	as->resv = 0;
	return 0;
}

EXPORT void hal_vm_drop_identity(void)
{
	/* TTBR0 を空の表に向ける。これで下位半分には何も写像されていない状態になり、
	 * プロセスに明け渡せる。カーネルは上位半分 (TTBR1) で走り続ける。 */
	AddrSpace none;

	none.root_pa = empty_root;
	none.asid = 0;
	none.resv = 0;
	hal_vm_switch(&none);

	__asm__ volatile(
		"dsb ishst\n"
		"tlbi vmalle1\n"
		"dsb ish\n"
		"isb\n" ::: "memory");
}

EXPORT void hal_vm_destroy(AddrSpace *as)
{
	/* 起動初期のバンプアロケータは解放できない。
	 * 正式な物理メモリ管理ができてから実装する。 */
	as->root_pa = 0;
	as->asid = 0;
}

EXPORT void hal_vm_switch(CONST AddrSpace *as)
{
	UD ttbr0 = ((UD)as->asid << 48) | (as->root_pa & D_ADDR_MASK);

	__asm__ volatile(
		"dsb ishst\n"
		"msr ttbr0_el1, %0\n"
		"isb\n"
		:
		: "r"(ttbr0)
		: "memory");
	/* ASID が付いているので TLB を流す必要はない (§3.2-4)。 */
}

EXPORT Bool hal_vm_enabled(void)
{
	return vm_on;
}

/* ------------------------------------------------------------------ */
/* 起動時の初期化                                                      */
/* ------------------------------------------------------------------ */

/* 物理範囲を直接マップ領域に張る。範囲はページ境界に丸める。 */
LOCAL ERR map_direct(UD start, UD end)
{
	UD ps = (UD)hal_page_size();

	start &= ~(ps - 1);
	end = (end + ps - 1) & ~(ps - 1);
	if (end <= start) {
		return 0;
	}
	return hal_vm_map(&kernel_space, BTRON_PA_TO_VA(start), start, end - start,
	                  BTRON_PG_READ | BTRON_PG_WRITE | BTRON_PG_EXEC);
}

EXPORT ERR hal_vm_init(void)
{
	UD ps = (UD)hal_page_size();
	/* この関数は物理で走る（第 1 段）。シンボルのアドレスは当てにできない
	 * ので、基準はリンカが .rodata に置いた値、大きさは差分から取る。 */
	UD ksize = (kernel_size() + ps - 1) & ~(ps - 1);
	UD kvma = __kernel_vma_base;
	UD kvma_end = kvma + ksize;
	UD kstart = hal_kernel_pa_base();
	UD kend = kstart + ksize;
	AddrSpace low;
	UD mair = MAIR_VALUE;
	UD tcr;
	UD sctlr;
	UW i;
	UD *l0;

	/* リンカスクリプトの KERNEL_VMA と vmem.h の BTRON_KERNEL_BASE が
	 * 食い違っていると、以後の VMA <-> LMA 換算が全部狂う。先に落とす。 */
	if (__kernel_vma_base != BTRON_KERNEL_BASE) {
		return -1;
	}

	/* --- 上位半分 (TTBR1 / 共有) ----------------------------------- */
	kernel_space.root_pa = pmem_early_alloc_page();
	if (kernel_space.root_pa == 0) {
		return -1;
	}
	kernel_space.asid = 0;

	/* 規律 R4: 上位半分の最上段エントリを起動時に全数確保し、以後変更しない。
	 * これにより共有領域への追加マップは必ず 1 段下で起き、最上段には届かない。
	 * arm64 は TTBR1 が独立しているので複製の問題自体が起きないが、
	 * 「最上段を起動時に固定する」規律は x86_64 と揃えて守る。 */
	l0 = table_ptr(kernel_space.root_pa);
	for (i = 0; i < L0_ENTRIES; i++) {
		UD t = pmem_early_alloc_page();
		if (t == 0) {
			return -1;
		}
		l0[i] = (t & D_ADDR_MASK) | D_VALID | D_TABLE;
	}

	/* 物理メモリの直接マップ。
	 *
	 * 重要: ページテーブル自身が置かれている早期プールを必ず含めること。
	 * MMU を有効にした後、table_ptr() は直接マップ経由でテーブルを読む。
	 * プールが直接マップに無いと、最初のページウォークで変換フォルトになる。
	 *
	 * カーネルとプールは連続しているとは限らない（UEFI 経由だとローダが
	 * 別々の場所に確保しうる）。1 本に丸めると間の MMIO まで巻き込むので、
	 * 2 範囲として別々に張る。 */
	if (map_direct(kstart, kend) != 0) {
		return -1;
	}
	if (map_direct(pmem_early_base(), pmem_early_limit()) != 0) {
		return -1;
	}
	/* カーネル本体を上位半分 (BTRON_KERNEL_BASE) に写像する。
	 * ここへ移ることで TTBR0 を丸ごとプロセスに明け渡せるようになる。 */
	if (hal_vm_map(&kernel_space, kvma, kstart, kvma_end - kvma,
	               BTRON_PG_READ | BTRON_PG_WRITE | BTRON_PG_EXEC) != 0) {
		return -1;
	}

	/* MMIO 区画に UART を置く */
	if (hal_vm_map(&kernel_space, BTRON_MMIO_BASE + PL011_PA, PL011_PA, ps,
	               BTRON_PG_READ | BTRON_PG_WRITE | BTRON_PG_DEVICE) != 0) {
		return -1;
	}

	/* --- 下位半分 (TTBR0) ------------------------------------------
	 * MMU を有効にした瞬間、まだ物理アドレスで走っているので
	 * カーネルと UART を恒等写像しておく。上位半分へジャンプした後に
	 * hal_vm_drop_identity() でこれを捨てる。 */
	if (hal_vm_create(&low) != 0) {
		return -1;
	}
	if (hal_vm_map(&low, kstart, kstart, kend - kstart,
	               BTRON_PG_READ | BTRON_PG_WRITE | BTRON_PG_EXEC) != 0) {
		return -1;
	}
	if (hal_vm_map(&low, PL011_PA, PL011_PA, ps,
	               BTRON_PG_READ | BTRON_PG_WRITE | BTRON_PG_DEVICE) != 0) {
		return -1;
	}

	/* --- システムレジスタ ------------------------------------------ */
	tcr = (T0SZ << 0)      /* T0SZ: 下位半分 2^47 */
	      | (1ULL << 8)    /* IRGN0 = WB WA */
	      | (1ULL << 10)   /* ORGN0 = WB WA */
	      | (3ULL << 12)   /* SH0   = Inner shareable */
	      | (0ULL << 14)   /* TG0   = 4KB */
	      | (T1SZ << 16)   /* T1SZ: 上位半分 2^47 */
	      | (1ULL << 24)   /* IRGN1 = WB WA */
	      | (1ULL << 26)   /* ORGN1 = WB WA */
	      | (3ULL << 28)   /* SH1   = Inner shareable */
	      | (2ULL << 30);  /* TG1   = 4KB (TG0 と符号化が違う) */

	/* 物理アドレスサイズはハードウェアの申告に合わせる */
	{
		UD mmfr0;
		__asm__ volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(mmfr0));
		tcr |= ((mmfr0 & 0xFULL) << 32); /* IPS */
	}

	__asm__ volatile(
		"msr mair_el1, %0\n"
		"msr tcr_el1,  %1\n"
		"msr ttbr0_el1, %2\n"
		"msr ttbr1_el1, %3\n"
		"isb\n"
		:
		: "r"(mair), "r"(tcr),
		  "r"(((UD)low.asid << 48) | (low.root_pa & D_ADDR_MASK)),
		  "r"(kernel_space.root_pa & D_ADDR_MASK)
		: "memory");

	/* TLB を全部捨ててから有効化する */
	__asm__ volatile(
		"dsb ishst\n"
		"tlbi vmalle1\n"
		"dsb ish\n"
		"isb\n" ::: "memory");

	__asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
	sctlr |= (1ULL << 0)   /* M: MMU 有効 */
	         | (1ULL << 2) /* C: データキャッシュ有効 */
	         | (1ULL << 12); /* I: 命令キャッシュ有効 */
	__asm__ volatile(
		"msr sctlr_el1, %0\n"
		"isb\n"
		:
		: "r"(sctlr)
		: "memory");

	vm_on = True;

	/* 恒等写像を捨てるときに TTBR0 が指す空の表を用意しておく。
	 * MMU 有効後に確保するので、直接マップ経由でゼロクリアされる。 */
	empty_root = pmem_early_alloc_page();
	if (empty_root == 0) {
		return -1;
	}
	return 0;
}

/* 検証用に公開する */
EXPORT CONST AddrSpace *hal_vm_kernel_space(void)
{
	return &kernel_space;
}

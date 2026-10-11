/*
 * vmem.h — 仮想メモリのアーキ中立インタフェースとアドレス空間レイアウト
 *
 * docs/design-issues.md §3.2「アドレス空間レイアウト」の実装面。
 *
 *   §3.2-1  下位カノニカル半分 128TB = プロセス固有（正のアドレス）
 *           上位カノニカル半分 128TB = 共有（負のアドレス）
 *           BTRON3 §1.5「共有・システムメモリは負、ローカルは正」を満たす
 *   §3.2-2  両アーキで上位半分の基底を 0xFFFF_8000_0000_0000 に統一
 *           arm64 は TCR_EL1.T0SZ = T1SZ = 17
 *   §3.2-3  上位半分の区画は 512GB 境界に揃え固定アドレスとする
 *   §3.2-4  上位半分の最上段エントリは起動時に全数確保し以後不変（規律 R4）
 *   §3.2-5  アーキ差分は生成と切替の 2 関数に閉じる（規律 R3）
 */

#ifndef __BTRON_VMEM_H__
#define __BTRON_VMEM_H__

#include <btron/typedef.h>

/* ------------------------------------------------------------------ */
/* アドレス空間の分割 (§3.2-1 / §3.2-2)                                */
/*                                                                     */
/* 各半分 2^47 = 128TB。x86_64 の 48bit VA のカノニカル分割に合わせ、   */
/* arm64 は TCR_EL1.T0SZ = T1SZ = 17 でこれに揃える。                   */
/* ------------------------------------------------------------------ */

#define BTRON_VA_BITS 47 /* 片側のビット数 */

#define BTRON_USER_BASE 0x0000000000000000ULL   /* プロセス固有 128TB */
#define BTRON_USER_LIMIT 0x0000800000000000ULL  /* 上限（この値は含まない） */

#define BTRON_SHARED_BASE 0xFFFF800000000000ULL /* 共有 128TB */

/* ------------------------------------------------------------------ */
/* 上位半分の区画 (§3.2-3)                                             */
/*                                                                     */
/* すべて 512GB (= x86_64 の 1 PML4 エントリ) 境界に揃えてある。        */
/* 境界を揃えることで、最上段エントリの整数個に対応し、規律 R4          */
/* 「最上段エントリは起動時に全数確保し以後不変」を区画単位で守れる。   */
/* ------------------------------------------------------------------ */

#define BTRON_DIRECTMAP_BASE 0xFFFF800000000000ULL /* 32TB 物理メモリ直接マップ */
#define BTRON_COMMON_BASE 0xFFFFA00000000000ULL    /* 32TB 共有メモリ M_COMMON  */
#define BTRON_SYSTEM_BASE 0xFFFFC00000000000ULL    /* 16TB システムメモリ M_SYSTEM */
#define BTRON_MAPREC_BASE 0xFFFFD00000000000ULL    /*  8TB map_rec() の宛先     */
#define BTRON_MMIO_BASE 0xFFFFD80000000000ULL      /*  8TB MMIO / フレームバッファ */
#define BTRON_RESERVED_BASE 0xFFFFE00000000000ULL  /*      予約                  */
#define BTRON_KERNEL_BASE 0xFFFFFF0000000000ULL    /*      中心核 / 周辺核       */

#define BTRON_DIRECTMAP_SIZE 0x0000200000000000ULL /* 32TB。扱える物理メモリの上限 */

/* 物理アドレスを直接マップ上の仮想アドレスに直す */
#define BTRON_PA_TO_VA(pa) (BTRON_DIRECTMAP_BASE + (UD)(pa))

/* ------------------------------------------------------------------ */
/* ページ属性（アーキ中立）                                            */
/* ------------------------------------------------------------------ */

#define BTRON_PG_READ 0x0001U   /* 読み出し可 */
#define BTRON_PG_WRITE 0x0002U  /* 書き込み可 */
#define BTRON_PG_EXEC 0x0004U   /* 実行可 */
#define BTRON_PG_USER 0x0008U   /* ユーザ特権からアクセス可 */
#define BTRON_PG_DEVICE 0x0010U /* デバイスメモリ（MMIO。キャッシュ無効・投機不可） */
#define BTRON_PG_GLOBAL 0x0020U /* 全アドレス空間で共通（上位半分） */

/* ------------------------------------------------------------------ */
/* アドレス空間                                                        */
/*                                                                     */
/* 中身はアーキ共通にしておく。root は最上段テーブルの物理アドレスで、  */
/* arm64 では TTBR0_EL1 に、x86_64 では CR3 に入る値の元になる。        */
/* asid は arm64 の ASID / x86_64 の PCID。どちらもアドレス空間切替で   */
/* TLB を流さないための札である (§3.2-4)。                              */
/* ------------------------------------------------------------------ */

typedef struct AddrSpace {
	UD root_pa; /* 最上段テーブルの物理アドレス */
	UW asid;    /* arm64: ASID / x86_64: PCID */
	UW resv;
} AddrSpace;

/* ------------------------------------------------------------------ */
/* 規律 R3: アーキ依存のアドレス空間操作はこの 2 関数に閉じる           */
/*                                                                     */
/* 他の場所に CR3 / TTBR / PCID / ASID を書いてはならない。             */
/* ------------------------------------------------------------------ */

IMPORT ERR hal_vm_create(AddrSpace *as);
IMPORT void hal_vm_switch(CONST AddrSpace *as);

/* --- 以下は上記 2 関数を支える補助 ---------------------------------- */

/* 起動時に 1 度だけ呼ぶ。共有側（上位半分）のページテーブルを作り、
 * 規律 R4 に従って最上段エントリを全数確保したうえで MMU を有効にする。 */
IMPORT ERR hal_vm_init(void);

IMPORT void hal_vm_destroy(AddrSpace *as);

/* size はページサイズの倍数であること。va / pa も同様。 */
IMPORT ERR hal_vm_map(AddrSpace *as, UD va, UD pa, UD size, UW attr);

/* 検証・デバッグ用。対応付けが無ければ 0 を返す。 */
IMPORT UD hal_vm_translate(CONST AddrSpace *as, UD va);

/* 物理の MMIO 範囲を上位半分の MMIO 区画 (BTRON_MMIO_BASE) に張り、
 * 仮想アドレスを返す。失敗時 0。
 *
 * 別名は BTRON_MMIO_BASE + 物理アドレスで一意に決まるので、同じ範囲を
 * 二度張っても同じ値が返る。デバイスメモリ属性（キャッシュ無効・投機不可）
 * で張る。GIC・フレームバッファ・各種レジスタはこれを通す。
 *
 * この写像はカーネルの上位半分にあり、プロセスへの貸出とは独立に
 * 存在し続ける（規律 R18）。 */
IMPORT UD hal_vm_map_device(UD pa, UD size);

/* 現在 MMU が有効か */
IMPORT Bool hal_vm_enabled(void);

/* 共有側（上位半分）のアドレス空間。検証・デバッグ用。 */
IMPORT CONST AddrSpace *hal_vm_kernel_space(void);

/* カーネルの仮想アドレス（上位半分の VMA）を物理アドレスに直す。
 *
 * カーネルは上位半分 (BTRON_KERNEL_BASE) にリンクされ、物理 (LMA) に
 * ロードされる。リンカスクリプトが両方の基準を .rodata の中に値として
 * 置いてあり、これを読んで換算する。
 *
 * 【注意】引数は必ず VMA であること。物理アドレスを渡してはならない。
 * 起動第 1 段（物理で走っている間）に C からシンボルのアドレスを取ると
 * adrp は PC 相対なので物理アドレスが返る。それをここに通すと二重に
 * ずれる。第 1 段ではこの関数ではなく hal_kernel_pa_base/end() を使う。 */
IMPORT UD hal_kva_to_pa(UD kva);

/* カーネルイメージの物理範囲。
 *
 * シンボル間の差分と「置かれた物理先頭」から求めるので、物理で走って
 * いても上位半分で走っていても同じ値を返す。起動第 1 段で使う。 */
IMPORT UD hal_kernel_pa_base(void);
IMPORT UD hal_kernel_pa_end(void);

/* カーネルイメージが実際に置かれた物理先頭を知らせる。
 *
 * 起動第 1 段のいちばん最初に 1 度だけ呼ぶ。UEFI ローダはリンク時の
 * 物理アドレス (LMA) が空いていなければ別の場所へ置くので、そこを
 * BootInfo.kernel_pa から受け取って教える。
 * 0 を渡すとリンカスクリプトの LMA を使う（QEMU の -kernel 直接起動）。 */
IMPORT void hal_kernel_set_pa_base(UD pa);

/* 起動時の恒等写像を捨て、TTBR0 / 下位半分をプロセスに明け渡す。
 *
 * カーネルが上位半分で走り始めた後に 1 度だけ呼ぶ。これ以降
 * hal_vm_switch() は実行中のコードを壊さない。 */
IMPORT void hal_vm_drop_identity(void);

/* ------------------------------------------------------------------ */
/* 起動初期の物理ページ確保                                            */
/*                                                                     */
/* ページテーブル自身を置くために必要。正式な物理メモリ管理（周辺核の   */
/* メモリ管理, BTRON3 §1.5）ができるまでの繋ぎ。                        */
/* ------------------------------------------------------------------ */

IMPORT void pmem_early_init(UD base, UD size);
IMPORT UD pmem_early_alloc_page(void); /* 失敗時 0。ゼロクリア済み */
IMPORT UD pmem_early_used(void);       /* 確保済みバイト数 */
IMPORT UD pmem_early_base(void);       /* プールの先頭物理アドレス */
IMPORT UD pmem_early_limit(void);      /* プールの終端物理アドレス */

#endif /* __BTRON_VMEM_H__ */

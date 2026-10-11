/*
 * pmem.c — 起動初期の物理ページ確保（アーキ中立）
 *
 * ページテーブル自身を置くために必要な、最小限のバンプアロケータ。
 * 解放はできない。正式な物理メモリ管理（周辺核のメモリ管理、
 * BTRON3 §1.5 / docs/design-issues.md A14）ができるまでの繋ぎ。
 *
 * 規律 R2: ページサイズをハードコードせず HAL から取る。
 */

#include <btron/hal.h>
#include <btron/vmem.h>

LOCAL UD pm_next;  /* 次に配る物理アドレス */
LOCAL UD pm_limit; /* この手前まで */
LOCAL UD pm_base;

EXPORT void pmem_early_init(UD base, UD size)
{
	UD ps = (UD)hal_page_size();

	pm_base = (base + ps - 1) & ~(ps - 1);
	pm_next = pm_base;
	pm_limit = base + size;
}

EXPORT UD pmem_early_alloc_page(void)
{
	UD ps = (UD)hal_page_size();
	UD pa;
	UD i;
	volatile UD *p;

	if (pm_next + ps > pm_limit) {
		return 0;
	}
	pa = pm_next;
	pm_next += ps;

	/* MMU を有効にする前はここが恒等写像、有効にした後は直接マップ上に
	 * 同じ物理アドレスが見えている必要がある。起動順序でそれを保証する。 */
	p = (volatile UD *)(VP)(hal_vm_enabled() ? BTRON_PA_TO_VA(pa) : pa);
	for (i = 0; i < ps / sizeof(UD); i++) {
		p[i] = 0;
	}
	return pa;
}

EXPORT UD pmem_early_used(void)
{
	return pm_next - pm_base;
}

EXPORT UD pmem_early_base(void)
{
	return pm_base;
}

EXPORT UD pmem_early_limit(void)
{
	return pm_limit;
}

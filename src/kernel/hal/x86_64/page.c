/*
 * page.c — ページサイズ (x86_64)
 *
 * A13: 4KB。規律 R2 により定数として配らずこの関数経由で参照する。
 * x86_64 には 16KB ページが存在しないので、将来 16KB へ移る場合は
 * 4KB ページ 4 枚を束ねる上位抽象が要る（詳細 A13）。
 */

#include <btron/hal.h>

#define PAGE_SHIFT_4K 12U

EXPORT UW hal_page_shift(void)
{
	return PAGE_SHIFT_4K;
}

EXPORT UW hal_page_size(void)
{
	return 1U << PAGE_SHIFT_4K;
}

/*
 * page.c — ページサイズ (arm64)
 *
 * A13: 4KB。ただし規律 R2 により定数として配らず、この関数経由で参照する。
 * 将来 16KB granule へ移行する場合、変更箇所はここに閉じる。
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

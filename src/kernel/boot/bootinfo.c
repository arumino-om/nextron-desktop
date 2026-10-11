/*
 * bootinfo.c — ブートローダからの引き継ぎ情報の受け取り（アーキ中立）
 *
 * ローダが置いた BootInfo は EfiLoaderData の中にあり、カーネルの
 * ページテーブルには写像されていない。MMU を有効にした瞬間に読めなくなる。
 * そこで**起動第 1 段（物理で走っている間）に内容をカーネル内へ複製する**。
 * 複製したものは .bss にあるので、以後は上位半分から普通に読める。
 *
 * 第 1 引数は起動経路によって中身が違う。
 *
 *   UEFI 経由          BootInfo の物理アドレス（magic で判別）
 *   QEMU -kernel + DTB DTB の物理アドレス（0xd00dfeed で判別）
 *   QEMU -kernel       0（ELF 直接起動では x0 に何も入らない）
 */

#include <btron/bootinfo.h>
#include <btron/hal.h>

LOCAL BootInfo info;
LOCAL BootMemRegion regions[BOOTINFO_MAX_REGIONS];
LOCAL UW region_count;

/* ビッグエンディアンの 32bit を読む。DTB のマジックは BE で置かれている
 * （規律 R7: 変換は境界にのみ置く。ここはまさに境界）。 */
LOCAL UW read_be32(UD pa)
{
	CONST volatile UB *p = (CONST volatile UB *)(VP)pa;

	return ((UW)p[0] << 24) | ((UW)p[1] << 16) | ((UW)p[2] << 8) | (UW)p[3];
}

LOCAL UD read_u64(UD pa)
{
	return *(CONST volatile UD *)(VP)pa;
}

EXPORT Bool bootinfo_take(UD arg)
{
	CONST volatile BootInfo *src;
	UW i;

	info.magic = BTRON_BOOTINFO_MAGIC;
	info.version = BTRON_BOOTINFO_VERSION;
	info.size = (UW)sizeof(BootInfo);
	region_count = 0;

	if (arg == 0) {
		return False;
	}
	if (read_u64(arg) != BTRON_BOOTINFO_MAGIC) {
		/* BootInfo ではない。DTB なら拾っておく。 */
		if (read_be32(arg) == BTRON_DTB_MAGIC) {
			info.dtb = arg;
		}
		return False;
	}

	src = (CONST volatile BootInfo *)(VP)arg;

	/* ローダが新しくカーネルが古い組み合わせでも落ちないように、
	 * 自分が知っている範囲までしか読まない。 */
	info.version = src->version;
	info.kernel_pa = src->kernel_pa;
	info.kernel_size = src->kernel_size;
	info.acpi_rsdp = src->acpi_rsdp;
	info.dtb = src->dtb;
	info.fb.base = src->fb.base;
	info.fb.size = src->fb.size;
	info.fb.width = src->fb.width;
	info.fb.height = src->fb.height;
	info.fb.pitch = src->fb.pitch;
	info.fb.format = src->fb.format;
	info.efi_system_table = src->efi_system_table;

	if (src->mem_pa != 0) {
		UD n = src->mem_count;
		CONST volatile BootMemRegion *r = (CONST volatile BootMemRegion *)(VP)src->mem_pa;

		if (n > BOOTINFO_MAX_REGIONS) {
			n = BOOTINFO_MAX_REGIONS;
		}
		for (i = 0; i < (UW)n; i++) {
			regions[i].base = r[i].base;
			regions[i].size = r[i].size;
			regions[i].type = r[i].type;
			regions[i].resv = 0;
		}
		region_count = (UW)n;
	}
	info.mem_count = region_count;
	info.mem_pa = 0; /* 複製後は意味を持たない。アクセサ経由で読ませる。 */
	return True;
}

EXPORT CONST BootInfo *bootinfo(void)
{
	return &info;
}

EXPORT UW bootinfo_mem_count(void)
{
	return region_count;
}

EXPORT CONST BootMemRegion *bootinfo_mem(UW i)
{
	if (i >= region_count) {
		return 0;
	}
	return &regions[i];
}

EXPORT UD bootinfo_find_free(UD size, UD after)
{
	UD ps = (UD)hal_page_size();
	UD best = 0;
	UW i;

	for (i = 0; i < region_count; i++) {
		UD base;
		UD limit;

		if (regions[i].type != BMEM_FREE) {
			continue;
		}
		base = regions[i].base;
		limit = base + regions[i].size;
		if (base < after) {
			base = after;
		}
		base = (base + ps - 1) & ~(ps - 1);
		if (base >= limit || limit - base < size) {
			continue;
		}
		if (best == 0 || base < best) {
			best = base;
		}
	}
	return best;
}

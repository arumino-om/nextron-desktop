/*
 * exec.c — 実行プログラムレコードをアドレス空間へ読み込む
 *
 * 形式は include/btron/objfmt.h（本OS が決めたもの。仕様は中身を
 * 規定していない）。ここは「レコードの中身を貰って、プロセスの
 * アドレス空間に像を作る」までを引き受ける。
 *
 * ------------------------------------------------------------------------
 * 読み込み先のアドレス空間は自分のものではない
 * ------------------------------------------------------------------------
 *
 * `cre_prc` を呼んだのは**親プロセス**なので、走っている TTBR0 は親の
 * ものである。子の空間へ直接書くことはできない。
 *
 * だから、物理ページを確保して**直接マップ経由（上位半分）で中身を
 * 書き**、そのページを子の空間に写像する。写像してから書こうとすると、
 * 空間を切り替える必要が出て話が難しくなる。
 *
 * ------------------------------------------------------------------------
 * 確保したページは自分で覚えておく
 * ------------------------------------------------------------------------
 *
 * `hal_vm_destroy` はページテーブルしか返さない（葉が指すデータページは
 * 写像した側の持ち物、という約束。規律 R18）。だからここで確保した
 * ページはここが覚えて、プロセスが終わるときに返す。
 */

#include <btron/file.h>

#include <btron/hal.h>
#include <btron/objfmt.h>
#include <btron/proc.h>
#include <btron/vmem.h>

#include "../fs/fs.h" /* fsb_get32 などのバイト取り出し */

/* プロセス 1 つ分の像。 */
typedef struct ExecImage {
	Bool used;
	W pid;
	UD *pa;  /* 確保した物理ページ */
	W npages;
} ExecImage;

LOCAL ExecImage images[PRC_MAX_PRCID + 1];

LOCAL W cpu_of_this_machine(void)
{
#if defined(__aarch64__)
	return OBJ_CPU_ARM64;
#elif defined(__x86_64__)
	return OBJ_CPU_X86_64;
#else
	return -1;
#endif
}

EXPORT W exec_cpu_type(void)
{
	return cpu_of_this_machine();
}

/* 1 ページ確保して中身を入れ、as に写像する。 */
LOCAL ERR map_page(AddrSpace *as, ExecImage *im, UD va, CONST UB *src, W len,
                   UW attr)
{
	UD ps = (UD)hal_page_size();
	UD pa = pmem_alloc(1);
	UB *dst;
	W i;

	if (pa == 0) {
		return ER_NOMEM;
	}
	/* **直接マップ経由で書く。** 子の空間はまだ切り替わっていない。 */
	dst = (UB *)(VP)BTRON_PA_TO_VA(pa);
	for (i = 0; i < (W)ps; i++) {
		dst[i] = (src != 0 && i < len) ? src[i] : 0;
	}
	if (hal_vm_map(as, va, pa, ps, attr) != 0) {
		pmem_free(pa, 1);
		return ER_NOMEM;
	}
	im->pa[im->npages++] = pa;
	return ER_OK;
}

EXPORT ERR exec_load(W pid, AddrSpace *as, CONST UB *obj, W size, UD *entry,
                     UD *user_sp)
{
	UD ps = (UD)hal_page_size();
	UW text_size;
	UW data_size;
	UW bss_size;
	UW stack_size;
	UD base;
	UD off;
	UD body;
	W total_pages;
	ExecImage *im;
	ERR er;

	if (pid < 1 || pid > PRC_MAX_PRCID || as == 0 || obj == 0 ||
	    entry == 0 || user_sp == 0) {
		return ER_ADR;
	}
	/* 仕様 cre_prc の ER_REC「プログラムレコードの内容が異常である」。 */
	if (size < OBJ_HDRSZ) {
		return ER_REC;
	}
	if (obj[OBJ_MAGIC + 0] != OBJ_MAGIC0 || obj[OBJ_MAGIC + 1] != OBJ_MAGIC1 ||
	    obj[OBJ_MAGIC + 2] != OBJ_MAGIC2 || obj[OBJ_MAGIC + 3] != OBJ_MAGIC3 ||
	    obj[OBJ_MAGIC + 4] != OBJ_MAGIC4 || obj[OBJ_MAGIC + 5] != OBJ_MAGIC5) {
		return ER_REC;
	}
	if (fsb_get32(obj, OBJ_VER) != OBJ_VERSION) {
		return ER_REC;
	}
	/* **CPU が違うものは走らせない。** レコードのサブタイプと同じ値が
	 * ヘッダにも入っているので、ここでも確かめる。 */
	if ((W)fsb_get32(obj, OBJ_CPU) != cpu_of_this_machine()) {
		return ER_REC;
	}
	base = (UD)fsb_get32(obj, OBJ_BASE);
	if (base != BTRON_PROC_TEXT_BASE) {
		return ER_REC;
	}
	text_size = fsb_get32(obj, OBJ_TEXT_SIZE);
	data_size = fsb_get32(obj, OBJ_DATA_SIZE);
	bss_size = fsb_get32(obj, OBJ_BSS_SIZE);
	stack_size = fsb_get32(obj, OBJ_STACK);
	if (stack_size == 0) {
		stack_size = OBJ_DEF_STACK;
	}
	/* text と data の境はページ境界でなければならない。
	 * またがると「書けるコード」か「実行できるデータ」ができる。 */
	if ((text_size & (UW)(ps - 1)) != 0) {
		return ER_REC;
	}
	if ((UD)OBJ_HDRSZ + text_size + data_size > (UD)size) {
		return ER_REC;
	}

	stack_size = (UW)(((UD)stack_size + ps - 1) & ~(ps - 1));
	total_pages = (W)((text_size + data_size + bss_size + ps - 1) / ps) +
	              (W)(stack_size / ps);

	im = &images[pid];
	if (im->used) {
		return ER_BUSY; /* 1 プロセスに 1 つ */
	}
	im->pa = (UD *)kmalloc((UD)total_pages * (UD)sizeof(UD));
	if (im->pa == 0) {
		return ER_NOSPC;
	}
	im->npages = 0;
	im->pid = pid;
	im->used = True;

	body = (UD)OBJ_HDRSZ;
	/* 読み出し＋実行の区画。 */
	for (off = 0; off < text_size; off += ps) {
		W len = (W)((text_size - off < ps) ? (text_size - off) : ps);

		er = map_page(as, im, base + off, obj + body + off, len,
		              BTRON_PG_READ | BTRON_PG_EXEC | BTRON_PG_USER);
		if (er < 0) {
			goto fail;
		}
	}
	/* 読み書きの区画（data ＋ bss）。bss はファイルに無いので 0 で埋まる。 */
	{
		UD rw = (UD)data_size + (UD)bss_size;

		for (off = 0; off < rw; off += ps) {
			UD at = (UD)text_size + off;
			CONST UB *src = 0;
			W len = 0;

			if (off < data_size) {
				src = obj + body + at;
				len = (W)((data_size - off < ps) ? (data_size - off) : ps);
			}
			er = map_page(as, im, base + at, src, len,
			              BTRON_PG_READ | BTRON_PG_WRITE | BTRON_PG_USER);
			if (er < 0) {
				goto fail;
			}
		}
	}
	/* ユーザスタック。上から下へ伸びる (vmem.h の区画表)。 */
	{
		UD top = BTRON_PROC_STACK_TOP;
		UD bottom = top - stack_size;

		for (off = 0; off < stack_size; off += ps) {
			er = map_page(as, im, bottom + off, 0, 0,
			              BTRON_PG_READ | BTRON_PG_WRITE | BTRON_PG_USER);
			if (er < 0) {
				goto fail;
			}
		}
		/* AAPCS64 は 16 バイト境界を要求する。 */
		*user_sp = top & ~0xFULL;
	}
	*entry = base + (UD)fsb_get32(obj, OBJ_ENTRY);
	return ER_OK;

fail:
	exec_release_process(pid);
	return er;
}

EXPORT void exec_release_process(W pid)
{
	ExecImage *im;
	W i;

	if (pid < 1 || pid > PRC_MAX_PRCID) {
		return;
	}
	im = &images[pid];
	if (!im->used) {
		return;
	}
	/* **ページテーブルより先に返す。** hal_vm_destroy は表しか返さない
	 * ので、ここで返さないとプロセスごとに像の分だけ失う。 */
	for (i = 0; i < im->npages; i++) {
		pmem_free(im->pa[i], 1);
	}
	if (im->pa != 0) {
		kfree(im->pa);
	}
	im->pa = 0;
	im->npages = 0;
	im->used = False;
}

EXPORT W exec_pages(W pid)
{
	if (pid < 1 || pid > PRC_MAX_PRCID || !images[pid].used) {
		return 0;
	}
	return images[pid].npages;
}

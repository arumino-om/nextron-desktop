/*
 * mm.c — 物理メモリとカーネルヒープ
 *
 * selftest.c から切り出したもの（W0-1）。区分の登録は
 * testsuite.h の BTRON_TEST_GROUP で行い、実行順は order で決まる。
 * order の帯は docs/registry.md で予約している。
 */

#include <btron/bootinfo.h>
#include <btron/device.h>
#include <btron/dp.h>
#include <btron/event.h>
#include <btron/file.h>
#include <btron/fontman.h>
#include <btron/machdesc.h>
#include <btron/manager.h>
#include <btron/objfmt.h>
#include <btron/screen.h>
#include <btron/system.h>
#include <btron/test.h>
#include <btron/virtio.h>
#include <btron/window.h>
#include <btron/winmsg.h>

#include "../knl.h"
#include "testsuite.h"

/* 合成したメモリマップの置き場（実機規模の検査用）。
 *
 * **実機のメモリマップは要素が 100 を超えることがある**（詳細 F9）が、
 * QEMU ではその構成を作れない。畳みの関数は純粋な関数にしてあるので
 * （src/kernel/boot/memmap.c）、合成したマップを直に食わせて確かめる。
 * これが無いと、畳みが壊れていても実機を手に入れるまで分からない。 */
#define MM_SYNTH 600
LOCAL BootMemRegion mm_synth[MM_SYNTH];

/* `r` が覆うバイトのうち BMEM_FREE のものを数える。 */
LOCAL UD mm_free_bytes(CONST BootMemRegion *r, UD n)
{
	UD sum = 0;
	UD i;

	for (i = 0; i < n; i++) {
		if (r[i].type == BMEM_FREE) {
			sum += r[i].size;
		}
	}
	return sum;
}

BTRON_TEST_GROUP("物理メモリとカーネルヒープ", 170, TST_PHASE_KERNEL, tst_mm)
{
	BTRON_TEST("削除すると物理メモリが戻る (Phase 2 の土台)", {
		if (pmem_total_pages() == 0) {
			/* メモリマップが無い構成。アロケータが立たないので返せない。 */
			BTRON_CHECK("アロケータ無し", pmem_free_pages() == 0);
		} else {
			UD free0 = pmem_free_pages();
			ID id;
			T_CMPF cf;
			T_CMPL cl;
			W i;

			for (i = 0; i < 8; i++) {
				cf.dsname[i] = 0;
				cl.dsname[i] = 0;
			}

			/* ① タスクのスタック。生成で減り、削除で戻る。
			 * ここが戻らないとタスクを作っては消すたびに漏れる。 */
			id = tst_make_task(tst_t_mark_b, 20, 0);
			BTRON_CHECK("生成で減った", pmem_free_pages() < free0);
			BTRON_CHECK("削除", tk_del_tsk(id) == E_OK);
			BTRON_CHECK("スタックが戻った", pmem_free_pages() == free0);

			/* ② 固定長メモリプール */
			cf.exinf = 0;
			cf.mpfatr = TA_TFIFO;
			cf.mpfcnt = 16;
			cf.blfsz = 256;
			cf.bufptr = 0;
			id = tk_cre_mpf(&cf);
			BTRON_CHECK("生成", id >= 1);
			BTRON_CHECK("減った", pmem_free_pages() < free0);
			BTRON_CHECK("削除", tk_del_mpf(id) == E_OK);
			BTRON_CHECK("戻った", pmem_free_pages() == free0);

			/* ③ 可変長メモリプール */
			cl.exinf = 0;
			cl.mplatr = TA_TFIFO;
			cl.mplsz = 8192;
			cl.bufptr = 0;
			id = tk_cre_mpl(&cl);
			BTRON_CHECK("生成", id >= 1);
			BTRON_CHECK("減った", pmem_free_pages() < free0);
			BTRON_CHECK("削除", tk_del_mpl(id) == E_OK);
			BTRON_CHECK("戻った", pmem_free_pages() == free0);

			/* ④ 繰り返しても漏れない。**これが本題。**
			 * 1 回だけなら偶然同じ数になることもあるが、
			 * 100 回繰り返して変わらなければ漏れていない。 */
			for (i = 0; i < 100; i++) {
				ID t = tst_make_task(tst_t_mark_b, 20, 0);

				if (t < 1 || tk_del_tsk(t) != E_OK) {
					break;
				}
			}
			BTRON_CHECK("100 回繰り返しても漏れない",
			            i == 100 && pmem_free_pages() == free0);
		}
	});

	BTRON_TEST("中心核が動いている", {
		BTRON_CHECK("起動済み", knl_running() == True);
		BTRON_CHECK("自タスクがいる", tk_get_tid() >= 1);
		BTRON_CHECK("アイドルではない", knl_cur != &knl_idle);
	});

	/* -------------------------------------------------------------- */
	/* 実機規模への耐性 (詳細 F9 の差分表)                            */
	/* -------------------------------------------------------------- */

	BTRON_TEST("メモリマップを上限まで畳む (詳細 F9)", {
		UD n;
		UD free0;
		UD i;
		Bool sorted = True;
		Bool covered = True;
		UD at;
		UD k;

		/* **種別を互い違いにした 500 領域。** 隣接同種の畳み
		 * (bootmem_fold) では 1 つも減らない形にしてある。
		 * ここで減らせなければ実機では必ず溢れる。 */
		for (i = 0; i < 500; i++) {
			mm_synth[i].base = 0x100000ULL + i * 0x1000ULL;
			mm_synth[i].size = 0x1000ULL;
			mm_synth[i].type = ((i % 2) == 0) ? BMEM_FREE : BMEM_FIRMWARE;
			mm_synth[i].resv = 0;
		}
		BTRON_CHECK("上限を超える数を作った", 500 > BOOTINFO_MAX_REGIONS);
		BTRON_CHECK("隣接同種では減らない", bootmem_fold(mm_synth, 500) == 500);

		free0 = mm_free_bytes(mm_synth, 500);
		n = bootmem_compact(mm_synth, 500, BOOTINFO_MAX_REGIONS);
		BTRON_CHECK("上限に収まった", n <= BOOTINFO_MAX_REGIONS);
		BTRON_CHECK("空にはならない", n > 0);

		/* 昇順・重なりなしが保たれていること。 */
		at = 0;
		for (i = 0; i < n; i++) {
			if (mm_synth[i].base < at) {
				sorted = False;
			}
			at = mm_synth[i].base + mm_synth[i].size;
		}
		BTRON_CHECK("昇順で重なりなし", sorted == True);
		BTRON_CHECK("端は動かない",
		            mm_synth[0].base == 0x100000ULL &&
		                at == 0x100000ULL + 500 * 0x1000ULL);

		/* **使える領域が増えていないこと。** 畳みで失うのは構わないが、
		 * 使えない領域が「空き」として現れてはならない。 */
		BTRON_CHECK("空きは増えない", mm_free_bytes(mm_synth, n) <= free0);
		for (i = 0; i < n && covered; i++) {
			if (mm_synth[i].type != BMEM_FREE) {
				continue;
			}
			/* 元は 1 ページおきにしか空きが無いので、畳んだ結果の
			 * 空き領域は必ず 1 ページでなければならない。 */
			for (k = mm_synth[i].base;
			     k < mm_synth[i].base + mm_synth[i].size; k += 0x1000ULL) {
				if (((k - 0x100000ULL) / 0x1000ULL) % 2 != 0) {
					covered = False;
				}
			}
		}
		BTRON_CHECK("空きが広がっていない", covered == True);
	});

	BTRON_TEST("穴を跨いで畳まない (詳細 F9)", {
		UD n;

		/* 間に 1 ページの穴がある 2 つの空き領域。**穴は「RAM ではない」**
		 * ので、繋げて空きにしてはならない（MMIO が空きメモリとして
		 * 現れる）。畳めないときは落とす方を選ぶ。 */
		mm_synth[0].base = 0x200000ULL;
		mm_synth[0].size = 0x2000ULL;
		mm_synth[0].type = BMEM_FREE;
		mm_synth[0].resv = 0;
		mm_synth[1].base = 0x204000ULL;
		mm_synth[1].size = 0x1000ULL;
		mm_synth[1].type = BMEM_FREE;
		mm_synth[1].resv = 0;

		n = bootmem_compact(mm_synth, 2, 1);
		BTRON_CHECK("1 本になった", n == 1);
		BTRON_CHECK("穴を含んでいない", mm_synth[0].size <= 0x2000ULL);
		BTRON_CHECK("小さい方が落ちた",
		            mm_synth[0].base == 0x200000ULL &&
		                mm_synth[0].size == 0x2000ULL);
	});

	BTRON_TEST("直接マップは RAM 全域をブロックで覆う (詳細 F9 / Phase 2 ㉛)", {
		CONST AddrSpace *ks = hal_vm_kernel_space();
		UD ps = (UD)hal_page_size();
		UW i;
		Bool mapped = True;
		Bool blocked = False;
		UD ram = 0;

		if (bootinfo_mem_count() == 0) {
			/* メモリマップが無い構成（-kernel 直接起動）。
			 * 直接マップはカーネルと早期プールしか張らない。 */
			BTRON_CHECK("マップが無いので飛ばす", True);
		} else {
			for (i = 0; i < bootinfo_mem_count(); i++) {
				CONST BootMemRegion *r = bootinfo_mem(i);
				UD last;

				if (r->type == BMEM_UNUSABLE || r->size == 0) {
					continue;
				}
				ram += r->size;
				last = r->base + r->size - ps;
				/* 先頭と末尾の両方が引けること。**末尾が大事** ——
				 * 32GB の機械で最後のページが落ちていても、
				 * 小さい構成では誰も気づかない。 */
				if (hal_vm_translate(ks, BTRON_PA_TO_VA(r->base)) != r->base ||
				    hal_vm_translate(ks, BTRON_PA_TO_VA(last)) != last) {
					mapped = False;
				}
				/* 2MB 以上の領域の中に、ブロックで張られた所があること。 */
				if (r->size >= 4ULL * 1024 * 1024) {
					CONST UD blk = 2ULL * 1024 * 1024;
					UD mid = (r->base + blk - 1) & ~(blk - 1);

					if (hal_vm_map_span(ks, BTRON_PA_TO_VA(mid)) > ps) {
						blocked = True;
					}
				}
			}
			BTRON_CHECK("全域が引ける", mapped == True);
			BTRON_CHECK("RAM がある", ram > 0);
			/* **これが 4KB 粒度への逆戻りを捕まえる。** 4KB で張ると
			 * 表が RAM の 1/512 になり、32GB では早期プール (16MB) に
			 * 入らず「hal_vm_init に失敗」で止まる。 */
			BTRON_CHECK("ブロックで張っている", blocked == True);
			/* 表の消費が RAM に比例していないこと。4KB 粒度なら
			 * RAM/512 を必ず超える。 */
			BTRON_CHECK("表が RAM に比例しない",
			            pmem_early_used() < 4 * 1024 * 1024);
		}
	});
}

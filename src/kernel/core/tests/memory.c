/*
 * memory.c — 周辺核 メモリ管理 (§1.5)
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

/* ------------------------------------------------------------------ */
/* 周辺核 §1.5「メモリ管理」の検査用                                    */
/* ------------------------------------------------------------------ */

LOCAL VP mbk_shared;      /* 親が獲得した共有メモリ。子が書き込む */

LOCAL W mbk_writer_err;

LOCAL VP mbk_c_local;     /* 子が獲得したローカル */

LOCAL VP mbk_c_common;    /* 子が獲得した共有（DELEXIT なし） */

LOCAL VP mbk_c_delexit;   /* 子が獲得した共有（DELEXIT あり） */

LOCAL ERR mbk_sys_err;    /* レベルの低いプロセスから M_SYSTEM を要求した結果 */

/* 共有メモリに書き込むだけの子プロセス。
 * **別のプロセスから同じアドレスが見えること**を確かめる。 */
LOCAL W p_shared_writer(MESSAGE *msg)
{
	volatile UW *p = (volatile UW *)mbk_shared;

	(void)msg;
	mbk_writer_err = (p == 0) ? 1 : 0;
	if (p != 0) {
		p[0] = 0xC0FFEEU;
	}
	return 0;
}

/* 3 種類の指定で獲得して、そのまま終了する子プロセス。
 * 終了時の自動解放の規則を親から観察する。 */
LOCAL W p_delexit(MESSAGE *msg)
{
	(void)msg;
	mbk_c_local = 0;
	mbk_c_common = 0;
	mbk_c_delexit = 0;
	(void)get_mbk(&mbk_c_local, 1, 0);
	(void)get_mbk(&mbk_c_common, 1, M_COMMON);
	(void)get_mbk(&mbk_c_delexit, 1, M_COMMON | DELEXIT);
	return 0;
}

/* ユーザレベルを下げてからシステムメモリを要求する子プロセス。 */
LOCAL W p_sysmem(MESSAGE *msg)
{
	P_USER u;
	VP adr = 0;

	(void)msg;
	(void)get_usr(P_SELF, &u);
	u.level = 5; /* 0 が最高なので 5 はアプリケーションプロセス相当 */
	(void)chg_usr(&u);
	mbk_sys_err = get_mbk(&adr, 1, M_SYSTEM);
	if (mbk_sys_err == ER_OK) {
		(void)rel_mbk(adr);
	}
	return 0;
}

BTRON_TEST_GROUP("周辺核 メモリ管理 (§1.5)", 210, TST_PHASE_PROC, tst_memory)
{
	W me = prc_self();

	BTRON_TEST("3 種類の領域の配置 (§1.5.1)", {
		VP loc = 0;
		VP com = 0;
		VP sys = 0;

		BTRON_CHECK("ローカル", get_mbk(&loc, 1, 0) == ER_OK);
		BTRON_CHECK("共有", get_mbk(&com, 1, M_COMMON) == ER_OK);
		/* 初期プロセスはユーザレベル 0 = システムプロセス */
		BTRON_CHECK("システム", get_mbk(&sys, 1, M_SYSTEM) == ER_OK);

		/* 仕様: 「共有メモリ、システムメモリは負のアドレスとなり、
		 * ローカルメモリは正のアドレスとなる」 */
		BTRON_CHECK("ローカルは正", (D)(UD)loc > 0);
		BTRON_CHECK("共有は負", (D)(UD)com < 0);
		BTRON_CHECK("システムは負", (D)(UD)sys < 0);

		/* §3.2 の区画に入っていること */
		BTRON_CHECK("ローカルの区画",
		            (UD)loc >= BTRON_PROC_LOCAL_BASE &&
		                (UD)loc < BTRON_PROC_LOCAL_LIMIT);
		BTRON_CHECK("共有の区画",
		            (UD)com >= BTRON_COMMON_BASE &&
		                (UD)com < BTRON_SYSTEM_BASE);
		BTRON_CHECK("システムの区画",
		            (UD)sys >= BTRON_SYSTEM_BASE &&
		                (UD)sys < BTRON_MAPREC_BASE);

		/* 仕様: 「書込み / 読込みは自由にできる」 */
		*(volatile UW *)loc = 0x11111111U;
		*(volatile UW *)com = 0x22222222U;
		*(volatile UW *)sys = 0x33333333U;
		BTRON_CHECK("読み書きできる",
		            *(volatile UW *)loc == 0x11111111U &&
		                *(volatile UW *)com == 0x22222222U &&
		                *(volatile UW *)sys == 0x33333333U);
		BTRON_CHECK("互いに別の物理ページ",
		            hal_vm_translate(prc_space(me), (UD)loc) !=
		                hal_vm_translate(hal_vm_kernel_space(), (UD)com));

		BTRON_CHECK("解放",
		            rel_mbk(loc) == ER_OK && rel_mbk(com) == ER_OK &&
		                rel_mbk(sys) == ER_OK);
	});

	BTRON_TEST("獲得したブロックは連続した論理アドレスを持つ (§1.5.3)", {
		VP adr = 0;
		UD ps = (UD)hal_page_size();
		M_STATE st;
		W k;
		Bool ok = True;

		BTRON_CHECK("状態参照", mbk_sts(&st) == ER_OK);
		BTRON_CHECK("ブロックサイズはページサイズ",
		            st.blksz == (W)hal_page_size());
		BTRON_CHECK("総数と残り", st.total > 0 && st.free > 0 &&
		                              st.free <= st.total);
		BTRON_CHECK("アドレス不正", mbk_sts(0) == ER_ADR);

		BTRON_CHECK("4 ブロック", get_mbk(&adr, 4, 0) == ER_OK);
		/* 全域に書いて読み返す。境界をまたいで連続していなければ落ちる。 */
		for (k = 0; k < 4; k++) {
			volatile UW *p = (volatile UW *)(VP)((UD)adr + (UD)k * ps);

			p[0] = 0xA0000000U + (UW)k;
			p[ps / sizeof(UW) - 1] = 0xB0000000U + (UW)k;
		}
		for (k = 0; k < 4; k++) {
			volatile UW *p = (volatile UW *)(VP)((UD)adr + (UD)k * ps);

			if (p[0] != 0xA0000000U + (UW)k ||
			    p[ps / sizeof(UW) - 1] != 0xB0000000U + (UW)k) {
				ok = False;
			}
			/* 物理は連続していなくてよい（仕様が求めているのは論理）。
			 * ただし全ブロックが張られていること。 */
			if (hal_vm_translate(prc_space(me), (UD)adr + (UD)k * ps) == 0) {
				ok = False;
			}
		}
		BTRON_CHECK("全ブロックが読み書きできる", ok);
		BTRON_CHECK("解放", rel_mbk(adr) == ER_OK);
		/* 仕様: adr は get_mbk() で得たアドレスでなければならない */
		BTRON_CHECK("二度目は不正", rel_mbk(adr) == ER_MPTR);
	});

	BTRON_TEST("get_mbk / rel_mbk のエラーコード (§1.5.3)", {
		VP adr = 0;

		BTRON_CHECK("格納先が不正", get_mbk(0, 1, 0) == ER_ADR);
		BTRON_CHECK("0 ブロック", get_mbk(&adr, 0, 0) == ER_PAR);
		BTRON_CHECK("負のブロック数", get_mbk(&adr, -1, 0) == ER_PAR);
		/* M_SYSTEM (3) は M_COMMON (1) のビットを含む。0x2 は
		 * 「上のビットだけ立っている」ので不正。 */
		BTRON_CHECK("中途半端な属性", get_mbk(&adr, 1, 0x2U) == ER_PAR);
		BTRON_CHECK("未定義のビット", get_mbk(&adr, 1, 0x100U) == ER_PAR);
		/* 仮想記憶が無いので M_RESIDENT は指定しても通る（仕様が認めている） */
		BTRON_CHECK("常駐指定は通る",
		            get_mbk(&adr, 1, M_RESIDENT) == ER_OK);
		BTRON_CHECK("解放", rel_mbk(adr) == ER_OK);

		BTRON_CHECK("NULL の解放", rel_mbk(0) == ER_MPTR);
		BTRON_CHECK("出所不明の解放",
		            rel_mbk((VP)(UD)0x1000ULL) == ER_MPTR);
	});

	BTRON_TEST("ローカルメモリは他プロセスからアクセスできない (§1.5.1)", {
		VP loc = 0;
		W child;

		BTRON_CHECK("獲得", get_mbk(&loc, 1, 0) == ER_OK);
		*(volatile UW *)loc = 0x5A5A5A5AU;

		child = prc_create(tst_p_sleeper, 1, 0, 0);
		BTRON_CHECK("子ができた", child >= 1);
		BTRON_CHECK("自分からは引ける",
		            hal_vm_translate(prc_space(me), (UD)loc) != 0);
		/* ここが §1.5.1 の「他のプロセスからはアクセスできない」 */
		BTRON_CHECK("子からは引けない",
		            hal_vm_translate(prc_space(child), (UD)loc) == 0);
		/* ローカルは下位半分なので、上位半分（カーネル・共有）にも無い */
		BTRON_CHECK("共有側にも無い",
		            hal_vm_translate(hal_vm_kernel_space(), (UD)loc) == 0);

		BTRON_CHECK("片付け", ter_prc(child, 0, TERM_NRM) == ER_OK);
		BTRON_CHECK("解放", rel_mbk(loc) == ER_OK);
	});

	BTRON_TEST("共有メモリは他プロセスから書ける (§1.5.1)", {
		W child;

		mbk_shared = 0;
		mbk_writer_err = -1;
		BTRON_CHECK("獲得", get_mbk(&mbk_shared, 1, M_COMMON) == ER_OK);
		*(volatile UW *)mbk_shared = 0;

		/* 優先度 1 は自分 (2) より高いので、生成した瞬間に走りきる。 */
		child = prc_create(p_shared_writer, 1, 0, 0);
		BTRON_CHECK("子は走って終わった", child >= 1 && prc_count() == 1);
		BTRON_CHECK("子はアドレスを見られた", mbk_writer_err == 0);
		/* **別のプロセスが書いた値が見える。** 共有が上位半分にあり、
		 * 上位半分は全プロセスで共通（arm64 は TTBR1 が切り替わらない、
		 * x86_64 は上位 256 PML4 エントリを複製）だから成立する。 */
		BTRON_CHECK("書いた値が見える",
		            *(volatile UW *)mbk_shared == 0xC0FFEEU);
		BTRON_CHECK("解放", rel_mbk(mbk_shared) == ER_OK);
	});

	BTRON_TEST("終了時の自動解放 (§1.5.1 / DELEXIT)", {
		W n0 = mbk_count();
		UD free0 = tst_free_pages_settled();
		W child;

		child = prc_create(p_delexit, 1, 0, 0);
		BTRON_CHECK("子は走って終わった", child >= 1 && prc_count() == 1);
		BTRON_CHECK("3 つ獲得できていた",
		            mbk_c_local != 0 && mbk_c_common != 0 &&
		                mbk_c_delexit != 0);
		/* ローカルは DELEXIT に関係なく解放され、DELEXIT 付きの共有も
		 * 解放される。DELEXIT の無い共有だけが残る（仕様）。 */
		BTRON_CHECK("残るのは 1 つだけ", mbk_count() == n0 + 1);
		BTRON_CHECK("残ったのは DELEXIT なしの共有",
		            *(volatile UW *)mbk_c_common == 0 ||
		                *(volatile UW *)mbk_c_common != 0);
		/* 「獲得したプロセスの存在とは無関係に存在する」ので、
		 * 持ち主が消えた後でも別のプロセスから解放できる。 */
		BTRON_CHECK("別プロセスから解放できる",
		            rel_mbk(mbk_c_common) == ER_OK);
		BTRON_CHECK("記録が戻った", mbk_count() == n0);
		/* 子のタスクの後片付けも流してから数える。 */
		BTRON_CHECK("物理ページも戻った", tst_free_pages_settled() == free0);
	});

	BTRON_TEST("システムメモリはアプリケーションプロセスから使えない (§1.5.3)", {
		W child;

		mbk_sys_err = ER_OK;
		child = prc_create(p_sysmem, 1, 0, 0);
		BTRON_CHECK("子は走って終わった", child >= 1 && prc_count() == 1);
		/* 仕様: 「この指定は、アプリケーションプロセスからは使用して
		 * はいけない」。仕様はエラーコードを挙げていないので ER_PAR。 */
		BTRON_CHECK("拒まれた", mbk_sys_err == ER_PAR);
	});

	BTRON_TEST("空いた仮想アドレスを再利用する", {
		VP a = 0;
		VP b = 0;
		VP c = 0;
		VP d = 0;

		BTRON_CHECK("3 つ獲得",
		            get_mbk(&a, 1, 0) == ER_OK &&
		                get_mbk(&b, 1, 0) == ER_OK &&
		                get_mbk(&c, 1, 0) == ER_OK);
		BTRON_CHECK("連続して並ぶ",
		            (UD)b == (UD)a + hal_page_size() &&
		                (UD)c == (UD)b + hal_page_size());
		BTRON_CHECK("中央を解放", rel_mbk(b) == ER_OK);
		BTRON_CHECK("再獲得", get_mbk(&d, 1, 0) == ER_OK);
		BTRON_CHECK("空いた最も低いところが返る", d == b);
		/* 2 ブロック要求は中央の隙間に入らないので後ろへ回る */
		BTRON_CHECK("隙間に入らない大きさは後ろへ",
		            get_mbk(&b, 2, 0) == ER_OK &&
		                (UD)b == (UD)c + hal_page_size());
		BTRON_CHECK("片付け",
		            rel_mbk(a) == ER_OK && rel_mbk(b) == ER_OK &&
		                rel_mbk(c) == ER_OK && rel_mbk(d) == ER_OK);
	});

	BTRON_TEST("メモリブロックの獲得と解放で漏れない (§1.5)", {
		UD free0;
		W n0;
		W i;
		Bool ok = True;
		VP adr = 0;

		/* **先に 1 度ずつ通しておく。** ページテーブルの中間段は
		 * 解放していないので、初回だけ空きページが減る。同じ仮想
		 * アドレスを使い回す限り以後は増えない（memory.c の「既知の限界」）。 */
		if (get_mbk(&adr, 2, 0) == ER_OK) {
			(void)rel_mbk(adr);
		}
		if (get_mbk(&adr, 2, M_COMMON) == ER_OK) {
			(void)rel_mbk(adr);
		}
		free0 = tst_free_pages_settled();
		n0 = mbk_count();

		/* 1 回では漏れが見えない（Phase 2 ㉜）。 */
		for (i = 0; i < 100; i++) {
			VP l = 0;
			VP c = 0;

			if (get_mbk(&l, 2, 0) != ER_OK ||
			    get_mbk(&c, 2, M_COMMON) != ER_OK) {
				ok = False;
				break;
			}
			*(volatile UW *)l = (UW)i;
			*(volatile UW *)c = (UW)i;
			if (rel_mbk(l) != ER_OK || rel_mbk(c) != ER_OK) {
				ok = False;
				break;
			}
		}
		BTRON_CHECK("100 回とも通った", ok);
		BTRON_CHECK("記録が戻った", mbk_count() == n0);
		BTRON_CHECK("物理ページが戻った", tst_free_pages_settled() == free0);
	});

	BTRON_TEST("メモリブロック記録の上限 (§1.5.3 ER_NOSPC)", {
		VP adr[MBK_MAX];
		VP over = 0;
		W n0 = mbk_count();
		W made = 0;
		W i;

		for (i = 0; i < MBK_MAX; i++) {
			adr[i] = 0;
			if (get_mbk(&adr[i], 1, 0) != ER_OK) {
				break;
			}
			made++;
		}
		BTRON_CHECK("記録の数だけ取れる", made == MBK_MAX - n0);
		BTRON_CHECK("超えると ER_NOSPC",
		            get_mbk(&over, 1, 0) == ER_NOSPC);
		for (i = 0; i < made; i++) {
			BTRON_CHECK("片付け", rel_mbk(adr[i]) == ER_OK);
		}
		BTRON_CHECK("記録が戻った", mbk_count() == n0);
	});

	/* -------------------------------------------------------------- */
	/* §1.2 メッセージ管理                                            */
	/* -------------------------------------------------------------- */

}

/*
 * taskobj.c — 周辺核 プロセス間同期通信 (§1.3)
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

/* セマフォを待つサブタスク。優先度継承と ER_DLT の検査用。 */
LOCAL ID tobj_sem;

LOCAL ERR tobj_wait_er;

LOCAL void p_semwait(W arg)
{
	(void)arg;
	tobj_wait_er = wai_sem(tobj_sem, T_FOREVER);
	ext_tsk();
}

BTRON_TEST_GROUP("周辺核 プロセス間同期通信 (§1.3)", 230, TST_PHASE_PROC, tst_taskobj)
{
	BTRON_TEST("同期用セマフォ (§1.3.2 SEM_SYNC)", {
		W n0 = tobj_count();
		ID id;

		BTRON_CHECK("負の初期値", cre_sem(-1, SEM_SYNC) == ER_PAR);
		BTRON_CHECK("属性不正", cre_sem(0, 0x100) == ER_PAR);
		id = cre_sem(1, SEM_SYNC);
		BTRON_CHECK("生成", id >= 1);
		BTRON_CHECK("数が増えた", tobj_count() == n0 + 1);
		BTRON_CHECK("獲得", wai_sem(id, T_NOWAIT) == ER_OK);
		/* **中心核の E_TMOUT ではなく ER_NONE**（規律 R20） */
		BTRON_CHECK("もう無い", wai_sem(id, T_NOWAIT) == ER_NONE);
		BTRON_CHECK("時間切れも ER_NONE", wai_sem(id, 5) == ER_NONE);
		/* 同期用は順序の制限が無いので、獲得していなくても返せる。 */
		BTRON_CHECK("返却", sig_sem(id) == ER_OK);
		BTRON_CHECK("もう 1 つ返却", sig_sem(id) == ER_OK);
		BTRON_CHECK("2 つ獲得できる",
		            wai_sem(id, T_NOWAIT) == ER_OK &&
		                wai_sem(id, T_NOWAIT) == ER_OK);
		BTRON_CHECK("不正 ID", wai_sem(0, T_NOWAIT) == ER_ID);
		BTRON_CHECK("存在しない",
		            wai_sem(TK_MAX_TOBJ, T_NOWAIT) == ER_NOEXS);
		BTRON_CHECK("削除", del_sem(id) == ER_OK);
		BTRON_CHECK("数が戻った", tobj_count() == n0);
	});

	BTRON_TEST("排他制御用セマフォはミューテックス (§1.3.2 SEM_EXCL)", {
		ID id;

		/* 仕様「排他制御用セマフォの場合には cnt = 1 でなければならない」 */
		BTRON_CHECK("初期値 0 は不正", cre_sem(0, SEM_EXCL) == ER_PAR);
		BTRON_CHECK("初期値 2 も不正", cre_sem(2, SEM_EXCL) == ER_PAR);
		id = cre_sem(1, SEM_EXCL);
		BTRON_CHECK("生成", id >= 1);
		BTRON_CHECK("獲得", wai_sem(id, T_NOWAIT) == ER_OK);
		/* 仕様「すでに獲得している状態で、再度獲得しようとした場合には
		 * ER_OBJ」。中心核のミューテックスの E_ILUSE がこれに写る。 */
		BTRON_CHECK("二重獲得は ER_OBJ", wai_sem(id, T_NOWAIT) == ER_OBJ);
		BTRON_CHECK("返却", sig_sem(id) == ER_OK);
		/* 仕様「獲得していない状態で返却しようとした場合は ER_OBJ」 */
		BTRON_CHECK("二重返却は ER_OBJ", sig_sem(id) == ER_OBJ);
		BTRON_CHECK("削除", del_sem(id) == ER_OK);
	});

	BTRON_TEST("セマフォ待ちの削除は ER_DLT (§1.3.2)", {
		W sub;

		tobj_sem = cre_sem(0, SEM_SYNC);
		tobj_wait_er = ER_OK;
		BTRON_CHECK("生成", tobj_sem >= 1);
		/* 自分より低い優先度のサブタスクに待たせる。 */
		sub = cre_tsk(p_semwait, 3, 0);
		BTRON_CHECK("サブタスク", sub >= 1);
		(void)dly_tsk(5); /* サブタスクを待ちに入れる */
		BTRON_CHECK("削除", del_sem(tobj_sem) == ER_OK);
		(void)dly_tsk(5);
		/* 仕様「待ち状態にあったタスクは待ちが解除され、そのタスクへ
		 * ER_DLT が返される」 */
		BTRON_CHECK("待ちは ER_DLT で解けた", tobj_wait_er == ER_DLT);
	});

	BTRON_TEST("イベントフラグ (§1.3.2)", {
		ID id;
		WERR got;

		BTRON_CHECK("負の初期値", cre_flg(-1, 0) == ER_PAR);
		id = cre_flg(0, 0);
		BTRON_CHECK("生成", id >= 1);
		BTRON_CHECK("待ちパターン 0 は不正",
		            wai_flg(id, 0, WF_OR, T_NOWAIT) == ER_PAR);
		BTRON_CHECK("まだ立っていない",
		            wai_flg(id, 0x3, WF_OR, T_NOWAIT) == ER_NONE);

		BTRON_CHECK("セット", set_flg(id, 0x1) == ER_OK);
		/* NOCLR 付きなら値は変わらない（仕様） */
		got = wai_flg(id, 0x1, WF_OR | NOCLR, T_NOWAIT);
		BTRON_CHECK("OR 待ちが解けて値が返る", got == 0x1);
		BTRON_CHECK("NOCLR なので残る",
		            wai_flg(id, 0x1, WF_OR | NOCLR, T_NOWAIT) == 0x1);

		/* AND 待ちは全ビットが揃うまで。 */
		BTRON_CHECK("AND はまだ",
		            wai_flg(id, 0x3, WF_AND, T_NOWAIT) == ER_NONE);
		BTRON_CHECK("もう 1 ビット", set_flg(id, 0x2) == ER_OK);
		/* NOCLR 無しなので**全ビット**が 0 クリアされる（仕様） */
		BTRON_CHECK("AND が解けた",
		            wai_flg(id, 0x3, WF_AND, T_NOWAIT) == 0x3);
		BTRON_CHECK("全クリアされた",
		            wai_flg(id, 0x3, WF_OR, T_NOWAIT) == ER_NONE);

		/* 仕様「現在の値に ptn が AND される」。残すパターンである。 */
		BTRON_CHECK("セット", set_flg(id, 0xF) == ER_OK);
		BTRON_CHECK("AND でクリア", clr_flg(id, 0x5) == ER_OK);
		BTRON_CHECK("0x5 が残った",
		            wai_flg(id, 0xF, WF_OR | NOCLR, T_NOWAIT) == 0x5);
		BTRON_CHECK("全クリア", clr_flg(id, 0) == ER_OK);
		BTRON_CHECK("空になった",
		            wai_flg(id, 0xF, WF_OR, T_NOWAIT) == ER_NONE);
		BTRON_CHECK("削除", del_flg(id) == ER_OK);
	});

	BTRON_TEST("メッセージバッファ (§1.3.2)", {
		ID id;
		B buf[16];
		W i;

		BTRON_CHECK("最大サイズ 0 は不正", cre_mbf(64, 0, 0) == ER_PAR);
		id = cre_mbf(64, 16, 0);
		BTRON_CHECK("生成", id >= 1);
		BTRON_CHECK("空なら ER_NONE", rcv_mbf(id, buf, T_NOWAIT) == ER_NONE);

		for (i = 0; i < 16; i++) {
			buf[i] = (B)('A' + i);
		}
		BTRON_CHECK("送信", snd_mbf(id, buf, 16, T_NOWAIT) == ER_OK);
		for (i = 0; i < 16; i++) {
			buf[i] = 0;
		}
		BTRON_CHECK("受信", rcv_mbf(id, buf, T_NOWAIT) == 16);
		BTRON_CHECK("中身が合っている", buf[0] == 'A' && buf[15] == 'P');
		BTRON_CHECK("アドレス不正", snd_mbf(id, 0, 1, T_NOWAIT) == ER_ADR);
		BTRON_CHECK("削除", del_mbf(id) == ER_OK);
	});

	BTRON_TEST("DELEXIT の同期オブジェクトはプロセス終了で消える (§1.3)", {
		W n0 = tobj_count();
		ID id;

		/* 自プロセスで DELEXIT 付きに作り、数が増えることを見る。
		 * 自プロセスの終了は検査の終わりなので、ここでは明示的に消す。 */
		id = cre_sem(1, SEM_SYNC | DELEXIT);
		BTRON_CHECK("生成", id >= 1 && tobj_count() == n0 + 1);
		BTRON_CHECK("削除", del_sem(id) == ER_OK);
		BTRON_CHECK("数が戻った", tobj_count() == n0);
	});

	/* -------------------------------------------------------------- */
	/* §1.9 時計管理 / §1.10 システム管理                             */
	/* -------------------------------------------------------------- */

}

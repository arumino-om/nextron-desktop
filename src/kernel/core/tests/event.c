/*
 * event.c — 周辺核 イベント管理 (§1.7)
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

BTRON_TEST_GROUP("周辺核 イベント管理 (§1.7)", 280, TST_PHASE_PROC, tst_event)
{
	BTRON_TEST("起動時はイベント管理が動いていない (§1.7.2)", {
		EVENT ev;
		W old;

		/* 仕様「システムのスタートアップ時点では…システムイベントマスクは
		 * 0 となっており、イベント管理機能は事実上動作していない」 */
		old = chg_emk(EM_NULL);
		BTRON_CHECK("マスクを 0 に戻せる", old >= 0);
		ev.type = EV_APPL1;
		ev.time = 0;
		ev.pos.x = 0;
		ev.pos.y = 0;
		ev.data.info = 1;
		ev.stat = 0;
		/* 仕様「システムイベントマスクで対象外のイベントは実際には発生せず
		 * 無視される」。**エラーではない。** */
		BTRON_CHECK("無視される", put_evt(&ev, EP_NONE) == ER_OK);
		BTRON_CHECK("入っていない", evt_count() == 0);

		BTRON_CHECK("有効にする", chg_emk(EM_ALL) == EM_NULL);
		BTRON_CHECK("入るようになった",
		            put_evt(&ev, EP_NONE) == ER_OK && evt_count() == 1);
		BTRON_CHECK("片付け", clr_evt(EM_ALL, EM_NULL) == ER_OK);
		BTRON_CHECK("マスクの範囲外", chg_emk(0x10000) == ER_PAR);
	});

	BTRON_TEST("イベントは優先度順に取り出される (§1.7.2)", {
		EVENT ev;
		EVENT got;

		(void)chg_emk(EM_ALL);
		(void)clr_evt(EM_ALL, EM_NULL);

		/* 仕様の優先度: APPL1〜4 > ボタン/キー > AUTKEY > DEVICE/RSV
		 *               > APPL5〜8 > NULL
		 * **発生順とは別。** 低い優先度を先に入れて確かめる。 */
		ev.time = 0;
		ev.pos.x = 1;
		ev.pos.y = 2;
		ev.stat = 0;
		ev.type = EV_APPL5; /* 優先度 5 */
		ev.data.info = 5;
		BTRON_CHECK("APPL5", put_evt(&ev, EP_NONE) == ER_OK);
		ev.type = EV_DEVICE; /* 優先度 4 */
		ev.data.dev.kind = DE_MOUNT;
		ev.data.dev.devno = 1;
		BTRON_CHECK("DEVICE", put_evt(&ev, EP_NONE) == ER_OK);
		ev.type = EV_BUTDWN; /* 優先度 2 */
		ev.data.info = 0;
		BTRON_CHECK("BUTDWN", put_evt(&ev, EP_NONE) == ER_OK);
		ev.type = EV_APPL1; /* 優先度 1 */
		ev.data.info = 1;
		BTRON_CHECK("APPL1", put_evt(&ev, EP_NONE) == ER_OK);

		BTRON_CHECK("最初は APPL1",
		            get_evt(EM_ALL, &got, CLR) == EV_APPL1);
		BTRON_CHECK("次は BUTDWN",
		            get_evt(EM_ALL, &got, CLR) == EV_BUTDWN);
		BTRON_CHECK("次は DEVICE",
		            get_evt(EM_ALL, &got, CLR) == EV_DEVICE);
		BTRON_CHECK("種別が渡る", got.data.dev.kind == DE_MOUNT);
		BTRON_CHECK("最後は APPL5",
		            get_evt(EM_ALL, &got, CLR) == EV_APPL5);
		/* 仕様「指定したタイプのイベントが発生していないときは、EV_NULL が
		 * 取り出される」 */
		BTRON_CHECK("空なら EV_NULL",
		            get_evt(EM_ALL, &got, CLR) == EV_NULL);

		/* 同一優先度は発生順。 */
		ev.type = EV_APPL1;
		ev.data.info = 11;
		(void)put_evt(&ev, EP_NONE);
		ev.type = EV_APPL2;
		ev.data.info = 22;
		(void)put_evt(&ev, EP_NONE);
		BTRON_CHECK("先に入れた方が先",
		            get_evt(EM_ALL, &got, CLR) == EV_APPL1 &&
		                got.data.info == 11);
		BTRON_CHECK("次", get_evt(EM_ALL, &got, CLR) == EV_APPL2);
	});

	BTRON_TEST("イベントのエラーコードと属性 (§1.7.6)", {
		EVENT ev;
		EVENT got;
		PNT p;

		(void)clr_evt(EM_ALL, EM_NULL);
		ev.type = EV_APPL1;
		ev.time = 0;
		ev.pos.x = 0;
		ev.pos.y = 0;
		ev.data.info = 0;
		ev.stat = 0;

		BTRON_CHECK("アドレス不正", put_evt(0, EP_NONE) == ER_ADR);
		BTRON_CHECK("属性不正", put_evt(&ev, 0x100) == ER_PAR);
		/* 仕様「EV_NULL および EV_AUTKEY の指定したときはエラー」 */
		ev.type = EV_NULL;
		BTRON_CHECK("EV_NULL は発生させられない",
		            put_evt(&ev, EP_NONE) == ER_PAR);
		ev.type = EV_AUTKEY;
		BTRON_CHECK("EV_AUTKEY も同じ",
		            put_evt(&ev, EP_NONE) == ER_PAR);
		BTRON_CHECK("取得のアドレス不正",
		            get_evt(EM_ALL, 0, CLR) == ER_ADR);
		BTRON_CHECK("取得のマスクが 0",
		            get_evt(0, &got, CLR) == ER_PAR);
		BTRON_CHECK("取得の属性不正",
		            get_evt(EM_ALL, &got, 7) == ER_PAR);

		/* EP_POS / EP_TIME で現在値が入る。 */
		p.x = 123;
		p.y = 45;
		BTRON_CHECK("PD 位置の設定", set_pdp(p) == ER_OK);
		BTRON_CHECK("PD 位置の取得",
		            get_pdp(&p) == ER_OK && p.x == 123 && p.y == 45);
		ev.type = EV_APPL1;
		ev.pos.x = 0;
		ev.pos.y = 0;
		ev.time = 0;
		BTRON_CHECK("発生", put_evt(&ev, EP_ALL) == ER_OK);
		BTRON_CHECK("取得", get_evt(EM_APPL1, &got, CLR) == EV_APPL1);
		BTRON_CHECK("pos が入った", got.pos.x == 123 && got.pos.y == 45);
		BTRON_CHECK("time が入った", got.time > 0);

		/* NOCLR では残る。 */
		(void)put_evt(&ev, EP_NONE);
		BTRON_CHECK("NOCLR", get_evt(EM_APPL1, &got, NOCLR) == EV_APPL1);
		BTRON_CHECK("残っている", evt_count() == 1);
		BTRON_CHECK("CLR", get_evt(EM_APPL1, &got, CLR) == EV_APPL1);
		BTRON_CHECK("消えた", evt_count() == 0);
	});

	BTRON_TEST("自動リピートキー (§1.7.2 / §1.7.3)", {
		EVENT ev;
		EVENT got;
		KeyMap km;
		W off = 0;
		W itv = 0;

		(void)clr_evt(EM_ALL, EM_NULL);
		BTRON_CHECK("リピート時間の設定", set_krp(20, 10) == ER_OK);
		BTRON_CHECK("取得",
		            get_krp(&off, &itv) == ER_OK && off == 20 && itv == 10);

		ev.type = EV_KEYDWN;
		ev.time = 0;
		ev.pos.x = 0;
		ev.pos.y = 0;
		ev.data.key.keytop = 65;
		ev.data.key.code = (TC)'A';
		ev.stat = 0;
		BTRON_CHECK("キーを押す", put_evt(&ev, EP_TIME) == ER_OK);
		/* キー状態が立つ（本来はドライバの仕事） */
		BTRON_CHECK("キー状態", get_kmp(km) == ER_OK);
		BTRON_CHECK("押されている", ((km[65 / 8] >> (65 % 8)) & 1) != 0);
		BTRON_CHECK("KEYDWN が取れる",
		            get_evt(EM_ALL, &got, CLR) == EV_KEYDWN);
		/* オフセット前はまだ出ない。 */
		BTRON_CHECK("まだリピートしない",
		            get_evt(EM_AUTKEY, &got, CLR) == EV_NULL);
		(void)dly_tsk(30);
		/* **自動リピートはキューに入らず、取り出し要求時に生成される**（仕様）。 */
		BTRON_CHECK("リピートが出る",
		            get_evt(EM_AUTKEY, &got, CLR) == EV_AUTKEY);
		BTRON_CHECK("押したキーが入る",
		            got.data.key.keytop == 65 && got.data.key.code == (TC)'A');
		BTRON_CHECK("キューには入っていない", evt_count() == 0);
		(void)dly_tsk(20);
		BTRON_CHECK("間隔を置いてまた出る",
		            get_evt(EM_AUTKEY, &got, CLR) == EV_AUTKEY);

		/* キーを離すと止まる。 */
		ev.type = EV_KEYUP;
		BTRON_CHECK("キーを離す", put_evt(&ev, EP_TIME) == ER_OK);
		BTRON_CHECK("KEYUP が取れる",
		            get_evt(EM_ALL, &got, CLR) == EV_KEYUP);
		(void)dly_tsk(30);
		BTRON_CHECK("もうリピートしない",
		            get_evt(EM_AUTKEY, &got, CLR) == EV_NULL);

		/* リピート対象外のキーはリピートしない（仕様の条件⑤）。 */
		{
			KeyMap none;
			W i;

			for (i = 0; i < KEYMAX / 8; i++) {
				none[i] = 0;
			}
			BTRON_CHECK("対象を空にする", set_krm(none) == ER_OK);
			ev.type = EV_KEYDWN;
			(void)put_evt(&ev, EP_TIME);
			(void)get_evt(EM_ALL, &got, CLR);
			(void)dly_tsk(30);
			BTRON_CHECK("対象外はリピートしない",
			            get_evt(EM_AUTKEY, &got, CLR) == EV_NULL);
			BTRON_CHECK("取得", get_krm(km) == ER_OK && km[0] == 0);
			for (i = 0; i < KEYMAX / 8; i++) {
				none[i] = 0xFF;
			}
			(void)set_krm(none);
			ev.type = EV_KEYUP;
			(void)put_evt(&ev, EP_TIME);
			(void)clr_evt(EM_ALL, EM_NULL);
		}
	});

	BTRON_TEST("キーボードと変換表 (§1.7.3)", {
		KBD_ID kid;
		KeyTab *kt;
		VP mem = 0;
		UW etm = 0;

		BTRON_CHECK("キーボード ID", get_kid(&kid) == ER_OK);
		/* 物理キーボードは接続されていない。 */
		BTRON_CHECK("未定義キーボード", kid.kind == 0);
		BTRON_CHECK("イベントタイマー",
		            get_etm(&etm) == ER_OK && etm > 0);

		/* 変換表。KeyTab は 8KB 近いのでメモリブロックに置く。 */
		BTRON_CHECK("設定前は存在しない", get_ktb(0) == ER_ADR);
		if (get_mbk(&mem, 4, 0) == ER_OK) {
			kt = (KeyTab *)mem;
			kt->keymax = 256;
			kt->kctmax = 4;
			{
				W i;

				for (i = 0; i < KCTSEL; i++) {
					/* CERLKA の 6 ビット。シフト無しは表 0、
					 * 左シフト (KM_L) は表 1 にする。 */
					kt->kctsel[i] = (UH)(((i & KM_L) != 0) ? 1 : 0);
				}
				for (i = 0; i < KCTMAX; i++) {
					kt->kct[i] = 0;
				}
			}
			kt->kct[0 * 256 + 65] = (UH)'a'; /* 表 0、キー 65 */
			kt->kct[1 * 256 + 65] = (UH)'A'; /* 表 1 */
			BTRON_CHECK("設定", set_ktb(kt) == ER_OK);
			BTRON_CHECK("取得",
			            get_ktb(kt) == (W)sizeof(KeyTab));
			BTRON_CHECK("表 0 の中身", kt->kct[0 * 256 + 65] == (UH)'a');
			BTRON_CHECK("左シフトは表 1",
			            kt->kctsel[KM_L] == 1 &&
			                kt->kct[1 * 256 + 65] == (UH)'A');
			/* 上限。keymax × kctmax が KCTMAX を超えてはいけない。 */
			kt->kctmax = 64;
			BTRON_CHECK("表が大きすぎる", set_ktb(kt) == ER_PAR);
			kt->keymax = 0;
			BTRON_CHECK("キー数が不正", set_ktb(kt) == ER_PAR);
			(void)rel_mbk(mem);
		}
	});

	BTRON_TEST("イベントメッセージ要求 (§1.7.6 req_evt)", {
		EVENT ev;
		MESSAGE r;
		CONST EVENT *rev;

		(void)clr_evt(EM_ALL, EM_NULL);
		(void)clr_msg(MM_ALL, MM_NULL);
		BTRON_CHECK("要求", req_evt(EM_APPL3) == ER_OK);

		ev.type = EV_APPL3;
		ev.time = 0;
		ev.pos.x = 7;
		ev.pos.y = 8;
		ev.data.info = 0x4321;
		ev.stat = 0;
		BTRON_CHECK("発生", put_evt(&ev, EP_NONE) == ER_OK);
		/* 仕様「msg_type は、MS_SYS5 固定」 */
		BTRON_CHECK("メッセージが来た",
		            rcv_msg(MM_SYS5, &r, (W)sizeof(r), NOWAIT) >= 0);
		BTRON_CHECK("MS_SYS5", r.msg_type == MS_SYS5);
		BTRON_CHECK("大きさ", r.msg_size == (W)sizeof(EVENT));
		rev = (CONST EVENT *)(CONST VP)&r.msg_body;
		BTRON_CHECK("イベントが入っている",
		            rev->type == EV_APPL3 && rev->data.info == 0x4321);
		/* 仕様「メッセージとして送信されても、そのイベントはイベント
		 * キューから取り除かれない」 */
		BTRON_CHECK("キューにも残っている", evt_count() == 1);

		/* 対象外のタイプはメッセージにならない。 */
		ev.type = EV_APPL4;
		(void)put_evt(&ev, EP_NONE);
		BTRON_CHECK("対象外は来ない",
		            rcv_msg(MM_SYS5, &r, (W)sizeof(r), NOWAIT) == ER_NONE);

		/* 仕様「EM_NULL を指定することにより…要求が解除される」 */
		BTRON_CHECK("解除", req_evt(EM_NULL) == ER_OK);
		ev.type = EV_APPL3;
		(void)put_evt(&ev, EP_NONE);
		BTRON_CHECK("もう来ない",
		            rcv_msg(MM_SYS5, &r, (W)sizeof(r), NOWAIT) == ER_NONE);
		(void)clr_evt(EM_ALL, EM_NULL);
		(void)clr_msg(MM_ALL, MM_NULL);
	});

	BTRON_TEST("clr_evt と las_evt (§1.7.6)", {
		EVENT ev;
		W el;

		(void)clr_evt(EM_ALL, EM_NULL);
		ev.time = 0;
		ev.pos.x = 0;
		ev.pos.y = 0;
		ev.data.info = 0;
		ev.stat = 0;
		ev.type = EV_APPL1;
		(void)put_evt(&ev, EP_TIME);
		ev.type = EV_APPL5;
		(void)put_evt(&ev, EP_TIME);
		ev.type = EV_APPL1;
		(void)put_evt(&ev, EP_TIME);
		BTRON_CHECK("3 つ", evt_count() == 3);
		/* APPL5 の直前まで = APPL1 が 1 つ消える。 */
		BTRON_CHECK("直前まで", clr_evt(EM_APPL1, EM_APPL5) == ER_OK);
		BTRON_CHECK("1 つ消えた", evt_count() == 2);
		/* EM_ALL の特例: 1 つだけ消す。 */
		BTRON_CHECK("1 つだけ", clr_evt(EM_ALL, EM_ALL) == ER_OK);
		BTRON_CHECK("もう 1 つ消えた", evt_count() == 1);
		BTRON_CHECK("全部", clr_evt(EM_ALL, EM_NULL) == ER_OK);
		BTRON_CHECK("空", evt_count() == 0);
		BTRON_CHECK("マスクが 0", clr_evt(0, EM_NULL) == ER_PAR);

		(void)dly_tsk(15);
		el = las_evt(EM_APPL1);
		BTRON_CHECK("最終発生からの経過時間", el >= 10);
		BTRON_CHECK("マスクが 0", las_evt(0) == ER_PAR);
	});

	BTRON_TEST("ブザー (§1.7.6)", {
		UW v = 0;

		/* 音源は無いが設定は保持される。 */
		BTRON_CHECK("鳴らしても失敗しない", sig_buz(0) == ER_OK);
		v = 7;
		BTRON_CHECK("音量の設定",
		            ctl_buz(SET_BUZ_VOLUME, &v) == ER_OK);
		v = 0;
		BTRON_CHECK("音量の取得",
		            ctl_buz(GET_BUZ_VOLUME, &v) == ER_OK && v == 7);
		BTRON_CHECK("アドレス不正", ctl_buz(GET_BUZ_VOLUME, 0) == ER_ADR);
	});

	/* -------------------------------------------------------------- */
	/* §1.8 デバイス管理                                              */
	/* -------------------------------------------------------------- */

}

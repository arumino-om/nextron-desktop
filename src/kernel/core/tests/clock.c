/*
 * clock.c — 周辺核 時計管理とシステム管理 (§1.9 / §1.10)
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

LOCAL CONST TC nm_cpunum[] = {(TC)'C', (TC)'P', (TC)'U', (TC)'N', (TC)'U',
                              (TC)'M', TNULL};

LOCAL CONST TC nm_osname[] = {(TC)'O', (TC)'S', (TC)'N', (TC)'A', (TC)'M',
                              (TC)'E', TNULL};

LOCAL CONST TC nm_nope[] = {(TC)'N', (TC)'O', (TC)'P', (TC)'E', TNULL};

BTRON_TEST_GROUP("周辺核 時計管理とシステム管理 (§1.9 / §1.10)", 240, TST_PHASE_PROC, tst_clock)
{
	BTRON_TEST("システム時間とカレンダーの変換 (§1.9)", {
		STIME t = 0;
		TIMEZONE tz;
		DATE_TIM dt;
		STIME back = 0;

		BTRON_CHECK("取得", get_tim(&t, &tz) == ER_OK);
		BTRON_CHECK("時差の初期値", tz.adjust == 0 && tz.dst_flg == 0);

		/* 原点。1985-01-01 は火曜日。 */
		BTRON_CHECK("原点の変換", get_tod(&dt, 1, 0) == ER_OK);
		BTRON_CHECK("1985 年",
		            dt.d_year == STIME_EPOCH_YEAR - 1900 &&
		                dt.d_month == 1 && dt.d_day == 1);
		BTRON_CHECK("火曜日", dt.d_wday == 2);
		BTRON_CHECK("年内 1 日目", dt.d_days == 1);

		/* 閏年の 2 月 29 日が通ること。2024-02-29 は木曜日。 */
		dt.d_year = 2024 - 1900;
		dt.d_month = 2;
		dt.d_day = 29;
		dt.d_hour = 12;
		dt.d_min = 34;
		dt.d_sec = 56;
		BTRON_CHECK("閏日 → システム時間", set_tod(&dt, &t, 0) == ER_OK);
		BTRON_CHECK("往復", get_tod(&dt, t, 0) == ER_OK);
		BTRON_CHECK("日付が戻った",
		            dt.d_year == 2024 - 1900 && dt.d_month == 2 &&
		                dt.d_day == 29);
		BTRON_CHECK("時刻が戻った",
		            dt.d_hour == 12 && dt.d_min == 34 && dt.d_sec == 56);
		BTRON_CHECK("木曜日", dt.d_wday == 4);
		BTRON_CHECK("年内 60 日目", dt.d_days == 31 + 29);

		/* 仕様「d_days の値は d_month = 0 のときのみ有効」 */
		dt.d_month = 0;
		dt.d_days = 60;
		dt.d_hour = 0;
		dt.d_min = 0;
		dt.d_sec = 0;
		BTRON_CHECK("通算日で指定", set_tod(&dt, &back, 0) == ER_OK);
		BTRON_CHECK("同じ日になる",
		            get_tod(&dt, back, 0) == ER_OK && dt.d_month == 2 &&
		                dt.d_day == 29);

		/* 時差。ローカル = システム - adjust */
		tz.adjust = -9 * 3600; /* 日本標準時。グリニッチから東 9 時間 */
		tz.dst_flg = 0;
		tz.dst_adj = 0;
		BTRON_CHECK("時差の設定", set_tim(0, &tz) == ER_OK);
		BTRON_CHECK("GMT", get_tod(&dt, 1, 0) == ER_OK && dt.d_hour == 0);
		BTRON_CHECK("ローカルは +9 時間",
		            get_tod(&dt, 1, 1) == ER_OK && dt.d_hour == 9);

		tz.adjust = TZ_ADJUST_MAX + 1;
		BTRON_CHECK("時差が範囲外", set_tim(0, &tz) == ER_PAR);
		tz.adjust = 0;
		BTRON_CHECK("戻す", set_tim(0, &tz) == ER_OK);
		BTRON_CHECK("アドレス不正", get_tod(0, 1, 0) == ER_ADR);

		dt.d_month = 13;
		BTRON_CHECK("月が範囲外", set_tod(&dt, &back, 0) == ER_PAR);
	});

	BTRON_TEST("OS バージョンと構成情報 (§1.10)", {
		T_VER v;
		W num = 0;
		TC str[16];

		BTRON_CHECK("アドレス不正", get_ver(0) == ER_ADR);
		BTRON_CHECK("取得", get_ver(&v) == ER_OK);
		BTRON_CHECK("仕様書バージョンは BTRON3", v.spver == OS_SPVER);
		BTRON_CHECK("CPU が入っている", v.cpu != 0);

		/* 仕様「関数値 = 0 は W の数値情報」 */
		BTRON_CHECK("数値項目",
		            get_cnf(nm_cpunum, &num, (W)sizeof(num)) == 0);
		BTRON_CHECK("CPU 数が取れた", num >= 1);
		/* 仕様「関数値 > 0 は TC 文字列情報で、関数値は文字数」 */
		BTRON_CHECK("文字列項目",
		            get_cnf(nm_osname, str, (W)sizeof(str)) == 6);
		BTRON_CHECK("中身", str[0] == (TC)'B' && str[6] == TNULL);
		BTRON_CHECK("無い項目",
		            get_cnf(nm_nope, &num, (W)sizeof(num)) == ER_NOEXS);
		BTRON_CHECK("アドレス不正",
		            get_cnf(0, &num, (W)sizeof(num)) == ER_ADR);
	});

	/* -------------------------------------------------------------- */
	/* 外殻マネージャ（フォントマネージャ）                            */
	/* -------------------------------------------------------------- */

}

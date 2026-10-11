/*
 * clock.c — 周辺核 §1.9「時計管理」
 *
 * get_tim / set_tim / get_tod / set_tod。
 *
 * ------------------------------------------------------------------------
 * 時計は 1 つ。単位だけ境界で換算する
 * ------------------------------------------------------------------------
 *
 * 中心核の `SYSTIM` は**ミリ秒**、BTRON3 の `STIME` は**秒**である。
 * 2 つ別に持つと必ずずれるので、**時計は中心核の 1 つだけ**にして、
 * ここで 1000 倍の換算をする（規律 R7: 変換は境界にのみ置く）。
 * `SYSTIM` の原点はこの仕様に合わせて 1985-01-01 00:00:00 GMT とする。
 *
 * 実時計 (RTC) はまだ読んでいないので、起動直後は 1985-01-01 である。
 * UEFI のランタイムサービス (`GetTime`) で合わせられる — BootInfo に
 * システムテーブルを残してあるので、デバイス管理 (§1.8) と一緒に入れる。
 *
 * ------------------------------------------------------------------------
 * 暦の変換
 * ------------------------------------------------------------------------
 *
 * グレゴリオ暦の日数計算は 400 年周期 (146097 日) を使う定石の形で書く。
 * 月を 3 月始まりにずらすと閏日が周期の末尾に来るので、分岐が消える。
 */

#include <btron/clock.h>
#include <btron/proc.h>

#include "../core/knl.h"

#define SECS_PER_DAY 86400

/* 1970-01-01 から 1985-01-01 までの日数。
 * 15 年 = 365×15 + 閏日 4 (1972/1976/1980/1984)。 */
#define EPOCH_DAYS 5479

LOCAL TIMEZONE tz_cur;

/* ------------------------------------------------------------------ */
/* 暦                                                                  */
/* ------------------------------------------------------------------ */

/* 西暦 y 年 m 月 d 日 → 1970-01-01 を 0 とする日数。 */
LOCAL D days_from_civil(W y, W m, W d)
{
	D era;
	W yoe;
	W doy;
	D doe;

	y -= (m <= 2) ? 1 : 0;
	era = (D)((y >= 0) ? y : (y - 399)) / 400;
	yoe = (W)((D)y - era * 400); /* 0..399 */
	doy = (153 * (m + ((m > 2) ? -3 : 9)) + 2) / 5 + d - 1;
	doe = (D)yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

/* 1970-01-01 を 0 とする日数 → 西暦。 */
LOCAL void civil_from_days(D z, W *y, W *m, W *d)
{
	D era;
	W doe;
	W yoe;
	W doy;
	W mp;
	W yy;

	z += 719468;
	era = ((z >= 0) ? z : (z - 146096)) / 146097;
	doe = (W)(z - era * 146097); /* 0..146096 */
	yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	yy = (W)((D)yoe + era * 400);
	doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	mp = (5 * doy + 2) / 153;
	*d = doy - (153 * mp + 2) / 5 + 1;
	*m = mp + ((mp < 10) ? 3 : -9);
	*y = yy + ((*m <= 2) ? 1 : 0);
}

/* ------------------------------------------------------------------ */
/* 時計本体                                                            */
/* ------------------------------------------------------------------ */

LOCAL STIME now_stime(void)
{
	SYSTIM ms = 0;

	(void)tk_get_tim(&ms);
	/* 負にはならないが、下向きに丸めるので 0 近傍だけ注意する。 */
	return (STIME)((ms >= 0) ? (ms / 1000) : 0);
}

/* ローカル時間の補正量（秒）。
 * ローカル = システム - adjust + (dst_flg ? dst_adj × 60 : 0) */
LOCAL D local_delta(void)
{
	D d = -(D)tz_cur.adjust;

	if (tz_cur.dst_flg != 0) {
		d += (D)tz_cur.dst_adj * 60;
	}
	return d;
}

LOCAL Bool is_level0(void)
{
	P_USER u;

	if (get_usr(P_SELF, &u) < 1) {
		return False;
	}
	return (u.level == P_LEVEL_MAX) ? True : False;
}

EXPORT ERR get_tim(STIME *time, TIMEZONE *tz)
{
	UD state = hal_int_disable();

	if (time != 0) {
		*time = now_stime();
	}
	if (tz != 0) {
		*tz = tz_cur;
	}
	hal_int_restore(state);
	return ER_OK;
}

EXPORT ERR set_tim(STIME time, CONST TIMEZONE *tz)
{
	UD state;

	/* 仕様「時間の設定はユーザレベル 0 のプロセス以外から行なうことは
	 * 禁止されている」 */
	if (!is_level0()) {
		return ER_ACCES;
	}
	if (tz != 0) {
		if (tz->adjust < -TZ_ADJUST_MAX || tz->adjust > TZ_ADJUST_MAX) {
			return ER_PAR;
		}
		if (tz->dst_adj < -TZ_DSTADJ_MAX || tz->dst_adj > TZ_DSTADJ_MAX) {
			return ER_PAR;
		}
	}
	state = hal_int_disable();
	if (time > 0) {
		SYSTIM ms = (SYSTIM)time * 1000;

		(void)tk_set_tim(&ms);
	}
	if (tz != 0) {
		tz_cur = *tz;
	}
	hal_int_restore(state);
	return ER_OK;
}

EXPORT ERR get_tod(DATE_TIM *dt, STIME time, Bool local)
{
	D secs;
	D days;
	W tod;
	W y;
	W m;
	W d;
	D jan1;

	if (dt == 0) {
		return ER_ADR;
	}
	secs = (time > 0) ? (D)time : (D)now_stime();
	if (local != 0) {
		secs += local_delta();
	}
	secs += (D)EPOCH_DAYS * SECS_PER_DAY; /* 1985 原点 → 1970 原点 */

	days = secs / SECS_PER_DAY;
	tod = (W)(secs - days * SECS_PER_DAY);
	if (tod < 0) { /* 負の時刻は前日に繰り下げる */
		tod += SECS_PER_DAY;
		days--;
	}
	civil_from_days(days, &y, &m, &d);

	dt->d_year = y - 1900; /* 仕様: 1900 年からのオフセット */
	dt->d_month = m;
	dt->d_day = d;
	dt->d_hour = tod / 3600;
	dt->d_min = (tod / 60) % 60;
	dt->d_sec = tod % 60;
	/* 1970-01-01 は木曜なので +4 して 0 = 日曜に合わせる。 */
	dt->d_wday = (W)(((days % 7) + 7 + 4) % 7);
	jan1 = days_from_civil(y, 1, 1);
	dt->d_days = (W)(days - jan1) + 1;
	/* 仕様「その年の 1 月 1 日の週を 1 とした場合の週の数」で範囲が 1〜54。
	 * 54 まで届くのは、1 月 1 日が週の後ろの方にある年があるから。
	 * つまり**暦上の週の境界で数える**。 */
	{
		W jan1_wday = (W)(((jan1 % 7) + 7 + 4) % 7);

		dt->d_week = (dt->d_days - 1 + jan1_wday) / 7 + 1;
	}
	return ER_OK;
}

EXPORT ERR set_tod(CONST DATE_TIM *dt, STIME *time, Bool local)
{
	D days;
	D secs;
	W y;

	if (dt == 0) {
		return ER_ADR;
	}
	/* 仕様「time = NULL なら現在のシステム時間として設定する
	 * （ユーザレベル 0 のプロセスのみ許可）」 */
	if (time == 0 && !is_level0()) {
		return ER_ACCES;
	}
	if (dt->d_hour < 0 || dt->d_hour > 23 || dt->d_min < 0 ||
	    dt->d_min > 59 || dt->d_sec < 0 || dt->d_sec > 59) {
		return ER_PAR;
	}
	y = dt->d_year + 1900;
	if (dt->d_month == 0) {
		/* 仕様「d_days の値は d_month = 0 のときのみ有効」。
		 * 月日の代わりに年内通算日で指定する形。 */
		if (dt->d_days < 1 || dt->d_days > 366) {
			return ER_PAR;
		}
		days = days_from_civil(y, 1, 1) + (D)(dt->d_days - 1);
	} else {
		if (dt->d_month < 1 || dt->d_month > 12 || dt->d_day < 1 ||
		    dt->d_day > 31) {
			return ER_PAR;
		}
		days = days_from_civil(y, dt->d_month, dt->d_day);
	}
	secs = days * SECS_PER_DAY + (D)dt->d_hour * 3600 +
	       (D)dt->d_min * 60 + (D)dt->d_sec;
	secs -= (D)EPOCH_DAYS * SECS_PER_DAY; /* 1970 原点 → 1985 原点 */
	if (local != 0) {
		secs -= local_delta();
	}
	if (time != 0) {
		*time = (STIME)secs;
		return ER_OK;
	}
	return set_tim((STIME)secs, 0);
}

EXPORT ERR clk_init(void)
{
	tz_cur.adjust = 0;
	tz_cur.dst_flg = 0;
	tz_cur.dst_adj = 0;
	return ER_OK;
}

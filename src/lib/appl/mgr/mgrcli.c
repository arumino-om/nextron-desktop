/*
 * mgrcli.c — 外殻マネージャの利用側
 *
 * **ここは `src/lib` 側（LGPL 予定。J2 / 規律 R14）。** サーバを立てる側は
 * `src/subsystems/mgr/manager.c`（GPL）で、こちらはマネージャを**呼ぶ**
 * アプリケーションが結合する部分である。
 *
 * 外殻をユーザ特権へ移すと、この分割が実利になる。ユーザプログラムは
 * 利用側だけを結合すればよく、サーバ本体を持ち込まずに済む。
 *
 * 呼ぶ側がすることは 2 つだけ。
 *
 *   1. グローバル名でポートを引く（プロセスごとに 1 度でよい）
 *   2. ランデブで 1 往復する
 */

#include <btron/gname.h>
#include <btron/manager.h>

EXPORT void mgr_name(CONST B *ascii, TC *out)
{
	W i = 0;

	while (i < N_NAMELEN && ascii[i] != 0) {
		out[i] = (TC)(UB)ascii[i];
		i++;
	}
	while (i < N_NAMELEN) {
		out[i] = TNULL;
		i++;
	}
}

EXPORT WERR mgr_lookup(CONST B *name)
{
	TC key[N_NAMELEN];
	W data = 0;
	ERR er;

	mgr_name(name, key);
	er = get_nam(key, &data);
	if (er < 0) {
		return er; /* ER_NOEXS = そのマネージャは立っていない */
	}
	return data;
}

EXPORT ERR mgr_call(ID por, MgrMsg *m, TMOUT tmo)
{
	WERR n;

	if (m == 0) {
		return ER_ADR;
	}
	n = cal_por(por, MGR_PTN, (VP)m, (W)sizeof(MgrMsg), tmo);
	if (n < 0) {
		return n;
	}
	return ER_OK;
}

EXPORT ERR mgr_request(CONST B *name, MgrMsg *m, TMOUT tmo)
{
	WERR por = mgr_lookup(name);

	if (por < 1) {
		return por;
	}
	return mgr_call((ID)por, m, tmo);
}

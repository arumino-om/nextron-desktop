/*
 * manager.c — 外殻マネージャの共通土台
 *
 * 「グローバル名で見つけて、ランデブで話す」という外殻サーバの型を
 * 1 か所にまとめたもの。各マネージャはこの上に機能番号を定義するだけで
 * サーバになる。
 *
 * ------------------------------------------------------------------------
 * なぜランデブなのか（詳細 E1 / E13）
 * ------------------------------------------------------------------------
 *
 * 要求と返答が 1 往復で閉じ、呼出側が返答まで待つ — 同期 RPC がそのまま
 * プリミティブになっている。メッセージ (§1.2) だと「送って」「受けて」
 * 「返して」「受けて」の 4 回になり、対応付けも自分で持つ必要がある。
 *
 * さらに `fwd_por` による**回送**がある。「ウィンドウマネージャに来た要求を
 * 実身/仮身マネージャに投げ直す」という 3 者間の受け渡しを、呼出側に
 * 気づかせずに行える（詳細 E1 ④）。外殻の構造にこれが要る。
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

EXPORT WERR mgr_listen(CONST B *name)
{
	TC key[N_NAMELEN];
	WERR por;
	ERR er;

	/* 呼出メッセージと返答メッセージは同じ構造体なので同じ大きさ。
	 * DELEXIT を付けて、サーバが死んだらポートも消えるようにする。 */
	por = cre_por((W)sizeof(MgrMsg), (W)sizeof(MgrMsg), DELEXIT);
	if (por < 1) {
		return por;
	}
	mgr_name(name, key);
	/* N_FORCE: 前のサーバが落ちた後に名前が残っていても上書きして立つ。
	 * DELEXIT: このプロセスが終わったら名前も消える。 */
	er = cre_nam(key, (W)por, N_FORCE | DELEXIT);
	if (er < 0) {
		(void)del_por(por);
		return er;
	}
	return por;
}

EXPORT ERR mgr_serve(ID por, MgrFunc f, TMOUT tmo)
{
	MgrMsg m;
	W rdv = 0;
	WERR n;

	n = acp_por(por, MGR_PTN, &rdv, (VP)&m, tmo);
	if (n < 0) {
		return n;
	}
	m.result = ER_OK;
	f(&m);
	/* 返答の大きさは常に構造体 1 個。可変長にしたくなったら
	 * ここで実際の長さを渡す（maxrmsz の範囲内）。 */
	return rpl_rdv(rdv, (VP)&m, (W)sizeof(MgrMsg));
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

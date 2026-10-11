/*
 * gname.c — 周辺核 §1.4「グローバル名管理」
 *
 * cre_nam / del_nam / get_nam。名前 → 32 ビットデータの表。
 *
 * ------------------------------------------------------------------------
 * なぜこれがマネージャの土台なのか
 * ------------------------------------------------------------------------
 *
 * 仕様が用途として最初に挙げているのが「サーバ機能を持ったプロセスの
 * プロセス ID」である。外殻のマネージャはサーバプロセスなので、
 * **自分を見つけてもらうための名前をここに登録する**。
 *
 *   マネージャ側  cre_por() でポートを作り、その ID を固定の名前で cre_nam
 *   利用側        get_nam で引いて cal_por でランデブを張る
 *
 * BTRON3 に「サービス探索」の仕組みが他に無いのは、この 2 つで足りると
 * 考えられていたからである。
 *
 * ------------------------------------------------------------------------
 * 名前の扱い
 * ------------------------------------------------------------------------
 *
 * 仕様「TNULL(0) までの最大 8 文字分 (16 バイト) の任意のデータが名前と
 * される」。文字コードとして意味がある必要はないので、**文字列としてでは
 * なく固定長 8 要素の TC 配列として比較する**。短い名前は TNULL で埋める。
 */

#include <btron/gname.h>
#include <btron/proc.h>

#include "../core/knl.h"

typedef struct GName {
	TC name[N_NAMELEN];
	W data;
	W owner; /* DELEXIT の対象プロセス。0 なら自動削除しない */
	Bool used;
} GName;

LOCAL GName tbl[N_MAX];

/* 名前を固定長 8 要素へ正規化する。空なら False（仕様では ER_PAR）。 */
LOCAL Bool norm(CONST TC *name, TC *out)
{
	W i = 0;

	if (name == 0) {
		return False;
	}
	while (i < N_NAMELEN && name[i] != TNULL) {
		out[i] = name[i];
		i++;
	}
	if (i == 0) {
		return False; /* 空の名前 */
	}
	while (i < N_NAMELEN) {
		out[i] = TNULL;
		i++;
	}
	return True;
}

LOCAL GName *find(CONST TC *key)
{
	W i;
	W j;

	for (i = 0; i < N_MAX; i++) {
		if (!tbl[i].used) {
			continue;
		}
		for (j = 0; j < N_NAMELEN; j++) {
			if (tbl[i].name[j] != key[j]) {
				break;
			}
		}
		if (j == N_NAMELEN) {
			return &tbl[i];
		}
	}
	return 0;
}

EXPORT WERR cre_nam(CONST TC *name, W data, W opt)
{
	TC key[N_NAMELEN];
	W mode = opt & 0x3;
	GName *g;
	UD state;
	W i;
	W j;

	if (name == 0) {
		return ER_ADR;
	}
	/* opt = (N_CREATE ‖ N_MODIFY ‖ N_FORCE) | [DELEXIT]。
	 * 下位 2 ビットの 3 は宣言されていないので不正。 */
	if (mode == 3 || (opt & ~(W)(0x3 | DELEXIT)) != 0) {
		return ER_PAR;
	}
	if (!norm(name, key)) {
		return ER_PAR; /* 仕様「name が空」 */
	}

	state = hal_int_disable();
	g = find(key);
	if (g != 0) {
		if (mode == N_CREATE) {
			hal_int_restore(state);
			return ER_EXS; /* 既に存在している */
		}
	} else {
		if (mode == N_MODIFY) {
			hal_int_restore(state);
			return ER_NOEXS; /* 存在していない */
		}
		for (i = 0; i < N_MAX; i++) {
			if (!tbl[i].used) {
				g = &tbl[i];
				break;
			}
		}
		if (g == 0) {
			hal_int_restore(state);
			return ER_NOSPC;
		}
		g->used = True;
		for (j = 0; j < N_NAMELEN; j++) {
			g->name[j] = key[j];
		}
	}
	g->data = data;
	/* 仕様「生成したプロセス、または最後にデータを変更したプロセスの
	 * 終了時に…DELEXIT 指定により本システムコールを最後に実行した
	 * プロセスが対象となる」。
	 *
	 * **DELEXIT を付けない変更では所有者が外れる**と読む。仕様は
	 * 「前者（DELEXIT なし）の場合はグローバル名データが不要となった
	 * 時点で明示的に削除する必要がある」と書いていて、DELEXIT 無しの
	 * 変更の後も自動削除が残るのは辻褄が合わない。 */
	g->owner = ((opt & DELEXIT) != 0) ? prc_self() : 0;
	hal_int_restore(state);
	return ER_OK;
}

EXPORT ERR del_nam(CONST TC *name)
{
	TC key[N_NAMELEN];
	GName *g;
	UD state;

	if (name == 0) {
		return ER_ADR;
	}
	if (!norm(name, key)) {
		return ER_PAR;
	}
	state = hal_int_disable();
	g = find(key);
	if (g == 0) {
		hal_int_restore(state);
		return ER_NOEXS;
	}
	g->used = False;
	g->owner = 0;
	hal_int_restore(state);
	return ER_OK;
}

EXPORT ERR get_nam(CONST TC *name, W *data)
{
	TC key[N_NAMELEN];
	GName *g;
	UD state;

	if (name == 0 || data == 0) {
		return ER_ADR;
	}
	if (!norm(name, key)) {
		return ER_PAR;
	}
	state = hal_int_disable();
	g = find(key);
	if (g == 0) {
		hal_int_restore(state);
		return ER_NOEXS;
	}
	*data = g->data;
	hal_int_restore(state);
	return ER_OK;
}

EXPORT void gnm_release_process(W pid)
{
	UD state;
	W i;

	if (pid < 1) {
		return;
	}
	state = hal_int_disable();
	for (i = 0; i < N_MAX; i++) {
		if (tbl[i].used && tbl[i].owner == pid) {
			tbl[i].used = False;
			tbl[i].owner = 0;
		}
	}
	hal_int_restore(state);
}

EXPORT W gnm_count(void)
{
	W n = 0;
	W i;

	for (i = 0; i < N_MAX; i++) {
		if (tbl[i].used) {
			n++;
		}
	}
	return n;
}

EXPORT ERR gnm_init(void)
{
	W i;

	for (i = 0; i < N_MAX; i++) {
		tbl[i].used = False;
		tbl[i].owner = 0;
		tbl[i].data = 0;
	}
	return ER_OK;
}

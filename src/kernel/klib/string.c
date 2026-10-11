/*
 * string.c — カーネル内部のメモリ操作（GPL 側）
 *
 * freestanding でも、コンパイラは構造体の代入や配列の初期化に対して
 * `memcpy` / `memset` の呼び出しを生成する。`-ffreestanding` が外すのは
 * 「標準ライブラリがあるという前提」だけで、これらの呼び出しは残る。
 * したがってカーネル自身が用意しなければならない。
 *
 * 【ライセンス境界】ここは src/kernel 配下 なので **GPLv2**。
 * アプリケーションがリンクする src/lib 配下（LGPL 予定, J2 / 規律 R14）とは
 * 別物である。名前が似ているので混同しないこと。アプリ向けの
 * `memcpy` 等は後で src/lib/std/ に別に用意する。
 *
 * 【規律 R5】大きさは `UD`（size_t 相当）で受ける。`W` にすると 4GB で溢れる。
 */

#include <btron/typedef.h>

EXPORT VP memcpy(VP dst, CONST VP src, UD n);
EXPORT VP memmove(VP dst, CONST VP src, UD n);
EXPORT VP memset(VP dst, W c, UD n);
EXPORT W memcmp(CONST VP a, CONST VP b, UD n);

EXPORT VP memcpy(VP dst, CONST VP src, UD n)
{
	UB *d = (UB *)dst;
	CONST UB *s = (CONST UB *)src;
	UD i;

	for (i = 0; i < n; i++) {
		d[i] = s[i];
	}
	return dst;
}

EXPORT VP memmove(VP dst, CONST VP src, UD n)
{
	UB *d = (UB *)dst;
	CONST UB *s = (CONST UB *)src;
	UD i;

	if (d == s || n == 0) {
		return dst;
	}
	if (d < s) {
		for (i = 0; i < n; i++) {
			d[i] = s[i];
		}
	} else {
		for (i = n; i > 0; i--) {
			d[i - 1] = s[i - 1];
		}
	}
	return dst;
}

EXPORT VP memset(VP dst, W c, UD n)
{
	UB *d = (UB *)dst;
	UD i;

	for (i = 0; i < n; i++) {
		d[i] = (UB)c;
	}
	return dst;
}

EXPORT W memcmp(CONST VP a, CONST VP b, UD n)
{
	CONST UB *p = (CONST UB *)a;
	CONST UB *q = (CONST UB *)b;
	UD i;

	for (i = 0; i < n; i++) {
		if (p[i] != q[i]) {
			return (W)p[i] - (W)q[i];
		}
	}
	return 0;
}

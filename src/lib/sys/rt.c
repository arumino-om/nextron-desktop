/*
 * rt.c — ユーザプログラムの最小実行時支援
 *
 * コンパイラが構造体の複写などで勝手に呼ぶ関数と、ライブラリ側が
 * 必要とする周辺核の小さな口を埋める。
 *
 * **カーネルには結合しない。** カーネル側には同じ役目の
 * `src/kernel/klib/string.c` と `prc_self()` がある。
 */

#include <btron/proc.h>

/* clang は構造体の代入やコピーでこれらを呼ぶことがある。
 * `-ffreestanding` でも「コンパイラが生成してよい」4 つの関数は
 * 自前で用意しなければならない。 */
EXPORT VP memcpy(VP dst, CONST VP src, UW n)
{
	UB *d = (UB *)dst;
	CONST UB *s = (CONST UB *)src;
	UW i;

	for (i = 0; i < n; i++) {
		d[i] = s[i];
	}
	return dst;
}

EXPORT VP memset(VP dst, W c, UW n)
{
	UB *d = (UB *)dst;
	UW i;

	for (i = 0; i < n; i++) {
		d[i] = (UB)c;
	}
	return dst;
}

EXPORT VP memmove(VP dst, CONST VP src, UW n)
{
	UB *d = (UB *)dst;
	CONST UB *s = (CONST UB *)src;
	UW i;

	if (d == s || n == 0) {
		return dst;
	}
	/* 重なっているときは向きを選ぶ。 */
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

EXPORT W memcmp(CONST VP a, CONST VP b, UW n)
{
	CONST UB *x = (CONST UB *)a;
	CONST UB *y = (CONST UB *)b;
	UW i;

	for (i = 0; i < n; i++) {
		if (x[i] != y[i]) {
			return (W)x[i] - (W)y[i];
		}
	}
	return 0;
}

/* 自プロセス ID。周辺核の内部関数だが、ディスプレイプリミティブが
 * 描画環境の持ち主を覚えるのに使う。ユーザ側は `prc_sts(P_SELF)` で
 * 同じ値が取れる。 */
EXPORT W prc_self(void)
{
	WERR r = prc_sts(P_SELF, 0, 0);

	return (r > 0) ? r : 0;
}

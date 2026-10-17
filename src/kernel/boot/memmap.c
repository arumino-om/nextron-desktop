/*
 * memmap.c — メモリマップの畳み込み（ローダとカーネルで共有する）
 *
 * 詳細 F9「実機で初めて効く差」:
 *
 *   メモリマップ  QEMU は要素が少ない / 実機は 100 を超えることがある
 *
 * `BOOTINFO_MAX_REGIONS` は 192 なので、UEFI の記述子をそのまま並べると
 * 実機では溢れる。溢れた分を切り捨てるのも、最後の 1 本を伸ばして
 * 全部「使用不可」にするのも、**RAM の大半を失う**。そうではなく
 * **隣り合う領域を畳んで数を減らす**。
 *
 * 【この 1 本をローダとカーネルの両方でコンパイルしている理由】
 *
 * 畳みが効いているかは、実機を手に入れるまで確かめられない。それでは
 * 遅すぎる。純粋な関数にして**カーネルの自己検査から合成したマップを
 * 食わせられる**ようにしてある（規律 R27 の「その検査が実際に走る構成で
 * 回す」を、構成を作れない側から満たす手）。
 * ローダは PE、カーネルは ELF と別のバイナリになるが、畳みの規則が
 * 2 つあってはならないので、同じソースを両方に入れる。
 *
 * 【安全側の定義】畳んだ結果の種別は必ず**厳しい方**を採る。
 * 「使えない領域が使えるものとして現れる」ことだけは起こしてはならない。
 * 逆（使える領域が使えなくなる）は損をするだけで壊れない。
 */

#include <btron/bootinfo.h>

/* 種別の厳しさ。大きいほど厳しい。
 *
 * BMEM_UNUSABLE の値が 0 で BMEM_FREE が 1 なので、**番号の大小は
 * 厳しさの順になっていない**。順序はここで明示する。 */
LOCAL UW strictness(UW type)
{
	switch (type) {
	case BMEM_FREE:
		return 0;
	case BMEM_LOADER:
		return 1;
	case BMEM_FIRMWARE:
		return 2;
	default:
		return 3; /* BMEM_UNUSABLE と未知の種別 */
	}
}

LOCAL UW stricter(UW a, UW b)
{
	return (strictness(a) >= strictness(b)) ? a : b;
}

EXPORT UD bootmem_fold(BootMemRegion *r, UD n)
{
	UD out = 0;
	UD i;

	for (i = 0; i < n; i++) {
		if (r[i].size == 0) {
			continue;
		}
		if (out > 0 && r[out - 1].type == r[i].type &&
		    r[out - 1].base + r[out - 1].size == r[i].base) {
			r[out - 1].size += r[i].size;
			continue;
		}
		if (out != i) {
			r[out] = r[i];
		}
		r[out].resv = 0;
		out++;
	}
	return out;
}

/* 2 つを 1 本にしたときに失う「自由に使える」バイト数。 */
LOCAL UD free_loss(CONST BootMemRegion *a, CONST BootMemRegion *b)
{
	UD lost = 0;

	if (stricter(a->type, b->type) == BMEM_FREE) {
		return 0; /* 両方 BMEM_FREE。畳んでも失わない */
	}
	if (a->type == BMEM_FREE) {
		lost += a->size;
	}
	if (b->type == BMEM_FREE) {
		lost += b->size;
	}
	return lost;
}

EXPORT UD bootmem_compact(BootMemRegion *r, UD n, UD max)
{
	if (max == 0) {
		return 0;
	}
	while (n > max) {
		UD best = n;      /* 畳む組の左側。n なら「見つからなかった」 */
		UD best_loss = 0;
		UD smallest = 0;
		UD i;

		for (i = 0; i < n; i++) {
			if (r[i].size < r[smallest].size) {
				smallest = i;
			}
			/* **穴を跨いで畳まない。** 一覧に出てこない物理アドレスは
			 * 「RAM ではない」のであって「空き」ではない。跨いで畳むと
			 * MMIO が空きメモリとして現れうる。 */
			if (i + 1 >= n || r[i].base + r[i].size != r[i + 1].base) {
				continue;
			}
			if (best == n || free_loss(&r[i], &r[i + 1]) < best_loss) {
				best_loss = free_loss(&r[i], &r[i + 1]);
				best = i;
			}
		}
		if (best == n) {
			/* 隣り合う組が 1 つも無い（穴だらけ）。最も小さい領域を
			 * 一覧から落とす。**落ちた領域は「RAM ではない」として
			 * 扱われる**ので、これも安全側に倒れる。 */
			for (i = smallest; i + 1 < n; i++) {
				r[i] = r[i + 1];
			}
			n--;
			continue;
		}
		r[best].size = r[best + 1].base + r[best + 1].size - r[best].base;
		r[best].type = stricter(r[best].type, r[best + 1].type);
		for (i = best + 1; i + 1 < n; i++) {
			r[i] = r[i + 1];
		}
		n--;
	}
	return n;
}

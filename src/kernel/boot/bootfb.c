/*
 * bootfb.c — 起動時フレームバッファの画素形式（ローダとカーネルで共有する）
 *
 * 詳細 F9「実機で初めて効く差」:
 *
 *   GOP の画素形式  QEMU は BGRA か RGBA / 実機には **PixelBitMask** を返す実装がある
 *
 * ビットマスク指定では R/G/B の位置も幅も機種任せになる。形式の番号で
 * 2 分岐していると、そういう機械では「画面が無い」ことになってしまう。
 * そこで **どの形式でもマスクに落として**から先へ渡し、色を組み立てる側は
 * マスクだけを見る（fb.c の screen_rgb / dp の screen_spec）。
 *
 * 【この 1 本をローダとカーネルの両方でコンパイルしている理由】
 * memmap.c と同じ。**PixelBitMask を返す GOP は QEMU では作れない**ので、
 * 純粋な関数にして合成した値をカーネルの自己検査から食わせる。
 * そうしないと、実機を手に入れるまで正しさを確かめられない。
 */

#include <btron/bootinfo.h>

/* 連続した 1 の並びか。1 色は必ず連続したビット列でなければ意味を成さない。 */
LOCAL Bool mask_contiguous(UW m)
{
	if (m == 0) {
		return False;
	}
	while ((m & 1U) == 0) {
		m >>= 1;
	}
	/* 下位の 0 を落とした残りが (2^n - 1) の形ならよい。 */
	return ((m & (m + 1U)) == 0) ? True : False;
}

EXPORT UW bootfb_mask_pos(UW mask)
{
	UW pos = 0;

	if (mask == 0) {
		return 0;
	}
	while ((mask & 1U) == 0) {
		mask >>= 1;
		pos++;
	}
	return pos;
}

EXPORT UW bootfb_mask_bits(UW mask)
{
	UW bits = 0;

	while (mask != 0 && (mask & 1U) == 0) {
		mask >>= 1;
	}
	while ((mask & 1U) != 0) {
		mask >>= 1;
		bits++;
	}
	return bits;
}

EXPORT UW bootfb_pack(UW r_mask, UW g_mask, UW b_mask, UW r, UW g, UW b)
{
	UW out = 0;
	UW i;
	CONST UW mask[3] = {r_mask, g_mask, b_mask};
	CONST UW value[3] = {r & 0xFFU, g & 0xFFU, b & 0xFFU};

	for (i = 0; i < 3; i++) {
		UW bits = bootfb_mask_bits(mask[i]);
		UW pos = bootfb_mask_pos(mask[i]);

		if (bits == 0) {
			continue;
		}
		/* 階調は 8 ビットで来る。**マスクの幅が 8 とは限らない** ——
		 * ビットマスク指定では 5 や 6 がありうる。上位側を合わせる。 */
		if (bits >= 8) {
			out |= (value[i] << (bits - 8)) << pos;
		} else {
			out |= (value[i] >> (8 - bits)) << pos;
		}
	}
	return out;
}

EXPORT Bool bootfb_set_masks(BootFrameBuffer *fb, UW r_mask, UW g_mask, UW b_mask)
{
	if (fb == 0) {
		return False;
	}
	if (!mask_contiguous(r_mask) || !mask_contiguous(g_mask) ||
	    !mask_contiguous(b_mask)) {
		return False;
	}
	/* 重なっていたら信用しない。同じビットが 2 色を表すことはない。 */
	if ((r_mask & g_mask) != 0 || (g_mask & b_mask) != 0 ||
	    (b_mask & r_mask) != 0) {
		return False;
	}
	fb->r_mask = r_mask;
	fb->g_mask = g_mask;
	fb->b_mask = b_mask;

	/* **形式の番号は、マスクから決める。** 受け取る側はマスクだけを
	 * 見ればよいが、人が読むログと「画面が無い」の判定に番号が要る。 */
	if (r_mask == 0x00FF0000U && g_mask == 0x0000FF00U && b_mask == 0x000000FFU) {
		fb->format = BFB_BGRA8;
	} else if (r_mask == 0x000000FFU && g_mask == 0x0000FF00U &&
	           b_mask == 0x00FF0000U) {
		fb->format = BFB_RGBA8;
	} else {
		fb->format = BFB_BITMASK;
	}
	return True;
}

/*
 * font.c — グリフの引き方
 *
 * データ本体は自動生成の font_data.c（tools/mkfont.py）。
 */

#include <btron/font.h>

LOCAL CONST UB *lookup(CONST UH *codes, CONST UB *bits, UW count, UW stride, UW code)
{
	UW lo = 0;
	UW hi = count;

	/* 昇順配列の二分探索。65536 エントリの表を持たない理由は font.h の注記。 */
	while (lo < hi) {
		UW mid = lo + (hi - lo) / 2;

		if (codes[mid] == (UH)code) {
			return &bits[mid * stride];
		}
		if (codes[mid] < (UH)code) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	return 0;
}

EXPORT CONST UB *font_glyph(CONST FontSet *f, UW code, UW *width)
{
	CONST UB *g;

	*width = 0;
	if (f == 0 || code > 0xFFFFU) {
		return 0;
	}
	g = lookup(f->wide_code, f->wide_bits, f->wide_count, 32U, code);
	if (g != 0) {
		*width = 16;
		return g;
	}
	g = lookup(f->narrow_code, f->narrow_bits, f->narrow_count, 16U, code);
	if (g != 0) {
		*width = 8;
	}
	return g;
}

EXPORT UW font_utf8_next(CONST B *s, UW *len)
{
	CONST UB *p = (CONST UB *)s;
	UW c = p[0];
	UW n;
	UW i;

	if (c == 0) {
		*len = 0;
		return 0;
	}
	if (c < 0x80U) {
		*len = 1;
		return c;
	}
	if ((c & 0xE0U) == 0xC0U) {
		c &= 0x1FU;
		n = 2;
	} else if ((c & 0xF0U) == 0xE0U) {
		c &= 0x0FU;
		n = 3;
	} else if ((c & 0xF8U) == 0xF0U) {
		c &= 0x07U;
		n = 4;
	} else {
		*len = 1; /* 不正なバイト。1 バイト捨てる */
		return 0xFFFDU;
	}
	for (i = 1; i < n; i++) {
		if ((p[i] & 0xC0U) != 0x80U) {
			*len = i;
			return 0xFFFDU;
		}
		c = (c << 6) | (p[i] & 0x3FU);
	}
	*len = n;
	return c;
}

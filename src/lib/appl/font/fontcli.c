/*
 * fontcli.c — フォントマネージャの利用側 (アプリケーションライブラリ)
 *
 * **ここは `src/lib` 側（LGPL 予定。J2 / 規律 R14）。** サーバ本体は
 * `src/subsystems/font/fontman.c`（GPL）で、こちらはアプリがリンクする
 * 部分である。規律 R11 のとおり、アプリから見える関数シグネチャは
 * サーバ呼び出しであることを隠す。
 *
 * 詳細 E1 の分類でフォントマネージャが「中間」に置かれているのが、
 * そのままこのファイルの存在理由である。
 *
 *   置き場の問い合わせ  サーバへランデブ（1 プロセス 1 回だけ）
 *   字形の引き当て      共有メモリを直接読む（IPC なし）
 *
 * 文字 1 個ごとに IPC を往復させないために、索引ごと共有メモリに置いて
 * 利用側が自分で引く。`fontman_glyph()` がその引き当てである。
 */

#include <btron/fontman.h>
#include <btron/manager.h>

/* 利用側が覚えておく置き場。プロセスごとに 1 度引けばよい。 */
LOCAL CONST FontShared *cached;

/* ------------------------------------------------------------------ */
/* 共有グリフデータの引き方（利用側・サーバ側の共通）                  */
/* ------------------------------------------------------------------ */

/* 昇順のコード表を二分探索する。font.c と同じ形。 */
LOCAL W bsearch_code(CONST UH *tbl, UW n, UW code)
{
	UW lo = 0;
	UW hi = n;

	while (lo < hi) {
		UW mid = lo + (hi - lo) / 2;

		if (tbl[mid] == (UH)code) {
			return (W)mid;
		}
		if (tbl[mid] < (UH)code) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	return -1;
}

EXPORT CONST UB *fontman_glyph(CONST FontShared *fs, UW code, UW *width)
{
	CONST UB *base = (CONST UB *)(CONST VP)fs;
	CONST UH *tbl;
	W idx;

	if (width != 0) {
		*width = 0;
	}
	if (fs == 0 || fs->magic != FONTSHARED_MAGIC || code > 0xFFFFU) {
		return 0;
	}
	tbl = (CONST UH *)(CONST VP)(base + fs->narrow_code);
	idx = bsearch_code(tbl, fs->narrow_count, code);
	if (idx >= 0) {
		if (width != 0) {
			*width = 8;
		}
		return base + fs->narrow_bits + (UD)idx * fs->height;
	}
	tbl = (CONST UH *)(CONST VP)(base + fs->wide_code);
	idx = bsearch_code(tbl, fs->wide_count, code);
	if (idx >= 0) {
		if (width != 0) {
			*width = 16;
		}
		return base + fs->wide_bits + (UD)idx * fs->height * 2;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* 利用側                                                              */
/* ------------------------------------------------------------------ */

EXPORT CONST FontShared *fontman_shared(void)
{
	MgrMsg m;

	if (cached != 0) {
		return cached; /* 2 回目以降は IPC を起こさない */
	}
	m.func = FONT_GET_INF;
	m.arg[0] = 0;
	m.arg[1] = 0;
	m.arg[2] = 0;
	m.arg[3] = 0;
	if (mgr_request(MGR_FONT, &m, T_FOREVER) < 0 || m.result < 0) {
		return 0;
	}
	cached = (CONST FontShared *)(CONST VP)(UD)m.arg[0];
	if (cached != 0 && cached->magic != FONTSHARED_MAGIC) {
		cached = 0;
	}
	return cached;
}

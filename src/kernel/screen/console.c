/*
 * console.c — 画面コンソール
 *
 * E24 の帰結として**カーネルが画面を持っている**ので、外殻（ウィンドウ
 * マネージャ）が無くても、落ちた後でも文字を出せる。これが BTRON3 の
 * IMS/BMS（シリアルのモニタ。詳細 D8）の画面版に当たる。
 *
 * 【等幅・16 ドット固定に割り切る】
 * 半角 8 ドット、全角 16 ドット。グリフは font.h の内蔵データ。
 * 比例幅・アンチエイリアス・任意領域クリッピングは Phase 4 の DP の仕事で、
 * ここでやるべきことではない。
 *
 * 【シリアルへの出力を複製する】`hal_console_tee()` に自分を登録するので、
 * 既存の `hal_console_puts()` の出力がそのまま画面にも出る。テストの
 * `[ PASS ]` も画面に出る。呼び出し側を 1 行も変えずに済む。
 *
 * 【規律 R7 / R8】UTF-8 → コードポイントの変換はここが境界。
 * 本来は TRON コードの `WTC` を内部表現にすべきだが、変換表 (T2-01) が
 * まだ無いのでいまは Unicode のまま扱う。font.h の注記も参照。
 */

#include <btron/font.h>
#include <btron/hal.h>
#include <btron/screen.h>

#define GLYPH_H 16
#define MARGIN 4 /* 画面端の余白（画素） */

LOCAL CONST FontSet *fnt;
LOCAL UW cols;
LOCAL UW rows;
LOCAL UW base_row; /* この行から下がスクロール領域。上は固定（表題帯） */
LOCAL UW cx; /* カーソル桁（半角単位） */
LOCAL UW cy; /* カーソル行 */
LOCAL UW fg;
LOCAL UW bg;
LOCAL Bool on = False;

/* 全角は 2 桁ぶん使う。*/
#define CELL_W 8

LOCAL inline W px(UW col)
{
	return (W)(MARGIN + col * CELL_W);
}

LOCAL inline W py(UW row)
{
	return (W)(MARGIN + row * GLYPH_H);
}

/* スクロール領域の row 行目の画素 y */
LOCAL inline W pyc(UW row)
{
	return py(base_row + row);
}

/* グリフを 1 つ描く。戻り値は進んだ桁数（1 か 2）。 */
LOCAL UW draw_glyph(W x, W y, UW code, UW f, UW b)
{
	UW width = 0;
	CONST UB *g = font_glyph(fnt, code, &width);
	UW r;
	UW c;

	if (g == 0) {
		/* 字が無い。空けるだけ。全角か半角かも分からないので半角扱い。 */
		screen_fill(x, y, CELL_W, GLYPH_H, b);
		return 1;
	}
	screen_fill(x, y, (W)width, GLYPH_H, b);
	for (r = 0; r < GLYPH_H; r++) {
		if (width == 8) {
			UB row = g[r];

			for (c = 0; c < 8; c++) {
				if ((row & (UB)(0x80U >> c)) != 0) {
					screen_pixel(x + (W)c, y + (W)r, f);
				}
			}
		} else {
			UW row = ((UW)g[r * 2] << 8) | g[r * 2 + 1];

			for (c = 0; c < 16; c++) {
				if ((row & (0x8000U >> c)) != 0) {
					screen_pixel(x + (W)c, y + (W)r, f);
				}
			}
		}
	}
	return (width == 16) ? 2U : 1U;
}

EXPORT void screen_text_at(W col, W row, CONST B *utf8, UW f, UW b)
{
	UW c = (UW)col;

	if (!on || col < 0 || row < 0) {
		return;
	}
	while (*utf8 != '\0' && c < cols) {
		UW len = 0;
		UW code = font_utf8_next(utf8, &len);

		if (len == 0) {
			break;
		}
		utf8 += len;
		if (code == '\n') {
			break;
		}
		c += draw_glyph(px(c), py((UW)row), code, f, b);
	}
}

/* スクロール領域を 1 行ぶん上へずらす。
 *
 * 固定領域（表題帯）は動かさない。画面はキャッシュの効かないデバイス
 * メモリなので、行単位でまとめて動かす（screen_scroll_up の注記）。 */
LOCAL void scroll(void)
{
	if (screen() == 0) {
		return;
	}
	screen_scroll_up(pyc(0), (W)(rows * GLYPH_H), GLYPH_H, bg);
}

LOCAL void newline(void)
{
	cx = 0;
	cy++;
	if (cy >= rows) {
		cy = rows - 1;
		scroll();
	}
}

EXPORT void screen_console_puts(CONST B *utf8)
{
	if (!on) {
		return;
	}
	while (*utf8 != '\0') {
		UW len = 0;
		UW code = font_utf8_next(utf8, &len);

		if (len == 0) {
			break;
		}
		utf8 += len;

		if (code == '\n') {
			newline();
			continue;
		}
		if (code == '\r') {
			cx = 0;
			continue;
		}
		if (code == '\t') {
			cx = (cx + 8U) & ~7U;
			if (cx >= cols) {
				newline();
			}
			continue;
		}
		/* 全角が行末に掛かるなら折り返してから描く */
		if (cx + 2U > cols) {
			newline();
		}
		cx += draw_glyph(px(cx), pyc(cy), code, fg, bg);
		if (cx >= cols) {
			newline();
		}
	}
}

EXPORT void screen_console_color(UW f, UW b)
{
	fg = f;
	bg = b;
}

/* 上から n 行を固定領域として確保する。
 *
 * 表題帯や状態表示を置いて、その下だけをスクロールさせるために使う。
 * 緊急画面も「上に例外の種類、下に詳細」という形にできる。 */
EXPORT void screen_console_reserve(UW n)
{
	UW total = base_row + rows;

	if (n >= total) {
		return;
	}
	base_row = n;
	rows = total - n;
	cx = 0;
	cy = 0;
}

EXPORT UW screen_console_cols(void)
{
	return cols;
}

EXPORT UW screen_console_rows(void)
{
	return rows;
}

EXPORT ERR screen_console_init(void)
{
	CONST Screen *s = screen();

	if (s == 0) {
		return E_NOEXS;
	}
	fnt = font_default();
	if (fnt == 0 || fnt->height != GLYPH_H) {
		return E_SYS;
	}
	cols = (s->width - 2U * MARGIN) / CELL_W;
	rows = (s->height - 2U * MARGIN) / GLYPH_H;
	if (cols < 8 || rows < 2) {
		return E_SYS;
	}
	cx = 0;
	cy = 0;
	base_row = 0;
	fg = screen_rgb(0xD0, 0xD8, 0xE0);
	bg = screen_rgb(0x10, 0x14, 0x20);
	on = True;
	screen_clear(bg);

	/* 以後 hal_console_puts() の出力が画面にも出る。
	 * 呼び出し側を 1 行も変えずに済む。 */
	hal_console_tee(screen_console_puts);
	return E_OK;
}

/* ------------------------------------------------------------------ */
/* 結果を画面に残して止まる (R31 / 詳細 F9)                            */
/* ------------------------------------------------------------------ */

EXPORT void screen_hold_result(void)
{
	/* シリアルが生きているなら、結果はそちらで読める。
	 * **自動検査はシリアルで結果を取るので、ここで居座ってはならない。** */
	if (hal_console_opened()) {
		return;
	}
	if (!on) {
		return; /* 画面も無い。残す先が無いので呼び出し側に任せる */
	}

	/* 最後の 1 行だけ添える。結果（`[ DONE ] n/n`）はもう上に出ている。 */
	screen_console_puts("\n  シリアルが無いので、結果を画面に残して止まります"
	                    "（電源を切ってください）\n");

	for (;;) {
		hal_cpu_halt();
	}
}

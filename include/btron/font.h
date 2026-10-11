/*
 * font.h — 内蔵グリフデータ（起動時コンソールと緊急画面のため）
 *
 * docs/design-issues.md B8 / 詳細 B8: GNU Unifont をビルド時に取得する。
 * データ本体は tools/mkfont.py が生成する font_data.c にある。
 *
 * 【これは B7 の標準フォントデータ形式ではない】
 * 仕様の `IDXWDATA` 形式（インデックス・文字幅データ・TrueType 対応・
 * グレースケール描画）は Phase 4 のフォントマネージャで別に用意する。
 * ここにあるのは**カーネル常駐の最小限**で、
 *
 *   - 画面コンソール（外殻が立ち上がる前・落ちた後の出力先）
 *   - 緊急画面（E24 / 規律 R18。WM の協力なしに動かなければならない）
 *
 * のためだけのもの。したがってビットマップ固定・16 ドット固定・
 * 等幅（半角 8 / 全角 16）に割り切っている。
 *
 * 【引き方】コードポイントの昇順配列を二分探索する。
 * 65536 エントリの表を持つと 256KB 無駄になるので、実在するものだけ並べる。
 */

#ifndef __BTRON_FONT_H__
#define __BTRON_FONT_H__

#include <btron/typedef.h>

typedef struct FontSet {
	CONST UH *narrow_code; /* 昇順。半角 */
	CONST UB *narrow_bits; /* [narrow_count][16]。1 行 1 バイト */
	UW narrow_count;

	CONST UH *wide_code; /* 昇順。全角 */
	CONST UB *wide_bits; /* [wide_count][32]。1 行 2 バイト（上位が左） */
	UW wide_count;

	UW height; /* 16 */
} FontSet;

IMPORT CONST FontSet *font_default(void);

/* コードポイント code のグリフを引く。
 *
 * 戻り値はビットマップの先頭。*width に 8 か 16 が入る。
 * 見つからなければ 0 を返す（*width は 0）。 */
IMPORT CONST UB *font_glyph(CONST FontSet *f, UW code, UW *width);

/* UTF-8 を 1 文字取り出す。*len に消費したバイト数が入る。
 *
 * 【規律 R7 / R8】ここは境界である。内部表現は TRON コードの `WTC`
 * （32bit ステートレス）にするのが本筋だが、変換表 (T2-01) がまだ無いので
 * いまは Unicode のコードポイントをそのまま扱う。第16-17面が Unicode を
 * 内包しているので、表ができれば `WTC` 経由に差し替わる。 */
IMPORT UW font_utf8_next(CONST B *s, UW *len);

#endif /* __BTRON_FONT_H__ */

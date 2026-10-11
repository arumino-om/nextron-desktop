#!/usr/bin/env python3
"""GNU Unifont の .hex からカーネル内蔵グリフデータ (C ソース) を作る。

docs/design-issues.md B8 / 詳細 B8:

    GNU Unifont をビルド時に取得する。リポジトリには入れない。
    GPL-2.0-or-later WITH Font-exception-2.0。フォント例外により
    出力文書にライセンスが伝染しない。

入力は `UNIFONT_SRC`（flake.nix が公開する上流ソース tarball）。
パッケージの出力には .otf と .pcf.gz しか無く、**.hex はソース側にある**。
.hex は "コード:ビットマップ" のテキストなので扱いが楽で、しかも
**日本語字形と中国語字形が別ファイルになっている**ので選べる。

    font/plane00/unifont-base.hex      ラテン・ギリシャ・キリル・記号・かな
    font/plane00/wqy.hex               CJK（WenQuanYi。中国語字形）
    font/plane00/izmg16-plane00.hex    CJK（Izumi。**日本語字形**）

BTRON3 向けなので **izmg16 を wqy より優先する**。同じ漢字でも
日本語と中国語で字形が違い（例: 直・骨・次）、日本語の OS で中国語字形が
出ると明らかに違和感がある。

    python3 tools/mkfont.py --out build/arm64/font_data.c

【これは B7 の IDXWDATA 形式ではない】起動時コンソールと緊急画面のための
暫定の内部形式。仕様の標準フォントデータ形式（B7。インデックス・文字幅
データ・TrueType 対応）は Phase 4 のフォントマネージャで別に用意する。
"""

from __future__ import annotations

import argparse
import os
import sys
import tarfile
from pathlib import Path

# 合成の順序。**後が勝つ。** izmg16 を wqy より後に置くのが要点。
HEX_FILES = [
    "font/plane00/spaces.hex",
    "font/plane00/unifont-base.hex",
    "font/plane00/wqy.hex",
    "font/plane00/izmg16-plane00.hex",
]

# 取り込む範囲。日本語の OS として実用になる最小限。
#
# BMP を全部入れると約 2MB になる。ハングル・アラビア・インド系などは
# 第16-17面（Unicode 面）の実装 (B3/B4) と一緒に入れればよいので、
# いまは日本語に必要な範囲に絞る。
RANGES = [
    (0x0020, 0x00FF, "ASCII + Latin-1"),
    (0x2010, 0x206F, "一般句読点（— や ‥ など）"),
    (0x2190, 0x21FF, "矢印"),
    (0x2500, 0x257F, "罫線素片（コンソールの枠に使う）"),
    (0x25A0, 0x25FF, "幾何学模様"),
    (0x3000, 0x30FF, "CJK 句読点・ひらがな・カタカナ"),
    (0x31F0, 0x31FF, "カタカナ拡張"),
    (0x4E00, 0x9FFF, "CJK 統合漢字"),
    (0xFF00, 0xFFEF, "半角・全角形"),
]


def load_hex(src: Path) -> dict[int, str]:
    """tarball から .hex を読み、コードポイント → ビットマップ文字列にする。"""
    glyphs: dict[int, str] = {}
    want = {}
    with tarfile.open(src, "r:*") as tf:
        # tarball の先頭ディレクトリ名は版によって変わるので実体から拾う
        root = ""
        for name in tf.getnames():
            if "/font/plane00/" in name:
                root = name.split("/font/")[0]
                break
        if not root:
            sys.exit(f"{src} に font/plane00/ がありません")
        for rel in HEX_FILES:
            want[f"{root}/{rel}"] = rel

        found = set()
        for member in tf.getmembers():
            if member.name not in want:
                continue
            found.add(want[member.name])
            f = tf.extractfile(member)
            if f is None:
                continue
            # 同じファイル内でも後の行が勝つ。ファイル間の優先順位は
            # HEX_FILES の順で処理したいので、いったん貯めて後で並べる。
            glyphs.setdefault(want[member.name], {})
            per = {}
            for line in f.read().decode("ascii", "replace").splitlines():
                line = line.strip()
                if not line or ":" not in line:
                    continue
                code_s, _, bits = line.partition(":")
                try:
                    code = int(code_s, 16)
                except ValueError:
                    continue
                if len(bits) not in (32, 64) or code > 0xFFFF:
                    continue
                per[code] = bits
            glyphs[want[member.name]] = per

    missing = [r for r in HEX_FILES if r not in found]
    if missing:
        sys.exit(f"tarball に無い: {missing}")

    merged: dict[int, str] = {}
    for rel in HEX_FILES:  # 後のファイルが勝つ
        merged.update(glyphs[rel])
    return merged


def in_ranges(code: int) -> bool:
    return any(lo <= code <= hi for lo, hi, _ in RANGES)


def emit(glyphs: dict[int, str], out: Path, version: str) -> tuple[int, int, int]:
    narrow: list[tuple[int, bytes]] = []
    wide: list[tuple[int, bytes]] = []

    for code in sorted(glyphs):
        if not in_ranges(code):
            continue
        bits = glyphs[code]
        raw = bytes.fromhex(bits)
        if len(raw) == 16:
            narrow.append((code, raw))
        elif len(raw) == 32:
            wide.append((code, raw))

    def bytes_block(items: list[tuple[int, bytes]]) -> str:
        chunks = []
        buf: list[str] = []
        for _, raw in items:
            buf.extend(f"0x{b:02x}," for b in raw)
        for i in range(0, len(buf), 16):
            chunks.append("\t" + "".join(buf[i : i + 16]))
        return "\n".join(chunks)

    def code_block(items: list[tuple[int, bytes]]) -> str:
        chunks = []
        vals = [f"0x{c:04x}," for c, _ in items]
        for i in range(0, len(vals), 12):
            chunks.append("\t" + "".join(vals[i : i + 12]))
        return "\n".join(chunks)

    ranges_doc = "\n".join(f" *   U+{lo:04X}-U+{hi:04X}  {name}" for lo, hi, name in RANGES)

    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(
        f"""/*
 * font_data.c — 内蔵グリフデータ（**自動生成。編集しない**）
 *
 * tools/mkfont.py が GNU Unifont {version} の .hex から作る。
 * docs/design-issues.md B8 / 詳細 B8。
 *
 * ライセンス: GPL-2.0-or-later WITH Font-exception-2.0。
 * **フォント例外により、このフォントで描いた出力物に GPL は伝染しない。**
 *
 * 字形の選択: CJK は izmg16（**日本語字形**）を wqy（中国語字形）より
 * 優先して合成している。同じ漢字でも字形が違うため。
 *
 * 取り込んだ範囲:
{ranges_doc}
 *
 * 半角 {len(narrow)} 字 (8x16) / 全角 {len(wide)} 字 (16x16)
 */

#include <btron/font.h>

LOCAL CONST UH narrow_code[] = {{
{code_block(narrow)}
}};

LOCAL CONST UB narrow_bits[] = {{
{bytes_block(narrow)}
}};

LOCAL CONST UH wide_code[] = {{
{code_block(wide)}
}};

LOCAL CONST UB wide_bits[] = {{
{bytes_block(wide)}
}};

LOCAL CONST FontSet unifont = {{
\tnarrow_code, narrow_bits, (UW){len(narrow)},
\twide_code, wide_bits, (UW){len(wide)},
\t16U,
}};

EXPORT CONST FontSet *font_default(void)
{{
\treturn &unifont;
}}
""",
        encoding="utf-8",
    )
    return len(narrow), len(wide), out.stat().st_size


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--src", type=Path, default=None,
                    help="Unifont のソース tarball。既定は $UNIFONT_SRC")
    args = ap.parse_args()

    src = args.src or os.environ.get("UNIFONT_SRC")
    if not src:
        sys.exit(
            "UNIFONT_SRC が未設定です。nix develop の中で実行してください。\n"
            "（グリフデータはリポジトリに入れず、flake で固定して取得します。B8）"
        )
    src = Path(src)
    if not src.exists():
        sys.exit(f"ありません: {src}")

    version = os.environ.get("UNIFONT_VERSION", "unknown")
    glyphs = load_hex(src)
    n, w, size = emit(glyphs, args.out, version)
    print(f"{args.out}  半角 {n} 字 / 全角 {w} 字  ({size // 1024} KB の C ソース)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

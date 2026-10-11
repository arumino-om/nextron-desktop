#!/usr/bin/env python3
"""仕様文書の図版 (GIF) を読める大きさの PNG に変換する。

図版にはビット配置など本文にない情報が含まれる
(例: レコードインデックスの 16 バイト構成、ファイルヘッダの byte offset)。
原寸の GIF は小さくて読み取れないので拡大する。

出力は .spec-work/fig/ 以下。コミットしない。

  python3 tools/spec/figures.py [--width 1600]
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys

import common


def convert_one(src, dst, width: int, tool: str) -> bool:
    dst.parent.mkdir(parents=True, exist_ok=True)
    if tool == "magick":
        # 図版は 2 値の線画。拡大するので最近傍 (point) で補間し、
        # 文字や罫線がボケないようにする。
        # `<` は「指定より小さいときだけ拡大」の意味 (`>` だと縮小のみで効かない)。
        cmd = ["magick", str(src), "-filter", "point", "-resize", f"{width}x<", str(dst)]
    else:  # macOS 標準の sips。nix 環境外でも動く退避路
        cmd = ["sips", "-s", "format", "png", "-Z", str(width), str(src), "--out", str(dst)]
    r = subprocess.run(cmd, capture_output=True)
    return r.returncode == 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--width", type=int, default=1600, help="拡大後の最大幅 (既定 1600)")
    args = ap.parse_args()

    tool = "magick" if shutil.which("magick") else ("sips" if shutil.which("sips") else None)
    if tool is None:
        sys.exit("エラー: magick も sips も見つかりません。nix develop の中で実行してください。")

    total = ok = 0
    for root, prefix in ((common.btron3(), "btron3"), (common.brightv(), "brightv")):
        n = 0
        for src in sorted(root.rglob("*.gif")):
            rel = src.relative_to(root)
            dst = common.FIG_DIR / prefix / rel.with_suffix(".png")
            total += 1
            if convert_one(src, dst, args.width, tool):
                ok += 1
                n += 1
        print(f"  {prefix:8s} {n:3d} 枚  <- {root}")

    print(f"計 {ok}/{total} 枚を {common.FIG_DIR} に出力しました ({tool}, 最大幅 {args.width}px)。")
    return 0 if ok == total else 1


if __name__ == "__main__":
    sys.exit(main())

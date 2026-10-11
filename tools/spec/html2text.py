#!/usr/bin/env python3
"""仕様文書の HTML (EUC-JP) を UTF-8 のプレーンテキストに変換する。

出力は .spec-work/text/ 以下。原文をほぼそのまま含むのでコミットしない。

  python3 tools/spec/html2text.py
"""

from __future__ import annotations

import html
import re
import sys

import common


def convert(raw: str) -> str:
    s = raw
    s = re.sub(r"(?is)<head>.*?</head>", "", s)
    s = re.sub(r"(?is)<!--.*?-->", "", s)
    # 画像は参照マーカとして残す（図版は別途 figures.sh で変換する）
    s = re.sub(r'(?is)<img[^>]*src="([^"]*)"[^>]*>', r"[FIG:\1]", s)
    s = re.sub(r"(?is)</?(pre|table)[^>]*>", "\n", s)
    s = re.sub(r"(?is)<br[^>]*>", "\n", s)
    s = re.sub(r"(?is)</(p|div|dt|dd|li|tr|h[1-6]|ul|ol|dl|center|blockquote)>", "\n", s)
    s = re.sub(r"(?is)<(p|div|tr|ul|ol|dl|center|blockquote)[^>]*>", "\n", s)
    s = re.sub(r"(?is)<h([1-6])[^>]*>", r"\n\n### ", s)
    s = re.sub(r"(?is)<(dt|li)[^>]*>", "\n- ", s)
    s = re.sub(r"(?is)<dd[^>]*>", "\n  ", s)
    s = re.sub(r"(?is)<(td|th)[^>]*>", " | ", s)
    s = re.sub(r"(?is)</(td|th)>", "", s)
    s = re.sub(r"(?is)<hr[^>]*>", "\n----\n", s)
    s = re.sub(r"(?is)<[^>]+>", "", s)
    s = html.unescape(s)
    s = re.sub(r"[ \t]+\n", "\n", s)
    s = re.sub(r"\n{3,}", "\n\n", s)
    return s.strip() + "\n"


def run(src_root, prefix: str) -> int:
    n = 0
    for p in sorted(src_root.rglob("*.html")):
        rel = p.relative_to(src_root)
        out = common.TEXT_DIR / prefix / rel.with_suffix(".txt")
        out.parent.mkdir(parents=True, exist_ok=True)
        raw = p.read_bytes().decode(common.ENCODING, errors="replace")
        out.write_text(convert(raw), encoding="utf-8")
        n += 1
    return n


def main() -> None:
    common.TEXT_DIR.mkdir(parents=True, exist_ok=True)
    total = 0
    for root, prefix in ((common.btron3(), "btron3"), (common.brightv(), "brightv")):
        n = run(root, prefix)
        print(f"  {prefix:8s} {n:3d} ファイル  <- {root}")
        total += n
    print(f"計 {total} ファイルを {common.TEXT_DIR} に出力しました。")


if __name__ == "__main__":
    sys.exit(main())

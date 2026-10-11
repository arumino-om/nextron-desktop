#!/usr/bin/env python3
"""μT-Kernel 3.0 仕様書 (TRON Forum) をローカルに取得する。

中心核の基準仕様 (docs/design-issues.md D10 / 詳細 D10)。
BTRON3 第2編1章「周辺核」の下に置く中心核の API 定義はこちらから取る。

  python3 tools/spec/fetch_mtk3.py [--lang jp|en|both]

取得先は .spec-work/mtk3_spec/ 以下。**リポジトリにはコミットしない。**
仕様書には次の注意書きがある:

    All copyrights and authorship belong to TRON Forum for the years 2019-2023.
    複製または部分的な複製には TRON Forum の明示的な許諾が必要。

したがって BTRON3 仕様書と同じ扱いとする。読んで独自実装するのは可。
文書そのものの再配布は不可 (docs/design-issues.md §1 / 規律 R15)。
"""

from __future__ import annotations

import argparse
import re
import sys
import time
import urllib.error
import urllib.request

import common

BASE = "https://tron-forum.github.io/mtk3_spec_{lang}/"
START = "index.html"
UA = "Mozilla/5.0 (compatible; btron-os spec fetcher)"

# 同一ディレクトリ内の .html だけを辿る（外部サイトへは出ない）
HREF = re.compile(rb'href\s*=\s*["\']([A-Za-z0-9_\-.]+\.html)(?:#[^"\']*)?["\']', re.I)


def fetch(url: str) -> bytes | None:
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return r.read()
    except (urllib.error.URLError, urllib.error.HTTPError, TimeoutError) as e:
        print(f"    ! {url}: {e}", file=sys.stderr)
        return None


def mirror(lang: str) -> int:
    base = BASE.format(lang=lang)
    out_dir = common.WORK / "mtk3_spec" / lang
    out_dir.mkdir(parents=True, exist_ok=True)

    todo = [START]
    seen: set[str] = set()
    got = 0
    while todo:
        name = todo.pop(0)
        if name in seen:
            continue
        seen.add(name)

        dst = out_dir / name
        if dst.exists():
            body = dst.read_bytes()
        else:
            body = fetch(base + name)
            if body is None:
                continue
            dst.write_bytes(body)
            got += 1
            time.sleep(0.2)  # 相手先に負荷をかけない

        for m in HREF.finditer(body):
            link = m.group(1).decode("ascii")
            if link not in seen:
                todo.append(link)

    print(f"  {lang}: {len(seen)} ページ (新規 {got}) -> {out_dir}")
    return len(seen)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--lang", choices=("jp", "en", "both"), default="both")
    args = ap.parse_args()

    langs = ["jp", "en"] if args.lang == "both" else [args.lang]
    for lang in langs:
        mirror(lang)

    print()
    print("  注意: 取得した仕様書はリポジトリにコミットしない。")
    print("        TRON Forum の明示的な許諾なく複製できない (規律 R15 / §1)。")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""μT-Kernel 3.0 仕様書から API 一覧を抽出する。

中心核の基準仕様 (docs/design-issues.md D10)。先に fetch_mtk3.py で取得しておく。

  python3 tools/spec/fetch_mtk3.py --lang jp
  python3 tools/spec/extract_mtk3.py

出力: docs/api/mtk3.md

抽出するのはインタフェースの事実 (関数名・シグネチャ・一行概要) だけで、
仕様書の解説文は含めない。仕様書そのものは TRON Forum の明示的な許諾なく
複製できないため、リポジトリには取り込まない (規律 R15 / §1)。
"""

from __future__ import annotations

import html
import re
import sys

import common

SPEC = common.WORK / "mtk3_spec" / "jp"

# DocBook 生成の HTML。関数原型は次の形で出てくる。
#   <CODE CLASS="FUNCDEF">ID semid = tk_cre_sem</CODE>(CONST T_CSEM *pk_csem);</CODE>
FUNCDEF = re.compile(
    r'<CODE\s*\nCLASS="FUNCDEF"\s*\n>(.*?)</CODE\s*\n>(.*?)</CODE\s*\n>',
    re.S | re.I,
)
# API 索引ページの項目。生 HTML の構造で拾う。
#   <A HREF="xxx.html#tk_cre_sem">tk_cre_sem</A> - セマフォ生成
# OS/SM は tk_*、DS は td_*。接頭辞を持たないユーティリティ関数
# (CheckInt / DI / EI / ControlCache 等) は索引に載らない。
# 平坦化したテキストに正規表現をかけると、貪欲マッチで次の項目を
# 飲み込んで取りこぼすため、タグの構造をそのまま使う。
INDEX_ENTRY = re.compile(
    r'HREF="[^"]*#((?:tk|td)_[a-z0-9_]+)"\s*\n>\1</A\s*\n>\s*[-–]\s*([^<]+)',
    re.I,
)

TAG = re.compile(r"(?s)<[^>]+>")

# 索引ページ -> 分類
INDEXES = {
    "index_of_utk_os_api.html": "μT-Kernel/OS",
    "index_of_utk_sm_api.html": "μT-Kernel/SM",
    "index_of_utk_ds_api.html": "μT-Kernel/DS",
}


def text(s: str) -> str:
    return re.sub(r"\s+", " ", html.unescape(TAG.sub("", s))).strip()


def load_descriptions() -> dict[str, tuple[str, str]]:
    """索引ページから 関数名 -> (分類, 一行概要) を作る。"""
    out: dict[str, tuple[str, str]] = {}
    for fname, group in INDEXES.items():
        p = SPEC / fname
        if not p.exists():
            continue
        raw = p.read_text(encoding="utf-8", errors="replace")
        for m in INDEX_ENTRY.finditer(raw):
            name = m.group(1)
            desc = re.sub(r"\s+", " ", html.unescape(m.group(2))).strip()
            if name not in out:
                out[name] = (group, desc)
    return out


def load_signatures() -> dict[str, str]:
    """各ページから 関数名 -> シグネチャ を作る。"""
    out: dict[str, str] = {}
    for p in sorted(SPEC.glob("*.html")):
        raw = p.read_text(encoding="utf-8", errors="replace")
        for m in FUNCDEF.finditer(raw):
            head = text(m.group(1))  # 例: "ID semid = tk_cre_sem"
            args = text(m.group(2))  # 例: "(CONST T_CSEM *pk_csem);"
            fm = re.search(r"([A-Za-z_][A-Za-z0-9_]*)\s*$", head)
            if not fm:
                continue
            name = fm.group(1)
            sig = f"{head}{args}".rstrip(";").strip()
            out.setdefault(name, sig)
    return out


def main() -> int:
    if not SPEC.exists():
        sys.exit(
            "μT-Kernel 3.0 仕様書が取得されていません。\n"
            "  python3 tools/spec/fetch_mtk3.py --lang jp"
        )

    descs = load_descriptions()
    sigs = load_signatures()
    names = sorted(set(descs) | set(sigs))

    groups: dict[str, list[str]] = {}
    for n in names:
        group = descs.get(n, ("ユーティリティ / システムライブラリ", ""))[0]
        groups.setdefault(group, []).append(n)

    out = [
        "# μT-Kernel 3.0 API 一覧（仕様書からの抽出）",
        "",
        "`tools/spec/extract_mtk3.py` が機械的に抽出したもの。**手で編集しない。**",
        "",
        "中心核の基準仕様（`docs/design-issues.md` D10 / 詳細 D10）。",
        "出典は TRON Forum の μT-Kernel 3.0 仕様書（IEEE 2050-2018）。",
        "",
        "> 仕様書そのものはリポジトリに含めない。TRON Forum の明示的な許諾なく",
        "> 複製できないため（規律 R15 / §1）。ここにあるのはインタフェースの事実",
        "> （関数名・シグネチャ・一行概要）だけで、解説文は含まない。",
        "",
        "> **T-License 2.2 の実装コード（`tron-forum/mtkernel_3`）は GPLv2・LGPL の",
        "> いずれとも非互換。仕様を読んで独自実装することだけを行う（規律 R15）。**",
        "",
        f"関数 {len(names)} 件（シグネチャ取得済み {len(sigs)} 件）",
        "",
    ]
    for group in sorted(groups):
        out += ["", f"## {group}", "", "| 関数 | シグネチャ | 概要 |", "|---|---|---|"]
        for n in groups[group]:
            sig = sigs.get(n, "")
            desc = descs.get(n, ("", ""))[1]
            esc = lambda s: s.replace("|", "\\|")
            out.append(f"| `{n}` | `{esc(sig)}` | {esc(desc)} |")
        out.append("")

    common.API_DIR.mkdir(parents=True, exist_ok=True)
    (common.API_DIR / "mtk3.md").write_text("\n".join(out) + "\n", encoding="utf-8")

    print(f"  mtk3.md : 関数 {len(names)} 件 / シグネチャ {len(sigs)} 件")
    for g in sorted(groups):
        print(f"            {g:16s} {len(groups[g]):3d}")
    print(f"  -> {common.API_DIR / 'mtk3.md'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

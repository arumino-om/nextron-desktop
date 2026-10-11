#!/usr/bin/env python3
"""仕様文書から API の一覧 (関数シグネチャ・定数) を抽出する。

抽出するのは「インタフェースの事実」であって解説文ではない。
出力は docs/api/ 以下でコミットする (docs/design-issues.md §1 の区別による)。

  python3 tools/spec/html2text.py   # 先にテキスト化しておく
  python3 tools/spec/extract_api.py

出力:
  docs/api/functions.md   関数シグネチャ一覧
  docs/api/constants.md   #define 定数一覧
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass, field

import common

# 【形式】の見出し。周辺核・外殻は "### "、DP は "- " で始まる。
FORM = re.compile(r"^\s*(?:###|-)\s*【形式】\s*$")
# 表のセル
CELL = re.compile(r"^\s*\|\s*(.*?)\s*$")
# 章見出し
HEAD = re.compile(r"^###\s+(\d[\d.]*\s+.+?)\s*$")
# C 識別子だけの行
IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
# #define NAME value   (関数形式マクロも拾う)
DEFINE = re.compile(r"^\s*#define\s+([A-Za-z_][A-Za-z0-9_]*(?:\([^)]*\))?)\s+(.*?)\s*(?:/\*(.*?)\*/)?\s*$")

# ファイルパス -> (層, 並び順)
LAYERS = [
    ("os_spec/kernel/", "周辺核", 1),
    ("os_spec/dp/", "ディスプレイプリミティブ", 2),
    ("os_spec/shell/", "外殻", 3),
    ("shared_data/", "共有データ", 0),
    ("itron/", "中心核 (I-right/V)", 4),
    ("library/", "ライブラリ", 5),
    ("device/", "デバイス", 6),
    ("ext/", "拡張機能", 7),
    ("diff/", "BTRON3 仕様への制限・補足", 8),
    ("dev_tool/", "開発ツール", 9),
]


def layer_of(rel: str):
    for pat, name, order in LAYERS:
        if pat in rel:
            return name, order
    return "その他", 99


@dataclass
class Func:
    name: str
    sig: str
    desc: str
    en: str = ""


@dataclass
class Section:
    rel: str
    layer: str
    order: int
    title: str
    funcs: list[Func] = field(default_factory=list)


def _signature(lines: list[str], i: int) -> str:
    """【形式】の直後からシグネチャを取り出す。括弧が閉じるまで連結する。"""
    buf: list[str] = []
    j = i + 1
    while j < len(lines) and j < i + 12:
        ln = lines[j].strip()
        j += 1
        if not ln:
            if buf:
                break
            continue
        if ln.startswith(("###", "- 【", "|")) or ln.startswith("【"):
            break
        buf.append(ln)
        joined = " ".join(buf)
        if joined.count("(") and joined.count("(") == joined.count(")"):
            break
    sig = re.sub(r"\s+", " ", " ".join(buf)).strip()
    return sig


def _context(lines: list[str], i: int) -> tuple[str, str, str]:
    """【形式】の手前にある表セルから 関数名 / 英語名 / 概要 を拾う。"""
    cells: list[str] = []
    for j in range(i - 1, max(-1, i - 16), -1):
        ln = lines[j]
        if not ln.strip():
            continue
        m = CELL.match(ln)
        if not m:
            break
        v = m.group(1)
        if v:
            cells.append(v)
    cells.reverse()
    name = en = desc = ""
    for c in cells:
        if IDENT.match(c):
            if not name:
                name = c
        elif c.isascii():
            if not en:
                en = c
        else:
            if not desc:
                desc = c
    return name, en, desc


def _parse_itron(lines: list[str], sec: Section) -> None:
    """中心核 (BV 第5編) はシグネチャを載せていないので、名前と概要だけ拾う。

    この編は μITRON3.0 標準ハンドブック (ISBN4-89362-154-8) との差分であり、
    完全な API 定義は仕様書に含まれない。表の
        | cre_tsk
        | タスク生成
    という 2 セルの並びがシステムコールの見出しになっている。
    """
    run: list[str] = []
    for ln in lines + [""]:
        m = CELL.match(ln)
        if m:
            v = m.group(1)
            if v:
                run.append(v)
            continue
        if not ln.strip():
            continue
        if len(run) == 2 and IDENT.match(run[0]) and not run[1].isascii():
            if not any(f.name == run[0] for f in sec.funcs):
                sec.funcs.append(Func(name=run[0], sig="", desc=run[1]))
        run = []


def parse(path, rel: str) -> Section | None:
    lines = path.read_text(encoding="utf-8").splitlines()
    layer, order = layer_of(rel)
    title = ""
    for ln in lines:
        m = HEAD.match(ln)
        if m:
            title = m.group(1)
            break
    sec = Section(rel=rel, layer=layer, order=order, title=title)

    if "itron/" in rel:
        _parse_itron(lines, sec)
        return sec if sec.funcs else None

    for i, ln in enumerate(lines):
        if not FORM.match(ln):
            continue
        sig = _signature(lines, i)
        if not sig:
            continue
        name, en, desc = _context(lines, i)
        if not name:
            # シグネチャから関数名を推定する
            m = re.search(r"([A-Za-z_][A-Za-z0-9_]*)\s*\(", sig)
            name = m.group(1) if m else ""
        if not name:
            # 関数名が取れないものは C の API ではない (コマンド構文など)
            continue
        sec.funcs.append(Func(name=name, sig=sig, desc=desc, en=en))
    return sec if sec.funcs else None


def collect_defines(path, rel: str):
    out = []
    for ln in path.read_text(encoding="utf-8").splitlines():
        m = DEFINE.match(ln)
        if m:
            name, val, cmt = m.group(1), m.group(2), (m.group(3) or "").strip()
            if val:
                out.append((name, val, cmt, rel))
    return out


def md_escape(s: str) -> str:
    return s.replace("|", "\\|")


def main() -> int:
    if not common.TEXT_DIR.exists():
        sys.exit("先に python3 tools/spec/html2text.py を実行してください。")

    sections: list[Section] = []
    defines: list[tuple[str, str, str, str]] = []
    for path in sorted(common.TEXT_DIR.rglob("*.txt")):
        rel = str(path.relative_to(common.TEXT_DIR))
        if rel.endswith(("index.txt", "indexfig.txt")):
            continue
        # 開発ツール (BMS / IMS / CLI) は C の API ではなくコマンドリファレンス。
        # 【形式】をコマンド構文に使っているため関数として拾ってしまう。
        if "dev_tool/" in rel:
            continue
        sec = parse(path, rel)
        if sec:
            sections.append(sec)
        defines.extend(collect_defines(path, rel))

    sections.sort(key=lambda s: (s.order, s.rel))
    common.API_DIR.mkdir(parents=True, exist_ok=True)

    # --- functions.md ---
    nfunc = sum(len(s.funcs) for s in sections)
    out = [
        "# API 一覧（仕様書からの抽出）",
        "",
        "`tools/spec/extract_api.py` が仕様文書から機械的に抽出したもの。**手で編集しない。**",
        "",
        "抽出元の仕様文書そのものはリポジトリに含めない（`docs/design-issues.md` §1）。",
        "ここにあるのはインタフェースの事実（関数名・シグネチャ・概要）だけで、解説文は含まない。",
        "",
        f"関数 {nfunc} 件 / セクション {len(sections)} 件",
        "",
    ]
    cur_layer = None
    for s in sections:
        if s.layer != cur_layer:
            out += ["", f"## {s.layer}", ""]
            cur_layer = s.layer
        out += [f"### {s.title or s.rel}", "", f"`{s.rel}`", ""]
        out += ["| 関数 | シグネチャ | 概要 |", "|---|---|---|"]
        for f in s.funcs:
            d = f.desc or f.en
            out.append(f"| `{f.name}` | `{md_escape(f.sig)}` | {md_escape(d)} |")
        out.append("")
    (common.API_DIR / "functions.md").write_text("\n".join(out) + "\n", encoding="utf-8")

    # --- constants.md ---
    seen: dict[str, tuple[str, str, str]] = {}
    for name, val, cmt, rel in defines:
        if name not in seen:
            seen[name] = (val, cmt, rel)
    out = [
        "# 定数一覧（仕様書からの抽出）",
        "",
        "`tools/spec/extract_api.py` が仕様文書から機械的に抽出したもの。**手で編集しない。**",
        "",
        f"{len(seen)} 件（同名は最初の出現を採用）",
        "",
        "| 定数 | 値 | 註 | 出典 |",
        "|---|---|---|---|",
    ]
    for name in sorted(seen):
        val, cmt, rel = seen[name]
        out.append(f"| `{name}` | `{md_escape(val)}` | {md_escape(cmt)} | `{rel}` |")
    (common.API_DIR / "constants.md").write_text("\n".join(out) + "\n", encoding="utf-8")

    print(f"  functions.md : 関数 {nfunc} 件 / セクション {len(sections)} 件")
    print(f"  constants.md : 定数 {len(seen)} 件")
    print(f"  -> {common.API_DIR}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

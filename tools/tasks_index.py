#!/usr/bin/env python3
"""docs/tasks/ の索引 (INDEX.md) を作る。

**索引は生成物である**（W0-4 / 規律 R29）。タスク 1 件が 1 ファイルなので、
並列に作業しても衝突しない。索引だけは全レーンが触るが、生成物なので
衝突したら作り直せばよい（解決が自明になる）。

    python3 tools/tasks_index.py          # docs/tasks/INDEX.md を更新
    python3 tools/tasks_index.py --check  # 最新かどうかだけ見る（CI 用）

各タスクのファイルの先頭はこの形にする。ここだけ機械が読む。

    # T3-01 BTRON ファイルシステムのオンディスク表現コーデック

    - **状態**: 未着手
    - **委譲**: ◎
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TASKS = ROOT / "docs" / "tasks"
INDEX = TASKS / "INDEX.md"

# 状態の並び順。完了を下にして、やることが上に来るようにする。
ORDER = ["着手中", "未着手", "一部完了", "完了"]

HEAD = """# タスク索引

**この表は生成物である。** 手で直さず `python3 tools/tasks_index.py` で
作り直す（W0-4 / 規律 R29）。状態と委譲可否は各タスクのファイルの先頭から
読んでいる。

作業単位の定義と受入条件は各ファイルに、決定と根拠は
[`../design-issues.md`](../design-issues.md)、並列作業の順番と分担は
[`../parallel.md`](../parallel.md)、番号の予約は
[`../registry.md`](../registry.md) にある。

| タスク | 委譲 | 状態 |
|---|---|---|
"""


def parse(path: Path) -> tuple[str, str, str, str] | None:
    text = path.read_text(encoding="utf-8")
    m = re.search(r"^#\s+(\S+)\s+(.+)$", text, re.M)
    if not m:
        return None
    tid, title = m.group(1), m.group(2).strip()
    st = re.search(r"^-\s+\*\*状態\*\*:\s*(.+)$", text, re.M)
    dl = re.search(r"^-\s+\*\*委譲\*\*:\s*(.+)$", text, re.M)
    return tid, title, (st.group(1).strip() if st else "?"), (dl.group(1).strip() if dl else "?")


def sort_key(row: tuple[str, str, str, str]) -> tuple[int, str]:
    tid, _, state, _ = row
    head = state.split("（")[0].split(" ")[0]
    rank = ORDER.index(head) if head in ORDER else len(ORDER)
    return (rank, tid)


def build() -> str:
    rows = []
    for path in sorted(TASKS.glob("*.md")):
        if path.name == "INDEX.md":
            continue
        row = parse(path)
        if row is None:
            sys.exit(f"{path.name}: 先頭の `# <ID> <題>` が読めない")
        rows.append(row)
    out = [HEAD]
    for tid, title, state, deleg in sorted(rows, key=sort_key):
        out.append(f"| [{tid} {title}]({tid}.md) | {deleg} | {state} |\n")
    out.append(f"\n合計 {len(rows)} 件。\n")
    return "".join(out)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="最新かどうかだけ見る")
    args = ap.parse_args()

    text = build()
    if args.check:
        cur = INDEX.read_text(encoding="utf-8") if INDEX.exists() else ""
        if cur != text:
            print("docs/tasks/INDEX.md が古い。python3 tools/tasks_index.py で作り直す")
            return 1
        print("docs/tasks/INDEX.md は最新")
        return 0
    INDEX.write_text(text, encoding="utf-8")
    print(f"{INDEX.relative_to(ROOT)} を書いた")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

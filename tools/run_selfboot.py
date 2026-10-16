#!/usr/bin/env python3
"""自立起動を 2 回続けて試す (D8 / startup.c)。

**永続性は 1 回の起動では確かめられない。** 同じイメージの写しを作って
2 度起動し、

    1 回目  BTRON 区画が未構築 → 構築し、実行プログラムと印を置く
    2 回目  既に構築済み → 前回の印が読める

を確かめる。**写しを作るのは、ビルド成果物を書き換えないため**
（検査が成果物を汚すと、2 度目の `ctest` が 1 回目と違う条件になる）。

    python3 tools/run_selfboot.py --arch arm64 build/arm64/btron-arm64-selfboot.img
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
RUN_QEMU = HERE / "run_qemu.py"

# 1 回目に出ていなければならないもの / 出てはいけないもの
FIRST_EXPECT = [
    "印        : SELFBOOT",
    "BTRON 区画が未構築なので構築します",
    "ファイルシステムを構築しました",
    "状態      : 初回（構築した）",
    "印        : 今回の起動で置いた",
]
FIRST_DENY = [
    "[ FAIL ]",
    "既に構築済み",
    "前回の印が残っている",
]

# 2 回目
SECOND_EXPECT = [
    "ファイルシステムは既に構築済みです",
    "状態      : 2 回目以降",
    "印        : 前回の印が残っている",
]
SECOND_DENY = [
    "[ FAIL ]",
    "BTRON 区画が未構築なので構築します",
]


def run_once(arch: str, image: Path, timeout: int, expect: list[str],
             deny: list[str], label: str) -> int:
    argv = [sys.executable, str(RUN_QEMU), "--arch", arch, "--uefi",
            "--timeout", str(timeout), "--input"]
    for e in expect:
        argv += ["--expect", e]
    for d in deny:
        argv += ["--deny", d]
    argv.append(str(image))

    print(f"=== {label} ===", flush=True)
    return subprocess.run(argv).returncode


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--arch", required=True, choices=("arm64", "x86_64"))
    ap.add_argument("--timeout", type=int, default=120)
    ap.add_argument("image", type=Path)
    args = ap.parse_args()

    if not args.image.exists():
        sys.exit(f"ありません: {args.image}")

    with tempfile.TemporaryDirectory(prefix="btron-selfboot-") as tmp:
        work = Path(tmp) / args.image.name
        shutil.copy2(args.image, work)

        rc = run_once(args.arch, work, args.timeout, FIRST_EXPECT, FIRST_DENY,
                      "1 回目: 未構築の区画を構築する")
        if rc != 0:
            print("1 回目で失敗しました", file=sys.stderr)
            return rc
        rc = run_once(args.arch, work, args.timeout, SECOND_EXPECT, SECOND_DENY,
                      "2 回目: 前回の内容が残っている")
        if rc != 0:
            print("2 回目で失敗しました（永続していない）", file=sys.stderr)
            return rc

    print("自立起動: 2 回の起動で内容が残ることを確かめました")
    return 0


if __name__ == "__main__":
    sys.exit(main())

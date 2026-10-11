#!/usr/bin/env python3
"""カーネルを QEMU で起動し、シリアル出力を判定する。

Phase 1 以降のテスト基盤 (docs/design-issues.md H8)。
CTest から呼ばれるほか、手で動かして様子を見るのにも使う。

起動経路は 2 つある (詳細 D8)。

  直接起動   QEMU の -kernel に ELF を渡す。速いがメモリマップも DTB も
             来ないので、カーネルが決め打ちで動く。
  UEFI 起動  GPT + ESP のディスクイメージをファームウェアに起動させる。
             D8 で決めた本来の方式。ACPI / DTB / GOP / メモリマップが来る。

  python3 tools/run_qemu.py --arch arm64 build/arm64/btron-arm64.elf
  python3 tools/run_qemu.py --arch arm64 --uefi build/arm64/btron-arm64.img
  python3 tools/run_qemu.py --arch arm64 --uefi --interactive ...

QEMU + ArmVirtQemu は既定で ACPI を渡してくる。D8 が arm64 に想定している
Device Tree 経路を試すには --machine virt,acpi=off を付ける。
GOP（起動時フレームバッファ）を出すには --extra=-device --extra=virtio-gpu-pci。

判定:
  - 終了コードが 0 でなければ失敗
  - --expect が指定されていれば、その文字列が出力に無ければ失敗
  - --deny が指定されていれば、その文字列が出力にあれば失敗
  - --timeout 秒を超えたら失敗 (QEMU を落とす)

テストハーネス (include/btron/test.h) は最後に "[ DONE ] 合格/総数" を出し、
失敗があれば終了コード 1 で ARM セミホスティング経由で終了する。
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]

# flake.nix の devShell が公開するファームウェアのパス
FIRMWARE_ENV = {"arm64": "ARM64_UEFI_FD", "x86_64": "X64_UEFI_FD"}


def firmware(arch: str) -> str:
    var = FIRMWARE_ENV[arch]
    path = os.environ.get(var)
    if not path or not Path(path).exists():
        sys.exit(
            f"{var} が未設定か、指すファイルがありません。\n"
            f"  nix develop の中で実行してください。\n"
            f"  非ネイティブ側は  nix build .#ovmf-x64 / .#ovmf-aarch64  で用意できます。"
        )
    return path


DEFAULT_MACHINE = {"arm64": "virt", "x86_64": "q35"}


def qemu_argv(arch: str, image: Path, interactive: bool, uefi: bool,
              machine: str, extra: list[str]) -> list[str]:
    if arch == "arm64":
        argv = [
            "qemu-system-aarch64",
            "-machine", machine,
            "-cpu", "cortex-a72",
            "-m", "512M",
            "-semihosting",          # hal_exit() が QEMU を終了させるのに使う
            "-nographic",
            "-no-reboot",
        ]
    elif arch == "x86_64":
        argv = [
            "qemu-system-x86_64",
            "-machine", machine,
            "-m", "512M",
            "-nographic",
            "-no-reboot",
            "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
        ]
    else:
        sys.exit(f"未知のアーキテクチャ: {arch}")

    if uefi:
        # -bios で与える。pflash で与えるには 64MB にパディングする必要があり、
        # NVRAM も要る。取り外し可能メディアの既定パス
        # (/EFI/BOOT/BOOTAA64.EFI) から起動するので NVRAM の起動項目は不要。
        argv += [
            "-bios", firmware(arch),
            "-drive", f"format=raw,if=virtio,file={image}",
        ]
    else:
        argv += ["-kernel", str(image)]

    argv += extra
    if not interactive:
        argv += ["-monitor", "none", "-serial", "stdio"]
    return argv


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("image", type=Path)
    ap.add_argument("--arch", required=True, choices=("arm64", "x86_64"))
    ap.add_argument("--timeout", type=float, default=30.0)
    ap.add_argument("--expect", action="append", default=[],
                    help="出力に含まれていなければ失敗とする文字列（複数可）")
    ap.add_argument("--deny", action="append", default=[],
                    help="出力に含まれていれば失敗とする文字列（複数可）")
    ap.add_argument("--interactive", action="store_true",
                    help="判定せず、端末をそのまま QEMU につなぐ")
    ap.add_argument("--uefi", action="store_true",
                    help="ディスクイメージを UEFI ファームウェアに起動させる (D8)")
    ap.add_argument("--machine",
                    help="QEMU の -machine に渡す文字列。"
                         "arm64 で ACPI を切り Device Tree を使わせるなら virt,acpi=off")
    ap.add_argument("--extra", action="append", default=[], metavar="ARG",
                    help="QEMU に素通しする引数（複数可）")
    ap.add_argument("--expect-exit", type=int, default=None, metavar="N",
                    help="この終了コードを正常とみなす。"
                         "わざと落として挙動を見る検査（緊急画面など）で使う")
    args = ap.parse_args()

    if not args.image.exists():
        sys.exit(f"イメージがありません: {args.image}")

    argv = qemu_argv(args.arch, args.image, args.interactive, args.uefi,
                     args.machine or DEFAULT_MACHINE[args.arch], args.extra)

    if args.interactive:
        print(f"$ {' '.join(argv)}\n", file=sys.stderr)
        return subprocess.call(argv)

    try:
        p = subprocess.run(argv, capture_output=True, text=True, timeout=args.timeout)
        out, rc, timed_out = p.stdout + p.stderr, p.returncode, False
    except subprocess.TimeoutExpired as e:
        # text=True でも TimeoutExpired が持つのは bytes のことがある
        def _s(v):
            if v is None:
                return ""
            return v.decode("utf-8", "replace") if isinstance(v, bytes) else v

        out = _s(e.stdout) + _s(e.stderr)
        rc, timed_out = -1, True
    except FileNotFoundError:
        sys.exit(f"{argv[0]} が見つかりません。nix develop の中で実行してください。")

    sys.stdout.write(out)
    if not out.endswith("\n"):
        sys.stdout.write("\n")

    problems: list[str] = []
    if timed_out:
        problems.append(f"{args.timeout} 秒でタイムアウトした")
    # x86_64 の isa-debug-exit は (code << 1) | 1 を返すので 1 は正常終了。
    ok_rc = (0,) if args.arch == "arm64" else (0, 1)
    if args.expect_exit is not None:
        ok_rc = (args.expect_exit,)
    if not timed_out and rc not in ok_rc:
        problems.append(f"終了コード {rc}")
    for s in args.expect:
        if s not in out:
            problems.append(f"期待した出力が無い: {s!r}")
    for s in args.deny:
        if s in out:
            problems.append(f"出てはいけない出力があった: {s!r}")

    # テストハーネスの集計行があれば、それも確かめる
    m = re.search(r"\[ DONE \] (\d+)/(\d+)", out)
    if m and m.group(1) != m.group(2):
        problems.append(f"テスト失敗 {m.group(1)}/{m.group(2)}")

    if problems:
        print("\n--- 失敗 ---", file=sys.stderr)
        for s in problems:
            print(f"  {s}", file=sys.stderr)
        return 1

    print("--- OK ---")
    return 0


if __name__ == "__main__":
    sys.exit(main())

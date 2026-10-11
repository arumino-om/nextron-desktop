#!/usr/bin/env python3
"""起動可能なディスクイメージ (GPT + ESP) を作る。

docs/design-issues.md 詳細 D8「本OS での方針」:

    ブートローダ  UEFI (x86_64 / arm64 共通)
    区画          GPT。BTRON 区画の GUID を独自定義

いまは ESP だけを作る。BTRON ファイルシステム区画は Phase 3 で足す。

    python3 tools/mkimage.py --arch arm64 \\
        --loader build/arm64/BOOTAA64.EFI \\
        --kernel build/arm64/btron-arm64.elf \\
        --out    build/arm64/btron-arm64.img

ESP の中身:

    /EFI/BOOT/BOOTAA64.EFI   取り外し可能メディアの既定パス。
                             NVRAM に起動項目が無くてもファームウェアが拾う
    /BTRON/KERNEL.ELF        カーネル本体。ローダがこのパスを開く
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

# 取り外し可能メディアの既定ブートパス (UEFI 仕様 3.5.1.1)
DEFAULT_BOOT_NAME = {
    "arm64": "BOOTAA64.EFI",
    "x86_64": "BOOTX64.EFI",
}

# GPT の区画タイプ
ESP_TYPE = "C12A7328-F81F-11D2-BA4B-00A0C93EC93B"  # EFI System Partition

# BTRON 区画のタイプ GUID（本OS で独自に定義する。詳細 D8）。
# 既存のどのタイプとも衝突しないよう、先頭を "B7120000" にしてある。
BTRON_FS_TYPE = "B7120000-0000-4254-524F-4E4653303300"

ESP_OFFSET_MIB = 1


def run(argv: list[str]) -> None:
    p = subprocess.run(argv, capture_output=True, text=True)
    if p.returncode != 0:
        sys.stderr.write(p.stdout + p.stderr)
        sys.exit(f"失敗: {' '.join(argv[:3])} ...  (終了コード {p.returncode})")


def need(prog: str) -> str:
    path = shutil.which(prog)
    if path is None:
        sys.exit(f"{prog} が見つかりません。nix develop の中で実行してください。")
    return path


def build_esp(esp: Path, size_mib: int, boot_name: str,
              loader: Path, kernel: Path, panic_test: bool = False,
              hold: bool = False) -> None:
    """FAT の ESP イメージを作り、ローダとカーネルを入れる。"""
    need("mformat")
    need("mcopy")
    need("mmd")

    esp.parent.mkdir(parents=True, exist_ok=True)
    with esp.open("wb") as f:
        f.truncate(size_mib * 1024 * 1024)

    # -F = FAT32。ESP は FAT32 か FAT16。32MB 未満だと FAT32 は作れない。
    run(["mformat", "-i", str(esp), "-F", "-v", "BTRONESP", "::"])
    run(["mmd", "-i", str(esp), "::/EFI", "::/EFI/BOOT", "::/BTRON"])
    run(["mcopy", "-i", str(esp), str(loader), f"::/EFI/BOOT/{boot_name}"])
    run(["mcopy", "-i", str(esp), str(kernel), "::/BTRON/KERNEL.ELF"])

    if panic_test:
        # 緊急画面 (T4-02) を実機の経路で検査するための印。
        #
        # **カーネルもローダも作り直さない。** フォントを含むカーネルは
        # 再ビルドが重いので、同じバイナリのまま ESP の中身だけ変えて
        # 経路を切り替える。ローダがこのファイルの有無を見て
        # BootInfo.flags に BI_F_PANIC_TEST を立てる。
        marker = esp.with_suffix(".marker")
        marker.write_text("緊急画面の検査用。この印があると、起動時検査の"
                          "あとにわざと例外を起こします。\n", encoding="utf-8")
        run(["mcopy", "-i", str(esp), str(marker), "::/BTRON/PANICTEST"])
        marker.unlink()

    if hold:
        # 起動時検査が終わっても電源を落とさず、画面を残して居座る印。
        # 手で画面を見るためにある（run_qemu.py --gui）。
        # これも**カーネルを作り直さない**。
        marker = esp.with_suffix(".hold")
        marker.write_text("画面を見るための印。これがあると、起動時検査の"
                          "あとも電源を落とさず画面を残します。\n",
                          encoding="utf-8")
        run(["mcopy", "-i", str(esp), str(marker), "::/BTRON/HOLD"])
        marker.unlink()


def build_disk(out: Path, esp: Path) -> None:
    """GPT を書いたディスクイメージを作り、ESP を流し込む。"""
    sgdisk = need("sgdisk")

    esp_size = esp.stat().st_size
    # 先頭 1MiB に GPT、末尾 1MiB にバックアップ GPT の余地を残す
    total = ESP_OFFSET_MIB * 1024 * 1024 + esp_size + 1024 * 1024

    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("wb") as f:
        f.truncate(total)

    run([sgdisk,
         "--clear",
         "--new", f"1:{ESP_OFFSET_MIB}MiB:+{esp_size // (1024 * 1024)}MiB",
         "--typecode", f"1:{ESP_TYPE}",
         "--change-name", "1:BTRON ESP",
         str(out)])

    data = esp.read_bytes()
    with out.open("r+b") as f:
        f.seek(ESP_OFFSET_MIB * 1024 * 1024)
        f.write(data)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--arch", required=True, choices=("arm64", "x86_64"))
    ap.add_argument("--loader", type=Path, required=True, help="PE/COFF の UEFI アプリ")
    ap.add_argument("--kernel", type=Path, required=True, help="ELF のカーネル")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--esp-size", type=int, default=48,
                    help="ESP の大きさ (MiB)。FAT32 の下限があるので 33 以上")
    ap.add_argument("--hold", action="store_true",
                    help="検査のあとも電源を落とさず画面を残す印を入れる。"
                         "手で画面を見るときに使う")
    ap.add_argument("--panic-test", action="store_true",
                    help="緊急画面 (T4-02) の検査用の印を入れる。"
                         "起動時検査のあとにわざと例外を起こすようになる")
    args = ap.parse_args()

    for p in (args.loader, args.kernel):
        if not p.exists():
            sys.exit(f"ありません: {p}")
    if args.esp_size < 33:
        sys.exit("--esp-size は 33 以上にしてください (FAT32 の下限)")

    esp = args.out.with_suffix(".esp")
    build_esp(esp, args.esp_size, DEFAULT_BOOT_NAME[args.arch], args.loader,
              args.kernel, args.panic_test, args.hold)
    build_disk(args.out, esp)

    print(f"{args.out}  ({args.out.stat().st_size // (1024 * 1024)}MiB)")
    print(f"  /EFI/BOOT/{DEFAULT_BOOT_NAME[args.arch]}  <- {args.loader}")
    print(f"  /BTRON/KERNEL.ELF        <- {args.kernel}")
    if args.panic_test:
        print("  /BTRON/PANICTEST         <- 緊急画面の検査用の印")
    if args.hold:
        print("  /BTRON/HOLD              <- 検査後も画面を残す印")
    return 0


if __name__ == "__main__":
    sys.exit(main())

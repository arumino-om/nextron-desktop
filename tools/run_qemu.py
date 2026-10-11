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

【画面を見る】既定は --nographic で、**画面はどこにも出ない**。

  --gui         本物の窓を開く。フレームバッファ (ramfb) も自動で付く。
                シリアルは端末に出たままなので、両方を同時に見られる。
  --screenshot DIR
                QEMU のモニタ経由で画面を PNG に落とす。窓を開かないので
                CI でも使える。変化したフレームだけを残す。

ramfb を選んでいるのは、**リニアなフレームバッファを見せる**デバイスだから。
virtio-gpu は Blt 転送しか持たず、UEFI の GOP が
PixelBltOnly を返すのでカーネルが画面を取れない（詳細 D8）。

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
import socket
import struct
import subprocess
import sys
import tempfile
import time
import zlib
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
              machine: str, extra: list[str], gui: bool = False,
              monitor: str = "") -> list[str]:
    if arch == "arm64":
        argv = [
            "qemu-system-aarch64",
            "-machine", machine,
            "-cpu", "cortex-a72",
            "-m", "512M",
            "-semihosting",          # hal_exit() が QEMU を終了させるのに使う
            "-no-reboot",
        ]
    elif arch == "x86_64":
        argv = [
            "qemu-system-x86_64",
            "-machine", machine,
            "-m", "512M",
            "-no-reboot",
            "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
        ]
    else:
        sys.exit(f"未知のアーキテクチャ: {arch}")

    # 画面。--gui なら窓を開き、--screenshot なら窓無しで中身だけ撮る。
    if gui or monitor:
        # ramfb はリニアなフレームバッファを見せる。カーネルが UEFI GOP
        # 経由で掴めるのはこちらで、virtio-gpu では掴めない。
        if not any(a == "ramfb" or a.startswith("virtio-gpu") for a in extra):
            argv += ["-device", "ramfb"]
        argv += ["-display", "cocoa" if (gui and sys.platform == "darwin")
                 else ("gtk" if gui else "none")]
    else:
        argv += ["-nographic"]

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
    if monitor:
        # 画面を撮るためにモニタを UNIX ソケットへ出す。
        argv += ["-monitor", f"unix:{monitor},server,nowait",
                 "-serial", "stdio"]
    elif gui:
        # 窓とは別に、シリアルは端末へ出す。
        argv += ["-serial", "stdio"]
    elif not interactive:
        argv += ["-monitor", "none", "-serial", "stdio"]
    return argv


def ppm_to_png(src: Path, dst: Path) -> tuple[int, int]:
    """QEMU の screendump が吐く PPM (P6) を PNG に直す。

    Pillow を入れずに済ませる。PNG は zlib があれば数行で書ける形式で、
    ここで要るのは無圧縮の truecolor 1 枚だけ。
    """
    data = src.read_bytes()
    parts: list[bytes] = []
    i = 0
    while len(parts) < 4:
        while i < len(data) and data[i:i + 1].isspace():
            i += 1
        if data[i:i + 1] == b"#":           # コメント行
            while data[i:i + 1] != b"\n":
                i += 1
            continue
        j = i
        while j < len(data) and not data[j:j + 1].isspace():
            j += 1
        parts.append(data[i:j])
        i = j
    i += 1                                   # 最後の空白 1 文字
    w, h = int(parts[1]), int(parts[2])
    rgb = data[i:i + w * h * 3]

    raw = bytearray()
    for y in range(h):
        raw.append(0)                        # フィルタ種別 0 (None)
        raw += rgb[y * w * 3:(y + 1) * w * 3]

    def chunk(tag: bytes, body: bytes) -> bytes:
        return (struct.pack(">I", len(body)) + tag + body +
                struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
    png += chunk(b"IEND", b"")
    dst.write_bytes(png)
    return w, h


def screenshot_run(args) -> int:
    """QEMU のモニタ経由で画面を撮り続ける。

    窓を開かずに画面の中身だけ取れるので、表示が無い環境でも、
    CI でも使える。**変化したフレームだけ**を残す — 画面はほとんどの時間
    止まっているので、全部残すと同じ絵が何十枚もできる。
    """
    outdir = args.screenshot
    outdir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        sock = str(Path(tmp) / "mon.sock")
        raw = Path(tmp) / "frame.ppm"
        argv = qemu_argv(args.arch, args.image, False, args.uefi,
                         args.machine or DEFAULT_MACHINE[args.arch],
                         args.extra, monitor=sock)
        print(f"$ {' '.join(argv)}\n", file=sys.stderr)
        proc = subprocess.Popen(argv, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        mon = None
        deadline = time.time() + args.timeout
        last = b""
        n = 0
        try:
            # ソケットができるまで待つ
            while time.time() < deadline and mon is None:
                try:
                    mon = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                    mon.connect(sock)
                except OSError:
                    mon = None
                    time.sleep(0.05)
            if mon is None:
                proc.kill()
                sys.exit("QEMU のモニタにつながりませんでした")
            mon.settimeout(2.0)
            while time.time() < deadline and proc.poll() is None:
                try:
                    mon.sendall(f"screendump {raw}\n".encode())
                    time.sleep(args.shot_interval)
                    try:
                        mon.recv(65536)
                    except socket.timeout:
                        pass
                    if not raw.exists():
                        continue
                    cur = raw.read_bytes()
                    if cur == last or len(cur) < 16:
                        continue
                    last = cur
                    n += 1
                    dst = outdir / f"screen-{n:03d}.png"
                    w, h = ppm_to_png(raw, dst)
                    print(f"  {dst}  ({w}x{h})", file=sys.stderr)
                except (BrokenPipeError, OSError):
                    break
        finally:
            if proc.poll() is None:
                proc.kill()
            proc.wait()
    print(f"\n画面 {n} 枚を {outdir} に保存しました。", file=sys.stderr)
    return 0 if n > 0 else 1


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
    ap.add_argument("--gui", action="store_true",
                    help="本物の窓を開いて画面を見る。フレームバッファも付く")
    ap.add_argument("--screenshot", type=Path, metavar="DIR",
                    help="画面を PNG に落とす。窓は開かないので CI でも使える")
    ap.add_argument("--shot-interval", type=float, default=0.3, metavar="SEC",
                    help="--screenshot のとき、何秒ごとに撮るか")
    args = ap.parse_args()

    if not args.image.exists():
        sys.exit(f"イメージがありません: {args.image}")

    if args.screenshot is not None:
        return screenshot_run(args)

    argv = qemu_argv(args.arch, args.image, args.interactive, args.uefi,
                     args.machine or DEFAULT_MACHINE[args.arch], args.extra,
                     gui=args.gui)

    if args.interactive or args.gui:
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

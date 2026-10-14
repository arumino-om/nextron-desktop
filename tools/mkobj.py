#!/usr/bin/env python3
"""mkobj.py — ユーザプログラムの ELF を実行プログラムレコードに直す.

共有データ仕様 3.2 のレコードタイプ 9「実行プログラムレコード」の中身は
本OS が決めた形式 (include/btron/objfmt.h)。ここはその生成器である。

ELF のプログラムヘッダを読んで

  - 実行可能な PT_LOAD を「読み出し＋実行」の区画
  - それ以外の PT_LOAD を「読み書き」の区画

として 1 枚の像にまとめ、64 バイトのヘッダを付ける。再配置はしない
（BTRON_PROC_TEXT_BASE に固定で結合してある前提）。

`--c-array` を付けると、カーネルに埋め込むための C ソースも出す。
検査が「実身に書いて cre_prc する」ところまでを本物の経路で通せる。
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

OBJ_HDRSZ = 64
TEXT_BASE = 0x400000
PAGE = 4096

CPU = {"arm64": 0x80, "x86_64": 0x64}


def read_elf(path: Path):
    b = path.read_bytes()
    if b[:4] != b"\x7fELF" or b[4] != 2 or b[5] != 1:
        sys.exit(f"{path}: 64 ビット・リトルエンディアンの ELF ではありません")
    e_entry, e_phoff = struct.unpack_from("<QQ", b, 24)
    e_phentsize, e_phnum = struct.unpack_from("<HH", b, 54)
    segs = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type, p_flags = struct.unpack_from("<II", b, off)
        p_offset, p_vaddr, _, p_filesz, p_memsz = struct.unpack_from(
            "<QQQQQ", b, off + 8)
        if p_type != 1 or p_memsz == 0:      # PT_LOAD のみ
            continue
        segs.append({
            "flags": p_flags, "off": p_offset, "vaddr": p_vaddr,
            "filesz": p_filesz, "memsz": p_memsz,
        })
    if not segs:
        sys.exit(f"{path}: PT_LOAD がありません")
    return b, e_entry, segs


def build(path: Path, arch: str):
    raw, entry, segs = read_elf(path)
    base = min(s["vaddr"] for s in segs)
    if base != TEXT_BASE:
        sys.exit(f"{path}: 先頭が 0x{base:x} です。0x{TEXT_BASE:x} に結合してください")

    # 実行可能な区画の終わりが text と data の境。
    exec_end = max((s["vaddr"] + s["memsz"]) for s in segs if s["flags"] & 1)
    text_size = (exec_end - base + PAGE - 1) & ~(PAGE - 1)

    mem_end = max(s["vaddr"] + s["memsz"] for s in segs)
    file_end = max((s["vaddr"] + s["filesz"]) for s in segs)

    image = bytearray(file_end - base)
    for s in segs:
        at = s["vaddr"] - base
        image[at:at + s["filesz"]] = raw[s["off"]:s["off"] + s["filesz"]]

    # text 区画をページ境界まで 0 で伸ばす。
    if len(image) < text_size:
        image += bytes(text_size - len(image))
    data_size = len(image) - text_size
    bss_size = (mem_end - base) - len(image)
    if bss_size < 0:
        bss_size = 0

    hdr = bytearray(OBJ_HDRSZ)
    hdr[0:6] = b"BTOBJ1"
    struct.pack_into("<I", hdr, 8, 1)                   # version
    struct.pack_into("<I", hdr, 12, CPU[arch])          # cpu
    struct.pack_into("<I", hdr, 16, entry - base)       # entry
    struct.pack_into("<I", hdr, 20, text_size)
    struct.pack_into("<I", hdr, 24, data_size)
    struct.pack_into("<I", hdr, 28, bss_size)
    struct.pack_into("<I", hdr, 32, 0)                  # stack = 既定
    struct.pack_into("<I", hdr, 36, base)
    return bytes(hdr) + bytes(image)


def c_array(blob: bytes, name: str) -> str:
    out = [
        "/* 自動生成 (tools/mkobj.py)。手で直さないこと。 */\n",
        "#include <btron/typedef.h>\n\n",
        f"EXPORT CONST UB {name}[] = {{\n",
    ]
    for i in range(0, len(blob), 12):
        row = ", ".join(f"0x{c:02x}" for c in blob[i:i + 12])
        out.append(f"\t{row},\n")
    out.append("};\n\n")
    out.append(f"EXPORT CONST UW {name}_size = {len(blob)};\n")
    return "".join(out)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("elf", type=Path)
    ap.add_argument("--arch", default="arm64", choices=sorted(CPU))
    ap.add_argument("--out", type=Path, help="実行プログラムレコードの中身")
    ap.add_argument("--c-array", type=Path, help="カーネルに埋め込む C ソース")
    ap.add_argument("--name", default="btron_user_hello")
    args = ap.parse_args()

    blob = build(args.elf, args.arch)
    if args.out:
        args.out.write_bytes(blob)
    if args.c_array:
        args.c_array.write_text(c_array(blob, args.name))
    print(f"  実行プログラムレコード {len(blob)} バイト "
          f"(ヘッダ {OBJ_HDRSZ} + 本体 {len(blob) - OBJ_HDRSZ})",
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

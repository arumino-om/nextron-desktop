#!/usr/bin/env python3
"""起動可能なディスクイメージ (GPT + ESP) を作る。

docs/design-issues.md 詳細 D8「本OS での方針」:

    ブートローダ  UEFI (x86_64 / arm64 共通)
    区画          GPT。BTRON 区画の GUID を独自定義

区画は 2 つ。ESP（ローダとカーネル）と **BTRON 区画**（ファイルシステム）。
BTRON 区画は**空のまま**置く — 中身の形式はカーネルが知っていて、
起動列が「未構築なら構築する」ので、ホスト側に同じ形式の実装を
二重に持たない（規律 R1 と同じ姿勢）。

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
import struct
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
              hold: bool = False, selfboot: bool = False,
              no_uart: bool = False) -> None:
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

    if selfboot:
        # 起動時検査の代わりに本来の起動列を通す印 (D8 / startup.c)。
        # ドライバを登録し、BTRON 区画を接続し（未構築なら構築し）、
        # ディスク上の実行プログラムから外殻を起こす。
        marker = esp.with_suffix(".selfboot")
        marker.write_text("本来の起動列を通す印。これがあると、起動時検査の"
                          "代わりにディスクからの起動を試します。\n",
                          encoding="utf-8")
        run(["mcopy", "-i", str(esp), str(marker), "::/BTRON/SELFBOOT"])
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

    if no_uart:
        # 「シリアルが無い機械」のふりをさせる印（規律 R29 の逆テスト）。
        #
        # 実機のノート PC には 16550 が無いことがあり、そのとき既定
        # アドレスへ書き込むと外部アボートで無言に止まる（詳細 F9）。
        # QEMU の virt / q35 は記述からシリアルを消せないので、
        # **この印だけが実機の経路を再現する手段**になる。
        marker = esp.with_suffix(".nouart")
        marker.write_text("シリアルが無い機械のふりをさせる印。これがあると、"
                          "記述から読んだ UART を捨てて黙ります。\n",
                          encoding="utf-8")
        run(["mcopy", "-i", str(esp), str(marker), "::/BTRON/NOUART"])
        marker.unlink()


def build_disk(out: Path, esp: Path, fs_size_mib: int) -> None:
    """GPT を書いたディスクイメージを作り、ESP を流し込む。

    区画 2 (BTRON 区画) は空のまま置く。カーネルの起動列が
    タイプ GUID でそれを見つけ、未構築なら構築する。
    """
    sgdisk = need("sgdisk")

    esp_size = esp.stat().st_size
    esp_mib = esp_size // (1024 * 1024)
    # 先頭 1MiB に GPT、末尾 1MiB にバックアップ GPT の余地を残す
    total = (ESP_OFFSET_MIB + esp_mib + fs_size_mib + 1) * 1024 * 1024

    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("wb") as f:
        f.truncate(total)

    argv = [sgdisk,
            "--clear",
            "--new", f"1:{ESP_OFFSET_MIB}MiB:+{esp_mib}MiB",
            "--typecode", f"1:{ESP_TYPE}",
            "--change-name", f"1:{ESP_NAME}"]
    if fs_size_mib > 0:
        argv += ["--new", f"2:0:+{fs_size_mib}MiB",
                 "--typecode", f"2:{BTRON_FS_TYPE}",
                 "--change-name", f"2:{BTRON_FS_NAME}"]
    argv.append(str(out))
    run(argv)

    data = esp.read_bytes()
    with out.open("r+b") as f:
        f.seek(ESP_OFFSET_MIB * 1024 * 1024)
        f.write(data)

    # **確かめるのは出来上がった物**。GPT を書いた直後ではなく、ESP を
    # 流し込んだ後に読み返す（区画 2 が空であることも、ここで初めて
    # 「最終状態で空」という意味になる）。
    verify_disk(out, esp_size)


# GPT の論理ブロックの大きさ。UEFI 仕様はメディアのブロック長を使うと
# 定めているが、ここで作るのはイメージなので 512 で固定する。
# 区画の名前。**作る側と確かめる側で同じ定数を使う**ために置いてある
# （sgdisk へ渡す値と verify_disk が照らす値を 2 か所に書かない）。
ESP_NAME = "BTRON ESP"
BTRON_FS_NAME = "BTRON FS"

MIB = 1024 * 1024
SECTOR = 512


def guid_bytes(text: str) -> bytes:
    """GUID の文字列表記を、GPT が置くバイト列へ直す。

    前 3 つの欄 (4/2/2 バイト) がリトルエンディアン、残り 8 バイトが
    そのままという混在順序。UEFI 仕様 Appendix A。
    """
    raw = bytes.fromhex(text.replace("-", ""))
    return raw[0:4][::-1] + raw[4:6][::-1] + raw[6:8][::-1] + raw[8:16]


def verify_disk(out: Path, esp_size: int) -> None:
    """書いた GPT を読み返して、区画が 2 つ切れていることを確かめる。

    「不変条件は文書上の約束にせずテストにする」(tasks.md 0.4-2) の
    ホスト側の分である。起動列 (T3-03) は区画表を読んでタイプ GUID で
    BTRON 区画を見つけるが、**媒体の作り方が壊れていると症状はそちらに
    出る** — 「区画が見つからない」とだけ言われて、原因がイメージ側か
    カーネル側か分からない。作った場所で作った物を確かめておけば、
    切り分けが要らなくなる。ctest の構成を増やさずに毎ビルド走る。
    """
    image = out.stat().st_size
    with out.open("rb") as f:
        f.seek(SECTOR)
        head = f.read(SECTOR)
        if head[0:8] != b"EFI PART":
            sys.exit(f"{out}: GPT のヘッダがありません")
        # ヘッダの +40 から FirstUsableLBA / LastUsableLBA、
        # +72 から PartitionEntryLBA / 個数 / 1 件の大きさ (UEFI 仕様 5.3.2)。
        # 区画の置ける範囲はヘッダ自身が持っているので、ここで数えない。
        first_usable, last_usable = struct.unpack_from("<QQ", head, 40)
        entry_lba, count, entry_size = struct.unpack_from("<QII", head, 72)

        if (last_usable + 1) * SECTOR > image:
            sys.exit(f"{out}: GPT が宣言する使用可能範囲がイメージより大きい")

        f.seek(entry_lba * SECTOR)
        table = f.read(count * entry_size)

        used = []
        for i in range(count):
            e = table[i * entry_size:(i + 1) * entry_size]
            if e[0:16] == bytes(16):
                continue
            start, end = struct.unpack_from("<QQ", e, 32)
            name = e[56:56 + 72].decode("utf-16-le").rstrip("\0")
            used.append((i + 1, e[0:16], start, end, name))

        want = [(ESP_TYPE, ESP_NAME), (BTRON_FS_TYPE, BTRON_FS_NAME)]
        if len(used) != len(want):
            sys.exit(f"{out}: 区画が {len(used)} 個です（{len(want)} 個のはず）")

        prev_end = first_usable - 1
        for (num, kind, start, end, name), (want_type, want_name) in zip(used, want):
            if kind != guid_bytes(want_type):
                sys.exit(f"{out}: 区画 {num} のタイプ GUID が違います")
            if name != want_name:
                sys.exit(f"{out}: 区画 {num} の名前が「{name}」です"
                         f"（「{want_name}」のはず）")
            if start <= prev_end:
                sys.exit(f"{out}: 区画 {num} が手前の区画（または GPT）と"
                         f"重なっています")
            if end > last_usable:
                sys.exit(f"{out}: 区画 {num} が使用可能範囲からはみ出しています")
            prev_end = end

        esp_num, _, esp_start, esp_end, _ = used[0]
        if esp_start * SECTOR != ESP_OFFSET_MIB * MIB:
            sys.exit(f"{out}: 区画 {esp_num} の開始位置が ESP を流し込んだ"
                     f"位置と違います")
        if (esp_end - esp_start + 1) * SECTOR != esp_size:
            sys.exit(f"{out}: 区画 {esp_num} の大きさが ESP と違います")

        # 区画 2 は切っただけで中身を書いていないこと（詳細 F9）。
        # 形式を書くのは OS の fsb_format() の仕事で、ここで書くと
        # 同じオンディスク形式が 2 つになる。
        f.seek(used[1][2] * SECTOR)
        if f.read(4096) != bytes(4096):
            sys.exit(f"{out}: 区画 2 に何か書かれています（空のはず）")

        # **ESP に FAT があること。**
        #
        # GPT だけ見ていると「区画は正しいが中身が無い」を通してしまう。
        # 実際にこの移植の途中で踏んだ —— mkimage.py が例外で落ちた後、
        # GPT は書けていて ESP だけが空のイメージが残り、ファームウェアは
        # `No bootable option or device was found` と言うだけだった。
        # どこが悪いのか（イメージか、ローダか、カーネルか）が分からない。
        #
        # FAT32 の BPB は +82 に "FAT32   "、+510 に 0x55AA を持つ
        # （Microsoft FAT 仕様）。署名だけ見れば mformat が走ったことは分かる。
        f.seek(esp_start * SECTOR)
        boot = f.read(SECTOR)
        if len(boot) < SECTOR or boot[510:512] != b"\x55\xAA":
            sys.exit(f"{out}: ESP に FAT の署名 (0x55AA) がありません")
        if boot[82:90] != b"FAT32   ":
            sys.exit(f"{out}: ESP が FAT32 ではありません")

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--arch", required=True, choices=("arm64", "x86_64"))
    ap.add_argument("--loader", type=Path, required=True, help="PE/COFF の UEFI アプリ")
    ap.add_argument("--kernel", type=Path, required=True, help="ELF のカーネル")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--esp-size", type=int, default=48,
                    help="ESP の大きさ (MiB)。FAT32 の下限があるので 33 以上")
    ap.add_argument("--fs-size", type=int, default=16,
                    help="BTRON 区画の大きさ (MiB)。0 なら作らない")
    ap.add_argument("--hold", action="store_true",
                    help="検査のあとも電源を落とさず画面を残す印を入れる。"
                         "手で画面を見るときに使う")
    ap.add_argument("--selfboot", action="store_true",
                    help="本来の起動列 (D8) を通す印を入れる。ドライバ登録・"
                         "BTRON 区画の構築と接続・ディスク上の実行プログラム"
                         "からの起動を行う")
    ap.add_argument("--panic-test", action="store_true",
                    help="緊急画面 (T4-02) の検査用の印を入れる。"
                         "起動時検査のあとにわざと例外を起こすようになる")
    ap.add_argument("--no-uart", action="store_true",
                    help="シリアルが無い機械のふりをさせる印を入れる (R29)。"
                         "記述から読んだ UART を捨てて黙るようになる")
    args = ap.parse_args()

    for p in (args.loader, args.kernel):
        if not p.exists():
            sys.exit(f"ありません: {p}")
    if args.esp_size < 33:
        sys.exit("--esp-size は 33 以上にしてください (FAT32 の下限)")

    esp = args.out.with_suffix(".esp")
    build_esp(esp, args.esp_size, DEFAULT_BOOT_NAME[args.arch], args.loader,
              args.kernel, args.panic_test, args.hold, args.selfboot,
              args.no_uart)
    build_disk(args.out, esp, args.fs_size)

    print(f"{args.out}  ({args.out.stat().st_size // (1024 * 1024)}MiB)")
    print(f"  /EFI/BOOT/{DEFAULT_BOOT_NAME[args.arch]}  <- {args.loader}")
    print(f"  /BTRON/KERNEL.ELF        <- {args.kernel}")
    if args.fs_size > 0:
        print(f"  区画 2: BTRON FS ({args.fs_size}MiB。空。起動列が構築する)")
    if args.panic_test:
        print("  /BTRON/PANICTEST         <- 緊急画面の検査用の印")
    if args.hold:
        print("  /BTRON/HOLD              <- 検査後も画面を残す印")
    if args.selfboot:
        print("  /BTRON/SELFBOOT          <- 本来の起動列を通す印")
    if args.no_uart:
        print("  /BTRON/NOUART            <- シリアルが無い機械のふりをさせる印")
    return 0


if __name__ == "__main__":
    sys.exit(main())

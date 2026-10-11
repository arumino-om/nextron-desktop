# nextron-desktop

**BTRON3 仕様への準拠を目標とする、GUI を持つ汎用 OS の独立実装。** x86_64 / arm64 向け。

## OS 概要

準拠の目標とするのは以下の 2 つの仕様です。

- **BTRON3 仕様書 Ver 3.20.00**（1999, TRON Association）
- **超漢字 (B-right/V R4) 仕様書**（1999-2001, Personal Media Corporation）

BTRON3 の四層構造（中心核 / 周辺核 / ディスプレイプリミティブ / 外殻）と、
ディレクトリを持たない実身・仮身モデルのファイルシステムを踏襲しつつ、
64bit・SMP・プロセス分離を前提に再設計しています。中心核の基準仕様には
μT-Kernel 3.0 (IEEE 2050-2018) を用います。

設計判断とその根拠は **[`docs/design-issues.md`](docs/design-issues.md)** に、
仕様書から抽出した API 一覧は **[`docs/api/`](docs/api/)** にあります。

実装は仕様書のみを根拠とする独立実装であり、既存の製品やそのバイナリに
由来するコードは含みません。TRON Association / Personal Media Corporation /
TRON Forum のいずれとも関係はなく、これらによる承認も受けていません。

> **現在の状態:** Phase 1（ブート + HAL）の途中です。**arm64 が UEFI 経由で
> 起動し**、ページングを立ち上げてカーネルを上位半分へ移し、QEMU 上で設計上の
> 不変条件 18 件を検査するところまで動きます。中心核（タスク・スケジューラ・
> 割込み・時間管理）は未着手です。x86_64 は HAL を型検査しているだけで未検証です。

## 開発環境の構築

[Nix](https://nixos.org/) の flake を使います。

```sh
nix develop
```

これで以下が揃います。

| | |
|---|---|
| clang 21 / lld / llvm-* | 単体で `x86_64-unknown-none-elf` と `aarch64-unknown-none-elf` の両方を出力する |
| lldb | QEMU の gdbstub に接続する |
| QEMU 11 | arm64 ホストでは `hvf` アクセラレーションが使える |
| UEFI ファームウェア | ホストと同じ CPU ファミリのものが自動で入る |
| CMake / Ninja | |
| Python / ImageMagick | 仕様文書の資産化ツール用 |

非ネイティブ側の UEFI ファームウェアは edk2 のクロスビルドになるため既定では
入れていません。必要になったら個別に取得します。

```sh
nix build .#ovmf-x64       # x86_64 のファームウェア
nix build .#ovmf-aarch64   # arm64 のファームウェア
```

### ビルド

```sh
cmake --preset arm64   && cmake --build --preset arm64
cmake --preset x86_64  && cmake --build --preset x86_64
```

`-Wall -Wextra -Wconversion -Wsign-conversion -Werror` でコンパイルします。
32bit と 64bit の暗黙の縮小変換を禁じる設計規律を、文書上の約束ではなく
ビルドで強制するためです。

ビルドすると次の 3 つができます。

| | |
|---|---|
| `btron-arm64.elf` | カーネル本体 |
| `BOOTAA64.EFI` | UEFI ブートローダ（PE/COFF。カーネルとは別のトリプルでビルドされます） |
| `btron-arm64.img` | 起動可能なディスクイメージ（GPT + ESP） |

### 実行

```sh
ctest --preset arm64                 # 直接起動 / UEFI / Device Tree / GOP の 4 通り

python3 tools/run_qemu.py --arch arm64 --uefi --interactive build/arm64/btron-arm64.img
python3 tools/run_qemu.py --arch arm64 --interactive        build/arm64/btron-arm64.elf
```

UEFI 経由がディスクから起動する本来の経路で、メモリマップ・ACPI / Device Tree・
フレームバッファを受け取ります。ELF を QEMU の `-kernel` に直接渡す経路も
残してあり、こちらはファームウェアを通らないので速く、カーネル単体の確認に使います。

### 仕様文書

**仕様文書はこのリポジトリに含まれていません**（後述）。`tools/spec/` の
資産化ツールを使う場合は、各自が入手した文書を環境変数 `BTRON_DOC` /
`BRIGHTV_DOC` で指すか、リポジトリルート直下に置いてください。
詳細は [`tools/spec/README.md`](tools/spec/README.md)。

## ライセンス

| 対象 | ライセンス |
|---|---|
| 中心核・周辺核・外殻サーバ・ドライバ | **GPLv2**（[`LICENSE`](LICENSE)） |
| `src/lib/` 以下（アプリケーションがリンクするライブラリ群） | **LGPL 予定**。現時点では未分離 |

アプリケーションがシステムコール経由で OS の機能を使うことは、Linux の
`COPYING` と同様に派生物とは見なしません。

### 取り込まないもの

- **仕様文書そのもの**（BTRON3 仕様書 / 超漢字仕様書）。著作権者が別にあります
- **T-License 2.2 のコード**（μT-Kernel 3.0 等）。ヘッダを含め、GPLv2・LGPL の
  いずれとも非互換です。仕様を読んで独自実装することだけを行います
- **フォントや辞書のデータ**（GT書体、大漢和辞典収録文字など）。別途ライセンスが必要です

経緯と根拠は `docs/design-issues.md` の §1・詳細 D10・詳細 J2 を参照してください。

### 商標

`TRON` / `BTRON` は TRON Forum、`超漢字` / `B-right/V` / `I-right/V` は
Personal Media Corporation の商標と考えられます。本プロジェクトはこれらを
製品名には使用せず、準拠目標の記述にのみ用いています。

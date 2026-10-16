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
作業単位と受入条件は **[`docs/tasks/`](docs/tasks/)**（一覧は
[`INDEX.md`](docs/tasks/INDEX.md)）に、並列に進めるときの順番と分担は
**[`docs/parallel.md`](docs/parallel.md)** に、番号の予約は
**[`docs/registry.md`](docs/registry.md)** に、仕様書から抽出した API 一覧は
**[`docs/api/`](docs/api/)** にあります。

実装は仕様書のみを根拠とする独立実装であり、既存の製品やそのバイナリに
由来するコードは含みません。TRON Association / Personal Media Corporation /
TRON Forum のいずれとも関係はなく、これらによる承認も受けていません。

> **現在の状態:** **arm64 で UEFI からディスクイメージを起動し、四層のうち
> 下 3 層が動きます。** QEMU 9 構成で設計上の不変条件 174 件を検査します。
>
> - **中心核（μT-Kernel 3.0 準拠）** — タスク・同期通信（セマフォ /
>   イベントフラグ / ミューテックス / メールボックス / メッセージバッファ）・
>   メモリプール・周期ハンドラ・時間管理・割込み（GICv2 / GICv3）
> - **周辺核（仕様 第1章）がほぼ揃いました** — プロセス / タスク (§1.1)・
>   メッセージ (§1.2)・プロセス間同期通信とランデブ (§1.3)・グローバル名 (§1.4)・
>   メモリ管理 (§1.5。ローカルは正、共有とシステムは負のアドレス)・
>   **ファイル管理 (§1.6。実身 / 仮身・レコード・パス名・アクセス管理)**・
>   イベント管理 (§1.7)・デバイス管理 (§1.8)・時計 (§1.9)・システム管理 (§1.10)
> - **ディスプレイプリミティブ（仕様 第2章）の基本部分** — 描画環境・点・直線・
>   長方形・ビットマップ転送・スクロール・文字描画・ポインタ制御。
>   曲線系・回転・任意領域クリップ・ピッキングはまだです
> - **外殻がユーザ特権 (EL0) で動きます** — ウィンドウマネージャ自体が EL0 の
>   プロセスで（詳細 E22「1 プロセス 1 サーバ」）、カーネルに残るのは利用側の
>   スタブだけ。`cre_prc` は**実身の実行プログラムレコードから読んで** EL0 で
>   走らせます。**窓ごとのバックバッファを持ち、マネージャが損傷矩形だけを
>   合成します**（E2）
> - **画面にかな・漢字が出ます**（GNU Unifont をビルド時に取得。B8）。
>   例外が起きると画面を占有して情報を出す**緊急画面**も動きます — 外殻の
>   協力を必要としないので、ウィンドウマネージャが落ちても使えます
> - **キーボードとマウス (virtio-input) で実際に触れます** — 窓を前に出す、
>   枠をつかんで動かす、四隅で変形する、描く、打つ
>
> **残っているもの。** 一覧は [`docs/tasks/INDEX.md`](docs/tasks/INDEX.md)、
> 順番と分担は [`docs/parallel.md`](docs/parallel.md) にあります。大きいのは
> 次の 4 つです。
>
> 1. **自立起動** — ブロックデバイスのドライバが無く、ファイルシステムは
>    RAM ディスク上にしかないので電源を切ると消えます。`/SYS/DEVCONF` と
>    初期プロセスから外殻を立ち上げる本来の起動列もまだで、いまは検査
>    コードが全部を駆動しています
> 2. **外殻の残りのマネージャ** — 実身/仮身・パーツ・メニュー・パネル・
>    トレー・データ・印刷・TCP/IP・TIP（仕様 第3章の 10 本）
> 3. **TAD と基本エディタ・日本語入力**（Phase 7）
> 4. **x86_64 の起動経路・SMP・タスク例外** — x86_64 は HAL を型検査して
>    いるだけで未検証です

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

### nix を使わない場合

`nix develop` が本来の経路で、`flake.lock` が clang / QEMU / ファームウェア /
Unifont のハッシュまで固定します。**それが使えない環境（WSL など）向けの
逃げ道**を書いておきます。ツールチェーンのバージョンは flake と揃えること。

```sh
# Ubuntu 26.04 の例。clang 21 が apt にある
sudo apt install -y clang-21 lld-21 llvm-21 cmake ninja-build      qemu-system-arm qemu-system-x86 qemu-efi-aarch64 ovmf      mtools dosfstools gdisk xorriso python3

# ツールチェーンは無印の名前で引かれるので繋ぐ
for t in clang clang++ ld.lld lld-link llvm-ar llvm-ranlib llvm-objcopy          llvm-objdump llvm-readelf llvm-nm llvm-strip llvm-size; do
  sudo ln -sf "$(command -v $t-21 || echo /usr/lib/llvm-21/bin/$t)" /usr/local/bin/$t
done

# グリフデータ (B8)。リポジトリには入れない。flake と同じ版を取る
curl -fsSLO https://ftp.gnu.org/gnu/unifont/unifont-17.0.05/unifont-17.0.05.tar.gz

export UNIFONT_SRC=$PWD/unifont-17.0.05.tar.gz
export UNIFONT_VERSION=17.0.05
export ARM64_UEFI_FD=/usr/share/qemu-efi-aarch64/QEMU_EFI.fd
export X64_UEFI_FD=/usr/share/ovmf/OVMF.fd
```

**受入条件は環境で変わりません** — どちらでも `ctest --preset arm64` が
9 構成すべて通ること。

### ビルド

```sh
cmake --preset arm64   && cmake --build --preset arm64
cmake --preset x86_64  && cmake --build --preset x86_64
```

`-Wall -Wextra -Wconversion -Wsign-conversion -Werror` でコンパイルします。
32bit と 64bit の暗黙の縮小変換を禁じる設計規律を、文書上の約束ではなく
ビルドで強制するためです。

ビルドすると次のものができます。

| | |
|---|---|
| `btron-arm64.elf` | カーネル本体 |
| `BOOTAA64.EFI` | UEFI ブートローダ（PE/COFF。カーネルとは別のトリプルでビルドされます） |
| `btron-arm64.img` | 起動可能なディスクイメージ（GPT + ESP） |
| `btron-arm64-gui.img` | 画面を見るためのイメージ。検査のあと電源を落とさず画面を残します |
| `btron-arm64-panic.img` | 緊急画面の検査用。わざと例外を起こします |

後ろの 2 つは**カーネルもローダも同じもの**で、ESP に置く印のファイルだけが
違います（`\BTRON\HOLD` と `\BTRON\PANICTEST`）。フォントを含むカーネルは
再ビルドが重いので、イメージの中身だけで振る舞いを切り替えています。

### 画面を見る

既定では `-nographic` で動かすので**画面はどこにも出ません**。
見るには次のどちらかを使います。

```sh
# 本物の窓を開く。ウィンドウが 2 枚出たまま止まります
python3 tools/run_qemu.py --arch arm64 --uefi --gui build/arm64/btron-arm64-gui.img

# 窓を開かずに画面を PNG に落とす（表示が無い環境でも、CI でも使える）
python3 tools/run_qemu.py --arch arm64 --uefi --timeout 30 \
        --screenshot /tmp/shots build/arm64/btron-arm64-gui.img
```

`--gui` は終了しないので、閉じるのは窓を閉じるか `Ctrl-C` です。
`--screenshot` は**変化したフレームだけ**を残します。

どちらも**キーボードとポインタ (virtio-input) が付きます**。付けたくない
ときは `--no-input`、画面なしで入力だけ試すときは `--input` を使います。

入力装置を付けると `-machine virt,acpi=off` になります。virtio-mmio の枠を
記述しているのは Device Tree だけで、ACPI では DSDT の AML の中にあり、
まだ読めないためです（docs/tasks.md の T1-15）。

`--gui` の窓では**実際に触れます**。

| 操作 | 結果 |
|---|---|
| ポインタを動かす | 矢印が追従します |
| 奥の窓を押す | 手前に出ます |
| 窓の枠を押して動かす | 窓が移動します（シフトで縦横に拘束） |
| 四隅の黒い四角を押して動かす | 窓が変形します |
| 右の窓で押しながら動かす | 点が描けます |
| キーを打つ | 左の窓の下に出ます |

表示の無い環境でも、入力を流し込んで結果を撮れます。

```sh
python3 tools/run_qemu.py --arch arm64 --uefi --timeout 60 --input \
        --screenshot /tmp/shots --send-after 12 \
        --send "ptr 12000 20000" --send "sendkey b" \
        --send "press" --send "ptr 17000 24000" --send "release" \
        build/arm64/btron-arm64-gui.img
```

`ptr X Y` は QEMU の絶対座標 (0〜32767) で、画素への換算はゲスト側の
仕事です。`press` / `release` はボタン、それ以外は QEMU モニタの命令
（`sendkey a` など）として送られます。

### ユーザ特権で動く外殻

外殻は**カーネルの外**、保護されたユーザ特権 (EL0) で動きます。

**ウィンドウマネージャ自体がユーザ特権のプロセス**です（詳細 E22
「1 プロセス 1 サーバ」）。カーネルに残っているのは利用側のスタブだけで、
`wopn_wnd` などの呼び出しはランデブでそのプロセスへ届きます。

検査のたびに `src/user/shell.c` も実身から起動され、画面を借りて描き、
画素を読み返して確かめています。

描画は**システムコールではありません**。ディスプレイプリミティブは
ライブラリとしてプログラムに結合されるので、特権境界を越えるのは
「画面を借りるとき」「フォントの置き場を引くとき」「イベントを取るとき」
の 3 か所だけです。

### 実行

```sh
ctest --preset arm64   # 直接起動 / UEFI(ACPI) / DT / 入力 / GICv3+SMP / GOP / 緊急画面 の 9 通り + 文書の検査

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

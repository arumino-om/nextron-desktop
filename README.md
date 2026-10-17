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
> 4. **x86_64 の中心核・SMP・タスク例外** — **x86_64 は UEFI から起動し、
>    ページングを立て、ACPI から APIC を読み、起動時の不変条件 24 件まで
>    通ります**（T1-11）。APIC・タイマ割込み・コンテキスト切替がまだで、
>    そこが入ると中心核以上も両アーキで走ります

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
| `btron-<arch>.elf` | カーネル本体 |
| `BOOTAA64.EFI` | UEFI ブートローダ（PE/COFF。カーネルとは別のトリプルでビルドされます） |
| `btron-arm64.img` | 起動可能なディスクイメージ（GPT。ESP と BTRON FS の 2 区画） |
| `btron-arm64-gui.img` | 画面を見るためのイメージ。検査のあと電源を落とさず画面を残します |
| `btron-arm64-panic.img` | 緊急画面の検査用。わざと例外を起こします |

後ろの 2 つは**カーネルもローダも同じもの**で、ESP に置く印のファイルだけが
違います（`\BTRON\HOLD` と `\BTRON\PANICTEST`）。フォントを含むカーネルは
再ビルドが重いので、イメージの中身だけで振る舞いを切り替えています。

### 実機で動かす

媒体の作り方と、実機に通すまでの手順です。

**まだ実機で画面が出るところまでは行きません。** QEMU では自立起動が通って
いますが（T3-03。ディスクの実行プログラムから外殻が EL0 で起きる）、実機に
出るには**装置の見つけ方**が足りません —— virtio-mmio ではなく PCIe の先に
NVMe と xHCI が居ます（`docs/design-issues.md` 詳細 F9、`docs/tasks/` の
T6-04〜T6-07）。x86_64 の起動経路 (T1-11) も中心核の手前までです。
ここにあるのは**媒体を作って実機に差すところまで**で、`ctest` の受入条件は
すべて QEMU 側で閉じています。実機でやるのは**実機でしか分からないことだけ**です。

#### 媒体の形

`btron-<arch>.img` は GPT の 2 区画です（詳細 F9「媒体と区画」）。

| 区画 | 形式 | 中身 |
|---|---|---|
| 1 | ESP (FAT32) | `/EFI/BOOT/BOOTAA64.EFI`（x86_64 なら `BOOTX64.EFI`）と `/BTRON/KERNEL.ELF` |
| 2 | BTRON FS | **空**。タイプ GUID `B7120000-0000-4254-524F-4E4653303300` だけ振ってあります |

**区画 2 の中身はホスト側で書きません。** 形式は OS が `fsb_format()` で
書きます。同じオンディスク形式を Python と C に二重実装すると必ずずれるので、
書き手を 1 つに保つためです（A7 / 規律 R1 と同じ理由）。
`tools/mkimage.py` の仕事は区画を切るところまでで、書いた GPT を読み返して
2 区画になっていることをその場で確かめています。

区画 1 の既定パスは取り外し可能メディアの既定ブートパス (UEFI 仕様 3.5.1.1)
なので、**NVRAM に起動項目を登録しなくてもファームウェアが拾います**。

#### USB メモリへ書く

イメージはそのままディスクの先頭から書けます。**書き込み先を間違えると
その装置の中身が消えます。** 容量と名前を見て確かめてください。

```sh
# Linux
lsblk                              # 書き込み先を確かめる
sudo dd if=build/arm64/btron-arm64.img of=/dev/sdX bs=4M conv=fsync status=progress

# macOS
diskutil list                      # 書き込み先を確かめる
diskutil unmountDisk /dev/diskN
sudo dd if=build/arm64/btron-arm64.img of=/dev/rdiskN bs=4m   # r 付きが生の装置
diskutil eject /dev/diskN
```

書いた直後は**バックアップ GPT がイメージの大きさの位置にいます**。媒体の
残りを BTRON FS 区画に使うなら、末尾へ移してから区画 2 を切り直します。

```sh
sudo sgdisk --move-second-header /dev/sdX
sudo sgdisk --delete=2 --new=2:0:0 \
            --typecode=2:B7120000-0000-4254-524F-4E4653303300 \
            --change-name="2:BTRON FS" /dev/sdX
```

切り直しても中身は書かれないので、形式は OS が書く形のままです。

#### Secure Boot を切る

**ローダに署名が無いので、Secure Boot が有効だとファームウェアが弾きます。**
症状は「起動項目に出ない」「選んでも何も起きずに次の候補へ進む」で、
**画面には理由が出ません**。切ったことを先に確かめてください。

1. 電源投入時にファームウェア設定へ入る（`F2` / `Del` / `F10` / `Esc` など機種による）
2. **Secure Boot** を `Disabled` にする。機種によっては先に管理者パスワードの
   設定か「Setup / Custom モード」への切り替えが要ります
3. **CSM / Legacy Boot は `Disabled` のまま**にする。UEFI 経由でしか起動しません
4. USB からの起動を許可し、起動順で USB を先にする

Mac（Apple Silicon）は外部媒体からの UEFI 起動ができないので、arm64 の
実機確認には使えません。arm64 の実機は UEFI (EDK II) が載った SBC か
Windows on Arm 機になります。

#### 見たものを残す

**実機にはシリアルが無いことがあります**（詳細 F9「実機で初めて効く差」）。
その場合、出力は画面だけになります。`hal_console_tee()` がコンソール出力を
画面へ複製しているので、起動時検査の `[ PASS ]` も緊急画面もそのまま画面に出ます。

**画面を写真で撮るだけでよいです。** スクリーンショットの比較は QEMU 側で
やっているので（`run_qemu.py --screenshot`）、実機では「QEMU で出るものと
同じものが出ているか」を目で見れば足ります。

- 検査が最後まで行くと `[ DONE ] 174/174` が出ます。ここが写っていれば通っています
- 途中で止まったら、**最後に出ている行**が分かるように撮ってください。
  どの検査で止まったかがそのまま手がかりになります
- 例外なら緊急画面に `*** 例外 ***` と原因が出ます。
  FSC（フォルトの種別）まで出るので、その行も入れてください

デバッグ用のシリアルヘッダや USB-シリアル変換が使える機械なら、そちらの方が
速いので使ってください。写真は「シリアルが取れないとき」の手段です。

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
ctest --preset arm64   # 直接起動 / UEFI(ACPI) / DT / 入力 / GICv3+SMP / GOP / 緊急画面 / 自立起動 + 文書の検査
ctest --preset x86_64  # UEFI から起動し、ページングと不変条件 24 件まで

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

{
  description = "BTRON3 / B-right/V R4 仕様準拠 OS — 開発環境";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { self, nixpkgs }:
    let
      systems = [
        "aarch64-darwin"
        "x86_64-darwin"
        "aarch64-linux"
        "x86_64-linux"
      ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (s: f nixpkgs.legacyPackages.${s});
    in
    {
      devShells = forAllSystems (
        pkgs:
        let
          llvm = pkgs.llvmPackages_21;

          # ベアメタル用のクロスツールチェーン。
          #
          # GCC のクロスツールチェーンを 2 本用意する代わりに LLVM を使う。
          # clang は単体でマルチターゲットなので `--target=` の指定だけで
          # x86_64 と aarch64 の両方を出力でき、darwin 上で GCC をソースビルド
          # する必要がない。D1 で ELF に決めたのでリンカは lld をそのまま使える。
          #
          # clang-unwrapped を使うのは、nixpkgs のラッパが注入するホスト向け
          # (darwin / Mach-O) のフラグが freestanding なクロスビルドを壊すため。
          cc = llvm.clang-unwrapped;

          # docs/design-issues.md §3.1 / D1 で決めたターゲット
          targetX64 = "x86_64-unknown-none-elf";
          targetArm64 = "aarch64-unknown-none-elf";

          # UEFI ファームウェア (D8)。
          #
          # nixpkgs の OVMF はホストの CPU ファミリ向けを作る。したがって
          # aarch64 ホストでは ArmVirtQemu (FV/QEMU_EFI.fd)、x86_64 ホストでは
          # OVMF (FV/OVMF.fd) がネイティブに得られる。
          #
          # 非ネイティブ側は edk2 のクロスビルドになり重い (643MB + 8 derivation)
          # ので devShell には入れず、packages.ovmf-* として opt-in にする。
          inherit (pkgs.stdenv.hostPlatform) isAarch64 isx86_64;
          ovmfAarch64 = if isAarch64 then pkgs.OVMF.fd else pkgs.pkgsCross.aarch64-multiplatform.OVMF.fd;
          ovmfX64 = if isx86_64 then pkgs.OVMF.fd else pkgs.pkgsCross.gnu64.OVMF.fd;
          # ホストと同じ CPU ファミリのものはネイティブに作れる。
          #
          # **非ネイティブ側は QEMU 同梱の edk2 を使う。** nixpkgs の qemu は
          # share/qemu/edk2-{x86_64,aarch64}-code.fd を持っているので、
          # edk2 のクロスビルド (643MB + 8 derivation) を踏まずに
          # 反対のアーキを回せる。**分割ファームウェアなので CODE と VARS の
          # 2 枚組で、run_qemu.py が pflash で渡す**（-bios では読めない）。
          qemuFw = "${pkgs.qemu}/share/qemu";
          nativeOvmf =
            (if isAarch64 then
              { ARM64_UEFI_FD = "${ovmfAarch64}/FV/QEMU_EFI.fd"; }
             else
              { ARM64_UEFI_FD = "${qemuFw}/edk2-aarch64-code.fd";
                ARM64_UEFI_VARS = "${qemuFw}/edk2-arm-vars.fd"; })
            // (if isx86_64 then
              { X64_UEFI_FD = "${ovmfX64}/FV/OVMF.fd"; }
             else
              { X64_UEFI_FD = "${qemuFw}/edk2-x86_64-code.fd";
                X64_UEFI_VARS = "${qemuFw}/edk2-i386-vars.fd"; });
        in
        {
          default = pkgs.mkShell {
            name = "btron-os";

            packages = with pkgs; [
              # --- ツールチェーン (D1: ELF / J1: C,C++ / H1: クロス開発) ---
              cc # clang 本体
              lld # ld.lld
              llvm.llvm # llvm-objdump / llvm-readelf / llvm-objcopy / llvm-nm / llvm-size
              llvm.lldb # QEMU の gdbstub に接続するデバッガ

              # --- ビルド ---
              cmake
              ninja
              gnumake
              pkg-config

              # --- 実行・検証 (D8: UEFI + QEMU) ---
              qemu

              # --- ブートイメージ作成 (D8: GPT + ESP) ---
              mtools # FAT の ESP を組む
              dosfstools # mkfs.vfat
              xorriso # 起動可能 ISO
              gptfdisk # GPT 操作

              # --- 仕様の資産化 (Phase 0) ---
              python3 # 変換・抽出スクリプト
              imagemagick # 図版 GIF -> PNG 変換（sips の代わり。再現性のため）

              # --- その他 ---
              git
              ripgrep
            ];

            env = nativeOvmf // {
              BTRON_CC = "${cc}/bin/clang";
              BTRON_LD = "${pkgs.lld}/bin/ld.lld";
              BTRON_TARGET_X64 = targetX64;
              BTRON_TARGET_ARM64 = targetArm64;

              # グリフデータ (B8 / 詳細 B8)。
              #
              # **リポジトリには入れない。ビルド時に取得する。**
              # ただし素朴に curl で取ると再現性が壊れる（上流が消える・
              # 内容が変わる）ので、nixpkgs 経由にして flake.lock で
              # ハッシュまで固定する。ネットワークに出るのは nix develop の
              # ときだけで、ビルド自体は決定的になる。
              #
              # 指すのは**上流のソース tarball**。パッケージの出力には
              # .otf と .pcf.gz しか無く、こちらには .hex がある。
              # .hex は "コード:ビットマップ" のテキストなので扱いが楽で、
              # しかも**日本語字形 (izmg16) と中国語字形 (wqy) が別ファイル**に
              # なっているので、日本語を優先して合成できる。
              #
              # ライセンス: GPL-2.0-or-later WITH Font-exception-2.0。
              # フォント例外により、このフォントで描いた出力物に GPL が
              # 伝染しない。
              UNIFONT_SRC = "${pkgs.unifont.src}";
              UNIFONT_VERSION = pkgs.unifont.version;
            };

            shellHook = ''
              echo "BTRON3 準拠 OS — 開発環境"
              echo "  host     : ${pkgs.stdenv.hostPlatform.system}"
              echo "  clang    : $(${cc}/bin/clang --version | head -1)"
              echo "  lld      : $(${pkgs.lld}/bin/ld.lld --version | head -1)"
              echo "  qemu     : $(${pkgs.qemu}/bin/qemu-system-x86_64 --version | head -1)"
              echo "  targets  : $BTRON_TARGET_X64 / $BTRON_TARGET_ARM64"
              echo "  unifont  : $UNIFONT_VERSION (B8。ビルド時に .hex から生成)"
              ${
                if isAarch64 then
                  ''echo "  UEFI fw  : arm64 = $ARM64_UEFI_FD"''
                else if isx86_64 then
                  ''echo "  UEFI fw  : x64   = $X64_UEFI_FD"''
                else
                  ''echo "  UEFI fw  : (なし)"''
              }
              echo "             非ネイティブ側は  nix build .#ovmf-x64 / .#ovmf-aarch64"
              echo
              echo "  ベアメタルのコンパイル例:"
              echo "    \$BTRON_CC --target=\$BTRON_TARGET_ARM64 -ffreestanding -nostdlib -c f.c"
              echo
            '';
          };
        }
      );

      # UEFI ファームウェア。ネイティブ側は devShell に入っているので、
      # これは主に非ネイティブ側を明示的に取得するための出口。
      packages = forAllSystems (
        pkgs:
        let
          inherit (pkgs.stdenv.hostPlatform) isAarch64 isx86_64;
        in
        {
          ovmf-aarch64 = if isAarch64 then pkgs.OVMF.fd else pkgs.pkgsCross.aarch64-multiplatform.OVMF.fd;
          ovmf-x64 = if isx86_64 then pkgs.OVMF.fd else pkgs.pkgsCross.gnu64.OVMF.fd;
        }
      );

      formatter = forAllSystems (pkgs: pkgs.nixfmt-tree);
    };
}

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
          nativeOvmf =
            if isAarch64 then
              { ARM64_UEFI_FD = "${ovmfAarch64}/FV/QEMU_EFI.fd"; }
            else if isx86_64 then
              { X64_UEFI_FD = "${ovmfX64}/FV/OVMF.fd"; }
            else
              { };
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
            };

            shellHook = ''
              echo "BTRON3 準拠 OS — 開発環境"
              echo "  host     : ${pkgs.stdenv.hostPlatform.system}"
              echo "  clang    : $(${cc}/bin/clang --version | head -1)"
              echo "  lld      : $(${pkgs.lld}/bin/ld.lld --version | head -1)"
              echo "  qemu     : $(${pkgs.qemu}/bin/qemu-system-x86_64 --version | head -1)"
              echo "  targets  : $BTRON_TARGET_X64 / $BTRON_TARGET_ARM64"
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

# arm64 (AArch64) ベアメタル
#
# Phase 1 の先行ターゲット。開発機が arm64 Mac なので QEMU が hvf で
# 準ネイティブに動き、UEFI ファームウェアも pkgs.OVMF.fd から得られる。
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(BTRON_TARGET_TRIPLE aarch64-unknown-none-elf)
set(BTRON_ARCH arm64)
include("${CMAKE_CURRENT_LIST_DIR}/btron-toolchain.cmake")

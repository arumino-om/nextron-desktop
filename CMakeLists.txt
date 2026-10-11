cmake_minimum_required(VERSION 3.25)

#
# BTRON3 / B-right/V R4 仕様準拠 OS
#
# 必ずツールチェーンファイルを指定して構成する。CMakePresets.json を使うのが楽。
#
#   nix develop
#   cmake --preset arm64 && cmake --build --preset arm64
#   cmake --preset x86_64 && cmake --build --preset x86_64
#
# 設計上の決定は docs/design-issues.md を参照。
#

project(btron-os
  VERSION 0.0.1
  DESCRIPTION "BTRON3 仕様準拠 OS"
  LANGUAGES C CXX ASM)

# ツールチェーンファイルは project() の時点で読み込まれる。
# それ以前にこの検査を置くと BTRON_TARGET_TRIPLE はまだ未定義になる。
if(NOT DEFINED BTRON_TARGET_TRIPLE)
  message(FATAL_ERROR
    "ツールチェーンファイルが指定されていません。\n"
    "  cmake --preset arm64\n"
    "または\n"
    "  cmake -B build/arm64 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm64.cmake")
endif()

# --------------------------------------------------------------------------
# 言語設定 (J1)
#
# C / C++ 混在。公開 API は C ABI (extern "C") で統一する (規律 R12)。
# カーネル内 C++ は例外と RTTI を使わない。
# --------------------------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# --------------------------------------------------------------------------
# 共通のコンパイルオプション
# --------------------------------------------------------------------------
add_library(btron_flags INTERFACE)

target_compile_options(btron_flags INTERFACE
  # ベアメタル
  -ffreestanding
  -fno-stack-protector
  -fno-common

  # 仕様書は B / VB を plain char で定義し、符号付きと注記している。
  # plain char の符号は処理系依存で arm64 の既定は unsigned なので強制する。
  # include/btron/typedef.h の _Static_assert が外れを検出する。
  -fsigned-char

  # 規律 R5: W(32bit) と D(64bit) の暗黙変換を許さない。
  # 縮小変換は明示キャストのみ。切り捨てが静かに起きるのを防ぐ。
  -Wall
  -Wextra
  -Wconversion
  -Wsign-conversion
  -Werror

  # 仕様書のヘッダに合わせる
  -Wno-unused-parameter

  $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
)

target_link_options(btron_flags INTERFACE -nostdlib -fuse-ld=lld)

# --------------------------------------------------------------------------
# 公開ヘッダ
# --------------------------------------------------------------------------
add_library(btron_headers INTERFACE)
target_include_directories(btron_headers INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include")
# ホストのヘッダを拾わない。clang 組込みの stdint.h 等だけを使う。
target_compile_options(btron_headers INTERFACE -nostdinc)

# --------------------------------------------------------------------------
# テスト基盤 (H8)
# --------------------------------------------------------------------------
include(CTest)

add_library(btron_test STATIC src/lib/std/test.c)
target_link_libraries(btron_test PUBLIC btron_flags btron_headers)

# --------------------------------------------------------------------------
# カーネル (Phase 1)
#
# いまのところ arm64 のみ。起動してシリアルに出し、実行時に設計上の
# 不変条件を検査して終了する。
# --------------------------------------------------------------------------
if(BTRON_ARCH STREQUAL "arm64")
  add_executable(btron-kernel
    src/boot/arm64/start.S
    src/boot/arm64/boot.c
    src/kernel/hal/arm64/uart.c
    src/kernel/hal/arm64/page.c
    src/kernel/hal/arm64/exit.c
    src/kernel/hal/arm64/mmu.c
    src/kernel/hal/arm64/vectors.S
    src/kernel/hal/arm64/exception.c
    src/kernel/mm/pmem.c)

  target_link_libraries(btron-kernel PRIVATE btron_flags btron_headers btron_test)
  set_target_properties(btron-kernel PROPERTIES OUTPUT_NAME "btron-arm64" SUFFIX ".elf")

  set(_ld "${CMAKE_CURRENT_SOURCE_DIR}/src/boot/arm64/kernel.ld")
  target_link_options(btron-kernel PRIVATE
    -Wl,-T,${_ld} -Wl,--build-id=none -static -nostartfiles)
  set_target_properties(btron-kernel PROPERTIES LINK_DEPENDS "${_ld}")

  add_test(NAME boot-arm64
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu.py"
            --arch arm64 --timeout 30
            --expect "BTRON3 準拠 OS"
            --deny   "[ FAIL ]"
            "$<TARGET_FILE:btron-kernel>")
endif()

# --------------------------------------------------------------------------
# x86_64 の HAL
#
# ブート経路（UEFI / multiboot）がまだ無いのでカーネルイメージは作らないが、
# arm64 と同一のインタフェースで実装してあり、同じ厳格フラグで型検査にかける。
# ブート経路ができたら add_executable に差し替える。
# --------------------------------------------------------------------------
if(BTRON_ARCH STREQUAL "x86_64")
  add_library(btron_hal_x86_64 STATIC
    src/kernel/hal/x86_64/uart.c
    src/kernel/hal/x86_64/page.c
    src/kernel/hal/x86_64/exit.c
    src/kernel/hal/x86_64/mmu.c
    src/kernel/mm/pmem.c)
  target_link_libraries(btron_hal_x86_64 PRIVATE btron_flags btron_headers)
endif()

# --------------------------------------------------------------------------
# ヘッダの自己検査
#
# include/btron/*.h の _Static_assert が成立することを、両ターゲットで
# ビルドのたびに確かめる。決定事項 (A1/A2/A3/A5/A8/A4/H2) が崩れたら
# ここで落ちる。
# --------------------------------------------------------------------------
add_library(btron_header_check STATIC src/header_check.c)
target_link_libraries(btron_header_check PRIVATE btron_flags btron_headers)

message(STATUS "BTRON OS  target : ${BTRON_TARGET_TRIPLE}  (${BTRON_ARCH})")
message(STATUS "BTRON OS  cc     : ${CMAKE_C_COMPILER}")

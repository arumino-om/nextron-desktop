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

# --------------------------------------------------------------------------
# 共通のコンパイルオプション
# --------------------------------------------------------------------------
add_library(btron_flags INTERFACE)

target_compile_options(btron_flags INTERFACE
  # ベアメタル
  -ffreestanding
  -fno-stack-protector
  -fno-common

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

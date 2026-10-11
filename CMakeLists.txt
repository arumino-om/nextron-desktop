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
# 内蔵グリフデータ (B8 / 詳細 B8)
#
# GNU Unifont をビルド時に取得する。**リポジトリには入れない。**
# tools/mkfont.py が $UNIFONT_SRC（flake.nix が公開する上流ソース tarball）の
# .hex から C ソースを生成する。日本語字形 (izmg16) を中国語字形 (wqy) より
# 優先して合成している。
#
# 生成物は build/ の下なので .gitignore 済み。
# --------------------------------------------------------------------------
set(BTRON_FONT_DATA "${CMAKE_CURRENT_BINARY_DIR}/font_data.c")

add_custom_command(
  OUTPUT "${BTRON_FONT_DATA}"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/mkfont.py"
          --out "${BTRON_FONT_DATA}"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/mkfont.py"
  COMMENT "グリフデータを生成 (GNU Unifont)"
  VERBATIM)

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
    src/kernel/hal/arm64/timer.c
    src/kernel/hal/arm64/cpu.c
    src/kernel/hal/arm64/gic.c
    src/kernel/hal/arm64/irq.c
    src/kernel/hal/arm64/context.c
    src/kernel/hal/arm64/context.S
    src/kernel/core/sched.c
    src/kernel/core/task.c
    src/kernel/core/tasksync.c
    src/kernel/core/sem.c
    src/kernel/core/flag.c
    src/kernel/core/mtx.c
    src/kernel/core/mbx.c
    src/kernel/core/mbf.c
    src/kernel/core/mempool.c
    src/kernel/core/cyclic.c
    src/kernel/periph/taskcomm.c
    src/kernel/periph/taskobj.c
    src/kernel/periph/proc.c
    src/kernel/periph/memory.c
    src/kernel/periph/message.c
    src/kernel/periph/gname.c
    src/kernel/periph/event.c
    src/kernel/periph/device.c
    src/drivers/serial/serdev.c
    src/drivers/ramdisk/ramdisk.c
    src/kernel/periph/clock.c
    src/kernel/periph/system.c
    src/kernel/core/time.c
    src/kernel/core/intc.c
    src/kernel/core/selftest.c
    src/kernel/boot/bootinfo.c
    src/kernel/boot/machdesc.c
    src/kernel/boot/fdt.c
    src/kernel/boot/acpi.c
    src/kernel/klib/string.c
    src/kernel/screen/fb.c
    src/kernel/screen/font.c
    src/kernel/screen/console.c
    src/kernel/screen/panic.c
    "${BTRON_FONT_DATA}"
    src/kernel/mm/pmem.c
    src/kernel/mm/page.c
    src/kernel/mm/kheap.c
    src/subsystems/mgr/manager.c
    src/subsystems/font/fontman.c
    src/subsystems/window/winman.c
    src/subsystems/window/demo.c
    src/lib/appl/font/fontcli.c
    src/lib/dp/pixel.c
    src/lib/dp/env.c
    src/lib/dp/draw.c
    src/lib/dp/bitmap.c
    src/lib/dp/text.c)

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
            --expect "QEMU -kernel"
            --expect "中心核は起動しない"
            --deny   "[ FAIL ]"
            "$<TARGET_FILE:btron-kernel>")
endif()

# --------------------------------------------------------------------------
# UEFI ブートローダ + 起動可能ディスクイメージ (D8)
#
#   ブートローダ  UEFI (x86_64 / arm64 共通)
#   区画          GPT
#   ハードウェア列挙  ACPI (x86_64) / Device Tree (arm64)
#   フレームバッファ  UEFI GOP
#
# ローダは PE/COFF なのでトリプルが違う。cmake/uefi-loader.cmake が
# clang と lld-link を直接叩いて作る。
# --------------------------------------------------------------------------
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/uefi-loader.cmake")

if(TARGET btron-kernel)
  set(BTRON_DISK_IMAGE "${CMAKE_CURRENT_BINARY_DIR}/btron-${BTRON_ARCH}.img")

  add_custom_command(
    OUTPUT "${BTRON_DISK_IMAGE}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/mkimage.py"
            --arch ${BTRON_ARCH}
            --loader "${BTRON_UEFI_IMAGE}"
            --kernel "$<TARGET_FILE:btron-kernel>"
            --out    "${BTRON_DISK_IMAGE}"
    DEPENDS "${BTRON_UEFI_IMAGE}" btron-kernel
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/mkimage.py"
    COMMENT "起動可能ディスクイメージ (GPT + ESP)"
    VERBATIM)

  add_custom_target(btron-image ALL DEPENDS "${BTRON_DISK_IMAGE}")

  # 緊急画面 (T4-02) の検査用イメージ。
  #
  # **カーネルもローダも同じバイナリ。** ESP に印のファイルを 1 つ足すだけで、
  # ローダがそれを見て BootInfo.flags を立て、起動時検査のあとに
  # わざと例外を起こす経路になる。フォントを含むカーネルの再ビルドを
  # 避けられるのが要点。
  set(BTRON_PANIC_IMAGE "${CMAKE_CURRENT_BINARY_DIR}/btron-${BTRON_ARCH}-panic.img")

  add_custom_command(
    OUTPUT "${BTRON_PANIC_IMAGE}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/mkimage.py"
            --arch ${BTRON_ARCH} --panic-test
            --loader "${BTRON_UEFI_IMAGE}"
            --kernel "$<TARGET_FILE:btron-kernel>"
            --out    "${BTRON_PANIC_IMAGE}"
    DEPENDS "${BTRON_UEFI_IMAGE}" btron-kernel
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/mkimage.py"
    COMMENT "緊急画面の検査用ディスクイメージ"
    VERBATIM)

  add_custom_target(btron-panic-image ALL DEPENDS "${BTRON_PANIC_IMAGE}")

  # 手で画面を見るためのイメージ。ESP に HOLD の印を置くだけで、
  # カーネルもローダも同じものを使う（Phase 1 ㉙）。
  #
  #   python3 tools/run_qemu.py --arch arm64 --uefi --gui \
  #           build/arm64/btron-arm64-gui.img
  set(BTRON_GUI_IMAGE "${CMAKE_CURRENT_BINARY_DIR}/btron-${BTRON_ARCH}-gui.img")

  add_custom_command(
    OUTPUT "${BTRON_GUI_IMAGE}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/mkimage.py"
            --arch ${BTRON_ARCH} --hold
            --loader "${BTRON_UEFI_IMAGE}"
            --kernel "$<TARGET_FILE:btron-kernel>"
            --out    "${BTRON_GUI_IMAGE}"
    DEPENDS "${BTRON_UEFI_IMAGE}" btron-kernel
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/mkimage.py"
    COMMENT "画面を手で見るためのディスクイメージ"
    VERBATIM)

  add_custom_target(btron-gui-image ALL DEPENDS "${BTRON_GUI_IMAGE}")

  # UEFI 経由の起動。直接起動と違い、メモリマップ・ACPI/DT・GOP が来る。
  #
  # QEMU + ArmVirtQemu は既定で ACPI を渡してくる。D8 が arm64 に想定して
  # いる Device Tree 経路と、GOP のフレームバッファ経路も別々に確かめる。
  add_test(NAME boot-uefi-${BTRON_ARCH}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu.py"
            --arch ${BTRON_ARCH} --uefi --timeout 90
            --expect "BTRON3 準拠 OS — UEFI ローダ"
            --expect "起動経路  : UEFI"
            --expect "ハード記述: ACPI"
            --deny   "[ FAIL ]"
            --deny   "ローダ異常終了"
            "${BTRON_DISK_IMAGE}")

  if(BTRON_ARCH STREQUAL "arm64")
    # Device Tree 経路。ACPI を切ると ArmVirtQemu は DTB を置く。
    # 同じ機種を 2 つの記述形式で読んで同じ結果になることが、
    # 2 つのパーサの相互検証になっている。
    add_test(NAME boot-uefi-dt-arm64
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu.py"
              --arch arm64 --uefi --timeout 90 --machine "virt,acpi=off"
              --expect "起動経路  : UEFI"
              --expect "ACPI RSDP : なし"
              --expect "ハード記述: Device Tree"
              --expect "GICv2 dist 0x0000000008000000 cpu 0x0000000008010000"
              --expect "EL1 物理 = 30"
              --expect "PSCI (HVC)"
              --deny   "DTB       : なし"
              --deny   "[ FAIL ]"
              --deny   "ローダ異常終了"
              "${BTRON_DISK_IMAGE}")

    # GICv2 の値（上）と同じ内容を ACPI 側でも確かめる。
    add_test(NAME boot-uefi-acpi-gic-arm64
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu.py"
              --arch arm64 --uefi --timeout 90
              --expect "ハード記述: ACPI"
              --expect "GICv2 dist 0x0000000008000000 cpu 0x0000000008010000"
              --expect "EL1 物理 = 30"
              --expect "PSCI (HVC)"
              --deny   "[ FAIL ]"
              "${BTRON_DISK_IMAGE}")

    # GICv3 と複数 CPU (D3 の前提)。DT と ACPI の両方で同じ結果を求める。
    foreach(_m "virt,gic-version=3" "virt,gic-version=3,acpi=off")
      string(REPLACE "," "-" _tag "${_m}")
      add_test(NAME boot-uefi-smp-${_tag}
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu.py"
                --arch arm64 --uefi --timeout 90 --machine "${_m}"
                --extra=-smp --extra=4
                --expect "GICv3 dist 0x0000000008000000 cpu 0x00000000080a0000"
                --expect "CPU       : 4 個"
                --expect "EL1 物理 = 30"
                --deny   "[ FAIL ]"
                --deny   "ローダ異常終了"
                "${BTRON_DISK_IMAGE}")
    endforeach()
  endif()

  # 緊急画面 (T4-02)。**わざと例外を起こして**、画面に出ることを確かめる。
  #
  # 落ちるのが正常なので --expect-exit で終了コードを指定する。
  # ここを普通の検査にすると「落ちなかったこと」しか確かめられない。
  add_test(NAME panic-screen-${BTRON_ARCH}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu.py"
            --arch ${BTRON_ARCH} --uefi --timeout 90
            --extra=-device --extra=ramfb
            --expect-exit 2
            --expect "印        : PANICTEST"
            --expect "[ DONE ]"
            --expect "わざと例外を起こします"
            --expect "*** 例外 ***"
            --expect "データアボート"
            # **これが緊急画面が本当に出たことの証拠。**
            # screen_panic_begin() が成功したときだけシリアルに出る。
            # 画面には出さない（利用者に見せるのは何が起きたかだけ）。
            #
            # 見出しまで含めて確かめる。自己検査も緊急画面を 1 度開くので、
            # 見出し無しだと本物の例外の経路が壊れても通ってしまう。
            --expect "緊急画面へ切り替えました: *** 例外 ***"
            --deny   "[ FAIL ]"
            "${BTRON_PANIC_IMAGE}")

  # ramfb はリニアなフレームバッファを見せる。virtio-gpu は Blt のみで
  # ExitBootServices 後に使えないので、こちらで確かめる。
  add_test(NAME boot-uefi-fb-${BTRON_ARCH}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu.py"
            --arch ${BTRON_ARCH} --uefi --timeout 90
            --extra=-device --extra=ramfb
            --expect "起動経路  : UEFI"
            --expect "pitch"
            --expect "画面コンソール:"
            --deny   "画面      : なし"
            --deny   "[ FAIL ]"
            --deny   "ローダ異常終了"
            "${BTRON_DISK_IMAGE}")
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
    src/kernel/hal/x86_64/timer.c
    src/kernel/boot/bootinfo.c
    src/kernel/boot/machdesc.c
    src/kernel/boot/fdt.c
    src/kernel/boot/acpi.c
    src/kernel/klib/string.c
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

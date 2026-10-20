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
if(BTRON_ARCH STREQUAL "x86_64")
  # カーネルと同じ条件で作る（下のカーネル本体に理由を書いた）。
  target_compile_options(btron_test PRIVATE -fpie -mno-red-zone)
endif()

# タスク索引の鮮度 (W0-4)。
#
# docs/tasks/INDEX.md は生成物なので、タスクを足したまま作り直すのを
# 忘れると古くなる。QEMU を使わないので一瞬で終わる。
add_test(NAME docs-task-index
  COMMAND "${Python3_EXECUTABLE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/tools/tasks_index.py" --check)

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

# ソースの列挙はディレクトリごとの sources.cmake にある（W0-2 / 規律 R29）。
# ソースを 1 本足す作業がこのファイルを触らずに済むようにするため。
foreach(_frag
    src/kernel/core/sources.cmake
    src/kernel/core/tests/sources.cmake
    src/kernel/periph/sources.cmake
    src/kernel/boot/sources.cmake
    src/kernel/mm/sources.cmake
    src/kernel/screen/sources.cmake
    src/kernel/klib/sources.cmake
    src/kernel/fs/sources.cmake
    src/drivers/sources.cmake
    src/subsystems/sources.cmake
    src/lib/dp/sources.cmake
    src/lib/appl/sources.cmake
    src/lib/sys/sources.cmake)
  include("${CMAKE_CURRENT_SOURCE_DIR}/${_frag}")
endforeach()

# アーキ依存の分。**中心核から上はアーキ中立**なので、ここだけが分かれる。
include("${CMAKE_CURRENT_SOURCE_DIR}/src/kernel/hal/${BTRON_ARCH}/sources.cmake" OPTIONAL)
include("${CMAKE_CURRENT_SOURCE_DIR}/src/boot/${BTRON_ARCH}/sources.cmake" OPTIONAL)


# --------------------------------------------------------------------------
# ユーザプログラム (EL0)
#
# **カーネルとは別に結合する。** 特権が違うので一緒にできない。
# src/lib/sys/ のスタブ（システムコールを svc に直す）と crt0 を足して
# BTRON_PROC_TEXT_BASE に固定で結合し、tools/mkobj.py が実行プログラム
# レコードの形式に直す。
#
# できた像はカーネルにも C 配列として埋め込む。検査が「実身に書いて
# cre_prc する」ところまでを本物の経路で通せるようにするため。
# --------------------------------------------------------------------------
set(BTRON_USER_OBJ "${CMAKE_CURRENT_BINARY_DIR}/user_hello.c")
set(BTRON_SHELL_OBJ "${CMAKE_CURRENT_BINARY_DIR}/user_shell.c")
set(BTRON_WM_OBJ "${CMAKE_CURRENT_BINARY_DIR}/user_winman.c")

# ユーザプログラムを 1 つ組み立てる。
#   name    実行ファイル名
#   srcs    固有のソース
#   libs    追加で結合するソース（ディスプレイプリミティブなど）
function(btron_add_user name blobname out_c)
  cmake_parse_arguments(U "" "" "SRCS" ${ARGN})
  add_executable(${name}
    ${BTRON_SRCS_SYS}
    ${U_SRCS})
  target_link_libraries(${name} PRIVATE btron_flags btron_headers)
  target_link_options(${name} PRIVATE
    -nostdlib -static
    "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/src/user/user.ld"
    -Wl,--build-id=none)
  set_target_properties(${name} PROPERTIES
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/user/user.ld")
  add_custom_command(
    OUTPUT "${out_c}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/mkobj.py"
            "$<TARGET_FILE:${name}>"
            --arch ${BTRON_ARCH}
            --name ${blobname}
            --out "${CMAKE_CURRENT_BINARY_DIR}/${name}.obj"
            --c-array "${out_c}"
    DEPENDS ${name} "${CMAKE_CURRENT_SOURCE_DIR}/tools/mkobj.py"
    COMMENT "実行プログラムレコードを生成 (${name})"
    VERBATIM)
endfunction()

# **ユーザプログラムは両アーキで結合する** (L5 の 4 段目)。
# crt0 とシステムコールのスタブ (src/lib/sys/) が x86_64 にも対応したので、
# 「空の像を置いてリンクだけ通す」逃げは要らなくなった。
btron_add_user(btron-user-hello btron_user_hello "${BTRON_USER_OBJ}"
    SRCS src/user/hello.c)

  # 外殻の最小形。ディスプレイプリミティブとフォントの利用側を
  # **ユーザプログラムに結合する**。描画はシステムコールではない。
  btron_add_user(btron-user-shell btron_user_shell "${BTRON_SHELL_OBJ}"
    SRCS src/user/shell.c
         ${BTRON_SRCS_SYS_SCREEN}
         ${BTRON_SRCS_DP}
         ${BTRON_SRCS_APPL})

  # **ウィンドウマネージャ本体。** 詳細 E22「1 プロセス 1 サーバ」。
  # カーネルには入らない。利用側 (wincli.c) だけがカーネルに残る。
  btron_add_user(btron-user-winman btron_user_winman "${BTRON_WM_OBJ}"
    SRCS src/user/winman_main.c
         ${BTRON_SRCS_WINMAN}
         ${BTRON_SRCS_SYS_SCREEN}
         ${BTRON_SRCS_DP}
         ${BTRON_SRCS_APPL})

# --------------------------------------------------------------------------
# カーネル (Phase 1)
#
# いまのところ arm64 のみ。起動してシリアルに出し、実行時に設計上の
# 不変条件を検査して終了する。
# --------------------------------------------------------------------------
# カーネル本体（アーキ非依存の分）。
set(BTRON_SRCS_KERNEL_COMMON
  ${BTRON_SRCS_KERNEL_CORE}
  ${BTRON_SRCS_TESTS}
  ${BTRON_SRCS_KERNEL_PERIPH}
  ${BTRON_SRCS_KERNEL_BOOT}
  ${BTRON_SRCS_KERNEL_MM}
  ${BTRON_SRCS_KERNEL_SCREEN}
  ${BTRON_SRCS_KERNEL_KLIB}
  ${BTRON_SRCS_KERNEL_FS}
  ${BTRON_SRCS_DRIVERS}
  ${BTRON_SRCS_SUBSYS}
  ${BTRON_SRCS_APPL}
  ${BTRON_SRCS_APPL_WINDOW}
  ${BTRON_SRCS_DP})

set(BTRON_SRCS_USER "${BTRON_USER_OBJ}" "${BTRON_SHELL_OBJ}" "${BTRON_WM_OBJ}")

if(DEFINED BTRON_SRCS_START)
  add_executable(btron-kernel
    ${BTRON_SRCS_START}
    ${BTRON_SRCS_HAL}
    ${BTRON_SRCS_KERNEL_COMMON}
    ${BTRON_SRCS_USER}
    "${BTRON_FONT_DATA}")

  if(BTRON_ARCH STREQUAL "x86_64")
    # **位置独立に作る (-fpie)。理由が 2 つある。**
    #
    # ① §3.2-3 の BTRON_KERNEL_BASE は 0xFFFFFF00_00000000 で、x86_64 の
    #    -mcmodel=kernel が前提とする「上位 2GB (0xFFFFFFFF80000000 以上)」に
    #    入らない。既定の small モデルはデータ参照を 32bit の符号付き絶対値で
    #    組むので、リンク時に "R_X86_64_32S out of range" で落ちる
    # ② large モデルなら届くが、**起動第 1 段が動かなくなる**。large は
    #    関数呼び出しも movabs + 間接 call にするので、物理アドレスで
    #    走っている第 1 段が上位半分の番地へ飛んでしまう（CR3 を
    #    差し替える前なので当然ページフォルトになる。実際に踏んだ）
    #
    # -fpie は参照を RIP 相対にするので、**同じ像が物理でも上位半分でも
    # 動く**。arm64 が adrp/add で自然にそうなっているのと同じ性質を、
    # x86_64 でも明示的に買う。リンクは位置固定 (-no-pie) のままで、
    # 再配置はリンク時に解決される。
    #
    # -mno-red-zone も要る。割込みハンドラが同じスタックを使うので、
    # 関数が rsp の下 128 バイトを勝手に使う前提は成り立たない。
    target_compile_options(btron-kernel PRIVATE -fpie -mno-red-zone)
    # リンクは位置固定。アドレスはリンカスクリプトが決めるので、
    # ドライバへ -no-pie を渡す必要はない（渡すと未使用警告になる）。
  endif()

  target_link_libraries(btron-kernel PRIVATE btron_flags btron_headers btron_test)
  set_target_properties(btron-kernel PROPERTIES
    OUTPUT_NAME "btron-${BTRON_ARCH}" SUFFIX ".elf")

  set(_ld "${BTRON_KERNEL_LD}")
  target_link_options(btron-kernel PRIVATE
    -Wl,-T,${_ld} -Wl,--build-id=none -static -nostartfiles)
  set_target_properties(btron-kernel PROPERTIES LINK_DEPENDS "${_ld}")

  # 直接起動は arm64 だけ。x86_64 で QEMU の -kernel に ELF を渡すと
  # Linux boot protocol を期待されるので、この経路は作らない。
  if(BTRON_ARCH STREQUAL "arm64")
    add_test(NAME boot-arm64
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu.py"
              --arch arm64 --timeout 30
              --expect "BTRON3 準拠 OS"
              --expect "QEMU -kernel"
              --expect "中心核は起動しない"
              --deny   "[ FAIL ]"
              "$<TARGET_FILE:btron-kernel>")
  endif()
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

  # シリアルが無い機械のふりをさせるイメージ（規律 R31 の逆テスト）。
  #
  # 実機のノート PC には 16550 が無いことがあり（詳細 F9）、そのとき
  # 既定アドレスへ書き込むと外部アボートで無言に止まる。**QEMU の
  # virt も q35 も記述からシリアルを消せない**ので、ESP に印を置いて
  # カーネル側で記述から捨てる形にした（PANICTEST と同じ仕掛け）。
  set(BTRON_NOUART_IMAGE "${CMAKE_CURRENT_BINARY_DIR}/btron-${BTRON_ARCH}-nouart.img")

  add_custom_command(
    OUTPUT "${BTRON_NOUART_IMAGE}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/mkimage.py"
            --arch ${BTRON_ARCH} --no-uart
            --loader "${BTRON_UEFI_IMAGE}"
            --kernel "$<TARGET_FILE:btron-kernel>"
            --out    "${BTRON_NOUART_IMAGE}"
    DEPENDS "${BTRON_UEFI_IMAGE}" btron-kernel
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/mkimage.py"
    COMMENT "シリアルが無い機械のふりをさせるディスクイメージ"
    VERBATIM)

  add_custom_target(btron-nouart-image ALL DEPENDS "${BTRON_NOUART_IMAGE}")

  if(BTRON_ARCH STREQUAL "x86_64")
    # **x86_64 の起動経路 (T1-11)。** ここで見るのは
    #
    #   - UEFI から ELF が読まれ、CR3 を自前の表に差し替えて上位半分へ移る
    #   - ACPI の MADT から Local APIC と IOAPIC が取れる
    #   - TSC を PIT で校正して周波数が出る（CPUID 0x15 が無い機種のため）
    #   - 起動時の不変条件検査（アーキ非依存の 24 件）が全部通る
    #
    # 中心核はまだ接続していないので、そこで止まることも検査する。
    # **嘘をつかずに止まる**ことが条件（スタブで動くふりをしない）。
    add_test(NAME boot-uefi-x86_64
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu.py"
              --arch x86_64 --uefi --timeout 90
              --expect "BTRON3 準拠 OS — UEFI ローダ"
              --expect "BTRON3 準拠 OS — x86_64"
              --expect "起動経路  : UEFI"
              --expect "上位半分へ移行しました"
              --expect "ハード記述: ACPI"
              --expect "割込み    : APIC"
              # **arm64 と同じ検査が同じ数だけ通ること**（L5 完了）。
              # T1-11 の受入条件「arm64 と同じテスト名が出ること」を
              # 外側からも要求する。ring 3 まで降りる区分（実身からの
              # プロセス起動・外殻）も含めて 184 件。
              --expect "中心核の初期化 (D10 / T1-01 / T1-02)"
              --expect "--- 中心核 タスク管理とスケジューラ ---"
              --expect "--- 周辺核 ランデブ (§1.3) ---"
              --expect "[EL0] ユーザプログラムが動いています"
              --expect "[外殻] ユーザ特権で起動しました"
              --expect "[WM] ユーザ特権で起動しました"
              --expect "[ DONE ] 184/184"
              --deny   "[ FAIL ]"
              --deny   "*** 例外 ***"
              "${BTRON_DISK_IMAGE}")
  endif()

  # 実機で死なない経路 (T6-02) — その 1: シリアルが無い機械。
  #
  # **これは逆テストである。** 既定アドレスへの書き込みが 1 つでも
  # 残っていれば、この構成で外部アボートになって落ちる（規律 R31）。
  #
  # シリアルが黙るので**カーネルの出力は 1 行も出ない**。だから
  # --expect では確かめられず、判定は
  #
  #   ・ローダの行（印が読まれたこと）が出ていること
  #   ・カーネルの行が出て**いない**こと = 本当に黙っていること
  #   ・終了コード 0 = 画面側の検査まで全部通ったこと
  #
  # の 3 つで行う。「画面に出ていること」はカーネル自身が画素を
  # 読み返して確かめている（earlytest.c の R31 の検査。㉗ と同じ考え方）。
  add_test(NAME no-uart-${BTRON_ARCH}
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu.py"
            --arch ${BTRON_ARCH} --uefi --timeout 120
            --extra=-device --extra=ramfb
            --expect "印        : NOUART"
            --deny   "ハード記述:"
            --deny   "上位半分へ移行しました"
            --deny   "*** 例外 ***"
            --deny   "[ FAIL ]"
            --deny   "ローダ異常終了"
            "${BTRON_NOUART_IMAGE}")

  # ここから下は arm64 の検査。x86_64 は中心核より上がまだ繋がって
  # いないので（T1-11 の残り）、同じ期待値では測れない。
  if(BTRON_ARCH STREQUAL "arm64")

  # 自立起動 (D8 / startup.c) の検査用イメージ。
  #
  # ESP に \BTRON\SELFBOOT の印を置く。カーネルはこの印があると
  # 起動時検査の代わりに**本来の起動列**を通す — ドライバを登録し、
  # BTRON 区画を探して接続し（未構築なら構築し）、ディスク上の実行
  # プログラムから外殻を起こす。**カーネルもローダも同じバイナリ**で、
  # 印だけが違う（PANICTEST / HOLD と同じ仕掛け）。
  set(BTRON_SELFBOOT_IMAGE
      "${CMAKE_CURRENT_BINARY_DIR}/btron-${BTRON_ARCH}-selfboot.img")

  add_custom_command(
    OUTPUT "${BTRON_SELFBOOT_IMAGE}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/mkimage.py"
            --arch ${BTRON_ARCH} --selfboot
            --loader "${BTRON_UEFI_IMAGE}"
            --kernel "$<TARGET_FILE:btron-kernel>"
            --out    "${BTRON_SELFBOOT_IMAGE}"
    DEPENDS "${BTRON_UEFI_IMAGE}" btron-kernel
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/mkimage.py"
    COMMENT "自立起動の検査用ディスクイメージ"
    VERBATIM)

  add_custom_target(btron-selfboot-image ALL DEPENDS "${BTRON_SELFBOOT_IMAGE}")

  # **永続性は 1 回の起動では確かめられない。** 同じイメージの写しで
  # 2 度起動し、1 回目に構築、2 回目に「前回の内容が残っている」ことを見る。
  add_test(NAME selfboot-${BTRON_ARCH}
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_selfboot.py"
            --arch ${BTRON_ARCH} --timeout 120
            "${BTRON_SELFBOOT_IMAGE}")

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
            # ユーザ特権 (EL0) の経路。**ここが通らなければ保護が無い。**
            --expect "[EL0] ユーザプログラムが動いています"
            --expect "[EL0] 起動メッセージ: BTRON"
            --expect "[EL0] カーネル番地は断られました"
            # システムコールを通さずカーネル番地を読んだプロセスが
            # 落とされること（カーネルは生き残る）。
            --expect "ユーザプログラムの異常"
            # 外殻はユーザ特権で起動する。**画面を借りられるかは構成次第**
            # なので、ここでは起動だけを見る（画面付きの構成は下で見る）。
            --expect "[外殻] ユーザ特権で起動しました"
            --deny   "読めてしまいました"
            --deny   "*** 画素が合いません ***"
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
              # PCIe の装置数は**この土台の QEMU の起動引数で決まる** ——
              # T3-03 で -drive if=virtio（virtio-blk-pci になる）をやめて
              # -device virtio-blk-device（mmio 版）にしたので、PCI バスに
              # 居るのはホストブリッジと virtio-net の 2 個である。
              # PCIe の ECAM 窓 (詳細 F8)。**この行は下の ACPI 構成と
              # 一字一句同じ**で、それが 2 つのパーサの相互検証になる
              # （DT は `reg` から大きさを直接、ACPI は MCFG のバス範囲から
              # 導くので、出どころが違うのに一致しなければならない）。
              --expect "PCIe ECAM : seg 0 bus 0-255 @ 0x0000004010000000 size 0x0000000010000000"
              --expect "PCIe 装置 : 2 個"
              --expect "class 0x06/0x00/0x00"
              --deny   "PCIe ECAM : なし"
              --deny   "DTB       : なし"
              --deny   "[ FAIL ]"
              --deny   "ローダ異常終了"
              "${BTRON_DISK_IMAGE}")

    # 入力装置 (virtio-input)。**これが無いと入力の経路は検査されない。**
    #
    # 既定の構成（ACPI）では virtio-mmio の枠が DSDT の AML の中にあって
    # 読めないので、枠が 0 個になり「装置が無い」で通ってしまう。
    # 規律 R27 の穴そのものなので、専用の構成を立てて、
    # **本物の装置が見つかって動いたこと**を要求する。
    add_test(NAME input-arm64
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu.py"
              --arch arm64 --uefi --timeout 120 --input
              --extra=-device --extra=ramfb
              --expect "ハード記述: Device Tree"
              --expect "virtio    : 32 枠"
              # 見つけた台数と動かせた台数が一致していること。
              # 旧式インタフェースで断ると「2 台」と「0 台」に割れる。
              --expect "入力枠    : 2 台"
              --expect "入力装置  : 2 台"
              --expect "キーボード"
              --expect "PD(絶対)"
              # **外殻がユーザ特権で画面を借りて描けること**（詳細 E22 / E1）。
              # 画面のある構成でしか確かめられない。
              --expect "[外殻] 画面を借りました"
              --expect "[外殻] 画素を読み返して確かめました"
              # **ウィンドウマネージャ自身がユーザ特権で動いていること。**
              --expect "[WM] ユーザ特権で起動しました"
              --deny   "[ FAIL ]"
              --deny   "ローダ異常終了"
              "${BTRON_DISK_IMAGE}")

    # GICv2 の値（上）と同じ内容を ACPI 側でも確かめる。
    # **PCIe の ECAM も同じ**（MCFG から来る。詳細 F8）。
    add_test(NAME boot-uefi-acpi-gic-arm64
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu.py"
              --arch arm64 --uefi --timeout 90
              --expect "ハード記述: ACPI"
              --expect "GICv2 dist 0x0000000008000000 cpu 0x0000000008010000"
              --expect "EL1 物理 = 30"
              --expect "PSCI (HVC)"
              --expect "PCIe ECAM : seg 0 bus 0-255 @ 0x0000004010000000 size 0x0000000010000000"
              --expect "PCIe 装置 : 2 個"
              --expect "class 0x06/0x00/0x00"
              --deny   "PCIe ECAM : なし"
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
  if(BTRON_ARCH STREQUAL "arm64")
    # 実機で死なない経路 (T6-02) — その 2: 終了の手段が無い機械。
    #
    # セミホスティングを外すと `hlt #0xF000` は「分類なし」の同期例外に
    # なる（Phase 2 (56)）。**それを異常として報告せず**、記述から取った
    # 呼び口で PSCI SYSTEM_OFF へ回ることを確かめる。
    #
    # 終了コードは持ち帰れない（PSCI で落ちると常に 0）ので、合否は
    # "[ DONE ] n/n" と --deny で見る。
    add_test(NAME poweroff-psci-arm64
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu.py"
              --arch arm64 --uefi --timeout 120 --no-semihosting
              --expect "[ DONE ]"
              --expect "電源を落とします (PSCI SYSTEM_OFF)"
              --deny   "*** 例外 ***"
              --deny   "[ FAIL ]"
              --deny   "ローダ異常終了"
              "${BTRON_DISK_IMAGE}")
  endif()

  # ------------------------------------------------------------------
  # 実機規模 (T6-03 / 詳細 F9「実機で初めて効く差」)
  #
  # QEMU の既定 (512MB / 1 CPU / 800x600) では当たっているが、実機では
  # 外れるものを、作れる範囲で実際に作って回す。
  # ------------------------------------------------------------------

  # RAM 32GB。**ページの表と直接マップの大きさがここで変わる。**
  # 4KB 粒度で直接マップを張ると表が 64MB になり、早期プール (16MB) に
  # 入らず「hal_vm_init に失敗」で止まる。ブロック記述子で張っている
  # ことがこの構成でしか確かめられない。
  #
  # 画面も付ける —— 画面が無いと塗り潰しの検査が丸ごと飛ぶ（Phase 2 ㊿）。
  add_test(NAME boot-uefi-bigmem-arm64
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu.py"
            --arch arm64 --uefi --timeout 180 --memory 32G
            --extra=-device --extra=ramfb
            --expect "起動経路  : UEFI"
            # 32GB 分が実際に管理下に入っていること。
            --expect "ページ) 管理下"
            --expect "画面コンソール:"
            --deny   "hal_vm_init に失敗"
            --deny   "空き物理メモリが見つからない"
            --deny   "[ FAIL ]"
            --deny   "ローダ異常終了"
            "${BTRON_DISK_IMAGE}")

  # CPU 16 個。記述から読んだ数がそのまま出ること。
  # **数を切り捨てない**ことは MD_MAX_CPUS (64) を超えないと効かないので、
  # そちらは合成した MADT で検査している（src/kernel/core/tests/mm.c）。
  add_test(NAME boot-uefi-smp16-arm64
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/run_qemu.py"
            --arch arm64 --uefi --timeout 120
            --extra=-smp --extra=16
            --expect "CPU       : 16 個"
            --deny   "[ FAIL ]"
            --deny   "ローダ異常終了"
            "${BTRON_DISK_IMAGE}")

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
  endif() # arm64 の検査群
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

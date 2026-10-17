# ベアメタル・クロスコンパイル用の共通ツールチェーン定義。
#
# 各アーキ用の toolchain-*.cmake から BTRON_TARGET_TRIPLE と
# CMAKE_SYSTEM_PROCESSOR を設定したうえで include する。
#
# clang は単体でマルチターゲットなので --target の指定だけで両アーキを
# 出力できる。GCC のクロスツールチェーンを 2 本用意する必要がない (H1)。

set(CMAKE_SYSTEM_NAME Generic)

# freestanding なので実行可能ファイルのリンクを伴う try_compile は失敗する。
# 静的ライブラリで検査させる。
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# nix develop が BTRON_CC / BTRON_LD を公開している。
# 外で動かす場合は PATH 上の clang を使う。
if(DEFINED ENV{BTRON_CC})
  set(CMAKE_C_COMPILER "$ENV{BTRON_CC}")
else()
  set(CMAKE_C_COMPILER clang)
endif()
set(CMAKE_CXX_COMPILER "${CMAKE_C_COMPILER}")
set(CMAKE_ASM_COMPILER "${CMAKE_C_COMPILER}")

set(CMAKE_C_COMPILER_TARGET ${BTRON_TARGET_TRIPLE})
set(CMAKE_CXX_COMPILER_TARGET ${BTRON_TARGET_TRIPLE})
set(CMAKE_ASM_COMPILER_TARGET ${BTRON_TARGET_TRIPLE})

# **リンクのときだけ別のトリプルを名乗る（x86_64 / darwin ホストの回避）。**
#
# clang の bare-metal ドライバは aarch64 では ld.lld を直接呼ぶが、
# `x86_64-*-none-elf` では Generic_GCC へ落ちて**リンクを `gcc` 経由で行う**。
# ホストが macOS だとその `gcc` は Apple clang なので、Mach-O 用の引数
# (`-arch arm64` / `-platform_version` / `-syslibroot`) が混ざり、
#
#     ld64.lld: error: unknown argument '-T'          （-fuse-ld=lld のとき）
#     ld.lld:   error: unknown argument '-arch'       （--ld-path のとき）
#
# のどちらかで落ちる。コンパイル (`-c`) は none-elf で問題なく、**リンクの
# 経路選択だけ**の問題なので、リンク時に `linux-gnu` を名乗って ELF の
# ld.lld を引く。あとから渡した `--target` が勝つ。`-nostdlib`
# `-nostartfiles` を付けているので Linux の crt も libc も引かない。
#
# **ホストが Linux なら元から通る**（Generic_GCC が GNU ld / lld を呼ぶ）。
# この 1 行は macOS で x86_64 を触れるようにするためのものである。
if(BTRON_ARCH STREQUAL "x86_64")
  set(CMAKE_EXE_LINKER_FLAGS_INIT "--target=x86_64-unknown-linux-gnu")
endif()

# D1: ELF。リンカは lld。
set(CMAKE_C_COMPILER_RANLIB llvm-ranlib)
set(CMAKE_CXX_COMPILER_RANLIB llvm-ranlib)
set(CMAKE_AR llvm-ar)
set(CMAKE_RANLIB llvm-ranlib)
set(CMAKE_OBJCOPY llvm-objcopy)
set(CMAKE_OBJDUMP llvm-objdump)
set(CMAKE_READELF llvm-readelf)
set(CMAKE_NM llvm-nm)
set(CMAKE_STRIP llvm-strip)

# ホスト側のライブラリやヘッダを拾わせない。
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM BEFORE)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

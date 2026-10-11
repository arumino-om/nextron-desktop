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

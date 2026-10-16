# カーネル内の最小ライブラリ
#
# **src/lib/ とは別物である**（詳細 H7 / 規律 R14）。あちらは
# アプリケーションがリンクする LGPL 予定の層で、こちらは GPLv2 の
# カーネル内部。混ぜてはならない。

set(BTRON_SRCS_KERNEL_KLIB
  ${CMAKE_CURRENT_LIST_DIR}/string.c)

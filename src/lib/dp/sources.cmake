# ディスプレイプリミティブ (仕様 第2章。LGPL 予定。J2 / 規律 R14)
#
# **サーバではなくプロセス内ライブラリである**（詳細 E1）。
# カーネルにも EL0 のプログラムにも同じものを結合する。

set(BTRON_SRCS_DP
  ${CMAKE_CURRENT_LIST_DIR}/pixel.c
  ${CMAKE_CURRENT_LIST_DIR}/env.c
  ${CMAKE_CURRENT_LIST_DIR}/draw.c
  ${CMAKE_CURRENT_LIST_DIR}/bitmap.c
  ${CMAKE_CURRENT_LIST_DIR}/text.c
  ${CMAKE_CURRENT_LIST_DIR}/pointer.c)

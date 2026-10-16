# 外殻のサーバ群 (GPLv2。E22「1 プロセス 1 サーバ」)
#
# **ウィンドウマネージャ本体 (winman.c / winsrv.c) はここに無い。**
# EL0 のユーザプログラムとして別に結合する（CMakeLists.txt の
# btron_add_user）。カーネルに残るのは起動係 (winboot.c) と
# 手で見るためのデモ (demo.c) だけ。

set(BTRON_SRCS_SUBSYS
  ${CMAKE_CURRENT_LIST_DIR}/mgr/manager.c
  ${CMAKE_CURRENT_LIST_DIR}/font/fontman.c
  ${CMAKE_CURRENT_LIST_DIR}/window/demo.c
  ${CMAKE_CURRENT_LIST_DIR}/window/winboot.c)

# ウィンドウマネージャ本体（EL0 のプロセスとして結合する分）
set(BTRON_SRCS_WINMAN
  ${CMAKE_CURRENT_LIST_DIR}/window/winman.c
  ${CMAKE_CURRENT_LIST_DIR}/window/winsrv.c)

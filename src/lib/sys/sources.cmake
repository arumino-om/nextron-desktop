# EL0 の実行時支援（システムコールを svc / syscall に直す層）
#
# **カーネルには入らない。** ユーザプログラムだけに結合する。

set(BTRON_SRCS_SYS
  ${CMAKE_CURRENT_LIST_DIR}/crt0.S
  ${CMAKE_CURRENT_LIST_DIR}/syscall.c
  ${CMAKE_CURRENT_LIST_DIR}/rt.c)

# 画面の貸し出しを使う分（外殻側のプログラムに足す）
set(BTRON_SRCS_SYS_SCREEN
  ${CMAKE_CURRENT_LIST_DIR}/screen.c)

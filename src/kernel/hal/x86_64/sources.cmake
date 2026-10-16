# HAL — x86_64 (規律 R2 / R3)
#
# **まだ中心核には接続していない**（T1-11）。起動・ページング・コンソール・
# 例外の報告までが入っていて、APIC / タイマ割込み / コンテキスト切替は
# stub.c が「呼ばれたら止まる」形で置いてある。嘘の成功を返さない。

set(BTRON_SRCS_HAL
  ${CMAKE_CURRENT_LIST_DIR}/uart.c
  ${CMAKE_CURRENT_LIST_DIR}/page.c
  ${CMAKE_CURRENT_LIST_DIR}/exit.c
  ${CMAKE_CURRENT_LIST_DIR}/mmu.c
  ${CMAKE_CURRENT_LIST_DIR}/cpu.c
  ${CMAKE_CURRENT_LIST_DIR}/vectors.S
  ${CMAKE_CURRENT_LIST_DIR}/exception.c
  ${CMAKE_CURRENT_LIST_DIR}/timer.c
  ${CMAKE_CURRENT_LIST_DIR}/stub.c)

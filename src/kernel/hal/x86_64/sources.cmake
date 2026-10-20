# HAL — x86_64 (規律 R2 / R3)
#
# **中心核への接続は途中**（T1-11 / L5）。起動・ページング・コンソール・
# 例外の報告・**Local APIC とタイマ割込み** (apic.c) が入っている。
# **コンテキスト切替** (context.c / context.S) も入った。残るのは
# ring 3 / syscall で、stub.c が「呼ばれたら止まる」形で置いてある。
# 嘘の成功を返さない。

set(BTRON_SRCS_HAL
  ${CMAKE_CURRENT_LIST_DIR}/uart.c
  ${CMAKE_CURRENT_LIST_DIR}/page.c
  ${CMAKE_CURRENT_LIST_DIR}/exit.c
  ${CMAKE_CURRENT_LIST_DIR}/mmu.c
  ${CMAKE_CURRENT_LIST_DIR}/cpu.c
  ${CMAKE_CURRENT_LIST_DIR}/vectors.S
  ${CMAKE_CURRENT_LIST_DIR}/exception.c
  ${CMAKE_CURRENT_LIST_DIR}/timer.c
  ${CMAKE_CURRENT_LIST_DIR}/apic.c
  ${CMAKE_CURRENT_LIST_DIR}/context.c
  ${CMAKE_CURRENT_LIST_DIR}/context.S
  ${CMAKE_CURRENT_LIST_DIR}/stub.c)

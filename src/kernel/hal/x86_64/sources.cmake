# HAL — x86_64 (規律 R2 / R3)
#
# **これで全部そろった** (T1-11 / L5)。起動・ページング・コンソール・
# 例外の報告 ＋ Local APIC とタイマ割込み (apic.c) ＋ コンテキスト切替
# (context.c / context.S) ＋ ring 3 と syscall (syscall.S / cpu.c の TSS)。
#
# 「呼ばれたら止まる」スタブを置いていた stub.c は、置くものが無くなった
# ので消した。嘘の成功を返さないという姿勢はそのまま、実体で満たしている。

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
  ${CMAKE_CURRENT_LIST_DIR}/syscall.S)

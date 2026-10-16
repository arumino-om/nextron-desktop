# 起動スタブ — arm64 (D8)
#
# UEFI ローダから、あるいは QEMU の -kernel から直接入る所。
# リンカスクリプト kernel.ld もここにある。

set(BTRON_SRCS_START
  ${CMAKE_CURRENT_LIST_DIR}/start.S
  ${CMAKE_CURRENT_LIST_DIR}/boot.c)

set(BTRON_KERNEL_LD ${CMAKE_CURRENT_LIST_DIR}/kernel.ld)

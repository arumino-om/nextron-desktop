# 起動スタブ — x86_64 (D8)
#
# **UEFI 経由だけを支える。** QEMU の -kernel で ELF を直接渡す経路は
# x86_64 には無い（QEMU は Linux boot protocol を期待する）。

set(BTRON_SRCS_START
  ${CMAKE_CURRENT_LIST_DIR}/start.S
  ${CMAKE_CURRENT_LIST_DIR}/boot.c)

set(BTRON_KERNEL_LD ${CMAKE_CURRENT_LIST_DIR}/kernel.ld)

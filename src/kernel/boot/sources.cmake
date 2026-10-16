# 引き継ぎ情報とハードウェア記述の読み取り (D8)

set(BTRON_SRCS_KERNEL_BOOT
  ${CMAKE_CURRENT_LIST_DIR}/bootinfo.c
  ${CMAKE_CURRENT_LIST_DIR}/machdesc.c
  ${CMAKE_CURRENT_LIST_DIR}/fdt.c
  ${CMAKE_CURRENT_LIST_DIR}/acpi.c
  ${CMAKE_CURRENT_LIST_DIR}/startup.c
  ${CMAKE_CURRENT_LIST_DIR}/earlytest.c
  ${CMAKE_CURRENT_LIST_DIR}/bootlog.c)

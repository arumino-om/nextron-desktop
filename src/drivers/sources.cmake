# ドライバ
#
# いまはカーネルに結合している。F1 でユーザランドへ出す予定。

set(BTRON_SRCS_DRIVERS
  ${CMAKE_CURRENT_LIST_DIR}/serial/serdev.c
  ${CMAKE_CURRENT_LIST_DIR}/ramdisk/ramdisk.c
  ${CMAKE_CURRENT_LIST_DIR}/virtio/mmio.c
  ${CMAKE_CURRENT_LIST_DIR}/virtio/input.c
  ${CMAKE_CURRENT_LIST_DIR}/virtio/blk.c
  ${CMAKE_CURRENT_LIST_DIR}/pci/ecam.c
  ${CMAKE_CURRENT_LIST_DIR}/pci/msi.c)

# 中心核 (μT-Kernel 3.0 準拠。D10)
#
# **ソースの列挙をディレクトリごとに分けている理由** (W0-1 / W0-2 / 規律 R29):
# 1 つの CMakeLists.txt に全部並べると、ソースを 1 本足す作業が必ず
# そのファイルを触ることになり、並列に進めたときの衝突点になる。
# ここはアーキ非依存。arch 依存は src/kernel/hal/<arch>/sources.cmake。

set(BTRON_SRCS_KERNEL_CORE
  ${CMAKE_CURRENT_LIST_DIR}/sched.c
  ${CMAKE_CURRENT_LIST_DIR}/task.c
  ${CMAKE_CURRENT_LIST_DIR}/tasksync.c
  ${CMAKE_CURRENT_LIST_DIR}/sem.c
  ${CMAKE_CURRENT_LIST_DIR}/flag.c
  ${CMAKE_CURRENT_LIST_DIR}/mtx.c
  ${CMAKE_CURRENT_LIST_DIR}/mbx.c
  ${CMAKE_CURRENT_LIST_DIR}/mbf.c
  ${CMAKE_CURRENT_LIST_DIR}/mempool.c
  ${CMAKE_CURRENT_LIST_DIR}/cyclic.c
  ${CMAKE_CURRENT_LIST_DIR}/time.c
  ${CMAKE_CURRENT_LIST_DIR}/intc.c
  ${CMAKE_CURRENT_LIST_DIR}/syscall.c
  ${CMAKE_CURRENT_LIST_DIR}/selftest.c)

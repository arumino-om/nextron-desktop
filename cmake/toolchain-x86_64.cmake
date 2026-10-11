# x86_64 ベアメタル
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(BTRON_TARGET_TRIPLE x86_64-unknown-none-elf)
set(BTRON_ARCH x86_64)
include("${CMAKE_CURRENT_LIST_DIR}/btron-toolchain.cmake")

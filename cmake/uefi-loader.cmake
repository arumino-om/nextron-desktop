# UEFI ブートローダ (PE/COFF) のビルド
#
# カーネルは ELF (D1) だが、UEFI アプリは PE/COFF でなければならない。
# ターゲットトリプルが違う（*-unknown-windows）ので、CMake の通常の
# ターゲットでは作れない。clang と lld-link を直接叩く。
#
#   呼出規約  arm64 = AAPCS64 / x86_64 = Microsoft x64
#             *-unknown-windows でビルドすると両方とも UEFI と一致する
#   wchar_t   UEFI の CHAR16 に合わせて -fshort-wchar で 16bit にする
#   __chkstk  Windows ターゲットのスタックプローブは呼べないので外す
#
# 依存関係は clang の -MD が出す depfile を Ninja に食わせて追跡する。

if(BTRON_ARCH STREQUAL "arm64")
  set(BTRON_UEFI_TRIPLE aarch64-unknown-windows)
  set(BTRON_UEFI_MACHINE ARM64)
  set(BTRON_UEFI_NAME BOOTAA64.EFI)
  set(BTRON_UEFI_ARCH_SRC src/boot/uefi/arm64.c)
elseif(BTRON_ARCH STREQUAL "x86_64")
  set(BTRON_UEFI_TRIPLE x86_64-unknown-windows)
  set(BTRON_UEFI_MACHINE X64)
  set(BTRON_UEFI_NAME BOOTX64.EFI)
  set(BTRON_UEFI_ARCH_SRC src/boot/uefi/x86_64.c)
else()
  message(FATAL_ERROR "UEFI ローダ: 未対応のアーキテクチャ ${BTRON_ARCH}")
endif()

find_program(BTRON_LLD_LINK lld-link REQUIRED)

set(_uefi_srcs
  src/boot/uefi/main.c
  src/boot/uefi/console.c
  ${BTRON_UEFI_ARCH_SRC})

set(_uefi_flags
  --target=${BTRON_UEFI_TRIPLE}
  -ffreestanding
  -fshort-wchar
  -fno-stack-protector
  -mno-stack-arg-probe      # __chkstk を呼ばせない
  -fno-builtin
  -nostdlib
  -nostdinc
  -fsigned-char             # 規律 R16
  -std=c11
  -Wall -Wextra -Wconversion -Wsign-conversion -Werror
  -Wno-unused-parameter
  -O2
  -g
  -I "${CMAKE_CURRENT_SOURCE_DIR}/include")

set(_uefi_objs "")
foreach(_src IN LISTS _uefi_srcs)
  get_filename_component(_name "${_src}" NAME_WE)
  set(_obj "${CMAKE_CURRENT_BINARY_DIR}/uefi/${_name}.obj")
  add_custom_command(
    OUTPUT "${_obj}"
    COMMAND "${CMAKE_C_COMPILER}" ${_uefi_flags}
            -MD -MF "${_obj}.d"
            -c "${CMAKE_CURRENT_SOURCE_DIR}/${_src}" -o "${_obj}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${_src}"
    DEPFILE "${_obj}.d"
    COMMENT "UEFI ローダ: ${_src}"
    VERBATIM)
  list(APPEND _uefi_objs "${_obj}")
endforeach()

set(BTRON_UEFI_IMAGE "${CMAKE_CURRENT_BINARY_DIR}/${BTRON_UEFI_NAME}")

add_custom_command(
  OUTPUT "${BTRON_UEFI_IMAGE}"
  COMMAND "${BTRON_LLD_LINK}"
          /subsystem:efi_application
          /entry:efi_main
          /machine:${BTRON_UEFI_MACHINE}
          /nodefaultlib
          /dynamicbase        # UEFI は任意のアドレスへロードするので .reloc が必要
          /debug:dwarf
          /out:${BTRON_UEFI_IMAGE}
          ${_uefi_objs}
  DEPENDS ${_uefi_objs}
  COMMENT "UEFI ローダをリンク: ${BTRON_UEFI_NAME}"
  VERBATIM)

add_custom_target(btron-loader ALL DEPENDS "${BTRON_UEFI_IMAGE}")

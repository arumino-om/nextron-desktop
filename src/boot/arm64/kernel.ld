/* arm64 カーネルのリンカスクリプト
 *
 * QEMU virt マシンの RAM は 0x4000_0000 から。
 * 先頭 512KB はファームウェア等が使う可能性があるので 0x4008_0000 に置く。 */

OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(_start)

PHDRS { text PT_LOAD FLAGS(5); rodata PT_LOAD FLAGS(4); data PT_LOAD FLAGS(6); }

SECTIONS
{
    . = 0x40080000;
    __kernel_start = .;

    .text : ALIGN(4096) { KEEP(*(.text.boot)) *(.text .text.*) } :text

    .rodata : ALIGN(4096) { *(.rodata .rodata.*) } :rodata

    .data : ALIGN(4096) { *(.data .data.*) } :data

    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss .bss.*) *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    } :data

    . = ALIGN(4096);
    __boot_stack_bottom = .;
    . += 0x10000;                   /* 64KB の起動スタック */
    __boot_stack_top = .;

    __kernel_end = .;

    /DISCARD/ : { *(.comment) *(.note.*) *(.eh_frame*) }
}

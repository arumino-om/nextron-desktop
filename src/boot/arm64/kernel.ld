/* arm64 カーネルのリンカスクリプト
 *
 * カーネルは上位半分 (BTRON_KERNEL_BASE) にリンクし、そこで実行する。
 * これにより TTBR0（下位半分）をプロセスに明け渡せる (§3.2 / 規律 R3)。
 *
 *   VMA = 0xFFFFFF0000000000 + offset   実行時のアドレス
 *   LMA = 0x40080000         + offset   QEMU がロードする物理アドレス
 *
 * QEMU の -kernel は ELF を p_paddr (= LMA) に置き、e_entry へ飛ぶ。
 * 起動直後は MMU 無効なので、エントリは物理アドレスでなければならない。
 * そこで ENTRY には _start の物理アドレス (_start_phys) を指定する。
 *
 * _start は MMU を有効にするまで adrp/add だけで自分の位置を求める。
 * adrp は PC 相対なので、物理で走っている間は物理アドレスを返す。
 */

OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(_start_phys)

KERNEL_LMA = 0x40080000;           /* QEMU virt の RAM は 0x4000_0000 から */
KERNEL_VMA = 0xFFFFFF0000000000;   /* BTRON_KERNEL_BASE (§3.2-3) */

PHDRS { text PT_LOAD FLAGS(5); rodata PT_LOAD FLAGS(4); data PT_LOAD FLAGS(6); }

SECTIONS
{
    . = KERNEL_VMA;
    __kernel_start = .;

    . = ALIGN(4096);
    .text : AT(ADDR(.text) - KERNEL_VMA + KERNEL_LMA) {
        KEEP(*(.text.boot))
        *(.text .text.*)
    } :text

    . = ALIGN(4096);
    .rodata : AT(ADDR(.rodata) - KERNEL_VMA + KERNEL_LMA) {
        *(.rodata .rodata.*)

        /* C から VMA <-> LMA を換算するための基準。
         *
         * 絶対シンボル (__x = KERNEL_LMA;) にすると、コンパイラがその
         * アドレスを adrp で求めようとして ±4GB の射程を外れる。
         * 値をイメージ内のデータとして置き、CONST UD として読む。 */
        . = ALIGN(8);
        __kernel_vma_base = .;
        QUAD(KERNEL_VMA);
        __kernel_lma_base = .;
        QUAD(KERNEL_LMA);
    } :rodata

    . = ALIGN(4096);
    .data : AT(ADDR(.data) - KERNEL_VMA + KERNEL_LMA) {
        *(.data .data.*)
    } :data

    . = ALIGN(16);
    .bss (NOLOAD) : AT(ADDR(.bss) - KERNEL_VMA + KERNEL_LMA) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    } :data

    . = ALIGN(4096);
    __boot_stack_bottom = .;
    . += 0x10000;                   /* 64KB の起動スタック */
    __boot_stack_top = .;

    . = ALIGN(4096);
    __kernel_end = .;

    /* ELF のエントリは _start の物理アドレス */
    _start_phys = _start - KERNEL_VMA + KERNEL_LMA;

    /DISCARD/ : { *(.comment) *(.note.*) *(.eh_frame*) }
}

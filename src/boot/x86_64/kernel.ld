/* x86_64 カーネルのリンカスクリプト
 *
 * arm64 側と同じ約束で書く（src/boot/arm64/kernel.ld を参照）。
 *
 *   VMA = 0xFFFFFF0000000000 + offset   実行時のアドレス（上位半分）
 *   LMA = 0x00400000         + offset   ローダが置こうとする物理アドレス
 *
 * **arm64 との違いは「ページングを切らない」こと。** ロングモードは
 * ページング必須で、CR0.PG を落とすには 32bit モードへ降りる必要がある。
 * CR3 の差し替えはロングモードのまま合法なので、カーネルは
 * 「UEFI が張った恒等写像の上」で受け取り、自前のページテーブル
 * （恒等写像を含む）を作って CR3 を書く。したがって起動直後も
 * 物理アドレスで走っており、エントリは物理でなければならない。
 *
 * LMA を 4MiB にしているのは、1MiB 以下が互換領域で使えないため。
 * ここが空いていなければローダが別の場所へ置いて差分を足す（規律 R17）。
 */

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start_phys)

KERNEL_LMA = 0x00400000;           /* 4MiB。1MiB 以下は互換領域 */
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

        /* 検査区分の登録（W0-1。src/kernel/core/tests/testsuite.h）。
         * arm64 側と同じ約束。拾い忘れると検査が 0 件で静かに通るので、
         * testrun.c が「1 件も無い」を失敗として報告する。 */
        . = ALIGN(8);
        __btron_tests_start = .;
        KEEP(*(btron_tests))
        __btron_tests_end = .;

        /* C から VMA <-> LMA を換算するための基準。
         * mmu.c が CONST UD として読む（arm64 と同じ形）。 */
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

        /* 起動スタック。.bss の中に置くのは、ローダが確保すべき大きさを
         * ELF だけから決められるようにするため（arm64 と同じ理由）。
         * __bss_end より後ろに置くのは、start.S の .bss クリアが
         * いま使っているスタックを消さないようにするため。 */
        . = ALIGN(4096);
        __boot_stack_bottom = .;
        . += 0x10000;               /* 64KB */
        __boot_stack_top = .;
    } :data

    . = ALIGN(4096);
    __kernel_end = .;

    /* ELF のエントリは _start の物理アドレス */
    _start_phys = _start - KERNEL_VMA + KERNEL_LMA;

    /DISCARD/ : { *(.comment) *(.note.*) *(.eh_frame*) }
}

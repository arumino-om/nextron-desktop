/*
 * start.S — arm64 の起動エントリ
 *
 * QEMU virt に -kernel で読み込ませた場合の入口。
 * 入口の状態: EL1、MMU 無効、キャッシュ無効、x0 = DTB の物理アドレス。
 */

    .section .text.boot, "ax"
    .globl _start
_start:
    /* 起動 CPU 以外は停止させる。SMP (D3) は中心核ができてから。 */
    mrs     x1, mpidr_el1
    and     x1, x1, #0xff
    cbnz    x1, .Lpark

    /* スタックを張る */
    adrp    x1, __boot_stack_top
    add     x1, x1, :lo12:__boot_stack_top
    mov     sp, x1

    /* .bss をゼロクリアする */
    adrp    x1, __bss_start
    add     x1, x1, :lo12:__bss_start
    adrp    x2, __bss_end
    add     x2, x2, :lo12:__bss_end
.Lbss:
    cmp     x1, x2
    b.hs    .Lmain
    str     xzr, [x1], #8
    b       .Lbss

.Lmain:
    /* x0 (DTB アドレス) をそのまま第 1 引数として渡す */
    bl      boot_main

.Lpark:
    wfe
    b       .Lpark

/*
 * start.S — arm64 の起動エントリ
 *
 * QEMU virt に -kernel で読み込ませた場合の入口。
 * 入口の状態: EL1、MMU 無効、キャッシュ無効、x0 = DTB の物理アドレス。
 *
 * カーネルは上位半分 (BTRON_KERNEL_BASE) にリンクされているが、
 * ここに来た時点では物理アドレスで走っている。MMU を有効にするまでは
 * adrp/add だけで自分の位置を求める（PC 相対なので物理アドレスが得られる）。
 * MMU を有効にしてから ldr =sym で絶対アドレス（= VMA）を取り、上位半分へ移る。
 */

    .section .text.boot, "ax"
    .globl _start
_start:
    /* 起動 CPU 以外は停止させる。SMP (D3) は中心核ができてから。 */
    mrs     x1, mpidr_el1
    and     x1, x1, #0xff
    cbnz    x1, .Lpark

    mov     x19, x0                 /* DTB アドレスを退避 */

    /* 物理のスタックを張る。adrp は PC 相対なので物理アドレスになる。 */
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
    b.hs    .Learly
    str     xzr, [x1], #8
    b       .Lbss

.Learly:
    /* 物理アドレスのまま、コンソールとページテーブルを用意し MMU を有効にする。
     * 戻ってきた時点で MMU は有効。恒等写像が残っているのでここも生きている。 */
    mov     x0, x19
    bl      boot_early

    /* ---- 上位半分へ移る ----
     * ここから先は ldr =sym で絶対アドレス（VMA）が使える。 */
    ldr     x1, =__boot_stack_top
    mov     sp, x1

    mov     x0, x19
    ldr     x1, =boot_main
    br      x1

.Lpark:
    wfe
    b       .Lpark

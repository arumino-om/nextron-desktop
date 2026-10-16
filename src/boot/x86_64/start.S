/*
 * start.S — x86_64 の起動エントリ
 *
 * 入口の状態: ロングモード、**ページング有効**（UEFI が張った恒等写像）、
 * 割込み禁止でよい、RDI = BootInfo の物理アドレス
 * （src/boot/uefi/x86_64.c の arch_jump_to_kernel が積む）。
 *
 * arm64 との違いはここだけである。arm64 は MMU 無効で入ってくるので
 * 「MMU を有効にする」のが第 1 段の仕事だが、x86_64 はロングモードが
 * ページング必須なので**既に有効**で、第 1 段の仕事は「自前の
 * ページテーブルへ CR3 を差し替える」ことになる。どちらも
 * 「恒等写像の上で走り、上位半分へ移る」という形は同じ。
 *
 * カーネルは上位半分 (BTRON_KERNEL_BASE) にリンクされているが、ここに
 * 来た時点では物理アドレスで走っている。CR3 を差し替えるまでは
 * lea (%rip) で自分の位置を求める（PC 相対なので物理アドレスが得られる）。
 */

    .section .text.boot, "ax"
    .globl _start
_start:
    cli
    cld

    /* BootInfo の物理アドレスを退避する。呼び出し先で壊れない
     * レジスタ (callee-saved) を使う。 */
    movq    %rdi, %r15

    /* 物理のスタックを張る。lea (%rip) は PC 相対なので物理アドレス。 */
    leaq    __boot_stack_top(%rip), %rsp

    /* .bss をゼロクリアする。*/
    leaq    __bss_start(%rip), %rdi
    leaq    __bss_end(%rip), %rcx
    subq    %rdi, %rcx
    xorl    %eax, %eax
    rep stosb

    /* 第 1 段。コンソールとページテーブルを用意し、CR3 を差し替えて戻る。
     * 恒等写像を自分でも張ってあるので、戻ってこられる。 */
    movq    %r15, %rdi
    call    boot_early

    /* ---- 上位半分へ移る ----
     * ここから先は絶対アドレス（VMA）で動く。movabsq で 64bit の
     * 絶対値を積んでから間接ジャンプする（jmp の相対変位は 32bit しか
     * 持てず、物理から上位半分へは届かない）。 */
    movabsq $__boot_stack_top, %rsp
    movq    %r15, %rdi
    movabsq $boot_main, %rax
    jmpq    *%rax

    /* boot_main は戻らない。万一戻ったら止める。 */
.Lpark:
    hlt
    jmp     .Lpark

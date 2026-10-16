/*
 * vectors.S — 割込み記述子表のスタブ (x86_64)
 *
 * **arm64 との構造差。** arm64 のベクタは 1 枚の表（VBAR_EL1）に
 * 「同期例外 / IRQ / FIQ / SError」の 4 種が 2KB 間隔で並ぶだけで、
 * 原因は ESR_EL1 を読んで分類する。x86_64 は**ベクタ番号ごとに別の
 * ゲート**（256 本）で、しかも一部の例外だけがエラーコードをスタックに
 * 積む。そのため 256 本のスタブを並べ、エラーコードを積まないベクタでは
 * 0 を自分で積んで形を揃え、ベクタ番号を積んでから共通の入口へ飛ばす。
 *
 * **スタブは 16 バイト間隔に揃えてある。** 1 本ずつラベルを付けて表を
 * 作る書き方（`btron_vec_\n` を 256 個）は LLVM の統合アセンブラの
 * altmacro では通らなかったので、**先頭番地 + 16 × ベクタ番号**で
 * 引けるようにした。C 側 (exception.c) はこの約束で IDT を組む。
 *
 * スタックの形（共通入口に入った時点。上が低位）:
 *
 *   [rsp+0]    退避した汎用レジスタ 15 本
 *   [rsp+15*8] ベクタ番号
 *   [rsp+16*8] エラーコード（積まないベクタでは 0）
 *   [rsp+17*8] RIP / CS / RFLAGS / RSP / SS   ← CPU が積む
 */

#define VEC_STRIDE 16

    .section .text, "ax"

.macro PUSH_REGS
    pushq   %rax
    pushq   %rcx
    pushq   %rdx
    pushq   %rbx
    pushq   %rbp
    pushq   %rsi
    pushq   %rdi
    pushq   %r8
    pushq   %r9
    pushq   %r10
    pushq   %r11
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15
.endm

.macro POP_REGS
    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %r11
    popq    %r10
    popq    %r9
    popq    %r8
    popq    %rdi
    popq    %rsi
    popq    %rbp
    popq    %rbx
    popq    %rdx
    popq    %rcx
    popq    %rax
.endm

    .globl btron_vec_common
btron_vec_common:
    PUSH_REGS
    /* System V AMD64 ABI: rdi, rsi, rdx, rcx が第 1〜4 引数。
     * 退避した 15 本の上に [ベクタ][エラー][RIP] が並んでいる。 */
    movq    15*8(%rsp), %rdi
    movq    16*8(%rsp), %rsi
    movq    17*8(%rsp), %rdx
    movq    %rsp, %rcx
    call    btron_exception_entry
    /* 戻ってきたら外部割込みだった。レジスタを戻して復帰する。 */
    POP_REGS
    addq    $16, %rsp           /* ベクタ番号とエラーコードを捨てる */
    iretq

/* ------------------------------------------------------------------ */
/* 256 本のスタブ。16 バイト間隔で並べる                               */
/* ------------------------------------------------------------------ */

    .balign VEC_STRIDE
    .globl btron_vec_base
btron_vec_base:

    .set vecno, 0
    .rept 256
    .balign VEC_STRIDE
    /* エラーコードを CPU が積むベクタ (8,10,11,12,13,14,17,21,29,30) 以外は
     * 0 を自分で積んで、共通入口から見た形を 1 つにする。 */
    .if (vecno != 8) && (vecno != 10) && (vecno != 11) && (vecno != 12) && \
        (vecno != 13) && (vecno != 14) && (vecno != 17) && (vecno != 21) && \
        (vecno != 29) && (vecno != 30)
    pushq   $0
    .endif
    pushq   $vecno
    jmp     btron_vec_common
    .set vecno, vecno + 1
    .endr

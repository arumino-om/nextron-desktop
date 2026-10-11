/*
 * vectors.S — 例外ベクタ (arm64)
 *
 * ベクタが無いとフォルトが無言のハングになり、何も分からない
 * （Phase 1 ③ の教訓）。同期例外は情報を出して終了、IRQ は中心核へ配る。
 *
 * VBAR_EL1 が指す表は 2KB 境界、16 エントリ × 128 バイト。
 *
 * 【IRQ の経路】
 *
 *   1. 呼び出し側保存レジスタと ELR/SPSR を全部スタックに積む
 *   2. intc_dispatch() を呼ぶ。この中でハンドラが走り、必要なら
 *      ディスパッチャがタスクを切り替える（hal_ctx_switch はこの
 *      フレームの上に自分のフレームを積むので、戻ってきたときには
 *      ここのフレームがそのまま残っている）
 *   3. 積んだものを戻して eret
 *
 * 切替が起きると、2 から戻るのは**別のタスクが次に切り替わってきたとき**に
 * なる。そのときスタックはこのタスクのものに戻っているので、3 は正しく動く。
 */

    .macro ENTRY_PAD label
    .align 7                        /* 128 バイト境界 */
    b       \label
    .endm

/* 呼び出し側保存レジスタ (x0-x18) と x29/x30、ELR_EL1 / SPSR_EL1 を積む。
 * 呼び出され側保存レジスタ (x19-x28) は、この先で呼ぶ C 関数が
 * 必要に応じて保存するので積まなくてよい。
 * 16 バイト境界を保つため、全体で 0x110 バイト取る。 */
    .macro SAVE_IRQ_FRAME
    sub     sp, sp, #0x110
    stp     x0,  x1,  [sp, #0x00]
    stp     x2,  x3,  [sp, #0x10]
    stp     x4,  x5,  [sp, #0x20]
    stp     x6,  x7,  [sp, #0x30]
    stp     x8,  x9,  [sp, #0x40]
    stp     x10, x11, [sp, #0x50]
    stp     x12, x13, [sp, #0x60]
    stp     x14, x15, [sp, #0x70]
    stp     x16, x17, [sp, #0x80]
    stp     x18, x29, [sp, #0x90]
    mrs     x0, elr_el1
    mrs     x1, spsr_el1
    stp     x30, x0,  [sp, #0xa0]
    str     x1,       [sp, #0xb0]
    .endm

    .macro RESTORE_IRQ_FRAME
    ldr     x1,       [sp, #0xb0]
    ldp     x30, x0,  [sp, #0xa0]
    msr     elr_el1, x0
    msr     spsr_el1, x1
    ldp     x0,  x1,  [sp, #0x00]
    ldp     x2,  x3,  [sp, #0x10]
    ldp     x4,  x5,  [sp, #0x20]
    ldp     x6,  x7,  [sp, #0x30]
    ldp     x8,  x9,  [sp, #0x40]
    ldp     x10, x11, [sp, #0x50]
    ldp     x12, x13, [sp, #0x60]
    ldp     x14, x15, [sp, #0x70]
    ldp     x16, x17, [sp, #0x80]
    ldp     x18, x29, [sp, #0x90]
    add     sp, sp, #0x110
    .endm

    .section .text, "ax"
    .align 11                       /* 2KB 境界 */
    .globl btron_vectors
btron_vectors:
    /* Current EL with SP0 */
    ENTRY_PAD .Lsync                /* Synchronous */
    ENTRY_PAD .Lirq                 /* IRQ */
    ENTRY_PAD .Lirq                 /* FIQ */
    ENTRY_PAD .Lsync                /* SError */
    /* Current EL with SPx */
    ENTRY_PAD .Lsync
    ENTRY_PAD .Lirq
    ENTRY_PAD .Lirq
    ENTRY_PAD .Lsync
    /* Lower EL using AArch64 */
    ENTRY_PAD .Lsync
    ENTRY_PAD .Lirq
    ENTRY_PAD .Lirq
    ENTRY_PAD .Lsync
    /* Lower EL using AArch32 */
    ENTRY_PAD .Lsync
    ENTRY_PAD .Lirq
    ENTRY_PAD .Lirq
    ENTRY_PAD .Lsync

.Lsync:
    /* 同期例外と SError。握りつぶして情報だけ出す。復帰はしない。 */
    mrs     x0, esr_el1
    mrs     x1, far_el1
    mrs     x2, elr_el1
    b       hal_exception_report

.Lirq:
    SAVE_IRQ_FRAME
    bl      intc_dispatch
    RESTORE_IRQ_FRAME
    eret

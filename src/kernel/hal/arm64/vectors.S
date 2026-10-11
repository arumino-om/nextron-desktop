/*
 * vectors.S — 例外ベクタ (arm64)
 *
 * ベクタが無いとフォルトが無言のハングになり、何も分からない。
 * いまは全ての例外を 1 つのハンドラに集めて情報を出し、終了する。
 * 割込み管理（中心核）ができたら IRQ を本来の経路に分ける。
 *
 * VBAR_EL1 が指す表は 2KB 境界、16 エントリ × 128 バイト。
 */

    .macro ENTRY_PAD label
    .align 7                        /* 128 バイト境界 */
    b       \label
    .endm

    .section .text, "ax"
    .align 11                       /* 2KB 境界 */
    .globl btron_vectors
btron_vectors:
    /* Current EL with SP0 */
    ENTRY_PAD .Lcommon              /* Synchronous */
    ENTRY_PAD .Lcommon              /* IRQ */
    ENTRY_PAD .Lcommon              /* FIQ */
    ENTRY_PAD .Lcommon              /* SError */
    /* Current EL with SPx */
    ENTRY_PAD .Lcommon
    ENTRY_PAD .Lcommon
    ENTRY_PAD .Lcommon
    ENTRY_PAD .Lcommon
    /* Lower EL using AArch64 */
    ENTRY_PAD .Lcommon
    ENTRY_PAD .Lcommon
    ENTRY_PAD .Lcommon
    ENTRY_PAD .Lcommon
    /* Lower EL using AArch32 */
    ENTRY_PAD .Lcommon
    ENTRY_PAD .Lcommon
    ENTRY_PAD .Lcommon
    ENTRY_PAD .Lcommon

.Lcommon:
    /* 握りつぶして情報だけ出す。復帰はしない。 */
    mrs     x0, esr_el1
    mrs     x1, far_el1
    mrs     x2, elr_el1
    b       hal_exception_report

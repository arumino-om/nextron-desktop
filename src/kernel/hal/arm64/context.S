/*
 * context.S — タスクコンテキストの切替 (arm64)
 *
 * 保存するのは AAPCS64 の呼び出され側保存レジスタと SP だけでよい。
 * この関数は普通の関数として呼ばれるので、呼び出し側保存レジスタ
 * (x0-x18, v0-v7, v16-v31) は呼び出し元が既に面倒を見ている。
 *
 * 保存フレーム（SP から上向き、全体 0xA0 = 160 バイト）:
 *
 *   0x00  x19 x20      0x50  x29 x30
 *   0x10  x21 x22      0x60  d8  d9
 *   0x20  x23 x24      0x70  d10 d11
 *   0x30  x25 x26      0x80  d12 d13
 *   0x40  x27 x28      0x90  d14 d15
 *
 * d8-d15 も呼び出され側保存である。カーネルは明示的に浮動小数点を
 * 使わないが、clang が構造体のコピーに SIMD を使うことがあるので保存する。
 *
 * このレイアウトは hal_ctx_init() (context.c) と一致させること。
 * 片方だけ変えると、最初の切替で意味不明な場所へ飛ぶ。
 */

    .section .text, "ax"

/* void hal_ctx_switch(UD *save_sp, UD new_sp) */
    .globl hal_ctx_switch
    .type hal_ctx_switch, @function
hal_ctx_switch:
    sub     sp, sp, #0xa0
    stp     x19, x20, [sp, #0x00]
    stp     x21, x22, [sp, #0x10]
    stp     x23, x24, [sp, #0x20]
    stp     x25, x26, [sp, #0x30]
    stp     x27, x28, [sp, #0x40]
    stp     x29, x30, [sp, #0x50]
    stp     d8,  d9,  [sp, #0x60]
    stp     d10, d11, [sp, #0x70]
    stp     d12, d13, [sp, #0x80]
    stp     d14, d15, [sp, #0x90]

    mov     x2, sp
    str     x2, [x0]                /* *save_sp = sp */
    mov     sp, x1                  /* sp = new_sp */

    ldp     x19, x20, [sp, #0x00]
    ldp     x21, x22, [sp, #0x10]
    ldp     x23, x24, [sp, #0x20]
    ldp     x25, x26, [sp, #0x30]
    ldp     x27, x28, [sp, #0x40]
    ldp     x29, x30, [sp, #0x50]
    ldp     d8,  d9,  [sp, #0x60]
    ldp     d10, d11, [sp, #0x70]
    ldp     d12, d13, [sp, #0x80]
    ldp     d14, d15, [sp, #0x90]
    add     sp, sp, #0xa0
    ret

/*
 * 新しいタスクが初めて走るときの着地点。
 *
 * hal_ctx_init() が x19 = 起動アドレス、x20 = 起動コード、x21 = 拡張情報、
 * x30 = ここ、として仕込んでいる。上の ldp x29,x30 + ret でここへ来る。
 *
 * タスク本体から return した場合は tk_ext_tsk() で終了させる。仕様は
 * 「タスクの終了は tk_ext_tsk を呼ぶこと」としているが、return したときに
 * 無言で暴走させるより明示的に終了させた方が安全。
 */
    .globl btron_task_trampoline
    .type btron_task_trampoline, @function
btron_task_trampoline:
    /* ディスパッチャは割込み禁止で切り替えてくる。既存のタスクは自分の
     * 退避点で呼び出し元が戻してくれるが、初回だけはここで許可する。 */
    msr     daifclr, #2
    mov     x0, x20                 /* stacd */
    mov     x1, x21                 /* exinf */
    blr     x19
    bl      tk_ext_tsk
    /* tk_ext_tsk は戻らない。万一戻ったら止める。 */
.Lhang:
    wfe
    b       .Lhang

/*
 * メッセージハンドラの差し込み先 (周辺核 §1.2.3)。
 *
 * hal_ctx_inject() が、待ちや実行可能状態で止まっているタスクの退避
 * フレームの**下に**もう 1 枚フレームを積み、x19 = ハンドラ、
 * x20/x21 = 引数、x22 = 元の SP、x30 = ここ、として仕込む。
 *
 * ディスパッチャがそのタスクを選ぶと、hal_ctx_switch の ldp x29,x30 + ret で
 * ここへ来る。ハンドラを呼び、戻ってきたら**元のフレームへ SP を戻して
 * 同じ復元列をもう 1 度回し**、割り込んだ位置へ帰す。
 *
 * つまり「割込みから戻る」のと同じ形を、タスクのスタックの上で作っている。
 * 仕様の「メッセージハンドラは、メインタスクの現在の処理に割り込む形で
 * 実行される」「割り込まれた位置からそのメインタスクの実行が再開される」が
 * これで成立する。
 *
 * レイアウトは上の hal_ctx_switch と一致させること。
 */
    .globl btron_msg_trampoline
    .type btron_msg_trampoline, @function
btron_msg_trampoline:
    /* ディスパッチャは割込み禁止で切り替えてくる。ハンドラは
     * 「通常のプロセスコードの一部として動作し、実行できるシステムコール等の
     * 制限はない」（仕様）ので、割込みを許可してから呼ぶ。 */
    msr     daifclr, #2
    mov     x0, x20                 /* 送信元 PID */
    mov     x1, x21                 /* MESSAGE * */
    blr     x19

    /* 割り込んだ位置へ戻す。ディスパッチャの約束（割込み禁止で戻る）に
     * 合わせてから SP を差し替える。 */
    msr     daifset, #2
    mov     sp, x22
    ldp     x19, x20, [sp, #0x00]
    ldp     x21, x22, [sp, #0x10]
    ldp     x23, x24, [sp, #0x20]
    ldp     x25, x26, [sp, #0x30]
    ldp     x27, x28, [sp, #0x40]
    ldp     x29, x30, [sp, #0x50]
    ldp     d8,  d9,  [sp, #0x60]
    ldp     d10, d11, [sp, #0x70]
    ldp     d12, d13, [sp, #0x80]
    ldp     d14, d15, [sp, #0x90]
    add     sp, sp, #0xa0
    ret

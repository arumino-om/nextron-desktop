/*
 * context.S — タスクコンテキストの切替 (x86_64)
 *
 * 保存するのは System V AMD64 ABI の呼び出され側保存レジスタと RSP だけ。
 * この関数は普通の関数として呼ばれるので、呼び出し側保存レジスタ
 * (rax rcx rdx rsi rdi r8-r11) は呼び出し元が面倒を見ている。
 *
 * 保存フレーム（RSP から上向き、全体 56 バイト）:
 *
 *   0x00  r15
 *   0x08  r14
 *   0x10  r13
 *   0x18  r12
 *   0x20  rbx
 *   0x28  rbp
 *   0x30  戻り番地        ← ret がここへ飛ぶ
 *
 * **arm64 との構造差。**
 *
 *   arm64   フレームの大きさを自分で決めて stp/ldp で出し入れし、x30
 *           （リンクレジスタ）に戻り先が入る
 *   x86_64  戻り先は**スタックに積まれている**（call が積む）。だから
 *           push/pop の列がそのままフレームになり、最後の ret が
 *           「どこへ戻るか」をスタックから取る
 *
 * これは hal_ctx_init() (context.c) と一致させること。片方だけ変えると
 * 最初の切替で意味不明な場所へ飛ぶ。
 *
 * 【整列】ABI は `call` の直前に RSP が 16 の倍数であることを要求する。
 * フレームが 56 バイト（8 の奇数倍）なので、**フレームの底が 8 mod 16 の
 * ところに来るように context.c が置く**。すると ret で着地した時点の RSP が
 * 16 の倍数になり、トランポリンがそのまま call できる。
 */

    .section .text, "ax"

/* void hal_ctx_switch(UD *save_sp, UD new_sp)
 *   rdi = save_sp, rsi = new_sp */
    .globl hal_ctx_switch
    .type hal_ctx_switch, @function
hal_ctx_switch:
    pushq   %rbp
    pushq   %rbx
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15

    movq    %rsp, (%rdi)            /* *save_sp = rsp */
    movq    %rsi, %rsp              /* rsp = new_sp */

    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %rbx
    popq    %rbp
    ret

/*
 * 新しいタスクが初めて走るときの着地点。
 *
 * hal_ctx_init() が rbx = 起動アドレス、r12 = 起動コード、r13 = 拡張情報、
 * 戻り番地 = ここ、として仕込んでいる。上の pop 列 + ret でここへ来る。
 *
 * タスク本体から return した場合は tk_ext_tsk() で終了させる。仕様は
 * 「タスクの終了は tk_ext_tsk を呼ぶこと」としているが、return したときに
 * 無言で暴走させるより明示的に終了させた方が安全（arm64 側と同じ）。
 */
    .globl btron_task_trampoline
    .type btron_task_trampoline, @function
btron_task_trampoline:
    /* ディスパッチャは割込み禁止で切り替えてくる。既存のタスクは自分の
     * 退避点で呼び出し元が戻してくれるが、初回だけはここで許可する。 */
    sti
    movq    %r12, %rdi              /* stacd */
    movq    %r13, %rsi              /* exinf */
    xorl    %ebp, %ebp              /* フレームポインタの鎖を切る */
    call    *%rbx
    call    tk_ext_tsk
    /* tk_ext_tsk は戻らない。万一戻ったら止める。 */
.Lhang:
    cli
    hlt
    jmp     .Lhang

/*
 * メッセージハンドラの差し込み先 (周辺核 §1.2.3)。
 *
 * hal_ctx_inject() が、止まっているタスクの退避フレームの**下に**もう 1 枚
 * フレームを積み、rbx = ハンドラ、r12/r13 = 引数、r14 = 元の RSP、
 * 戻り番地 = ここ、として仕込む。
 *
 * ディスパッチャがそのタスクを選ぶと pop 列 + ret でここへ来る。
 * ハンドラを呼び、戻ってきたら**元のフレームへ RSP を戻して同じ復元列を
 * もう 1 度回し**、割り込んだ位置へ帰す。つまり「割込みから戻る」のと
 * 同じ形をタスクのスタックの上で作っている。
 *
 * レイアウトは上の hal_ctx_switch と一致させること。
 */
    .globl btron_msg_trampoline
    .type btron_msg_trampoline, @function
btron_msg_trampoline:
    /* ハンドラは「通常のプロセスコードの一部として動作し、実行できる
     * システムコール等の制限はない」（仕様）ので、割込みを許可して呼ぶ。 */
    sti
    movq    %r12, %rdi              /* 送信元 PID */
    movq    %r13, %rsi              /* MESSAGE * */
    movq    %r14, %rbp              /* 元の RSP を退避（rbp は下で潰す） */
    xorl    %r15d, %r15d
    call    *%rbx

    /* 割り込んだ位置へ戻す。ディスパッチャの約束（割込み禁止で戻る）に
     * 合わせてから RSP を差し替える。 */
    cli
    movq    %rbp, %rsp              /* 元のフレームへ */
    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %rbx
    popq    %rbp
    ret

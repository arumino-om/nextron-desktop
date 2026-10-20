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

    /* **%gs をカーネル側に据え直す。** 入れ替えの釣り合いに頼れない
     * 理由は cpu.c の hal_gs_to_kernel に書いた。call が積む戻り番地は
     * これから pop するフレームの**下**なので、フレームは壊れない。 */
    call    hal_gs_to_kernel

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
 * ユーザ特権 (ring 3) で走り始めるタスクの着地点。
 *
 * hal_ctx_init_user() が rbx = ユーザのエントリ、r12 = ユーザスタックの
 * 先頭、r13 = 第 1 引数、として仕込んでいる。
 *
 * **ここで ring 0 から ring 3 へ降りる。** arm64 は ERET 1 命令で
 * （ELR_EL1 / SPSR_EL1 / SP_EL0 を書いてから）降りるが、x86 は
 * **iretq に積むフレームを自分で作る** —— SS, RSP, RFLAGS, CS, RIP の
 * 5 本を順に積んで iretq する。sysretq でも降りられるが、RFLAGS の
 * 指定が r11 任せになり最初の 1 回には向かない。
 *
 * 降りる前に**このタスクのカーネルスタックの先頭を覚えさせる**
 * （hal_ctx_set_kernel_sp）。以後このタスクは
 *
 *   ring 3 で走る → syscall か割込み → TSS.RSP0 / %gs:0 のスタックで
 *   処理 → iretq / sysretq で ring 3 へ戻る
 *
 * を繰り返す。arm64 で SP_EL1 がタスクごとに残るのと同じ状態を、
 * x86 では TSS と per-CPU 領域で作っている。
 */
    .globl btron_user_trampoline
    .type btron_user_trampoline, @function
btron_user_trampoline:
    /* いまの RSP = このタスクのカーネルスタックの使い始め。 */
    movq    %rsp, %rdi
    call    hal_ctx_set_kernel_sp

    movq    %r13, %rdi              /* ユーザ側の第 1 引数 */
    xorl    %esi, %esi
    xorl    %edx, %edx
    xorl    %ecx, %ecx
    xorl    %ebp, %ebp

    pushq   $0x1B                   /* SS  = SEL_DATA3 | RPL 3 */
    pushq   %r12                    /* RSP = ユーザスタック */
    pushq   $0x202                  /* RFLAGS: IF=1、bit1 は常に 1 */
    pushq   $0x23                   /* CS  = SEL_CODE3 | RPL 3 */
    pushq   %rbx                    /* RIP = ユーザのエントリ */
    /* **ここを間違えると ring 0 のまま走り続けて保護が効かない。**
     * CS の RPL が 3 でなければ降りていない（arm64 の SPSR_EL1 = 0 と
     * 同じ意味を持つ場所）。 */
    xorl    %ebx, %ebx
    xorl    %r12d, %r12d
    xorl    %r13d, %r13d
    /* **ここも「カーネルから ring 3 へ出る」経路である。**
     * 割込みの出口 (vectors.S) と syscall の出口 (syscall.S) と同じく
     * %gs をユーザ側へ戻す。忘れると ring 3 がカーネル側の %gs を持った
     * まま走り、そのタスクの最初の syscall が 0 + 8 番地へ書く
     * （cpu.c の hal_gs_to_kernel に経緯）。 */
    swapgs
    iretq

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

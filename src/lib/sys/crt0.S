/*
 * crt0.S — ユーザプログラムの入口
 *
 * カーネル（btron_user_trampoline）は第 1 引数レジスタ（arm64 は x0、
 * x86_64 は rdi）にプロセス起動メッセージへのポインタを入れて
 * ユーザ特権へ降ろしてくる。仕様 §1.1.6 の形式-A
 *
 *   W MAIN (MESSAGE *msg)
 *
 * をそのまま呼ぶ。
 *
 * 仕様「MAIN() および main() からのリターンはプロセスの終了となり、
 * ext_prc() による終了と同等である。終了コードは、正常終了メッセージに
 * 設定され、親プロセスに通知される」。だから戻ったら ext_prc を呼ぶ。
 */

    .section .text.start, "ax"
    .globl _start
    .type _start, @function
_start:
#if defined(__x86_64__)
    /* フレームポインタを切っておく。**ここが呼び出し連鎖の根**なので、
     * 上へ辿れてはいけない。x86 は戻り番地がスタックにあるので、
     * レジスタを切るだけでは足りず「戻らない」ことで示す。 */
    xorl    %ebp, %ebp
    call    MAIN
    movl    %eax, %edi              /* 戻り値 = 終了コード */
    call    ext_prc
.Lhang:
    /* **ring 3 なので cli / hlt は使えない**（特権命令で #GP になる）。
     * ext_prc は戻らないので来ないはずの場所だが、来たときに
     * 一般保護例外を足すより素直に回る。 */
    pause
    jmp     .Lhang
#else
    /* フレームポインタと戻り番地を切っておく。
     * **ここが呼び出し連鎖の根**なので、上へ辿れてはいけない。 */
    mov     x29, xzr
    mov     x30, xzr
    bl      MAIN
    /* 戻り値 = 終了コード */
    bl      ext_prc
.Lhang:
    wfe
    b       .Lhang
#endif

/*
 * crt0.S — ユーザプログラムの入口 (arm64)
 *
 * カーネル（btron_user_trampoline）は x0 にプロセス起動メッセージへの
 * ポインタを入れて EL0 へ降ろしてくる。仕様 §1.1.6 の形式-A
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

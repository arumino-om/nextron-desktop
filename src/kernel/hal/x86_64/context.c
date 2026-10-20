/*
 * context.c — タスクの初期スタック構築 (x86_64)
 *
 * 保存フレームのレイアウトは context.S の注記と一致させること。
 */

#include <btron/hal.h>

IMPORT void btron_task_trampoline(void);
IMPORT void btron_msg_trampoline(void);

/* フレームの大きさ。呼び出され側保存レジスタ 6 本 ＋ 戻り番地。
 *
 * **arm64 とは値が違う**（あちらは 0xA0）。レジスタの本数も、戻り先が
 * レジスタ (x30) かスタックかも違うので、アーキごとに持つしかない。 */
#define CTX_FRAME 56

/* フレーム内の添字 (8 バイト単位)。context.S の push 順の逆。 */
#define F_R15 0
#define F_R14 1
#define F_R13 2
#define F_R12 3
#define F_RBX 4
#define F_RBP 5
#define F_RET 6

#define F_WORDS (CTX_FRAME / 8)

/* フレームを置く番地を決める。
 *
 * **整列が ABI の要求と逆向きに効く。** ABI は `call` の直前に RSP が
 * 16 の倍数であることを要求する。トランポリンは `ret` で着地するので
 * 戻り番地は積まれておらず、着地時点の RSP = (フレームの底 + 56) が
 * 16 の倍数でなければならない。56 は 8 mod 16 なので、**底を 8 mod 16 に
 * 置く**。`(top & ~0xF) - 56` がちょうどそれになる。
 *
 * ここを間違えると、SSE を使う命令（clang が構造体の複写で出す）が
 * 一般保護例外になる。しかも「たまたま揃っている経路」では動くので、
 * 症状が構造体の大きさに依存して現れる。 */
LOCAL inline UD frame_base(UD stack_top)
{
	return (stack_top & ~0xFULL) - CTX_FRAME;
}

EXPORT UD hal_ctx_init(UD stack_top, void (*entry)(W stacd, VP exinf), W stacd,
                       VP exinf)
{
	UD sp = frame_base(stack_top);
	UD *f = (UD *)(VP)sp;
	W i;

	for (i = 0; i < F_WORDS; i++) {
		f[i] = 0;
	}
	/* btron_task_trampoline が読む 3 つ */
	f[F_RBX] = (UD)(VP)entry;
	f[F_R12] = (UD)(UW)stacd;
	f[F_R13] = (UD)(VP)exinf;
	/* hal_ctx_switch の pop 列 + ret でここへ飛ぶ */
	f[F_RET] = (UD)(VP)btron_task_trampoline;
	return sp;
}

EXPORT UD hal_ctx_inject(UD sp, void (*handler)(W a0, VP a1), W a0, VP a1)
{
	UD newsp;
	UD *f;
	W i;

	if (sp == 0 || handler == 0) {
		return 0;
	}
	/* 退避フレームの**下**にもう 1 枚積む。元のフレームはそのまま残すので、
	 * ハンドラから戻ったときに割り込んだ位置へ帰れる。 */
	newsp = frame_base(sp);
	f = (UD *)(VP)newsp;

	for (i = 0; i < F_WORDS; i++) {
		f[i] = 0;
	}
	f[F_RBX] = (UD)(VP)handler;
	f[F_R12] = (UD)(UW)a0;
	f[F_R13] = (UD)(VP)a1;
	f[F_R14] = sp; /* 戻り先の RSP。btron_msg_trampoline が使う */
	f[F_RET] = (UD)(VP)btron_msg_trampoline;
	return newsp;
}

/*
 * context.c — タスクの初期スタック構築 (arm64)
 *
 * 保存フレームのレイアウトは context.S の注記と一致させること。
 */

#include <btron/hal.h>

/* フレームの大きさ。x19-x30 の 12 本 ＋ d8-d15 の 8 本 = 160 バイト。
 * **x86_64 とは値が違う**（あちらは 56）。context.S の注記と一致させる。 */
#define HAL_CTX_FRAME_SIZE 0xA0

IMPORT void btron_task_trampoline(void);
IMPORT void btron_msg_trampoline(void);
IMPORT void btron_user_trampoline(void);

/* フレーム内の添字 (8 バイト単位)。context.S のオフセットを 8 で割った値。 */
#define F_X19 0
#define F_X20 1
#define F_X21 2
#define F_X22 3
#define F_X23 4
#define F_X24 5
#define F_X25 6
#define F_X26 7
#define F_X27 8
#define F_X28 9
#define F_X29 10
#define F_X30 11

#define F_WORDS (HAL_CTX_FRAME_SIZE / 8)

EXPORT UD hal_ctx_init(UD stack_top, void (*entry)(W stacd, VP exinf), W stacd, VP exinf)
{
	UD sp;
	UD *f;
	W i;

	/* AAPCS64 は SP の 16 バイト境界を要求する。 */
	sp = (stack_top & ~0xFULL) - HAL_CTX_FRAME_SIZE;
	f = (UD *)(VP)sp;

	for (i = 0; i < F_WORDS; i++) {
		f[i] = 0;
	}
	/* btron_task_trampoline が読む 3 つ */
	f[F_X19] = (UD)(VP)entry;
	f[F_X20] = (UD)(UW)stacd;
	f[F_X21] = (UD)(VP)exinf;
	/* hal_ctx_switch の ldp x29,x30 + ret でここへ飛ぶ */
	f[F_X29] = 0;
	f[F_X30] = (UD)(VP)btron_task_trampoline;
	return sp;
}

EXPORT UD hal_ctx_init_user(UD stack_top, UD entry, UD user_sp, UD arg)
{
	UD sp;
	UD *f;
	W i;

	sp = (stack_top & ~0xFULL) - HAL_CTX_FRAME_SIZE;
	f = (UD *)(VP)sp;

	for (i = 0; i < F_WORDS; i++) {
		f[i] = 0;
	}
	/* btron_user_trampoline が読む 3 つ。 */
	f[F_X19] = entry;
	f[F_X20] = user_sp;
	f[F_X21] = arg;
	f[F_X29] = 0;
	f[F_X30] = (UD)(VP)btron_user_trampoline;
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
	newsp = (sp & ~0xFULL) - HAL_CTX_FRAME_SIZE;
	f = (UD *)(VP)newsp;

	for (i = 0; i < F_WORDS; i++) {
		f[i] = 0;
	}
	f[F_X19] = (UD)(VP)handler;
	f[F_X20] = (UD)(UW)a0;
	f[F_X21] = (UD)(VP)a1;
	f[F_X22] = sp; /* 戻り先の SP。btron_msg_trampoline が使う */
	f[F_X29] = 0;
	f[F_X30] = (UD)(VP)btron_msg_trampoline;
	return newsp;
}

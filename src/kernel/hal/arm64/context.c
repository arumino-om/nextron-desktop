/*
 * context.c — タスクの初期スタック構築 (arm64)
 *
 * 保存フレームのレイアウトは context.S の注記と一致させること。
 */

#include <btron/hal.h>

IMPORT void btron_task_trampoline(void);

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

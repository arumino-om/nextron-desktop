/*
 * stub.c — まだ実装していない HAL の口 (x86_64)
 *
 * **嘘の成功を返さない。** 呼ばれたら「何が無いのか」を出して止める。
 * スタブが黙って 0 を返すと、中心核は動いているように見えて実際には
 * スケジュールされない、という最悪の形になる（Phase 1 の ①〜⑮ が
 * 全部「無言のハング」から始まったことへの対処と同じ姿勢）。
 *
 * いまの x86_64 は起動・ページング・コンソール・例外の報告までで、
 * 中心核には接続していない。boot_main がそこで `hal_exit()` するので、
 * ここの関数は通常呼ばれない。
 *
 * 残っているもの（T1-11 の続き = L5。arm64 側の対応物を括弧に書く）:
 *
 *   - コンテキスト切替                            (context.c / context.S)
 *   - TSS と ring 3 への降り方                    (arm64 は ERET だけで済む)
 *   - システムコールの入口: syscall / sysret      (vectors.S の svc 経路)
 *
 * **入ったもの**: Local APIC と タイマ割込みは apic.c にある。
 */

#include <btron/hal.h>
#include <btron/intc.h>

LOCAL void missing(CONST B *what) __attribute__((noreturn));

LOCAL void missing(CONST B *what)
{
	hal_console_puts("\n*** x86_64 で未実装の HAL が呼ばれた: ");
	hal_console_puts(what);
	hal_console_puts(" ***\n");
	hal_console_puts("  （T1-11 の残り。src/kernel/hal/x86_64/stub.c）\n");
	hal_exit(1);
}

/* ---- 割込みコントローラ (Local APIC / IOAPIC) ---- */

/* ---- タイマ割込み ---- */

/* ---- コンテキスト切替 ---- */

EXPORT UD hal_ctx_init(UD stack_top, void (*entry)(W stacd, VP exinf), W stacd,
                       VP exinf)
{
	(void)stack_top;
	(void)entry;
	(void)stacd;
	(void)exinf;
	missing("hal_ctx_init（コンテキストの初期化）");
}

EXPORT UD hal_ctx_inject(UD sp, void (*handler)(W a0, VP a1), W a0, VP a1)
{
	(void)sp;
	(void)handler;
	(void)a0;
	(void)a1;
	missing("hal_ctx_inject（ハンドラの差し込み）");
}

EXPORT void hal_ctx_switch(UD *save_sp, UD new_sp)
{
	(void)save_sp;
	(void)new_sp;
	missing("hal_ctx_switch（コンテキスト切替）");
}

EXPORT UD hal_ctx_init_user(UD stack_top, UD entry, UD user_sp, UD arg)
{
	(void)stack_top;
	(void)entry;
	(void)user_sp;
	(void)arg;
	missing("hal_ctx_init_user（ユーザ特権への初期化）");
}


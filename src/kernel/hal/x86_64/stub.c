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
 * 残っているもの（T1-11 の続き。arm64 側の対応物を括弧に書く）:
 *
 *   - 割込みコントローラ: Local APIC / IOAPIC   (gic.c)
 *   - タイマ割込み: APIC タイマ                  (timer.c の tick 系)
 *   - コンテキスト切替                            (context.c / context.S)
 *   - 自前の GDT と TSS（ring 3 と例外スタック）  (arm64 は不要)
 *   - システムコールの入口: syscall / sysret      (vectors.S の svc 経路)
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

EXPORT ER hal_intc_init(void)
{
	return E_NOSPT;
}

EXPORT ER hal_intc_enable(UW intid)
{
	(void)intid;
	return E_NOSPT;
}

EXPORT ER hal_intc_disable(UW intid)
{
	(void)intid;
	return E_NOSPT;
}

EXPORT ER hal_intc_set_priority(UW intid, UW prio)
{
	(void)intid;
	(void)prio;
	return E_NOSPT;
}

EXPORT UW hal_intc_acknowledge(void)
{
	return 1023U; /* 何も発生していない、として扱われる値 */
}

EXPORT void hal_intc_end(UW intid)
{
	(void)intid;
}

EXPORT UW hal_intc_version(void)
{
	return 0U; /* 不明。中心核の検査はこれを見て落ちる */
}

/* ---- タイマ割込み ---- */

EXPORT ER hal_timer_tick_start(UW hz)
{
	(void)hz;
	return E_NOSPT;
}

EXPORT UW hal_timer_tick_ms(void)
{
	return 0U;
}

EXPORT void hal_timer_tick_reload(void)
{
}

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

/* MSI の宛先の割り当て (詳細 F8)。
 *
 * **これは missing() ではない。** PCIe 層は「宛先を割り当てられるか」を
 * 問い合わせる側で、`E_NOSPT` は正常な答えである（arm64 も ITS / GICv2m が
 * 無いので同じ値を返す）。x86_64 で本当に割り当てられるようになるのは
 * Local APIC が入ってから — 宛先は `0xFEE0_0000` 系のアドレスと
 * ベクタ番号で、追加のハードウェアは要らない（詳細 F8）。 */
EXPORT ER hal_msi_alloc(UW *intid, UD *addr, UW *data)
{
	(void)intid;
	(void)addr;
	(void)data;
	return E_NOSPT;
}

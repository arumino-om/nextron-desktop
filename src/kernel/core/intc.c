/*
 * intc.c — 割込みの受付と配送（アーキ中立）
 *
 * コントローラの操作は hal_intc_* に閉じている（GICv2 / GICv3 / APIC の差は
 * そちら側）。ここは「どの INTID に誰が登録されているか」だけを持つ。
 *
 * 【規律】ハンドラはタスク独立部で走る。ここから自タスクを待ち状態にする
 * システムコールを呼んではならない（仕様はこれを E_CTX とする）。
 */

#include <btron/intc.h>
#include <btron/kernel.h>

/* 割込み発生のたびに引くので、配列で持つ。INTID は 1020 まであるが、
 * 実際に使うのは SGI/PPI と少数の SPI なので上限を切る（D9 暫定）。 */
#define INTC_TABLE_SIZE 256

typedef struct Vector {
	INTHANDLER handler;
	VP arg;
} Vector;

LOCAL Vector table[INTC_TABLE_SIZE];
LOCAL UD irq_total;
LOCAL UD irq_spurious;

/* 中心核のディスパッチャ。割込み処理の最後に呼ぶ。
 * 中心核が未起動なら何もしない。 */
IMPORT void knl_dispatch_from_irq(void);

EXPORT ER intc_init(void)
{
	UW i;

	for (i = 0; i < INTC_TABLE_SIZE; i++) {
		table[i].handler = 0;
		table[i].arg = 0;
	}
	irq_total = 0;
	irq_spurious = 0;
	return hal_intc_init();
}

EXPORT ER intc_set_handler(UW intid, INTHANDLER handler, VP arg)
{
	UD state;

	if (intid >= INTC_TABLE_SIZE || handler == 0) {
		return E_PAR;
	}
	state = hal_int_disable();
	table[intid].handler = handler;
	table[intid].arg = arg;
	hal_int_restore(state);
	return E_OK;
}

EXPORT ER intc_clr_handler(UW intid)
{
	UD state;

	if (intid >= INTC_TABLE_SIZE) {
		return E_PAR;
	}
	state = hal_int_disable();
	table[intid].handler = 0;
	table[intid].arg = 0;
	hal_int_restore(state);
	return E_OK;
}

EXPORT ER intc_enable(UW intid)
{
	return hal_intc_enable(intid);
}

EXPORT ER intc_disable(UW intid)
{
	return hal_intc_disable(intid);
}

EXPORT ER intc_set_priority(UW intid, UW prio)
{
	return hal_intc_set_priority(intid, prio);
}

EXPORT UD intc_count(void)
{
	return irq_total;
}

/* ------------------------------------------------------------------ */
/* ベクタから呼ばれる入口                                              */
/* ------------------------------------------------------------------ */

EXPORT void intc_dispatch(void)
{
	/* 1 回の例外で複数の割込みが溜まっていることがあるので、
	 * spurious が返るまで繰り返す。 */
	for (;;) {
		UW intid = hal_intc_acknowledge();

		if (intid >= 1020U) {
			break; /* 1020-1023 は予約。1023 = spurious */
		}
		irq_total++;
		if (intid < INTC_TABLE_SIZE && table[intid].handler != 0) {
			table[intid].handler(intid, table[intid].arg);
		} else {
			irq_spurious++;
			/* ハンドラが無い割込みは、EOI を出して黙らせるしかない。
			 * 原因を黙って捨てると無言のハングに戻るので数は残す。 */
		}
		hal_intc_end(intid);
	}

	/* ハンドラがタスクを起こした可能性がある。ここで切り替える。
	 * vectors.S のフレームの上で切り替わるので、戻ってくるのは
	 * このタスクが次に選ばれたときになる。 */
	knl_dispatch_from_irq();
}

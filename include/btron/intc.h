/*
 * intc.h — 割込みコントローラと割込みハンドラ
 *
 * docs/design-issues.md 詳細 D8 / 規律 R19。
 *
 * コントローラの種別とアドレスは `MachDesc` から来る（GICv2 / GICv3 / APIC）。
 * **ここにアドレスを直書きしてはならない。**
 *
 * 割込み番号は GIC の INTID をそのまま使う。
 *
 *   0   〜 15   SGI (ソフトウェア生成割込み。コア間通信)
 *   16  〜 31   PPI (コアごとの割込み。generic timer がここ)
 *   32  〜      SPI (共有周辺割込み)
 *
 * FDT の PPI 番号は 16 を足して INTID にしてある（machdesc.h の注記）。
 */

#ifndef __BTRON_INTC_H__
#define __BTRON_INTC_H__

#include <btron/errcode.h>
#include <btron/typedef.h>

#define INTC_MAX_INTID 1020 /* GIC の仕様上の上限 */

/* 割込みハンドラ。タスク独立部で走る。
 * ここから自タスクを待ち状態にするシステムコールを呼んではならない (E_CTX)。 */
typedef void (*INTHANDLER)(UW intid, VP arg);

/* 起動時に 1 度だけ。MachDesc を見てコントローラを初期化する。 */
IMPORT ER intc_init(void);

/* ハンドラの登録と解除。*/
IMPORT ER intc_set_handler(UW intid, INTHANDLER handler, VP arg);
IMPORT ER intc_clr_handler(UW intid);

/* 個別の割込みの有効化・無効化と優先度 (0 が最高)。 */
IMPORT ER intc_enable(UW intid);
IMPORT ER intc_disable(UW intid);
IMPORT ER intc_set_priority(UW intid, UW prio);

/* ベクタから呼ばれる。登録されたハンドラへ配る。 */
IMPORT void intc_dispatch(void);

/* 受け付けた割込みの総数。デバッグとテスト用。 */
IMPORT UD intc_count(void);

/* ------------------------------------------------------------------ */
/* 割込みの禁止・許可（アーキ依存）                                    */
/*                                                                     */
/* 短いクリティカルセクションに使う。保存・復元の対で使うこと。          */
/* ------------------------------------------------------------------ */

IMPORT UD hal_int_disable(void); /* 以前の状態を返す */
IMPORT void hal_int_restore(UD state);
IMPORT void hal_int_enable(void);
IMPORT Bool hal_int_enabled(void);

/* 割込みを待って停止する。アイドルループで使う。 */
IMPORT void hal_cpu_idle(void);

/* ------------------------------------------------------------------ */
/* アーキ依存のコントローラ操作                                        */
/*                                                                     */
/* src/kernel/core/intc.c だけが呼ぶ。他から直接触らない。              */
/* 実装は GICv2 / GICv3 / APIC の差をここで吸収する。                   */
/* ------------------------------------------------------------------ */

IMPORT ER hal_intc_init(void);
IMPORT ER hal_intc_enable(UW intid);
IMPORT ER hal_intc_disable(UW intid);
IMPORT ER hal_intc_set_priority(UW intid, UW prio);
IMPORT UW hal_intc_acknowledge(void); /* 発生した INTID。無ければ 1023 */
IMPORT void hal_intc_end(UW intid);
IMPORT UW hal_intc_version(void); /* GIC の版。ログとテスト用 */

/* ------------------------------------------------------------------ */
/* タイマ割込み                                                        */
/* ------------------------------------------------------------------ */

/* hz Hz の周期割込みを仕掛ける。MachDesc の timer_intid を使う。 */
IMPORT ER hal_timer_tick_start(UW hz);

/* 1 tick のミリ秒。システム時刻の刻み。 */
IMPORT UW hal_timer_tick_ms(void);

/* 次の tick を仕込む。タイマハンドラの先頭で呼ぶ。 */
IMPORT void hal_timer_tick_reload(void);

#endif /* __BTRON_INTC_H__ */

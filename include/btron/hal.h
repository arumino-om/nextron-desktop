/*
 * hal.h — HAL (ハードウェア抽象層) の最小インタフェース
 *
 * アーキ依存の処理はここを通す。docs/design-issues.md の規律:
 *   R2  ページサイズを定数でハードコードせず、HAL が公開する値を参照する
 *   R3  アドレス空間の生成・切替は 2 関数に閉じる
 */

#ifndef __BTRON_HAL_H__
#define __BTRON_HAL_H__

#include <btron/typedef.h>

/* --- CPU の起動時設定 ----------------------------------------------
 * 起動第 1 段のいちばん最初に呼ぶ。MMU より前でよい。 */

IMPORT void hal_cpu_init(void);

/* --- コンソール（起動初期のシリアル出力） ------------------------- */

IMPORT void hal_console_init(void);
IMPORT void hal_console_putc(B c);
IMPORT void hal_console_puts(CONST B *s);

/* 起動直後は物理アドレス（恒等写像）で UART を触る。カーネルが上位半分へ
 * 移った後、MMIO 区画の別名 (BTRON_MMIO_BASE + 物理) に切り替える。
 * これ以降は恒等写像を捨てられる。宛先はドライバ自身が知っている。 */
IMPORT void hal_console_remap(void);

/* ファームウェアの記述（DT / ACPI）から読んだシリアルをドライバに教える。
 *
 * type は machdesc.h の MD_UART_*。ドライバが扱える種類でなければ False を
 * 返し、既定値のまま動き続ける。起動第 1 段で、MMU を有効にする前に呼ぶ。
 *
 * これにより規律 R2 と同じ形（アドレスを直書きせず読み取った値を使う）が
 * シリアルについても成立する。既定値は「記述が読めないときの保険」。 */
IMPORT Bool hal_console_set(UW type, UD base);

/* ドライバが使っている物理アドレス。hal_vm_init() が MMIO 区画に
 * 張るべきページを知るために使う。 */
IMPORT UD hal_console_pa(void);

/* コンソール出力の複製先を登録する。
 *
 * 画面コンソール (screen_console_init) が自分を登録するので、既存の
 * hal_console_puts() の呼び出しがそのまま画面にも出る。呼び出し側を
 * 1 行も変えずに済むのが狙い。0 を渡すと複製をやめる。 */
IMPORT void hal_console_tee(void (*fn)(CONST B *s));

/* --- ページサイズ (R2) --------------------------------------------
 * A13 で 4KB と決めたが、定数として配らず HAL 経由で参照する。
 * 将来 16KB granule へ移行する余地を残すため。            */

IMPORT UW hal_page_size(void);
IMPORT UW hal_page_shift(void);

/* --- 実行の終了 ----------------------------------------------------
 * テスト実行時に QEMU を終了させるために使う。
 * 実機では停止ループになる。                              */

IMPORT void hal_exit(W code) __attribute__((noreturn));

/* --- 例外 ----------------------------------------------------------
 * ベクタが無いとフォルトが無言のハングになる。起動直後に入れる。 */

IMPORT void hal_exception_init(void);

/* --- タスクコンテキストの切替 --------------------------------------
 * 中心核のディスパッチャが使う。保存するのは AAPCS64 の呼び出され側保存
 * レジスタ (x19-x30, d8-d15) と SP だけ。呼び出し側保存レジスタは
 * 普通の関数呼び出しと同じ扱いなので、呼び出し元が面倒を見ている。
 *
 * 割込みハンドラの中から呼んでもよい。その場合、割込み入口で全レジスタを
 * 退避した上にこのフレームが積まれ、復帰時に逆順に戻る。 */

/* 新しいタスクの初期スタックを作り、切替に渡す SP を返す。
 * 最初の hal_ctx_switch() で entry(stacd, exinf) が呼ばれるように仕込む。 */
IMPORT UD hal_ctx_init(UD stack_top, void (*entry)(W stacd, VP exinf), W stacd, VP exinf);

/* 止まっているタスクのスタックにハンドラ呼び出しを差し込む。
 *
 * `sp` はそのタスクの退避した SP。戻り値が新しい SP で、呼び出し側が
 * TCB に入れ直す。ディスパッチャがそのタスクを選ぶと、まず `handler`
 * が走り、戻ると**割り込んだ位置から再開する**。
 *
 * 周辺核 §1.2.3 のメッセージハンドラがこれを使う。タスク例外 (T1-14) も
 * 同じ仕掛けになる。失敗時 0。
 *
 * 【前提】`sp` が有効な退避フレームを指していること。実行状態のタスク
 * （自分自身）には使えない — SP が退避されていないので、その場合は
 * 呼び出し側がハンドラを直接呼ぶ。 */
IMPORT UD hal_ctx_inject(UD sp, void (*handler)(W a0, VP a1), W a0, VP a1);

/* *save_sp に現在の SP を保存し、new_sp へ切り替える。 */
IMPORT void hal_ctx_switch(UD *save_sp, UD new_sp);

/* 保存フレームの大きさ。スタックサイズの検査に使う。 */
#define HAL_CTX_FRAME_SIZE 0xA0

/* --- 時間の読み出し ------------------------------------------------
 * 中心核の時間管理（BTRON3 §1.3）の土台。いまは単調カウンタの読み出しだけで、
 * 割込みは GIC ができてから (D8 / machdesc の timer_intid)。
 *
 * 周波数はレジスタから直接読めるので、ファームウェアの記述に依らない。 */

IMPORT UD hal_timer_freq(void);  /* Hz。0 なら読めなかった */
IMPORT UD hal_timer_count(void); /* 単調増加のカウンタ値 */

/* usec マイクロ秒だけ待つ。カウンタを回して待つだけで、割込みは使わない。 */
IMPORT void hal_timer_wait_us(UD usec);

#endif /* __BTRON_HAL_H__ */

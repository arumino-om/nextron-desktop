/*
 * exit.c — 実行の終了 (arm64)
 *
 * 試す順番は 3 段（詳細 F9「報告と終了」/ F7）。
 *
 *   1. ARM セミホスティング    QEMU を終了コード付きで終わらせる
 *   2. PSCI SYSTEM_OFF         記述から取った呼び口で電源を落とす
 *   3. 停止ループ
 *
 * ------------------------------------------------------------------------
 * なぜセミホスティングを先に試すのか
 * ------------------------------------------------------------------------
 *
 * **終了コードを持ち帰れるのはセミホスティングだけ**である。検査の合否は
 * 終了コードそのもので、`ctest` はそれを見ている。PSCI SYSTEM_OFF で
 * 落とすと QEMU は常に 0 で終わるので、落ちた検査が通ったように見える。
 *
 * そして「セミホスティングが使えるか」は**試す以外に知る方法が無い**。
 * ARM のセミホスティングには存在確認の呼び出しが無く、使えない環境では
 * `hlt #0xF000` 自体が「分類なし」の同期例外になる（Phase 2 (56)）。
 * つまり**例外になったことが、そのまま「実機である」という答え**になる。
 * そこで `hal_exception_report()` の先頭でこの状態を見て、2 段目へ回す
 * （`hal_exit_pending()` / `hal_exit_continue()`）。
 *
 * ------------------------------------------------------------------------
 * なぜ無条件に電源を落とさないのか
 * ------------------------------------------------------------------------
 *
 * 実機のノート PC にはシリアルが無いことがあり（詳細 F9）、そのとき
 * **結果を読む手段は画面だけ**である。電源を落とすと消えるので、
 * シリアルが黙っている構成では画面に残して止まる。`HOLD` の印
 * （ESP に置く開発用の印）が無い実機でも読めるようにするため。
 */

#include <btron/hal.h>
#include <btron/machdesc.h>
#include <btron/screen.h>

#define SYS_EXIT 0x18
#define ADP_STOPPED_APPLICATION_EXIT 0x20026UL

/* PSCI 0.2 以降の関数 ID（SMC 呼び出し規約の 32bit 側）。 */
#define PSCI_SYSTEM_OFF 0x84000008UL

/* hal_exit の中にいるか。セミホスティングの命令が例外になったとき、
 * それが「異常」ではなく「終了の途中」だと見分けるために使う。 */
LOCAL Bool exiting;
LOCAL W exit_code;

LOCAL void exit_fallback(W code) __attribute__((noreturn));

EXPORT Bool hal_exit_pending(void)
{
	return exiting;
}

LOCAL void psci_system_off(void)
{
	CONST MachDesc *md = machdesc();
	register UD x0 __asm__("x0") = PSCI_SYSTEM_OFF;

	/* 呼び口は記述から取る（規律 R19）。HVC か SMC かは機種で違い、
	 * 間違った方を使うと例外になる。 */
	switch (md->boot_method) {
	case MD_BOOT_PSCI_HVC:
		__asm__ volatile("hvc #0" : "+r"(x0) : : "memory");
		break;
	case MD_BOOT_PSCI_SMC:
		__asm__ volatile("smc #0" : "+r"(x0) : : "memory");
		break;
	default:
		break; /* PSCI の記述が無い。停止ループへ落ちる */
	}
}

/* 2 段目以降。セミホスティングが使えなかったときの経路。 */
LOCAL void exit_fallback(W code)
{
	(void)code;

	/* 結果を読む手段が画面しか無ければ、残して止まる。
	 * 戻ってきたら「他に読む手段がある」か「画面が無い」のどちらか。 */
	screen_hold_result();

	hal_console_puts("\n  電源を落とします (PSCI SYSTEM_OFF)\n");
	psci_system_off();

	/* PSCI が無い、または効かなかった。
	 * **ここでセミホスティングに戻ってはならない** — それが効かないから
	 * ここに居る。黙って止まる（Phase 2 (56) の無限ループを作らない）。 */
	for (;;) {
		__asm__ volatile("wfe");
	}
}

/* セミホスティングの `hlt` が例外になった後、例外ハンドラから呼ばれる。
 * 元の命令へ戻る必要は無い（どうせ終了する）ので、そのまま先へ進む。 */
EXPORT void hal_exit_continue(void)
{
	exit_fallback(exit_code);
}

EXPORT void hal_exit(W code)
{
	UD block[2] = { ADP_STOPPED_APPLICATION_EXIT, (UD)(UW)code };

	register UD x0 __asm__("x0") = SYS_EXIT;
	register UD x1 __asm__("x1") = (UD)(VP)block;

	exiting = True;
	exit_code = code;

	/* セミホスティングがあればここで終わる。無ければこの命令自体が
	 * 同期例外になり、hal_exception_report() が hal_exit_continue() を
	 * 呼ぶので、ここへは戻ってこない。 */
	__asm__ volatile("hlt #0xF000" : : "r"(x0), "r"(x1) : "memory");

	/* 戻ってきた（将来の実装差への保険）。同じ 2 段目へ進む。 */
	exit_fallback(code);
}

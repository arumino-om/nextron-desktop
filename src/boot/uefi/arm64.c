/*
 * arm64.c — ローダの最後の 1 手 (arm64)
 *
 * UEFI は「すべての RAM を恒等写像し、MMU とキャッシュを有効にした」状態で
 * 制御を渡してくる。一方カーネルの起動第 1 段は「MMU 無効・物理アドレス」を
 * 前提にしている（§3.2 / Phase 1 ④）。
 *
 * MMU が有効なまま TCR_EL1 を書き換えることはアーキテクチャ上許されない
 * (CONSTRAINED UNPREDICTABLE) ので、ここで MMU を切ってから渡す。
 *
 * 【x86_64 との違い】x86_64 はロングモードのままページングを切れない。
 * 代わりに CR3 の差し替えは合法なので、x86_64 のカーネルは
 * 「ページング有効・恒等写像済み」で受け取り、自前のテーブルを作って
 * CR3 を書くだけでよい。つまりこの関数は x86_64 では単なるジャンプになる。
 *
 * 【キャッシュ】MMU とデータキャッシュを切ると、以後のアクセスは
 * キャッシュを経由しない。キャッシュに残った dirty な行は見えなくなるので、
 * 切る前に PoC (Point of Coherency) まで書き出しておかなければならない。
 * 対象は (1) 読み込んだカーネルイメージと引き継ぎ情報、(2) この関数自身の
 * コード（切った後に物理メモリから取り直して実行されるため）。
 */

#include "loader.h"

LOCAL UD cache_line(void)
{
	UD ctr;

	/* CTR_EL0.DminLine = bits[19:16] は「ワード数の log2」 */
	__asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
	return 4ULL << ((ctr >> 16) & 0xFULL);
}

LOCAL void clean_to_poc(UD base, UD size)
{
	UD line = cache_line();
	UD p = base & ~(line - 1);
	UD end = base + size;

	for (; p < end; p += line) {
		__asm__ volatile("dc civac, %0" : : "r"(p) : "memory");
	}
	__asm__ volatile("dsb sy" ::: "memory");
}

EXPORT void arch_jump_to_kernel(UD entry, UD bootinfo_pa, UD clean_base, UD clean_size)
{
	/* x0 / x1 に固定して渡す。カーネルの _start は x0 を第 1 引数として読む。 */
	register UD arg __asm__("x0") = bootinfo_pa;
	register UD dst __asm__("x1") = entry;

	clean_to_poc(clean_base, clean_size);

	/* この関数自身のコードも書き出す。MMU とキャッシュを切った直後の
	 * 命令フェッチは物理メモリから行われるので、ファームウェアが
	 * ロード時に PoU までしか書き出していないと古い内容を拾いうる。
	 * 関数先頭から 4KB あれば以下のシーケンスを十分覆う。 */
	clean_to_poc((UD)(VP)&arch_jump_to_kernel, 4096);

	__asm__ volatile(
	    "mrs x2, sctlr_el1\n"
	    "bic x2, x2, #(1 << 0)\n"  /* M: MMU */
	    "bic x2, x2, #(1 << 2)\n"  /* C: データキャッシュ */
	    "bic x2, x2, #(1 << 12)\n" /* I: 命令キャッシュ */
	    "msr sctlr_el1, x2\n"
	    "isb\n"
	    "ic iallu\n"       /* 命令キャッシュを捨てる */
	    "tlbi vmalle1\n"   /* UEFI が張った写像の TLB を捨てる */
	    "dsb nsh\n"
	    "isb\n"
	    "br x1\n"
	    :
	    : "r"(arg), "r"(dst)
	    : "x2", "memory");
	__builtin_unreachable();
}

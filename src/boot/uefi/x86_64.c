/*
 * x86_64.c — ローダの最後の 1 手 (x86_64)
 *
 * arm64 と違い、**ページングを切らずに渡す**。
 *
 * ロングモードはページング必須で、CR0.PG を落とすには一旦 32bit モードへ
 * 降りる必要がある。一方 CR3 の差し替えはロングモードのまま合法なので、
 * x86_64 のカーネルは「ページング有効・UEFI が張った恒等写像の上」で
 * 受け取り、自前のページテーブル（恒等写像を含む）を作って CR3 を書けばよい。
 * 恒等写像を含めてあるので、書いた瞬間に PC が無効になることもない。
 * これは src/kernel/hal/x86_64/mmu.c の hal_vm_init() が既にそうなっている。
 *
 * 【状態】x86_64 のカーネル起動経路（start.S 相当）がまだ無いため未検証。
 */

#include "loader.h"

EXPORT void arch_jump_to_kernel(UD entry, UD bootinfo_pa, UD clean_base, UD clean_size)
{
	/* ページングを切らないのでキャッシュ整合の操作も要らない。
	 * UEFI は WB キャッシュ・コヒーレントな状態で渡してくる。 */
	(void)clean_base;
	(void)clean_size;

	/* System V AMD64 ABI で第 1 引数は RDI。カーネル側もそれで受ける。
	 * ローダ自身は Microsoft x64 ABI でビルドされているので、
	 * ここで明示的に積み替える。 */
	__asm__ volatile(
	    "movq %0, %%rdi\n"
	    "jmp  *%1\n"
	    :
	    : "r"(bootinfo_pa), "r"(entry)
	    : "rdi");
	__builtin_unreachable();
}

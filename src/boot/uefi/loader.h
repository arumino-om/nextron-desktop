/*
 * loader.h — UEFI ローダの内部インタフェース
 *
 * アーキ依存は「MMU を切ってカーネルへ飛ぶ」1 関数だけに閉じる。
 * これは規律 R3（アドレス空間操作を 2 関数に閉じる）と同じ考え方で、
 * ローダ側の最後の 1 手を 1 箇所に集めたもの。
 */

#ifndef __BTRON_UEFI_LOADER_H__
#define __BTRON_UEFI_LOADER_H__

#include <btron/typedef.h>

/* コンソール（ローダ用）。Serial I/O があればそれ、無ければ ConOut。 */
IMPORT void lcon_init(VP systab);
IMPORT void lcon_puts(CONST B *s);
IMPORT void lcon_hex(UD v);
IMPORT void lcon_dec(UD v);

/* ExitBootServices の直前に呼ぶ。以後 lcon_* は何もしない。
 * ConOut も Serial I/O もブートサービスのドライバなので、
 * ExitBootServices の後に呼ぶと未定義動作になる。 */
IMPORT void lcon_shutdown(void);

/* ExitBootServices 後に呼ぶ。MMU とキャッシュを切り、entry へ飛ぶ。
 *
 * UEFI はすべての RAM を恒等写像し MMU を有効にした状態で制御を渡してくる。
 * カーネルの起動第 1 段は「MMU 無効・物理アドレス」を前提にしているので、
 * ここで切ってから渡す。切る前にカーネルイメージと引き継ぎ情報を
 * PoC まで書き出しておかないと、キャッシュに残った分が失われる。
 *
 * 戻らない。 */
IMPORT void arch_jump_to_kernel(UD entry, UD bootinfo_pa, UD clean_base, UD clean_size)
    __attribute__((noreturn));

#endif /* __BTRON_UEFI_LOADER_H__ */

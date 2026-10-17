/*
 * bootinfo.h — ブートローダからカーネルへの引き継ぎ情報
 *
 * docs/design-issues.md 詳細 D8「本OS での方針」の実装面。
 *
 *   ハードウェア列挙  ACPI (x86_64) / Device Tree (arm64)
 *   起動時フレームバッファ  UEFI GOP
 *   区画  GPT
 *
 * この構造体は**ローダとカーネルの間の ABI** である。両者は別のバイナリで、
 * ローダは PE (UEFI アプリ)、カーネルは ELF (D1) として別々にビルドされる。
 * したがってここを変えると両方を作り直さなければならない。`version` を上げ、
 * 後方互換を壊す変更は `BTRON_BOOTINFO_VERSION` の更新で検出させる。
 *
 * 【規律】すべてのアドレスは**物理アドレス**。ローダは MMU を切ってから
 * カーネルへ飛ぶので、引き継いだ時点で仮想アドレスは存在しない。
 *
 * 【規律】大きさは必ず `size` を見ること。ローダが新しくカーネルが古い
 * 組み合わせでも、先頭の共通部分だけは読めるようにしてある。
 */

#ifndef __BTRON_BOOTINFO_H__
#define __BTRON_BOOTINFO_H__

#include <btron/typedef.h>

/* "BTRONBI1" をリトルエンディアンで並べたもの。
 * 第 1 引数がこの値で始まっていなければ BootInfo ではない。 */
#define BTRON_BOOTINFO_MAGIC 0x3149424E4F525442ULL
#define BTRON_BOOTINFO_VERSION 1

/* Device Tree Blob の先頭マジック（ビッグエンディアン 0xd00dfeed）。
 * QEMU の -kernel 直接起動では第 1 引数が DTB のアドレスになるので、
 * BootInfo と区別するために使う。 */
#define BTRON_DTB_MAGIC 0xD00DFEEDU

/* ------------------------------------------------------------------ */
/* メモリ領域                                                          */
/*                                                                     */
/* UEFI の EFI_MEMORY_DESCRIPTOR を、カーネルが使う 4 種類に畳んだもの。 */
/* ------------------------------------------------------------------ */

#define BMEM_UNUSABLE 0 /* 使ってはならない（予約・故障・ファームウェア専有） */
#define BMEM_FREE 1     /* 自由に使える */
#define BMEM_LOADER 2   /* ローダが置いたもの（カーネル本体・この構造体自身） */
#define BMEM_FIRMWARE 3 /* ファームウェアのランタイムが要求する（ACPI NVS 等） */

typedef struct BootMemRegion {
	UD base; /* 物理アドレス。ページ境界 */
	UD size; /* バイト数。ページサイズの倍数 */
	UW type; /* BMEM_* */
	UW resv;
} BootMemRegion;

/* ------------------------------------------------------------------ */
/* 起動時フレームバッファ (UEFI GOP)                                   */
/*                                                                     */
/* Phase 4 のディスプレイプリミティブが本来のドライバを立てるまでの間、   */
/* ファームウェアが設定した画面をそのまま使う。                         */
/* ------------------------------------------------------------------ */

#define BFB_NONE 0
#define BFB_BGRA8 1 /* 下位から B,G,R,予約 の各 8bit (GOP の BlueGreenRedReserved) */
#define BFB_RGBA8 2 /* 下位から R,G,B,予約 の各 8bit */
#define BFB_OTHER 3 /* ビットマスク指定。いまは使わない */

typedef struct BootFrameBuffer {
	UD base;   /* 物理アドレス。0 ならフレームバッファ無し */
	UD size;   /* バイト数 */
	UW width;  /* 有効画素数 */
	UW height;
	UW pitch;  /* 1 走査線のバイト数。width * 4 とは限らない */
	UW format; /* BFB_* */
} BootFrameBuffer;

/* ------------------------------------------------------------------ */
/* 引き継ぎ情報の本体                                                  */
/* ------------------------------------------------------------------ */

typedef struct BootInfo {
	UD magic;   /* BTRON_BOOTINFO_MAGIC */
	UW version; /* BTRON_BOOTINFO_VERSION */
	UW size;    /* sizeof(BootInfo)。これより後ろの追加は version で判断する */

	UD kernel_pa;   /* カーネルイメージを実際に置いた物理先頭 */
	UD kernel_size; /* バイト数（.bss と起動スタックを含む） */

	/* ハードウェア列挙の入口。どちらか一方、または両方が 0 でないことを期待する。
	 * arm64 の QEMU virt は DTB、x86_64 は ACPI。 */
	UD acpi_rsdp; /* ACPI 2.0 の RSDP。0 なら無し */
	UD dtb;       /* Device Tree Blob。0 なら無し */

	UD mem_count; /* BootMemRegion の個数 */
	UD mem_pa;    /* BootMemRegion 配列の物理アドレス */

	BootFrameBuffer fb;

	/* UEFI のランタイムサービス（時計・変数・リセット）を後で使うために残す。
	 * ExitBootServices 後も有効。0 なら UEFI 経由で起動していない。 */
	UD efi_system_table;

	UD flags; /* BI_F_* */
} BootInfo;

/* ESP に `\BTRON\PANICTEST` が置かれていた。
 *
 * **緊急画面 (T4-02) を実機の経路で検査するためだけの印。**
 * 検査用の起動ディスクにだけ置く（tools/mkimage.py --panic-test）。
 * これが立っていると、起動時検査がすべて終わった後にわざと例外を起こし、
 * 緊急画面が出ることを確かめる。
 *
 * ビルドを分けずに済むのが要点。フォントを含むカーネルは再ビルドが重いので、
 * **同じバイナリのまま ESP の中身だけ変えて**経路を切り替える。 */
#define BI_F_PANIC_TEST 0x0001ULL

/* ESP に `\BTRON\HOLD` が置かれていた。
 *
 * **起動時検査が終わっても電源を落とさず、画面を残して居座る印。**
 * 手で画面を見るためにある（`run_qemu.py --gui`）。これが無いと
 * 検査の最後に `hal_exit()` が走って QEMU ごと終わり、窓が即座に閉じる。
 *
 * PANICTEST と同じく**カーネルもローダも作り直さない**。同じバイナリの
 * まま ESP の中身だけ変えて振る舞いを切り替える（Phase 1 ㉙）。 */
#define BI_F_HOLD 0x0002ULL

/* ESP に `\BTRON\SELFBOOT` が置かれていた。
 *
 * **起動時検査の代わりに、本来の起動列を通す印**（D8 / startup.c）。
 * ドライバを登録し、BTRON 区画を探して接続し（未構築なら構築し）、
 * **ディスク上の実行プログラムから外殻を起こす**。
 *
 * 検査の経路と分けてあるのは、検査が「ファイルシステムは 1 つも
 * 接続されていない状態から始まる」ことを前提に書かれているため。
 * これも PANICTEST / HOLD と同じく**同じバイナリのまま**切り替える。 */
#define BI_F_SELFBOOT 0x0004ULL

/* ESP に `\BTRON\NOUART` が置かれていた。
 *
 * **「シリアルが無い機械」のふりをさせる印**（規律 R31 の逆テスト）。
 * これが立っていると `machdesc_probe()` が読み取ったシリアルを捨て、
 * 記述に UART が無い機械として振る舞う。
 *
 * 実機のノート PC には 16550 が無いことがあり（詳細 F9）、そのとき
 * **既定アドレスへ書き込むと外部アボートで無言に止まる**。その経路を
 * QEMU で再現する手段が他に無い — QEMU の `virt` も `q35` も、記述から
 * シリアルを消せない。PANICTEST と同じく**カーネルもローダも作り直さず**、
 * ESP の中身だけで経路を切り替える（Phase 1 ㉙）。 */
#define BI_F_NO_UART 0x0008ULL

/* 起動時の不変条件検査（アーキ非依存。src/kernel/boot/earlytest.c）。
 *
 * **中心核が立つ前に起動文脈のまま走らせる。** 両アーキで同じ検査が
 * 同じ順で走るように 1 か所に置いてある。 */
IMPORT void boot_early_tests(void);

/* 起動時の表示（アーキ非依存。src/kernel/boot/bootlog.c）。
 * **出力の形は ctest の --expect が見ている。** */
IMPORT void boot_put_dec(UD v);
IMPORT void boot_put_hex(UD v);
IMPORT void boot_show_bootinfo(void);
IMPORT void boot_show_machdesc(void);
IMPORT void boot_draw_splash(void);

/* ------------------------------------------------------------------ */
/* カーネル側の受け取り口                                              */
/*                                                                     */
/* 起動第 1 段（物理で走っている間）に bootinfo_take() を呼ぶ。          */
/* ローダが置いた領域はカーネルのページテーブルに写像されていないので、   */
/* 内容はカーネル内の静的領域へ複製する。以後はアクセサ経由で読む。      */
/* ------------------------------------------------------------------ */

#define BOOTINFO_MAX_REGIONS 192

/* arg は start.S が受け取った第 1 引数。BootInfo でも DTB でも 0 でもよい。
 * 中身を見て判別し、複製する。戻り値は BootInfo として解釈できたか。 */
IMPORT Bool bootinfo_take(UD arg);

IMPORT CONST BootInfo *bootinfo(void);              /* 常に非 0 */
IMPORT UW bootinfo_mem_count(void);
IMPORT CONST BootMemRegion *bootinfo_mem(UW i);     /* 範囲外は 0 */

/* 条件に合う BMEM_FREE 領域を 1 つ選ぶ。
 * `after` 以降で `size` バイト以上入る最も低い領域を返す。無ければ 0。 */
IMPORT UD bootinfo_find_free(UD size, UD after);

#endif /* __BTRON_BOOTINFO_H__ */

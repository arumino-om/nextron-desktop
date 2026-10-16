/*
 * exception.c — 割込み記述子表と例外の報告 (x86_64)
 *
 * arm64 側 (src/kernel/hal/arm64/exception.c) と同じ振る舞いにする。
 * 無言のハングを作らないことが第一で、原因（どのベクタか、エラーコード、
 * RIP、ページフォルトなら CR2）を全部出して止める。
 *
 * **IDT は 2 度張る。** 第 1 段（恒等写像の上）で張るゲートはスタブの
 * 物理アドレスを指しており、上位半分へ移って恒等写像を捨てると届かなく
 * なる。boot_main が張り直す（arm64 の VBAR_EL1 と同じ事情）。
 */

#include <btron/hal.h>
#include <btron/intc.h>
#include <btron/screen.h>

IMPORT UD btron_last_syscall;

/* vectors.S。スタブは先頭から 16 バイト間隔で並んでいる。
 * **表ではなく間隔で引く** — 256 本のラベルを生成する書き方は
 * LLVM の統合アセンブラで通らなかった（vectors.S の冒頭に経緯）。 */
IMPORT B btron_vec_base[];
#define VEC_STRIDE 16U

EXPORT void btron_exception_entry(UD vec, UD err, UD rip, UD regs);

#define IDT_ENTRIES 256

/* 64bit の割込みゲート記述子。16 バイト。
 * **構造体を直接ハードウェアへ渡す形になるので並びは規定どおり。** */
typedef struct {
	UH off_lo;
	UH sel;
	UH attr; /* IST(3) | 予約(5) | type(4) | 0 | DPL(2) | P(1) */
	UH off_mid;
	UW off_hi;
	UW zero;
} IdtEntry;

typedef struct __attribute__((packed)) {
	UH limit;
	UD base;
} IdtPtr;

LOCAL IdtEntry idt[IDT_ENTRIES] __attribute__((aligned(16)));

/* type = 0xE（64bit 割込みゲート）、P = 1、DPL = 0 */
#define GATE_ATTR 0x8E00U

LOCAL inline UH read_cs(void)
{
	UH v;

	__asm__ volatile("mov %%cs, %0" : "=r"(v));
	return v;
}

LOCAL inline UD read_cr2(void)
{
	UD v;

	__asm__ volatile("mov %%cr2, %0" : "=r"(v));
	return v;
}

LOCAL void put_hex(UD v)
{
	CONST B *digits = "0123456789abcdef";
	W i;

	hal_console_puts("0x");
	for (i = 60; i >= 0; i -= 4) {
		hal_console_putc(digits[(v >> (UW)i) & 0xF]);
	}
}

EXPORT void hal_exception_init(void)
{
	/* **コードセグメントセレクタは実行時に読む。** UEFI が使っている
	 * GDT のどのエントリで動いているかは実装によって違う（0x38 の
	 * ことが多いが決め打ちにしない。規律 R19 と同じ姿勢）。 */
	UH sel = read_cs();
	IdtPtr p;
	UW i;

	for (i = 0; i < IDT_ENTRIES; i++) {
		UD h = (UD)(VP)btron_vec_base + (UD)i * VEC_STRIDE;

		idt[i].off_lo = (UH)(h & 0xFFFFULL);
		idt[i].sel = sel;
		idt[i].attr = GATE_ATTR;
		idt[i].off_mid = (UH)((h >> 16) & 0xFFFFULL);
		idt[i].off_hi = (UW)(h >> 32);
		idt[i].zero = 0;
	}

	p.limit = (UH)(sizeof(idt) - 1);
	p.base = (UD)(VP)idt;
	__asm__ volatile("lidt %0" : : "m"(p) : "memory");
}

/* ベクタの名前。原因が一目で分かるようにする。 */
LOCAL CONST B *vec_name(UD v)
{
	switch (v) {
	case 0:
		return "除算エラー (#DE)";
	case 1:
		return "デバッグ (#DB)";
	case 2:
		return "NMI";
	case 3:
		return "ブレークポイント (#BP)";
	case 4:
		return "オーバフロー (#OF)";
	case 5:
		return "範囲外 (#BR)";
	case 6:
		return "無効命令 (#UD)";
	case 7:
		return "デバイス使用不可 (#NM。CR0.TS / SSE 未許可)";
	case 8:
		return "ダブルフォルト (#DF)";
	case 10:
		return "無効 TSS (#TS)";
	case 11:
		return "セグメント不在 (#NP)";
	case 12:
		return "スタックフォルト (#SS)";
	case 13:
		return "一般保護例外 (#GP)";
	case 14:
		return "ページフォルト (#PF)";
	case 16:
		return "浮動小数点エラー (#MF)";
	case 17:
		return "アラインメント (#AC)";
	case 18:
		return "マシンチェック (#MC)";
	case 19:
		return "SIMD 浮動小数点 (#XF)";
	default:
		return "（分類なし）";
	}
}

/* ページフォルトのエラーコードを読み解く。
 * 「写像が無い」のか「権限が無い」のかで原因が全く違う。 */
LOCAL void put_pf(UD err)
{
	hal_console_puts("  ");
	hal_console_puts(((err & 0x1ULL) != 0) ? "（権限違反"
	                                       : "（写像が無い");
	hal_console_puts(((err & 0x2ULL) != 0) ? " / 書き込み" : " / 読み出し");
	if ((err & 0x4ULL) != 0) {
		hal_console_puts(" / ユーザ特権から");
	}
	if ((err & 0x8ULL) != 0) {
		hal_console_puts(" / 予約ビットが立っている");
	}
	if ((err & 0x10ULL) != 0) {
		hal_console_puts(" / 命令フェッチ");
	}
	hal_console_puts("）\n");
}

/* 例外報告の入れ子の深さ。
 *
 * **報告の途中でもう 1 度例外が起きると無限に回る。** arm64 側で
 * 実際に踏んだ罠（あちらのコメントに経緯がある）。2 度目からは何も
 * 出さずに止める。最初の報告が画面に残る。 */
LOCAL W report_depth;

EXPORT void btron_exception_entry(UD vec, UD err, UD rip, UD regs)
{
	(void)regs;

	/* 32 以降は外部割込み。中心核の割込み管理へ回す。
	 * **いまはまだ APIC を立てていないので、ここへは来ない**（T1-11 の
	 * 残り）。経路だけ先に通してある。 */
	if (vec >= 32) {
		intc_dispatch();
		return;
	}

	report_depth++;
	if (report_depth > 1) {
		for (;;) {
			__asm__ volatile("cli\n hlt");
		}
	}

	/* 画面があれば占有して緊急画面に切り替える（E24 / T4-02）。
	 * 以後の hal_console_puts() がそのままそこへ流れる。 */
	(void)screen_panic_begin("*** 例外 ***");

	hal_console_puts("\n*** 例外 ***\n");
	hal_console_puts("  SYSC    = ");
	put_hex(btron_last_syscall);
	hal_console_puts("\n  ベクタ  = ");
	put_hex(vec);
	hal_console_puts("  ");
	hal_console_puts(vec_name(vec));
	hal_console_puts("\n  エラー  = ");
	put_hex(err);
	hal_console_puts("\n  RIP     = ");
	put_hex(rip);
	if (vec == 14) {
		hal_console_puts("\n  CR2     = ");
		put_hex(read_cr2());
		hal_console_puts("\n");
		put_pf(err);
	} else {
		hal_console_puts("\n");
	}

	hal_exit(2);
}

/*
 * cpu.c — CPU の初期化と割込みの禁止・許可 (x86_64)
 *
 * arm64 側 (src/kernel/hal/arm64/cpu.c) と同じ口を x86_64 で用意する。
 *
 * ------------------------------------------------------------------------
 * なぜ自前の GDT が要るのか（踏んだ罠）
 * ------------------------------------------------------------------------
 *
 * 最初は「UEFI の GDT をそのまま使えばよい」と考えた。ロングモードで
 * 動いている時点で 64bit のコードセグメントとデータセグメントは設定済みで、
 * 特権レベルを変えないなら足りる、という理屈である。
 *
 * **これは CR3 を自前の表に差し替えた瞬間に壊れる。** UEFI の GDT は
 * ファームウェアのメモリにあり、本OS の直接マップはカーネルと早期プール
 * しか覆っていない。GDT は普段 CPU の中に取り込まれているので一見動くが、
 * **割込みや例外でゲートの CS を読み込むときに GDT を引く**ので、そこで
 * ページフォルト → GDT が引けないのでダブルフォルト → トリプルフォルトに
 * なる。症状は「出力が途中で止まり QEMU が黙って終わる」で、
 * 例外の報告すら出ない（まさに無言のハング）。
 *
 * だから**カーネルのメモリに GDT を置き、起動の両段で張り直す。**
 * 第 1 段では物理アドレス、上位半分へ移ったら上位半分のアドレスで
 * 読み込む（IDT を 2 度張るのと同じ事情）。
 *
 * ユーザ特権 (ring 3) 用の記述子も今のうちに並べておく。TSS（特権レベル
 * ごとのスタック）は EL0 を入れるときに足す（T1-11 の残り）。
 */

#include <btron/hal.h>

#define RFLAGS_IF (1ULL << 9)

LOCAL inline UD read_rflags(void)
{
	UD v;

	__asm__ volatile("pushfq\n popq %0" : "=r"(v)::"memory");
	return v;
}

LOCAL inline UD read_cr0(void)
{
	UD v;

	__asm__ volatile("mov %%cr0, %0" : "=r"(v));
	return v;
}

LOCAL inline void write_cr0(UD v)
{
	__asm__ volatile("mov %0, %%cr0" : : "r"(v) : "memory");
}

LOCAL inline UD read_cr4(void)
{
	UD v;

	__asm__ volatile("mov %%cr4, %0" : "=r"(v));
	return v;
}

LOCAL inline void write_cr4(UD v)
{
	__asm__ volatile("mov %0, %%cr4" : : "r"(v) : "memory");
}

#define CR0_EM (1ULL << 2)  /* エミュレーション（立っていると SSE が #UD） */
#define CR0_TS (1ULL << 3)  /* タスクスイッチ */
#define CR0_NE (1ULL << 5)  /* 数値例外 */
#define CR4_OSFXSR (1ULL << 9)     /* FXSAVE/FXRSTOR と SSE を許す */
#define CR4_OSXMMEXCPT (1ULL << 10) /* SSE 例外を #XF で受ける */

/* ------------------------------------------------------------------ */
/* 大域記述子表 (GDT)                                                  */
/* ------------------------------------------------------------------ */

/* セグメント記述子。64bit モードでは基底と上限は無視されるので、
 * 意味を持つのは種別・特権レベル・L ビットだけである。
 *
 *   P=1 S=1 L=1  コード (DPL 0 / DPL 3)
 *   P=1 S=1      データ (DPL 0 / DPL 3)
 */
#define GDT_CODE0 0x00209A0000000000ULL /* 実行可・読み出し可・DPL0・L=1 */
#define GDT_DATA0 0x0000920000000000ULL /* 読み書き・DPL0 */
#define GDT_CODE3 0x0020FA0000000000ULL /* DPL3 */
#define GDT_DATA3 0x0000F20000000000ULL /* DPL3 */

/* セレクタ（添字 × 8）。exception.c は実行時に CS を読むので、
 * ここを変えても IDT 側は追従する。 */
#define SEL_CODE0 0x08U
#define SEL_DATA0 0x10U

#define GDT_ENTRIES 5

LOCAL UD gdt[GDT_ENTRIES];

typedef struct __attribute__((packed)) {
	UH limit;
	UD base;
} GdtPtr;

/* GDT を張り、CS と データセグメントを自前のものに載せ替える。
 *
 * **CS は mov では変えられない。** far return (lretq) でスタックから
 * CS:RIP を取り直すのが 64bit モードでの作法。 */
LOCAL void load_gdt(void)
{
	GdtPtr p;

	gdt[0] = 0;
	gdt[1] = GDT_CODE0;
	gdt[2] = GDT_DATA0;
	gdt[3] = GDT_CODE3;
	gdt[4] = GDT_DATA3;

	p.limit = (UH)(sizeof(gdt) - 1);
	p.base = (UD)(VP)gdt;

	__asm__ volatile(
	    "lgdt %0\n"
	    /* far return で CS を載せ替える。戻り先は次の命令。 */
	    "pushq %1\n"
	    "leaq 1f(%%rip), %%rax\n"
	    "pushq %%rax\n"
	    "lretq\n"
	    "1:\n"
	    /* データセグメントも自前のものにする。64bit では基底は無視される
	     * が、セレクタが指す記述子が引けなくなると例外になるので、
	     * ファームウェアの GDT を指したままにはできない。 */
	    "movl %2, %%eax\n"
	    "movw %%ax, %%ds\n"
	    "movw %%ax, %%es\n"
	    "movw %%ax, %%ss\n"
	    "movw %%ax, %%fs\n"
	    "movw %%ax, %%gs\n"
	    :
	    : "m"(p), "i"((UD)SEL_CODE0), "i"(SEL_DATA0)
	    : "rax", "memory");
}

EXPORT void hal_cpu_init(void)
{
	UD cr0 = read_cr0();
	UD cr4 = read_cr4();

	/* **起動の両段で呼ぶ。** GDTR に入れる番地は第 1 段では物理、
	 * 上位半分へ移ったら上位半分のものでなければならない。 */
	load_gdt();

	/* SSE を使えるようにする。**arm64 で CPACR_EL1.FPEN を立てるのと
	 * 同じ理由** — カーネルが明示的に浮動小数点を使わなくても、
	 * コンパイラが構造体の複製に XMM を使い、コンテキスト切替も
	 * それらを退避する（Phase 1 の ③ と同じ罠）。 */
	cr0 &= ~CR0_EM;
	cr0 &= ~CR0_TS;
	cr0 |= CR0_NE;
	write_cr0(cr0);

	cr4 |= CR4_OSFXSR | CR4_OSXMMEXCPT;
	write_cr4(cr4);
}

EXPORT UD hal_int_disable(void)
{
	UD flags = read_rflags();

	__asm__ volatile("cli" ::: "memory");
	return flags & RFLAGS_IF;
}

EXPORT void hal_int_restore(UD state)
{
	if ((state & RFLAGS_IF) != 0) {
		__asm__ volatile("sti" ::: "memory");
	}
}

EXPORT void hal_int_enable(void)
{
	__asm__ volatile("sti" ::: "memory");
}

EXPORT Bool hal_int_enabled(void)
{
	return ((read_rflags() & RFLAGS_IF) != 0) ? True : False;
}

/* 割込みが来るまで止める。**戻ってもよい**（起こされただけ）。
 * 終了も電源断もできないときの停止ループに使う（F7 / 詳細 F9）。
 * `hal_cpu_idle` と違い、割込みを許可しない — 既に死んでいる文脈から
 * 呼ばれるため。 */
EXPORT void hal_cpu_halt(void)
{
	__asm__ volatile("hlt");
}

EXPORT void hal_cpu_idle(void)
{
	/* 割込みが来るまで止まる。**sti と hlt を続けて書くのが要点** —
	 * 間に割込みが入ると hlt したまま誰も起こしてくれないことがある
	 * （sti の直後 1 命令は割込みが入らないという規定を使う）。 */
	__asm__ volatile("sti\n hlt" ::: "memory");
}

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
#define GDT_DATA3 0x0000F20000000000ULL /* 読み書き・DPL3 */
#define GDT_CODE3 0x0020FA0000000000ULL /* 実行可・DPL3・L=1 */

/* セレクタ（添字 × 8）。exception.c は実行時に CS を読むので、
 * ここを変えても IDT 側は追従する。
 *
 * **並びは `sysret` が決めている。** STAR の上位 16bit を base として
 *
 *   syscall:  CS = base_k,      SS = base_k + 8
 *   sysret:   CS = base_u + 16, SS = base_u + 8
 *
 * と**ハードウェアが決め打ちで足す**ので、カーネルは code0, data0 の順、
 * ユーザは data3, code3 の順でなければならない（ユーザ側だけ逆になる）。
 * ここを入れ替えると sysret がデータセグメントを CS に載せようとして
 * 一般保護例外になる。L5 の 4 段目でこの順に直した。 */
#define SEL_CODE0 0x08U
#define SEL_DATA0 0x10U
#define SEL_DATA3 0x18U
#define SEL_CODE3 0x20U
#define SEL_TSS 0x28U

/* null, code0, data0, data3, code3, TSS(16 バイトなので 2 枠) */
#define GDT_ENTRIES 7

LOCAL UD gdt[GDT_ENTRIES];

/* ------------------------------------------------------------------ */
/* TSS と per-CPU 領域 (L5 の 4 段目)                                   */
/*                                                                     */
/* **arm64 には要らないもの。** あちらは SP_EL1 / SP_EL0 が別のレジスタ  */
/* なので、EL0 から例外が入れば自動でカーネルスタックに切り替わる。      */
/* x86 は「どのスタックに切り替えるか」を TSS の RSP0 に書いておく       */
/* 必要があり、しかも `syscall` 命令は**スタックを切り替えない**ので、    */
/* ソフトウェアが自分で載せ替える。その置き場が per-CPU 領域で、         */
/* KERNEL_GS_BASE から %gs 経由で引く。                                 */
/* ------------------------------------------------------------------ */

/* 64bit TSS。使うのは RSP0 だけだが、長さは規格どおり 104 バイト必要
 * （短いと LTR が一般保護例外になる）。 */
typedef struct __attribute__((packed)) {
	UW resv0;
	UD rsp0;
	UD rsp1;
	UD rsp2;
	UD resv1;
	UD ist[7];
	UD resv2;
	UH resv3;
	UH iomap_base;
} Tss;

LOCAL Tss tss __attribute__((aligned(16)));

/* per-CPU 領域。**並びは syscall.S が番地で読む**ので変えてはならない。 */
typedef struct __attribute__((packed)) {
	UD kernel_rsp; /* +0  いまのタスクのカーネルスタックの先頭 */
	UD user_rsp;   /* +8  入口でユーザの RSP を置く場所 */
} PerCpu;

LOCAL PerCpu percpu __attribute__((aligned(16)));

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
	/* **ユーザ側は data, code の順**（上の SEL_* の注記。sysret の要求）。 */
	gdt[3] = GDT_DATA3;
	gdt[4] = GDT_CODE3;
	/* TSS 記述子は 16 バイト。型 0x9 = 使用可能な 64bit TSS。 */
	{
		UD base = (UD)(VP)&tss;
		UD limit = sizeof(tss) - 1;

		gdt[5] = (limit & 0xFFFFULL) | ((base & 0xFFFFFFULL) << 16)
		         | (0x89ULL << 40) | (((base >> 24) & 0xFFULL) << 56);
		gdt[6] = (base >> 32) & 0xFFFFFFFFULL;
	}

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

/* TSS を載せる。RSP0 は最初のタスクが ring 3 へ降りるときに入る。 */
LOCAL void load_tss(void)
{
	tss.iomap_base = (UH)sizeof(tss); /* I/O 許可ビットマップは持たない */
	__asm__ volatile("ltr %w0" : : "r"((UH)SEL_TSS));
}

/* MSR の読み書き。 */
LOCAL inline void wrmsr(UW msr, UD v)
{
	__asm__ volatile("wrmsr"
	                 :
	                 : "c"(msr), "a"((UW)(v & 0xFFFFFFFFU)),
	                   "d"((UW)(v >> 32)));
}

LOCAL inline UD rdmsr(UW msr)
{
	UW lo;
	UW hi;

	__asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
	return ((UD)hi << 32) | lo;
}

#define MSR_EFER 0xC0000080U
#define MSR_STAR 0xC0000081U
#define MSR_LSTAR 0xC0000082U
#define MSR_SFMASK 0xC0000084U
#define MSR_GS_BASE 0xC0000101U
#define MSR_KERNEL_GS_BASE 0xC0000102U

#define EFER_SCE (1ULL << 0) /* syscall / sysret を使えるようにする */

IMPORT void btron_syscall_stub(void);

/* 下で定義する。setup_syscall が初期値を入れるのに使う。 */
EXPORT void hal_gs_to_kernel(void);

/* `syscall` 命令の経路を立てる。
 *
 * **arm64 の svc には設定が要らない。** あちらは例外ベクタの一部で、
 * VBAR_EL1 を立てた時点で使える。x86 の syscall は「どこへ飛ぶか」
 * （LSTAR）「どのセグメントになるか」（STAR）「どのフラグを落とすか」
 * （SFMASK）を MSR で与える必要がある。
 *
 * SFMASK で IF を落とすのは arm64 に合わせるため —— あちらは例外を
 * 取った時点で PSTATE.I が立つ（割込み禁止）ので、システムコールの
 * 本体は割込み禁止で走る。ここも同じにする。 */
LOCAL void setup_syscall(void)
{
	wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_SCE);
	/* STAR[47:32] = カーネルの base、STAR[63:48] = ユーザの base。
	 * ハードウェアが +8 / +16 を足すので、上の SEL_* の並びが要る。 */
	wrmsr(MSR_STAR, ((UD)SEL_DATA0 << 48) | ((UD)SEL_CODE0 << 32));
	wrmsr(MSR_LSTAR, (UD)(VP)btron_syscall_stub);
	wrmsr(MSR_SFMASK, RFLAGS_IF);
	/* 不変条件の初期値を入れる（hal_gs_to_kernel と同じ状態）。
	 * いまはカーネルの中なので GS_BASE 側が per-CPU 領域である。 */
	hal_gs_to_kernel();
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

	load_tss();
	setup_syscall();
}

/* いまのタスクのカーネルスタックの先頭を覚える (L5 の 4 段目)。
 *
 * **2 か所に同じ値を書く。** 割込み・例外で ring 3 から入るときは
 * ハードウェアが TSS の RSP0 を見て切り替える。`syscall` 命令は
 * 切り替えないので、入口が %gs 経由で同じ値を引く。
 *
 * 呼ぶのは「カーネルから ring 3 へ出る直前」の 2 経路だけである
 * （btron_user_trampoline と、割込みから ring 3 へ戻る iretq の手前）。
 * そこでの RSP が、そのタスクのカーネルスタックの使い始めになる。 */
/* %gs をカーネル側に据え直す (L5 の 4 段目)。
 *
 * **`swapgs` の入れ替えはタスクをまたいで釣り合わない。** 入れ替えの状態は
 * CPU ごとに 1 つしか無いのに、「カーネルの中か ring 3 か」はタスクごとの
 * 性質である。カーネルがシステムコールの中でブロックし、別のタスクが
 * ring 3 へ戻る（そこで swapgs する）と、最初のタスクから見た入れ替えの
 * 回数が奇数回ずれる。そのタスクが後で出口の swapgs を通ると、
 * **ring 3 がカーネル側の %gs を持ったまま走り**、次の syscall の入口で
 * `movq %rsp, %gs:8` が 0 + 8 番地への書き込みになる。
 *
 * そこで釣り合いに頼らず、**不変条件で持つ**ことにした。
 *
 *   カーネルの中にいる間   GS_BASE = per-CPU 領域、KERNEL_GS_BASE = 0
 *   ring 3 にいる間        GS_BASE = 0、KERNEL_GS_BASE = per-CPU 領域
 *
 * 入口と出口の swapgs がこの 2 状態を行き来し、**タスクを切り替えた直後に
 * ここで据え直す**（hal_ctx_switch から呼ぶ）。arm64 には要らない —— あちらは
 * カーネルスタックが SP_EL1 として独立しているので、per-CPU の置き場を
 * 自前で引く必要がない。 */
EXPORT void hal_gs_to_kernel(void)
{
	wrmsr(MSR_GS_BASE, (UD)(VP)&percpu);
	wrmsr(MSR_KERNEL_GS_BASE, 0);
}

EXPORT void hal_ctx_set_kernel_sp(UD top)
{
	tss.rsp0 = top;
	percpu.kernel_rsp = top;
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

/*
 * memory.c — 周辺核 §1.5「メモリ管理」
 *
 * get_mbk / rel_mbk / mbk_sts。ブロック単位（= ページ単位）で配る。
 *
 * ------------------------------------------------------------------------
 * 3 種類の領域をアドレス空間のどこへ置くか
 * ------------------------------------------------------------------------
 *
 * §1.5.1「メモリ管理機能のある CPU の場合には、原則として、共有メモリ、
 * システムメモリは負のアドレスとなり、ローカルメモリは正のアドレスとなる」。
 * §3.2 のレイアウトがこれをそのまま満たしている。
 *
 *   ローカル   BTRON_PROC_LOCAL_BASE  下位半分 = 正。プロセスごとの空間へ張る
 *   共有       BTRON_COMMON_BASE      上位半分 = 負。全プロセス共通
 *   システム   BTRON_SYSTEM_BASE      上位半分 = 負。EL0 / ring3 からは不可
 *
 * 上位半分は全プロセスで共通なので（arm64 は TTBR1 が切り替わらない、
 * x86_64 は上位 256 PML4 エントリを複製。規律 R4）、1 回張れば全プロセスから
 * 同じアドレスで見える。**共有メモリの実装がこれだけで済むのが、
 * 「共有は負のアドレス」という仕様の配置を守った見返りである。**
 *
 * 保護の違いはページ属性で出す。
 *
 *   ローカル   READ|WRITE|USER  そのプロセスの空間にしか存在しない
 *   共有       READ|WRITE|USER  上位半分だが EL0 から触れる
 *   システム   READ|WRITE       USER を付けない = EL1 専用
 *
 * どれにも EXEC を付けない。§1.5.1「原則として実行することはできない」。
 *
 * ------------------------------------------------------------------------
 * 物理の連続は要求されていない
 * ------------------------------------------------------------------------
 *
 * 仕様が求めているのは「連続した（論理）アドレス」だけである。だから
 * **1 ブロックずつ物理ページを取って張る。** 物理で連続を要求すると、
 * 断片化したときに大きな要求が通らなくなる。MMU がある意味がここで出る。
 *
 * ------------------------------------------------------------------------
 * 既知の限界
 * ------------------------------------------------------------------------
 *
 * ページテーブルの**中間段は解放していない**。解放するには段ごとの
 * 参照数が必要で、いまは持っていない。同じ仮想アドレスを再利用する限り
 * 増えないので、実害が出るのは「広い範囲に散らして取って返す」使い方に
 * なってからである。そのときに参照数を足す。
 */

#include <btron/memory.h>
#include <btron/proc.h>

#include "../core/knl.h"

/* 領域の種別。属性の値そのままではなく内部の区分。 */
#define MBK_LOCAL 0
#define MBK_COMMON 1
#define MBK_SYSTEM 2

/* 属性として受け付けるビット。これ以外が立っていたら ER_PAR。 */
#define MBK_ATR_VALID (M_SYSTEM | M_RESIDENT | (UW)DELEXIT)

typedef struct MBK {
	UD va;    /* 先頭仮想アドレス */
	UW nblk;  /* ブロック数 */
	UW atr;   /* 獲得時の属性 */
	W owner;  /* 獲得したプロセスの PID。0 は「もう居ない」 */
	W kind;   /* MBK_* */
	Bool used;
} MBK;

LOCAL MBK mbk[MBK_MAX];

/* ------------------------------------------------------------------ */
/* 領域                                                                */
/* ------------------------------------------------------------------ */

LOCAL void region_of(W kind, UD *base, UD *limit)
{
	switch (kind) {
	case MBK_COMMON:
		*base = BTRON_COMMON_BASE;
		*limit = BTRON_SYSTEM_BASE;
		break;
	case MBK_SYSTEM:
		*base = BTRON_SYSTEM_BASE;
		*limit = BTRON_MAPREC_BASE;
		break;
	default:
		*base = BTRON_PROC_LOCAL_BASE;
		*limit = BTRON_PROC_LOCAL_LIMIT;
		break;
	}
}

/* 同じ仮想アドレス空間を共有している記録か。
 *
 * ローカルメモリは**プロセスごとに別の空間**なので、別のプロセスの
 * ローカル記録と仮想アドレスが重なっていても構わない。所有者で絞る。 */
LOCAL Bool same_space(CONST MBK *m, W kind, W owner)
{
	if (!m->used || m->kind != kind) {
		return False;
	}
	return (kind == MBK_LOCAL) ? ((m->owner == owner) ? True : False) : True;
}

/* nblk 個分の空いた仮想アドレスを探す。見つからなければ 0。
 *
 * 生きている記録と重なる間だけ後ろへ押し出す。押し出しは単調増加で
 * 上限で止まるので必ず終わる。結果は**最も低い空き**になる（それより
 * 低い場所は重なりで弾かれている）。記録は最大 MBK_MAX 個なので、
 * 素朴な走査で足りる。 */
LOCAL UD alloc_va(W kind, W owner, UW nblk)
{
	UD ps = (UD)hal_page_size();
	UD size = (UD)nblk * ps;
	UD base;
	UD limit;
	UD cand;
	Bool moved;
	W i;

	region_of(kind, &base, &limit);
	cand = base;
	do {
		moved = False;
		for (i = 0; i < MBK_MAX; i++) {
			UD mb;
			UD me;

			if (!same_space(&mbk[i], kind, owner)) {
				continue;
			}
			mb = mbk[i].va;
			me = mb + (UD)mbk[i].nblk * ps;
			if (cand < me && mb < cand + size) {
				cand = me;
				moved = True;
			}
		}
	} while (moved);

	if (cand < base || cand + size > limit || cand + size < cand) {
		return 0;
	}
	return cand;
}

/* ------------------------------------------------------------------ */
/* 写像                                                                */
/* ------------------------------------------------------------------ */

LOCAL UW attr_of(W kind)
{
	UW a = BTRON_PG_READ | BTRON_PG_WRITE;

	/* §1.5.1「原則として実行することはできない」ので EXEC は付けない。
	 * システムメモリだけ USER を付けない = EL0 / ring3 から触れない。 */
	if (kind != MBK_SYSTEM) {
		a |= BTRON_PG_USER;
	}
	return a;
}

LOCAL ERR map_block(W kind, AddrSpace *as, UD va, UD pa)
{
	UD ps = (UD)hal_page_size();

	if (kind == MBK_LOCAL) {
		return hal_vm_map(as, va, pa, ps, attr_of(kind));
	}
	return hal_vm_map_shared(va, pa, ps, attr_of(kind));
}

LOCAL UD unmap_block(W kind, AddrSpace *as, UD va)
{
	UD ps = (UD)hal_page_size();
	UD pa;

	if (kind == MBK_LOCAL) {
		if (as == 0) {
			return 0;
		}
		pa = hal_vm_translate(as, va);
		(void)hal_vm_unmap(as, va, ps);
	} else {
		pa = hal_vm_translate(hal_vm_kernel_space(), va);
		(void)hal_vm_unmap_shared(va, ps);
	}
	return pa & ~(ps - 1);
}

/* ブロック群を解放する。記録も空ける。 */
LOCAL void free_blocks(MBK *m)
{
	UD ps = (UD)hal_page_size();
	AddrSpace *as = (m->kind == MBK_LOCAL) ? prc_space(m->owner) : 0;
	UW k;

	for (k = 0; k < m->nblk; k++) {
		UD pa = unmap_block(m->kind, as, m->va + (UD)k * ps);

		if (pa != 0) {
			pmem_free(pa, 1);
		}
	}
	m->used = False;
	m->va = 0;
	m->nblk = 0;
	m->atr = 0;
	m->owner = 0;
	m->kind = MBK_LOCAL;
}

/* ------------------------------------------------------------------ */
/* システムコール                                                      */
/* ------------------------------------------------------------------ */

/* 属性から内部の種別を決める。不正なら -1。
 *
 * **M_SYSTEM (3) は M_COMMON (1) のビットを含む。** だから値で 3 分岐する。
 * 0x2 は「M_COMMON のビットが無いのに上のビットだけ立っている」ので不正。 */
LOCAL W kind_of(UW atr)
{
	switch (atr & M_SYSTEM) {
	case 0:
		return MBK_LOCAL;
	case M_COMMON:
		return MBK_COMMON;
	case M_SYSTEM:
		return MBK_SYSTEM;
	default:
		return -1;
	}
}

/* システムプロセスか。
 *
 * **仕様はシステムプロセスの判定基準を書いていない。** §1.1.5 の
 * ユーザレベル 0（最高）をシステムプロセスとする。chg_usr の判定と同じ。 */
LOCAL Bool is_system_process(void)
{
	P_USER u;

	if (get_usr(P_SELF, &u) < 1) {
		return False;
	}
	return (u.level == P_LEVEL_MAX) ? True : False;
}

EXPORT ERR get_mbk(VP *adr, W nblk, UW atr)
{
	UD ps = (UD)hal_page_size();
	W pid = prc_self();
	W kind;
	MBK *m = 0;
	AddrSpace *as = 0;
	UD va = 0;
	UD state;
	UW k;
	W i;

	if (adr == 0) {
		return ER_ADR;
	}
	if (nblk <= 0) {
		return ER_PAR;
	}
	if ((atr & ~MBK_ATR_VALID) != 0) {
		return ER_PAR;
	}
	kind = kind_of(atr);
	if (kind < 0) {
		return ER_PAR;
	}
	/* 仕様: 「不正なコンテキストから呼び出した」。プロセスに属さない
	 * タスク（中心核だけのタスク・タスク独立部）はローカルも共有も
	 * 持ち主になれない。カーネル自身は kmalloc を使う。 */
	if (pid < 1) {
		return ER_CTX;
	}
	/* 仕様: 「この指定は、アプリケーションプロセスからは使用してはいけない」。
	 * 仕様のエラー一覧に権限用のコードが無いので ER_PAR を返す。 */
	if (kind == MBK_SYSTEM && !is_system_process()) {
		return ER_PAR;
	}
	if (kind == MBK_LOCAL) {
		as = prc_space(pid);
		if (as == 0) {
			return ER_NOMEM; /* アドレス空間を持たないプロセス */
		}
	}

	/* 記録と仮想アドレスを一息に押さえる。先に押さえておけば、
	 * 以降の物理ページの確保と写像は割込みを許したまま行える。 */
	state = hal_int_disable();
	for (i = 0; i < MBK_MAX; i++) {
		if (!mbk[i].used) {
			m = &mbk[i];
			break;
		}
	}
	if (m != 0) {
		va = alloc_va(kind, pid, (UW)nblk);
		if (va != 0) {
			m->used = True;
			m->va = va;
			m->nblk = (UW)nblk;
			m->atr = atr;
			m->owner = pid;
			m->kind = kind;
		} else {
			m = 0;
		}
	}
	hal_int_restore(state);
	if (m == 0) {
		return ER_NOSPC; /* 記録か仮想アドレスが尽きた */
	}

	/* **1 ブロックずつ取って張る。** 仕様が要求しているのは論理アドレスの
	 * 連続だけなので、物理の連続を求めて断片化で失敗する必要はない。 */
	for (k = 0; k < (UW)nblk; k++) {
		UD pa = pmem_alloc(1);

		if (pa == 0 ||
		    map_block(kind, as, va + (UD)k * ps, pa) != 0) {
			if (pa != 0) {
				pmem_free(pa, 1);
			}
			/* ここまでに張った分を巻き戻す。中途半端な写像を残さない。 */
			m->nblk = k;
			free_blocks(m);
			return ER_NOMEM;
		}
	}
	*adr = (VP)(UD)va;
	return ER_OK;
}

EXPORT ERR rel_mbk(VP adr)
{
	W pid = prc_self();
	UD va = (UD)adr;
	MBK *m = 0;
	UD state;
	W i;

	if (pid < 1) {
		return ER_CTX;
	}
	state = hal_int_disable();
	for (i = 0; i < MBK_MAX; i++) {
		if (!mbk[i].used || mbk[i].va != va) {
			continue;
		}
		/* 他のプロセスのローカルメモリは触れない（別の空間にある同じ
		 * 仮想アドレスなので、間違って解放すると相手が壊れる）。
		 * 共有・システムは「獲得したプロセスの存在とは無関係に存在する」
		 * ので、どのプロセスからでも解放できる。 */
		if (mbk[i].kind == MBK_LOCAL && mbk[i].owner != pid) {
			continue;
		}
		m = &mbk[i];
		break;
	}
	hal_int_restore(state);
	if (m == 0) {
		return ER_MPTR; /* get_mbk() で得たアドレスではない */
	}
	free_blocks(m);
	return ER_OK;
}

EXPORT ERR mbk_sts(M_STATE *sts)
{
	UD total = pmem_total_pages();
	UD free = pmem_free_pages();

	if (sts == 0) {
		return ER_ADR;
	}
	sts->blksz = (W)hal_page_size();
	if (total > 0x7FFFFFFFULL) {
		/* 仕様: 「具体的な値が設定できない場合は、全ブロック数、
		 * 残りブロック数ともに -1 を設定するものとする」 */
		sts->total = -1;
		sts->free = -1;
		return ER_OK;
	}
	sts->total = (W)total;
	sts->free = (W)free;
	return ER_OK;
}

/* ------------------------------------------------------------------ */
/* プロセス終了時の自動解放                                            */
/* ------------------------------------------------------------------ */

EXPORT void mbk_release_process(W pid)
{
	W i;

	if (pid < 1) {
		return;
	}
	for (i = 0; i < MBK_MAX; i++) {
		MBK *m = &mbk[i];

		if (!m->used || m->owner != pid) {
			continue;
		}
		/* 仕様: 「ローカルメモリとして確保した領域は、プロセス終了時に
		 * 自動的に解放される」「ただし、ローカルメモリの場合は、
		 * この指定の有無に関係なく、プロセスの終了時に解放される」。
		 * 共有・システムは DELEXIT が付いていれば解放する。 */
		if (m->kind != MBK_LOCAL && (m->atr & (UW)DELEXIT) == 0) {
			/* 「獲得したプロセスの存在とは無関係に存在する」。
			 * 持ち主だけ外す（もう居ないプロセスを指し続けないため）。 */
			m->owner = 0;
			continue;
		}
		free_blocks(m);
	}
}

/* ------------------------------------------------------------------ */
/* 検証用・初期化                                                      */
/* ------------------------------------------------------------------ */

EXPORT W mbk_count(void)
{
	W n = 0;
	W i;

	for (i = 0; i < MBK_MAX; i++) {
		if (mbk[i].used) {
			n++;
		}
	}
	return n;
}

EXPORT W mbk_blocks(void)
{
	W n = 0;
	W i;

	for (i = 0; i < MBK_MAX; i++) {
		if (mbk[i].used) {
			n += (W)mbk[i].nblk;
		}
	}
	return n;
}

EXPORT ERR mbk_init(void)
{
	W i;

	for (i = 0; i < MBK_MAX; i++) {
		mbk[i].used = False;
		mbk[i].va = 0;
		mbk[i].nblk = 0;
		mbk[i].atr = 0;
		mbk[i].owner = 0;
		mbk[i].kind = MBK_LOCAL;
	}
	return ER_OK;
}

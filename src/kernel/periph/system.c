/*
 * system.c — 周辺核 §1.10「システム管理機能」
 *
 * get_ver / get_cnf。
 *
 * 例外処理ハンドラ (`def_exc` / `ret_exc`) は中心核のタスク例外 (T1-14) と
 * 同じ機構なので、そちらと一緒に入れる。`hal_ctx_inject()` を §1.2 の
 * メッセージハンドラのために用意したので土台はできている。
 *
 * ------------------------------------------------------------------------
 * get_cnf の項目
 * ------------------------------------------------------------------------
 *
 * 仕様は項目名を「インプリメントに依存する」としている。本OS は
 * **すでに持っている情報だけを出す** — ハードウェア記述 (MachDesc)、
 * 物理メモリ、画面。決め打ちの値を返す項目は作らない（規律 R19 の精神）。
 */

#include <btron/machdesc.h>
#include <btron/screen.h>
#include <btron/system.h>

#include "../core/knl.h"

/* ------------------------------------------------------------------ */
/* バージョン                                                          */
/* ------------------------------------------------------------------ */

EXPORT ERR get_ver(T_VER *version)
{
	W i;

	if (version == 0) {
		return ER_ADR;
	}
	version->maker = OS_MAKER;
	version->id = OS_ID;
	version->spver = OS_SPVER;
	version->prver = OS_PRVER;
	for (i = 0; i < 4; i++) {
		version->prno[i] = 0;
	}
#if defined(__aarch64__)
	version->cpu = OS_CPU_ARM64;
#elif defined(__x86_64__)
	version->cpu = OS_CPU_X86_64;
#else
	version->cpu = 0;
#endif
	version->var = 0;
	return ER_OK;
}

/* ------------------------------------------------------------------ */
/* コンフィグレーション情報                                            */
/* ------------------------------------------------------------------ */

#define CNF_NUM 0 /* W の数値 */
#define CNF_STR 1 /* TC 文字列 */

typedef struct CnfItem {
	CONST B *name; /* ASCII で書いておき、比較時に TC へ広げる */
	W kind;
	W (*num)(void);
	CONST B *str;
} CnfItem;

LOCAL W cnf_cpunum(void)
{
	return (W)machdesc()->cpu_count;
}

LOCAL W cnf_pagesz(void)
{
	return (W)hal_page_size();
}

LOCAL W cnf_memblk(void)
{
	UD n = pmem_total_pages();

	return (n > 0x7FFFFFFFULL) ? -1 : (W)n;
}

LOCAL W cnf_intcver(void)
{
	return (W)hal_intc_version();
}

LOCAL W cnf_screenw(void)
{
	CONST Screen *s = screen();

	return (s != 0) ? (W)s->width : 0; /* 画面が無い構成では 0 */
}

LOCAL W cnf_screenh(void)
{
	CONST Screen *s = screen();

	return (s != 0) ? (W)s->height : 0;
}

LOCAL W cnf_tickhz(void)
{
	return 1000 / ((W)hal_timer_tick_ms() > 0 ? (W)hal_timer_tick_ms() : 1);
}

LOCAL CONST CnfItem cnf_tbl[] = {
	{"OSNAME", CNF_STR, 0, "BTRON3"},
	{"CPUNUM", CNF_NUM, cnf_cpunum, 0},
	{"PAGESZ", CNF_NUM, cnf_pagesz, 0},
	{"MEMBLK", CNF_NUM, cnf_memblk, 0},
	{"INTCVER", CNF_NUM, cnf_intcver, 0},
	{"SCREENW", CNF_NUM, cnf_screenw, 0},
	{"SCREENH", CNF_NUM, cnf_screenh, 0},
	{"TICKHZ", CNF_NUM, cnf_tickhz, 0},
};

#define CNF_COUNT ((W)(sizeof(cnf_tbl) / sizeof(cnf_tbl[0])))

/* TC の項目名と ASCII の表を比べる。項目名は ASCII の範囲しか使わない。 */
LOCAL Bool name_eq(CONST TC *name, CONST B *ascii)
{
	W i;

	for (i = 0; i < CNF_NAMELEN; i++) {
		TC c = name[i];
		TC a = (TC)(UB)ascii[i];

		if (c != a) {
			return False;
		}
		if (c == TNULL) {
			return True;
		}
	}
	return (ascii[CNF_NAMELEN] == 0) ? True : False;
}

EXPORT WERR get_cnf(CONST TC *name, VP value, W len)
{
	W i;

	if (name == 0) {
		return ER_ADR;
	}
	for (i = 0; i < CNF_COUNT; i++) {
		CONST CnfItem *it = &cnf_tbl[i];

		if (!name_eq(name, it->name)) {
			continue;
		}
		if (it->kind == CNF_NUM) {
			if (value == 0 || len < (W)sizeof(W)) {
				return ER_PAR;
			}
			*(W *)value = it->num();
			return 0; /* 仕様: 戻り値 = 0 は W の数値情報 */
		}
		/* TC 文字列。文字数を返し、最後に 0 を格納する。
		 * 仕様「格納領域が不足するときは格納領域のサイズ分のみ格納される」 */
		{
			W n = 0;
			W room;
			TC *dst = (TC *)value;

			while (it->str[n] != 0) {
				n++;
			}
			if (value == 0 || len < (W)sizeof(TC)) {
				return ER_PAR;
			}
			room = len / (W)sizeof(TC);
			{
				W k;

				for (k = 0; k < n && k < room - 1; k++) {
					dst[k] = (TC)(UB)it->str[k];
				}
				dst[k] = TNULL;
			}
			return n;
		}
	}
	return ER_NOEXS; /* 項目名は存在していない */
}

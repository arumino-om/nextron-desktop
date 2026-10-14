/*
 * syscall.c — ユーザ特権 (EL0) からのシステムコールの受け口
 *
 * 約束は include/btron/syscall.h に書いた。ここは「番号から関数へ配る」
 * ことと「ユーザから来たポインタを検める」ことの 2 つだけをする。
 *
 * ------------------------------------------------------------------------
 * なぜ分厚い表を書くのか
 * ------------------------------------------------------------------------
 *
 * 関数ポインタの配列にして一律に呼べば短くなるが、**引数の型と個数が
 * 呼び出しごとに違う**ので、それをやると「どのポインタを検めるべきか」が
 * 表から消えてしまう。検めを忘れた 1 本が、カーネル全体の穴になる。
 * 1 本ずつ書くのは冗長だが、検めが目に見える。
 */

#include <btron/syscall.h>

#include <btron/clock.h>
#include <btron/device.h>
#include <btron/event.h>
#include <btron/file.h>
#include <btron/hal.h>
#include <btron/memory.h>
#include <btron/message.h>
#include <btron/gname.h>
#include <btron/proc.h>
#include <btron/screen.h>
#include <btron/taskcomm.h>
#include <btron/system.h>
#include <btron/vmem.h>

/* ------------------------------------------------------------------ */
/* ユーザポインタの検め                                                */
/* ------------------------------------------------------------------ */

EXPORT Bool usr_ok(UD p, W size, Bool write)
{
	CONST AddrSpace *as;
	UD ps = (UD)hal_page_size();
	UD a;
	UD end;

	if (p == 0) {
		return False;
	}
	/* **上位半分は問答無用で断る。** カーネル・共有メモリ・MMIO の
	 * 別名がすべてそこにある（vmem.h の区画表）。 */
	if (p >= BTRON_USER_LIMIT) {
		return False;
	}
	if (size < 0) {
		return False;
	}
	end = p + (UD)size;
	if (end < p || end > BTRON_USER_LIMIT) {
		return False; /* 折り返しと、はみ出し */
	}
	as = prc_space(prc_self());
	if (as == 0) {
		return False;
	}
	/* 写像されているかをページごとに見る。
	 * **書き込みの可否までは hal_vm_translate では分からない。**
	 * 読み出し専用のページへ書くと EL1 でフォルトし、カーネルの例外に
	 * なってしまう。いまは「写像されていること」までを確かめ、
	 * 書き込み権の検査は hal_vm_protect ができるまで持ち越す。 */
	(void)write;
	for (a = p & ~(ps - 1); a < end; a += ps) {
		if (hal_vm_translate(as, a) == 0) {
			return False;
		}
	}
	return True;
}

/* 文字列（TC 列）の検め。終端まで写像されていることを確かめる。 */
LOCAL Bool usr_ok_tc(UD p, W max)
{
	UD ps = (UD)hal_page_size();
	CONST TC *s;
	W i;

	if (!usr_ok(p, 2, False)) {
		return False;
	}
	s = (CONST TC *)(VP)p;
	for (i = 0; i < max; i++) {
		UD a = p + (UD)i * 2;

		/* ページをまたぐたびに写像を確かめ直す。 */
		if ((a & (ps - 1)) == 0 && !usr_ok(a, 2, False)) {
			return False;
		}
		if (s[i] == TNULL) {
			return True;
		}
	}
	return False; /* 終端が見つからない */
}

/* ------------------------------------------------------------------ */
/* EL0 からの、システムコール以外の同期例外                            */
/* ------------------------------------------------------------------ */

LOCAL void put_hex(UD v)
{
	CONST B *digits = "0123456789abcdef";
	W i;

	hal_console_puts("0x");
	for (i = 60; i >= 0; i -= 4) {
		hal_console_putc(digits[(v >> i) & 0xFULL]);
	}
}

EXPORT void hal_user_fault(UD esr, UD far, UD elr)
{
	W me = prc_self();

	hal_console_puts("\n*** ユーザプログラムの異常 ***\n  プロセス ");
	put_hex((UD)(UW)me);
	hal_console_puts("\n  ESR = ");
	put_hex(esr);
	hal_console_puts("\n  FAR = ");
	put_hex(far);
	hal_console_puts("\n  PC  = ");
	put_hex(elr);
	hal_console_puts("\n");
	/* **カーネルごと止めない。** そのプロセスだけを落とす。
	 * 保護の意味はここにある。 */
	if (me >= 1) {
		(void)prc_abort(me, -1);
	}
	/* prc_abort が戻ってきた = 落とせなかった。呼び出し元が
	 * カーネルの例外として扱う。 */
}

/* ------------------------------------------------------------------ */
/* 配り先                                                              */
/* ------------------------------------------------------------------ */

#define A(n) ((W)(W)(UW)(a##n))
#define P(n) ((UD)(a##n))

/* 最後に入ったシステムコールの番号。
 *
 * **カーネルが落ちたときに「どの呼び出しの最中だったか」が分かる。**
 * ユーザから来たポインタを触るのはシステムコールの中だけなので、
 * 例外の原因を探すとき最初に見たい情報がこれになる。実際、
 * ランデブがアドレス空間をまたぐ問題はこの 1 語で切り分けられた。 */
EXPORT UD btron_last_syscall;

EXPORT D btron_syscall_entry(UD a0, UD a1, UD a2, UD a3, UD a4, UD a5, UD num)
{
	btron_last_syscall = num;
	switch (num) {
	/* --- §1.1 プロセス / タスク管理 --- */
	case SYS_ext_prc:
		ext_prc(A(0));
		return 0; /* 戻らない */
	case SYS_ter_prc:
		return ter_prc(A(0), A(1), A(2));
	case SYS_chg_pri:
		return chg_pri(A(0), A(1), A(2));
	case SYS_prc_sts:
		if (a1 != 0 && !usr_ok(P(1), (W)sizeof(P_STATE), True)) {
			return ER_ADR;
		}
		if (a2 != 0 && !usr_ok(P(2), (P_NAMELEN + 1) * 2, True)) {
			return ER_ADR;
		}
		return prc_sts(A(0), (P_STATE *)(VP)a1, (TC *)(VP)a2);
	case SYS_get_usr:
		if (a1 != 0 && !usr_ok(P(1), (W)sizeof(P_USER), True)) {
			return ER_ADR;
		}
		return get_usr(A(0), (P_USER *)(VP)a1);
	case SYS_slp_tsk:
		return slp_tsk(A(0));
	case SYS_wup_tsk:
		return wup_tsk(A(0));
	case SYS_dly_tsk:
		return dly_tsk(A(0));
	case SYS_get_tid:
		return get_tid();
	case SYS_cre_prc:
		if (!usr_ok(P(0), (W)sizeof(LINK), False)) {
			return ER_ADR;
		}
		if (a2 != 0 && !usr_ok(P(2), MSG_HEADER_SIZE, False)) {
			return ER_ADR;
		}
		return cre_prc((CONST LINK *)(CONST VP)a0, A(1),
		               (CONST MESSAGE *)(CONST VP)a2);

	/* --- §1.2 メッセージ管理 --- */
	case SYS_snd_msg:
		if (!usr_ok(P(1), MSG_HEADER_SIZE, False)) {
			return ER_ADR;
		}
		return snd_msg(A(0), (CONST MESSAGE *)(CONST VP)a1, A(2));
	case SYS_rcv_msg:
		if (!usr_ok(P(1), A(2), True)) {
			return ER_ADR;
		}
		return rcv_msg(A(0), (MESSAGE *)(VP)a1, A(2), A(3));
	case SYS_clr_msg:
		return clr_msg(A(0), A(1));

	/* --- §1.5 メモリ管理 --- */
	case SYS_get_mbk:
		if (!usr_ok(P(0), (W)sizeof(VP), True)) {
			return ER_ADR;
		}
		return get_mbk((VP *)(VP)a0, A(1), (UW)(a2 & 0xFFFFFFFFULL));
	case SYS_rel_mbk:
		return rel_mbk((VP)a0);
	case SYS_mbk_sts:
		if (!usr_ok(P(0), (W)sizeof(M_STATE), True)) {
			return ER_ADR;
		}
		return mbk_sts((M_STATE *)(VP)a0);

	/* --- §1.9 / §1.10 --- */
	case SYS_get_tim:
		if (a0 != 0 && !usr_ok(P(0), (W)sizeof(STIME), True)) {
			return ER_ADR;
		}
		if (a1 != 0 && !usr_ok(P(1), (W)sizeof(TIMEZONE), True)) {
			return ER_ADR;
		}
		return get_tim((STIME *)(VP)a0, (TIMEZONE *)(VP)a1);
	case SYS_set_tim:
		if (!usr_ok(P(0), (W)sizeof(STIME), False)) {
			return ER_ADR;
		}
		return set_tim(*(CONST STIME *)(CONST VP)a0,
		               (CONST TIMEZONE *)(CONST VP)a1);
	case SYS_get_ver:
		if (!usr_ok(P(0), (W)sizeof(T_VER), True)) {
			return ER_ADR;
		}
		return get_ver((T_VER *)(VP)a0);
	case SYS_get_cnf:
		if (!usr_ok_tc(P(0), CNF_NAMELEN + 1)) {
			return ER_ADR;
		}
		if (!usr_ok(P(1), A(2) * (W)sizeof(W), True)) {
			return ER_ADR;
		}
		return get_cnf((CONST TC *)(CONST VP)a0, (VP)a1, A(2));

	/* --- §1.8 デバイス管理 --- */
	case SYS_opn_dev:
		if (!usr_ok_tc(P(0), L_DEVNM + 4)) {
			return ER_ADR;
		}
		if (a2 != 0 && !usr_ok(P(2), (W)sizeof(W), True)) {
			return ER_ADR;
		}
		return opn_dev((CONST TC *)(CONST VP)a0, A(1), (W *)(VP)a2);
	case SYS_cls_dev:
		if (a2 != 0 && !usr_ok(P(2), (W)sizeof(W), True)) {
			return ER_ADR;
		}
		return cls_dev(A(0), A(1), (W *)(VP)a2);
	case SYS_rea_dev:
		if (A(3) > 0 && !usr_ok(P(2), A(3), True)) {
			return ER_ADR;
		}
		if (a4 != 0 && !usr_ok(P(4), (W)sizeof(W), True)) {
			return ER_ADR;
		}
		return rea_dev(A(0), A(1), (B *)(VP)a2, A(3), (W *)(VP)a4,
		               (W *)(VP)a5);
	case SYS_wri_dev:
		if (A(3) > 0 && !usr_ok(P(2), A(3), False)) {
			return ER_ADR;
		}
		if (a4 != 0 && !usr_ok(P(4), (W)sizeof(W), True)) {
			return ER_ADR;
		}
		return wri_dev(A(0), A(1), (CONST B *)(CONST VP)a2, A(3),
		               (W *)(VP)a4, (W *)(VP)a5);

	/* --- §1.6 ファイル管理 --- */
	case SYS_get_lnk:
		if (a0 != 0 && !usr_ok_tc(P(0), L_PATHNM + 1)) {
			return ER_ADR;
		}
		if (!usr_ok(P(1), (W)sizeof(LINK), True)) {
			return ER_ADR;
		}
		return get_lnk((CONST TC *)(CONST VP)a0, (LINK *)(VP)a1, A(2));
	case SYS_chg_wrk:
		if (a0 != 0 && !usr_ok(P(0), (W)sizeof(LINK), False)) {
			return ER_ADR;
		}
		return chg_wrk((CONST LINK *)(CONST VP)a0);
	case SYS_cre_fil:
		if (!usr_ok(P(0), (W)sizeof(LINK), True)) {
			return ER_ADR;
		}
		if (a1 != 0 && !usr_ok_tc(P(1), L_FNM + 1)) {
			return ER_ADR;
		}
		if (a2 != 0 && !usr_ok(P(2), (W)sizeof(A_MODE), False)) {
			return ER_ADR;
		}
		return cre_fil((LINK *)(VP)a0, (CONST TC *)(CONST VP)a1,
		               (CONST A_MODE *)(CONST VP)a2, (UH)a3, A(4));
	case SYS_del_fil:
		if (a0 != 0 && !usr_ok(P(0), (W)sizeof(LINK), False)) {
			return ER_ADR;
		}
		if (!usr_ok(P(1), (W)sizeof(LINK), False)) {
			return ER_ADR;
		}
		return del_fil((CONST LINK *)(CONST VP)a0,
		               (CONST LINK *)(CONST VP)a1, A(2));
	case SYS_opn_fil:
		if (!usr_ok(P(0), (W)sizeof(LINK), False)) {
			return ER_ADR;
		}
		if (a2 != 0 && !usr_ok_tc(P(2), L_CONNM + 1)) {
			return ER_ADR;
		}
		return opn_fil((CONST LINK *)(CONST VP)a0, A(1),
		               (CONST TC *)(CONST VP)a2);
	case SYS_cls_fil:
		return cls_fil(A(0));
	case SYS_see_rec:
		if (a3 != 0 && !usr_ok(P(3), (W)sizeof(W), True)) {
			return ER_ADR;
		}
		return see_rec(A(0), A(1), A(2), (W *)(VP)a3);
	case SYS_fnd_rec:
		if (a4 != 0 && !usr_ok(P(4), (W)sizeof(W), True)) {
			return ER_ADR;
		}
		return fnd_rec(A(0), A(1), (UW)(a2 & 0xFFFFFFFFULL), (UH)a3,
		               (W *)(VP)a4);
	case SYS_rea_rec:
		if (a2 != 0 && A(3) > 0 && !usr_ok(P(2), A(3), True)) {
			return ER_ADR;
		}
		if (a4 != 0 && !usr_ok(P(4), (W)sizeof(W), True)) {
			return ER_ADR;
		}
		if (a5 != 0 && !usr_ok(P(5), (W)sizeof(UH), True)) {
			return ER_ADR;
		}
		return rea_rec(A(0), A(1), (B *)(VP)a2, A(3), (W *)(VP)a4,
		               (UH *)(VP)a5);
	case SYS_del_rec:
		return del_rec(A(0));
	case SYS_fil_sts:
		if (!usr_ok(P(0), (W)sizeof(LINK), False)) {
			return ER_ADR;
		}
		if (a1 != 0 && !usr_ok(P(1), (L_FNM + 1) * 2, True)) {
			return ER_ADR;
		}
		if (a2 != 0 && !usr_ok(P(2), (W)sizeof(F_STATE), True)) {
			return ER_ADR;
		}
		if (a3 != 0 && !usr_ok(P(3), (W)sizeof(F_LOCATE), True)) {
			return ER_ADR;
		}
		return fil_sts((CONST LINK *)(CONST VP)a0, (TC *)(VP)a1,
		               (F_STATE *)(VP)a2, (F_LOCATE *)(VP)a3);

	/* --- §1.7 イベント管理 --- */
	case SYS_get_evt:
		if (!usr_ok(P(1), (W)sizeof(EVENT), True)) {
			return ER_ADR;
		}
		return get_evt(A(0), (EVENT *)(VP)a1, A(2));
	case SYS_put_evt:
		if (!usr_ok(P(0), (W)sizeof(EVENT), False)) {
			return ER_ADR;
		}
		return put_evt((CONST EVENT *)(CONST VP)a0, A(1));
	case SYS_chg_emk:
		return chg_emk(A(0));
	case SYS_get_pdp:
		if (!usr_ok(P(0), (W)sizeof(PNT), True)) {
			return ER_ADR;
		}
		return get_pdp((PNT *)(VP)a0);

	/* --- §1.3 同期通信（ランデブ） --- */
	case SYS_cre_por:
		return cre_por(A(0), A(1), (UW)(a2 & 0xFFFFFFFFULL));
	case SYS_del_por:
		return del_por(A(0));
	case SYS_cal_por:
		if (!usr_ok(P(2), A(3), True)) {
			return ER_ADR;
		}
		return cal_por(A(0), (UW)(a1 & 0xFFFFFFFFULL), (VP)a2, A(3), A(4));
	case SYS_acp_por:
		if (!usr_ok(P(2), (W)sizeof(W), True)) {
			return ER_ADR;
		}
		if (!usr_ok(P(3), 1, True)) {
			return ER_ADR;
		}
		return acp_por(A(0), (UW)(a1 & 0xFFFFFFFFULL), (W *)(VP)a2,
		               (VP)a3, A(4));
	case SYS_fwd_por:
		if (!usr_ok(P(3), A(4), False)) {
			return ER_ADR;
		}
		return fwd_por(A(0), (UW)(a1 & 0xFFFFFFFFULL), A(2), (VP)a3, A(4));
	case SYS_rpl_rdv:
		if (A(2) > 0 && !usr_ok(P(1), A(2), False)) {
			return ER_ADR;
		}
		return rpl_rdv(A(0), (VP)a1, A(2));

	/* --- §1.4 グローバル名 --- */
	case SYS_cre_nam:
		if (!usr_ok(P(0), N_NAMELEN * 2, False)) {
			return ER_ADR;
		}
		return cre_nam((CONST TC *)(CONST VP)a0, A(1), A(2));
	case SYS_del_nam:
		if (!usr_ok(P(0), N_NAMELEN * 2, False)) {
			return ER_ADR;
		}
		return del_nam((CONST TC *)(CONST VP)a0);
	case SYS_get_nam:
		if (!usr_ok(P(0), N_NAMELEN * 2, False)) {
			return ER_ADR;
		}
		if (a1 != 0 && !usr_ok(P(1), (W)sizeof(W), True)) {
			return ER_ADR;
		}
		return get_nam((CONST TC *)(CONST VP)a0, (W *)(VP)a1);

	/* --- 本OS 固有: 画面の貸し出し --- */
	case SYS_lend_screen: {
		CONST Screen *sc = screen();
		UD base = 0;
		Screen *out = (Screen *)(VP)a0;
		AddrSpace *as = prc_space(prc_self());

		if (!usr_ok(P(0), (W)sizeof(Screen), True)) {
			return ER_ADR;
		}
		if (sc == 0 || as == 0) {
			return ER_NOEXS;
		}
		if (screen_lend(as, &base) != 0) {
			return ER_NOMEM;
		}
		*out = *sc;
		/* **ユーザに渡すのは下位半分の番地だけ。**
		 * 上位半分の別名と物理アドレスは教えない。 */
		out->base = base;
		out->pa = 0;
		return ER_OK;
	}

	default:
		break;
	}
	/* 仕様の「未定義システムコール」。番号を間違えたユーザプログラムは
	 * 落とさずにエラーを返す。 */
	return ER_UNF;
}

/* 書き込み系でまだ通していないものは、ユーザから呼ばれても
 * ER_UNF になる。番号だけ先に決めてあるのは、**後から追加するときに
 * 番号をずらさないため**（ずらすと古いプログラムが別の呼び出しに化ける）。 */

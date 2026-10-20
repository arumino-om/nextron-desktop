/*
 * syscall.c — ユーザ特権からシステムコールを呼ぶスタブ
 *
 * **これはユーザプログラム側のライブラリ**で、カーネルには入らない。
 * アプリケーションは `opn_dev(...)` と普通に書き、ここが特権境界を
 * 越える呼び出しに直す。約束は include/btron/syscall.h にある。
 *
 * 【層の約束】src/lib は LGPL 側（J2 / 規律 R14）。カーネルや外殻の
 * ソースから include してはならない。逆にここから使ってよいのは
 * include/btron の公開ヘッダだけである。
 *
 * 【なぜインラインアセンブラか】
 * 関数呼び出しの規約 (AAPCS64) と、システムコールの規約（x8 に番号）を
 * 繋ぐだけなので、アセンブラのファイルを別に持つより近くに置いた方が
 * 読み違えにくい。番号とレジスタの対応が 1 か所に見える。
 */

#include <btron/syscall.h>

#include <btron/clock.h>
#include <btron/device.h>
#include <btron/event.h>
#include <btron/file.h>
#include <btron/memory.h>
#include <btron/message.h>
#include <btron/gname.h>
#include <btron/proc.h>
#include <btron/screen.h>
#include <btron/taskcomm.h>
#include <btron/system.h>

#if defined(__aarch64__)

LOCAL inline D sysc(UD n, UD a0, UD a1, UD a2, UD a3, UD a4, UD a5)
{
	register UD x8 __asm__("x8") = n;
	register UD x0 __asm__("x0") = a0;
	register UD x1 __asm__("x1") = a1;
	register UD x2 __asm__("x2") = a2;
	register UD x3 __asm__("x3") = a3;
	register UD x4 __asm__("x4") = a4;
	register UD x5 __asm__("x5") = a5;

	__asm__ volatile("svc #0"
	                 : "+r"(x0)
	                 : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
	                 : "memory");
	return (D)x0;
}

#elif defined(__x86_64__)

/* **第 4 引数が rcx ではなく r10 である。**
 *
 * `syscall` 命令は戻り先 RIP を rcx に、RFLAGS を r11 に入れるので、
 * System V の第 4 引数レジスタ (rcx) は使えない。カーネル側の入口
 * (src/kernel/hal/x86_64/syscall.S) が r10 を rcx へ移し替えて C の
 * 並びに直す。番号は rax で、戻り値も rax。
 *
 * clobber に rcx と r11 を書くのは**命令が潰すから**で、書き忘れると
 * 呼び出し元が持っていた値が壊れる（最適化で現れ方が変わるので、
 * 症状が出たり出なかったりする類になる）。 */
LOCAL inline D sysc(UD n, UD a0, UD a1, UD a2, UD a3, UD a4, UD a5)
{
	register UD rax __asm__("rax") = n;
	register UD rdi __asm__("rdi") = a0;
	register UD rsi __asm__("rsi") = a1;
	register UD rdx __asm__("rdx") = a2;
	register UD r10 __asm__("r10") = a3;
	register UD r8 __asm__("r8") = a4;
	register UD r9 __asm__("r9") = a5;

	__asm__ volatile("syscall"
	                 : "+r"(rax)
	                 : "r"(rdi), "r"(rsi), "r"(rdx), "r"(r10), "r"(r8),
	                   "r"(r9)
	                 : "rcx", "r11", "memory");
	return (D)rax;
}

#else
#error "システムコールのスタブはこのアーキテクチャに未対応"
#endif

#define S0(n) sysc((n), 0, 0, 0, 0, 0, 0)
#define S1(n, a) sysc((n), (UD)(a), 0, 0, 0, 0, 0)
#define S2(n, a, b) sysc((n), (UD)(a), (UD)(b), 0, 0, 0, 0)
#define S3(n, a, b, c) sysc((n), (UD)(a), (UD)(b), (UD)(c), 0, 0, 0)
#define S4(n, a, b, c, d) sysc((n), (UD)(a), (UD)(b), (UD)(c), (UD)(d), 0, 0)
#define S5(n, a, b, c, d, e)                                               \
	sysc((n), (UD)(a), (UD)(b), (UD)(c), (UD)(d), (UD)(e), 0)
#define S6(n, a, b, c, d, e, f)                                            \
	sysc((n), (UD)(a), (UD)(b), (UD)(c), (UD)(d), (UD)(e), (UD)(f))

/* --- §1.1 プロセス / タスク管理 --- */

EXPORT void ext_prc(W code)
{
	(void)S1(SYS_ext_prc, (UW)code);
	/* 戻らない。万一戻ったら止める。 */
	for (;;) {
	}
}

EXPORT ERR ter_prc(W pid, W code, W opt)
{
	return (ERR)S3(SYS_ter_prc, (UW)pid, (UW)code, (UW)opt);
}

EXPORT ERR chg_pri(W pid, W pri, W opt)
{
	return (ERR)S3(SYS_chg_pri, (UW)pid, (UW)pri, (UW)opt);
}

EXPORT WERR prc_sts(W pid, P_STATE *buff, TC *name)
{
	return (WERR)S3(SYS_prc_sts, (UW)pid, buff, name);
}

EXPORT WERR get_usr(W pid, P_USER *buff)
{
	return (WERR)S2(SYS_get_usr, (UW)pid, buff);
}

EXPORT ERR slp_tsk(W tmout)
{
	return (ERR)S1(SYS_slp_tsk, (UW)tmout);
}

EXPORT ERR wup_tsk(W tskid)
{
	return (ERR)S1(SYS_wup_tsk, (UW)tskid);
}

EXPORT ERR dly_tsk(W dlytim)
{
	return (ERR)S1(SYS_dly_tsk, (UW)dlytim);
}

EXPORT WERR get_tid(void)
{
	return (WERR)S0(SYS_get_tid);
}

EXPORT WERR cre_prc(CONST LINK *lnk, W pri, CONST MESSAGE *msg)
{
	return (WERR)S3(SYS_cre_prc, lnk, (UW)pri, msg);
}

/* --- §1.2 メッセージ管理 --- */

EXPORT ERR snd_msg(W pid, CONST MESSAGE *msg, W opt)
{
	return (ERR)S3(SYS_snd_msg, (UW)pid, msg, (UW)opt);
}

EXPORT WERR rcv_msg(W t_mask, MESSAGE *msg, W size, W opt)
{
	return (WERR)S4(SYS_rcv_msg, (UW)t_mask, msg, (UW)size, (UW)opt);
}

EXPORT ERR clr_msg(W t_mask, W last_mask)
{
	return (ERR)S2(SYS_clr_msg, (UW)t_mask, (UW)last_mask);
}

/* --- §1.5 メモリ管理 --- */

EXPORT ERR get_mbk(VP *adr, W nblk, UW atr)
{
	return (ERR)S3(SYS_get_mbk, adr, (UW)nblk, atr);
}

EXPORT ERR rel_mbk(VP adr)
{
	return (ERR)S1(SYS_rel_mbk, adr);
}

EXPORT ERR mbk_sts(M_STATE *stat)
{
	return (ERR)S1(SYS_mbk_sts, stat);
}

/* --- §1.9 / §1.10 --- */

EXPORT ERR get_tim(STIME *time, TIMEZONE *tz)
{
	return (ERR)S2(SYS_get_tim, time, tz);
}

EXPORT ERR get_ver(T_VER *ver)
{
	return (ERR)S1(SYS_get_ver, ver);
}

EXPORT WERR get_cnf(CONST TC *name, VP value, W len)
{
	return (WERR)S3(SYS_get_cnf, name, value, (UW)len);
}

/* --- §1.8 デバイス管理 --- */

EXPORT WERR opn_dev(CONST TC *dev, W o_mode, W *error)
{
	return (WERR)S3(SYS_opn_dev, dev, (UW)o_mode, error);
}

EXPORT ERR cls_dev(W dd, W eject, W *error)
{
	return (ERR)S3(SYS_cls_dev, (UW)dd, (UW)eject, error);
}

EXPORT ERR rea_dev(W dd, W start, B *buf, W size, W *a_size, W *error)
{
	return (ERR)S6(SYS_rea_dev, (UW)dd, (UW)start, buf, (UW)size, a_size,
	               error);
}

EXPORT ERR wri_dev(W dd, W start, CONST B *buf, W size, W *a_size, W *error)
{
	return (ERR)S6(SYS_wri_dev, (UW)dd, (UW)start, buf, (UW)size, a_size,
	               error);
}

/* --- §1.6 ファイル管理 --- */

EXPORT WERR get_lnk(CONST TC *path, LINK *lnk, W mode)
{
	return (WERR)S3(SYS_get_lnk, path, lnk, (UW)mode);
}

EXPORT ERR chg_wrk(CONST LINK *lnk)
{
	return (ERR)S1(SYS_chg_wrk, lnk);
}

EXPORT WERR cre_fil(LINK *lnk, CONST TC *name, CONST A_MODE *mode, UH atype,
                    W opt)
{
	return (WERR)S5(SYS_cre_fil, lnk, name, mode, atype, (UW)opt);
}

EXPORT WERR del_fil(CONST LINK *org, CONST LINK *lnk, W force)
{
	return (WERR)S3(SYS_del_fil, org, lnk, (UW)force);
}

EXPORT WERR opn_fil(CONST LINK *lnk, W o_mode, CONST TC *pwd)
{
	return (WERR)S3(SYS_opn_fil, lnk, (UW)o_mode, pwd);
}

EXPORT ERR cls_fil(W fd)
{
	return (ERR)S1(SYS_cls_fil, (UW)fd);
}

EXPORT ERR see_rec(W fd, W offset, W mode, W *recnum)
{
	return (ERR)S4(SYS_see_rec, (UW)fd, (UW)offset, (UW)mode, recnum);
}

EXPORT WERR fnd_rec(W fd, W mode, UW typemask, UH subtype, W *recnum)
{
	return (WERR)S5(SYS_fnd_rec, (UW)fd, (UW)mode, typemask, subtype, recnum);
}

EXPORT WERR rea_rec(W fd, W offset, B *buf, W size, W *r_size, UH *subtype)
{
	return (WERR)S6(SYS_rea_rec, (UW)fd, (UW)offset, buf, (UW)size, r_size,
	                subtype);
}

EXPORT WERR del_rec(W fd)
{
	return (WERR)S1(SYS_del_rec, (UW)fd);
}

EXPORT WERR fil_sts(CONST LINK *lnk, TC *name, F_STATE *stat, F_LOCATE *locat)
{
	return (WERR)S4(SYS_fil_sts, lnk, name, stat, locat);
}

/* --- §1.3 同期通信（ランデブ） --- */

EXPORT WERR cre_por(W maxcmsz, W maxrmsz, UW opt)
{
	return (WERR)S3(SYS_cre_por, (UW)maxcmsz, (UW)maxrmsz, opt);
}

EXPORT ERR del_por(ID id)
{
	return (ERR)S1(SYS_del_por, (UW)id);
}

EXPORT WERR cal_por(ID id, UW calptn, VP msg, W cmsz, TMOUT tmo)
{
	return (WERR)S5(SYS_cal_por, (UW)id, calptn, msg, (UW)cmsz, (UW)tmo);
}

EXPORT WERR acp_por(ID id, UW acpptn, W *rdv, VP msg, TMOUT tmo)
{
	return (WERR)S5(SYS_acp_por, (UW)id, acpptn, rdv, msg, (UW)tmo);
}

EXPORT ERR fwd_por(ID id, UW calptn, W rdv, VP msg, W cmsz)
{
	return (ERR)S5(SYS_fwd_por, (UW)id, calptn, (UW)rdv, msg, (UW)cmsz);
}

EXPORT ERR rpl_rdv(W rdv, VP msg, W rmsz)
{
	return (ERR)S3(SYS_rpl_rdv, (UW)rdv, msg, (UW)rmsz);
}

/* --- §1.4 グローバル名 --- */

EXPORT WERR cre_nam(CONST TC *name, W data, W opt)
{
	return (WERR)S3(SYS_cre_nam, name, (UW)data, (UW)opt);
}

EXPORT ERR del_nam(CONST TC *name)
{
	return (ERR)S1(SYS_del_nam, name);
}

EXPORT ERR get_nam(CONST TC *name, W *data)
{
	return (ERR)S2(SYS_get_nam, name, data);
}

EXPORT W rdv_caller(W rdv)
{
	return (W)S1(SYS_rdv_caller, (UW)rdv);
}

/* --- 本OS 固有: 画面の貸し出し --- */

EXPORT D btron_sys_lend_screen(Screen *out)
{
	return S1(SYS_lend_screen, out);
}

/* --- §1.7 イベント管理 --- */

EXPORT WERR get_evt(W t_mask, EVENT *evt, W opt)
{
	return (WERR)S3(SYS_get_evt, (UW)t_mask, evt, (UW)opt);
}

EXPORT ERR put_evt(CONST EVENT *evt, W opt)
{
	return (ERR)S2(SYS_put_evt, evt, (UW)opt);
}

EXPORT WERR chg_emk(W mask)
{
	return (WERR)S1(SYS_chg_emk, (UW)mask);
}

EXPORT WERR get_pdp(PNT *pos)
{
	return (WERR)S1(SYS_get_pdp, pos);
}

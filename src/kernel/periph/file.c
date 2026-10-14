/*
 * file.c — 周辺核 §1.6「ファイル管理」
 *
 * 考え方と仕様の要点は include/btron/file.h の冒頭に書いた。
 * ディスク形式は src/kernel/fs/（本OS 独自）にある。
 *
 * ------------------------------------------------------------------------
 * 開いている間はレコードストリームをメモリに持つ
 * ------------------------------------------------------------------------
 *
 * ファイルは「可変長レコードの順序列」で、挿入・削除が日常的に起きる。
 * これをブロック連鎖の上で直接やると、1 レコード挿すたびに後ろ全部を
 * 書き直すことになる。
 *
 * **開いている間だけストリーム全体をメモリに写し、閉じるときに書き戻す。**
 * 写しは「システム全体のオープン」ごとに 1 つで、同じファイルを複数の
 * fd が開いても共有する。そうしないと、共有モードで開いた 2 つの fd が
 * 別々の内容を見てしまう。
 *
 * 仕様が `syn_fls`（同期）と `FS_ASYN`（非同期指定）を用意しているのは、
 * **書き込みが遅れうることを仕様自身が認めている**からである。
 *
 * ------------------------------------------------------------------------
 * 現在レコードは番号ではなく「そのレコード」
 * ------------------------------------------------------------------------
 *
 * 仕様 §1.6.3:「現在レコードはレコードの挿入 / 削除によっても変化せず、
 * 現在レコードに対応するレコード番号が変化することになる」。
 *
 * だから挿入・削除のときは、**同じファイルを開いている他の fd の現在
 * レコード番号をずらす**。番号をそのままにすると、他の fd が突然別の
 * レコードを指すことになる。
 */

#include <btron/file.h>

#include <btron/clock.h>
#include <btron/device.h>
#include <btron/hal.h>
#include <btron/memory.h>
#include <btron/proc.h>
#include <btron/vmem.h>

#include "../fs/fs.h"

/* ------------------------------------------------------------------ */
/* 状態                                                                */
/* ------------------------------------------------------------------ */

LOCAL Fs fstab[FS_MAX_ATTACH];

/* システム全体のオープン。同じファイルを開く fd はこれを共有する。 */
typedef struct OpenFile {
	Bool used;
	W fs;   /* fstab の添字 */
	UW fid;
	W nref; /* このファイルを指している fd の数 */
	W nexcl;  /* 排他モードで開いた数 */
	W nwexcl; /* 排他書き込みモードで開いた数 */
	W nwrite; /* 書き込み可で開いた数 */
	UB *buf;  /* レコードストリームの写し */
	W len;
	W cap;
	Bool dirty;
	W lock_rec; /* ロック中のレコード番号。-1 なら無し */
	W lock_pid;
} OpenFile;

LOCAL OpenFile ofl[FS_MAX_OPEN];

/* ファイルディスクリプタ。**プロセスごとに別の番号空間**（仕様
 * 「各プロセス毎で定義されるファイルディスクリプタ」）。 */
typedef struct Fdesc {
	Bool used;
	W of;   /* ofl の添字 */
	W mode; /* オープンモード */
	W cur;  /* 現在レコード番号 */
} Fdesc;

LOCAL Fdesc fdt[PRC_MAX_PRCID + 1][FD_MAX];

/* プロセスごとの環境。作業ファイルは子へ継承される（仕様 §1.6.2）。 */
typedef struct FEnv {
	Bool has_work;
	LINK work;
} FEnv;

LOCAL FEnv fenv[PRC_MAX_PRCID + 1];

/* ユーザごとのデフォルトアクセスモード。
 * 仕様「デフォールトアクセスモードはユーザ毎に定義され、変更した場合は
 * 同一ユーザーのすべてのプロセスに対して即時に有効となる」。
 * ユーザ管理が無いので、いまは 1 組だけ持つ。 */
LOCAL DA_MODE defmode;
LOCAL Bool fil_on;

/* ------------------------------------------------------------------ */
/* 小道具                                                              */
/* ------------------------------------------------------------------ */

LOCAL W tc_len(CONST TC *s, W max)
{
	W n = 0;

	while (n < max && s != 0 && s[n] != TNULL) {
		n++;
	}
	return n;
}

LOCAL Bool tc_eq(CONST TC *a, CONST TC *b, W n)
{
	W i;

	for (i = 0; i < n; i++) {
		TC x = (a != 0) ? a[i] : TNULL;
		TC y = (b != 0) ? b[i] : TNULL;

		if (x != y) {
			return False;
		}
		if (x == TNULL) {
			return True;
		}
	}
	return True;
}

LOCAL void tc_cpy(TC *dst, CONST TC *src, W n)
{
	W i;
	Bool end = False;

	for (i = 0; i < n; i++) {
		TC c = 0;

		if (!end && src != 0) {
			c = src[i];
			if (c == TNULL) {
				end = True;
				c = 0;
			}
		}
		dst[i] = c;
	}
}

LOCAL STIME now_time(void)
{
	STIME t = 0;

	(void)get_tim(&t, 0);
	return t;
}

/* ------------------------------------------------------------------ */
/* ファイルシステムの検索                                              */
/* ------------------------------------------------------------------ */

LOCAL W fs_by_dev(CONST TC *dev)
{
	W i;

	for (i = 0; i < FS_MAX_ATTACH; i++) {
		if (fstab[i].used && tc_eq(fstab[i].dev, dev, L_DEVNM)) {
			return i;
		}
	}
	return -1;
}

LOCAL W fs_by_conn(CONST TC *conn, W len)
{
	W i;

	for (i = 0; i < FS_MAX_ATTACH; i++) {
		if (!fstab[i].used) {
			continue;
		}
		if (tc_len(fstab[i].conn, L_CONNM) == len &&
		    tc_eq(fstab[i].conn, conn, len)) {
			return i;
		}
	}
	return -1;
}

LOCAL W fs_by_name(CONST TC *name)
{
	W i;

	for (i = 0; i < FS_MAX_ATTACH; i++) {
		if (fstab[i].used && tc_eq(fstab[i].fs_name, name, L_FSNM)) {
			return i;
		}
	}
	return -1;
}

/* リンクからファイルシステムを引く。
 * 仕様「固定リンクはファイルシステム名を持たない」ので、取り出した
 * 時点で名前が埋まっている前提。空なら作業ファイルのものとはしない
 * （呼び出し側が埋める）。 */
LOCAL W fs_of_link(CONST LINK *lnk)
{
	if (lnk == 0) {
		return -1;
	}
	return fs_by_name(lnk->fs_name);
}

/* ------------------------------------------------------------------ */
/* レコードストリームの走査                                            */
/*                                                                     */
/* ストリームは                                                        */
/*   UH タイプ / UH サブタイプ / UW サイズ / 本体 / 4 バイト詰め物       */
/* の並び。番号からバイト位置を出すのは毎回先頭から辿る。ファイルは      */
/* 小さいので十分で、索引を持つと挿入・削除のたびに作り直しになる。      */
/* ------------------------------------------------------------------ */

LOCAL W rec_at(CONST OpenFile *o, W n)
{
	W off = 0;
	W i;

	for (i = 0; i < n; i++) {
		W sz;

		if (off + REC_HDR > o->len) {
			return -1;
		}
		sz = (W)fsb_get32(o->buf, off + 4);
		off += REC_HDR + REC_PAD(sz);
	}
	return (off <= o->len) ? off : -1;
}

LOCAL W rec_count(CONST OpenFile *o)
{
	W off = 0;
	W n = 0;

	while (off + REC_HDR <= o->len) {
		W sz = (W)fsb_get32(o->buf, off + 4);

		off += REC_HDR + REC_PAD(sz);
		if (off > o->len) {
			break;
		}
		n++;
	}
	return n;
}

#define REC_TYPE(o, off) ((W)fsb_get16((o)->buf, (off)))
#define REC_SUB(o, off) (fsb_get16((o)->buf, (off) + 2))
#define REC_SIZE(o, off) ((W)fsb_get32((o)->buf, (off) + 4))
#define REC_BODY(o, off) ((o)->buf + (off) + REC_HDR)

/* ストリームの一部を差し替える。growth に応じて領域を取り直す。 */
LOCAL ERR buf_splice(OpenFile *o, W at, W oldlen, CONST UB *data, W newlen)
{
	W need = o->len - oldlen + newlen;
	W i;

	if (at < 0 || oldlen < 0 || newlen < 0 || at + oldlen > o->len) {
		return ER_PAR;
	}
	if (need > o->cap) {
		W cap = (o->cap < 512) ? 512 : o->cap;
		UB *nb;

		while (cap < need) {
			cap *= 2;
		}
		nb = (UB *)kmalloc((UD)cap);
		if (nb == 0) {
			return ER_NOSPC;
		}
		for (i = 0; i < o->len; i++) {
			nb[i] = o->buf[i];
		}
		if (o->buf != 0) {
			kfree(o->buf);
		}
		o->buf = nb;
		o->cap = cap;
	}
	/* 後ろをずらす。**伸びるときは後ろから、縮むときは前から**
	 * 動かさないと重なった領域を壊す。 */
	if (newlen > oldlen) {
		for (i = o->len - 1; i >= at + oldlen; i--) {
			o->buf[i + (newlen - oldlen)] = o->buf[i];
		}
	} else if (newlen < oldlen) {
		for (i = at + oldlen; i < o->len; i++) {
			o->buf[i - (oldlen - newlen)] = o->buf[i];
		}
	}
	for (i = 0; i < newlen; i++) {
		o->buf[at + i] = (data != 0) ? data[i] : 0;
	}
	o->len = need;
	o->dirty = True;
	return ER_OK;
}

/* ------------------------------------------------------------------ */
/* ファイルディスクリプタ                                              */
/* ------------------------------------------------------------------ */

LOCAL Fdesc *get_fd(W fd)
{
	W me = prc_self();

	if (me < 1 || me > PRC_MAX_PRCID) {
		return 0;
	}
	if (fd < 1 || fd > FD_MAX || !fdt[me][fd - 1].used) {
		return 0;
	}
	return &fdt[me][fd - 1];
}

/* 同じファイルを開いている他の fd の現在レコード番号をずらす。 */
LOCAL void shift_cur(W of, W from, W delta, CONST Fdesc *except)
{
	W p;
	W i;

	for (p = 1; p <= PRC_MAX_PRCID; p++) {
		for (i = 0; i < FD_MAX; i++) {
			Fdesc *f = &fdt[p][i];

			if (!f->used || f->of != of || f == except) {
				continue;
			}
			if (f->cur >= from) {
				f->cur += delta;
				if (f->cur < 0) {
					f->cur = 0;
				}
			}
		}
	}
}

/* そのレコードを現在レコードにしている他の fd があるか（del_rec の
 * ER_BUSY のため。仕様「他のオープンで現在レコードとなっているとき」）。 */
LOCAL Bool cur_used_by_other(W of, W recnum, CONST Fdesc *me)
{
	W p;
	W i;

	for (p = 1; p <= PRC_MAX_PRCID; p++) {
		for (i = 0; i < FD_MAX; i++) {
			CONST Fdesc *f = &fdt[p][i];

			if (!f->used || f->of != of || f == me) {
				continue;
			}
			if (f->cur == recnum) {
				return True;
			}
		}
	}
	return False;
}

/* ------------------------------------------------------------------ */
/* ファイルの読み書き（エントリ単位）                                  */
/* ------------------------------------------------------------------ */

LOCAL ERR ent_load(W fsx, UW fid, UB *ent)
{
	if (fsx < 0 || fsx >= FS_MAX_ATTACH || !fstab[fsx].used) {
		return ER_NOFS;
	}
	return fsb_ent_read(&fstab[fsx], fid, ent);
}

LOCAL Bool ent_exists(CONST UB *ent)
{
	return (fsb_get32(ent, FE_FLAGS) & FE_F_USED) != 0;
}

/* 参照カウントを増減する。 */
LOCAL ERR ent_nlink(W fsx, UW fid, W delta, W *after)
{
	UB ent[FSB_ENTSZ];
	W n;
	ERR er = ent_load(fsx, fid, ent);

	if (er < 0) {
		return er;
	}
	if (!ent_exists(ent)) {
		return ER_NOEXS;
	}
	n = (W)fsb_get32(ent, FE_NLINK) + delta;
	if (n < 0) {
		n = 0;
	}
	if (n > FSB_MAX_NLINK) {
		return ER_LIMIT;
	}
	fsb_put32(ent, FE_NLINK, (UW)n);
	if (after != 0) {
		*after = n;
	}
	return fsb_ent_write(&fstab[fsx], fid, ent);
}

/* ------------------------------------------------------------------ */
/* アクセスチェック §1.6.4                                             */
/* ------------------------------------------------------------------ */

/* 要求 (F_READ / F_WRITE / F_EXCUTE の組) が通るか。 */
LOCAL Bool acc_ok(W fsx, CONST UB *ent, W want)
{
	P_USER u;
	UH ftype = fsb_get16(ent, FE_TYPE);
	TC owner[L_USRNM + 1];
	TC group[L_USRNM + 1];
	W level;
	UH grpacc;
	UH pubacc;
	UH lv;
	W i;

	if (want == 0) {
		return True; /* F_EXIST: 存在すれば可 */
	}
	/* 仕様「書込不可属性…この属性が ON の状態ではすべての変更操作は
	 * 所有者を含めた全てのユーザに対して禁止される」 */
	if ((want & F_WRITE) != 0 && (ftype & F_RONLY) != 0) {
		return False;
	}
	if (fstab[fsx].ronly && (want & F_WRITE) != 0) {
		return False;
	}
	/* レベル 0 のファイルシステムは管理しない（仕様 §1.6.4 a）。 */
	if (fstab[fsx].level == F_LEVEL0) {
		return True;
	}
	if (!prc_user(prc_self(), &u)) {
		return False;
	}
	level = u.level;
	fsb_get_tc(ent, FE_OWNER, owner, L_USRNM);
	fsb_get_tc(ent, FE_GROUP, group, L_USRNM);
	grpacc = fsb_get16(ent, FE_GRPACC);
	pubacc = fsb_get16(ent, FE_PUBACC);

	/* 所有者か。**所有者名の先頭が 0 なら所有者はいない**（仕様）。 */
	if (owner[0] != TNULL && tc_eq(owner, u.usr_name, L_USRNM)) {
		return (ftype & (UH)want & F_OWNACC) == (UH)want;
	}
	/* 所属グループか。 */
	if (group[0] != TNULL) {
		CONST TC *g[N_GRP];

		g[0] = u.grp_name1;
		g[1] = u.grp_name2;
		g[2] = u.grp_name3;
		g[3] = u.grp_name4;
		for (i = 0; i < N_GRP; i++) {
			if (g[i][0] != TNULL && tc_eq(group, g[i], L_USRNM)) {
				/* グループアクセスレベルは RWE それぞれ 4 ビット。
				 * **許可される最低ユーザレベル**なので、
				 * 自分のレベルがそれ以下なら通る。 */
				if ((want & F_READ) != 0) {
					lv = (UH)((grpacc >> 8) & 0x0FU);
					if (level > (W)lv) {
						return False;
					}
				}
				if ((want & F_WRITE) != 0) {
					lv = (UH)((grpacc >> 4) & 0x0FU);
					if (level > (W)lv) {
						return False;
					}
				}
				if ((want & F_EXCUTE) != 0) {
					lv = (UH)(grpacc & 0x0FU);
					if (level > (W)lv) {
						return False;
					}
				}
				return True;
			}
		}
	}
	/* 仕様「レベル 0 のユーザは、基本的にすべてのファイルの RWE が可能
	 * ( 書込不可属性が付けられた場合には W はできない)」 */
	if (level == P_LEVEL_MAX) {
		return True;
	}
	if ((want & F_READ) != 0 && level > (W)((pubacc >> 8) & 0x0FU)) {
		return False;
	}
	if ((want & F_WRITE) != 0 && level > (W)((pubacc >> 4) & 0x0FU)) {
		return False;
	}
	if ((want & F_EXCUTE) != 0 && level > (W)(pubacc & 0x0FU)) {
		return False;
	}
	return True;
}

/* 所有者としての権利があるか（アクセス管理情報の変更に要る）。 */
LOCAL Bool is_owner(W fsx, CONST UB *ent)
{
	P_USER u;
	TC owner[L_USRNM + 1];

	/* 仕様「所有者のチェックに関しては、レベル0のファイルシステムは
	 * 特別に取り扱われ、誰でも所有者としての権利を持つ」 */
	if (fstab[fsx].level == F_LEVEL0) {
		return True;
	}
	if (!prc_user(prc_self(), &u)) {
		return False;
	}
	fsb_get_tc(ent, FE_OWNER, owner, L_USRNM);
	if (owner[0] == TNULL) {
		/* 仕様「所有者がないファイルの場合は、例外的にレベル 0 の
		 * ユーザーであればアクセス管理情報を変更することができる」 */
		return u.level == P_LEVEL_MAX;
	}
	return tc_eq(owner, u.usr_name, L_USRNM);
}

/* ------------------------------------------------------------------ */
/* 初期化とプロセス後始末                                              */
/* ------------------------------------------------------------------ */

EXPORT ERR fil_init(void)
{
	W i;
	W j;

	for (i = 0; i < FS_MAX_ATTACH; i++) {
		fstab[i].used = False;
	}
	for (i = 0; i < FS_MAX_OPEN; i++) {
		ofl[i].used = False;
		ofl[i].buf = 0;
		ofl[i].cap = 0;
	}
	for (i = 0; i <= PRC_MAX_PRCID; i++) {
		fenv[i].has_work = False;
		for (j = 0; j < FD_MAX; j++) {
			fdt[i][j].used = False;
		}
	}
	/* 既定のアクセスモード。所有者は RWE 可、グループと一般は
	 * レベル 0 のみ（= 事実上の非公開）。 */
	defmode.f_ownacc = F_OWNACR | F_OWNACW | F_OWNACE;
	defmode.f_grpacc = 0x0000;
	defmode.f_pubacc = 0x0000;
	defmode.f_grpno = 0;
	for (i = 0; i < N_GRP; i++) {
		defmode.f_gacc[i] = 0;
	}
	fil_on = True;
	return ER_OK;
}

EXPORT void fil_inherit(W parent, W child)
{
	if (parent < 1 || parent > PRC_MAX_PRCID || child < 1 ||
	    child > PRC_MAX_PRCID) {
		return;
	}
	/* 仕様「作業ファイルはプロセスの実行環境として保持されており、
	 * 生成した子プロセスに継承される」。
	 * **ファイルディスクリプタと現在レコードは継承されない。** */
	fenv[child] = fenv[parent];
}

EXPORT void fil_release_process(W pid)
{
	W i;

	if (pid < 1 || pid > PRC_MAX_PRCID) {
		return;
	}
	/* 仕様「プロセスの終了時には、オープンしたファイルはすべて
	 * 自動的にクローズされる」 */
	for (i = 0; i < FD_MAX; i++) {
		if (fdt[pid][i].used) {
			(void)cls_fil(i + 1);
		}
	}
	fenv[pid].has_work = False;
}

EXPORT W fil_open_count(void)
{
	W n = 0;
	W i;

	for (i = 0; i < FS_MAX_OPEN; i++) {
		if (ofl[i].used) {
			n++;
		}
	}
	return n;
}

/* ------------------------------------------------------------------ */
/* ファイルシステムの接続 §1.6.8                                       */
/* ------------------------------------------------------------------ */

EXPORT ERR fil_format(CONST TC *dev, CONST TC *fs_name, CONST TC *fs_locat,
                      W level, W lang)
{
	W dd;
	ERR er;

	if (dev == 0 || fs_name == 0) {
		return ER_ADR;
	}
	if (level < F_LEVEL0 || level > F_LEVEL2) {
		return ER_PAR;
	}
	/* 既に接続されている媒体は作り直さない。 */
	if (fs_by_dev(dev) >= 0) {
		return ER_BUSY;
	}
	dd = dev_open_system(dev, D_UPDATE | D_EXCL);
	if (dd < 0) {
		return (ERR)dd;
	}
	er = fsb_format(dd, fs_name, fs_locat, level, lang);
	(void)cls_dev(dd, 0, 0);
	return er;
}

EXPORT ERR att_fls(CONST TC *dev, CONST TC *name, LINK *lnk, UW mode)
{
	W slot = -1;
	W i;
	W dd;
	Fs *fs;
	ERR er;

	if (dev == 0 || name == 0) {
		return ER_ADR;
	}
	if ((mode & ~(UW)(FS_ASYN | FS_RONLY)) != 0) {
		return ER_PAR;
	}
	if (tc_len(name, L_CONNM + 1) > L_CONNM || name[0] == TNULL) {
		return ER_PAR;
	}
	if (fs_by_dev(dev) >= 0) {
		return ER_EXS; /* 同じデバイスは二度接続できない */
	}
	if (fs_by_conn(name, tc_len(name, L_CONNM)) >= 0) {
		return ER_EXS; /* 接続名が重なっている */
	}
	for (i = 0; i < FS_MAX_ATTACH; i++) {
		if (!fstab[i].used) {
			slot = i;
			break;
		}
	}
	if (slot < 0) {
		return ER_LIMIT;
	}
	fs = &fstab[slot];
	dd = dev_open_system(dev, ((mode & FS_RONLY) != 0) ? D_READ : D_UPDATE);
	if (dd < 0) {
		return (ERR)dd;
	}
	fs->dd = dd;
	fs->ronly = ((mode & FS_RONLY) != 0);
	fs->async = ((mode & FS_ASYN) != 0);
	er = fsb_mount(fs);
	if (er < 0) {
		(void)cls_dev(dd, 0, 0);
		return er;
	}
	/* **名前の衝突はマウントしてから分かる。** 仕様「ファイルシステム名が
	 * 同一のファイルシステムは、同一とみなされるため、ファイルシステム名は
	 * ユニークでなければいけない」 */
	if (fs_by_name(fs->fs_name) >= 0) {
		(void)cls_dev(dd, 0, 0);
		return ER_EXS;
	}
	tc_cpy(fs->dev, dev, L_DEVNM);
	fs->dev[L_DEVNM] = TNULL;
	tc_cpy(fs->conn, name, L_CONNM);
	fs->conn[L_CONNM] = TNULL;
	fs->used = True;

	/* 仕様「接続時には接続したファイルシステムのルートファイルへの
	 * リンクが得られる」 */
	if (lnk != 0) {
		tc_cpy(lnk->fs_name, fs->fs_name, L_FSNM);
		lnk->f_id = 0;
		lnk->atr1 = 0;
		lnk->atr2 = 0;
		lnk->atr3 = 0;
		lnk->atr4 = 0;
		lnk->atr5 = 0;
	}
	return ER_OK;
}

EXPORT ERR det_fls(CONST TC *dev, W eject)
{
	W fsx;
	W i;

	if (dev == 0) {
		return ER_ADR;
	}
	fsx = fs_by_dev(dev);
	if (fsx < 0) {
		return ER_NOEXS;
	}
	/* 開いているファイルが残っていたら切り離さない。
	 * 仕様は「切断状態」を虚身として許すが、**書き戻せていない写しを
	 * 捨てることになる**ので断る方が安全。 */
	for (i = 0; i < FS_MAX_OPEN; i++) {
		if (ofl[i].used && ofl[i].fs == fsx) {
			return ER_BUSY;
		}
	}
	(void)fsb_sync(&fstab[fsx]);
	(void)cls_dev(fstab[fsx].dd, eject, 0);
	fstab[fsx].used = False;
	return ER_OK;
}

EXPORT ERR syn_fls(void)
{
	W i;

	for (i = 0; i < FS_MAX_ATTACH; i++) {
		if (fstab[i].used) {
			(void)fsb_sync(&fstab[i]);
		}
	}
	return ER_OK;
}

EXPORT WERR fls_sts(CONST TC *dev, FS_STATE *buff)
{
	W fsx;
	Fs *fs;

	if (dev == 0) {
		return ER_ADR;
	}
	fsx = fs_by_dev(dev);
	if (fsx < 0) {
		return ER_NOEXS;
	}
	fs = &fstab[fsx];
	if (buff != 0) {
		buff->fs_bsize = FSB_BSIZE;
		buff->fs_nfile = fs->nfile;
		buff->fs_lang = (H)fs->lang;
		buff->fs_level = (H)fs->level;
		buff->fs_nblk = (W)fs->nblk;
		buff->fs_nfree = (W)fs->nfree;
		buff->fs_mtime = fs->mtime;
		buff->fs_ctime = fs->ctime;
		tc_cpy(buff->fs_name, fs->fs_name, L_FSNM);
		tc_cpy(buff->fs_locat, fs->fs_locat, L_DLNM);
	}
	return (W)sizeof(FS_STATE);
}

EXPORT ERR chg_fls(CONST TC *dev, CONST TC *fs_name, CONST TC *fs_locate)
{
	W fsx;
	Fs *fs;

	if (dev == 0) {
		return ER_ADR;
	}
	fsx = fs_by_dev(dev);
	if (fsx < 0) {
		return ER_NOEXS;
	}
	fs = &fstab[fsx];
	if (fs->ronly) {
		return ER_RONLY;
	}
	if (fs_name != 0) {
		W other = fs_by_name(fs_name);

		if (other >= 0 && other != fsx) {
			return ER_EXS;
		}
		tc_cpy(fs->fs_name, fs_name, L_FSNM);
		fs->fs_name[L_FSNM] = TNULL;
		/* 仕様「ファイルシステム名はルートファイルの名称としても
		 * 設定される」。ルートの名前も合わせる。 */
		{
			UB ent[FSB_ENTSZ];

			if (fsb_ent_read(fs, 0, ent) >= 0) {
				fsb_put_tc(ent, FE_NAME, fs_name, L_FSNM);
				(void)fsb_ent_write(fs, 0, ent);
			}
		}
	}
	if (fs_locate != 0) {
		tc_cpy(fs->fs_locat, fs_locate, L_DLNM);
		fs->fs_locat[L_DLNM] = TNULL;
	}
	fs->sb_dirty = True;
	return fsb_sync(fs);
}

EXPORT WERR lst_fls(F_ATTACH *buff, W cnt)
{
	W n = 0;
	W i;

	if (cnt < 0) {
		return ER_PAR;
	}
	for (i = 0; i < FS_MAX_ATTACH; i++) {
		if (!fstab[i].used) {
			continue;
		}
		if (buff != 0 && n < cnt) {
			tc_cpy(buff[n].a_name, fstab[i].conn, L_CONNM);
			tc_cpy(buff[n].dev, fstab[i].dev, L_DEVNM);
		}
		n++;
	}
	return n;
}

EXPORT WERR chg_fsm(CONST TC *dev, UW mode)
{
	W fsx;
	W old;

	if (dev == 0) {
		return ER_ADR;
	}
	fsx = fs_by_dev(dev);
	if (fsx < 0) {
		return ER_NOEXS;
	}
	old = (fstab[fsx].ronly ? FS_RONLY : 0) |
	      (fstab[fsx].async ? FS_ASYN : 0);
	if ((mode & ~(UW)(FS_ASYN | FS_RONLY)) != 0) {
		return ER_PAR;
	}
	fstab[fsx].ronly = ((mode & FS_RONLY) != 0);
	fstab[fsx].async = ((mode & FS_ASYN) != 0);
	return old;
}

/* ------------------------------------------------------------------ */
/* ファイルの生成と削除 §1.6.8                                         */
/* ------------------------------------------------------------------ */

/* 新しいファイルのエントリを組み立てる。 */
LOCAL void ent_init(W fsx, UB *ent, CONST TC *name, CONST A_MODE *mode,
                    UH atype, Bool linkfile)
{
	P_USER u;
	STIME now = now_time();
	UH ftype = F_FILE;
	W i;

	for (i = 0; i < FSB_ENTSZ; i++) {
		ent[i] = 0;
	}
	fsb_put32(ent, FE_FLAGS, (UW)(FE_F_USED | (linkfile ? FE_F_LINKF : 0)));
	fsb_put16(ent, FE_ATYPE, atype);
	fsb_put32(ent, FE_NLINK, 0); /* 仕様「生成した時点では 0」 */
	fsb_put64(ent, FE_CTIME, (UD)now);
	fsb_put64(ent, FE_MTIME, (UD)now);
	fsb_put64(ent, FE_ATIME, (UD)now);
	fsb_put_tc(ent, FE_NAME, name, L_FNM);

	if (fstab[fsx].level == F_LEVEL0) {
		/* 仕様 §1.6.4 a: レベル 0 は常にこの内容。 */
		fsb_put16(ent, FE_TYPE, (UH)F_FILE);
		fsb_put16(ent, FE_GRPACC, 0);
		fsb_put16(ent, FE_PUBACC, 0x0FFF);
		return;
	}
	if (mode != 0) {
		ftype = (UH)(F_FILE | (mode->f_ownacc & F_OWNACC));
		fsb_put16(ent, FE_GRPACC, mode->f_grpacc);
		fsb_put16(ent, FE_PUBACC, mode->f_pubacc);
	} else {
		ftype = (UH)(F_FILE | (defmode.f_ownacc & F_OWNACC));
		fsb_put16(ent, FE_GRPACC, defmode.f_grpacc);
		fsb_put16(ent, FE_PUBACC, defmode.f_pubacc);
	}
	fsb_put16(ent, FE_TYPE, ftype);

	/* 仕様「ファイルの所有者名は、そのファイルを生成したユーザ名が
	 * 自動的に設定され、生成した後は一切変更することはできない」 */
	if (prc_user(prc_self(), &u)) {
		TC tmp[L_USRNM + 1];
		W gno = (mode != 0) ? mode->f_grpno : defmode.f_grpno;

		tc_cpy(tmp, u.usr_name, L_USRNM);
		/* 仕様 §1.6.4 b: レベル 1 は隠し名を常に 0 にする。 */
		if (fstab[fsx].level == F_LEVEL1) {
			for (i = P_VISLEN; i < L_USRNM; i++) {
				tmp[i] = TNULL;
			}
		}
		fsb_put_tc(ent, FE_OWNER, tmp, L_USRNM);
		if (gno >= 1 && gno <= N_GRP) {
			CONST TC *g[N_GRP];

			g[0] = u.grp_name1;
			g[1] = u.grp_name2;
			g[2] = u.grp_name3;
			g[3] = u.grp_name4;
			tc_cpy(tmp, g[gno - 1], L_USRNM);
			if (fstab[fsx].level == F_LEVEL1) {
				for (i = P_VISLEN; i < L_USRNM; i++) {
					tmp[i] = TNULL;
				}
			}
			fsb_put_tc(ent, FE_GROUP, tmp, L_USRNM);
		}
	}
}

/* 固定リンクのレコード本体を組み立てる / 取り出す。
 * 仕様「固定リンクはファイルシステム名を持たない」 */
LOCAL void fixlnk_put(UB *p, CONST LINK *l)
{
	fsb_put32(p, 0, l->f_id);
	fsb_put16(p, 4, l->atr1);
	fsb_put16(p, 6, l->atr2);
	fsb_put16(p, 8, l->atr3);
	fsb_put16(p, 10, l->atr4);
	fsb_put16(p, 12, l->atr5);
}

LOCAL void fixlnk_get(CONST UB *p, LINK *l, CONST TC *fsname)
{
	tc_cpy(l->fs_name, fsname, L_FSNM);
	l->f_id = fsb_get32(p, 0);
	l->atr1 = fsb_get16(p, 4);
	l->atr2 = fsb_get16(p, 6);
	l->atr3 = fsb_get16(p, 8);
	l->atr4 = fsb_get16(p, 10);
	l->atr5 = fsb_get16(p, 12);
}

/* 開いている fd の属するファイルを指すリンクを得る。 */
LOCAL void link_of(W fsx, UW fid, LINK *lnk)
{
	tc_cpy(lnk->fs_name, fstab[fsx].fs_name, L_FSNM);
	lnk->f_id = fid;
	lnk->atr1 = 0;
	lnk->atr2 = 0;
	lnk->atr3 = 0;
	lnk->atr4 = 0;
	lnk->atr5 = 0;
}

/* cre_fil / gen_fil の共通部分。 */
LOCAL WERR create_file(LINK *lnk, CONST TC *name, CONST A_MODE *mode,
                       UH atype, W opt, Bool linkfile, CONST F_LINK *ref)
{
	W fsx;
	W fid;
	UB ent[FSB_ENTSZ];
	ERR er;
	W me = prc_self();

	if (lnk == 0) {
		return ER_ADR;
	}
	if ((opt & ~(W)(F_FIX | F_FILEID)) != 0) {
		return ER_PAR;
	}
	/* 生成先のファイルシステムは、リンクが示すもの。
	 * 仕様 cre_fil は「lnk が示すファイルと同一ファイルシステム上に
	 * 生成する」。空なら作業ファイルのファイルシステム。 */
	fsx = fs_of_link(lnk);
	if (fsx < 0) {
		if (fenv[me].has_work) {
			fsx = fs_of_link(&fenv[me].work);
		}
	}
	if (fsx < 0) {
		return ER_NOFS;
	}
	if (fstab[fsx].ronly) {
		return ER_RONLY;
	}
	fid = fsb_ent_alloc(&fstab[fsx], ((opt & F_FILEID) != 0) ? lnk->f_id : 0);
	if (fid < 0) {
		return (WERR)fid;
	}
	ent_init(fsx, ent, name, mode, atype, linkfile);
	if (linkfile && ref != 0) {
		/* リンクファイルの中身は F_LINK そのもの。
		 * レコードとしてではなくデータとして置く。 */
		UB body[sizeof(F_LINK)];
		W i;
		CONST UB *src = (CONST UB *)(CONST VP)ref;

		for (i = 0; i < (W)sizeof(F_LINK); i++) {
			body[i] = src[i];
		}
		er = fsb_stream_write(&fstab[fsx], ent, body, (W)sizeof(F_LINK));
		fsb_put32(ent, FE_SIZE, (UW)sizeof(F_LINK));
	} else {
		er = fsb_stream_write(&fstab[fsx], ent, 0, 0);
	}
	if (er < 0) {
		return er;
	}
	er = fsb_ent_write(&fstab[fsx], (UW)fid, ent);
	if (er < 0) {
		return er;
	}
	link_of(fsx, (UW)fid, lnk);
	fstab[fsx].sb_dirty = True;
	return fid;
}

EXPORT WERR cre_fil(LINK *lnk, CONST TC *name, CONST A_MODE *mode, UH atype,
                    W opt)
{
	return create_file(lnk, name, mode, atype, opt, False, 0);
}

EXPORT ERR cre_lnk(LINK *lnk, CONST F_LINK *ref, W opt)
{
	WERR r;

	if (ref == 0) {
		return ER_ADR;
	}
	r = create_file(lnk, ref->f_name, 0, ref->f_atype, opt, True, ref);
	return (r < 0) ? (ERR)r : ER_OK;
}

EXPORT WERR gen_fil(LINK *lnk, CONST TC *name, CONST F_STATE *stat,
                    CONST F_LINK *ref, W opt)
{
	A_MODE m;
	WERR r;

	/* 仕様 gen_fil は「ファイル管理情報を指定してファイルを生成する」。
	 * 所有者名は生成者が入るので、指定できるのはモードと属性だけ。 */
	if (stat == 0) {
		return ER_ADR;
	}
	m.f_ownacc = (UH)(stat->f_type & F_OWNACC);
	m.f_grpacc = stat->f_grpacc;
	m.f_pubacc = stat->f_pubacc;
	m.f_grpno = 0;
	r = create_file(lnk, name, &m, stat->f_atype, opt, (ref != 0), ref);
	if (r < 0) {
		return r;
	}
	/* 保存期限など、生成後に入れられるものを反映する。 */
	{
		W fsx = fs_of_link(lnk);
		UB ent[FSB_ENTSZ];

		if (fsx >= 0 && fsb_ent_read(&fstab[fsx], lnk->f_id, ent) >= 0) {
			UH t = fsb_get16(ent, FE_TYPE);

			t = (UH)((t & ~(UH)(F_RONLY | F_PERM | F_APLATR1 | F_APLATR2)) |
			         (stat->f_type &
			          (UH)(F_RONLY | F_PERM | F_APLATR1 | F_APLATR2)));
			fsb_put16(ent, FE_TYPE, t);
			fsb_put64(ent, FE_LTIME, (UD)stat->f_ltime);
			(void)fsb_ent_write(&fstab[fsx], lnk->f_id, ent);
		}
	}
	return r;
}

/* ------------------------------------------------------------------ */
/* オープンとクローズ §1.6.8                                           */
/* ------------------------------------------------------------------ */

/* そのファイルを開いている OpenFile を探す。無ければ -1。 */
LOCAL W find_open(W fsx, UW fid)
{
	W i;

	for (i = 0; i < FS_MAX_OPEN; i++) {
		if (ofl[i].used && ofl[i].fs == fsx && ofl[i].fid == fid) {
			return i;
		}
	}
	return -1;
}

/* 作業ファイルにしているプロセスがあるか（del_fil の ER_BUSY）。 */
LOCAL Bool is_work_of_any(W fsx, UW fid)
{
	W i;

	for (i = 1; i <= PRC_MAX_PRCID; i++) {
		if (fenv[i].has_work && fenv[i].work.f_id == fid &&
		    fs_of_link(&fenv[i].work) == fsx) {
			return True;
		}
	}
	return False;
}

/* 写しを書き戻す。 */
LOCAL ERR flush_open(OpenFile *o)
{
	UB ent[FSB_ENTSZ];
	Fs *fs;
	ERR er;

	if (!o->dirty) {
		return ER_OK;
	}
	fs = &fstab[o->fs];
	if (fs->ronly) {
		return ER_RONLY;
	}
	er = fsb_ent_read(fs, o->fid, ent);
	if (er < 0) {
		return er;
	}
	er = fsb_stream_write(fs, ent, o->buf, o->len);
	if (er < 0) {
		return er;
	}
	{
		/* f_size は仕様で「ファイルの総バイト数」。リンクレコードは
		 * 「ファイル管理情報としての総バイト数には、カウントされない」
		 * ので、データレコードの本体だけを数える。 */
		W off = 0;
		W total = 0;
		W nrec = 0;

		while (off + REC_HDR <= o->len) {
			W sz = REC_SIZE(o, off);
			W t = REC_TYPE(o, off);

			if (t != F_LINKREC) {
				total += sz;
			}
			off += REC_HDR + REC_PAD(sz);
			nrec++;
		}
		fsb_put32(ent, FE_SIZE, (UW)total);
		fsb_put32(ent, FE_NREC, (UW)nrec);
	}
	fsb_put64(ent, FE_MTIME, (UD)now_time());
	er = fsb_ent_write(fs, o->fid, ent);
	if (er >= 0) {
		o->dirty = False;
	}
	fs->sb_dirty = True;
	return er;
}

EXPORT WERR opn_fil(CONST LINK *lnk, W o_mode, CONST TC *pwd)
{
	W fsx;
	UB ent[FSB_ENTSZ];
	W acc = o_mode & (F_READ | F_WRITE);
	W of;
	W me = prc_self();
	W i;
	OpenFile *o;
	ERR er;

	if (lnk == 0) {
		return ER_ADR;
	}
	if (acc != F_READ && acc != F_WRITE && acc != F_UPDATE) {
		return ER_PAR;
	}
	if ((o_mode & ~(W)(F_UPDATE | F_EXCL | F_WEXCL)) != 0) {
		return ER_PAR;
	}
	if ((o_mode & F_EXCL) != 0 && (o_mode & F_WEXCL) != 0) {
		return ER_PAR;
	}
	if (me < 1 || me > PRC_MAX_PRCID) {
		return ER_CTX;
	}
	fsx = fs_of_link(lnk);
	if (fsx < 0) {
		return ER_NOFS;
	}
	er = ent_load(fsx, lnk->f_id, ent);
	if (er < 0) {
		return er;
	}
	if (!ent_exists(ent)) {
		return ER_NOEXS;
	}
	if ((acc & F_WRITE) != 0 && fstab[fsx].ronly) {
		return ER_RONLY;
	}
	if ((acc & F_WRITE) != 0 &&
	    (fsb_get16(ent, FE_TYPE) & F_RONLY) != 0) {
		return ER_RONLY;
	}
	if (!acc_ok(fsx, ent, acc)) {
		return ER_ACCES;
	}
	/* パスワード。設定されていれば一致が要る。 */
	{
		TC fpw[L_CONNM + 1];

		fsb_get_tc(ent, FE_PWD, fpw, L_CONNM);
		if (fpw[0] != TNULL && !tc_eq(fpw, pwd, L_CONNM)) {
			return ER_PWD;
		}
	}

	/* 共有の判定。仕様 §1.6.5 の表のとおり。 */
	of = find_open(fsx, lnk->f_id);
	if (of >= 0) {
		o = &ofl[of];
		if (o->nexcl > 0 || (o_mode & F_EXCL) != 0) {
			return ER_BUSY;
		}
		if ((o->nwexcl > 0 && (acc & F_WRITE) != 0) ||
		    ((o_mode & F_WEXCL) != 0 && o->nwrite > 0)) {
			return ER_BUSY;
		}
	} else {
		for (i = 0; i < FS_MAX_OPEN; i++) {
			if (!ofl[i].used) {
				of = i;
				break;
			}
		}
		if (of < 0) {
			return ER_LIMIT;
		}
		o = &ofl[of];
		o->used = True;
		o->fs = fsx;
		o->fid = lnk->f_id;
		o->nref = 0;
		o->nexcl = 0;
		o->nwexcl = 0;
		o->nwrite = 0;
		o->buf = 0;
		o->len = 0;
		o->cap = 0;
		o->dirty = False;
		o->lock_rec = -1;
		o->lock_pid = 0;
		/* レコードストリームの写しを作る。 */
		{
			W total = (W)fsb_get32(ent, FE_NBLK) * FSB_DATASZ;

			if (total > 0) {
				o->buf = (UB *)kmalloc((UD)total);
				if (o->buf == 0) {
					o->used = False;
					return ER_NOSPC;
				}
				o->cap = total;
				o->len = fsb_stream_read(&fstab[fsx],
				                         fsb_get32(ent, FE_FIRST), o->buf,
				                         total);
				if (o->len < 0) {
					kfree(o->buf);
					o->used = False;
					return ER_IO;
				}
				/* ストリームの正味の長さは、レコードを辿って決める。
				 * **ブロックの余りはゼロ埋めなので、そのまま使うと
				 * 大きさ 0 のレコードが無数にあるように見える。** */
				{
					W off = 0;

					while (off + REC_HDR <= o->len) {
						W sz = (W)fsb_get32(o->buf, off + 4);
						W t = (W)fsb_get16(o->buf, off);
						W nx = off + REC_HDR + REC_PAD(sz);

						if (nx > o->len || t > F_RECTYPE_MAX ||
						    (t == 0 && sz == 0 && nx == off + REC_HDR)) {
							break;
						}
						off = nx;
					}
					o->len = off;
				}
			}
		}
	}

	/* 空きディスクリプタ。 */
	for (i = 0; i < FD_MAX; i++) {
		if (!fdt[me][i].used) {
			Fdesc *f = &fdt[me][i];

			f->used = True;
			f->of = of;
			f->mode = o_mode;
			f->cur = 0; /* 仕様「先頭レコードが現在レコードとなる」 */
			o->nref++;
			if ((o_mode & F_EXCL) != 0) {
				o->nexcl++;
			}
			if ((o_mode & F_WEXCL) != 0) {
				o->nwexcl++;
			}
			if ((acc & F_WRITE) != 0) {
				o->nwrite++;
			}
			fsb_put64(ent, FE_ATIME, (UD)now_time());
			(void)fsb_ent_write(&fstab[fsx], lnk->f_id, ent);
			return i + 1;
		}
	}
	if (o->nref == 0) {
		if (o->buf != 0) {
			kfree(o->buf);
			o->buf = 0;
		}
		o->used = False;
	}
	return ER_LIMIT;
}

EXPORT ERR cls_fil(W fd)
{
	Fdesc *f = get_fd(fd);
	OpenFile *o;
	ERR er = ER_OK;

	if (f == 0) {
		return ER_FD;
	}
	o = &ofl[f->of];
	if ((f->mode & F_EXCL) != 0) {
		o->nexcl--;
	}
	if ((f->mode & F_WEXCL) != 0) {
		o->nwexcl--;
	}
	if ((f->mode & F_WRITE) != 0) {
		o->nwrite--;
	}
	/* 自分がかけたロックは外す。 */
	if (o->lock_pid == prc_self() && o->lock_rec >= 0) {
		o->lock_rec = -1;
		o->lock_pid = 0;
	}
	f->used = False;
	o->nref--;
	if (o->nref <= 0) {
		er = flush_open(o);
		if (o->buf != 0) {
			kfree(o->buf);
			o->buf = 0;
		}
		o->cap = 0;
		o->len = 0;
		o->used = False;
	}
	return er;
}

EXPORT WERR del_fil(CONST LINK *org, CONST LINK *lnk, W force)
{
	W fsx;
	UB ent[FSB_ENTSZ];
	W nzero = 0;
	ERR er;

	if (lnk == 0) {
		return ER_ADR;
	}
	fsx = fs_of_link(lnk);
	if (fsx < 0) {
		return ER_NOFS;
	}
	if (fstab[fsx].ronly) {
		return ER_RONLY;
	}
	er = ent_load(fsx, lnk->f_id, ent);
	if (er < 0) {
		return er;
	}
	if (!ent_exists(ent)) {
		return ER_NOEXS;
	}
	/* 仕様「削除不可属性が設定されているとき」 */
	if ((fsb_get16(ent, FE_TYPE) & F_PERM) != 0) {
		return ER_PERM;
	}
	/* 仕様「オープンしているプロセスが存在しているとき」
	 * 「作業ファイルとしているプロセスが存在しているとき」 */
	if (find_open(fsx, lnk->f_id) >= 0) {
		return ER_BUSY;
	}
	if (is_work_of_any(fsx, lnk->f_id)) {
		return ER_BUSY;
	}

	if (org != 0) {
		/* 親ファイルの中のリンクレコードを消して参照カウントを減らす。
		 * **ここは外殻のための経路**で、いまは未実装。親を開いて
		 * fnd_lnk → del_rec すれば同じことができる。 */
		return ER_NOSPT;
	}
	/* 仕様「org = NULL のときは、参照カウントが 0 の場合にファイルを
	 * 削除する。参照カウントが 0 でない場合はエラー」 */
	if (fsb_get32(ent, FE_NLINK) != 0) {
		return ER_BUSY;
	}

	/* 中に固定リンクが入っているか調べる。 */
	{
		W total = (W)fsb_get32(ent, FE_NBLK) * FSB_DATASZ;
		UB *tmp = 0;
		W len = 0;
		W off = 0;

		if (total > 0) {
			tmp = (UB *)kmalloc((UD)total);
			if (tmp == 0) {
				return ER_NOSPC;
			}
			len = fsb_stream_read(&fstab[fsx], fsb_get32(ent, FE_FIRST), tmp,
			                      total);
			if (len < 0) {
				kfree(tmp);
				return ER_IO;
			}
		}
		while (off + REC_HDR <= len) {
			W sz = (W)fsb_get32(tmp, off + 4);
			W t = (W)fsb_get16(tmp, off);
			W nx = off + REC_HDR + REC_PAD(sz);

			if (nx > len || t > F_RECTYPE_MAX) {
				break;
			}
			if (t == F_LINKREC) {
				if (force == 0) {
					kfree(tmp);
					return ER_REC; /* 仕様: force=0 ならエラー */
				}
				{
					LINK l;
					W after = 0;

					fixlnk_get(tmp + off + REC_HDR, &l,
					           fstab[fsx].fs_name);
					if (ent_nlink(fsx, l.f_id, -1, &after) >= 0 &&
					    after == 0) {
						nzero++;
					}
				}
			}
			off = nx;
		}
		if (tmp != 0) {
			kfree(tmp);
		}
	}

	(void)fsb_stream_free(&fstab[fsx], ent);
	fsb_put32(ent, FE_FLAGS, 0);
	er = fsb_ent_write(&fstab[fsx], lnk->f_id, ent);
	if (er < 0) {
		return er;
	}
	fstab[fsx].sb_dirty = True;
	return nzero;
}

/* ------------------------------------------------------------------ */
/* 現在レコードの移動と検索 §1.6.8                                     */
/* ------------------------------------------------------------------ */

EXPORT ERR see_rec(W fd, W offset, W mode, W *recnum)
{
	Fdesc *f = get_fd(fd);
	OpenFile *o;
	W n;
	W total;

	if (f == 0) {
		return ER_FD;
	}
	o = &ofl[f->of];
	total = rec_count(o);
	if (mode == 0) {
		n = f->cur + offset;
	} else if (mode > 0) {
		if (offset < 0) {
			return ER_PAR;
		}
		n = offset;
	} else {
		if (offset > 0) {
			return ER_PAR;
		}
		n = total + offset;
	}
	/* 仕様「指定した移動先が存在するレコードの範囲を越える場合はエラーと
	 * なり、現在レコードは変化しない」。**終端レコード (= total) は
	 * 範囲内**で、そこへは移動できる。 */
	if (n < 0 || n > total) {
		return ER_REC;
	}
	f->cur = n;
	if (recnum != 0) {
		*recnum = n;
	}
	return ER_OK;
}

/* 検索の開始位置と向きを決める。見つからなければ -1。 */
LOCAL W search(CONST OpenFile *o, W cur, W mode, W total, W *dir)
{
	switch (mode & F_DIRMASK) {
	case F_FWD:
		*dir = 1;
		return cur;
	case F_NFWD:
		*dir = 1;
		return cur + 1;
	case F_BWD:
		*dir = -1;
		return cur;
	case F_NBWD:
		*dir = -1;
		return cur - 1;
	case F_TOPEND:
		*dir = 1;
		return 0;
	case F_ENDTOP:
		*dir = -1;
		return total - 1;
	default:
		break;
	}
	(void)o;
	*dir = 0;
	return -1;
}

EXPORT WERR fnd_rec(W fd, W mode, UW typemask, UH subtype, W *recnum)
{
	Fdesc *f = get_fd(fd);
	OpenFile *o;
	W total;
	W dir = 0;
	W i;

	if (f == 0) {
		return ER_FD;
	}
	if ((mode & F_DIRMASK) > F_ENDTOP) {
		return ER_PAR;
	}
	if (typemask == 0) {
		return ER_REC; /* 仕様「typemask=0 の場合も含む」 */
	}
	o = &ofl[f->of];
	total = rec_count(o);
	i = search(o, f->cur, mode, total, &dir);
	for (; i >= 0 && i < total; i += dir) {
		W off = rec_at(o, i);
		W t;

		if (off < 0) {
			break;
		}
		t = REC_TYPE(o, off);
		if ((typemask & F_TYPEMASK(t)) == 0) {
			continue;
		}
		/* 仕様「0 全サブタイプが対象(サブタイプのチェックなし)」 */
		if (subtype != 0 && REC_SUB(o, off) != subtype) {
			continue;
		}
		f->cur = i;
		if (recnum != 0) {
			*recnum = i;
		}
		return t;
	}
	return ER_REC;
}

EXPORT ERR fnd_lnk(W fd, W mode, LINK *lnk, UH subtype, W *recnum)
{
	Fdesc *f = get_fd(fd);
	OpenFile *o;
	W total;
	W dir = 0;
	W i;
	W cond = mode & F_SMASK;
	TC want_name[L_FNM + 1];
	Bool have_name = False;

	if (f == 0) {
		return ER_FD;
	}
	if ((mode & F_DIRMASK) > F_ENDTOP) {
		return ER_PAR;
	}
	if (cond != 0 && lnk == 0) {
		return ER_ADR;
	}
	o = &ofl[f->of];
	if ((cond & F_SNAME) != 0) {
		W fsx = fs_of_link(lnk);
		UB ent[FSB_ENTSZ];

		if (fsx < 0) {
			return ER_NOFS;
		}
		if (ent_load(fsx, lnk->f_id, ent) < 0 || !ent_exists(ent)) {
			return ER_NOEXS;
		}
		fsb_get_tc(ent, FE_NAME, want_name, L_FNM);
		have_name = True;
	}
	total = rec_count(o);
	i = search(o, f->cur, mode, total, &dir);
	for (; i >= 0 && i < total; i += dir) {
		W off = rec_at(o, i);
		LINK cur;

		if (off < 0) {
			break;
		}
		if (REC_TYPE(o, off) != F_LINKREC) {
			continue;
		}
		if (subtype != 0 && REC_SUB(o, off) != subtype) {
			continue;
		}
		fixlnk_get(REC_BODY(o, off), &cur, fstab[o->fs].fs_name);
		if ((cond & F_SFILE) != 0 && cur.f_id != lnk->f_id) {
			continue;
		}
		if (have_name) {
			UB ent[FSB_ENTSZ];
			TC nm[L_FNM + 1];

			if (ent_load(o->fs, cur.f_id, ent) < 0) {
				continue;
			}
			fsb_get_tc(ent, FE_NAME, nm, L_FNM);
			if (!tc_eq(nm, want_name, L_FNM)) {
				continue;
			}
		}
		if ((cond & F_SATR1) != 0 && cur.atr1 != lnk->atr1) {
			continue;
		}
		if ((cond & F_SATR2) != 0 && cur.atr2 != lnk->atr2) {
			continue;
		}
		if ((cond & F_SATR3) != 0 && cur.atr3 != lnk->atr3) {
			continue;
		}
		if ((cond & F_SATR4) != 0 && cur.atr4 != lnk->atr4) {
			continue;
		}
		if ((cond & F_SATR5) != 0 && cur.atr5 != lnk->atr5) {
			continue;
		}
		f->cur = i;
		if (recnum != 0) {
			*recnum = i;
		}
		if (lnk != 0) {
			*lnk = cur;
		}
		return ER_OK;
	}
	return ER_REC;
}

/* ------------------------------------------------------------------ */
/* レコードの入出力 §1.6.8                                             */
/* ------------------------------------------------------------------ */

/* 他プロセスがロックしているレコードか。 */
LOCAL Bool locked_by_other(CONST OpenFile *o, W recnum)
{
	return o->lock_rec == recnum && o->lock_pid != prc_self() &&
	       o->lock_pid != 0;
}

EXPORT WERR rea_rec(W fd, W offset, B *buf, W size, W *r_size, UH *subtype)
{
	Fdesc *f = get_fd(fd);
	OpenFile *o;
	W off;
	W sz;
	W t;
	W rest;
	W i;

	if (f == 0) {
		return ER_FD;
	}
	/* 仕様「F_WRITE オープンである」も ER_FD。 */
	if ((f->mode & F_READ) == 0) {
		return ER_FD;
	}
	if (offset < 0 || size < 0) {
		return ER_PAR;
	}
	o = &ofl[f->of];
	if (f->cur >= rec_count(o)) {
		return ER_ENDR; /* 現在レコードは終端レコード */
	}
	if (locked_by_other(o, f->cur)) {
		return ER_LOCK;
	}
	off = rec_at(o, f->cur);
	if (off < 0) {
		return ER_IO;
	}
	t = REC_TYPE(o, off);
	sz = REC_SIZE(o, off);

	if (t == F_LINKREC) {
		LINK l;

		/* 仕様「offset = 0、size ≧ LINK 構造体のサイズ
		 * ( または size = 0 ) でなくてはいけない」 */
		if (offset != 0 || (size != 0 && size < L_LNKSZ)) {
			return ER_PAR;
		}
		fixlnk_get(REC_BODY(o, off), &l, fstab[o->fs].fs_name);
		if (buf != 0 && size >= L_LNKSZ) {
			CONST B *src = (CONST B *)(CONST VP)&l;

			for (i = 0; i < L_LNKSZ; i++) {
				buf[i] = src[i];
			}
		}
		if (r_size != 0) {
			*r_size = L_LNKSZ;
		}
		if (subtype != 0) {
			*subtype = REC_SUB(o, off);
		}
		return t;
	}

	rest = sz - offset;
	if (rest < 0) {
		rest = 0;
	}
	if (buf != 0 && size > 0 && rest > 0) {
		W n = (rest < size) ? rest : size;

		for (i = 0; i < n; i++) {
			buf[i] = (B)REC_BODY(o, off)[offset + i];
		}
	}
	if (r_size != 0) {
		*r_size = rest;
	}
	if (subtype != 0) {
		*subtype = REC_SUB(o, off);
	}
	return t;
}

EXPORT ERR wri_rec(W fd, W offset, CONST B *buf, W size, W *r_size,
                   CONST UH *subtype, UW units)
{
	Fdesc *f = get_fd(fd);
	OpenFile *o;
	W off;
	W sz;
	W t;
	W at;
	ERR er;

	(void)units; /* 連続ブロックの獲得単位はまだ扱わない */
	if (f == 0) {
		return ER_FD;
	}
	if ((f->mode & F_WRITE) == 0) {
		return ER_FD;
	}
	if (size < 0 || offset < -1) {
		return ER_PAR;
	}
	o = &ofl[f->of];
	if (f->cur >= rec_count(o)) {
		return ER_ENDR;
	}
	if (locked_by_other(o, f->cur)) {
		return ER_LOCK;
	}
	off = rec_at(o, f->cur);
	if (off < 0) {
		return ER_IO;
	}
	t = REC_TYPE(o, off);
	sz = REC_SIZE(o, off);

	if (t == F_LINKREC) {
		CONST LINK *l = (CONST LINK *)(CONST VP)buf;
		UB body[FIXLNK_SIZE];
		LINK cur;

		if (offset != 0 || (size != 0 && size < L_LNKSZ)) {
			return ER_PAR;
		}
		if (buf != 0 && size >= L_LNKSZ) {
			/* 仕様「属性データ部分のみが書き込まれ、参照するファイル
			 * 自体を変更することはできない」 */
			fixlnk_get(REC_BODY(o, off), &cur, fstab[o->fs].fs_name);
			cur.atr1 = l->atr1;
			cur.atr2 = l->atr2;
			cur.atr3 = l->atr3;
			cur.atr4 = l->atr4;
			cur.atr5 = l->atr5;
			fixlnk_put(body, &cur);
			{
				W i;

				for (i = 0; i < FIXLNK_SIZE; i++) {
					o->buf[off + REC_HDR + i] = body[i];
				}
			}
			o->dirty = True;
		}
		if (subtype != 0) {
			fsb_put16(o->buf, off + 2, *subtype);
			o->dirty = True;
		}
		if (r_size != 0) {
			*r_size = L_LNKSZ;
		}
		return ER_OK;
	}

	/* 仕様「offset = -1 のときは、常にその時点のレコードの最後に
	 * データを書き込み」 */
	at = (offset < 0) ? sz : offset;
	if (at > sz) {
		return ER_PAR;
	}
	if (subtype != 0) {
		fsb_put16(o->buf, off + 2, *subtype);
		o->dirty = True;
	}
	if (size > 0) {
		W newsz = (at + size > sz) ? at + size : sz;
		W oldpad = REC_PAD(sz);
		W newpad = REC_PAD(newsz);

		if (newpad != oldpad) {
			/* 本体の長さが変わるので、詰め物ごと入れ替える。 */
			er = buf_splice(o, off + REC_HDR + sz, oldpad - sz, 0,
			                newpad - sz);
			if (er < 0) {
				return er;
			}
		}
		if (buf != 0) {
			W i;

			for (i = 0; i < size; i++) {
				o->buf[off + REC_HDR + at + i] = (UB)buf[i];
			}
		}
		fsb_put32(o->buf, off + 4, (UW)newsz);
		o->dirty = True;
		sz = newsz;
	}
	if (r_size != 0) {
		/* 仕様「offset = -1 のときは…*r_size には size の値が格納される」 */
		*r_size = (offset < 0) ? size : (sz - at);
	}
	return ER_OK;
}

/* レコードを 1 つ組み立てて at に差し込む。 */
LOCAL ERR insert_rec(OpenFile *o, W at, CONST B *buf, W size, W type,
                     UH subtype)
{
	UB hdr[REC_HDR];
	W pad = REC_PAD(size);
	ERR er;
	W i;

	er = buf_splice(o, at, 0, 0, REC_HDR + pad);
	if (er < 0) {
		return er;
	}
	fsb_put16(hdr, 0, (UH)type);
	fsb_put16(hdr, 2, subtype);
	fsb_put32(hdr, 4, (UW)size);
	for (i = 0; i < REC_HDR; i++) {
		o->buf[at + i] = hdr[i];
	}
	for (i = 0; i < pad; i++) {
		o->buf[at + REC_HDR + i] =
		    (buf != 0 && i < size) ? (UB)buf[i] : 0;
	}
	return ER_OK;
}

LOCAL ERR add_rec(W fd, CONST B *buf, W size, W type, UH subtype, UW units,
                  Bool append)
{
	Fdesc *f = get_fd(fd);
	OpenFile *o;
	W total;
	W where;
	W at;
	ERR er;

	(void)units;
	if (f == 0) {
		return ER_FD;
	}
	if ((f->mode & F_WRITE) == 0) {
		return ER_FD;
	}
	if (size < 0 || type < F_RECTYPE_MIN || type > F_RECTYPE_MAX) {
		return ER_PAR;
	}
	o = &ofl[f->of];
	if (fstab[o->fs].ronly) {
		return ER_RONLY;
	}
	total = rec_count(o);
	where = append ? total : f->cur;
	at = rec_at(o, where);
	if (at < 0) {
		return ER_IO;
	}

	if (type == F_LINKREC) {
		CONST LINK *l = (CONST LINK *)(CONST VP)buf;
		UB body[FIXLNK_SIZE];
		W fsx;
		W after = 0;

		/* 仕様「buf ≠ NULL、size = LINK 構造体のサイズ、かつリンクの
		 * 示すファイルは同一ファイルシステム上に存在していなくては
		 * いけない」 */
		if (buf == 0 || size != L_LNKSZ) {
			return ER_PAR;
		}
		fsx = fs_of_link(l);
		if (fsx < 0) {
			return ER_NOFS;
		}
		if (fsx != o->fs) {
			return ER_REC; /* 別ファイルシステムを参照している */
		}
		{
			UB ent[FSB_ENTSZ];

			if (ent_load(fsx, l->f_id, ent) < 0 || !ent_exists(ent)) {
				return ER_NOEXS;
			}
		}
		/* 仕様「リンクレコードの挿入により、そのリンクの示すファイルの
		 * 参照カウントは +1 される」 */
		er = ent_nlink(fsx, l->f_id, 1, &after);
		if (er < 0) {
			return er;
		}
		fixlnk_put(body, l);
		er = insert_rec(o, at, (CONST B *)body, FIXLNK_SIZE, type, subtype);
		if (er < 0) {
			(void)ent_nlink(fsx, l->f_id, -1, 0);
			return er;
		}
	} else {
		er = insert_rec(o, at, buf, size, type, subtype);
		if (er < 0) {
			return er;
		}
	}
	/* **他の fd の現在レコードをずらす。**（仕様 §1.6.3） */
	shift_cur(f->of, where, 1, f);
	if (!append && f->cur >= where) {
		f->cur++; /* 自分も挿入したレコードの後ろを指し続ける */
	}
	return ER_OK;
}

EXPORT ERR ins_rec(W fd, CONST B *buf, W size, W type, UH subtype, UW units)
{
	return add_rec(fd, buf, size, type, subtype, units, False);
}

EXPORT ERR apd_rec(W fd, CONST B *buf, W size, W type, UH subtype, UW units)
{
	return add_rec(fd, buf, size, type, subtype, units, True);
}

EXPORT WERR del_rec(W fd)
{
	Fdesc *f = get_fd(fd);
	OpenFile *o;
	W off;
	W sz;
	W t;
	W ret = 0;
	ERR er;

	if (f == 0) {
		return ER_FD;
	}
	if ((f->mode & F_WRITE) == 0) {
		return ER_FD;
	}
	o = &ofl[f->of];
	if (f->cur >= rec_count(o)) {
		return ER_ENDR;
	}
	if (locked_by_other(o, f->cur)) {
		return ER_LOCK;
	}
	/* 仕様「他のオープンで現在レコードとなっているときはエラー」 */
	if (cur_used_by_other(f->of, f->cur, f)) {
		return ER_BUSY;
	}
	off = rec_at(o, f->cur);
	if (off < 0) {
		return ER_IO;
	}
	t = REC_TYPE(o, off);
	sz = REC_SIZE(o, off);
	if (t == F_LINKREC) {
		LINK l;
		W after = 0;

		fixlnk_get(REC_BODY(o, off), &l, fstab[o->fs].fs_name);
		if (ent_nlink(o->fs, l.f_id, -1, &after) >= 0 && after == 0) {
			ret = 1; /* 仕様「参照カウント=0 となった」 */
		}
	}
	er = buf_splice(o, off, REC_HDR + REC_PAD(sz), 0, 0);
	if (er < 0) {
		return er;
	}
	/* 仕様「削除した次のレコードに現在レコードを移動する」。
	 * 番号は変わらず、指す先が次のレコードになる。 */
	shift_cur(f->of, f->cur + 1, -1, f);
	return ret;
}

EXPORT ERR trc_rec(W fd, W size)
{
	Fdesc *f = get_fd(fd);
	OpenFile *o;
	W off;
	W sz;
	ERR er;

	if (f == 0) {
		return ER_FD;
	}
	if ((f->mode & F_WRITE) == 0) {
		return ER_FD;
	}
	if (size < 0) {
		return ER_PAR;
	}
	o = &ofl[f->of];
	if (f->cur >= rec_count(o)) {
		return ER_ENDR;
	}
	if (locked_by_other(o, f->cur)) {
		return ER_LOCK;
	}
	off = rec_at(o, f->cur);
	if (off < 0) {
		return ER_IO;
	}
	if (REC_TYPE(o, off) == F_LINKREC) {
		return ER_REC; /* 仕様「リンクレコードのとき…エラー」 */
	}
	sz = REC_SIZE(o, off);
	if (sz <= size) {
		return ER_OK; /* 仕様「レコードサイズ≦ size のときは何もしない」 */
	}
	er = buf_splice(o, off + REC_HDR + size, REC_PAD(sz) - size, 0,
	                REC_PAD(size) - size);
	if (er < 0) {
		return er;
	}
	fsb_put32(o->buf, off + 4, (UW)size);
	o->dirty = True;
	return ER_OK;
}

EXPORT ERR xch_fil(W fd_1, W fd_2)
{
	Fdesc *a = get_fd(fd_1);
	Fdesc *b = get_fd(fd_2);
	OpenFile *oa;
	OpenFile *ob;
	UB *tb;
	W tl;
	W tc;

	if (a == 0 || b == 0) {
		return ER_FD;
	}
	/* 仕様「排他モードで更新用オープンしてなくてはいけない」 */
	if ((a->mode & (F_UPDATE | F_EXCL)) != (F_UPDATE | F_EXCL) ||
	    (b->mode & (F_UPDATE | F_EXCL)) != (F_UPDATE | F_EXCL)) {
		return ER_FD;
	}
	if (a->of == b->of) {
		return ER_PAR; /* 同一ファイル */
	}
	oa = &ofl[a->of];
	ob = &ofl[b->of];
	if (oa->fs != ob->fs) {
		return ER_XFS;
	}
	if (oa->lock_rec >= 0 || ob->lock_rec >= 0) {
		return ER_LOCK;
	}
	tb = oa->buf;
	tl = oa->len;
	tc = oa->cap;
	oa->buf = ob->buf;
	oa->len = ob->len;
	oa->cap = ob->cap;
	ob->buf = tb;
	ob->len = tl;
	ob->cap = tc;
	oa->dirty = True;
	ob->dirty = True;
	/* 仕様「交換後の現在レコードは、それぞれ先頭レコードとなる」 */
	a->cur = 0;
	b->cur = 0;
	return ER_OK;
}

EXPORT ERR loc_rec(W fd, W mode)
{
	Fdesc *f = get_fd(fd);
	OpenFile *o;
	W me = prc_self();

	if (f == 0) {
		return ER_FD;
	}
	o = &ofl[f->of];
	switch (mode) {
	case F_UNLOCK:
		if (o->lock_rec == f->cur && o->lock_pid == me) {
			o->lock_rec = -1;
			o->lock_pid = 0;
		}
		return ER_OK;
	case F_LOCK:
	case F_TSLOCK:
		if (o->lock_rec >= 0 && o->lock_pid != me) {
			return ER_LOCK;
		}
		o->lock_rec = f->cur;
		o->lock_pid = me;
		return ER_OK;
	case F_CKLOCK:
		return (o->lock_rec == f->cur && o->lock_pid != 0) ? ER_LOCK
		                                                   : ER_OK;
	default:
		break;
	}
	return ER_PAR;
}

/* 仕様 §1.6.8 map_rec / ump_rec。
 * レコードをメモリに写像する。写像の仕掛け（MAPREC 区画・ページ単位の
 * 遅延読み込み・書き戻し）が無いので、はっきり断る。 */
EXPORT WERR map_rec(W fd, W offset, B **addr, W size, W mode)
{
	(void)fd;
	(void)offset;
	(void)addr;
	(void)size;
	(void)mode;
	return ER_NOSPT;
}

EXPORT ERR ump_rec(W fd, W mapid)
{
	(void)fd;
	(void)mapid;
	return ER_NOSPT;
}

/* ------------------------------------------------------------------ */
/* 状態の取得と変更 §1.6.8                                             */
/* ------------------------------------------------------------------ */

LOCAL void ent_to_state(CONST UB *ent, F_STATE *st)
{
	st->f_type = fsb_get16(ent, FE_TYPE);
	st->f_atype = fsb_get16(ent, FE_ATYPE);
	fsb_get_tc(ent, FE_OWNER, st->f_owner, L_USRNM - 1);
	fsb_get_tc(ent, FE_GROUP, st->f_group, L_USRNM - 1);
	/* 仕様「ファイル所有者名 (隠し名は常に0)」 */
	st->f_owner[L_USRNM - 2] = TNULL;
	st->f_owner[L_USRNM - 1] = TNULL;
	st->f_group[L_USRNM - 2] = TNULL;
	st->f_group[L_USRNM - 1] = TNULL;
	st->f_grpacc = fsb_get16(ent, FE_GRPACC);
	st->f_pubacc = fsb_get16(ent, FE_PUBACC);
	st->f_nlink = (H)fsb_get32(ent, FE_NLINK);
	st->f_index = 0;
	st->f_size = (W)fsb_get32(ent, FE_SIZE);
	st->f_nblk = (W)fsb_get32(ent, FE_NBLK);
	st->f_nrec = (W)fsb_get32(ent, FE_NREC);
	st->f_ltime = (STIME)fsb_get64(ent, FE_LTIME);
	st->f_atime = (STIME)fsb_get64(ent, FE_ATIME);
	st->f_mtime = (STIME)fsb_get64(ent, FE_MTIME);
	st->f_ctime = (STIME)fsb_get64(ent, FE_CTIME);
}

LOCAL void fs_to_locate(W fsx, F_LOCATE *lo)
{
	lo->fs_ctime = fstab[fsx].ctime;
	tc_cpy(lo->fs_name, fstab[fsx].fs_name, L_FSNM);
	tc_cpy(lo->fs_locat, fstab[fsx].fs_locat, L_DLNM);
	tc_cpy(lo->fs_dev, fstab[fsx].dev, L_DEVNM);
}

EXPORT WERR fil_sts(CONST LINK *lnk, TC *name, F_STATE *stat, F_LOCATE *locat)
{
	W fsx;
	UB ent[FSB_ENTSZ];
	ERR er;

	if (lnk == 0) {
		return ER_ADR;
	}
	fsx = fs_of_link(lnk);
	if (fsx < 0) {
		return ER_NOFS;
	}
	er = ent_load(fsx, lnk->f_id, ent);
	if (er < 0) {
		return er;
	}
	if (!ent_exists(ent)) {
		return ER_NOEXS;
	}
	if (name != 0) {
		fsb_get_tc(ent, FE_NAME, name, L_FNM);
	}
	if (stat != 0) {
		ent_to_state(ent, stat);
	}
	if (locat != 0) {
		fs_to_locate(fsx, locat);
	}
	/* 戻り値はファイルタイプ（0 = 通常ファイル、1 = リンクファイル）。 */
	return ((fsb_get32(ent, FE_FLAGS) & FE_F_LINKF) != 0) ? 1 : 0;
}

EXPORT WERR ofl_sts(W fd, TC *name, F_STATE *stat, F_LOCATE *locat)
{
	Fdesc *f = get_fd(fd);
	LINK l;

	if (f == 0) {
		return ER_FD;
	}
	link_of(ofl[f->of].fs, ofl[f->of].fid, &l);
	return fil_sts(&l, name, stat, locat);
}

EXPORT WERR chk_fil(CONST LINK *lnk, W mode, CONST TC *pwd)
{
	W fsx;
	UB ent[FSB_ENTSZ];
	ERR er;
	W acc = mode & (F_READ | F_WRITE | F_EXCUTE);

	if (lnk == 0) {
		return ER_ADR;
	}
	if ((mode & ~(W)(F_READ | F_WRITE | F_EXCUTE | F_PASWD)) != 0) {
		return ER_PAR;
	}
	fsx = fs_of_link(lnk);
	if (fsx < 0) {
		return ER_NOFS;
	}
	er = ent_load(fsx, lnk->f_id, ent);
	if (er < 0) {
		return er;
	}
	if (!ent_exists(ent)) {
		return ER_NOEXS;
	}
	if ((mode & F_PASWD) != 0) {
		TC fpw[L_CONNM + 1];

		fsb_get_tc(ent, FE_PWD, fpw, L_CONNM);
		if (fpw[0] != TNULL) {
			if (!tc_eq(fpw, pwd, L_CONNM)) {
				return ER_PWD;
			}
		}
	}
	if (!acc_ok(fsx, ent, acc)) {
		return ER_ACCES;
	}
	return ((fsb_get32(ent, FE_FLAGS) & FE_F_LINKF) != 0) ? 1 : 0;
}

EXPORT ERR chg_fmd(CONST LINK *lnk, CONST A_MODE *mode)
{
	W fsx;
	UB ent[FSB_ENTSZ];
	ERR er;
	UH t;

	if (lnk == 0 || mode == 0) {
		return ER_ADR;
	}
	fsx = fs_of_link(lnk);
	if (fsx < 0) {
		return ER_NOFS;
	}
	if (fstab[fsx].ronly) {
		return ER_RONLY;
	}
	er = ent_load(fsx, lnk->f_id, ent);
	if (er < 0) {
		return er;
	}
	if (!ent_exists(ent)) {
		return ER_NOEXS;
	}
	/* 仕様「ファイルに設定されたアクセス管理情報は、その所有者のみが
	 * 変更できる」 */
	if (!is_owner(fsx, ent)) {
		return ER_ACCES;
	}
	if (fstab[fsx].level == F_LEVEL0) {
		return ER_OK; /* レベル 0 は常に固定（仕様 §1.6.4 a） */
	}
	t = fsb_get16(ent, FE_TYPE);
	/* 仕様 F_NOCHG「変更しない」 */
	if ((mode->f_ownacc & F_NOCHG) == 0) {
		t = (UH)((t & ~(UH)F_OWNACC) | (mode->f_ownacc & F_OWNACC));
		fsb_put16(ent, FE_TYPE, t);
	}
	if ((mode->f_grpacc & F_NOCHG) == 0) {
		fsb_put16(ent, FE_GRPACC, mode->f_grpacc);
	}
	if ((mode->f_pubacc & F_NOCHG) == 0) {
		fsb_put16(ent, FE_PUBACC, mode->f_pubacc);
	}
	return fsb_ent_write(&fstab[fsx], lnk->f_id, ent);
}

EXPORT ERR chg_fat(CONST LINK *lnk, W attr)
{
	W fsx;
	UB ent[FSB_ENTSZ];
	ERR er;
	UH t;
	UH bit;
	Bool set;

	if (lnk == 0) {
		return ER_ADR;
	}
	switch (attr) {
	case F_SETRONLY:
		bit = F_RONLY;
		set = True;
		break;
	case F_RSTRONLY:
		bit = F_RONLY;
		set = False;
		break;
	case F_SETPERM:
		bit = F_PERM;
		set = True;
		break;
	case F_RSTPERM:
		bit = F_PERM;
		set = False;
		break;
	case F_SETA1:
		bit = F_APLATR1;
		set = True;
		break;
	case F_RSTA1:
		bit = F_APLATR1;
		set = False;
		break;
	case F_SETA2:
		bit = F_APLATR2;
		set = True;
		break;
	case F_RSTA2:
		bit = F_APLATR2;
		set = False;
		break;
	default:
		return ER_PAR;
	}
	fsx = fs_of_link(lnk);
	if (fsx < 0) {
		return ER_NOFS;
	}
	if (fstab[fsx].ronly) {
		return ER_RONLY;
	}
	er = ent_load(fsx, lnk->f_id, ent);
	if (er < 0) {
		return er;
	}
	if (!ent_exists(ent)) {
		return ER_NOEXS;
	}
	if (!is_owner(fsx, ent)) {
		return ER_ACCES;
	}
	t = fsb_get16(ent, FE_TYPE);
	t = set ? (UH)(t | bit) : (UH)(t & ~bit);
	fsb_put16(ent, FE_TYPE, t);
	return fsb_ent_write(&fstab[fsx], lnk->f_id, ent);
}

EXPORT ERR chg_fpw(W fd, CONST TC *pwd)
{
	Fdesc *f = get_fd(fd);
	UB ent[FSB_ENTSZ];
	OpenFile *o;
	ERR er;

	if (f == 0) {
		return ER_FD;
	}
	o = &ofl[f->of];
	if (fstab[o->fs].ronly) {
		return ER_RONLY;
	}
	er = ent_load(o->fs, o->fid, ent);
	if (er < 0) {
		return er;
	}
	if (!is_owner(o->fs, ent)) {
		return ER_ACCES;
	}
	/* pwd = NULL でパスワードを外す。 */
	fsb_put_tc(ent, FE_PWD, pwd, L_CONNM);
	return fsb_ent_write(&fstab[o->fs], o->fid, ent);
}

EXPORT ERR chg_fnm(CONST LINK *lnk, CONST TC *name)
{
	W fsx;
	UB ent[FSB_ENTSZ];
	ERR er;

	if (lnk == 0 || name == 0) {
		return ER_ADR;
	}
	if (tc_len(name, L_FNM + 1) > L_FNM) {
		return ER_FNAME;
	}
	/* 仕様 §1.6.2「／ ： ≡ の記号は…特殊コードであるため、ファイル名
	 * としては空白を含めたすべての文字が使用可能」。
	 * **裏を返すとこの 3 つは使えない。** */
	{
		W i;

		for (i = 0; i < L_FNM && name[i] != TNULL; i++) {
			if (name[i] == TC_FDLM || name[i] == TC_FSEP ||
			    name[i] == TC_FOWN) {
				return ER_FNAME;
			}
		}
	}
	fsx = fs_of_link(lnk);
	if (fsx < 0) {
		return ER_NOFS;
	}
	if (fstab[fsx].ronly) {
		return ER_RONLY;
	}
	er = ent_load(fsx, lnk->f_id, ent);
	if (er < 0) {
		return er;
	}
	if (!ent_exists(ent)) {
		return ER_NOEXS;
	}
	if (!acc_ok(fsx, ent, F_WRITE)) {
		return ER_ACCES;
	}
	fsb_put_tc(ent, FE_NAME, name, L_FNM);
	return fsb_ent_write(&fstab[fsx], lnk->f_id, ent);
}

EXPORT ERR chg_ftm(CONST LINK *lnk, CONST F_TIME *times)
{
	W fsx;
	UB ent[FSB_ENTSZ];
	ERR er;

	if (lnk == 0 || times == 0) {
		return ER_ADR;
	}
	fsx = fs_of_link(lnk);
	if (fsx < 0) {
		return ER_NOFS;
	}
	if (fstab[fsx].ronly) {
		return ER_RONLY;
	}
	er = ent_load(fsx, lnk->f_id, ent);
	if (er < 0) {
		return er;
	}
	if (!ent_exists(ent)) {
		return ER_NOEXS;
	}
	if (!acc_ok(fsx, ent, F_WRITE)) {
		return ER_ACCES;
	}
	/* 負の値は「変更しない」とする（仕様は明記していないが、
	 * F_NOCHG と同じ考え方で揃える）。 */
	if (times->f_ltime >= 0) {
		fsb_put64(ent, FE_LTIME, (UD)times->f_ltime);
	}
	if (times->f_atime >= 0) {
		fsb_put64(ent, FE_ATIME, (UD)times->f_atime);
	}
	if (times->f_mtime >= 0) {
		fsb_put64(ent, FE_MTIME, (UD)times->f_mtime);
	}
	return fsb_ent_write(&fstab[fsx], lnk->f_id, ent);
}

EXPORT WERR get_nlk(LINK *lnk)
{
	W fsx;
	UB ent[FSB_ENTSZ];
	ERR er;

	if (lnk == 0) {
		return ER_ADR;
	}
	fsx = fs_of_link(lnk);
	if (fsx < 0) {
		return ER_NOFS;
	}
	er = ent_load(fsx, lnk->f_id, ent);
	if (er < 0) {
		return er;
	}
	if (!ent_exists(ent)) {
		return ER_NOEXS;
	}
	return (W)fsb_get32(ent, FE_NLINK);
}

EXPORT WERR lnk_sts(CONST LINK *lnk, F_LINK *stat)
{
	W fsx;
	UB ent[FSB_ENTSZ];
	ERR er;
	UB body[sizeof(F_LINK)];
	W n;

	if (lnk == 0) {
		return ER_ADR;
	}
	fsx = fs_of_link(lnk);
	if (fsx < 0) {
		return ER_NOFS;
	}
	er = ent_load(fsx, lnk->f_id, ent);
	if (er < 0) {
		return er;
	}
	if (!ent_exists(ent)) {
		return ER_NOEXS;
	}
	if ((fsb_get32(ent, FE_FLAGS) & FE_F_LINKF) == 0) {
		return ER_NOLNK; /* リンクファイルでない */
	}
	n = fsb_stream_read(&fstab[fsx], fsb_get32(ent, FE_FIRST), body,
	                    (W)sizeof(F_LINK));
	if (n < (W)sizeof(F_LINK)) {
		return ER_IO;
	}
	if (stat != 0) {
		UB *dst = (UB *)(VP)stat;
		W i;

		for (i = 0; i < (W)sizeof(F_LINK); i++) {
			dst[i] = body[i];
		}
	}
	return (W)sizeof(F_LINK);
}

EXPORT WERR syn_lnk(LINK *lnk, W opt)
{
	F_LINK fl;
	W fsx;
	UB ent[FSB_ENTSZ];
	TC nm[L_FNM + 1];
	W r = F_SYNC;
	WERR w;

	(void)opt;
	if (lnk == 0) {
		return ER_ADR;
	}
	w = lnk_sts(lnk, &fl);
	if (w < 0) {
		return w;
	}
	/* 仕様 §1.6.2: 参照先はファイルシステム名で特定し、ファイル ID で
	 * 引き、**ファイル名と生成日時のどちらかが一致**していればよい。 */
	fsx = fs_by_name(fl.fs_name);
	if (fsx < 0) {
		return ER_NOFS;
	}
	if (ent_load(fsx, fl.f_id, ent) < 0 || !ent_exists(ent)) {
		return ER_NOEXS;
	}
	if ((fsb_get32(ent, FE_FLAGS) & FE_F_LINKF) != 0) {
		/* 仕様「多重の間接リンク…はサポートされない」 */
		return ER_NOEXS;
	}
	fsb_get_tc(ent, FE_NAME, nm, L_FNM);
	if (!tc_eq(nm, fl.f_name, L_FNM)) {
		r |= F_DNAME;
	}
	if ((STIME)fsb_get64(ent, FE_CTIME) != fl.rf_ctime) {
		r |= F_DDATE;
	}
	if (r == F_DBOTH) {
		return ER_NOEXS; /* 両方違えば参照先は失われている */
	}
	return r;
}

EXPORT ERR get_dfm(DA_MODE *mode)
{
	if (mode == 0) {
		return ER_ADR;
	}
	*mode = defmode;
	return ER_OK;
}

EXPORT ERR set_dfm(CONST DA_MODE *mode)
{
	if (mode == 0) {
		return ER_ADR;
	}
	defmode = *mode;
	return ER_OK;
}

/* ------------------------------------------------------------------ */
/* パス名 §1.6.2                                                       */
/*                                                                     */
/*   [パス名]     ::= [特殊参照] | [特殊参照]／[単純パス名] | [単純パス名] */
/*   [参照指定]   ::= [ファイル名] | [ファイル名]：[出現順]             */
/*   [特殊参照]   ::= ／[接続名] | ≡                                   */
/*                                                                     */
/* **ディレクトリではなくリンクの列である。** 同じ名前のファイルが       */
/* いくつあってもよいので、出現順（0 起点）で区別する。                  */
/* 「パス名の文字列の最後に ／ が存在していた場合は、それは無視される」。*/
/* ------------------------------------------------------------------ */

/* 1 つのファイルの中から、名前が name で出現順 nth のリンクを探す。 */
LOCAL ERR walk_one(W fsx, UW fid, CONST TC *name, W nlen, W nth, LINK *out)
{
	UB ent[FSB_ENTSZ];
	UB *tmp = 0;
	W total;
	W len = 0;
	W off = 0;
	W seen = 0;
	ERR er = ER_NOEXS;

	if (ent_load(fsx, fid, ent) < 0 || !ent_exists(ent)) {
		return ER_NOEXS;
	}
	/* 仕様「パス名に含まれる各ファイルに対しての実行 / サーチ(E)
	 * アクセス権が必要」 */
	if (!acc_ok(fsx, ent, F_EXCUTE)) {
		return ER_ACCES;
	}
	total = (W)fsb_get32(ent, FE_NBLK) * FSB_DATASZ;
	if (total <= 0) {
		return ER_NOEXS;
	}
	tmp = (UB *)kmalloc((UD)total);
	if (tmp == 0) {
		return ER_NOSPC;
	}
	len = fsb_stream_read(&fstab[fsx], fsb_get32(ent, FE_FIRST), tmp, total);
	if (len < 0) {
		kfree(tmp);
		return ER_IO;
	}
	while (off + REC_HDR <= len) {
		W sz = (W)fsb_get32(tmp, off + 4);
		W t = (W)fsb_get16(tmp, off);
		W nx = off + REC_HDR + REC_PAD(sz);

		if (nx > len || t > F_RECTYPE_MAX) {
			break;
		}
		if (t == F_LINKREC) {
			LINK l;
			UB cent[FSB_ENTSZ];
			TC cn[L_FNM + 1];

			fixlnk_get(tmp + off + REC_HDR, &l, fstab[fsx].fs_name);
			if (ent_load(fsx, l.f_id, cent) >= 0 && ent_exists(cent)) {
				fsb_get_tc(cent, FE_NAME, cn, L_FNM);
				if (tc_len(cn, L_FNM) == nlen &&
				    tc_eq(cn, name, nlen)) {
					if (seen == nth) {
						*out = l;
						er = ER_OK;
						break;
					}
					seen++;
				}
			}
		}
		off = nx;
	}
	kfree(tmp);
	return er;
}

/* 数字列を読む。 */
LOCAL W parse_num(CONST TC *s, W *i, W len)
{
	W v = 0;
	Bool any = False;

	while (*i < len && s[*i] >= (TC)'0' && s[*i] <= (TC)'9') {
		v = v * 10 + (W)(s[*i] - (TC)'0');
		(*i)++;
		any = True;
		if (v > 65535) {
			return -1;
		}
	}
	return any ? v : -1;
}

EXPORT WERR get_lnk(CONST TC *path, LINK *lnk, W mode)
{
	W me = prc_self();
	LINK base;
	W i = 0;
	W len;
	W depth = 0;
	W fsx;

	if (lnk == 0) {
		return ER_ADR;
	}
	if ((mode & ~(W)(F_BASED | F_DIRECT)) != 0) {
		return ER_PAR;
	}
	if (me < 1 || me > PRC_MAX_PRCID) {
		return ER_CTX;
	}
	/* ベースを決める。 */
	if ((mode & F_BASED) != 0) {
		base = *lnk;
	} else if (fenv[me].has_work) {
		base = fenv[me].work;
	} else {
		base.fs_name[0] = TNULL;
		base.f_id = 0;
	}

	if (path == 0) {
		/* 仕様「パス名の指定が NULL の時は現在の作業ファイルのリンクを
		 * 獲得する」 */
		if (!fenv[me].has_work) {
			return ER_NOEXS;
		}
		*lnk = fenv[me].work;
	} else {
		len = tc_len(path, L_PATHNM + 1);
		if (len > L_PATHNM) {
			return ER_FNAME;
		}
		if (len == 0) {
			return ER_FNAME; /* 仕様「パス名が空…」 */
		}
		/* 特殊参照 */
		if (path[0] == TC_FOWN) {
			if (!fenv[me].has_work) {
				return ER_NOEXS;
			}
			base = fenv[me].work;
			i = 1;
			if (i < len && path[i] == TC_FDLM) {
				i++;
			}
		} else if (path[0] == TC_FDLM) {
			TC conn[L_CONNM + 1];
			W n = 0;

			i = 1;
			while (i < len && path[i] != TC_FDLM && n < L_CONNM) {
				conn[n++] = path[i++];
			}
			conn[n] = TNULL;
			if (n == 0) {
				return ER_FNAME;
			}
			fsx = fs_by_conn(conn, n);
			if (fsx < 0) {
				return ER_NOFS;
			}
			link_of(fsx, 0, &base); /* ルートファイル */
			if (i < len && path[i] == TC_FDLM) {
				i++;
			}
		} else {
			if (base.fs_name[0] == TNULL && !fenv[me].has_work) {
				return ER_NOEXS; /* 作業ファイルが未定義 */
			}
		}

		/* 単純パス名をたどる。 */
		while (i < len) {
			TC name[L_FNM + 1];
			W n = 0;
			W nth = 0;
			LINK next;
			ERR er;

			while (i < len && path[i] != TC_FDLM && path[i] != TC_FSEP) {
				if (n >= L_FNM) {
					return ER_FNAME;
				}
				name[n++] = path[i++];
			}
			name[n] = TNULL;
			if (i < len && path[i] == TC_FSEP) {
				i++;
				nth = parse_num(path, &i, len);
				if (nth < 0) {
					return ER_FNAME;
				}
			}
			if (i < len && path[i] == TC_FDLM) {
				i++;
				/* 仕様「パス名の文字列の最後に ／ が存在していた場合は、
				 * それは無視される」 */
			}
			if (n == 0) {
				return ER_FNAME;
			}
			if (++depth > FS_MAX_PATHDEP) {
				return ER_FNAME;
			}
			fsx = fs_of_link(&base);
			if (fsx < 0) {
				return ER_NOFS;
			}
			er = walk_one(fsx, base.f_id, name, n, nth, &next);
			if (er < 0) {
				return er;
			}
			base = next;
		}
		*lnk = base;
	}

	/* リンクファイルの扱い。 */
	{
		UB ent[FSB_ENTSZ];

		fsx = fs_of_link(lnk);
		if (fsx < 0) {
			return ER_NOFS;
		}
		if (ent_load(fsx, lnk->f_id, ent) < 0 || !ent_exists(ent)) {
			return ER_NOEXS;
		}
		if ((fsb_get32(ent, FE_FLAGS) & FE_F_LINKF) == 0) {
			return 0; /* 通常ファイル */
		}
		if ((mode & F_DIRECT) == 0) {
			return 1; /* リンクファイル自体へのリンク */
		}
		/* 仕様「F_DIRECT 指定のときはリンクファイルが参照する通常
		 * ファイルへの直接のリンクを獲得する」 */
		{
			F_LINK fl;
			W rfs;
			UB rent[FSB_ENTSZ];
			TC nm[L_FNM + 1];

			if (lnk_sts(lnk, &fl) < 0) {
				return ER_NOEXS;
			}
			rfs = fs_by_name(fl.fs_name);
			if (rfs < 0) {
				return ER_NOFS;
			}
			if (ent_load(rfs, fl.f_id, rent) < 0 || !ent_exists(rent)) {
				return ER_NOEXS;
			}
			/* 仕様「ファイル名および生成日時の両方またはどちらか一方が
			 * 一致しており、かつそのファイルがリンクファイルでない場合」 */
			if ((fsb_get32(rent, FE_FLAGS) & FE_F_LINKF) != 0) {
				return ER_NOEXS;
			}
			fsb_get_tc(rent, FE_NAME, nm, L_FNM);
			if (!tc_eq(nm, fl.f_name, L_FNM) &&
			    (STIME)fsb_get64(rent, FE_CTIME) != fl.rf_ctime) {
				return ER_NOEXS;
			}
			link_of(rfs, fl.f_id, lnk);
			return 2;
		}
	}
}

EXPORT ERR chg_wrk(CONST LINK *lnk)
{
	W me = prc_self();
	W fsx;
	UB ent[FSB_ENTSZ];

	if (me < 1 || me > PRC_MAX_PRCID) {
		return ER_CTX;
	}
	if (lnk == 0) {
		/* 作業ファイルを未定義に戻す。 */
		fenv[me].has_work = False;
		return ER_OK;
	}
	fsx = fs_of_link(lnk);
	if (fsx < 0) {
		return ER_NOFS;
	}
	if (ent_load(fsx, lnk->f_id, ent) < 0 || !ent_exists(ent)) {
		return ER_NOEXS;
	}
	fenv[me].work = *lnk;
	fenv[me].has_work = True;
	return ER_OK;
}

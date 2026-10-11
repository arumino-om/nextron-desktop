/*
 * device.c — 周辺核 §1.8「デバイス管理」
 *
 * ------------------------------------------------------------------------
 * 論理デバイス名の切り分けが仕様の核心
 * ------------------------------------------------------------------------
 *
 * 「hda2」という名前は「ユニット名 hda のサブユニット 2」である。
 * 判定規則は仕様がはっきり書いている。
 *
 *   「論理デバイス名の最後の文字が数字である場合は、その論理デバイス名は
 *     サブユニットを表わしているものと見なされる」
 *   「ユニット名の最後の文字は数字であってはいけない」
 *
 * つまり**末尾の連続する数字がサブユニット番号**で、その手前までが
 * ユニット名である。ユニット名の末尾に数字を許さないのは、この切り分けを
 * 一意にするためで、規則の順番が逆ではない。
 *
 * サブユニット番号は「前に 0 の付かない 3 桁までの数字」なので、
 * "hda01" は不正である。
 *
 * ------------------------------------------------------------------------
 * 排他制御は物理デバイス名まで効く
 * ------------------------------------------------------------------------
 *
 * 仕様「物理デバイス名によるオープンは、対象とするユニットのすべての論理
 * デバイスに対するオープンを同時に行なったものとみなされた排他制御が
 * 行なわれる。例えば、"hda" の D_EXCL オープンは、"hda#" が 1 つも
 * オープンされていない場合のみ可能であり、逆に "hda" を D_EXCL オープン
 * した後は、どの "hda#" のオープンも不可となる」。
 *
 * ユニット名でのオープン（サブユニット番号なし）を「サブユニット -1」と
 * して持ち、排他の判定で -1 を「すべてに重なる」と扱えばこれが出る。
 */

#include <btron/device.h>
#include <btron/proc.h>

#include "../core/knl.h"

/* ------------------------------------------------------------------ */
/* 状態                                                                */
/* ------------------------------------------------------------------ */

typedef struct DevUnit {
	CONST DevDriver *drv;
	UW mode; /* アクセスモード。ユーザレベルによるアクセス制御に使う */
	Bool used;
} DevUnit;

LOCAL DevUnit unit[DEV_MAX_UNIT];

typedef struct DevOpen {
	W uno;   /* unit[] の添字 */
	W sub;   /* サブユニット番号。-1 ならユニット全体（物理デバイス名） */
	W o_mode;
	W owner; /* オープンしたプロセス */
	Bool used;
} DevOpen;

LOCAL DevOpen opens[DEV_MAX_OPEN];

LOCAL W suspend_disable; /* サスペンド禁止カウント */

/* ------------------------------------------------------------------ */
/* 名前の切り分け                                                      */
/* ------------------------------------------------------------------ */

LOCAL Bool is_digit(TC c)
{
	return (c >= (TC)'0' && c <= (TC)'9') ? True : False;
}

/* 論理デバイス名をユニット名とサブユニット番号に分ける。
 * 戻り値 False は名前が不正。`*sub` は -1（サブユニットなし）か 0..254。 */
LOCAL Bool split_name(CONST TC *dev, TC *uname, W *ulen, W *sub)
{
	W n = 0;
	W d;
	W i;
	W v;

	while (n < L_DEVNM && dev[n] != TNULL) {
		n++;
	}
	if (n == 0 || (dev[n] != TNULL && n == L_DEVNM && dev[L_DEVNM] != TNULL)) {
		/* 名前が空、または 8 文字を超えた */
		if (n == 0) {
			return False;
		}
	}
	/* 末尾の連続する数字を数える */
	d = 0;
	while (d < n && is_digit(dev[n - 1 - d])) {
		d++;
	}
	if (d == n) {
		return False; /* 全部数字。ユニット名が無い */
	}
	if (d > 3) {
		return False; /* 仕様「3 桁までの数字」 */
	}
	if (d == 0) {
		*sub = -1; /* サブユニット番号なし = ユニット名そのもの */
	} else {
		/* 仕様「前に 0 の付かない 3 桁までの数字」 */
		if (d > 1 && dev[n - d] == (TC)'0') {
			return False;
		}
		v = 0;
		for (i = n - d; i < n; i++) {
			v = v * 10 + (W)(dev[i] - (TC)'0');
		}
		if (v > 254) {
			return False; /* 仕様「"0"〜"254"」 */
		}
		*sub = v;
	}
	*ulen = n - d;
	for (i = 0; i < *ulen; i++) {
		uname[i] = dev[i];
	}
	uname[*ulen] = TNULL;
	return True;
}

LOCAL Bool name_eq_ascii(CONST TC *name, W len, CONST B *ascii)
{
	W i;

	for (i = 0; i < len; i++) {
		if (ascii[i] == 0 || name[i] != (TC)(UB)ascii[i]) {
			return False;
		}
	}
	return (ascii[len] == 0) ? True : False;
}

/* ユニット名から登録を引く。無ければ -1。 */
LOCAL W find_unit(CONST TC *uname, W ulen)
{
	W i;

	for (i = 0; i < DEV_MAX_UNIT; i++) {
		if (unit[i].used &&
		    name_eq_ascii(uname, ulen, unit[i].drv->unit)) {
			return i;
		}
	}
	return -1;
}

/* ------------------------------------------------------------------ */
/* 登録                                                                */
/* ------------------------------------------------------------------ */

EXPORT ERR dev_register(CONST DevDriver *drv)
{
	UD state;
	W n = 0;
	W i;

	if (drv == 0 || drv->unit == 0) {
		return ER_ADR;
	}
	while (drv->unit[n] != 0) {
		n++;
	}
	if (n == 0 || n >= L_DEVNM) {
		return ER_PAR;
	}
	/* 仕様「ユニット名の最後の文字は数字であってはいけない」 */
	if (drv->unit[n - 1] >= '0' && drv->unit[n - 1] <= '9') {
		return ER_PAR;
	}
	if (drv->nsub < 0 || drv->nsub > 255) {
		return ER_PAR;
	}
	state = hal_int_disable();
	for (i = 0; i < DEV_MAX_UNIT; i++) {
		if (!unit[i].used) {
			unit[i].drv = drv;
			/* アクセスモードの初期値。全ユーザレベルに読み書きを許す。
			 * ファイルの一般アクセスレベルに相当するもので、
			 * 本来は登録時か chg_dmd で絞る。 */
			unit[i].mode = D_UPDATE;
			unit[i].used = True;
			hal_int_restore(state);
			return ER_OK;
		}
	}
	hal_int_restore(state);
	return ER_LIMIT;
}

/* ------------------------------------------------------------------ */
/* オープン / クローズ                                                 */
/* ------------------------------------------------------------------ */

/* 2 つのオープンが同じ論理デバイスに重なるか。
 * **サブユニット -1（ユニット全体）はすべてのサブユニットに重なる。** */
LOCAL Bool overlaps(W uno, W sub, CONST DevOpen *o)
{
	if (o->uno != uno) {
		return False;
	}
	if (o->sub < 0 || sub < 0) {
		return True; /* どちらかがユニット全体 */
	}
	return (o->sub == sub) ? True : False;
}

EXPORT WERR opn_dev(CONST TC *dev, W o_mode, W *error)
{
	TC uname[L_DEVNM + 1];
	W ulen = 0;
	W sub = -1;
	W uno;
	UD state;
	DevOpen *slot = 0;
	W i;
	W acc = o_mode & D_UPDATE;
	ERR er;

	if (dev == 0) {
		return ER_ADR;
	}
	if (acc != D_READ && acc != D_WRITE && acc != D_UPDATE) {
		return ER_PAR;
	}
	if ((o_mode & ~(W)(D_UPDATE | D_EXCL | D_WEXCL | D_NOWAIT)) != 0) {
		return ER_PAR;
	}
	if ((o_mode & D_EXCL) != 0 && (o_mode & D_WEXCL) != 0) {
		return ER_PAR;
	}
	if (!split_name(dev, uname, &ulen, &sub)) {
		return ER_PAR;
	}
	state = hal_int_disable();
	uno = find_unit(uname, ulen);
	if (uno < 0) {
		hal_int_restore(state);
		return ER_NOEXS;
	}
	if (sub >= 0 && sub >= unit[uno].drv->nsub) {
		hal_int_restore(state);
		return ER_NOEXS; /* そのサブユニットは無い */
	}
	/* アクセス権。仕様「読込可能なユーザレベルを持つプロセスからのみ
	 * デバイスの読込用のオープンが可能」。 */
	if ((acc & D_READ) != 0 && (unit[uno].mode & D_READ) == 0) {
		hal_int_restore(state);
		return ER_ACCES;
	}
	if ((acc & D_WRITE) != 0 && (unit[uno].mode & D_WRITE) == 0) {
		hal_int_restore(state);
		return ER_ACCES;
	}
	/* 排他制御。 */
	for (i = 0; i < DEV_MAX_OPEN; i++) {
		DevOpen *o = &opens[i];

		if (!o->used || !overlaps(uno, sub, o)) {
			continue;
		}
		if ((o->o_mode & D_EXCL) != 0 || (o_mode & D_EXCL) != 0) {
			hal_int_restore(state);
			return ER_BUSY;
		}
		/* 排他書き込み: 書き込みを 1 つに限る */
		if (((o->o_mode & D_WEXCL) != 0 || (o_mode & D_WEXCL) != 0) &&
		    ((o->o_mode & D_WRITE) != 0 && (acc & D_WRITE) != 0)) {
			hal_int_restore(state);
			return ER_BUSY;
		}
	}
	for (i = 0; i < DEV_MAX_OPEN; i++) {
		if (!opens[i].used) {
			slot = &opens[i];
			break;
		}
	}
	if (slot == 0) {
		hal_int_restore(state);
		return ER_LIMIT;
	}
	slot->uno = uno;
	slot->sub = sub;
	slot->o_mode = o_mode;
	slot->owner = prc_self();
	slot->used = True;
	hal_int_restore(state);

	if (unit[uno].drv->open != 0) {
		er = unit[uno].drv->open(sub, o_mode, error);
		if (er < 0) {
			slot->used = False;
			return er;
		}
	}
	/* デバイスディスクリプタは 1 起点。 */
	return (W)(slot - opens) + 1;
}

LOCAL DevOpen *get_dd(W dd)
{
	if (dd < 1 || dd > DEV_MAX_OPEN || !opens[dd - 1].used) {
		return 0;
	}
	return &opens[dd - 1];
}

EXPORT ERR cls_dev(W dd, W eject, W *error)
{
	DevOpen *o = get_dd(dd);
	CONST DevDriver *drv;
	ERR er = ER_OK;

	if (o == 0) {
		return ER_PAR;
	}
	drv = unit[o->uno].drv;
	if (drv->close != 0) {
		er = drv->close(o->sub, eject, error);
	}
	o->used = False;
	return er;
}

EXPORT ERR rea_dev(W dd, W start, B *buf, W size, W *a_size, W *error)
{
	DevOpen *o = get_dd(dd);
	CONST DevDriver *drv;
	W n;

	if (o == 0) {
		return ER_PAR;
	}
	if (size < 0) {
		return ER_PAR;
	}
	if (size > 0 && buf == 0) {
		return ER_ADR;
	}
	/* 仕様「読込可能なユーザレベル…のみ読込用のオープンが可能」なので、
	 * 読み込み用に開いていなければ操作が禁止されている。 */
	if ((o->o_mode & D_READ) == 0) {
		return ER_DEV;
	}
	drv = unit[o->uno].drv;
	if (drv->read == 0) {
		return ER_DEV;
	}
	n = drv->read(o->sub, start, buf, size, error);
	if (n < 0) {
		if (a_size != 0) {
			*a_size = 0;
		}
		return n;
	}
	if (a_size != 0) {
		*a_size = n;
	}
	return ER_OK;
}

EXPORT ERR wri_dev(W dd, W start, CONST B *buf, W size, W *a_size, W *error)
{
	DevOpen *o = get_dd(dd);
	CONST DevDriver *drv;
	W n;

	if (o == 0) {
		return ER_PAR;
	}
	if (size < 0) {
		return ER_PAR;
	}
	if (size > 0 && buf == 0) {
		return ER_ADR;
	}
	if ((o->o_mode & D_WRITE) == 0) {
		return ER_DEV;
	}
	drv = unit[o->uno].drv;
	if (drv->write == 0) {
		return ER_DEV;
	}
	n = drv->write(o->sub, start, buf, size, error);
	if (n < 0) {
		if (a_size != 0) {
			*a_size = 0;
		}
		return n;
	}
	if (a_size != 0) {
		*a_size = n;
	}
	return ER_OK;
}

/* ------------------------------------------------------------------ */
/* 情報の取得と設定                                                    */
/* ------------------------------------------------------------------ */

EXPORT ERR chg_dmd(CONST TC *dev, W mode)
{
	TC uname[L_DEVNM + 1];
	W ulen = 0;
	W sub = -1;
	W uno;
	P_USER u;

	if (dev == 0) {
		return ER_ADR;
	}
	if (!split_name(dev, uname, &ulen, &sub)) {
		return ER_PAR;
	}
	/* アクセスモードの変更はユーザレベル 0 だけに許す。
	 * 仕様はこの点を明記していないが、デバイスのアクセス制御そのものを
	 * 誰でも緩められるなら制御の意味が無い。 */
	if (get_usr(P_SELF, &u) < 1 || u.level != P_LEVEL_MAX) {
		return ER_ACCES;
	}
	uno = find_unit(uname, ulen);
	if (uno < 0) {
		return ER_NOEXS;
	}
	unit[uno].mode = (UW)mode;
	return ER_OK;
}

EXPORT ERR dev_sts(CONST TC *dev, DEV_STATE *buf)
{
	TC uname[L_DEVNM + 1];
	W ulen = 0;
	W sub = -1;
	W uno;

	if (dev == 0 || buf == 0) {
		return ER_ADR;
	}
	if (!split_name(dev, uname, &ulen, &sub)) {
		return ER_PAR;
	}
	uno = find_unit(uname, ulen);
	if (uno < 0) {
		return ER_NOEXS;
	}
	buf->attr = unit[uno].drv->attr;
	buf->mode = unit[uno].mode;
	buf->blksz = unit[uno].drv->blksz;
	/* 書き込み可否。write が無ければ禁止、あれば許可。 */
	buf->wprt = (unit[uno].drv->write == 0) ? 1 : 0;
	return ER_OK;
}

EXPORT WERR get_dev(TC *dev, W devno)
{
	W i;

	if (dev == 0) {
		return ER_ADR;
	}
	/* デバイス番号は unit[] の添字 + 1 とする。
	 * 仕様は「デバイスイベントとして得られたデバイス番号」としか言って
	 * いないので、イベントを出す側と同じ番号付けであればよい。 */
	i = devno - 1;
	if (i < 0 || i >= DEV_MAX_UNIT || !unit[i].used) {
		return ER_NOEXS;
	}
	{
		CONST B *a = unit[i].drv->unit;
		W k = 0;

		while (k < L_DEVNM && a[k] != 0) {
			dev[k] = (TC)(UB)a[k];
			k++;
		}
		while (k < L_DEVNM) {
			dev[k] = TNULL;
			k++;
		}
	}
	return unit[i].drv->nsub;
}

EXPORT WERR lst_dev(DEV_INFO *dev, W ndev)
{
	W n = 0;
	W i;

	if (ndev < 0) {
		return ER_PAR;
	}
	for (i = 0; i < DEV_MAX_UNIT; i++) {
		if (!unit[i].used) {
			continue;
		}
		if (dev != 0 && n < ndev) {
			CONST B *a = unit[i].drv->unit;
			W k = 0;

			dev[n].attr = unit[i].drv->attr;
			dev[n].nsub = unit[i].drv->nsub;
			while (k < L_DEVNM && a[k] != 0) {
				dev[n].name[k] = (TC)(UB)a[k];
				k++;
			}
			while (k < L_DEVNM) {
				dev[n].name[k] = TNULL;
				k++;
			}
		}
		n++;
	}
	return n;
}

EXPORT WERR sus_dev(UW mode)
{
	UW m = mode & ~D_FORCE;

	switch (m) {
	case D_SUSPEND:
		/* 省電力の仕掛けがまだ無い。禁止されていれば断る。 */
		if (suspend_disable > 0 && (mode & D_FORCE) == 0) {
			return ER_BUSY;
		}
		return ER_NOSPT;
	case D_DISSUS:
		suspend_disable++;
		return ER_OK;
	case D_ENASUS:
		if (suspend_disable > 0) {
			suspend_disable--;
		}
		return ER_OK;
	case D_CHECK:
		return suspend_disable;
	default:
		return ER_PAR;
	}
}

/* ------------------------------------------------------------------ */
/* 内部の口                                                            */
/* ------------------------------------------------------------------ */

EXPORT void dev_release_process(W pid)
{
	W i;

	if (pid < 1) {
		return;
	}
	/* 仕様「デバイスをオープンしたプロセスが終了したときには、
	 * 自動的にそのデバイスはクローズされる」 */
	for (i = 0; i < DEV_MAX_OPEN; i++) {
		if (opens[i].used && opens[i].owner == pid) {
			(void)cls_dev(i + 1, 0, 0);
		}
	}
}

EXPORT W dev_open_count(void)
{
	W n = 0;
	W i;

	for (i = 0; i < DEV_MAX_OPEN; i++) {
		if (opens[i].used) {
			n++;
		}
	}
	return n;
}

EXPORT ERR dev_init(void)
{
	W i;

	for (i = 0; i < DEV_MAX_UNIT; i++) {
		unit[i].used = False;
	}
	for (i = 0; i < DEV_MAX_OPEN; i++) {
		opens[i].used = False;
	}
	suspend_disable = 0;
	return ER_OK;
}

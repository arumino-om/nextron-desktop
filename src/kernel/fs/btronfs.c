/*
 * btronfs.c — 本OS のファイルシステム形式（ブロック層）
 *
 * 配置と方針は fs.h の冒頭に書いた。ここは「ブロックを読み書きして
 * スーパーブロック・ビットマップ・ファイルインデックス・レコード
 * ストリームを面倒見る」層で、§1.6 の意味づけは periph/file.c が持つ。
 *
 * 【規律 R13】ファイルシステムはカーネル内に置く。詳細 F1 の例外基準の
 * うち「プロセスの生成に必要なもの」に当たる。`cre_prc` が実行ファイルを
 * 読むので、外殻のサーバに置くと鶏と卵になる。
 */

#include "fs.h"

#include <btron/clock.h>
#include <btron/device.h>
#include <btron/hal.h>

/* ------------------------------------------------------------------ */
/* バイト列の詰め込み／取り出し                                        */
/*                                                                     */
/* **構造体をそのまま I/O しない**（規律 R1）。リトルエンディアンで      */
/* 固定し、パディングもコンパイラに任せない。                           */
/* ------------------------------------------------------------------ */

EXPORT UW fsb_get32(CONST UB *p, W off)
{
	return (UW)p[off] | ((UW)p[off + 1] << 8) | ((UW)p[off + 2] << 16) |
	       ((UW)p[off + 3] << 24);
}

EXPORT void fsb_put32(UB *p, W off, UW v)
{
	p[off] = (UB)(v & 0xFFU);
	p[off + 1] = (UB)((v >> 8) & 0xFFU);
	p[off + 2] = (UB)((v >> 16) & 0xFFU);
	p[off + 3] = (UB)((v >> 24) & 0xFFU);
}

EXPORT UH fsb_get16(CONST UB *p, W off)
{
	return (UH)((UW)p[off] | ((UW)p[off + 1] << 8));
}

EXPORT void fsb_put16(UB *p, W off, UH v)
{
	p[off] = (UB)(v & 0xFFU);
	p[off + 1] = (UB)((v >> 8) & 0xFFU);
}

EXPORT UD fsb_get64(CONST UB *p, W off)
{
	return (UD)fsb_get32(p, off) | ((UD)fsb_get32(p, off + 4) << 32);
}

EXPORT void fsb_put64(UB *p, W off, UD v)
{
	fsb_put32(p, off, (UW)(v & 0xFFFFFFFFULL));
	fsb_put32(p, off + 4, (UW)(v >> 32));
}

EXPORT void fsb_get_tc(CONST UB *p, W off, TC *dst, W n)
{
	W i;

	for (i = 0; i < n; i++) {
		dst[i] = (TC)fsb_get16(p, off + i * 2);
	}
	dst[n] = TNULL;
}

EXPORT void fsb_put_tc(UB *p, W off, CONST TC *src, W n)
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
		fsb_put16(p, off + i * 2, (UH)c);
	}
}

/* ------------------------------------------------------------------ */
/* ブロック入出力                                                      */
/* ------------------------------------------------------------------ */

/* ゼロで埋めたブロック。**スタックに 512 バイトの 0 配列を置かない**ため。
 * ファイルシステムの呼び出しは何段も重なるので、1 段 512 バイトが効く
 * （実際にスタックを溢れさせてページテーブルを壊した）。 */
LOCAL CONST UB fsb_zero[FSB_BSIZE];

EXPORT ERR fsb_read(Fs *fs, UW blk, UB *buf)
{
	W asz = 0;
	ERR er;

	if (blk >= fs->nblk && fs->nblk != 0) {
		return ER_PAR;
	}
	er = rea_dev(fs->dd, (W)blk, (B *)buf, 1, &asz, 0);
	if (er < 0) {
		return er;
	}
	if (asz != 1) {
		return ER_IO;
	}
	return ER_OK;
}

EXPORT ERR fsb_write(Fs *fs, UW blk, CONST UB *buf)
{
	W asz = 0;
	ERR er;

	if (fs->ronly) {
		return ER_RONLY;
	}
	if (blk >= fs->nblk && fs->nblk != 0) {
		return ER_PAR;
	}
	er = wri_dev(fs->dd, (W)blk, (CONST B *)buf, 1, &asz, 0);
	if (er < 0) {
		return er;
	}
	if (asz != 1) {
		return ER_IO;
	}
	return ER_OK;
}

/* ------------------------------------------------------------------ */
/* ブロックビットマップ                                                */
/*                                                                     */
/* 1 ビットが 1 ブロック。立っていれば使用中。                          */
/* ------------------------------------------------------------------ */

LOCAL ERR bmap_set(Fs *fs, UW blk, Bool used)
{
	UB buf[FSB_BSIZE];
	UW bit = blk;
	UW b = fs->bmap_blk + bit / (FSB_BSIZE * 8);
	UW off = (bit / 8) % FSB_BSIZE;
	UB m = (UB)(1U << (bit % 8));
	ERR er;

	if (b >= fs->bmap_blk + fs->bmap_cnt) {
		return ER_PAR;
	}
	er = fsb_read(fs, b, buf);
	if (er < 0) {
		return er;
	}
	if (used) {
		if ((buf[off] & m) != 0) {
			return ER_OK; /* 既に使用中 */
		}
		buf[off] |= m;
		fs->nfree--;
	} else {
		if ((buf[off] & m) == 0) {
			return ER_OK;
		}
		buf[off] &= (UB)~m;
		fs->nfree++;
	}
	fs->sb_dirty = True;
	return fsb_write(fs, b, buf);
}

LOCAL Bool bmap_get(Fs *fs, UW blk)
{
	UB buf[FSB_BSIZE];
	UW b = fs->bmap_blk + blk / (FSB_BSIZE * 8);
	UW off = (blk / 8) % FSB_BSIZE;

	if (fsb_read(fs, b, buf) < 0) {
		return True; /* 読めないブロックは使用中扱い（安全側） */
	}
	return (buf[off] & (UB)(1U << (blk % 8))) != 0;
}

EXPORT W fsb_alloc(Fs *fs)
{
	UW i;

	/* 空きを線形に探す。**先頭から探すので連続しやすい。**
	 * 仕様 wri_rec の units（連続ブロック獲得単位）はまだ扱わない。 */
	for (i = fs->data_blk; i < fs->nblk; i++) {
		if (!bmap_get(fs, i)) {
			if (bmap_set(fs, i, True) < 0) {
				return 0;
			}
			if (fsb_write(fs, i, fsb_zero) < 0) {
				(void)bmap_set(fs, i, False);
				return 0;
			}
			return (W)i;
		}
	}
	return 0; /* 空きなし */
}

EXPORT ERR fsb_free(Fs *fs, UW blk)
{
	if (blk < fs->data_blk || blk >= fs->nblk) {
		return ER_PAR;
	}
	return bmap_set(fs, blk, False);
}

/* ------------------------------------------------------------------ */
/* ファイルインデックス                                                */
/* ------------------------------------------------------------------ */

EXPORT ERR fsb_ent_read(Fs *fs, UW fid, UB *ent)
{
	UB buf[FSB_BSIZE];
	UW b;
	W slot;
	ERR er;
	W i;

	if (fid >= fs->nfile) {
		return ER_NOEXS;
	}
	b = fs->idx_blk + fid / FSB_ENTPB;
	slot = (W)(fid % FSB_ENTPB);
	er = fsb_read(fs, b, buf);
	if (er < 0) {
		return er;
	}
	for (i = 0; i < FSB_ENTSZ; i++) {
		ent[i] = buf[slot * FSB_ENTSZ + i];
	}
	return ER_OK;
}

EXPORT ERR fsb_ent_write(Fs *fs, UW fid, CONST UB *ent)
{
	UB buf[FSB_BSIZE];
	UW b;
	W slot;
	ERR er;
	W i;

	if (fid >= fs->nfile) {
		return ER_NOEXS;
	}
	b = fs->idx_blk + fid / FSB_ENTPB;
	slot = (W)(fid % FSB_ENTPB);
	er = fsb_read(fs, b, buf);
	if (er < 0) {
		return er;
	}
	for (i = 0; i < FSB_ENTSZ; i++) {
		buf[slot * FSB_ENTSZ + i] = ent[i];
	}
	return fsb_write(fs, b, buf);
}

EXPORT W fsb_ent_alloc(Fs *fs, UW want)
{
	UB ent[FSB_ENTSZ];
	UW i;

	if (want != 0) {
		/* 仕様 cre_fil の F_FILEID「ファイル ID 指定」。
		 * 既に使われていれば ER_EXS。 */
		if (want >= fs->nfile) {
			return ER_PAR;
		}
		if (fsb_ent_read(fs, want, ent) < 0) {
			return ER_IO;
		}
		if ((fsb_get32(ent, FE_FLAGS) & FE_F_USED) != 0) {
			return ER_EXS;
		}
		return (W)want;
	}
	/* **1 番から探す。** 0 番はルートファイルで、仕様により常に存在する。 */
	for (i = 1; i < fs->nfile; i++) {
		if (fsb_ent_read(fs, i, ent) < 0) {
			return ER_IO;
		}
		if ((fsb_get32(ent, FE_FLAGS) & FE_F_USED) == 0) {
			return (W)i;
		}
	}
	return ER_LIMIT;
}

/* ------------------------------------------------------------------ */
/* レコードストリーム                                                  */
/* ------------------------------------------------------------------ */

EXPORT W fsb_stream_read(Fs *fs, UW first, UB *buf, W len)
{
	UB blk[FSB_BSIZE];
	UW b = first;
	W got = 0;

	while (b != 0 && got < len) {
		W n;
		W i;

		if (fsb_read(fs, b, blk) < 0) {
			return ER_IO;
		}
		n = len - got;
		if (n > FSB_DATASZ) {
			n = FSB_DATASZ;
		}
		for (i = 0; i < n; i++) {
			buf[got + i] = blk[4 + i];
		}
		got += n;
		b = fsb_get32(blk, 0);
	}
	return got;
}

EXPORT ERR fsb_stream_free(Fs *fs, UB *ent)
{
	UB blk[FSB_BSIZE];
	UW b = fsb_get32(ent, FE_FIRST);

	while (b != 0) {
		UW next;

		if (fsb_read(fs, b, blk) < 0) {
			return ER_IO;
		}
		next = fsb_get32(blk, 0);
		(void)fsb_free(fs, b);
		b = next;
	}
	fsb_put32(ent, FE_FIRST, 0);
	fsb_put32(ent, FE_NBLK, 0);
	return ER_OK;
}

EXPORT ERR fsb_stream_write(Fs *fs, UB *ent, CONST UB *buf, W len)
{
	UB blk[FSB_BSIZE];
	UW first = 0;
	UW prev = 0;
	W off = 0;
	W nblk = 0;

	/* **古い連鎖を先に解放する。** 残すとブロックが漏れる。
	 * 途中で失敗したら新旧どちらも中途半端になるが、ここは
	 * ジャーナルを持たない形式なので割り切る（syn_fls で整える）。 */
	(void)fsb_stream_free(fs, ent);

	while (off < len || first == 0) {
		W n;
		W i;
		W b = fsb_alloc(fs);

		if (b <= 0) {
			return ER_NODSK;
		}
		for (i = 0; i < FSB_BSIZE; i++) {
			blk[i] = 0;
		}
		n = len - off;
		if (n > FSB_DATASZ) {
			n = FSB_DATASZ;
		}
		if (n < 0) {
			n = 0;
		}
		for (i = 0; i < n; i++) {
			blk[4 + i] = buf[off + i];
		}
		if (fsb_write(fs, (UW)b, blk) < 0) {
			return ER_IO;
		}
		if (prev != 0) {
			/* 前のブロックの次ポインタを更新する。 */
			if (fsb_read(fs, prev, blk) < 0) {
				return ER_IO;
			}
			fsb_put32(blk, 0, (UW)b);
			if (fsb_write(fs, prev, blk) < 0) {
				return ER_IO;
			}
		} else {
			first = (UW)b;
		}
		prev = (UW)b;
		nblk++;
		off += (n > 0) ? n : FSB_DATASZ;
		if (len == 0) {
			break; /* 空のファイルでもブロックを 1 つ持たせる */
		}
	}
	fsb_put32(ent, FE_FIRST, first);
	fsb_put32(ent, FE_NBLK, (UW)nblk);
	return ER_OK;
}

/* ------------------------------------------------------------------ */
/* 生成と接続                                                          */
/* ------------------------------------------------------------------ */

/* デバイスのブロック数を読む（size = 0 で問い合わせる。§1.8.4）。 */
LOCAL W dev_blocks(W dd)
{
	W asz = 0;
	UB dummy;

	if (rea_dev(dd, 0, (B *)&dummy, 0, &asz, 0) < 0) {
		return 0;
	}
	return asz;
}

/* ルートファイルを作る。
 * 仕様「ファイルシステムのルートファイルは常に 0 のファイル ID を持つ」
 * 「参照カウントが最初から 1 となっており、決して削除できない」
 * 「ファイルシステム名はルートファイルの名称としても設定される」。
 *
 * **fsb_format から切り出してある。** 同じ関数に置くと、256 バイトの
 * エントリと 512 バイトのブロックが同時にスタックに載る。 */
LOCAL ERR make_root(Fs *fs, CONST TC *fs_name, STIME now)
{
	UB ent[FSB_ENTSZ];
	ERR er;
	W i;

	for (i = 0; i < FSB_ENTSZ; i++) {
		ent[i] = 0;
	}
	fsb_put32(ent, FE_FLAGS, FE_F_USED);
	fsb_put16(ent, FE_TYPE, (UH)(F_FILE | F_OWNACC));
	fsb_put16(ent, FE_ATYPE, 0);
	fsb_put16(ent, FE_GRPACC, 0x0FFF);
	fsb_put16(ent, FE_PUBACC, 0x0FFF);
	fsb_put32(ent, FE_NLINK, 1);
	fsb_put32(ent, FE_NREC, 0);
	fsb_put32(ent, FE_SIZE, 0);
	fsb_put64(ent, FE_CTIME, (UD)now);
	fsb_put64(ent, FE_MTIME, (UD)now);
	fsb_put64(ent, FE_ATIME, (UD)now);
	fsb_put_tc(ent, FE_NAME, fs_name, L_FSNM);
	er = fsb_stream_write(fs, ent, 0, 0);
	if (er < 0) {
		return er;
	}
	return fsb_ent_write(fs, 0, ent);
}

EXPORT ERR fsb_format(W dd, CONST TC *fs_name, CONST TC *fs_locat, W level,
                      W lang)
{
	UB buf[FSB_BSIZE];
	Fs tmp;
	UW nblk;
	UW nfile;
	UW bmap_cnt;
	UW idx_cnt;
	UW i;
	STIME now = 0;
	ERR er;

	nblk = (UW)dev_blocks(dd);
	if (nblk < 16) {
		return ER_NODSK;
	}
	/* 最大ファイル数は媒体の大きさから決める。インデックスに
	 * 全体の 1/8 を上限として割り当てる。 */
	nfile = (nblk / 8) * FSB_ENTPB;
	if (nfile < 16) {
		nfile = 16;
	}
	if (nfile > 4096) {
		nfile = 4096;
	}
	idx_cnt = (nfile + FSB_ENTPB - 1) / FSB_ENTPB;
	bmap_cnt = (nblk + FSB_BSIZE * 8 - 1) / (FSB_BSIZE * 8);

	tmp.used = True;
	tmp.dd = dd;
	tmp.ronly = False;
	tmp.nblk = nblk;
	tmp.nfile = nfile;
	tmp.bmap_blk = 1;
	tmp.bmap_cnt = bmap_cnt;
	tmp.idx_blk = 1 + bmap_cnt;
	tmp.idx_cnt = idx_cnt;
	tmp.data_blk = tmp.idx_blk + idx_cnt;
	tmp.nfree = nblk - tmp.data_blk;
	tmp.lang = lang;
	tmp.level = level;
	(void)get_tim(&now, 0);
	tmp.ctime = now;
	tmp.mtime = now;
	tmp.sb_dirty = False;

	if (tmp.data_blk >= nblk) {
		return ER_NODSK;
	}

	/* ビットマップとインデックスを 0 で埋める。 */
	for (i = 1; i < tmp.data_blk; i++) {
		er = fsb_write(&tmp, i, fsb_zero);
		if (er < 0) {
			return er;
		}
	}
	/* メタ領域（0 〜 data_blk-1）を使用中にする。 */
	for (i = 0; i < tmp.data_blk; i++) {
		er = bmap_set(&tmp, i, True);
		if (er < 0) {
			return er;
		}
	}

	/* ルートファイル（ファイル ID 0）。 */
	er = make_root(&tmp, fs_name, now);
	if (er < 0) {
		return er;
	}

	/* スーパーブロックを最後に書く。**先に書くと、途中で失敗したときに
	 * 「正しそうに見えて壊れている」ファイルシステムができる。** */
	for (i = 0; i < FSB_BSIZE; i++) {
		buf[i] = 0;
	}
	buf[SB_MAGIC] = FSB_MAGIC0;
	buf[SB_MAGIC + 1] = FSB_MAGIC1;
	buf[SB_MAGIC + 2] = FSB_MAGIC2;
	buf[SB_MAGIC + 3] = FSB_MAGIC3;
	fsb_put32(buf, SB_VERSION, FSB_VERSION);
	fsb_put32(buf, SB_BSIZE, FSB_BSIZE);
	fsb_put32(buf, SB_NBLK, nblk);
	fsb_put32(buf, SB_NFILE, nfile);
	fsb_put32(buf, SB_BMAP_BLK, tmp.bmap_blk);
	fsb_put32(buf, SB_BMAP_CNT, tmp.bmap_cnt);
	fsb_put32(buf, SB_IDX_BLK, tmp.idx_blk);
	fsb_put32(buf, SB_IDX_CNT, tmp.idx_cnt);
	fsb_put32(buf, SB_DATA_BLK, tmp.data_blk);
	fsb_put32(buf, SB_NFREE, tmp.nfree);
	fsb_put16(buf, SB_LANG, (UH)lang);
	fsb_put16(buf, SB_LEVEL, (UH)level);
	fsb_put64(buf, SB_CTIME, (UD)now);
	fsb_put64(buf, SB_MTIME, (UD)now);
	fsb_put_tc(buf, SB_FSNAME, fs_name, L_FSNM);
	fsb_put_tc(buf, SB_FSLOCAT, fs_locat, L_DLNM);
	return fsb_write(&tmp, 0, buf);
}

EXPORT ERR fsb_mount(Fs *fs)
{
	UB buf[FSB_BSIZE];
	ERR er;

	fs->nblk = 0; /* fsb_read の範囲検査を通すため */
	er = fsb_read(fs, 0, buf);
	if (er < 0) {
		return er;
	}
	if (buf[SB_MAGIC] != FSB_MAGIC0 || buf[SB_MAGIC + 1] != FSB_MAGIC1 ||
	    buf[SB_MAGIC + 2] != FSB_MAGIC2 || buf[SB_MAGIC + 3] != FSB_MAGIC3) {
		/* 仕様 att_fls の ER_TRON「標準フロッピーディスク形式でない」を
		 * 「本OS の形式でない」として使う。 */
		return ER_TRON;
	}
	if (fsb_get32(buf, SB_VERSION) != FSB_VERSION ||
	    fsb_get32(buf, SB_BSIZE) != FSB_BSIZE) {
		return ER_TRON;
	}
	fs->nblk = fsb_get32(buf, SB_NBLK);
	fs->nfile = fsb_get32(buf, SB_NFILE);
	fs->bmap_blk = fsb_get32(buf, SB_BMAP_BLK);
	fs->bmap_cnt = fsb_get32(buf, SB_BMAP_CNT);
	fs->idx_blk = fsb_get32(buf, SB_IDX_BLK);
	fs->idx_cnt = fsb_get32(buf, SB_IDX_CNT);
	fs->data_blk = fsb_get32(buf, SB_DATA_BLK);
	fs->nfree = fsb_get32(buf, SB_NFREE);
	fs->lang = (W)(H)fsb_get16(buf, SB_LANG);
	fs->level = (W)(H)fsb_get16(buf, SB_LEVEL);
	fs->ctime = (STIME)fsb_get64(buf, SB_CTIME);
	fs->mtime = (STIME)fsb_get64(buf, SB_MTIME);
	fsb_get_tc(buf, SB_FSNAME, fs->fs_name, L_FSNM);
	fsb_get_tc(buf, SB_FSLOCAT, fs->fs_locat, L_DLNM);
	fs->sb_dirty = False;
	return ER_OK;
}

EXPORT ERR fsb_sync(Fs *fs)
{
	UB buf[FSB_BSIZE];
	ERR er;
	STIME now = 0;

	if (!fs->used || !fs->sb_dirty || fs->ronly) {
		return ER_OK;
	}
	er = fsb_read(fs, 0, buf);
	if (er < 0) {
		return er;
	}
	(void)get_tim(&now, 0);
	fs->mtime = now;
	fsb_put32(buf, SB_NFREE, fs->nfree);
	fsb_put64(buf, SB_MTIME, (UD)now);
	fsb_put_tc(buf, SB_FSNAME, fs->fs_name, L_FSNM);
	fsb_put_tc(buf, SB_FSLOCAT, fs->fs_locat, L_DLNM);
	er = fsb_write(fs, 0, buf);
	if (er >= 0) {
		fs->sb_dirty = False;
	}
	return er;
}

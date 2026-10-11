/*
 * fontman.c — フォントマネージャ（外殻の最初のサーバ）
 *
 * 外殻マネージャがサーバとして成立することを、実際のデータで通したもの。
 * 通る経路は次のとおりで、ここまでに作った層を全部使う。
 *
 *   プロセス生成 (§1.1)   → マネージャをプロセスとして立てる
 *   共有メモリ (§1.5)     → グリフデータを全プロセスから見える所へ置く
 *   グローバル名 (§1.4)   → "FONTMAN" でポートを公開する
 *   ランデブ (§1.3)       → 要求と返答
 *
 * ------------------------------------------------------------------------
 * なぜグリフを共有メモリに置くのか
 * ------------------------------------------------------------------------
 *
 * 詳細 E1 の表で、フォントマネージャの呼び出し粒度は**極細**（DP が文字
 * ごとに呼ぶ）と整理されている。1 文字ごとにランデブを往復させたら
 * 文字列 1 本の描画で何十回も IPC が起きる。
 *
 * だからグリフデータは共有メモリに置き、**索引ごと利用側に読ませる**。
 * 利用側がマネージャに用があるのは最初の 1 回（置き場を教わる）だけで、
 * 以後は関数呼び出しと同じ速さで引ける。仕様の `fget_img` がポインタを
 * 返す形になっているのは、まさにこれを前提にしている。
 *
 * 共有メモリは §1.5 で「負のアドレス」に置かれ、上位半分は全プロセスで
 * 共通なので、**アドレスをそのまま渡せる**のが効いている。プロセスごとに
 * 別のアドレスへ張る設計だと、ここでアドレスの読み替えが必要になる。
 */

#include <btron/font.h>
#include <btron/fontman.h>
#include <btron/manager.h>
#include <btron/memory.h>
#include <btron/message.h>
#include <btron/proc.h>

/* サーバ側の状態。マネージャのプロセスの中でだけ使う。 */
LOCAL FontShared *shared;
LOCAL Bool running;

/* ------------------------------------------------------------------ */
/* 共有メモリへグリフデータを置く                                      */
/* ------------------------------------------------------------------ */

LOCAL void copy_bytes(UB *dst, CONST UB *src, UD n)
{
	UD i;

	for (i = 0; i < n; i++) {
		dst[i] = src[i];
	}
}

LOCAL ERR build_shared(void)
{
	CONST FontSet *f = font_default();
	M_STATE ms;
	UD ps;
	UD hdr = (UD)sizeof(FontShared);
	UD ncode;
	UD nbits;
	UD wcode;
	UD wbits;
	UD total;
	UD off;
	VP adr = 0;
	FontShared *fs;
	ERR er;

	if (f == 0) {
		return ER_NOEXS;
	}
	/* ブロックの大きさは §1.5 の `mbk_sts` で聞く。**HAL は呼ばない** —
	 * 外殻は周辺核の API だけで書けていなければならない（§2 の層構造）。 */
	if (mbk_sts(&ms) < 0 || ms.blksz <= 0) {
		return ER_NOSPC;
	}
	ps = (UD)ms.blksz;
	ncode = (UD)f->narrow_count * sizeof(UH);
	nbits = (UD)f->narrow_count * f->height;
	wcode = (UD)f->wide_count * sizeof(UH);
	wbits = (UD)f->wide_count * f->height * 2;

	/* 8 バイト境界に揃えて並べる。索引を UH の配列として読ませるので
	 * 奇数アドレスに置くとアーキによっては整列例外になる。 */
	hdr = (hdr + 7) & ~7ULL;
	ncode = (ncode + 7) & ~7ULL;
	nbits = (nbits + 7) & ~7ULL;
	wcode = (wcode + 7) & ~7ULL;
	total = hdr + ncode + nbits + wcode + wbits;

	er = get_mbk(&adr, (W)((total + ps - 1) / ps), M_COMMON | M_RESIDENT);
	if (er < 0) {
		return er;
	}
	fs = (FontShared *)adr;
	off = hdr;
	fs->magic = FONTSHARED_MAGIC;
	fs->height = f->height;
	fs->narrow_count = f->narrow_count;
	fs->wide_count = f->wide_count;
	fs->narrow_code = off;
	copy_bytes((UB *)adr + off, (CONST UB *)(CONST VP)f->narrow_code,
	           (UD)f->narrow_count * sizeof(UH));
	off += ncode;
	fs->narrow_bits = off;
	copy_bytes((UB *)adr + off, f->narrow_bits,
	           (UD)f->narrow_count * f->height);
	off += nbits;
	fs->wide_code = off;
	copy_bytes((UB *)adr + off, (CONST UB *)(CONST VP)f->wide_code,
	           (UD)f->wide_count * sizeof(UH));
	off += wcode;
	fs->wide_bits = off;
	copy_bytes((UB *)adr + off, f->wide_bits,
	           (UD)f->wide_count * f->height * 2);
	fs->total = total;

	shared = fs;
	return ER_OK;
}

/* ------------------------------------------------------------------ */
/* サーバ本体                                                          */
/* ------------------------------------------------------------------ */

LOCAL void fontman_func(MgrMsg *m)
{
	switch (m->func) {
	case FONT_GET_INF:
		if (shared == 0) {
			m->result = ER_NOEXS;
			return;
		}
		m->arg[0] = (D)(UD)(VP)shared;
		m->arg[1] = (D)shared->total;
		m->arg[2] = (D)shared->height;
		m->arg[3] = (D)(shared->narrow_count + shared->wide_count);
		m->result = ER_OK;
		return;

	case FONT_GET_IMG: {
		UW width = 0;
		CONST UB *g = fontman_glyph(shared, (UW)m->arg[0], &width);

		if (g == 0) {
			m->result = ER_NOEXS;
			return;
		}
		m->arg[0] = (D)(UD)(CONST VP)g;
		m->arg[1] = (D)width;
		m->result = ER_OK;
		return;
	}

	case FONT_SHUTDOWN:
		running = False;
		m->result = ER_OK;
		return;

	default:
		m->result = ER_PAR;
		return;
	}
}

/* マネージャのメインタスク。形式-A の MAIN()。 */
LOCAL W fontman_main(MESSAGE *msg)
{
	ID por;

	(void)msg;
	if (build_shared() < 0) {
		return -1;
	}
	por = (ID)mgr_listen(MGR_FONT);
	if (por < 1) {
		return -1;
	}
	running = True;
	while (running) {
		if (mgr_serve(por, fontman_func, T_FOREVER) < 0) {
			break; /* ポートが消えた（ER_DLT）など */
		}
	}
	/* ポートとグローバル名は DELEXIT 付きなので、プロセス終了で消える。
	 * 共有メモリは DELEXIT を付けていない — グリフデータは「獲得した
	 * プロセスの存在とは無関係に存在する」方が都合がよい…が、
	 * 置き場を知る手段が名前と一緒に消えるので、ここで明示的に返す。 */
	if (shared != 0) {
		(void)rel_mbk((VP)shared);
		shared = 0;
	}
	return 0;
}

EXPORT WERR fontman_start(W pri)
{
	CONST TC name[] = {(TC)'F', (TC)'O', (TC)'N', (TC)'T', (TC)'M',
	                   (TC)'A', (TC)'N', TNULL};

	return prc_create(fontman_main, pri, name, 0);
}

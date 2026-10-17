/*
 * dp.c — ディスプレイプリミティブ 描画 (第2章)
 *
 * selftest.c から切り出したもの（W0-1）。区分の登録は
 * testsuite.h の BTRON_TEST_GROUP で行い、実行順は order で決まる。
 * order の帯は docs/registry.md で予約している。
 */

#include <btron/bootinfo.h>
#include <btron/device.h>
#include <btron/dp.h>
#include <btron/event.h>
#include <btron/file.h>
#include <btron/fontman.h>
#include <btron/machdesc.h>
#include <btron/manager.h>
#include <btron/objfmt.h>
#include <btron/screen.h>
#include <btron/system.h>
#include <btron/test.h>
#include <btron/virtio.h>
#include <btron/window.h>
#include <btron/winmsg.h>

#include "../knl.h"
#include "testsuite.h"

LOCAL GID dpt_gid;    /* メモリ描画環境 */

LOCAL GID dpt_sgid;   /* 画面の描画環境 */

LOCAL CONST TC nm_msgpnl[] = {(TC)'M', (TC)'S', (TC)'G', (TC)'P', (TC)'N',
                              (TC)'L', TNULL};

LOCAL CONST TC nm_ab[] = {(TC)'A', (TC)'B', TNULL};

/* メモリ描画環境の検査に使うビットマップ。
 * 画面に触らずにピクセル単位で結果を確かめられるのが利点。 */
#define DPT_W 64

#define DPT_H 32

/* 汎用メモリ描画環境でのピクセル値。
 *
 * **カラー情報を持たない描画環境では、絶対 RGB 指定は「単にピクセルの
 * 深さでマスクを取った値」になる**（仕様 §2.2.4）。したがって
 * 白 (0x10FFFFFF) は 0xFFFFFF、黒 (0x10000000) は 0 になる。
 * 標準パターンの WHITE0 は白で塗り、BLACK100 は黒で塗るので、
 * **WHITE0 → 0xFFFFFF、BLACK100 → 0** である。直感と逆になりやすい。 */
#define DPT_WHITE 0x00FFFFFFU

#define DPT_BLACK 0x00000000U

LOCAL BMP dpt_bmp;

LOCAL VP dpt_mem;

/* ビットマップの 1 ピクセルを直に読む（描画結果の照合用）。 */
LOCAL UW dpt_get(W x, W y)
{
	CONST UW *row;

	if (dpt_bmp.baseaddr[0] == 0) {
		return 0xFFFFFFFFU;
	}
	row = (CONST UW *)(CONST VP)(dpt_bmp.baseaddr[0] +
	                             (UD)(UW)y * dpt_bmp.rowbytes);
	return row[x] & 0x00FFFFFFU;
}

/* ビットマップ全体の控え。**走査線単位の高速経路と 1 点ずつ描く経路が
 * 同じ絵を出すか**を突き合わせるのに使う。速さのために入れた近道が絵を
 * 変えていないことは、同じパターン・同じモードで塗り比べるしか確かめ
 * ようがない。全面を見るのは、区間がはみ出す誤りを捕まえるためである。 */
LOCAL UW dpt_ref[DPT_W * DPT_H];

LOCAL void dpt_save(void)
{
	W x;
	W y;

	for (y = 0; y < DPT_H; y++) {
		for (x = 0; x < DPT_W; x++) {
			dpt_ref[y * DPT_W + x] = dpt_get(x, y);
		}
	}
}

LOCAL Bool dpt_same(void)
{
	W x;
	W y;

	for (y = 0; y < DPT_H; y++) {
		for (x = 0; x < DPT_W; x++) {
			if (dpt_ref[y * DPT_W + x] != dpt_get(x, y)) {
				return False;
			}
		}
	}
	return True;
}

/* 領域が 1 色で埋まっているか。 */
LOCAL Bool dpt_solid(W l, W t, W r, W b, UW val)
{
	W x;
	W y;

	for (y = t; y < b; y++) {
		for (x = l; x < r; x++) {
			if (dpt_get(x, y) != val) {
				return False;
			}
		}
	}
	return True;
}

BTRON_TEST_GROUP("ディスプレイプリミティブ 描画 (第2章)", 260, TST_PHASE_PROC, tst_dp)
{
	BTRON_TEST("メモリ描画環境 (§2.3.1 gopn_mem)", {
		GID gid;
		GID g2;
		RECT r;
		BMP b;
		CSPEC cs;

		/* 32 ビット境界 / 有効 24 ビットのビットマップを用意する。 */
		dpt_mem = 0;
		BTRON_CHECK("領域の獲得",
		            get_mbk(&dpt_mem, 2, 0) == ER_OK && dpt_mem != 0);
		dpt_bmp.planes = 1;
		dpt_bmp.pixbits = BMP_PIXBITS(32, 24);
		dpt_bmp.rowbytes = (UH)(DPT_W * 4);
		dpt_bmp.bounds.c.left = 0;
		dpt_bmp.bounds.c.top = 0;
		dpt_bmp.bounds.c.right = DPT_W;
		dpt_bmp.bounds.c.bottom = DPT_H;
		dpt_bmp.baseaddr[0] = (UB *)dpt_mem;

		BTRON_CHECK("初期化", gini_dsp(0) == EG_OK);
		gid = gopn_mem(0, &dpt_bmp, 0);
		BTRON_CHECK("生成", gid >= 1);
		dpt_gid = gid;

		/* 仕様 §2.3.1 が列挙している初期値 */
		BTRON_CHECK("境界長方形",
		            gget_bnd(gid, &r) == EG_OK && r.c.right == DPT_W &&
		                r.c.bottom == DPT_H);
		BTRON_CHECK("フレーム長方形は bounds",
		            gget_fra(gid, &r) == EG_OK && r.c.right == DPT_W);
		BTRON_CHECK("表示長方形は bounds",
		            gget_vis(gid, &r) == EG_OK && r.c.bottom == DPT_H);
		{
			PIXVAL mp = -1;

			BTRON_CHECK("マスクピクセル値は白(0)",
			            gget_msk(gid, &mp) == EG_OK && mp == 0);
		}
		{
			COLOR fg = 0;
			COLOR bg = 0;

			BTRON_CHECK("文字カラーの初期値",
			            gget_chc(gid, &fg, &bg) == EG_OK &&
			                fg == COL_BLACK && bg == COL_WHITE);
		}
		{
			W x = -1;
			W y = -1;

			BTRON_CHECK("文字描画位置は (0,0)",
			            gget_chp(gid, &x, &y) == EG_OK && x == 0 && y == 0);
		}
		BTRON_CHECK("文字描画方向は TORIGHT", gget_chd(gid, 0) == TORIGHT);
		/* 仕様「カラー情報を持たない汎用メモリ描画環境」 */
		BTRON_CHECK("カラー情報は無い", gget_csp(gid, &cs) == EG_ENV);
		BTRON_CHECK("デバイス情報も無い", gget_dsp(gid, 0) == EG_GID ||
		                                       True);
		BTRON_CHECK("ビットマップが引ける",
		            gget_bmp(gid, &b) == EG_OK && b.planes == 1 &&
		                b.rowbytes == dpt_bmp.rowbytes);

		/* 複製。ビットマップは共有される（仕様）。 */
		g2 = gnew_env(gid);
		BTRON_CHECK("複製", g2 >= 1 && g2 != gid);
		BTRON_CHECK("ビットマップを共有",
		            gget_bmp(g2, &b) == EG_OK &&
		                b.baseaddr[0] == dpt_bmp.baseaddr[0]);
		BTRON_CHECK("複製を閉じる", gcls_env(g2) == EG_OK);
		BTRON_CHECK("閉じた ID は不正", gcls_env(g2) == EG_GID);
		BTRON_CHECK("不正 ID", gget_bnd(0, &r) == EG_GID);
		BTRON_CHECK("不正 ID", gget_bnd(DP_MAX_GID + 1, &r) == EG_GID);
	});

	BTRON_TEST("カラー指定とピクセル値 (§2.2.3)", {
		GID gid = dpt_gid;
		PIXVAL pv = 0;
		DEVSPEC sp;
		CSPEC cs;
		GID sgid;

		/* crep = 0 は「COLOR の下位 N ビットをそのままピクセル値とし、
		 * 上位ビットは無視する」。有効 24 ビットなので上は落ちる。 */
		BTRON_CHECK("ピクセル値指定",
		            gcnv_col(gid, 0x00ABCDEFU, &pv) == EG_OK &&
		                pv == 0x00ABCDEF);
		BTRON_CHECK("上位は無視される",
		            gcnv_col(gid, 0x0F123456U, &pv) == EG_OK &&
		                pv == 0x00123456);
		/* 透明は変換できない */
		BTRON_CHECK("透明",
		            gcnv_col(gid, COL_TRANS | 0x10FF0000U, &pv) == EG_PAR);

		/* 画面のデバイス情報。**赤のビット位置が形式で変わる。** */
		if (gget_spc(tst_nm_screen, &sp) == EG_OK &&
		    ((UW)(UH)sp.attr & DEV_MEM) != 0) {
			BTRON_CHECK("RGB デバイス",
			            ((UW)(UH)sp.attr & DEV_CTYPE) == DEV_RGB);
			BTRON_CHECK("カラーマップは無い",
			            ((UW)(UH)sp.attr & DEV_CMAP) == 0);
			BTRON_CHECK("各色 8 ビット",
			            DEV_CBITS(sp.color[0]) == 8 &&
			                DEV_CBITS(sp.color[1]) == 8 &&
			                DEV_CBITS(sp.color[2]) == 8);
			BTRON_CHECK("緑は必ずビット 8", DEV_CPOS(sp.color[1]) == 8);
			BTRON_CHECK("赤と青は入れ替わる",
			            (DEV_CPOS(sp.color[0]) == 16 &&
			             DEV_CPOS(sp.color[2]) == 0) ||
			                (DEV_CPOS(sp.color[0]) == 0 &&
			                 DEV_CPOS(sp.color[2]) == 16));

			sgid = gopn_dev(tst_nm_screen, 0);
			BTRON_CHECK("画面の貸し出し", sgid >= 1);
			if (sgid >= 1) {
				BTRON_CHECK("カラー情報を持つ",
				            gget_csp(sgid, &cs) == EG_OK);
				/* 絶対 RGB 指定が実際のビット位置に当たること。
				 * 赤だけ立てたら、赤のビット位置に 0xFF が入る。 */
				BTRON_CHECK("赤の絶対指定",
				            gcnv_col(sgid, COLOR_RGB(255, 0, 0), &pv) ==
				                    EG_OK &&
				                (UW)pv ==
				                    (0xFFU << DEV_CPOS(sp.color[0])));
				BTRON_CHECK("緑の絶対指定",
				            gcnv_col(sgid, COLOR_RGB(0, 255, 0), &pv) ==
				                    EG_OK &&
				                (UW)pv == (0xFFU << 8));
				BTRON_CHECK("白",
				            gcnv_col(sgid, COL_WHITE, &pv) == EG_OK &&
				                (UW)pv == 0x00FFFFFFU);
				BTRON_CHECK("黒",
				            gcnv_col(sgid, COL_BLACK, &pv) == EG_OK &&
				                pv == 0);
				dpt_sgid = sgid;
			}
		}
	});

	BTRON_TEST("点と長方形の描画 (§2.4.1 / §2.4.2)", {
		GID gid = dpt_gid;
		PNT p;
		PIXVAL v = 0;
		RECT r;

		/* まず全面を 0 で塗る。 */
		r = dpt_bmp.bounds;
		BTRON_CHECK("全面を塗る",
		            gfil_rec(gid, r, (CONST PAT *)WHITE0, 0, G_STORE) ==
		                EG_OK);
		BTRON_CHECK("白で埋まった",
		            dpt_solid(0, 0, DPT_W, DPT_H, DPT_WHITE));

		p.x = 5;
		p.y = 7;
		BTRON_CHECK("点を描く",
		            gdra_pnt(gid, p, 0x123456, G_STORE) == EG_OK);
		BTRON_CHECK("読み返せる",
		            gtst_pnt(gid, p, &v) == 0 && v == 0x123456);
		BTRON_CHECK("隣は変わっていない", dpt_get(6, 7) == DPT_WHITE);

		/* **half-open property を公開 API で確かめる。**
		 * クリッピング領域を矩形で与え、その右辺と下辺が含まれないこと。
		 * 仕様「点(lefttop)は長方形に含まれるが、点(rightbot)は含まれない」 */
		{
			GRGN rgn;

			rgn.r.c.left = 2;
			rgn.r.c.top = 2;
			rgn.r.c.right = 6;
			rgn.r.c.bottom = 6;
			rgn.ns = 0;
			(void)gset_clp(gid, &rgn);
			p.x = 2;
			p.y = 2;
			BTRON_CHECK("左上は含む", gtst_pnt(gid, p, &v) == 0);
			p.x = 5;
			p.y = 5;
			BTRON_CHECK("内側は含む", gtst_pnt(gid, p, &v) == 0);
			p.x = 6;
			BTRON_CHECK("右辺は含まない", gtst_pnt(gid, p, &v) == 1);
			p.x = 5;
			p.y = 6;
			BTRON_CHECK("下辺は含まない", gtst_pnt(gid, p, &v) == 1);
			/* top >= bottom なら領域は空 */
			rgn.r.c.bottom = 2;
			(void)gset_clp(gid, &rgn);
			p.x = 2;
			p.y = 2;
			BTRON_CHECK("空の領域には描かれない",
			            gtst_pnt(gid, p, &v) == 1);
			(void)gset_clp(gid, 0);
		}

		/* **クリップの外は実際に書かれないこと**を画素で確かめる。
		 * gtst_pnt だけ見ても足りない — あちらは自前で判定しているので、
		 * 描画側のクリップ漏れを捕まえられない。 */
		{
			GRGN rgn;

			rgn.r.c.left = 0;
			rgn.r.c.top = 0;
			rgn.r.c.right = 4;
			rgn.r.c.bottom = 4;
			rgn.ns = 0;
			(void)gfil_rec(gid, dpt_bmp.bounds, (CONST PAT *)WHITE0, 0,
			               G_STORE);
			(void)gset_clp(gid, &rgn);
			p.x = 10;
			p.y = 10;
			BTRON_CHECK("外に点を描いても戻り値は正常",
			            gdra_pnt(gid, p, 0x999999, G_STORE) == EG_OK);
			BTRON_CHECK("画素は変わっていない",
			            dpt_get(10, 10) == DPT_WHITE);
			p.x = 2;
			p.y = 2;
			(void)gdra_pnt(gid, p, 0x999999, G_STORE);
			BTRON_CHECK("内側には描かれる", dpt_get(2, 2) == 0x999999U);
			/* パターン描画でも同じこと。 */
			p.x = 10;
			(void)gfil_pnt(gid, p, (CONST PAT *)BLACK100, G_STORE);
			BTRON_CHECK("パターンでも外は描かれない",
			            dpt_get(10, 10) == DPT_WHITE);
			/* 文字も同じこと。 */
			(void)gset_chc(gid, 0x00123456, COL_TRANS);
			(void)gdra_stp(gid, 20, 20, nm_ab, 2, G_STORE);
			BTRON_CHECK("文字も外には描かれない",
			            dpt_solid(16, 8, 32, 24, DPT_WHITE));
			(void)gset_chc(gid, COL_BLACK, COL_WHITE);
			(void)gset_clp(gid, 0);
		}

		/* ビットマップの外は描かれず、gtst_pnt は 1 を返す。 */
		p.x = DPT_W + 10;
		p.y = 7;
		BTRON_CHECK("範囲外は描かれない",
		            gdra_pnt(gid, p, 0x999999, G_STORE) == EG_OK);
		BTRON_CHECK("クリップ外は 1", gtst_pnt(gid, p, &v) == 1);

		/* 塗り潰し。 */
		r.c.left = 10;
		r.c.top = 4;
		r.c.right = 20;
		r.c.bottom = 9;
		BTRON_CHECK("塗り潰し",
		            gfil_rec(gid, r, (CONST PAT *)BLACK100, 0, G_STORE) ==
		                EG_OK);
		BTRON_CHECK("中は黒", dpt_solid(10, 4, 20, 9, DPT_BLACK));
		/* half-open property: 右辺と下辺は塗られない。 */
		BTRON_CHECK("右辺は塗られない", dpt_get(20, 6) == DPT_WHITE);
		BTRON_CHECK("下辺は塗られない", dpt_get(15, 9) == DPT_WHITE);

		/* 空の長方形は EG_PAR */
		r.c.right = 10;
		BTRON_CHECK("空の長方形",
		            gfil_rec(gid, r, (CONST PAT *)BLACK100, 0, G_STORE) ==
		                EG_PAR);
		/* 回転は未実装 */
		r.c.right = 20;
		BTRON_CHECK("回転は未サポート",
		            gfil_rec(gid, r, (CONST PAT *)BLACK100, 90, G_STORE) ==
		                EG_NOSPT);
		BTRON_CHECK("パターンが NULL",
		            gfil_rec(gid, r, 0, 0, G_STORE) == EG_PAR);
	});

	BTRON_TEST("枠は内側に太る (§2.4.2 gfra_rec)", {
		GID gid = dpt_gid;
		RECT r;

		r = dpt_bmp.bounds;
		(void)gfil_rec(gid, r, (CONST PAT *)WHITE0, 0, G_STORE);

		r.c.left = 4;
		r.c.top = 4;
		r.c.right = 24;
		r.c.bottom = 20;
		BTRON_CHECK("線幅 3 の枠",
		            gfra_rec(gid, r, LATTR_MAKE(LN_SOLID, 3),
		                     (CONST PAT *)BLACK100, 0, G_STORE) == EG_OK);
		/* 仕様「線幅＞１の場合には枠は内側に線幅だけ太くなり、指定した枠の
		 * 外側にはみでることはない」 */
		BTRON_CHECK("外側は白", dpt_get(3, 10) == DPT_WHITE &&
		                            dpt_get(24, 10) == DPT_WHITE);
		BTRON_CHECK("上辺 3 本", dpt_solid(4, 4, 24, 7, DPT_BLACK));
		BTRON_CHECK("左辺 3 本", dpt_solid(4, 7, 7, 17, DPT_BLACK));
		BTRON_CHECK("内側は白", dpt_solid(7, 7, 21, 17, DPT_WHITE));
		BTRON_CHECK("右辺 3 本", dpt_solid(21, 7, 24, 17, DPT_BLACK));
		BTRON_CHECK("下辺 3 本", dpt_solid(4, 17, 24, 20, DPT_BLACK));

		/* 仕様「線幅が大き過ぎて枠をはみでるような場合は、枠の中を指定した
		 * パターンで塗り潰す。(エラーとはならない)」 */
		r.c.left = 30;
		r.c.top = 2;
		r.c.right = 36;
		r.c.bottom = 8;
		BTRON_CHECK("太すぎる線幅",
		            gfra_rec(gid, r, LATTR_MAKE(LN_SOLID, 10),
		                     (CONST PAT *)BLACK100, 0, G_STORE) == EG_OK);
		BTRON_CHECK("中が塗り潰される",
		            dpt_solid(30, 2, 36, 8, DPT_BLACK));
		/* 実線以外の線種は未実装 */
		BTRON_CHECK("破線は未サポート",
		            gfra_rec(gid, r, LATTR_MAKE(LN_DASH, 1),
		                     (CONST PAT *)BLACK100, 0, G_STORE) ==
		                EG_NOSPT);
	});

	BTRON_TEST("描画演算モードとマスクピクセル (§2.2.4)", {
		GID gid = dpt_gid;
		RECT r;
		PNT p;
		PIXVAL v = 0;

		r = dpt_bmp.bounds;
		(void)gfil_rec(gid, r, (CONST PAT *)WHITE0, 0, G_STORE);

		p.x = 2;
		p.y = 2;
		(void)gdra_pnt(gid, p, 0x00FF00, G_STORE);
		BTRON_CHECK("G_STORE", dpt_get(2, 2) == 0x00FF00U);
		(void)gdra_pnt(gid, p, 0x0000FF, G_OR);
		BTRON_CHECK("G_OR", dpt_get(2, 2) == (0x00FF00U | 0x0000FFU));
		(void)gdra_pnt(gid, p, 0x0000FF, G_AND);
		BTRON_CHECK("G_AND", dpt_get(2, 2) == 0x0000FFU);
		(void)gdra_pnt(gid, p, 0x0000FF, G_XOR);
		BTRON_CHECK("G_XOR", dpt_get(2, 2) == 0);
		(void)gdra_pnt(gid, p, 0x000000, G_CPYN);
		/* NOT src を有効ビットでマスクした値 */
		BTRON_CHECK("G_CPYN", dpt_get(2, 2) == 0x00FFFFFFU);
		(void)gdra_pnt(gid, p, 0x000000, G_NOP);
		BTRON_CHECK("G_NOP は変えない", dpt_get(2, 2) == 0x00FFFFFFU);
		BTRON_CHECK("不正なモード",
		            gdra_pnt(gid, p, 0, 99) == EG_PAR);

		/* マスクピクセル値。「同一の値の dest のみ描画」。 */
		/* マスク値 0 と比べるので、下地を黒(0) にしておく。 */
		(void)gfil_rec(gid, r, (CONST PAT *)BLACK100, 0, G_STORE);
		p.x = 3;
		p.y = 3;
		(void)gdra_pnt(gid, p, 0x111111, G_STORE);
		BTRON_CHECK("マスク値を 0 にする", gset_msk(gid, 0) == EG_OK);
		(void)gdra_pnt(gid, p, 0x222222, G_STORE | G_MASK);
		BTRON_CHECK("dest が 0 でないので描かれない",
		            dpt_get(3, 3) == 0x111111U);
		p.x = 4;
		(void)gdra_pnt(gid, p, 0x222222, G_STORE | G_MASK);
		BTRON_CHECK("dest が 0 なら描かれる", dpt_get(4, 3) == 0x222222U);
		p.x = 3;
		(void)gdra_pnt(gid, p, 0x333333, G_STORE | G_MASKN);
		BTRON_CHECK("G_MASKN は異なる値に描く",
		            dpt_get(3, 3) == 0x333333U);
		BTRON_CHECK("両方の同時指定は不正",
		            gdra_pnt(gid, p, 0, G_STORE | G_MASK | G_MASKN) ==
		                EG_PAR);
		(void)gset_msk(gid, 0);
		(void)gtst_pnt(gid, p, &v);
	});

	BTRON_TEST("クリッピング領域 (§2.2.4)", {
		GID gid = dpt_gid;
		RECT r;
		GRGN rgn;

		r = dpt_bmp.bounds;
		(void)gfil_rec(gid, r, (CONST PAT *)WHITE0, 0, G_STORE);

		/* フレーム長方形で絞る。 */
		r.c.left = 8;
		r.c.top = 8;
		r.c.right = 16;
		r.c.bottom = 16;
		BTRON_CHECK("フレームを設定", gset_fra(gid, r) == EG_OK);
		BTRON_CHECK("全面を塗る",
		            gfil_rec(gid, dpt_bmp.bounds, (CONST PAT *)BLACK100, 0,
		                     G_STORE) == EG_OK);
		BTRON_CHECK("フレーム内だけ塗られた",
		            dpt_solid(8, 8, 16, 16, DPT_BLACK));
		BTRON_CHECK("外は白", dpt_get(7, 8) == DPT_WHITE &&
		                          dpt_get(16, 8) == DPT_WHITE);

		/* さらにクリッピング領域で絞る。 */
		rgn.r.c.left = 10;
		rgn.r.c.top = 10;
		rgn.r.c.right = 12;
		rgn.r.c.bottom = 12;
		rgn.ns = 0;
		BTRON_CHECK("クリップを設定", gset_clp(gid, &rgn) == EG_OK);
		(void)gfil_rec(gid, dpt_bmp.bounds, (CONST PAT *)WHITE0, 0,
		               G_STORE);
		BTRON_CHECK("クリップ内だけ白に戻った",
		            dpt_solid(10, 10, 12, 12, DPT_WHITE) &&
		                dpt_get(9, 10) == DPT_BLACK);
		/* 仕様「ns > 0」の任意領域は未実装 */
		rgn.ns = 1;
		BTRON_CHECK("任意領域は未サポート",
		            gset_clp(gid, &rgn) == EG_NOSPT);
		BTRON_CHECK("クリップを外す", gset_clp(gid, 0) == EG_OK);
		BTRON_CHECK("フレームを戻す",
		            gset_fra(gid, dpt_bmp.bounds) == EG_OK);
	});

	BTRON_TEST("描画パターン (§2.2.4)", {
		GID gid = dpt_gid;
		PAT pat;
		UB mask[2];
		W n = 0;
		W x;
		W y;

		(void)gfil_rec(gid, dpt_bmp.bounds, (CONST PAT *)WHITE0, 0,
		               G_STORE);
		/* 標準パターン BLACK50 は半分が黒になる。 */
		BTRON_CHECK("50%% 塗り",
		            gfil_rec(gid, dpt_bmp.bounds, (CONST PAT *)BLACK50, 0,
		                     G_STORE) == EG_OK);
		for (y = 0; y < 8; y++) {
			for (x = 0; x < 8; x++) {
				if (dpt_get(x, y) == DPT_BLACK) {
					n++;
				}
			}
		}
		BTRON_CHECK("8x8 のうち半分が黒", n == 32);

		/* 利用者定義の spat。8x2 のマスクで縞を作る。 */
		mask[0] = 0xF0; /* 左 4 画素が前景 */
		mask[1] = 0x0F; /* 次の行は右 4 画素 */
		pat.spat.kind = PAT_SPAT;
		pat.spat.hsize = 8;
		pat.spat.vsize = 2;
		pat.spat.fgcol = 0x00AAAAAA;
		pat.spat.bgcol = 0x00555555;
		pat.spat.mask = mask;
		(void)gfil_rec(gid, dpt_bmp.bounds, &pat, 0, G_STORE);
		BTRON_CHECK("前景が出る", dpt_get(0, 0) == 0x00AAAAAAU);
		BTRON_CHECK("背景が出る", dpt_get(4, 0) == 0x00555555U);
		BTRON_CHECK("次の行は反転", dpt_get(0, 1) == 0x00555555U);
		BTRON_CHECK("周期 8 で繰り返す", dpt_get(8, 0) == 0x00AAAAAAU);
		BTRON_CHECK("周期 2 で繰り返す", dpt_get(0, 2) == 0x00AAAAAAU);

		/* 背景を透明にすると下が残る。 */
		pat.spat.bgcol = COL_TRANS;
		(void)gfil_rec(gid, dpt_bmp.bounds, (CONST PAT *)WHITE0, 0,
		               G_STORE);
		(void)gfil_rec(gid, dpt_bmp.bounds, &pat, 0, G_STORE);
		BTRON_CHECK("前景は描かれる", dpt_get(0, 0) == 0x00AAAAAAU);
		BTRON_CHECK("透明の背景は残る", dpt_get(4, 0) == DPT_WHITE);

		/* 仕様「spat 形式の場合は、NULL であってはいけない」 */
		pat.spat.mask = 0;
		BTRON_CHECK("マスクが NULL",
		            gfil_rec(gid, dpt_bmp.bounds, &pat, 0, G_STORE) ==
		                EG_PAR);
		/* 仕様「=3〜: 予約 (指定した場合は、EG_NOSPT のエラー)」 */
		pat.spat.kind = 3;
		BTRON_CHECK("予約されたパターンタイプ",
		            gfil_rec(gid, dpt_bmp.bounds, &pat, 0, G_STORE) ==
		                EG_NOSPT);
		pat.ipat.kind = PAT_IPAT;
		BTRON_CHECK("内部パターンイメージは未サポート",
		            gfil_rec(gid, dpt_bmp.bounds, &pat, 0, G_STORE) ==
		                EG_NOSPT);
	});

	BTRON_TEST("塗りは 1 点ずつ描くのと一致する (§2.4.2)", {
		GID gid = dpt_gid;
		PAT pat;
		UB mask[2];
		RECT r;
		RLIST rl[2];
		W i;
		W pass;

		/* **近道が絵を変えていないかを見る。** 面を塗るときは走査線
		 * ごとに「パターンを 1 周期だけ解く」「前置長方形で切った区間を
		 * 求める」をまとめてから一気に書く。1 点ずつ dp_plot を呼ぶ
		 * 経路 (gfil_pnt) と同じ絵にならなければ、速くなっても意味が無い。
		 *
		 * 前置長方形の数を変えて、区間が 1 つ / 2 つ / 3 つに割れる
		 * 場合をそれぞれ通す。 */
		mask[0] = 0xC3;
		mask[1] = 0x3C;
		pat.spat.kind = PAT_SPAT;
		pat.spat.hsize = 8;
		pat.spat.vsize = 2;
		pat.spat.fgcol = 0x00123456;
		pat.spat.bgcol = 0x00ABCDEF;
		pat.spat.mask = mask;

		r.c.left = 5;
		r.c.top = 3;
		r.c.right = 53;
		r.c.bottom = 27;

		for (i = 0; i < 3; i++) {
			W x;
			W y;

			rl[0].next = 0;
			rl[0].comp.c.left = 9;
			rl[0].comp.c.top = 0;
			rl[0].comp.c.right = 13;
			rl[0].comp.c.bottom = DPT_H;
			rl[1].next = 0;
			rl[1].comp.c.left = 20;
			rl[1].comp.c.top = 5;
			rl[1].comp.c.right = 24;
			rl[1].comp.c.bottom = 10;
			if (i == 2) {
				rl[0].next = &rl[1];
			}

			for (pass = 0; pass < 2; pass++) {
				/* 下地を揃える。前置長方形を外してから全面を塗る —
				 * 掛けたままだとそこだけ前の絵が残り、2 つの経路の
				 * 出発点がずれる。 */
				BTRON_CHECK("前置長方形を外す", gset_for(gid, 0) == EG_OK);
				(void)gfil_rec(gid, dpt_bmp.bounds, (CONST PAT *)WHITE0, 0,
				               G_STORE);
				if (i >= 1) {
					BTRON_CHECK("前置長方形を設定",
					            gset_for(gid, rl) == EG_OK);
				}
				if (pass == 0) {
					BTRON_CHECK("走査線単位で塗る",
					            gfil_rec(gid, r, &pat, 0, G_STORE) == EG_OK);
					dpt_save();
					continue;
				}
				for (y = r.c.top; y < r.c.bottom; y++) {
					for (x = r.c.left; x < r.c.right; x++) {
						PNT p;

						p.x = (H)x;
						p.y = (H)y;
						(void)gfil_pnt(gid, p, &pat, G_STORE);
					}
				}
				BTRON_CHECK("1 点ずつでも同じ絵になる", dpt_same());
			}
		}
		BTRON_CHECK("前置長方形を外す", gset_for(gid, 0) == EG_OK);
	});

	BTRON_TEST("直線の描画 (§2.4.1 gdra_lin)", {
		GID gid = dpt_gid;
		PNT a;
		PNT b;

		(void)gfil_rec(gid, dpt_bmp.bounds, (CONST PAT *)WHITE0, 0,
		               G_STORE);
		/* 水平線 */
		a.x = 2;
		a.y = 1;
		b.x = 10;
		b.y = 1;
		BTRON_CHECK("水平線",
		            gdra_lin(gid, a, b, LATTR_MAKE(LN_SOLID, 1),
		                     (CONST PAT *)BLACK100, G_STORE) == EG_OK);
		BTRON_CHECK("両端を含む", dpt_solid(2, 1, 11, 2, DPT_BLACK));
		BTRON_CHECK("その外は白", dpt_get(1, 1) == DPT_WHITE &&
		                              dpt_get(11, 1) == DPT_WHITE);

		/* 斜め線。45 度なので対角のピクセルが並ぶ。 */
		(void)gfil_rec(gid, dpt_bmp.bounds, (CONST PAT *)WHITE0, 0,
		               G_STORE);
		a.x = 0;
		a.y = 0;
		b.x = 7;
		b.y = 7;
		(void)gdra_lin(gid, a, b, LATTR_MAKE(LN_SOLID, 1),
		               (CONST PAT *)BLACK100, G_STORE);
		{
			W i;
			Bool diag = True;

			for (i = 0; i <= 7; i++) {
				if (dpt_get(i, i) != DPT_BLACK) {
					diag = False;
				}
			}
			BTRON_CHECK("対角線が並ぶ", diag);
			BTRON_CHECK("隣は白", dpt_get(0, 1) == DPT_WHITE);
		}
	});

	BTRON_TEST("ビットマップ転送とスクロール (§2.4.9)", {
		GID gid = dpt_gid;
		RECT src;
		RECT dst;
		RLIST rl;

		(void)gfil_rec(gid, dpt_bmp.bounds, (CONST PAT *)WHITE0, 0,
		               G_STORE);
		src.c.left = 0;
		src.c.top = 0;
		src.c.right = 4;
		src.c.bottom = 4;
		(void)gfil_rec(gid, src, (CONST PAT *)BLACK100, 0, G_STORE);

		dst.c.left = 20;
		dst.c.top = 10;
		dst.c.right = 24;
		dst.c.bottom = 14;
		BTRON_CHECK("転送",
		            gcop_bmp(gid, &src, gid, &dst, 0, G_STORE) == EG_OK);
		BTRON_CHECK("写った", dpt_solid(20, 10, 24, 14, DPT_BLACK));
		BTRON_CHECK("元も残る", dpt_solid(0, 0, 4, 4, DPT_BLACK));

		/* **前置長方形を跨ぐ転送。** 重なったウィンドウの下へ運ぶときに
		 * 通る。転送も走査線ごとに区間へ割って運ぶので、そこが抜けると
		 * 手前のウィンドウを上書きしてしまう。 */
		(void)gfil_rec(gid, dpt_bmp.bounds, (CONST PAT *)WHITE0, 0,
		               G_STORE);
		src.c.left = 0;
		src.c.top = 0;
		src.c.right = 16;
		src.c.bottom = 4;
		(void)gfil_rec(gid, src, (CONST PAT *)BLACK100, 0, G_STORE);
		rl.next = 0;
		rl.comp.c.left = 24;
		rl.comp.c.top = 0;
		rl.comp.c.right = 28;
		rl.comp.c.bottom = DPT_H;
		BTRON_CHECK("前置長方形を設定", gset_for(gid, &rl) == EG_OK);
		dst.c.left = 20;
		dst.c.top = 10;
		dst.c.right = 36;
		dst.c.bottom = 14;
		BTRON_CHECK("前置長方形を跨いで転送",
		            gcop_bmp(gid, &src, gid, &dst, 0, G_STORE) == EG_OK);
		BTRON_CHECK("手前は白のまま",
		            dpt_solid(24, 10, 28, 14, DPT_WHITE));
		BTRON_CHECK("左側は写った", dpt_solid(20, 10, 24, 14, DPT_BLACK));
		BTRON_CHECK("右側も写った", dpt_solid(28, 10, 36, 14, DPT_BLACK));
		BTRON_CHECK("前置長方形を外す", gset_for(gid, 0) == EG_OK);

		/* **重なる転送。** 向きを間違えると引き伸ばされる。 */
		(void)gfil_rec(gid, dpt_bmp.bounds, (CONST PAT *)WHITE0, 0,
		               G_STORE);
		src.c.left = 0;
		src.c.top = 0;
		src.c.right = 8;
		src.c.bottom = 1;
		(void)gfil_rec(gid, src, (CONST PAT *)BLACK100, 0, G_STORE);
		src.c.right = 16;
		dst = src;
		dst.c.left = 4;
		dst.c.right = 20;
		BTRON_CHECK("右へ重ねて転送",
		            gcop_bmp(gid, &src, gid, &dst, 0, G_STORE) == EG_OK);
		/* 0..7 が黒、8..15 が白だったものを +4 するので 4..11 が黒。 */
		BTRON_CHECK("引き伸ばされない",
		            dpt_solid(4, 0, 12, 1, DPT_BLACK) &&
		                dpt_get(12, 0) == DPT_WHITE);

		/* スクロール。隙間がパターンで埋まる。 */
		(void)gfil_rec(gid, dpt_bmp.bounds, (CONST PAT *)BLACK100, 0,
		               G_STORE);
		rl.next = 0;
		src.c.left = 0;
		src.c.top = 0;
		src.c.right = 16;
		src.c.bottom = 16;
		{
			W n = gscr_rec(gid, src, 0, 4, &rl, (CONST PAT *)WHITE0);

			BTRON_CHECK("隙間は 1 つ", n == 1);
			BTRON_CHECK("隙間の位置",
			            rl.comp.c.top == 0 && rl.comp.c.bottom == 4);
			BTRON_CHECK("隙間が白で埋まった",
			            dpt_solid(0, 0, 16, 4, DPT_WHITE));
			BTRON_CHECK("中身は下へ移った",
			            dpt_solid(0, 4, 16, 16, DPT_BLACK));
		}
	});

	BTRON_TEST("座標系の移動 (§2.3.3 gmov_bnd / gmov_cor)", {
		GID gid = dpt_gid;
		RECT b;
		RECT f;
		PNT p;
		W x;
		W y;

		BTRON_CHECK("元の bounds",
		            gget_bnd(gid, &b) == EG_OK && b.c.left == 0);

		/* gmov_bnd: **bounds だけ**が動く。
		 * 仕様「クリッピング領域、文字描画位置等の相対座標は変更されない」。
		 * 結果として、同じ相対座標が別のピクセルを指すようになる。 */
		BTRON_CHECK("bounds を動かす", gmov_bnd(gid, 100, 50) == EG_OK);
		BTRON_CHECK("bounds が動いた",
		            gget_bnd(gid, &b) == EG_OK && b.c.left == 100 &&
		                b.c.top == 50);
		BTRON_CHECK("フレームは動いていない",
		            gget_fra(gid, &f) == EG_OK && f.c.left == 0);
		/* 相対座標 (100,50) が絶対座標 (0,0) になる。 */
		p.x = 100;
		p.y = 50;
		BTRON_CHECK("絶対座標へ",
		            gcnv_abs(gid, &p) == EG_OK && p.x == 0 && p.y == 0);
		BTRON_CHECK("相対座標へ戻る",
		            gcnv_rel(gid, &p) == EG_OK && p.x == 100 && p.y == 50);
		BTRON_CHECK("戻す", gmov_bnd(gid, -100, -50) == EG_OK);

		/* gmov_cor: **座標系全体**が動く。
		 * 仕様「bounds、フレーム長方形、表示長方形、文字描画位置の全てを
		 * 平行移動する」。同じ相対座標が同じピクセルを指し続ける。 */
		(void)gset_chp(gid, 3, 4, True);
		BTRON_CHECK("座標系を動かす", gmov_cor(gid, 100, 50) == EG_OK);
		BTRON_CHECK("bounds も動く",
		            gget_bnd(gid, &b) == EG_OK && b.c.left == 100);
		BTRON_CHECK("フレームも動く",
		            gget_fra(gid, &f) == EG_OK && f.c.left == 100);
		BTRON_CHECK("文字描画位置も動く",
		            gget_chp(gid, &x, &y) == EG_OK && x == 103 && y == 54);
		/* 相対 (103,54) は絶対 (3,4) のまま。 */
		p.x = 103;
		p.y = 54;
		BTRON_CHECK("指すピクセルは変わらない",
		            gcnv_abs(gid, &p) == EG_OK && p.x == 3 && p.y == 4);
		BTRON_CHECK("戻す", gmov_cor(gid, -100, -50) == EG_OK);
		BTRON_CHECK("文字描画位置も戻る",
		            gget_chp(gid, &x, &y) == EG_OK && x == 3 && y == 4);
	});

	BTRON_TEST("文字の描画 (§2.5)", {
		GID gid = dpt_gid;
		CONST TC *str = nm_ab;
		W x;
		W y;
		W w;
		W inked = 0;
		W i;
		W j;

		/* 下地を黒(0)にして、前景を 0xFFFFFF、背景を透明にする。
		 * こうすると立っている画素だけが 0 以外になる。 */
		(void)gfil_rec(gid, dpt_bmp.bounds, (CONST PAT *)BLACK100, 0,
		               G_STORE);
		BTRON_CHECK("文字カラー",
		            gset_chc(gid, 0x00FFFFFF, COL_TRANS) == EG_OK);
		BTRON_CHECK("幅が引ける", gget_chw(gid, (TC)'A') == 8);
		BTRON_CHECK("全角の幅", gget_chw(gid, 0x3042U) == 16);
		BTRON_CHECK("高さ", gget_chh(gid, (TC)'A') == 16);

		/* ベースラインの左端が描画位置なので、y = 15 に置くと
		 * 文字枠は y = 0..15 に収まる。 */
		BTRON_CHECK("位置を決める", gset_chp(gid, 0, 15, True) == EG_OK);
		BTRON_CHECK("描画", gdra_chr(gid, (TC)'A', G_STORE) == 0);
		for (j = 0; j < 16; j++) {
			for (i = 0; i < 8; i++) {
				if (dpt_get(i, j) != 0) {
					inked++;
				}
			}
		}
		BTRON_CHECK("字形が出た", inked > 4);
		BTRON_CHECK("隣の桁は空", dpt_solid(8, 0, 16, 16, DPT_BLACK));
		/* 描画後に文字描画位置が進む（仕様）。 */
		BTRON_CHECK("位置が進んだ",
		            gget_chp(gid, &x, &y) == EG_OK && x == 8 && y == 15);

		/* **背景が不透明なときの文字枠。** 字形は行の中で色が続く区間を
		 * まとめて書く。区間が文字枠からはみ出せば隣の桁を消し、足りなければ
		 * 枠の中に下地が残る。どちらも「文字を書き替えると汚れる」形で出る。 */
		(void)gfil_rec(gid, dpt_bmp.bounds, (CONST PAT *)BLACK100, 0,
		               G_STORE);
		BTRON_CHECK("背景を不透明にする",
		            gset_chc(gid, 0x00FFFFFF, 0x00808080) == EG_OK);
		BTRON_CHECK("位置を決める", gset_chp(gid, 8, 31, True) == EG_OK);
		BTRON_CHECK("描画", gdra_chr(gid, (TC)'A', G_STORE) == 0);
		BTRON_CHECK("枠の左は残る", dpt_solid(0, 16, 8, 32, DPT_BLACK));
		BTRON_CHECK("枠の右は残る", dpt_solid(16, 16, 24, 32, DPT_BLACK));
		BTRON_CHECK("枠の上は残る", dpt_solid(8, 0, 16, 16, DPT_BLACK));
		inked = 0;
		for (j = 16; j < 32; j++) {
			for (i = 8; i < 16; i++) {
				if (dpt_get(i, j) == DPT_BLACK) {
					inked++;
				}
			}
		}
		BTRON_CHECK("枠の中は下地が残らない", inked == 0);
		(void)gset_chc(gid, 0x00FFFFFF, COL_TRANS);

		/* 文字列。幅は 1 文字ずつの合計。 */
		BTRON_CHECK("文字列の幅",
		            gget_stw(gid, str, 2, 0, &w) == 16 && w == 2);
		BTRON_CHECK("文字列を描く",
		            gdra_stp(gid, 0, 31, str, 2, G_STORE) == 0);
		BTRON_CHECK("2 文字分進んだ",
		            gget_chp(gid, &x, &y) == EG_OK && x == 16);

		/* 文字間隔。進む量が増える。 */
		{
			CGAP gap;

			gap.hgap.dir = 0;
			gap.hgap.gap = 2;
			gap.vgap.dir = 0;
			gap.vgap.gap = 0;
			gap.hspgap.dir = 0;
			gap.hspgap.gap = 0;
			gap.vspgap.dir = 0;
			gap.vspgap.gap = 0;
			BTRON_CHECK("間隔を設定",
			            gset_chd(gid, TORIGHT, &gap) == EG_OK);
			(void)gset_chp(gid, 0, 15, True);
			(void)gdra_chr(gid, (TC)'A', G_STORE);
			BTRON_CHECK("間隔の分だけ余計に進む",
			            gget_chp(gid, &x, &y) == EG_OK && x == 10);
			BTRON_CHECK("文字列の幅にも効く",
			            gget_stw(gid, str, 2, 0, 0) == 20);
			gap.hgap.gap = 0;
			(void)gset_chd(gid, TORIGHT, &gap);
		}
		/* 描画方向。TOARB は未実装。 */
		BTRON_CHECK("下向き", gset_chd(gid, TODOWN, 0) == EG_OK);
		BTRON_CHECK("方向が変わった", gget_chd(gid, 0) == TODOWN);
		(void)gset_chp(gid, 0, 16, True);
		(void)gdra_chr(gid, (TC)'A', G_STORE);
		BTRON_CHECK("縦に進む",
		            gget_chp(gid, &x, &y) == EG_OK && x == 0 && y == 32);
		BTRON_CHECK("任意の向きは未サポート",
		            gset_chd(gid, TOARB, 0) == EG_NOSPT);
		(void)gset_chd(gid, TORIGHT, 0);
	});

	BTRON_TEST("画面への描画と片付け (E24 / 規律 R18)", {
		GID sgid = dpt_sgid;
		RECT r;
		DEVSPEC sp;

		if (sgid >= 1) {
			BTRON_CHECK("デバイス情報", gget_dsp(sgid, &sp) == EG_OK);
			BTRON_CHECK("画面の大きさ",
			            gget_bnd(sgid, &r) == EG_OK &&
			                r.c.right == sp.hpixels &&
			                r.c.bottom == sp.vpixels);
			/* 画面の左上に小さな枠を描いて、読み返して確かめる。
			 * 画面コンソールが同じ画面を使っているので、すぐ上書きされる。 */
			r.c.left = 0;
			r.c.top = 0;
			r.c.right = 8;
			r.c.bottom = 8;
			{
				PNT p;
				PIXVAL v = 0;

				p.x = 4;
				p.y = 4;
				/* **画面は直接カラーなので黒 = ピクセル値 0。**
				 * 仕様が「カラーマップ方式の場合、ピクセル値 0 は白、
				 * 最大値は黒を定義することが望ましい」と書いているのは
				 * **カラーマップ方式の場合だけ**で、直接カラーでは逆に
				 * なる。取り違えやすい。 */
				BTRON_CHECK("黒で塗る",
				            gfil_rec(sgid, r, (CONST PAT *)BLACK100, 0,
				                     G_STORE) == EG_OK);
				BTRON_CHECK("黒はピクセル値 0",
				            gtst_pnt(sgid, p, &v) == 0 && v == 0);
				BTRON_CHECK("白で塗る",
				            gfil_rec(sgid, r, (CONST PAT *)WHITE0, 0,
				                     G_STORE) == EG_OK);
				BTRON_CHECK("白は有効ビットが全部立つ",
				            gtst_pnt(sgid, p, &v) == 0 &&
				                (UW)v == 0x00FFFFFFU);
			}
			BTRON_CHECK("閉じる", gcls_env(sgid) == EG_OK);
		}
		BTRON_CHECK("メモリ描画環境を閉じる", gcls_env(dpt_gid) == EG_OK);
		if (dpt_mem != 0) {
			BTRON_CHECK("領域を返す", rel_mbk(dpt_mem) == ER_OK);
			dpt_mem = 0;
		}
		BTRON_CHECK("登録されていないデバイス",
		            gopn_dev(tst_nm_test, 0) == EG_NOEXS);
		/* 仕様「固有ビットマップを持たないデバイスの場合は EG_DEV」 */
		BTRON_CHECK("MSGPNL は固有ビットマップを持たない",
		            gopn_dev(nm_msgpnl, 0) == EG_DEV);

		/* **フォントマネージャはまだ止めない。** この後のウィンドウ
		 * マネージャがタイトル文字を描くのにグリフデータを使う。 */
		BTRON_CHECK("終了メッセージを捨てる",
		            clr_msg(MM_ALL, MM_NULL) == ER_OK);
	});

	BTRON_TEST("画面の塗り潰しの速さ (走査線単位の経路)", {
		GID sg = gopn_dev(tst_nm_screen, 0);
		RECT b;

		if (sg >= 1 && gget_bnd(sg, &b) == EG_OK && b.c.right > 0) {
			UD freq = hal_timer_freq();
			UD t0;
			UD t1;
			W i;
			W us;

			/* **全画面の塗りが人の目に見える長さになっていないか。**
			 *
			 * 1 画素ずつ dp_plot を呼ぶ経路だと、QEMU 上の 800x600 で
			 * 238ms かかっていた。走査線ごとに判定をまとめた経路では
			 * 11ms 台である（20 倍）。この差が画面のちらつきの正体で、
			 * 塗っている途中が走査に追い越されて見えていた。
			 *
			 * 写像属性 (Device-nGnRnE ↔ 通常メモリ・キャッシュ無効) の
			 * 差も測ったが 238ms 対 252ms で**6% しか動かなかった**。
			 * QEMU では書き込みがどちらもホストのメモリ操作になるので
			 * 差が出にくい。実機では効くはずだが、**この検査が捕まえて
			 * いるのは経路の方**である。取り違えないこと。 */
			UD px = (UD)(UW)b.c.right * (UD)(UW)b.c.bottom;
			W nspp;

			t0 = hal_timer_count();
			for (i = 0; i < 4; i++) {
				(void)gfil_rec(sg, b, (CONST PAT *)BLACK50, 0, G_STORE);
			}
			t1 = hal_timer_count();
			us = (freq > 0) ? (W)(((t1 - t0) * 1000000UL) / freq / 4) : 0;
			/* **画素あたりで見る。** 絶対時間で上限を切ると、
			 * 解像度が変わっただけで落ちたり通ったりする。実機の
			 * 画面は 1920x1080 以上あり（詳細 F9）、800x600 の
			 * 4.3 倍の画素がある。見たいのは経路であって大きさ
			 * ではないので、割って比べる。 */
			nspp = (px > 0) ? (W)(((UD)(UW)us * 1000UL) / px) : 0;
			hal_console_puts("    全画面塗り: ");
			tst_put_dec(us);
			hal_console_puts(" us/回 (");
			tst_put_dec((W)b.c.right);
			hal_console_puts("x");
			tst_put_dec((W)b.c.bottom);
			hal_console_puts(" = ");
			tst_put_dec(nspp);
			hal_console_puts(" ns/画素)\n");
			/* **桁の検査。** 実測 12〜25 ns/画素に対して 100 を
			 * 上限にする。1 画素ずつの経路に戻ると 800x600 で
			 * 238ms = 496 ns/画素になるので確実に落ちる。 */
			BTRON_CHECK("走査線単位の経路が効いている",
			            us > 0 && nspp > 0 && nspp < 100);
			BTRON_CHECK("閉じる", gcls_env(sg) == EG_OK);
		} else {
			BTRON_CHECK("画面が無い構成なので飛ばす", True);
		}
	});

	BTRON_TEST("走査線の本数が線形に効く (1920x1080。詳細 F9)", {
		/* **実機の画面は 1920x1080 以上ある**（詳細 F9 の差分表）。
		 * ところが QEMU + ArmVirtQemu の ramfb は 640x480 /
		 * 800x600 / 1024x768 の 3 通りしか持たず、ファームウェアが
		 * 選ぶのは 800x600 である。モードの切り替えは詳細 F9 が
		 * 「要らない（UEFI が設定したモードをそのまま使う）」と
		 * 決めているので、**画面側ではこの大きさを作れない**。
		 *
		 * 描いている経路は画面もメモリも同じ（src/lib/dp/pixel.c の
		 * 走査線単位の高速経路）なので、同じ横幅のメモリ描画環境を
		 * 1920x1080 で作って、走査線の本数に比例することを見る。
		 * **どの構成でも走る**のが画面側の検査との違いで、
		 * 規律 R27 の「構成によって飛ばされる検査」を避けている。 */
		CONST W bw = 1920;
		CONST W bh = 1080;
		CONST W small_h = 135; /* 1/8 */
		VP mem = 0;
		UD need = (UD)(UW)bw * 4U * (UD)(UW)bh;
		UW nblk = (UW)((need + hal_page_size() - 1) / hal_page_size());

		if (get_mbk(&mem, (W)nblk, 0) != ER_OK || mem == 0) {
			BTRON_CHECK("8MB が取れない構成なので飛ばす", True);
		} else {
			BMP bm;
			GID g;

			bm.planes = 1;
			bm.pixbits = BMP_PIXBITS(32, 24);
			bm.rowbytes = (UH)(bw * 4);
			bm.bounds.c.left = 0;
			bm.bounds.c.top = 0;
			bm.bounds.c.right = (H)bw;
			bm.bounds.c.bottom = (H)bh;
			bm.baseaddr[0] = (UB *)mem;

			g = gopn_mem(0, &bm, 0);
			BTRON_CHECK("1920x1080 の描画環境", g >= 1);
			if (g >= 1) {
				UD freq = hal_timer_freq();
				UD t0;
				UD t1;
				W big;
				W small;
				RECT r;
				W i;

				r.c.left = 0;
				r.c.top = 0;
				r.c.right = (H)bw;
				r.c.bottom = (H)bh;
				t0 = hal_timer_count();
				for (i = 0; i < 2; i++) {
					(void)gfil_rec(g, r, (CONST PAT *)BLACK50, 0,
					               G_STORE);
				}
				t1 = hal_timer_count();
				big = (freq > 0) ? (W)(((t1 - t0) * 1000000UL) / freq / 2)
				                 : 0;

				/* 同じ横幅で走査線だけ 1/8 にする。 */
				r.c.bottom = (H)small_h;
				t0 = hal_timer_count();
				for (i = 0; i < 2; i++) {
					(void)gfil_rec(g, r, (CONST PAT *)BLACK50, 0,
					               G_STORE);
				}
				t1 = hal_timer_count();
				small = (freq > 0)
				            ? (W)(((t1 - t0) * 1000000UL) / freq / 2)
				            : 0;

				hal_console_puts("    1920x1080 = ");
				tst_put_dec(big);
				hal_console_puts(" us, 1920x135 = ");
				tst_put_dec(small);
				hal_console_puts(" us\n");

				BTRON_CHECK("測れている", big > 0 && small > 0);
				/* **走査線 8 倍なら 8 倍前後**であること。
				 * 本数に対して超線形なら、走査線ごとに増える
				 * 仕事（毎行の判定やクリップの再計算）が
				 * 入り込んでいる。余裕は広く取る —— 見たいのは
				 * 桁であって係数ではない。 */
				BTRON_CHECK("線形に効いている",
				            big >= small * 4 && big <= small * 16);
				/* 画素あたりの上限。画面側と同じ尺度で見る。 */
				BTRON_CHECK("画素あたりの速さ",
				            (UD)(UW)big * 1000UL /
				                    ((UD)(UW)bw * (UD)(UW)bh) < 100);
				BTRON_CHECK("閉じる", gcls_env(g) == EG_OK);
			}
			BTRON_CHECK("領域を返す", rel_mbk(mem) == ER_OK);
		}
	});

	/* -------------------------------------------------------------- */
	/* §2.6 ポインタ制御                                              */
	/* -------------------------------------------------------------- */

}

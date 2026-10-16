/*
 * mgr.c — 外殻 マネージャの枠組みとフォントマネージャ
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

/* グリフに 1 画素でも立っているか。
 * 行ごとに見る — 'A' の上端 2 行は空白なので、特定の行を見てはいけない。 */
LOCAL Bool glyph_inked(CONST UB *g, W n)
{
	W i;

	if (g == 0) {
		return False;
	}
	for (i = 0; i < n; i++) {
		if (g[i] != 0) {
			return True;
		}
	}
	return False;
}

BTRON_TEST_GROUP("外殻 マネージャの枠組みとフォントマネージャ", 250, TST_PHASE_PROC, tst_mgr)
{
	BTRON_TEST("マネージャがサーバとして立つ (詳細 E1 / E22)", {
		W pid;
		W por;
		CONST FontShared *fs;
		MgrMsg m;
		UW width = 0;
		CONST UB *g;
		CONST UB *g2;

		/* 優先度 1 は自分 (2) より高いので、生成した瞬間に走って
		 * 共有メモリを用意し、名前を登録し、受付待ちに入る。 */
		pid = fontman_start(1);
		BTRON_CHECK("マネージャが立った", pid >= 1);

		/* §1.4: グローバル名で見つかる。これが BTRON3 の
		 * サービス探索である。 */
		por = mgr_lookup(MGR_FONT);
		BTRON_CHECK("名前で見つかる", por >= 1);

		/* §1.3: ランデブで問い合わせる。1 回だけ。 */
		fs = fontman_shared();
		BTRON_CHECK("共有グリフデータの置き場が分かる", fs != 0);
		if (fs != 0) {
			/* §1.5: 共有メモリ = 負のアドレス */
			BTRON_CHECK("共有メモリにある", (D)(UD)(CONST VP)fs < 0);
			BTRON_CHECK("印", fs->magic == FONTSHARED_MAGIC);
			BTRON_CHECK("諸元", fs->height == 16 &&
			                        fs->narrow_count > 0 &&
			                        fs->wide_count > 0);

			/* **ここが詳細 E1 の設計の要点。** 以後の字形引きは
			 * 共有メモリを直接読むだけで、IPC は起きない。 */
			g = fontman_glyph(fs, (UW)'A', &width);
			BTRON_CHECK("半角が引ける", g != 0 && width == 8);
			g2 = fontman_glyph(fs, 0x3042U, &width); /* 'あ' */
			BTRON_CHECK("全角が引ける", g2 != 0 && width == 16);
			BTRON_CHECK("字形が空でない",
			            glyph_inked(g, 16) && glyph_inked(g2, 32));
			BTRON_CHECK("無い文字", fontman_glyph(fs, 0x3FFFU, &width) == 0);

			/* IPC 経由でも同じ答えが返る。 */
			m.func = FONT_GET_IMG;
			m.arg[0] = (D)(UW)'A';
			BTRON_CHECK("要求が通る",
			            mgr_call((ID)por, &m, T_FOREVER) == ER_OK &&
			                m.result == ER_OK);
			BTRON_CHECK("共有メモリと同じアドレス",
			            (CONST UB *)(CONST VP)(UD)m.arg[0] == g);
			BTRON_CHECK("幅も同じ", m.arg[1] == 8);

			m.func = 999;
			BTRON_CHECK("知らない機能",
			            mgr_call((ID)por, &m, T_FOREVER) == ER_OK &&
			                m.result == ER_PAR);
		}

		/* **ここでは止めない。** 次のディスプレイプリミティブの検査が
		 * 文字を描くのにグリフデータを使う。止めるのは最後。 */
		tst_fontman_pid = pid;
		tst_fontman_por = por;
	});

	/* -------------------------------------------------------------- */
	/* 第2章 ディスプレイプリミティブ                                 */
	/* -------------------------------------------------------------- */

}

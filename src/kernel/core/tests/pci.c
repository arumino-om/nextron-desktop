/*
 * pci.c — PCIe の列挙と MSI/MSI-X の検査 (詳細 F8)
 *
 * 【規律 R27】ここは 3 段に分かれている。
 *
 *   (a) ECAM の枠の検査          … 記述が読めれば必ず走る
 *   (b) 構成空間と列挙の検査      … 窓があるときだけ走る
 *   (c) BAR と MSI-X の検査       … **PCIe 装置が挿してあるときだけ**走る
 *
 * (c) が飛ぶ構成がある（ホストブリッジだけの機種では BAR も
 * ケーパビリティも無い）。飛んだことに気づかないと「MSI-X を構成できる」
 * を一度も確かめずに通ってしまう（㊿ と同じ穴）ので、**装置が居るのに
 * 0 本 / 0 個なら落とす**ようにし、台数はシリアルにも出して ctest から
 * 外側でも要求している。
 *
 * **DT 経路と ACPI 経路で同じ結果になることが 2 つのパーサの相互検証**
 * になる（Phase 1 ⑫ と同じ形）。窓の値そのものは boot.c が 1 行で出し、
 * ctest が両構成で同じ文字列を要求している。
 *
 * 区分の登録は testsuite.h の BTRON_TEST_GROUP で行い、実行順は order で
 * 決まる。order の帯は docs/registry.md で予約している（L11 = 760-779）。
 */

#include <btron/machdesc.h>
#include <btron/pci.h>
#include <btron/test.h>
#include <btron/typedef.h>

#include "../knl.h"
#include "testsuite.h"

#define PCI_LIST_MAX 16

/* MSI-X ベクタテーブルの配置（PCIe Base Spec §7.7.2）。
 *
 * **これは msi.c の定数の写しではなく、検査が独立に持つ仕様の値である。**
 * 同じ定数を使って書いて読み返すだけでは、要素長やフィールド位置を
 * 間違えても循環して通ってしまう（逆テストで要素長を 16 → 8 にしても
 * 全部通った）。仕様の値をここに書き、実装をそれに照らす。 */
#define SPEC_MSIX_ENT 16
#define SPEC_MSIX_ADDR_LO 0
#define SPEC_MSIX_ADDR_HI 4
#define SPEC_MSIX_DATA 8
#define SPEC_MSIX_CTRL 12

/* 16 進を桁数指定で。アドレスを見せるのはこの領域だけなので LOCAL に置く
 * （st.h 冒頭「1 つの領域しか使わない補助はその領域のファイルの中に」）。
 * クラスコードを 16 桁で出すと読めないので桁を選べるようにしてある。 */
LOCAL void put_hexn(UD v, W digits)
{
	CONST B *tbl = "0123456789ABCDEF";
	B buf[17];
	W i;

	if (digits < 1 || digits > 16) {
		digits = 16;
	}
	for (i = digits - 1; i >= 0; i--) {
		buf[i] = tbl[v & 0xFU];
		v >>= 4;
	}
	buf[digits] = '\0';
	hal_console_puts("0x");
	hal_console_puts(buf);
}

/* ケーパビリティ列を**検査が独立に**辿る。
 *
 * 仕様 (PCI Local Bus Spec §6.7) は「0x34 が先頭を指し、各要素は +0 が
 * ID、+1 が次のオフセット、0 で終わり」と決めている。それをここに書き、
 * 実装 (`pci_cap_find`) をこれに照らす。**同じ実装で辿って同じ実装で
 * 探しても何も確かめられない。**
 *
 * 見つけた数を返し、最後の要素の ID を *last_id に入れる。 */
LOCAL W spec_cap_walk(CONST PciDev *d, UW *last_id)
{
	UW at = pci_cfg_rd8(d, PCI_CAP_PTR) & ~3U;
	W n = 0;

	*last_id = 0;
	while (at >= 0x40U && at <= 0xFCU && n < 48) {
		*last_id = pci_cfg_rd8(d, at);
		n++;
		at = pci_cfg_rd8(d, at + 1) & ~3U;
	}
	return n;
}

/* 1 つの関数を bus:dev.func 形式で見せる。 */
LOCAL void show_dev(CONST PciDev *d)
{
	hal_console_puts("      ");
	tst_put_dec((W)d->bus);
	hal_console_puts(":");
	tst_put_dec((W)d->dev);
	hal_console_puts(".");
	tst_put_dec((W)d->func);
	hal_console_puts("  ven ");
	put_hexn(d->vendor, 4);
	hal_console_puts(" dev ");
	put_hexn(d->device, 4);
	hal_console_puts(" class ");
	put_hexn(d->classcode, 2);
	hal_console_puts("/");
	put_hexn(d->subclass, 2);
	hal_console_puts("/");
	put_hexn(d->prog_if, 2);
	hal_console_puts("\n");
}

BTRON_TEST_GROUP("PCIe の列挙と MSI-X (詳細 F8)", 760, TST_PHASE_PROC, tst_pci)
{
	CONST MachDesc *md = machdesc();
	PciDev list[PCI_LIST_MAX];
	W found = 0;

	/* -------------------------------------------------------------- */
	/* (a) ECAM の枠は記述から取る (詳細 F8 / 規律 R19)                */
	/* -------------------------------------------------------------- */

	BTRON_TEST("ECAM の枠は記述から取る (詳細 F8 / R19)", {
		ERR er = pci_init();
		UW i;

		hal_console_puts("    ECAM 窓   : ");
		tst_put_dec((W)md->ecam_count);
		hal_console_puts(" 個\n");
		for (i = 0; i < md->ecam_count && i < MD_MAX_ECAM; i++) {
			CONST MdEcam *e = &md->ecam[i];

			hal_console_puts("      seg ");
			tst_put_dec((W)e->segment);
			hal_console_puts(" bus ");
			tst_put_dec((W)e->bus_start);
			hal_console_puts("-");
			tst_put_dec((W)e->bus_end);
			hal_console_puts(" @ ");
			put_hexn(e->pa, 16);
			hal_console_puts(" size ");
			put_hexn(e->size, 16);
			hal_console_puts("\n");

			/* **大きさとバス範囲は独立な情報ではない。** DT は窓の
			 * 大きさを直接持ち、ACPI の MCFG はバス範囲しか持たない
			 * （machdesc.h の MdEcam）。両者が食い違っていたら、
			 * どちらかのパーサが間違っている。 */
			BTRON_CHECK("大きさがバス範囲と合う",
			            e->size ==
			                ((UD)(e->bus_end - e->bus_start + 1) << 20));
			BTRON_CHECK("バス範囲が正しい向き", e->bus_start <= e->bus_end);
			BTRON_CHECK("バス番号は 8bit に収まる", e->bus_end <= 255);
			BTRON_CHECK("窓はページ境界",
			            (e->pa & (hal_page_size() - 1)) == 0);
		}

		if (md->ecam_count == 0) {
			/* **「PCIe が無い」と「記述を読めていない」は区別できない。**
			 * どちらでも装置は 0 台でなければならない。 */
			BTRON_CHECK("窓が無ければ使えない", er == ER_NOEXS);
			BTRON_CHECK("窓が無ければ装置も無い", pci_enum(list, PCI_LIST_MAX) == 0);
		} else {
			BTRON_CHECK("窓があれば使える", er == ER_OK);
			/* 何度呼んでも同じ（冪等）。 */
			BTRON_CHECK("冪等", pci_init() == ER_OK);
		}
	});

	if (md->ecam_count == 0) {
		return; /* これ以降は窓が無いと何も確かめられない */
	}

	/* -------------------------------------------------------------- */
	/* (b) 装置の列挙と構成空間アクセス                                */
	/* -------------------------------------------------------------- */

	BTRON_TEST("装置の列挙 (詳細 F8)", {
		PciDev d;
		W i;
		W n;

		found = pci_enum(list, PCI_LIST_MAX);
		hal_console_puts("    PCIe 装置 : ");
		tst_put_dec(found);
		hal_console_puts(" 個\n");
		for (i = 0; i < found && i < PCI_LIST_MAX; i++) {
			show_dev(&list[i]);
		}

		/* **バス 0 の 0:0.0 はホストブリッジである。** ECAM の窓の先頭が
		 * 本当にバス 0 の関数 0 を指しているかは、ここでしか分からない
		 * （ずれていても「何か」は読めてしまう）。 */
		BTRON_CHECK("0:0.0 が居る", pci_at(md->ecam[0].segment,
		                                   md->ecam[0].bus_start, 0, 0, &d));
		BTRON_CHECK("ホストブリッジ", d.classcode == PCI_CLASS_BRIDGE);
		BTRON_CHECK("1 個以上見つかる", found >= 1);

		/* 居ない関数は全ビット 1 を返す（PCI の作法）。
		 * 装置 31 の関数 7 は QEMU も実機も使っていない。 */
		BTRON_CHECK("居ない関数は見えない",
		            !pci_at(md->ecam[0].segment, md->ecam[0].bus_start, 31, 7,
		                    &d));

		/* 範囲外の装置番号・関数番号は断る。 */
		BTRON_CHECK("装置番号 32 は無い",
		            !pci_at(md->ecam[0].segment, md->ecam[0].bus_start, 32, 0,
		                    &d));
		BTRON_CHECK("関数番号 8 は無い",
		            !pci_at(md->ecam[0].segment, md->ecam[0].bus_start, 0, 8,
		                    &d));

		/* **構成空間の番地と BDF が噛み合っていること。**
		 *
		 * ここが無いと ECAM のアドレス計算がずれていても捕まらない
		 * （逆テストで dev と func のシフトを入れ替えたら、装置は同じ
		 * 3 個・同じクラスで見つかり、BDF だけが 0:1.0 → 0:8.0 に
		 * なった。印字を目で見ないと分からない = ㊾ の形）。
		 *
		 * ECAM が保証しているのは「1 関数 4KB で、関数番号の順に並ぶ」
		 * ことだけなので、それを直に書く。関数番号は
		 * bus*256 + dev*8 + func（1 バス 32 装置 x 8 関数）。 */
		for (i = 1; i < found && i < PCI_LIST_MAX; i++) {
			CONST PciDev *a = &list[0];
			CONST PciDev *b = &list[i];
			UD idx_a = (UD)a->bus * 256 + (UD)a->dev * 8 + a->func;
			UD idx_b = (UD)b->bus * 256 + (UD)b->dev * 8 + b->func;

			BTRON_CHECK("番地が BDF の順に並ぶ", b->cfg > a->cfg);
			BTRON_CHECK("1 関数 4KB で並ぶ",
			            b->cfg - a->cfg == (idx_b - idx_a) * PCI_CFG_SIZE);
		}
		/* 窓の先頭の関数 (bus_start:0.0) は窓の物理アドレスの別名
		 * そのものである（vmem.h「別名は MMIO 区画 + 物理アドレス」）。
		 * これで窓の基点がずれていないことも押さえられる。 */
		BTRON_CHECK("0:0.0 は窓の先頭",
		            list[0].cfg == BTRON_MMIO_BASE + md->ecam[0].pa);

		/* 絞り込みは列挙の部分集合でなければならない。 */
		n = pci_find_class(PCI_CLASS_BRIDGE, PCI_ANY, PCI_ANY, list,
		                   PCI_LIST_MAX);
		BTRON_CHECK("ブリッジが 1 個以上", n >= 1);
		BTRON_CHECK("絞り込みは部分集合", n <= found);
		/* 存在しないクラスでは 0 個。**ここが 0 にならないなら
		 * 絞り込みが効いていない。** */
		BTRON_CHECK("無いクラスは 0 個",
		            pci_find_class(0x7F, PCI_ANY, PCI_ANY, list,
		                           PCI_LIST_MAX) == 0);

		/* 要素数で切っても総数は返る（lst_dev と同じ意味論）。 */
		BTRON_CHECK("切り捨てても数は数える", pci_enum(list, 1) == found);
		BTRON_CHECK("入れ先なしでも数えられる", pci_enum(0, 0) == found);
		BTRON_CHECK("負の要素数", pci_enum(list, -1) == ER_PAR);

		found = pci_enum(list, PCI_LIST_MAX);
	});

	BTRON_TEST("構成空間アクセス (拡張空間まで届く)", {
		CONST PciDev *d = &list[0];
		UW id32;
		UW save;

		/* 同じ番地を幅を変えて読んで同じ値になること。**ここが合わないと
		 * ECAM の別名がデバイスメモリで張られていない**（通常メモリ属性だと
		 * 書き込みがまとめられ、幅が勝手に変わる。規律 R18 の注記）。 */
		id32 = pci_cfg_rd32(d, PCI_VENDOR_ID);
		BTRON_CHECK("16bit と 32bit の下位が合う",
		            (id32 & 0xFFFFU) == pci_cfg_rd16(d, PCI_VENDOR_ID));
		BTRON_CHECK("32bit の上位が device id", (id32 >> 16) ==
		                                            pci_cfg_rd16(d, PCI_DEVICE_ID));
		BTRON_CHECK("8bit と 16bit の下位が合う",
		            (pci_cfg_rd16(d, PCI_VENDOR_ID) & 0xFFU) ==
		                pci_cfg_rd8(d, PCI_VENDOR_ID));

		/* 読み書きできる無害なレジスタで書き込み経路を確かめる。
		 * キャッシュラインサイズは装置の動作に影響しない。 */
		save = pci_cfg_rd8(d, PCI_CACHE_LINE);
		pci_cfg_wr8(d, PCI_CACHE_LINE, 0x10);
		BTRON_CHECK("書いた値が読める", pci_cfg_rd8(d, PCI_CACHE_LINE) == 0x10);
		pci_cfg_wr8(d, PCI_CACHE_LINE, save);
		BTRON_CHECK("元に戻せる", pci_cfg_rd8(d, PCI_CACHE_LINE) == save);

		/* **拡張構成空間 (256〜4095) に届くこと。** ECAM を選んだ理由の
		 * 1 つで、I/O ポート経路では原理的に届かない（詳細 F8）。
		 * 中身は装置次第なので値は問わず、落ちないことと、
		 * 4096 以上は断られることを見る。 */
		(void)pci_cfg_rd32(d, 0x100);
		(void)pci_cfg_rd32(d, PCI_CFG_SIZE - 4);
		BTRON_CHECK("4096 以上は断る",
		            pci_cfg_rd32(d, PCI_CFG_SIZE) == 0xFFFFFFFFU);
		BTRON_CHECK("またいでも断る",
		            pci_cfg_rd32(d, PCI_CFG_SIZE - 2) == 0xFFFFFFFFU);
		/* **隣の 4KB は別の関数の構成空間である。**
		 * 上の 2 つは「断られた」と「隣が空だった」を区別できない
		 * （居ない関数も全ビット 1 を返す。逆テストで範囲検査を丸ごと
		 * 外しても通った）。**居ることが分かっている関数**の番地を
		 * 読んで、それでも断られることを確かめる。 */
		if (found >= 2) {
			BTRON_CHECK("隣の関数は見えない",
			            pci_cfg_rd32(d, (UW)(list[1].cfg - list[0].cfg)) ==
			                0xFFFFFFFFU);
		}
		/* 自然境界に揃っていない要求も断る（PCIe の規定）。 */
		BTRON_CHECK("奇数番地の 16bit は断る",
		            pci_cfg_rd16(d, 0x01) == 0xFFFFU);
		BTRON_CHECK("4 で割れない 32bit は断る",
		            pci_cfg_rd32(d, 0x02) == 0xFFFFFFFFU);
	});

	/* -------------------------------------------------------------- */
	/* (c) BAR と MSI-X。**装置が挿してあるときだけ走る**              */
	/* -------------------------------------------------------------- */

	BTRON_TEST("BAR を写して読み返せる (規律 R18)", {
		W i;
		W mapped = 0;

		for (i = 0; i < found && i < PCI_LIST_MAX; i++) {
			CONST PciDev *d = &list[i];
			UW idx = 0;

			/* **表示装置の BAR は測らない。** 測定の間 Command の MEM を
			 * 落とすので、実機ではそれが画面そのものである（pci.h の
			 * 注意書き / 詳細 F9「GOP のリニアフレームバッファ」）。 */
			if (d->classcode == PCI_CLASS_DISPLAY) {
				continue;
			}
			while (idx < 6) {
				PciBar b;
				UW cmd_before;
				UD va;
				UD size = 0;

				cmd_before = pci_cfg_rd16(d, PCI_COMMAND);
				if (!pci_bar(d, idx, &b)) {
					idx++;
					continue;
				}
				/* **測定は Command を一度落とすが、必ず元に戻すこと。**
				 * 戻し忘れると以後その装置が一切応答しなくなり、症状は
				 * 「読むと全部 0xFF」という分かりにくい形で出る。 */
				BTRON_CHECK("Command が元に戻る",
				            pci_cfg_rd16(d, PCI_COMMAND) == cmd_before);

				hal_console_puts("      BAR");
				tst_put_dec((W)idx);
				hal_console_puts(" @ ");
				put_hexn(b.pa, 16);
				hal_console_puts(" size ");
				put_hexn(b.size, 8);
				hal_console_puts(b.is_io ? " (I/O)" : " (MMIO)");
				hal_console_puts(b.is_64 ? " 64bit\n" : "\n");

				/* 大きさは必ず 2 のべき乗。測り方（全ビット 1 を書いて
				 * 反転して +1）がそれを保証するので、外れていたら
				 * 読み返しかマスクが壊れている。 */
				BTRON_CHECK("大きさは 2 のべき乗",
				            (b.size & (b.size - 1)) == 0);
				BTRON_CHECK("アドレスは大きさに整列",
				            (b.pa & (b.size - 1)) == 0);

				if (b.is_io) {
					/* I/O 空間は ECAM を選んだ時点で使わないと決めた
					 * （詳細 F8）。MMIO として写せてはいけない。 */
					BTRON_CHECK("I/O BAR は写せない",
					            pci_bar_map(d, idx, 0) == 0);
					idx++;
					continue;
				}
				va = pci_bar_map(d, idx, &size);
				if (va == 0) {
					idx += b.is_64 ? 2U : 1U;
					continue;
				}
				mapped++;
				BTRON_CHECK("大きさが返る", size == b.size);
				/* 写像は上位半分の MMIO 区画（規律 R18）。
				 * 下位半分に張ると画面の貸出と一緒に消える。 */
				BTRON_CHECK("MMIO 区画の中",
				            va >= BTRON_MMIO_BASE &&
				                va < BTRON_RESERVED_BASE);
				/* 別名は物理アドレスで一意なので二度目も同じ番地。 */
				BTRON_CHECK("冪等", pci_bar_map(d, idx, 0) == va);

				/* **写しただけでは読めない。** 応答を許すまでは BAR の
				 * 番地に装置が出てこない（ファームウェアは使い終わった
				 * 装置の MEM を落として渡してくる。pci_enable の注記）。 */
				BTRON_CHECK("応答を許せる", pci_enable(d, False) == ER_OK);
				BTRON_CHECK("MEM が立った",
				            (pci_cfg_rd16(d, PCI_COMMAND) & PCI_CMD_MEM) != 0);
				/* 読み返せること。値は装置次第なので問わない。 */
				(void)*(volatile UW *)(VP)va;

				idx += b.is_64 ? 2U : 1U;
			}
		}
		hal_console_puts("    写した BAR: ");
		tst_put_dec(mapped);
		hal_console_puts(" 本\n");
		/* **1 本も写せていないなら、この検査は何も確かめていない。**
		 * ㊿ の穴（構成によって飛ばされる検査）に落ちないよう、
		 * 装置が居るのに 0 本なら落とす。 */
		BTRON_CHECK("装置が居れば BAR が写せる", mapped > 0);
	});

	BTRON_TEST("MSI-X テーブルを構成できる (詳細 F8)", {
		W i;
		W with_msix = 0;

		for (i = 0; i < found && i < PCI_LIST_MAX; i++) {
			CONST PciDev *d = &list[i];
			PciMsix mx;
			UD addr = 0;
			UW data = 0;
			UW intid = 0;
			ER hal_er;

			if (d->classcode == PCI_CLASS_DISPLAY) {
				continue; /* BAR を測るので表示装置は触らない */
			}
			/* MSI（MSI-X が無い装置向け）の口も、有無を正しく答える
			 * こと。実測では QEMU の virtio-pci はどれも MSI を持たず
			 * MSI-X だけで、**それが MSI-X を本線にした裏付けでもある**
			 * （詳細 F8）。 */
			{
				PciMsi ms;

				BTRON_CHECK("MSI の有無と口が一致",
				            (pci_cap_find(d, PCI_CAP_MSI) != 0) ==
				                (pci_msi_open(d, &ms) == ER_OK));
			}
			if (pci_cap_find(d, PCI_CAP_MSIX) == 0) {
				/* MSI-X が無ければ open も断らなければならない。 */
				BTRON_CHECK("MSI-X 無しは断る",
				            pci_msix_open(d, &mx) == ER_NOSPT);
				continue;
			}
			if (pci_msix_open(d, &mx) != ER_OK) {
				continue;
			}
			with_msix++;
			hal_console_puts("      MSI-X ");
			tst_put_dec((W)mx.count);
			hal_console_puts(" ベクタ (cap +");
			put_hexn(mx.cap, 2);
			hal_console_puts(" table ");
			put_hexn(mx.table, 16);
			hal_console_puts(")\n");

			BTRON_CHECK("ベクタが 1 本以上", mx.count >= 1);
			/* **鎖を辿れていること。**
			 *
			 * 実測で **MSI-X は鎖の先頭に来る**（0x98=0x11 のあと
			 * 0x84/0x70/0x60/0x50/0x40 が全部ベンダ固有 0x09）。つまり
			 * MSI-X が見つかっただけでは next ポインタを 1 度も辿って
			 * いない。逆テストで next を ID と取り違えても全部通った
			 * のがこれで、**鎖の最後の要素を探させて初めて辿らせられる**。 */
			{
				UW last_id = 0;
				W ncap = spec_cap_walk(d, &last_id);
				UW at;

				hal_console_puts("      ケーパビリティ ");
				tst_put_dec(ncap);
				hal_console_puts(" 個（最後は ");
				put_hexn(last_id, 2);
				hal_console_puts("）\n");

				BTRON_CHECK("鎖に 2 個以上ある", ncap >= 2);
				at = pci_cap_find(d, last_id);
				BTRON_CHECK("鎖の先も見つかる", at != 0);
				BTRON_CHECK("見つけた先の ID が合う",
				            at != 0 && pci_cfg_rd8(d, at) == last_id);
			}
			BTRON_CHECK("無いケーパビリティは 0",
			            pci_cap_find(d, 0xFE) == 0);
			BTRON_CHECK("テーブルは MMIO 区画の中",
			            mx.table >= BTRON_MMIO_BASE &&
			                mx.table < BTRON_RESERVED_BASE);
			/* **open しただけでは有効にしない。** 宛先を書く前に有効に
			 * すると、装置が何も書いていない番地へ割込みを送る
			 * （規律 R29 と同じ形。msi.c の「有効にする順番」）。 */
			BTRON_CHECK("open では有効にしない", !pci_msix_enabled(d, &mx));

			/* 宛先はアーキの割込みコントローラが決める（詳細 F8）。
			 * arm64 には ITS / GICv2m がまだ無いので取れない。
			 * **取れないことと、テーブルを構成できることは別。** */
			hal_er = hal_msi_alloc(&intid, &addr, &data);
			hal_console_puts("      宛先の割り当て: ");
			if (hal_er == E_OK) {
				hal_console_puts("取れた INTID ");
				tst_put_dec((W)intid);
				hal_console_puts("\n");
			} else {
				hal_console_puts("まだ無い（翻訳器が要る）\n");
				/* 取れないときは出力を触っていないこと。中途半端な値を
				 * 書くと、実機で装置が何処でもない番地へ書き始める。 */
				BTRON_CHECK("取れないなら触らない",
				            hal_er == E_NOSPT && addr == 0 && data == 0 &&
				                intid == 0);
				/* 検査用の値で構成そのものを確かめる。マスクしたままで
				 * MSI-X も有効にしないので、どこにも割込みは飛ばない。
				 * **これはハードウェアのアドレスではない**（規律 R19）。
				 * 本物の宛先は上の hal_msi_alloc からしか来ない。
				 * 見れば偽物と分かる値にしてある。 */
				addr = 0x00000000DEADBE00ULL;
				data = 0x000000A5U;
			}

			/* ここが本題 — **全ベクタに別々の宛先を書いて、仕様の配置で
			 * 読み返せること。**
			 *
			 * 1 本だけ書いて同じ API で読み返すと、要素長やフィールド
			 * 位置を間違えても循環して通る。全部に違う値を書き、
			 * **検査が持っている仕様の配置** (SPEC_MSIX_*) で直接
			 * 読み返して照らす。 */
			{
				UW v;
				Bool ok = True;

				for (v = 0; v < mx.count; v++) {
					if (pci_msix_set(&mx, v, addr + (UD)v * 0x40,
					                 data + v) != ER_OK) {
						ok = False;
					}
				}
				BTRON_CHECK("全ベクタに書ける", ok);

				for (v = 0; v < mx.count; v++) {
					UD at = mx.table + (UD)v * SPEC_MSIX_ENT;
					UD want_addr = addr + (UD)v * 0x40;
					UD got_addr =
					    (UD)*(volatile UW *)(VP)(at + SPEC_MSIX_ADDR_LO) |
					    ((UD)*(volatile UW *)(VP)(at + SPEC_MSIX_ADDR_HI)
					     << 32);

					if (got_addr != want_addr) {
						ok = False;
					}
					if (*(volatile UW *)(VP)(at + SPEC_MSIX_DATA) !=
					    data + v) {
						ok = False;
					}
					/* 書いた直後は必ずマスクされている（msi.c の手順 1）。
					 * **ここがデータ欄とマスク欄の取り違えを捕まえる。** */
					if ((*(volatile UW *)(VP)(at + SPEC_MSIX_CTRL) & 1U) ==
					    0) {
						ok = False;
					}
				}
				BTRON_CHECK("仕様の配置で読み返せる", ok);
			}
			/* API 経由の読み返しも同じ値を返すこと。 */
			{
				UD rd_addr = 0;
				UW rd_data = 0;

				BTRON_CHECK("読み返せる",
				            pci_msix_get(&mx, 0, &rd_addr, &rd_data));
				BTRON_CHECK("アドレスが一致", rd_addr == addr);
				BTRON_CHECK("データが一致", rd_data == data);
			}
			BTRON_CHECK("マスクを下げられる",
			            pci_msix_mask(&mx, 0, False) == ER_OK);
			BTRON_CHECK("下がった",
			            (*(volatile UW *)(VP)(mx.table + SPEC_MSIX_CTRL) &
			             1U) == 0);
			BTRON_CHECK("上げ直せる", pci_msix_mask(&mx, 0, True) == ER_OK);
			BTRON_CHECK("上がった",
			            (*(volatile UW *)(VP)(mx.table + SPEC_MSIX_CTRL) &
			             1U) != 0);

			/* 範囲外のベクタは断る。 */
			BTRON_CHECK("範囲外のベクタ",
			            pci_msix_set(&mx, mx.count, addr, data) == ER_PAR);

			/* **宛先が取れていないので有効にはしない。** 有効にして
			 * よいのは割込みが本当に届く宛先を書けたときだけ。 */
			BTRON_CHECK("まだ有効にしていない", !pci_msix_enabled(d, &mx));
		}
		hal_console_puts("    MSI-X 装置: ");
		tst_put_dec(with_msix);
		hal_console_puts(" 個\n");
		/* ㊿ の穴を塞ぐ。装置が居るのに MSI-X が 1 つも無いなら、
		 * ケーパビリティ列の辿り方が壊れている疑いがある。 */
		BTRON_CHECK("装置が居れば MSI-X も見つかる", with_msix > 0);
	});
}

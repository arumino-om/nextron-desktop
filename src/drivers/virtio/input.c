/*
 * input.c — virtio-input（キーボード／ポインティングデバイス）
 *
 * 周辺核 §1.7 のイベント管理に実体を与える。仕様 §1.8 の冒頭が言う
 * 「キーボード／PD はアプリケーションから見ればイベント管理だが、
 * ドライバとのインタフェースはデバイス管理でサポートしている」に従い、
 * 論理デバイス名 "kbd" / "pd" でデバイス管理にも登録する。
 *
 * ------------------------------------------------------------------------
 * キートップコードをどう決めたか
 * ------------------------------------------------------------------------
 *
 * 仕様 §1.7.3 は「キートップコードは、キーの物理的位置に応じた固定的な
 * 8 ビットのコード (0〜255)」と言うだけで、値は規定していない
 * （「keymax はキーボードにより規定される」）。つまり**ここが決める**。
 *
 * 本OS は **evdev のキーコードをそのままキートップコードとする**。理由は
 *
 *   - evdev のコードは元々「キーの物理的位置」の番号で、意味が一致する
 *   - 0〜255 に収まり、KEYMAX = 256 とちょうど合う
 *   - virtio-input / USB HID から変換表なしで通る
 *
 * したがって `KeyTab` は「evdev コード → TRON 文字コード」の表になる。
 * 配列の違い（US / JP）はこの表の差で吸収する。
 *
 * ------------------------------------------------------------------------
 * 文字コードは TRON の制御コードに合わせる
 * ------------------------------------------------------------------------
 *
 * 共有データ仕様 2.3.3 の制御コードは
 *
 *   0x09 タブ / 0x0A 改段落 / 0x0B 改コラム / 0x0C 改ページ / 0x0D 改行
 *
 * で、**通常の「実行」キーは 0x0A（改段落）**であって 0x0D ではない。
 * ASCII の習慣で 0x0D にすると、段落の概念を持つ TAD と食い違う。
 *
 * 矢印キーや機能キーには TRON 文字コードが無いので TNULL を返す。
 * アプリはキートップコードとメタキー状態で判断する（命令シフトと
 * 組み合わせて使うのが BTRON の流儀）。
 */

#include <btron/device.h>
#include <btron/event.h>
#include <btron/intc.h>
#include <btron/screen.h>
#include <btron/virtio.h>

/* ------------------------------------------------------------------ */
/* virtio-input の設定空間とイベント形式 (VIRTIO 1.1 §5.8)             */
/* ------------------------------------------------------------------ */

#define VIN_CFG_SELECT 0x00
#define VIN_CFG_SUBSEL 0x01
#define VIN_CFG_SIZE 0x02
#define VIN_CFG_UNION 0x08

#define VIN_CFG_ID_NAME 0x01
#define VIN_CFG_ID_DEVIDS 0x03
#define VIN_CFG_EV_BITS 0x11
#define VIN_CFG_ABS_INFO 0x12

/* イベントキューは 0 番、ステータスキューは 1 番。 */
#define VIN_Q_EVENT 0
#define VIN_EVENT_SIZE 8 /* le16 type, le16 code, le32 value */

/* Linux input の種別コード（evdev）。 */
#define EV_SYN_ 0x00
#define EV_KEY_ 0x01
#define EV_REL_ 0x02
#define EV_ABS_ 0x03

#define REL_X 0x00
#define REL_Y 0x01
#define ABS_X 0x00
#define ABS_Y 0x01

#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111
#define BTN_MIDDLE 0x112
#define BTN_TOUCH 0x14a

/* メタキーに割り当てる evdev コード。 */
#define KEY_LEFTCTRL 29
#define KEY_LEFTSHIFT 42
#define KEY_RIGHTSHIFT 54
#define KEY_LEFTALT 56
#define KEY_CAPSLOCK 58
#define KEY_RIGHTCTRL 97
#define KEY_RIGHTALT 100
#define KEY_A_ 30 /* 装置がキーボードかの判定に使う */

/* ------------------------------------------------------------------ */
/* 既定の文字コード変換表（US 101 配列）                               */
/* ------------------------------------------------------------------ */

/* 変換表の番号。kctsel がこの 3 つを選び分ける。 */
#define KT_PLAIN 0
#define KT_SHIFT 1
#define KT_CMD 2 /* 命令シフト。制御コードを出す */
#define KT_COUNT 3

typedef struct KeyRow {
	UB code;  /* evdev キーコード = キートップコード */
	UH plain; /* シフト無し */
	UH shift; /* 左右どちらかのシフト */
} KeyRow;

/* TRON 制御コード（共有データ仕様 2.3.3）。 */
#define TRON_TAB 0x0009
#define TRON_PARA 0x000A /* 改段落。通常の「実行」 */
#define TRON_LINE 0x000D /* 改行。段落内の改行 */
/* 後退は仕様が特別な意味を与えていないが、制御コードの範囲
 * (00)(00)〜(00)(20) に含まれるので ASCII と同じ 0x08 を使う。 */
#define TRON_BS 0x0008
#define TRON_SEP 0x0020 /* セパレータ（空白） */

LOCAL CONST KeyRow us101[] = {
    {2, '1', '!'}, {3, '2', '@'}, {4, '3', '#'}, {5, '4', '$'},
    {6, '5', '%'}, {7, '6', '^'}, {8, '7', '&'}, {9, '8', '*'},
    {10, '9', '('}, {11, '0', ')'}, {12, '-', '_'}, {13, '=', '+'},
    {14, TRON_BS, TRON_BS}, {15, TRON_TAB, TRON_TAB},
    {16, 'q', 'Q'}, {17, 'w', 'W'}, {18, 'e', 'E'}, {19, 'r', 'R'},
    {20, 't', 'T'}, {21, 'y', 'Y'}, {22, 'u', 'U'}, {23, 'i', 'I'},
    {24, 'o', 'O'}, {25, 'p', 'P'}, {26, '[', '{'}, {27, ']', '}'},
    {28, TRON_PARA, TRON_LINE}, /* 実行 / シフト+実行 = 段落内改行 */
    {30, 'a', 'A'}, {31, 's', 'S'}, {32, 'd', 'D'}, {33, 'f', 'F'},
    {34, 'g', 'G'}, {35, 'h', 'H'}, {36, 'j', 'J'}, {37, 'k', 'K'},
    {38, 'l', 'L'}, {39, ';', ':'}, {40, '\'', '"'}, {41, '`', '~'},
    {43, '\\', '|'},
    {44, 'z', 'Z'}, {45, 'x', 'X'}, {46, 'c', 'C'}, {47, 'v', 'V'},
    {48, 'b', 'B'}, {49, 'n', 'N'}, {50, 'm', 'M'},
    {51, ',', '<'}, {52, '.', '>'}, {53, '/', '?'},
    {55, '*', '*'}, {57, TRON_SEP, TRON_SEP},
    /* テンキー。ロック状態は扱わないので数字を固定で出す。 */
    {71, '7', '7'}, {72, '8', '8'}, {73, '9', '9'}, {74, '-', '-'},
    {75, '4', '4'}, {76, '5', '5'}, {77, '6', '6'}, {78, '+', '+'},
    {79, '1', '1'}, {80, '2', '2'}, {81, '3', '3'}, {82, '0', '0'},
    {83, '.', '.'}, {96, TRON_PARA, TRON_PARA}, {98, '/', '/'},
};

LOCAL KeyTab keytab;

/* kctsel はメタキー状態 (CERLKA 6 ビット) から表番号への写像。
 * 命令シフトが最優先、次にシフト、それ以外は素の表。
 * 日本語/カタカナのビット (A, K) は**いまは無視する**。日本語入力は
 * TIP（かな漢字変換）マネージャの仕事で、そこができたら表を増やす。 */
LOCAL void build_keytab(void)
{
	W i;

	keytab.keymax = KEYMAX;
	keytab.kctmax = KT_COUNT;
	for (i = 0; i < KCTSEL; i++) {
		if ((i & KM_C) != 0) {
			keytab.kctsel[i] = KT_CMD;
		} else if ((i & (KM_L | KM_R)) != 0) {
			keytab.kctsel[i] = KT_SHIFT;
		} else {
			keytab.kctsel[i] = KT_PLAIN;
		}
	}
	for (i = 0; i < KT_COUNT * KEYMAX; i++) {
		keytab.kct[i] = TNULL;
	}
	for (i = 0; i < (W)(sizeof(us101) / sizeof(us101[0])); i++) {
		CONST KeyRow *r = &us101[i];

		keytab.kct[KT_PLAIN * KEYMAX + r->code] = r->plain;
		keytab.kct[KT_SHIFT * KEYMAX + r->code] = r->shift;
		/* 命令シフトは英字で制御コードを出す（Ctrl+A = 0x01）。
		 * 英字以外は素の文字をそのまま通す。 */
		if (r->plain >= 'a' && r->plain <= 'z') {
			keytab.kct[KT_CMD * KEYMAX + r->code] =
			    (UH)(r->plain - 'a' + 1);
		} else {
			keytab.kct[KT_CMD * KEYMAX + r->code] = r->plain;
		}
	}
}

/* ------------------------------------------------------------------ */
/* 装置                                                                */
/* ------------------------------------------------------------------ */

#define VIN_MAX VIN_PROBE_MAX
#define VIN_QSZ 16 /* イベントキューの深さ。押しっぱなしでも足りる */

typedef struct InDev {
	VirtioDev d;
	VirtQ q;
	W kind;
	/* 絶対座標の範囲。装置が持つレンジから画面へ伸縮する。 */
	W amin[2];
	W amax[2];
	W pending[2]; /* 同期前の座標 (ABS_X / ABS_Y) */
	Bool has[2];
} InDev;

LOCAL InDev dev[VIN_MAX];
LOCAL W ndev;

/* ------------------------------------------------------------------ */
/* 設定空間の読み出し                                                  */
/* ------------------------------------------------------------------ */

/* select / subsel を立てて size を読む。戻り値は有効バイト数。 */
LOCAL UW cfg_select(CONST VirtioDev *d, UW sel, UW sub)
{
	virtio_cfg_wr8(d, VIN_CFG_SELECT, sel);
	virtio_cfg_wr8(d, VIN_CFG_SUBSEL, sub);
	return virtio_cfg_rd8(d, VIN_CFG_SIZE);
}

/* EV_BITS のビットマップで bit が立っているか。 */
LOCAL Bool ev_bit(CONST VirtioDev *d, UW evtype, UW bit)
{
	UW size = cfg_select(d, VIN_CFG_EV_BITS, evtype);
	UW byte = bit / 8;

	if (size == 0 || byte >= size) {
		return False;
	}
	return (virtio_cfg_rd8(d, VIN_CFG_UNION + byte) & (1U << (bit % 8))) != 0;
}

/* ABS_INFO から min / max を取る。 */
LOCAL void abs_range(CONST VirtioDev *d, UW axis, W *min, W *max)
{
	UW size = cfg_select(d, VIN_CFG_ABS_INFO, axis);

	if (size < 8) {
		*min = 0;
		*max = 0;
		return;
	}
	*min = (W)virtio_cfg_rd32(d, VIN_CFG_UNION + 0);
	*max = (W)virtio_cfg_rd32(d, VIN_CFG_UNION + 4);
}

/* 装置の種類を見分ける。
 *
 * 名前（ID_NAME）で判定すると QEMU 固有の文字列に依存するので、
 * **能力ビット**で判定する。これが virtio-input の本来の使い方である。 */
LOCAL W classify(CONST VirtioDev *d)
{
	if (ev_bit(d, EV_ABS_, ABS_X) && ev_bit(d, EV_ABS_, ABS_Y)) {
		return VIN_KIND_ABS;
	}
	if (ev_bit(d, EV_REL_, REL_X) && ev_bit(d, EV_REL_, REL_Y)) {
		return VIN_KIND_REL;
	}
	if (ev_bit(d, EV_KEY_, KEY_A_)) {
		return VIN_KIND_KBD;
	}
	return VIN_KIND_NONE;
}

/* ------------------------------------------------------------------ */
/* イベントの解釈                                                      */
/* ------------------------------------------------------------------ */

/* メタキーの evdev コード → ES_* 。文字にならないキーはここで吸収する。 */
LOCAL UW meta_bit(UW code)
{
	switch (code) {
	case KEY_LEFTSHIFT:
		return ES_LSHFT;
	case KEY_RIGHTSHIFT:
		return ES_RSHFT;
	case KEY_LEFTCTRL:
	case KEY_RIGHTCTRL:
		return ES_CMD; /* 命令シフト */
	case KEY_LEFTALT:
	case KEY_RIGHTALT:
		return ES_EXT; /* 拡張シフト */
	default:
		return 0;
	}
}

/* 絶対座標を画面の大きさへ伸縮する。 */
LOCAL W scale(W v, W min, W max, W span)
{
	W range = max - min;

	if (range <= 0 || span <= 0) {
		return 0;
	}
	if (v < min) {
		v = min;
	}
	if (v > max) {
		v = max;
	}
	/* 四捨五入せず切り捨てる。端が 1 画素届かないのを防ぐため
	 * 分母を range にして span-1 を掛ける。 */
	return (W)(((UD)(v - min) * (UD)(span - 1)) / (UD)range);
}

LOCAL void handle(InDev *v, UW type, UW code, UW value)
{
	switch (type) {
	case EV_KEY_:
		if (code == BTN_LEFT || code == BTN_TOUCH) {
			evt_pd_button(False, value != 0);
		} else if (code == BTN_RIGHT || code == BTN_MIDDLE) {
			evt_pd_button(True, value != 0);
		} else if (code == KEY_CAPSLOCK) {
			/* ロックキー。押し下げのたびに反転する。
			 * 仕様 §1.7.3 の「英語ロックキー」に当てる。 */
			if (value == 1) {
				static Bool on = False;

				on = !on;
				evt_meta(on ? ES_ALPH : 0, ES_ALPH);
			}
		} else if (meta_bit(code) != 0) {
			evt_meta(value != 0 ? meta_bit(code) : 0, meta_bit(code));
		} else if (code < KEYMAX) {
			/* value 2 は装置側の自動リピート。**捨てる。**
			 * 自動リピートは周辺核が EV_AUTKEY として生成するもので
			 * （仕様: キューに入れない）、二重になる。 */
			if (value == 0 || value == 1) {
				evt_key((UH)code, value != 0);
			}
		}
		break;
	case EV_REL_:
		if (code == REL_X) {
			evt_pd_delta((W)value, 0);
		} else if (code == REL_Y) {
			evt_pd_delta(0, (W)value);
		}
		break;
	case EV_ABS_:
		/* 軸ごとに別イベントで来るので、EV_SYN まで溜めてから動かす。
		 * 片方だけで動かすと斜めの動きが階段になる。 */
		if (code == ABS_X || code == ABS_Y) {
			v->pending[code] = (W)value;
			v->has[code] = True;
		}
		break;
	case EV_SYN_: {
		CONST Screen *sc = screen();
		PNT p;

		if (v->kind != VIN_KIND_ABS || sc == 0) {
			break;
		}
		if (!v->has[ABS_X] && !v->has[ABS_Y]) {
			break;
		}
		p = evt_pd_pos();
		if (v->has[ABS_X]) {
			p.x = (H)scale(v->pending[ABS_X], v->amin[ABS_X],
			               v->amax[ABS_X], (W)sc->width);
		}
		if (v->has[ABS_Y]) {
			p.y = (H)scale(v->pending[ABS_Y], v->amin[ABS_Y],
			               v->amax[ABS_Y], (W)sc->height);
		}
		v->has[ABS_X] = False;
		v->has[ABS_Y] = False;
		evt_pd_moved(p);
		break;
	}
	default:
		break;
	}
}

/* イベントキューを空にする。割込みハンドラから呼ぶ。 */
LOCAL void drain(InDev *v)
{
	UW idx;
	UW len;

	while (vq_take(&v->q, &idx, &len)) {
		if (len >= VIN_EVENT_SIZE && idx < v->q.qsz) {
			CONST UB *b = VQ_BUF(&v->q, idx);
			UW type = (UW)b[0] | ((UW)b[1] << 8);
			UW code = (UW)b[2] | ((UW)b[3] << 8);
			UW value = (UW)b[4] | ((UW)b[5] << 8) | ((UW)b[6] << 16) |
			           ((UW)b[7] << 24);

			handle(v, type, code, value);
		}
		/* 空いた箱をすぐ返す。返さないと装置が書く場所を失う。 */
		vq_offer(&v->q, idx);
	}
	vq_kick(&v->d, &v->q);
}

LOCAL void vin_isr(UW intid, VP arg)
{
	InDev *v = (InDev *)arg;
	UW st;

	(void)intid;
	st = virtio_int_status(&v->d);
	if (st == 0) {
		return;
	}
	virtio_int_ack(&v->d, st);
	if ((st & VIRTIO_INT_USED) != 0) {
		drain(v);
	}
}

/* ------------------------------------------------------------------ */
/* デバイス管理 (§1.8) への登録                                        */
/* ------------------------------------------------------------------ */

/* キーボードと PD は「アプリから見ればイベント管理」なので、
 * デバイス管理側では状態の確認だけができればよい。read / write は
 * 置かない（置くと「読めるふり」になる）。 */

LOCAL CONST DevDriver kbd_drv = {
    "kbd", DA_CHARDEV | DK_UNDEF, 0, -1, 0, 0, 0, 0,
};

LOCAL CONST DevDriver pd_drv = {
    "pd", DA_CHARDEV | DK_UNDEF, 0, -1, 0, 0, 0, 0,
};

/* ------------------------------------------------------------------ */
/* 起動                                                                */
/* ------------------------------------------------------------------ */

LOCAL ERR start_one(InDev *v)
{
	ERR er;
	UW i;

	er = virtio_setup(&v->d, 0);
	if (er < 0) {
		return er;
	}
	er = virtio_queue_setup(&v->d, &v->q, VIN_Q_EVENT, VIN_QSZ,
	                        VIN_EVENT_SIZE, True);
	if (er < 0) {
		return er;
	}
	v->kind = classify(&v->d);
	if (v->kind == VIN_KIND_ABS) {
		abs_range(&v->d, ABS_X, &v->amin[ABS_X], &v->amax[ABS_X]);
		abs_range(&v->d, ABS_Y, &v->amin[ABS_Y], &v->amax[ABS_Y]);
	}
	v->has[ABS_X] = False;
	v->has[ABS_Y] = False;

	/* 空の箱をすべて装置に渡す。これを忘れると 1 つもイベントが来ない。 */
	for (i = 0; i < v->q.qsz; i++) {
		vq_offer(&v->q, i);
	}

	if (v->d.intid != 0) {
		er = (ERR)intc_set_handler(v->d.intid, vin_isr, v);
		if (er < 0) {
			return ER_SYS;
		}
		(void)intc_enable(v->d.intid);
	}
	/* DRIVER_OK を立ててから kick する。逆だと装置が受け取らない。 */
	virtio_ready(&v->d);
	vq_kick(&v->d, &v->q);
	return ER_OK;
}

EXPORT ERR vinput_register(void)
{
	VirtioDev found[VIN_MAX];
	KBD_ID id;
	W n;
	W i;
	Bool has_kbd = False;
	Bool has_pd = False;

	ndev = 0;
	n = virtio_mmio_probe(VIRTIO_ID_INPUT, found, VIN_MAX);
	if (n < 0) {
		return (ERR)n;
	}
	for (i = 0; i < n; i++) {
		InDev *v = &dev[ndev];

		v->d = found[i];
		v->kind = VIN_KIND_NONE;
		if (start_one(v) < 0) {
			continue;
		}
		if (v->kind == VIN_KIND_NONE) {
			continue; /* 能力が分からない装置は使わない */
		}
		if (v->kind == VIN_KIND_KBD) {
			has_kbd = True;
		} else {
			has_pd = True;
			/* 属性は装置の性質。絶対動作タイプなら set_pdp は不可。 */
			evt_set_pda(v->kind == VIN_KIND_ABS ? PD_ABS : PD_REL);
		}
		ndev++;
	}

	if (has_kbd) {
		build_keytab();
		/* evdev のコードを使うので、配列としては IBM 101 系英語。 */
		id.kind = (UH)(KB_IBM101 << KB_KIND_SHIFT);
		id.maker = 0;
		id.id[0] = 'V';
		id.id[1] = 'I';
		id.id[2] = 'R';
		id.id[3] = 'T';
		evt_set_kbd(&id, &keytab);
		(void)dev_register(&kbd_drv);
	}
	if (has_pd) {
		(void)dev_register(&pd_drv);
	}
	if (ndev == 0) {
		return ER_NOEXS; /* 入力装置が無い構成 */
	}
	return ER_OK;
}

EXPORT W vinput_count(void)
{
	return ndev;
}

EXPORT W vinput_kind(W i)
{
	if (i < 0 || i >= ndev) {
		return VIN_KIND_NONE;
	}
	return dev[i].kind;
}

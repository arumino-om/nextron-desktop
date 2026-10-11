/*
 * typedef.h — 基本データタイプ
 *
 * BTRON3 仕様書 第1編 第1章「基本データタイプ」に準拠。
 * 本OS での拡張は docs/design-issues.md の決定に従う。
 *
 *   A1  W は 32bit を維持し、溢れるフィールドのみ 64bit 型で拡張する
 *   A3  TC は 16bit を維持する
 *   A5  STIME は 64bit 化する（エポックは 1985-01-01 00:00:00 GMT のまま）
 *   A8  64bit 整数は D / UD（T-Kernel 系の慣例。B -> H -> W -> D）
 *   A2  LINK の f_id を 32bit に拡張する
 *
 * 規律 R5: W(32bit) と D(64bit) の暗黙変換を許さない。縮小変換は明示キャストのみ。
 */

#ifndef __BTRON_TYPEDEF_H__
#define __BTRON_TYPEDEF_H__

/* ------------------------------------------------------------------ */
/* 基本データ型 : ITRON 共通                                           */
/* ------------------------------------------------------------------ */

typedef signed char B;   /* 符号付き  8ビット整数 */
typedef signed short H;  /* 符号付き 16ビット整数 */
typedef signed int W;    /* 符号付き 32ビット整数 */
typedef unsigned char UB;    /* 符号無し  8ビット整数 */
typedef unsigned short UH;   /* 符号無し 16ビット整数 */
typedef unsigned int UW;     /* 符号無し 32ビット整数 */

typedef signed char VB;  /* 不定型  8ビットデータ */
typedef signed short VH; /* 不定型 16ビットデータ */
typedef signed int VW;   /* 不定型 32ビットデータ */

/* --- 本OS の拡張 (A8) --------------------------------------------- */
typedef signed long long D;    /* 符号付き 64ビット整数 */
typedef unsigned long long UD; /* 符号無し 64ビット整数 */
typedef signed long long VD;   /* 不定型  64ビットデータ */

typedef void *VP;     /* 不定型データへのポインタ */
typedef void (*FP)(); /* 一般関数ポインタ */

/* ------------------------------------------------------------------ */
/* Volatile : コンパイラによる最適化禁止指定                           */
/* ------------------------------------------------------------------ */

typedef volatile B _B;
typedef volatile H _H;
typedef volatile W _W;
typedef volatile D _D;
typedef volatile UB _UB;
typedef volatile UH _UH;
typedef volatile UW _UW;
typedef volatile UD _UD;
typedef volatile void _void;

/* ------------------------------------------------------------------ */
/* 修飾型                                                              */
/*                                                                     */
/* 仕様書の規定をそのまま用いる。EXPORT / IMPORT の対が BTRON のスタイル */
/* で、定義は 1 箇所に EXPORT、参照側は IMPORT と書く。                 */
/* 仕様書は「モジュール = ソースファイル 1 個」と定義している。          */
/* ------------------------------------------------------------------ */

#define CONST const
#define LOCAL static /* モジュール内でのみ使われる変数・関数 */
#define EXPORT       /* プログラム中の任意の箇所で参照可能な変数・関数の定義 */
                     /* 1 個につきプログラム中に 1 箇所だけ記述             */
#define IMPORT extern /* 同じものの参照宣言 */
#define VOID void

/* ------------------------------------------------------------------ */
/* 数値型                                                              */
/* ------------------------------------------------------------------ */

typedef float FLOAT;   /* 単精度 (32ビット) 浮動小数点 */
typedef double DOUBLE; /* 倍精度 (64ビット) 浮動小数点 */
typedef int BOOL;      /* TRUE or FALSE */
typedef W (*FUNCP)();  /* W 関数へのポインタ */

typedef enum { False = 0, True = 1 } Bool;

/* ------------------------------------------------------------------ */
/* ID 番号                                                             */
/* ------------------------------------------------------------------ */

typedef W ID;

/* ------------------------------------------------------------------ */
/* 時間                                                                */
/*                                                                     */
/* A5: STIME を 64bit 化。32bit のままだと 1985 年起点の符号付き秒が     */
/*     2053 年頃に溢れる。エポックは互換のため変更しない。              */
/* ------------------------------------------------------------------ */

typedef W MSEC; /* ミリ秒 */
typedef D STIME; /* システム時間。1985/01/01 00:00:00 GMT からの秒数 */

/* ------------------------------------------------------------------ */
/* 文字コード                                                          */
/*                                                                     */
/* A3 / B1: TC は 16bit ステートフル（面は言語指定コード 0xFE2x が運ぶ）。 */
/*          内部処理は 32bit ステートレスの WTC を使う。                 */
/*          規律 R8: 文字列の検索・カーソル移動・ソートを TC のまま      */
/*          行わない。「文字」は (TLANG, TC) の対である。                */
/* ------------------------------------------------------------------ */

typedef UH TC;             /* TRON 文字コード */
#define TNULL ((TC)0)      /* 文字列終端 */

typedef UH TLANG;          /* 言語(スクリプト)指定コードの短縮形 */
                           /* 0xAABB  AA = 0xFE の繰り返し数 - 1 */
                           /*         BB = 0xFE に続くコード      */
#define TSC_SYS 0x0021     /* システムスクリプト */
#define TSC_MIN 0x0021
#define TSC_MAX 0x003f

typedef W WTC;             /* ワイド TRON 文字 (TLANG << 16 | TC) */
#define WTNULL 0
#define towtc(lang, tc) (((WTC)(lang) & 0x7fff) << 16 | ((WTC)(tc) & 0xffff))
#define wtcchar(wtc) ((WTC)(wtc) & 0x0000ffff)
#define wtclang(wtc) ((WTC)(wtc) >> 16)

/* ------------------------------------------------------------------ */
/* エラーコード                                                        */
/*                                                                     */
/* 上位ハーフワードがエラークラス、下位が詳細エラーコード。             */
/* 本OS はリトルエンディアンに決定済み (A4) なので little 側の定義のみ。  */
/* ------------------------------------------------------------------ */

typedef union {
	W err; /* エラーコード */
	struct {
		UH detail; /* 詳細エラー   */
		H eclass;  /* エラークラス */
	} c;
} ErrCode;

typedef W ERR;  /* エラーまたは正常のみ戻す場合 */
typedef W WERR; /* エラーまたは意味のある値を戻す場合 */

#define OK (0)

/* ------------------------------------------------------------------ */
/* 共通定義                                                            */
/* ------------------------------------------------------------------ */

#define CLR 0x0000     /* クリア指定     */
#define NOCLR 0x0008   /* ノークリア指定 */
#define DELEXIT 0x8000 /* 終了時削除指定 */

/* ------------------------------------------------------------------ */
/* 大きさ / 点 / 長方形                                                */
/*                                                                     */
/* 座標系は 16bit 整数。E9（高DPI・スケーリング）で見直す可能性がある。 */
/* ------------------------------------------------------------------ */

typedef struct Size {
	H h; /* 幅   */
	H v; /* 高さ */
} SIZE;

typedef struct point {
	H x; /* 水平座標値 */
	H y; /* 垂直座標値 */
} PNT;

typedef union rect {
	struct _rect {
		H left;
		H top;
		H right;
		H bottom;
	} c;
	struct {
		PNT lefttop;
		PNT rightbot;
	} p;
} RECT;

/* ------------------------------------------------------------------ */
/* ファイルへのリンク（仮身）                                          */
/*                                                                     */
/* A2: f_id を UH -> UW に拡張。1 ファイルシステムあたりの実身数の上限が */
/*     65,535 では汎用 OS として不足するため。                          */
/*                                                                     */
/* 注意: この構造体はディスク上のリンクレコードに対応するが、           */
/*       メモリ表現とディスク表現は別物である（規律 R1）。              */
/*       ディスク側は 詳細 C1 の拡張形式3（レコードインデックス 32B）。  */
/*       この構造体を直接 I/O してはならない。                          */
/* ------------------------------------------------------------------ */

#define L_FSNM 20 /* ファイルシステム名の長さ(文字数) */

typedef struct {
	TC fs_name[L_FSNM]; /* ファイルシステム名 */
	UW f_id;            /* ファイル ID  (A2: UH -> UW) */
	UH atr1;            /* 属性データ1 */
	UH atr2;            /* 属性データ2 */
	UH atr3;            /* 属性データ3 */
	UH atr4;            /* 属性データ4 */
	UH atr5;            /* 属性データ5 */
} LINK;

/* ------------------------------------------------------------------ */
/* ウィンドウ                                                          */
/* ------------------------------------------------------------------ */

typedef W WID; /* ウィンドウ ID */

/* ------------------------------------------------------------------ */
/* 幅の検証                                                            */
/* ------------------------------------------------------------------ */

_Static_assert(sizeof(B) == 1 && sizeof(H) == 2 && sizeof(W) == 4, "B/H/W");
_Static_assert(sizeof(UB) == 1 && sizeof(UH) == 2 && sizeof(UW) == 4, "UB/UH/UW");
_Static_assert(sizeof(D) == 8 && sizeof(UD) == 8, "D/UD");
_Static_assert(sizeof(TC) == 2 && sizeof(WTC) == 4, "TC/WTC");
_Static_assert(sizeof(STIME) == 8, "STIME (A5)");
_Static_assert(sizeof(VP) == 8, "64bit pointer");

#endif /* __BTRON_TYPEDEF_H__ */

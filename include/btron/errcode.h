/*
 * errcode.h — エラーコード
 *
 * docs/design-issues.md A15 / 規律 R6 / 規律 R20。
 *
 * **エラーコードの族が層ごとに 2 つある。混ぜてはならない（規律 R20）。**
 *
 *   中心核 (μT-Kernel 3.0)   E_*     tk_* が返す
 *   周辺核 (BTRON3)          ER_*    周辺核 API が返す
 *   DP (BTRON3)              EG_*
 *   外殻 (BTRON3)            EX_*
 *
 * どちらも符号化は同じで「上位ハーフワード = メインエラーコード、
 * 下位 = サブエラーコード」だが、**メインエラーコードの番号空間が重なっている**。
 * 例: 周辺核の `ER_PAR` はメイン -2、中心核の `E_PAR` はメイン -17。
 * -51 以降は A15 が「周辺核の仕様拡張用に空ける」と決めた範囲だが、
 * μT-Kernel は既に `E_DLT`(-51) / `E_DISWAI`(-52) を使っている。
 *
 * **両仕様がそう作られているので直せない。** 仕様自身も「どの API を呼んだか」で
 * 族を区別している。したがって本OS も同じ規律で扱う:
 *
 *   - 1 つの戻り値に両方の族を混ぜない
 *   - 層をまたぐときは呼び出し側が翻訳する
 *   - 「エラーコードから名前を引く」汎用関数を作らない（族が分からないため）
 */

#ifndef __BTRON_ERRCODE_H__
#define __BTRON_ERRCODE_H__

#include <btron/typedef.h>

typedef W ER; /* 中心核のエラーコード。BTRON3 の ERR とは族が違う */

/* 仕様の符号化。TK_SUPPORT_SERCD = TRUE 相当（ER が 32bit なので推奨値）。
 *
 * 仕様書の定義は `((ER)(mer) << 16 | (ER)(UH)(ser))` だが、メインエラーコードは
 * 負なので**符号付きの左シフトは未定義動作**になる（-Wshift-negative-value）。
 * 符号無しでシフトしてから ER に戻す形に直してある。値は同じ。 */
#define MERCD(er) ((ER)(er) >> 16)                 /* メインエラーコード */
#define SERCD(er) ((H)(er))                        /* サブエラーコード */
#define ERCD(mer, ser) ((ER)(((UW)(W)(mer) << 16) | (UW)(UH)(ser)))

/* ------------------------------------------------------------------ */
/* 中心核 (μT-Kernel 3.0) のエラーコード                               */
/* ------------------------------------------------------------------ */

#define E_OK 0 /* 正常終了 */

/* 内部エラークラス (5〜8) */
#define E_SYS ERCD(-5, 0)   /* システムエラー */
#define E_NOCOP ERCD(-6, 0) /* コプロセッサ使用不可 */

/* 未サポートエラークラス (9〜16) */
#define E_NOSPT ERCD(-9, 0)   /* 未サポート機能 */
#define E_RSFN ERCD(-10, 0)   /* 予約機能コード番号 */
#define E_RSATR ERCD(-11, 0)  /* 予約属性 */

/* パラメータエラークラス (17〜24) */
#define E_PAR ERCD(-17, 0) /* パラメータエラー */
#define E_ID ERCD(-18, 0)  /* 不正 ID 番号 */

/* 呼出コンテキストエラークラス (25〜32) */
#define E_CTX ERCD(-25, 0)   /* コンテキストエラー */
#define E_MACV ERCD(-26, 0)  /* メモリアクセス不能 */
#define E_OACV ERCD(-27, 0)  /* オブジェクトアクセス権違反 */
#define E_ILUSE ERCD(-28, 0) /* システムコール不正使用 */

/* 資源不足エラークラス (33〜40) */
#define E_NOMEM ERCD(-33, 0) /* メモリ不足 */
#define E_LIMIT ERCD(-34, 0) /* システムの制限を超えた */

/* オブジェクト状態エラークラス (41〜48) */
#define E_OBJ ERCD(-41, 0)   /* オブジェクトの状態が不正 */
#define E_NOEXS ERCD(-42, 0) /* オブジェクトが存在していない */
#define E_QOVR ERCD(-43, 0)  /* キューイング・ネストのオーバーフロー */

/* 待ち解除エラークラス (49〜56) */
#define E_RLWAI ERCD(-49, 0)  /* 待ち状態を強制解除された */
#define E_TMOUT ERCD(-50, 0)  /* ポーリング失敗またはタイムアウト */
#define E_DLT ERCD(-51, 0)    /* 待ちオブジェクトが削除された */
#define E_DISWAI ERCD(-52, 0) /* 待ち禁止による待ち解除 */

/* デバイスエラークラス (57〜64) */
#define E_IO ERCD(-57, 0)    /* 入出力エラー */
#define E_NOMDA ERCD(-58, 0) /* メディアが存在しない */

/* 各種状態エラークラス (65〜72) */
#define E_BUSY ERCD(-65, 0)  /* 使用中 */
#define E_ABORT ERCD(-66, 0) /* 処理の中断 */
#define E_RONLY ERCD(-67, 0) /* 書き込み禁止 */

/* ------------------------------------------------------------------ */
/* 周辺核 (BTRON3) のエラーコード                                      */
/*                                                                     */
/* 上と**別の族**である（規律 R20）。番号空間が重なっているので混ぜない。 */
/* 値は B-right/V の errcode.h より。仕様から抽出した事実なので置いてよい。 */
/*                                                                     */
/* **BTRON3 自身が族の違いを認めている。** `ER_INNER` の説明が             */
/* 「中心核 (ITRON) システムエラー」であり、中心核のエラーを周辺核の        */
/* 族へ畳む口がちゃんと用意されている。層をまたぐときはこれを使う。        */
/* ------------------------------------------------------------------ */

#define ER_OK 0

#define ER_ADR ERCD(-1, 0)    /* 不正アドレス */
#define ER_PAR ERCD(-2, 0)    /* 不正パラメータ */
#define ER_NOSPT ERCD(-3, 0)  /* 未サポート機能 */
#define ER_NOSPC ERCD(-4, 0)  /* システムメモリ領域不足 */
#define ER_ID ERCD(-5, 0)     /* 不正 ID */
#define ER_EXS ERCD(-6, 0)    /* 既存在オブジェクト */
#define ER_NOEXS ERCD(-7, 0)  /* 未存在オブジェクト */
#define ER_NOMEM ERCD(-8, 0)  /* メモリ領域不足 */
#define ER_CTX ERCD(-9, 0)    /* コンテキストエラー */
#define ER_SYS ERCD(-10, 0)   /* システムエラー（未定義のエラー） */
#define ER_UNF ERCD(-11, 0)   /* 未定義システムコール */
#define ER_SZOVR ERCD(-12, 0) /* システムの制限超過（長さ） */
#define ER_MPTR ERCD(-13, 0)  /* 不正共有メモリポインタ */
#define ER_AKEY ERCD(-14, 0)  /* 不正共有メモリアクセスキー */
#define ER_PPRI ERCD(-17, 0)  /* 不正プロセス優先度 */
#define ER_NOPRC ERCD(-18, 0) /* 未存在プロセス */
#define ER_SELF ERCD(-19, 0)  /* 自プロセス指定 */
#define ER_LEVEL ERCD(-20, 0) /* 高いレベルのプロセス指定 */
#define ER_MINTR ERCD(-21, 0) /* メッセージハンドラでの待ち中断 */
#define ER_DLT ERCD(-22, 0)   /* 待ち中にオブジェクトが削除された */
#define ER_OBJ ERCD(-23, 0)   /* オブジェクトに対する不正操作 */
#define ER_NONE ERCD(-24, 0)  /* 得られなかった（タイムアウト・ポーリング失敗） */
#define ER_INIT ERCD(-25, 0)  /* 初期化ハンドラ異常終了 */
#define ER_FNAME ERCD(-26, 0) /* 不正パス名 */
#define ER_FD ERCD(-27, 0)    /* 不正ファイルディスクリプタ */
#define ER_ACCES ERCD(-28, 0) /* アクセス権不可 */
#define ER_PERM ERCD(-29, 0)  /* 削除不可 */
#define ER_RONLY ERCD(-30, 0) /* 書き込み保護 */
#define ER_PWD ERCD(-31, 0)   /* 不正ファイル合言葉 */
#define ER_ENDR ERCD(-32, 0)  /* 現在レコードは終端レコード */
#define ER_REC ERCD(-33, 0)   /* 不正レコードタイプ */
#define ER_NOLNK ERCD(-34, 0) /* リンクファイルでない */
#define ER_LOCK ERCD(-35, 0)  /* ロックされたレコード */
#define ER_LIMIT ERCD(-36, 0) /* システムの制限超過（個数） */
#define ER_XFS ERCD(-37, 0)   /* 異なるファイルシステム */
#define ER_NOFS ERCD(-38, 0)  /* ファイルシステム未接続 */
#define ER_NODSK ERCD(-39, 0) /* ディスクの領域不足 */
#define ER_TRON ERCD(-40, 0)  /* 標準フロッピーディスク形式でない */
#define ER_DD ERCD(-41, 0)    /* デバイス・ディスクリプタは存在 */
#define ER_DEV ERCD(-42, 0)   /* 指定した操作は禁止 */
#define ER_BUSY ERCD(-43, 0)  /* ビジー状態 */
#define ER_NODEV ERCD(-44, 0) /* デバイスへのアクセス不可 */
#define ER_ERDEV ERCD(-45, 0) /* 装置異常 */
#define ER_NOMDA ERCD(-46, 0) /* メディアが存在しない */
#define ER_IO ERCD(-47, 0)    /* 入出力エラー */

/* 中心核 (ITRON / μT-Kernel) のエラーを周辺核の族へ畳む口。
 * 規律 R20 の「層をまたぐときは呼び出し側が翻訳する」の実体。
 * 詳細エラーコードに中心核側のメインエラーコードを入れて渡す。 */
#define ER_INNER ERCD(-50, 0)
#define ER_INNER_OF(e) ERCD(-50, MERCD(e))

/* 詳細エラーコード (ErrCode.c.detail) */
#define ED_NONE 0
#define ED_CMD 0x8000
#define ED_DEVID 0x8001
#define ED_DATANO 0x8002
#define ED_DATACNT 0x8003
#define ED_DATA 0x8004

/* ------------------------------------------------------------------ */
/* 本OS の独自拡張 (A15)                                               */
/*                                                                     */
/* 仕様の使用済み範囲の近傍は将来の仕様拡張のために空け、-1024 以降を使う。 */
/* 規律 R6: 予約済み範囲 (-51〜-63 / -83〜-120 / -286〜-319) を使わない。  */
/* ------------------------------------------------------------------ */

#define E_BTRON_BASE (-1024)

#endif /* __BTRON_ERRCODE_H__ */

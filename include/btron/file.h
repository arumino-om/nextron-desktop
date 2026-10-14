/*
 * file.h — 周辺核 §1.6「ファイル管理」
 *
 * ------------------------------------------------------------------------
 * ディレクトリは無い。実身 / 仮身である
 * ------------------------------------------------------------------------
 *
 * 仕様 §1.6.1:
 *
 *   「ファイルシステムは、実身 / 仮身モデルを基本とした構造となっており…
 *     ファイルに含まれるリンク ( 仮身 ) による任意のネットワーク状の
 *     参照関係 ( 従来のファイルシステムにおけるディレクトリは存在しない )」
 *
 * **ここが BTRON の根幹**で、他の OS のファイル管理と最も違うところ。
 *
 *   - ファイル（実身）は「可変長レコードの順序列」であって、
 *     バイト列ではない
 *   - リンク（仮身）は**ファイルの中の 1 レコード**として置かれる。
 *     だから参照関係は木ではなく網になる
 *   - ファイル名は一意ではない。「1 つの検索キー」でしかない
 *     （同じ名前のファイルがいくつあってもよい）
 *   - 親ディレクトリという概念が無いので、パス名は「たどるリンクの列」
 *     であり、同名のときは**出現順**で区別する（`名前：2` のように書く）
 *
 * ------------------------------------------------------------------------
 * 参照カウントが削除を支配する
 * ------------------------------------------------------------------------
 *
 * 仕様 §1.6.5:「ファイルの削除は参照カウント 0 のファイルに対してのみ
 * 可能である」。参照カウントは**同一ファイルシステム内の固定リンクの数**
 * で、リンクファイル経由の参照は数えない。ルートファイルは最初から 1 で、
 * 決して削除できない。
 *
 * ------------------------------------------------------------------------
 * 本OS の決めごと
 * ------------------------------------------------------------------------
 *
 * 【A2】`LINK.f_id` を UH から UW に広げている（typedef.h）。
 * 仕様はリンクレコードの大きさを「LINK 構造体のサイズ (52 バイト)」と
 * 定義しているので、本OS ではその値も `sizeof(LINK)` に従って変わる。
 * `L_LNKSZ` を使うこと。
 *
 * 【規律 R1】この構造体群はメモリ表現である。**ディスク表現とは別物**で、
 * 直接 I/O してはならない。本OS のディスク形式は src/kernel/fs/ に別に
 * 定義してある。交換用フロッピーの標準形式 (詳細 C1) はさらに別で、
 * そちらは T3-01 として分けてある。
 */

#ifndef __BTRON_FILE_H__
#define __BTRON_FILE_H__

#include <btron/device.h> /* L_DEVNM（論理デバイス名）。§1.8 と同じもの */
#include <btron/errcode.h>
#include <btron/typedef.h>

/* ------------------------------------------------------------------ */
/* 各種名前の長さ（文字数）§1.6.7                                     */
/* ------------------------------------------------------------------ */

/* L_FSNM (20) は typedef.h で LINK と一緒に定義している。 */
#define L_DLNM 20      /* デバイス所在名 */
#define L_CONNM 8      /* 接続名 */
#define L_USRNM (12 + 2) /* ユーザ(グループ)名 + 隠し名 */
#define L_FNM 20       /* ファイル名 */
#define L_PATHNM 256   /* パス名 */

#define N_GRP 4 /* 所属グループ数 */

/* リンクレコードの大きさ。仕様は 52 だが、A2 で f_id を広げたので
 * 構造体から導く（仕様の 52 も LINK 構造体のサイズに由来する）。 */
#define L_LNKSZ ((W)sizeof(LINK))

/* ------------------------------------------------------------------ */
/* パス名の特殊記号 §1.6.2                                             */
/*                                                                     */
/* **この 3 つはファイル名に使えない。** それ以外は空白を含めて          */
/* 表示可能なすべての文字が使える（仕様）。                             */
/* ------------------------------------------------------------------ */

#define TC_FDLM 0xff21 /* '／' パス名の区切り */
#define TC_FSEP 0xff22 /* '：' 出現順の区切り */
#define TC_FOWN 0xff23 /* '≡' 自分自身（作業ファイル） */

/* ------------------------------------------------------------------ */
/* モード指定 §1.6.7                                                   */
/*                                                                     */
/* 【規律 R28 と同じ注意】**値は呼び出しごとに意味が違う。**             */
/* 例えば 0x0002 は get_lnk では F_DIRECT、opn_fil では F_WRITE、       */
/* cre_fil では F_FILEID である。どの関数に渡すかで族が決まる。         */
/* ------------------------------------------------------------------ */

/* get_lnk */
#define F_NORM 0x0000   /* 通常指定 */
#define F_BASED 0x0001  /* ベース指定 */
#define F_DIRECT 0x0002 /* 直接取り出し指定 */

/* opn_fil / map_rec / chk_fil のアクセス指定。
 * **3 つの関数で同じ値を使う**（仕様がそう定めている）。 */
#define F_READ 0x0004   /* 読み込み */
#define F_WRITE 0x0002  /* 書き込み */
#define F_EXCUTE 0x0001 /* 実行 / リンクの検索 */
#define F_UPDATE 0x0006 /* 更新（読込 | 書込） */

/* opn_fil の共有指定 */
#define F_EXCL 0x0100  /* 排他モード */
#define F_WEXCL 0x0200 /* 排他書き込みモード */

/* cre_fil / cre_lnk */
#define F_FLOAT 0x0000  /* 固定リンクとしない */
#define F_FIX 0x0001    /* 固定リンクとする */
#define F_FILEID 0x0002 /* ファイル ID 指定 */

/* fnd_rec / fnd_lnk の方向 */
#define F_FWD 0x0000    /* 前向きサーチ */
#define F_NFWD 0x0001   /* 前向きサーチ（次から） */
#define F_BWD 0x0002    /* 後向きサーチ */
#define F_NBWD 0x0003   /* 後向きサーチ（手前から） */
#define F_TOPEND 0x0004 /* 先頭から終端 */
#define F_ENDTOP 0x0005 /* 終端から先頭 */
#define F_DIRMASK 0x0007

/* fnd_lnk の一致条件 */
#define F_SFILE 0x4000 /* 同一ファイル */
#define F_SNAME 0x2000 /* 同一ファイル名 */
#define F_SATR1 0x1000 /* 同一属性データ1 */
#define F_SATR2 0x0800
#define F_SATR3 0x0400
#define F_SATR4 0x0200
#define F_SATR5 0x0100
#define F_SMASK 0x7F00

/* loc_rec */
#define F_UNLOCK 0x0000 /* アンロック */
#define F_LOCK 0x0001   /* ロック */
#define F_TSLOCK 0x0002 /* テスト & ロック */
#define F_CKLOCK 0x0003 /* ロック状態のチェック */

/* map_rec の写像先 */
#define F_COMMON 0x0100 /* 共有メモリ空間にマップ */
#define F_SYSTEM 0x0300 /* システムメモリ空間にマップ */

/* chk_fil */
#define F_EXIST 0x0000 /* ファイルの存在チェック */
#define F_PASWD 0x0008 /* パスワードの有無 */

/* chg_fat */
#define F_SETRONLY 0x0001 /* 書込不可属性のセット */
#define F_RSTRONLY 0x0002
#define F_SETPERM 0x0003 /* 削除不可属性のセット */
#define F_RSTPERM 0x0004
#define F_SETA1 0x0005 /* アプリ属性1のセット */
#define F_RSTA1 0x0006
#define F_SETA2 0x0007
#define F_RSTA2 0x0008

/* att_fls / chg_fsm */
#define FS_SYNC 0x0000  /* 同期指定 */
#define FS_ASYN 0x0002  /* 非同期指定 */
#define FS_RONLY 0x0001 /* 書き込み禁止 */

/* lst_fls */
#define F_GETDEV (-1) /* 対応するデバイス名を得る */
#define F_GETNAM (-2) /* 対応する接続名を得る */

/* syn_lnk の戻り値 */
#define F_SYNC 0  /* 一致している */
#define F_DNAME 1 /* ファイル名が異なっていた */
#define F_DDATE 2 /* 生成日時が異なっていた */
#define F_DBOTH 3 /* ファイル名と生成日時が異なる */

/* ------------------------------------------------------------------ */
/* レコード §1.6.3                                                     */
/* ------------------------------------------------------------------ */

/* レコードタイプは 0〜31。**0 はリンクレコードで特別扱い**（仕様）。 */
#define F_RECTYPE_MIN 0
#define F_RECTYPE_MAX 31
#define F_LINKREC 0 /* リンクレコード */

/* fnd_rec の typemask はタイプ番号のビット位置で指定する。 */
#define F_TYPEMASK(t) (1U << (t))
#define F_TYPEALL 0xFFFFFFFFU

/* ------------------------------------------------------------------ */
/* ファイルタイプ / アクセス属性 (f_type) §1.6.7                       */
/* ------------------------------------------------------------------ */

#define F_FILE 0x1000 /* 通常ファイル */

#define F_APLATR1 0x0040 /* アプリケーション属性1 */
#define F_APLATR2 0x0080 /* アプリケーション属性2 */
#define F_RONLY 0x0010   /* 書込不可属性 */
#define F_PERM 0x0020    /* 削除不可属性 */

#define F_OWNACC 0x0007 /* 所有者アクセス属性 (RWE) */
#define F_OWNACR 0x0004 /* 所有者の R アクセス属性 */
#define F_OWNACW 0x0002 /* 所有者の W アクセス属性 */
#define F_OWNACE 0x0001 /* 所有者の E アクセス属性 */

#define F_NOCHG 0x8000 /* 変更しない */

/* ------------------------------------------------------------------ */
/* 使用言語 §1.6.7                                                     */
/* ------------------------------------------------------------------ */

#define F_JPN 0x0021 /* 日本語（日本語文字族） */
#define F_ENG 0x0080 /* 英語（ラテン文字族1） */

/* ------------------------------------------------------------------ */
/* アクセス管理のレベル §1.6.4                                         */
/*                                                                     */
/* ファイルシステム単位の設定で、生成時に決まる。                       */
/*   0  一切行わない（取り外し媒体向け）                                */
/*   1  部分的（隠し名は常に 0）                                        */
/*   2  完全                                                           */
/* レベル 0 のファイルシステムでは**誰でも所有者としての権利を持つ**。   */
/* ------------------------------------------------------------------ */

#define F_LEVEL0 0
#define F_LEVEL1 1
#define F_LEVEL2 2

/* ------------------------------------------------------------------ */
/* 構造体 §1.6.7                                                       */
/* ------------------------------------------------------------------ */

/* ファイルアクセスモード。 */
typedef struct {
	UH f_ownacc; /* 所有者アクセスモード (RWE) */
	UH f_grpacc; /* グループアクセスレベル */
	UH f_pubacc; /* 一般アクセスレベル */
	H f_grpno;   /* グループ番号 (0 = グループなし, 1〜4) */
} A_MODE;

/* デフォルトファイルアクセスモード（ユーザ毎）。 */
typedef struct {
	UH f_ownacc;
	UH f_grpacc;
	UH f_pubacc;
	H f_grpno;
	UH f_gacc[N_GRP]; /* グループアクセスレベル */
} DA_MODE;

/* ファイル日時。 */
typedef struct {
	STIME f_ltime; /* ファイルの保存期限 */
	STIME f_atime; /* 最新のアクセス日時 */
	STIME f_mtime; /* 最新の更新日時 */
} F_TIME;

/* ファイル管理情報。 */
typedef struct {
	UH f_type;             /* ファイルタイプ / 所有者アクセスモード */
	UH f_atype;            /* アプリケーションタイプ */
	TC f_owner[L_USRNM];   /* ファイル所有者名（隠し名は常に 0） */
	TC f_group[L_USRNM];   /* 所有グループ名 */
	UH f_grpacc;           /* グループアクセスレベル */
	UH f_pubacc;           /* 一般アクセスレベル */
	H f_nlink;             /* 含まれているリンク数 */
	H f_index;             /* インデックスレベル */
	W f_size;              /* ファイルの総バイト数 */
	W f_nblk;              /* 総使用論理ブロック数 */
	W f_nrec;              /* 総レコード数 */
	STIME f_ltime;         /* 保存期限 */
	STIME f_atime;         /* 最新のアクセス日時 */
	STIME f_mtime;         /* 最新の更新日時 */
	STIME f_ctime;         /* 作成日時 */
} F_STATE;

/* ファイル所在情報。 */
typedef struct {
	STIME fs_ctime;       /* ファイルシステムの生成日時 */
	TC fs_name[L_FSNM];   /* ファイルシステム名 */
	TC fs_locat[L_DLNM];  /* デバイス所在名 */
	TC fs_dev[L_DEVNM];   /* 論理デバイス名 */
} F_LOCATE;

/* リンク情報（リンクファイルの中身）。 */
typedef struct {
	STIME f_ctime;       /* リンクファイル自体の生成日時 */
	UH f_atype;          /* 参照ファイルのアプリタイプ */
	TC f_name[L_FNM];    /* 参照ファイル名 */
	UW f_id;             /* 参照ファイル ID (A2: UH -> UW) */
	STIME rf_ctime;      /* 参照ファイルの生成日時 */
	TC fs_name[L_FSNM];  /* ファイルシステム名 */
	TC fs_locat[L_DLNM]; /* デバイス所在名 */
} F_LINK;

/* ファイルシステム管理情報。 */
typedef struct {
	H fs_bsize;          /* 論理ブロックのバイト数 */
	UW fs_nfile;         /* 最大ファイル数 (A2: UH -> UW) */
	H fs_lang;           /* 使用言語 */
	H fs_level;          /* アクセス管理レベル */
	W fs_nblk;           /* 全体のブロック数 */
	W fs_nfree;          /* 未使用ブロックの総数 */
	STIME fs_mtime;      /* 最新のシステムブロックの更新日時 */
	STIME fs_ctime;      /* 生成日時 */
	TC fs_name[L_FSNM];  /* ファイルシステム名 */
	TC fs_locat[L_DLNM]; /* デバイス所在名 */
} FS_STATE;

/* ファイルシステム接続情報。 */
typedef struct {
	TC a_name[L_CONNM]; /* 接続名 */
	TC dev[L_DEVNM];    /* 論理デバイス名 */
} F_ATTACH;

/* ------------------------------------------------------------------ */
/* 実装の制限値 (D9。暫定)                                             */
/* ------------------------------------------------------------------ */

#define FS_MAX_ATTACH 8  /* 同時に接続できるファイルシステム数 */
#define FS_MAX_OPEN 32   /* システム全体で同時に開けるファイル数 */
#define FD_MAX 16        /* 1 プロセスが開けるファイル数 */
#define FS_MAX_PATHDEP 32 /* パス名でたどれるリンクの深さ */

/* ------------------------------------------------------------------ */
/* システムコール §1.6.8                                               */
/* ------------------------------------------------------------------ */

/* --- リンクと作業ファイル --- */
IMPORT WERR get_lnk(CONST TC *path, LINK *lnk, W mode);
IMPORT ERR chg_wrk(CONST LINK *lnk);

/* --- 生成と削除 --- */
IMPORT WERR cre_fil(LINK *lnk, CONST TC *name, CONST A_MODE *mode, UH atype,
                    W opt);
IMPORT ERR cre_lnk(LINK *lnk, CONST F_LINK *ref, W opt);
IMPORT WERR gen_fil(LINK *lnk, CONST TC *name, CONST F_STATE *stat,
                    CONST F_LINK *ref, W opt);
IMPORT WERR del_fil(CONST LINK *org, CONST LINK *lnk, W force);

/* --- オープンとクローズ --- */
IMPORT WERR opn_fil(CONST LINK *lnk, W o_mode, CONST TC *pwd);
IMPORT ERR cls_fil(W fd);

/* --- 現在レコードの移動と検索 --- */
IMPORT ERR see_rec(W fd, W offset, W mode, W *recnum);
IMPORT WERR fnd_rec(W fd, W mode, UW typemask, UH subtype, W *recnum);
IMPORT ERR fnd_lnk(W fd, W mode, LINK *lnk, UH subtype, W *recnum);

/* --- レコードの入出力 --- */
IMPORT WERR rea_rec(W fd, W offset, B *buf, W size, W *r_size, UH *subtype);
IMPORT ERR wri_rec(W fd, W offset, CONST B *buf, W size, W *r_size,
                   CONST UH *subtype, UW units);
IMPORT ERR ins_rec(W fd, CONST B *buf, W size, W type, UH subtype, UW units);
IMPORT ERR apd_rec(W fd, CONST B *buf, W size, W type, UH subtype, UW units);
IMPORT WERR del_rec(W fd);
IMPORT ERR trc_rec(W fd, W size);
IMPORT ERR xch_fil(W fd_1, W fd_2);
IMPORT ERR loc_rec(W fd, W mode);

/* --- レコードの写像 --- */
IMPORT WERR map_rec(W fd, W offset, B **addr, W size, W mode);
IMPORT ERR ump_rec(W fd, W mapid);

/* --- 状態の取得と変更 --- */
IMPORT WERR chk_fil(CONST LINK *lnk, W mode, CONST TC *pwd);
IMPORT ERR chg_fmd(CONST LINK *lnk, CONST A_MODE *mode);
IMPORT ERR chg_fat(CONST LINK *lnk, W attr);
IMPORT ERR chg_fpw(W fd, CONST TC *pwd);
IMPORT ERR chg_fnm(CONST LINK *lnk, CONST TC *name);
IMPORT ERR chg_ftm(CONST LINK *lnk, CONST F_TIME *times);
IMPORT WERR fil_sts(CONST LINK *lnk, TC *name, F_STATE *stat, F_LOCATE *locat);
IMPORT WERR ofl_sts(W fd, TC *name, F_STATE *stat, F_LOCATE *locat);
IMPORT WERR lnk_sts(CONST LINK *lnk, F_LINK *stat);
IMPORT WERR syn_lnk(LINK *lnk, W opt);
IMPORT WERR get_nlk(LINK *lnk);

/* --- デフォルトアクセスモード --- */
IMPORT ERR get_dfm(DA_MODE *mode);
IMPORT ERR set_dfm(CONST DA_MODE *mode);

/* --- ファイルシステムの接続 --- */
IMPORT ERR att_fls(CONST TC *dev, CONST TC *name, LINK *lnk, UW mode);
IMPORT ERR det_fls(CONST TC *dev, W eject);
IMPORT ERR syn_fls(void);
IMPORT WERR fls_sts(CONST TC *dev, FS_STATE *buff);
IMPORT ERR chg_fls(CONST TC *dev, CONST TC *fs_name, CONST TC *fs_locate);
IMPORT WERR lst_fls(F_ATTACH *buff, W cnt);
IMPORT WERR chg_fsm(CONST TC *dev, UW mode);

/* ------------------------------------------------------------------ */
/* 以下は BTRON3 の API ではない。本OS 内部の口                         */
/* ------------------------------------------------------------------ */

IMPORT ERR fil_init(void);

/* プロセス終了時に、開いたファイルをすべて閉じる（仕様 §1.6.5）。 */
IMPORT void fil_release_process(W pid);

/* 子プロセスへ引き継ぐ環境（作業ファイル・ユーザ情報）。 */
IMPORT void fil_inherit(W parent, W child);

/* ファイルシステムを作る。仕様は生成手段を規定していない
 * （「ファイルシステムの生成時には…」とあるだけ）ので本OS の口。
 * level はアクセス管理レベル (F_LEVEL0〜2)。 */
IMPORT ERR fil_format(CONST TC *dev, CONST TC *fs_name, CONST TC *fs_locat,
                      W level, W lang);

IMPORT W fil_open_count(void); /* 検証用 */

#endif /* __BTRON_FILE_H__ */

# 定数一覧（仕様書からの抽出）

`tools/spec/extract_api.py` が仕様文書から機械的に抽出したもの。**手で編集しない。**

1334 件（同名は最初の出現を採用）

| 定数 | 値 | 註 | 出典 |
|---|---|---|---|
| `ACT_ITEM` | `0x80` | 動作項目(上記項目にORで指定) | `btron3/os_spec/shell/panel.txt` |
| `AF_INET` | `2` | TCP/IP and related | `btron3/os_spec/shell/tcpip.txt` |
| `AF_UNSPEC` | `0` | unspecified address family | `btron3/os_spec/shell/tcpip.txt` |
| `ALLOW_MISALIGN` | `1` | ミスアライメントアクセス可能 | `brightv/library/standard/basic.txt` |
| `AS_PARTS` | `5` | テキストオルタネートスイッチ | `btron3/os_spec/shell/parts.txt` |
| `ATR_TEXT` | `0x20` | 属性付き文字列(上記項目にORで指定) | `btron3/os_spec/shell/panel.txt` |
| `BASE_SIZE_IMAGESEG` | `(sizeof(IMAGESEG)-sizeof(UW))` | bounds まで | `brightv/library/standard/tad.txt` |
| `BASE_SIZE_IMAGESEG_EXTEND` | `(sizeof(UH)*2)` | len まで | `brightv/library/standard/tad.txt` |
| `BIGENDIAN` | `0` | リトルエンディアン | `brightv/library/standard/basic.txt` |
| `BLACK100` | `((PAT*)_BLACK100)` |  | `btron3/os_spec/dp/basic_concept.txt` |
| `BLACK12` | `((PAT*)_BLACK12)` |  | `btron3/os_spec/dp/basic_concept.txt` |
| `BLACK25` | `((PAT*)_BLACK25)` |  | `btron3/os_spec/dp/basic_concept.txt` |
| `BLACK50` | `((PAT*)_BLACK50)` |  | `btron3/os_spec/dp/basic_concept.txt` |
| `BLACK75` | `((PAT*)_BLACK75)` |  | `btron3/os_spec/dp/basic_concept.txt` |
| `BLACK87` | `((PAT*)_BLACK87)` |  | `btron3/os_spec/dp/basic_concept.txt` |
| `BLT_TEXT` | `0x40` |  | `btron3/os_spec/shell/panel.txt` |
| `BMAP_DATA` | `4` | 図形ビットマップデータ | `btron3/os_spec/shell/data.txt` |
| `BMAP_ITEM` | `4` | ビットマップ | `btron3/os_spec/shell/panel.txt` |
| `BREAK_CODE` | `0x1140` | [命令]＋[削除] | `brightv/library/standard/keycode.txt` |
| `BTRON_SPEC` | `( SPEC_BTRON \| 0x302 )` | BTRON Ver 3.02 | `brightv/library/standard/basic.txt` |
| `CANONICAL` | `3` | 1 行入力 (CR を LF に変換) | `brightv/device/console.txt` |
| `CA_16BIT` | `0x0800` | 16 ビットアクセス指定 | `brightv/device/cardmgr.txt` |
| `CA_ATTRMEM` | `0x4000` | 属性メモリ指定 | `brightv/device/cardmgr.txt` |
| `CA_IOCS16` | `0x0400` | IOCS16 ソース | `brightv/device/cardmgr.txt` |
| `CA_IOMAP` | `0x8000` | IO 空間のマップ | `brightv/device/cardmgr.txt` |
| `CA_IONOCHK` | `0x10000` | IO 空間のリソース管理なし | `brightv/device/cardmgr.txt` |
| `CA_SPEED` | `0x00FF` | 50 ns 単位のアクセス速度指定 | `brightv/device/cardmgr.txt` |
| `CA_WAIT0` | `0x0100` | 追加 WAIT 0 指定 | `brightv/device/cardmgr.txt` |
| `CA_WAIT1` | `0x0101` | 追加 WAIT 1 指定 | `brightv/device/cardmgr.txt` |
| `CA_WAIT2` | `0x0102` | 追加 WAIT 2 指定 | `brightv/device/cardmgr.txt` |
| `CA_WAIT3` | `(CA_WAIT1 \| CA_WAIT2)` | 追加 WAIT 3 指定 | `brightv/device/cardmgr.txt` |
| `CA_WPROTECT` | `0x2000` | 書き込み禁止 | `brightv/device/cardmgr.txt` |
| `CA_ZWAIT` | `0x0104` | ゼロ WAIT 指定 | `brightv/device/cardmgr.txt` |
| `CE_BATTERY` | `3` | カードバッテリ状態変化 | `brightv/device/cardmgr.txt` |
| `CE_EJECT` | `2` | カード脱着 | `brightv/device/cardmgr.txt` |
| `CE_INSERT` | `1` | カード挿着 | `brightv/device/cardmgr.txt` |
| `CHECK` | `0x0002` | メッセージチェック | `btron3/os_spec/kernel/message.txt` |
| `CK_AIMS` | `7` | AIMS | `brightv/device/cardmgr.txt` |
| `CK_ANY` | `255` | 任意(不正) | `brightv/device/cardmgr.txt` |
| `CK_FIXED` | `4` | 固定ディスク | `brightv/device/cardmgr.txt` |
| `CK_MEMORY` | `1` | メモリ | `brightv/device/cardmgr.txt` |
| `CK_MULTI` | `0` | マルチファンクション | `brightv/device/cardmgr.txt` |
| `CK_NETWORK` | `6` | ネットワーク | `brightv/device/cardmgr.txt` |
| `CK_NONE` | `(-1)` | なし(登録削除) | `brightv/device/cardmgr.txt` |
| `CK_PARALLEL` | `3` | パラレルポート | `brightv/device/cardmgr.txt` |
| `CK_SCSI` | `8` | SCSI | `brightv/device/cardmgr.txt` |
| `CK_SERIAL` | `2` | シリアルポート | `brightv/device/cardmgr.txt` |
| `CK_VIDEO` | `5` | ビデオ | `brightv/device/cardmgr.txt` |
| `CLR` | `0` | 表示をクリア | `btron3/os_spec/shell/window.txt` |
| `CONFM` | `0x0002` | 受信(確認)待ち | `btron3/os_spec/kernel/message.txt` |
| `CONF_BUFIO` | `(-2)` | バッファ I/O | `brightv/device/console.txt` |
| `CONF_SELF` | `(-1)` | セルフコンソール | `brightv/device/console.txt` |
| `CONF_SERIAL(n)` | `(n)` | シリアルポート # N | `brightv/device/console.txt` |
| `CONF_SERIAL_0` | `(0)` | シリアルポート # 0 | `brightv/device/console.txt` |
| `CONSOLE_PORT` | `1` | デバッグ用コンソール | `brightv/device/console.txt` |
| `CONST` | `const` | 変数・関数の代入禁止指定 | `btron3/shared_data/data_type.txt` |
| `CPU_I386` | `0x00000010` | i386 互換 | `brightv/library/standard/basic.txt` |
| `CPU_ID` | `CPU_I386` | i386 互換 CPU | `brightv/library/standard/basic.txt` |
| `CPU_TRON` | `0x00000001` | TRON 仕様 | `brightv/library/standard/basic.txt` |
| `CPU_V810` | `0x00001000` | V810 | `brightv/library/standard/basic.txt` |
| `CP_POFFREQ(tm)` | `(((tm) << 16) + 0x99)` | 指定時間 ( 秒 ) 後電源 OFF | `brightv/device/cardmgr.txt` |
| `CP_POWEROFF` | `0x00` | 電源 OFF | `brightv/device/cardmgr.txt` |
| `CP_POWERON` | `0x01` | 電源 ON | `brightv/device/cardmgr.txt` |
| `CP_RESUME` | `0x11` | レジューム ( 電源 ON ) | `brightv/device/cardmgr.txt` |
| `CP_SUSPEND` | `0x10` | サスペンド ( 電源 OFF ) | `brightv/device/cardmgr.txt` |
| `CP_SUSRIRES` | `0x12` | サスペンド ( RI レジューム ) | `brightv/device/cardmgr.txt` |
| `CR_NONE` | `0` | 対象カードでない | `brightv/device/cardmgr.txt` |
| `CR_OWN` | `1` | 対象カード：占有する | `brightv/device/cardmgr.txt` |
| `CR_SHARE` | `2` | 対象カード：共有する | `brightv/device/cardmgr.txt` |
| `CS_CREATE` | `0x11` | コンソールの生成 | `brightv/device/console.txt` |
| `CS_DELETE` | `0x12` | コンソールの削除 | `brightv/device/console.txt` |
| `CS_GETCONF` | `0x14` | コンソール構成の取り出し | `brightv/device/console.txt` |
| `CS_GETPORT` | `0x21` | 標準コンソールの取り出し | `brightv/device/console.txt` |
| `CS_SETCONF` | `0x13` | コンソール構成の設定 | `brightv/device/console.txt` |
| `CS_SETPORT` | `0x22` | 標準コンソールの設定 | `brightv/device/console.txt` |
| `CS_SRCHPORT` | `0x23` | コンソールポートのサーチ | `brightv/device/console.txt` |
| `DA_CHARDEV` | `0x8000` | 文字型デバイス | `btron3/os_spec/kernel/device.txt` |
| `DA_DEVINFO` | `0xFFFF0000` | デバイス/メディア依存情報 | `btron3/os_spec/kernel/device.txt` |
| `DA_DEVKIND` | `0xFF` | デバイス/メディア種別 | `btron3/os_spec/kernel/device.txt` |
| `DA_DEVTYPE` | `0xF0` | デバイス/メディアタイプマスク | `btron3/os_spec/kernel/device.txt` |
| `DA_REMOVABLE` | `0x4000` | 取り外し可能 | `btron3/os_spec/kernel/device.txt` |
| `DBL_DIG` | `( 15 )` |  | `brightv/library/standard/bmath.txt` |
| `DBL_EPSILON` | `( 2.2204460492503131e-16 )` |  | `brightv/library/standard/bmath.txt` |
| `DBL_MANT_DIG` | `( 53 )` |  | `brightv/library/standard/bmath.txt` |
| `DBL_MAX` | `( 1.7976931348623157e+308 )` |  | `brightv/library/standard/bmath.txt` |
| `DBL_MAX_10_EXP` | `( 308 )` | 倍精度実数の10に対する累乗最大値 | `brightv/library/standard/bmath.txt` |
| `DBL_MAX_EXP` | `( 1024 )` | 倍精度実数の2に対する累乗最大値 | `brightv/library/standard/bmath.txt` |
| `DBL_MIN` | `( 2.2250738585072014e-308 )` |  | `brightv/library/standard/bmath.txt` |
| `DBL_MIN_10_EXP` | `( -307 )` | 倍精度実数の10に対する累乗最小値 | `brightv/library/standard/bmath.txt` |
| `DBL_MIN_EXP` | `( -1021 )` | 倍精度実数の2に対する累乗最小値 | `brightv/library/standard/bmath.txt` |
| `DB_MENU` | `0xa000` | メニューデータ番号 | `brightv/library/appl/libuse.txt` |
| `DB_MISC` | `0xf000` | その他のデータ番号 | `brightv/library/appl/libuse.txt` |
| `DB_PSTR` | `0x9000` | パネル文字バッファデータ番号 | `brightv/library/appl/libuse.txt` |
| `DB_SMSG` | `0xb000` | システムメッセージデータ番号 | `brightv/library/appl/libuse.txt` |
| `DB_SPNL` | `0x8000` | 単純パネルデータ番号 | `brightv/library/appl/libuse.txt` |
| `DELEXIT` | `0x8000` | 終了時削除指定 | `btron3/shared_data/data_type.txt` |
| `DFUSENSEG_STRUCT` | `"hhhhhwwwh3h16hw"` |  | `brightv/library/standard/tad.txt` |
| `DISPREQ` | `0x11` |  | `btron3/os_spec/shell/omgr.txt` |
| `DK_DISK` | `0x10` | ディスクタイプ | `brightv/device/common.txt` |
| `DK_DISK_CDROM` | `0x16` | CD-ROM | `brightv/device/common.txt` |
| `DK_DISK_FD` | `0x14` | フロッピーディスク | `brightv/device/common.txt` |
| `DK_DISK_FLA` | `0x13` | FLASH ROM / SS ディスク | `brightv/device/common.txt` |
| `DK_DISK_HD` | `0x15` | ハードディスク | `brightv/device/common.txt` |
| `DK_DISK_RAM` | `0x11` | RAM ディスク | `brightv/device/common.txt` |
| `DK_DISK_ROM` | `0x12` | ROM ディスク | `brightv/device/common.txt` |
| `DK_DISK_UNDEF` | `0x10` | その他のディスク | `brightv/device/common.txt` |
| `DK_UNDEF` | `0x00` | 未定義/不明 | `brightv/device/common.txt` |
| `DN_BEEPMELODY(no)` | `( DN_BEEPMELODY_S + 1 + (no) )` |  | `brightv/device/beep.txt` |
| `DN_KEYDEF(kid)` | `(DN_KEYDEF_S - (kid))` |  | `brightv/device/kbpd.txt` |
| `DN_KEYDEF2(kid)` | `(DN_KEYDEF2_S - (kid))` |  | `brightv/device/kbpd.txt` |
| `DRGEVT` | `0x0030` | パーツ用  (相対座標) | `btron3/os_spec/shell/window.txt` |
| `DTYPE_LEN` | `32` | データタイプ名のバイト長 | `btron3/os_spec/shell/omgr.txt` |
| `DT_DIR` | `4` | ディレクトリ | `brightv/ext/unixemu.txt` |
| `DT_REG` | `8` | 普通のファイル | `brightv/ext/unixemu.txt` |
| `DT_UNKNOWN` | `0` | 不明 | `brightv/ext/unixemu.txt` |
| `D_ABORT_PTN` | `( D_CALPTN(DC_ABORT)    \` |  | `brightv/device/common.txt` |
| `D_CALPTN(devcmd)` | `( 1 << (devcmd) )` |  | `brightv/device/common.txt` |
| `D_CHECK` | `0x0004` | サスペンド禁止カウントを調べる | `btron3/os_spec/kernel/device.txt` |
| `D_DISSUS` | `0x0002` | サスペンドを禁止する | `btron3/os_spec/kernel/device.txt` |
| `D_ENASUS` | `0x0003` | サスペンドを許可する | `btron3/os_spec/kernel/device.txt` |
| `D_EXCL` | `0x0100` | 排他モード指定 | `btron3/os_spec/kernel/device.txt` |
| `D_FORCE` | `0x8000` | 強制サスペンド指定 | `btron3/os_spec/kernel/device.txt` |
| `D_NORM_PTN` | `( D_CALPTN(DC_OPEN)     \` |  | `brightv/device/common.txt` |
| `D_NOWAIT` | `0x8000` | 待ちなしモード指定 | `btron3/os_spec/kernel/device.txt` |
| `D_READ` | `0x0001` | 読み込み専用オープン | `btron3/os_spec/kernel/device.txt` |
| `D_SUSPEND` | `0x0001` | サスペンドする | `btron3/os_spec/kernel/device.txt` |
| `D_UNDEF` | `1.797693e308` | NaN | `btron3/os_spec/shell/parts.txt` |
| `D_UPDATE` | `0x0003` | 更新用オープン | `btron3/os_spec/kernel/device.txt` |
| `D_WEXCL` | `0x0200` | 排他書き込みモード指定 | `btron3/os_spec/kernel/device.txt` |
| `D_WRITE` | `0x0002` | 書き込み専用オープン | `btron3/os_spec/kernel/device.txt` |
| `ECHO` | `1` | エコーの有無 (0:無、1:有) | `brightv/device/console.txt` |
| `EDIT` | `5` | 1 行編集入力 | `brightv/device/console.txt` |
| `ED_CMD` | `(0x8000)` | 不正コマンド | `brightv/library/standard/errcode.txt` |
| `ED_DATA` | `(0x8004)` | 不正データ | `brightv/library/standard/errcode.txt` |
| `ED_DATACNT` | `(0x8003)` | 不正データ数 | `brightv/library/standard/errcode.txt` |
| `ED_DATANO` | `(0x8002)` | 不正データ番号 | `brightv/library/standard/errcode.txt` |
| `ED_DEVID` | `(0x8001)` | 不正デバイスID | `brightv/library/standard/errcode.txt` |
| `ED_NONE` | `(0)` | 詳細エラーコードなし | `brightv/library/standard/errcode.txt` |
| `ED_PR_ABORT` | `0x00` | アボート要求 | `brightv/device/printer.txt` |
| `ED_PR_BUSY` | `0x04` | ビジー状態(タイムアウト) | `brightv/device/printer.txt` |
| `ED_PR_OFFLINE` | `0x02` | オフライン | `brightv/device/printer.txt` |
| `ED_PR_PAPER` | `0x03` | 用紙切れ | `brightv/device/printer.txt` |
| `ED_PR_POWER` | `0x01` | 非接続、電源 OFF | `brightv/device/printer.txt` |
| `EM_ALL` | `0x7fff` |  | `btron3/os_spec/kernel/event.txt` |
| `EM_APPL1` | `0x0080` |  | `btron3/os_spec/kernel/event.txt` |
| `EM_APPL2` | `0x0100` |  | `btron3/os_spec/kernel/event.txt` |
| `EM_APPL3` | `0x0200` |  | `btron3/os_spec/kernel/event.txt` |
| `EM_APPL4` | `0x0400` |  | `btron3/os_spec/kernel/event.txt` |
| `EM_APPL5` | `0x0800` |  | `btron3/os_spec/kernel/event.txt` |
| `EM_APPL6` | `0x1000` |  | `btron3/os_spec/kernel/event.txt` |
| `EM_APPL7` | `0x2000` |  | `btron3/os_spec/kernel/event.txt` |
| `EM_APPL8` | `0x4000` |  | `btron3/os_spec/kernel/event.txt` |
| `EM_AUTKEY` | `0x0010` |  | `btron3/os_spec/kernel/event.txt` |
| `EM_BUTDWN` | `0x0001` |  | `btron3/os_spec/kernel/event.txt` |
| `EM_BUTUP` | `0x0002` |  | `btron3/os_spec/kernel/event.txt` |
| `EM_DEVICE` | `0x0020` |  | `btron3/os_spec/kernel/event.txt` |
| `EM_KEYDWN` | `0x0004` |  | `btron3/os_spec/kernel/event.txt` |
| `EM_KEYUP` | `0x0008` |  | `btron3/os_spec/kernel/event.txt` |
| `EM_NULL` | `0x0000` |  | `btron3/os_spec/kernel/event.txt` |
| `EM_RSV` | `0x0040` |  | `btron3/os_spec/kernel/event.txt` |
| `EPNL_WINDOW` | `(DB_SPNL + 1)` | エラーパネル | `brightv/library/appl/libuse.txt` |
| `EP_ABORT` | `(-8)` | 中断された | `brightv/device/primgdrv.txt` |
| `EP_ALL` | `0x0007` | pos,stat,time に現在値を設定 | `btron3/os_spec/kernel/event.txt` |
| `EP_CMND` | `(-1)` | コマンド形式が不正 | `brightv/device/primgdrv.txt` |
| `EP_NONE` | `0x0000` | time,pos,stat はそのまま | `btron3/os_spec/kernel/event.txt` |
| `EP_NOSPC` | `(-6)` | 資源が不足した | `brightv/device/primgdrv.txt` |
| `EP_NOSPT` | `(-3)` | 未サポート | `brightv/device/primgdrv.txt` |
| `EP_OK` | `(0)` | 正常 | `brightv/device/primgdrv.txt` |
| `EP_OUTBUF` | `(-7)` | 出力バッファが未設定 | `brightv/device/primgdrv.txt` |
| `EP_PAPER` | `(-4)` | 用紙サイズが不正 | `brightv/device/primgdrv.txt` |
| `EP_PAR` | `(-2)` | パラメータが不正 | `brightv/device/primgdrv.txt` |
| `EP_POS` | `0x0001` | pos に現在PD位置を設定 | `btron3/os_spec/kernel/event.txt` |
| `EP_SPEC` | `(-5)` | 印刷仕様が不正 | `brightv/device/primgdrv.txt` |
| `EP_STAT` | `0x0002` | stat に現在メタキー状態を設定 | `btron3/os_spec/kernel/event.txt` |
| `EP_TIME` | `0x0004` | time に現在時間を設定 | `btron3/os_spec/kernel/event.txt` |
| `ES_ALPH` | `0x00000004` | 英語ロックキー | `btron3/os_spec/kernel/event.txt` |
| `ES_BUT` | `0x00000001` | PDメインボタン | `btron3/os_spec/kernel/event.txt` |
| `ES_BUT2` | `0x00000002` | PDサブボタン | `btron3/os_spec/kernel/event.txt` |
| `ES_CMD` | `0x00000080` | 命令キー | `btron3/os_spec/kernel/event.txt` |
| `ES_EXT` | `0x00000040` | 拡張キー | `btron3/os_spec/kernel/event.txt` |
| `ES_HAN` | `0x00010000` | 半角キー | `btron3/os_spec/kernel/event.txt` |
| `ES_KANA` | `0x00000008` | カタカナロックキー | `btron3/os_spec/kernel/event.txt` |
| `ES_LCMD` | `0x00000800` | 命令簡易ロック | `btron3/os_spec/kernel/event.txt` |
| `ES_LEXT` | `0x00000400` | 拡張簡易ロック | `btron3/os_spec/kernel/event.txt` |
| `ES_LLSHFT` | `0x00000100` | 左シフト簡易ロック | `btron3/os_spec/kernel/event.txt` |
| `ES_LRSHFT` | `0x00000200` | 右シフト簡易ロック | `btron3/os_spec/kernel/event.txt` |
| `ES_LSHFT` | `0x00000010` | 左シフトキー | `btron3/os_spec/kernel/event.txt` |
| `ES_NODSP` | `0x00400000` | ポインタ非表示 | `btron3/os_spec/kernel/event.txt` |
| `ES_PDSIM` | `0x00800000` | PDシュミレーション | `btron3/os_spec/kernel/event.txt` |
| `ES_RSHFT` | `0x00000020` | 右シフトキー | `btron3/os_spec/kernel/event.txt` |
| `ES_TCMD` | `0x00008000` | 命令一時シフト | `btron3/os_spec/kernel/event.txt` |
| `ES_TEXT` | `0x00004000` | 拡張一時シフト | `btron3/os_spec/kernel/event.txt` |
| `ES_TLSHFT` | `0x00001000` | 左シフト一時シフト | `btron3/os_spec/kernel/event.txt` |
| `ES_TRSHFT` | `0x00002000` | 右シフト一時シフト | `btron3/os_spec/kernel/event.txt` |
| `EVENT_ANY` | `0x08` |  | `brightv/device/usbmgr.txt` |
| `EVENT_CLASS` | `0x01` |  | `brightv/device/usbmgr.txt` |
| `EVENT_PROTOCOL` | `0x04` |  | `brightv/device/usbmgr.txt` |
| `EVENT_SUBCLASS` | `0x02` |  | `brightv/device/usbmgr.txt` |
| `EV_APPL1` | `8` | アプリケーションイベント#1 | `btron3/os_spec/kernel/event.txt` |
| `EV_APPL2` | `9` | アプリケーションイベント#2 | `btron3/os_spec/kernel/event.txt` |
| `EV_APPL3` | `10` | アプリケーションイベント#3 | `btron3/os_spec/kernel/event.txt` |
| `EV_APPL4` | `11` | アプリケーションイベント#4 | `btron3/os_spec/kernel/event.txt` |
| `EV_APPL5` | `12` | アプリケーションイベント#5 | `btron3/os_spec/kernel/event.txt` |
| `EV_APPL6` | `13` | アプリケーションイベント#6 | `btron3/os_spec/kernel/event.txt` |
| `EV_APPL7` | `14` | アプリケーションイベント#7 | `btron3/os_spec/kernel/event.txt` |
| `EV_APPL8` | `15` | アプリケーションイベント#8 | `btron3/os_spec/kernel/event.txt` |
| `EV_AUTKEY` | `5` | 自動キーリピート | `btron3/os_spec/kernel/event.txt` |
| `EV_BUTDWN` | `1` | ボタンダウン | `btron3/os_spec/kernel/event.txt` |
| `EV_BUTUP` | `2` | ボタンアップ | `btron3/os_spec/kernel/event.txt` |
| `EV_DEVICE` | `6` | デバイスイベント | `btron3/os_spec/kernel/event.txt` |
| `EV_INACT` | `17` | 入力不可状態に切換わった | `btron3/os_spec/shell/window.txt` |
| `EV_KEYDWN` | `3` | キーダウン | `btron3/os_spec/kernel/event.txt` |
| `EV_KEYUP` | `4` | キーアップ | `btron3/os_spec/kernel/event.txt` |
| `EV_MENU` | `10` | メニューイベント | `btron3/os_spec/shell/window.txt` |
| `EV_MSG` | `18` | 他のメッセージが得られた | `btron3/os_spec/shell/window.txt` |
| `EV_NOMSG` | `19` | メッセージは得られなかった | `btron3/os_spec/shell/window.txt` |
| `EV_NULL` | `0` | ヌルイベント | `btron3/os_spec/kernel/event.txt` |
| `EV_REQUEST` | `9` | 要求/応答イベント | `btron3/os_spec/shell/window.txt` |
| `EV_RSV` | `7` | 予約 | `btron3/os_spec/kernel/event.txt` |
| `EV_RSWITCH` | `16` | 切換イベント(再表示要) | `btron3/os_spec/shell/window.txt` |
| `EV_SWITCH` | `8` | 切換イベント | `btron3/os_spec/shell/window.txt` |
| `EXECREQ` | `0x10` |  | `btron3/os_spec/shell/omgr.txt` |
| `EX_ADDRINUSE` | `((-417) << 16)` | アドレスは使用中 | `btron3/os_spec/shell/tcpip.txt` |
| `EX_ADDRNOTAVAIL` | `((-418) << 16)` | アドレスが利用できない | `btron3/os_spec/shell/tcpip.txt` |
| `EX_AFNOSUPPORT` | `((-416) << 16)` | サポートされていないアドレスファミリ | `btron3/os_spec/shell/tcpip.txt` |
| `EX_ALREADY` | `((-428) << 16)` | 既に処理中の動作 | `btron3/os_spec/shell/tcpip.txt` |
| `EX_BADF` | `((-405) << 16)` | ソケットが無効 | `btron3/os_spec/shell/tcpip.txt` |
| `EX_CONNABORTED` | `((-403) << 16)` | 接続がアボートされた | `btron3/os_spec/shell/tcpip.txt` |
| `EX_CONNREFUSED` | `((-426) << 16)` | 接続が拒絶された | `btron3/os_spec/shell/tcpip.txt` |
| `EX_CONNRESET` | `((-422) << 16)` | 接続がリセットされた | `btron3/os_spec/shell/tcpip.txt` |
| `EX_DESTADDRREQ` | `((-409) << 16)` | 送信先アドレス指定が必要 | `btron3/os_spec/shell/tcpip.txt` |
| `EX_HOSTDOWN` | `((-427) << 16)` | ホストがダウンしている | `btron3/os_spec/shell/tcpip.txt` |
| `EX_HOSTUNREACH` | `((-401) << 16)` | ホストが見つからない | `btron3/os_spec/shell/tcpip.txt` |
| `EX_INPROGRESS` | `((-429) << 16)` | 非ブロック化ソケットで接続中 | `btron3/os_spec/shell/tcpip.txt` |
| `EX_ISCONN` | `((-423) << 16)` | ソケットは接続済み | `btron3/os_spec/shell/tcpip.txt` |
| `EX_MSGSIZE` | `((-408) << 16)` | 送信メッセージの細分化ができない | `btron3/os_spec/shell/tcpip.txt` |
| `EX_NETDOWN` | `((-419) << 16)` | ネットワーク機能が無効 | `btron3/os_spec/shell/tcpip.txt` |
| `EX_NETRESET` | `((-421) << 16)` | ネットワークがリセットされた | `btron3/os_spec/shell/tcpip.txt` |
| `EX_NETUNREACH` | `((-420) << 16)` | ネットワークが見つからない | `btron3/os_spec/shell/tcpip.txt` |
| `EX_NOBUFS` | `((-404) << 16)` | TCPIP 内部領域不足 | `btron3/os_spec/shell/tcpip.txt` |
| `EX_NOPROTOOPT` | `((-411) << 16)` | 利用できないプロトコル指定 | `btron3/os_spec/shell/tcpip.txt` |
| `EX_NOTCONN` | `((-424) << 16)` | ソケットは接続されていない | `btron3/os_spec/shell/tcpip.txt` |
| `EX_OPNOTSUPP` | `((-414) << 16)` | サポートされていない動作指定 | `btron3/os_spec/shell/tcpip.txt` |
| `EX_PFNOSUPPORT` | `((-415) << 16)` | サポートされていないプロトコルファミリ | `btron3/os_spec/shell/tcpip.txt` |
| `EX_PROTONOSUPPORT` | `((-412) << 16)` | 認識されていないオプション指定 | `btron3/os_spec/shell/tcpip.txt` |
| `EX_PROTOTYPE` | `((-410) << 16)` | プロトコルが不正 | `btron3/os_spec/shell/tcpip.txt` |
| `EX_SHUTDOWN` | `((-425) << 16)` | 送信は禁止されている | `btron3/os_spec/shell/tcpip.txt` |
| `EX_SOCKTNOSUPPORT` | `((-413) << 16)` | サポートされていないソケットタイプ | `btron3/os_spec/shell/tcpip.txt` |
| `EX_TIMEDOUT` | `((-402) << 16)` | TCPIP タイムアウト | `btron3/os_spec/shell/tcpip.txt` |
| `EX_WOULDBLOCK` | `((-407) << 16)` | ブロックされる処理がある | `btron3/os_spec/shell/tcpip.txt` |
| `FD_CLR(n, p)` | `((p)->fds_bits[(n)/NFDBITS] &= ~(1 << ((n) % NFDBITS)))` |  | `brightv/diff/diff.txt` |
| `FD_ISSET(n, p)` | `((p)->fds_bits[(n)/NFDBITS] & (1 << ((n) % NFDBITS)))` |  | `brightv/diff/diff.txt` |
| `FD_SET(n, p)` | `((p)->fds_bits[(n)/NFDBITS] \|= (1 << ((n) % NFDBITS)))` |  | `brightv/diff/diff.txt` |
| `FD_SETSIZE` | `256` |  | `brightv/diff/diff.txt` |
| `FD_ZERO(p)` | `memset((void *)(p), 0, sizeof (*(p)))` |  | `brightv/diff/diff.txt` |
| `FIGSEG_STRUCT` | `"hhhhhhhhhhw"` |  | `brightv/library/standard/tad.txt` |
| `FIG_DATA` | `5` | 図形セグメントデータ | `btron3/os_spec/shell/data.txt` |
| `FIG_ITEM` | `5` |  | `btron3/os_spec/shell/panel.txt` |
| `FILL0` | `((B*)_FILL0)` |  | `btron3/os_spec/dp/basic_concept.txt` |
| `FILL100` | `((B*)_FILL100)` |  | `btron3/os_spec/dp/basic_concept.txt` |
| `FILL12` | `((B*)_FILL12)` |  | `btron3/os_spec/dp/basic_concept.txt` |
| `FILL25` | `((B*)_FILL25)` |  | `btron3/os_spec/dp/basic_concept.txt` |
| `FILL50` | `((B*)_FILL50)` |  | `btron3/os_spec/dp/basic_concept.txt` |
| `FILL75` | `((B*)_FILL75)` |  | `btron3/os_spec/dp/basic_concept.txt` |
| `FILL87` | `((B*)_FILL87)` |  | `btron3/os_spec/dp/basic_concept.txt` |
| `FIONBIO` | `(0x80000000 \| (4 << 16) \| ('f' << 8) \| 126)` |  | `brightv/diff/diff.txt` |
| `FIONREAD` | `(0x40000000 \| (4 << 16) \| ('f' << 8) \| 127)` |  | `brightv/diff/diff.txt` |
| `FLOWC` | `4` | フロー制御 (0: 無, IXON, etc) | `brightv/device/console.txt` |
| `FLT_DIG` | `( 6 )` |  | `brightv/library/standard/bmath.txt` |
| `FLT_EPSILON` | `( 1.19209290e-07F )` |  | `brightv/library/standard/bmath.txt` |
| `FLT_MANT_DIG` | `( 24 )` |  | `brightv/library/standard/bmath.txt` |
| `FLT_MAX` | `( 3.40282347e+38F )` | 単精度実数の最大値 | `brightv/library/standard/bmath.txt` |
| `FLT_MAX_10_EXP` | `( 38 )` | 単精度実数の10に対する累乗最大値 | `brightv/library/standard/bmath.txt` |
| `FLT_MAX_EXP` | `( 128 )` | 単精度実数の2に対する累乗最大値 | `brightv/library/standard/bmath.txt` |
| `FLT_MIN` | `( 1.17549435e-38F )` | 単精度実数の最小値 | `brightv/library/standard/bmath.txt` |
| `FLT_MIN_10_EXP` | `( -37 )` | 単精度実数の10に対する累乗最小値 | `brightv/library/standard/bmath.txt` |
| `FLT_MIN_EXP` | `( -125 )` | 単精度実数の2に対する累乗最小値 | `brightv/library/standard/bmath.txt` |
| `FLT_RADIX` | `( 2 )` |  | `brightv/library/standard/bmath.txt` |
| `FLT_ROUNDS` | `( 1 )` | 四捨五入 | `brightv/library/standard/bmath.txt` |
| `FNDELAY` | `O_NDELAY` | synonym | `btron3/os_spec/shell/tcpip.txt` |
| `FSNM_LEN` | `40` | ファイルシステム名のバイト長 | `btron3/os_spec/shell/omgr.txt` |
| `FS_ASYN` | `0x0002` | 非同期指定 | `btron3/os_spec/kernel/file.txt` |
| `FS_RONLY` | `0x0001` | 書き込み禁止 | `btron3/os_spec/kernel/file.txt` |
| `FS_SYNC` | `0x0000` | 同期指定 | `btron3/os_spec/kernel/file.txt` |
| `FTC_DEFAULT` | `0x80000000` |  | `btron3/os_spec/shell/font_mgr.txt` |
| `FTC_MINCHO` | `0x000060c6` |  | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_ALL` | `-1` | すべての一覧 | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_ALTBASE` | `3` | ベースフォントファミリー(代替) | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_ALTTARGET` | `1` | 対象フォントファミリー(代替) | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_BASE` | `2` | ベースフォントファミリー | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_BOLD` | `000050` | 太字: bold | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_BOUNDLESS` | `0x00080000` | イメージを文字枠に収めない | `brightv/diff/diff.txt` |
| `FT_BW` | `0x0000` | 白黒二値 | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_COND` | `000001` | 圧縮: condense | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_DBOLD` | `000040` | 中太字: demi bold | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_DEFAULT` | `4` | デフォルトフォントファミリー | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_FAMILY` | `-2` | 異なるフォントファミリーの一覧 | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_FILE` | `1` | 動的フォント | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_FIXED` | `0x0000` | 固定ピッチ | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_FIXIMG` | `0` | 固定イメージ幅形式 | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_FONT` | `0` | 同一フォントファミリーの一覧 | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_GRAYSCALE` | `0x10000` | グレースケール | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_HDRAW` | `0x0000` | 横書き文字 | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_HSLOPE1` | `000100` | 水平斜体 : italic | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_HSLOPE2` | `000200` | 水平斜体 | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_HSLOPE3` | `000300` | 水平斜体 | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_IDXINDIRECT` | `1` | 間接インデックス形式 | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_IDXNONE` | `0` | インデックスデータなし | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_IMAGE` | `0x00000001` | イメージの取り出しを指定 | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_IMGWDATA` | `2` | イメージ幅付き文字幅データ | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_INDEXWDATA` | `1` | 文字幅／インデックスデータ | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_LIGHT` | `000020` | 細字: light | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_LOC` | `3` | 同一ファイルの一覧 | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_MEM` | `0` | 固定フォント | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_MIDI` | `000000` | 中字: midium | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_OUTLINE` | `001000` | 袋文字: out line | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_PROP` | `0x8000` | 比例ピッチ | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_REGULAR` | `000000` | 通常: regular | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_RES` | `0x0100` | 常駐タイプ指定 | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_RIGHT` | `000000` | 正体: right | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_SCALL` | `1` | 同一文字セットの一覧 | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_SCFAMILY` | `2   /* 同一文字セットの` |  | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_SHADOW` | `002000` | 影付文字: shadow | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_SIMPLEWDATA` | `0` | 文字幅データ | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_SOLID` | `000000` | 通常: solid | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_SYS` | `0x00000002` | (システムで使用) | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_SYSTEM` | `2` | システムフォント | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_TARGET` | `0` | 対象フォントファミリー | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_TRUETYPE` | `3` | TrueType形式 | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_UBOLD` | `000070` | 超太字: ultra bold | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_UCOND` | `000003` | 超圧縮: ultra condense | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_UNDEF` | `-1` | 未定義文字 | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_UWIDE` | `000007` | 超幅広: ultra wide | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_VALIMG` | `1` | 可変イメージ幅形式 | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_VDRAW` | `0x4000` | 縦書き文字 | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_VECTOR` | `2` | ベクトル形式(アウトライン) | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_VSLOPE1` | `000500` | 垂直斜体 | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_VSLOPE2` | `000600` | 垂直斜体 | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_VSLOPE3` | `000700` | 垂直斜体 | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_WIDE` | `000005` | 幅広: wide | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_WSHADOW` | `003000` | 白影付文字: white shadow | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_XBOLD` | `000060` | 極太字: extra bold | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_XCOND` | `000002` | 極圧縮: extra condense | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_XLIGHT` | `000010` | 極細字: extra light | `btron3/os_spec/shell/font_mgr.txt` |
| `FT_XWIDE` | `000006` | 極幅広: extra wide | `btron3/os_spec/shell/font_mgr.txt` |
| `FUSENREQ` | `0x13` |  | `btron3/os_spec/shell/omgr.txt` |
| `FUSENSEG_STRUCT` | `"hhhhhwwwh3h16h16hh"` |  | `brightv/library/standard/tad.txt` |
| `F_APLATR1` | `0x0040` | アプリケーション属性1 | `btron3/os_spec/kernel/file.txt` |
| `F_APLATR2` | `0x0080` | アプリケーション属性2 | `btron3/os_spec/kernel/file.txt` |
| `F_BASED` | `0x0001` | ベース指定 | `btron3/os_spec/kernel/file.txt` |
| `F_BWD` | `0x0002` | 後向きサーチ | `btron3/os_spec/kernel/file.txt` |
| `F_CKLOCK` | `0x0003` | ロック状態のチェック | `btron3/os_spec/kernel/file.txt` |
| `F_COMMON` | `0x0100` | 共有メモリー空間にマップ | `btron3/os_spec/kernel/file.txt` |
| `F_DBOTH` | `3` | ファイル名と生成日時が異なる | `btron3/os_spec/kernel/file.txt` |
| `F_DDATE` | `2` | 生成日時が異なっていた | `btron3/os_spec/kernel/file.txt` |
| `F_DIRECT` | `0x0002` | 直接取り出し指定 | `btron3/os_spec/kernel/file.txt` |
| `F_DNAME` | `1` | ファイル名が異なっていた | `btron3/os_spec/kernel/file.txt` |
| `F_ENDTOP` | `0x0005` | 終端から先頭 | `btron3/os_spec/kernel/file.txt` |
| `F_ENG` | `0x0080` | 英語 (ラテン文字族1) | `btron3/os_spec/kernel/file.txt` |
| `F_EXCL` | `0x0100` | 排他モード | `btron3/os_spec/kernel/file.txt` |
| `F_EXCUTE` | `0x0001` | E アクセス | `btron3/os_spec/kernel/file.txt` |
| `F_EXIST` | `0x0000` | ファイルの存在チェック | `btron3/os_spec/kernel/file.txt` |
| `F_FILE` | `0x1000` | 通常ファイル | `btron3/os_spec/kernel/file.txt` |
| `F_FILEID` | `0x0002` | ファイル ID 指定 | `btron3/os_spec/kernel/file.txt` |
| `F_FIX` | `0x0001` | 固定リンクとする | `btron3/os_spec/kernel/file.txt` |
| `F_FLOAT` | `0x0000` | 固定リンクとしない | `btron3/os_spec/kernel/file.txt` |
| `F_FWD` | `0x0000` | 前向きサーチ | `btron3/os_spec/kernel/file.txt` |
| `F_GETDEV` | `(-1)` | 対応するデバイス名を得る | `btron3/os_spec/kernel/file.txt` |
| `F_GETFL` | `3` | get flags | `btron3/os_spec/shell/tcpip.txt` |
| `F_GETNAM` | `(-2)` | 対応する接続名を得る | `btron3/os_spec/kernel/file.txt` |
| `F_HIDDEN` | `0x8000` | 隠蔽付箋 | `btron3/os_spec/shell/omgr.txt` |
| `F_JPN` | `0x0021` | 日本語 (日本語文字族) | `btron3/os_spec/kernel/file.txt` |
| `F_LOCK` | `0x0001` | ロック | `btron3/os_spec/kernel/file.txt` |
| `F_NBWD` | `0x0003` | 後向きサーチ(手前から) | `btron3/os_spec/kernel/file.txt` |
| `F_NFWD` | `0x0001` | 前向きサーチ(次から) | `btron3/os_spec/kernel/file.txt` |
| `F_NOCHG` | `0x8000` | 変更しない | `btron3/os_spec/kernel/file.txt` |
| `F_NONAME` | `0x0800` | 付箋名の表示なし | `btron3/os_spec/shell/omgr.txt` |
| `F_NORM` | `0x0000` | 通常指定 | `btron3/os_spec/kernel/file.txt` |
| `F_NOTYPE` | `0x0400` | データタイプの表示なし | `btron3/os_spec/shell/omgr.txt` |
| `F_OWNACC` | `0x0007` | 所有者アクセス属性(RWE) | `btron3/os_spec/kernel/file.txt` |
| `F_OWNACE` | `0x0001` | 所有者のEアクセス属性 | `btron3/os_spec/kernel/file.txt` |
| `F_OWNACR` | `0x0004` | 所有者のRアクセス属性 | `btron3/os_spec/kernel/file.txt` |
| `F_OWNACW` | `0x0002` | 所有者のWアクセス属性 | `btron3/os_spec/kernel/file.txt` |
| `F_PASWD` | `0x0008` | パスワードの有無 | `btron3/os_spec/kernel/file.txt` |
| `F_PERM` | `0x0020` | 削除不可属性 | `btron3/os_spec/kernel/file.txt` |
| `F_READ` | `0x0004` | 読み込み専用オープン | `btron3/os_spec/kernel/file.txt` |
| `F_RONLY` | `0x0010` | 書込不可属性 | `btron3/os_spec/kernel/file.txt` |
| `F_RSTA1` | `0x0006` | アプリ属性1のリセット | `btron3/os_spec/kernel/file.txt` |
| `F_RSTA2` | `0x0008` | アプリ属性2のリセット | `btron3/os_spec/kernel/file.txt` |
| `F_RSTPERM` | `0x0004` | 削除不可属性のリセット | `btron3/os_spec/kernel/file.txt` |
| `F_RSTRONLY` | `0x0002` | 書込不可属性のリセット | `btron3/os_spec/kernel/file.txt` |
| `F_SATR1` | `0x1000` | 同一属性データ1 | `btron3/os_spec/kernel/file.txt` |
| `F_SATR2` | `0x0800` | 同一属性データ2 | `btron3/os_spec/kernel/file.txt` |
| `F_SATR3` | `0x0400` | 同一属性データ3 | `btron3/os_spec/kernel/file.txt` |
| `F_SATR4` | `0x0200` | 同一属性データ4 | `btron3/os_spec/kernel/file.txt` |
| `F_SATR5` | `0x0100` | 同一属性データ5 | `btron3/os_spec/kernel/file.txt` |
| `F_SETA1` | `0x0005` | アプリ属性1のセット | `btron3/os_spec/kernel/file.txt` |
| `F_SETA2` | `0x0007` | アプリ属性2のセット | `btron3/os_spec/kernel/file.txt` |
| `F_SETFL` | `4` | set flags | `btron3/os_spec/shell/tcpip.txt` |
| `F_SETPERM` | `0x0003` | 削除不可属性のセット | `btron3/os_spec/kernel/file.txt` |
| `F_SETRONLY` | `0x0001` | 書込不可属性のセット | `btron3/os_spec/kernel/file.txt` |
| `F_SFILE` | `0x4000` | 同一ファイル | `btron3/os_spec/kernel/file.txt` |
| `F_SNAME` | `0x2000` | 同一ファイル名 | `btron3/os_spec/kernel/file.txt` |
| `F_SYNC` | `0` | 一致している | `btron3/os_spec/kernel/file.txt` |
| `F_SYSTEM` | `0x0300` | システムメモリー空間にマップ | `btron3/os_spec/kernel/file.txt` |
| `F_TOPEND` | `0x0004` | 先頭から終端 | `btron3/os_spec/kernel/file.txt` |
| `F_TSLOCK` | `0x0002` | テスト & ロック | `btron3/os_spec/kernel/file.txt` |
| `F_UNLOCK` | `0x0000` | アンロック | `btron3/os_spec/kernel/file.txt` |
| `F_UPDATE` | `0x0006` | 更新用(読込／書込)オープン | `btron3/os_spec/kernel/file.txt` |
| `F_WEXCL` | `0x0200` | 排他書き込みモード | `btron3/os_spec/kernel/file.txt` |
| `F_WRITE` | `0x0002` | 書き込み専用オープン | `btron3/os_spec/kernel/file.txt` |
| `GETCTL` | `0x100` | 設定取り出し | `brightv/device/console.txt` |
| `GET_BUZ_BEEP` | `0x0000` | 音色：標準の警告音 | `btron3/os_spec/kernel/event.txt` |
| `GET_BUZ_KEYOFF` | `0x0004` | 音色：キーOFFのクリック音 | `btron3/os_spec/kernel/event.txt` |
| `GET_BUZ_KEYON` | `0x0003` | 音色：キーONのクリック音 | `btron3/os_spec/kernel/event.txt` |
| `GET_BUZ_PDOFF` | `0x0002` | 音色：ボタンOFFのクリック音 | `btron3/os_spec/kernel/event.txt` |
| `GET_BUZ_PDON` | `0x0001` | 音色：ボタンONのクリック音 | `btron3/os_spec/kernel/event.txt` |
| `GET_BUZ_VOLUME` | `0x0010` | 音量 | `btron3/os_spec/kernel/event.txt` |
| `GMENU_DATA` | `9` | 汎用メニュー定義 | `btron3/os_spec/shell/data.txt` |
| `HUGE_VAL` | `( __infinity() )` | 事実上の無限大 | `brightv/library/standard/bmath.txt` |
| `HW_COM1` | `(0x0050)` | シリアルポート 1 | `brightv/device/hwresmgr.txt` |
| `HW_COM2` | `(0x0051)` | シリアルポート 2 | `brightv/device/hwresmgr.txt` |
| `HW_COM3` | `(0x0052)` | シリアルポート 3 | `brightv/device/hwresmgr.txt` |
| `HW_COM4` | `(0x0053)` | シリアルポート 4 | `brightv/device/hwresmgr.txt` |
| `HW_COP` | `(0x0008)` | コプロセッサ | `brightv/device/hwresmgr.txt` |
| `HW_DMA` | `(0x0007)` | DMA コントローラ | `brightv/device/hwresmgr.txt` |
| `HW_FDC` | `(0x0020)` | フロッピーディスク | `brightv/device/hwresmgr.txt` |
| `HW_IDE1` | `(0x0030)` | IDE コントローラ 1 | `brightv/device/hwresmgr.txt` |
| `HW_IDE2` | `(0x0031)` | IDE コントローラ 2 | `brightv/device/hwresmgr.txt` |
| `HW_IO(k)` | `(0x10000 \| (k))` | I/O アドレス | `brightv/device/hwresmgr.txt` |
| `HW_IRQ(k)` | `(0x20000 \| (k))` | 割り込み番号 | `brightv/device/hwresmgr.txt` |
| `HW_KBD` | `(0x0002)` | キーボード | `brightv/device/hwresmgr.txt` |
| `HW_LPT1` | `(0x0040)` | プリンタポート 1 | `brightv/device/hwresmgr.txt` |
| `HW_LPT2` | `(0x0041)` | プリンタポート 2 | `brightv/device/hwresmgr.txt` |
| `HW_LPT3` | `(0x0042)` | プリンタポート 3 | `brightv/device/hwresmgr.txt` |
| `HW_MOUSE` | `(0x0003)` | マウス | `brightv/device/hwresmgr.txt` |
| `HW_NET` | `(0x0070)` | ネットワークアダプタ | `brightv/device/hwresmgr.txt` |
| `HW_PCIC` | `(0x0060)` | PC カードコントローラ | `brightv/device/hwresmgr.txt` |
| `HW_PCIDEV` | `(0x8000)` | PCI デバイス(その他) | `brightv/device/hwresmgr.txt` |
| `HW_PCSLT1` | `(0x0100)` | PC カードスロット 1 | `brightv/device/hwresmgr.txt` |
| `HW_PCSLT2` | `(0x0101)` | PC カードスロット 2 | `brightv/device/hwresmgr.txt` |
| `HW_PIC` | `(0x0006)` | 割り込みコントローラ | `brightv/device/hwresmgr.txt` |
| `HW_RSV` | `(0xFFFF)` | 予約 (使用不可) | `brightv/device/hwresmgr.txt` |
| `HW_RTC` | `(0x0005)` | RTC | `brightv/device/hwresmgr.txt` |
| `HW_SOUND` | `(0x0080)` | サウンドデバイス | `brightv/device/hwresmgr.txt` |
| `HW_SYSTEM` | `(0x0001)` | システム | `brightv/device/hwresmgr.txt` |
| `HW_TIMER` | `(0x0004)` | システムタイマー | `brightv/device/hwresmgr.txt` |
| `HW_TYPEMASK` | `(0xFFFF0000)` |  | `brightv/device/hwresmgr.txt` |
| `HW_USB1` | `(0x0090)` | USB ホストコンローラ 1 | `brightv/device/hwresmgr.txt` |
| `HW_USB2` | `(0x0091)` | USB ホストコンローラ 2 | `brightv/device/hwresmgr.txt` |
| `HW_VIDEO` | `(0x0010)` | ビデオ | `brightv/device/hwresmgr.txt` |
| `IFC_100FX` | `(5)` | 100 Base FX | `brightv/device/network.txt` |
| `IFC_100TX` | `(4)` | 100 Base TX | `brightv/device/network.txt` |
| `IFC_AUI` | `(1)` | AUI (10 Base 5) | `brightv/device/network.txt` |
| `IFC_AUTO` | `(6)` | Auto | `brightv/device/network.txt` |
| `IFC_BNC` | `(3)` | BNC (10 Base 2) | `brightv/device/network.txt` |
| `IFC_TPE` | `(2)` | TPE (10 Base T) | `brightv/device/network.txt` |
| `IFC_UNKNOWN` | `(0)` | 不明 / デフォールト | `brightv/device/network.txt` |
| `IMAGESEG_EXTEND_STRUCT` | `"hh"` | len まで | `brightv/library/standard/tad.txt` |
| `IMAGESEG_STRUCT` | `"4h4hhhhh4hwwwhhhh4h"` | bounds まで | `brightv/library/standard/tad.txt` |
| `IMPORT` | `extern` | プログラム中の任意の箇所で参照 | `btron3/shared_data/data_type.txt` |
| `IM_ALPH` | `(ES_ALPH)` | 英語(小文字) | `btron3/os_spec/kernel/event.txt` |
| `IM_CAPS` | `(ES_ALPH \| ES_KANA)` | 英語(大文字) | `btron3/os_spec/kernel/event.txt` |
| `IM_HIRA` | `0x0000` | 日本語ひらがな | `btron3/os_spec/kernel/event.txt` |
| `IM_KATA` | `(ES_KANA)` | 日本語カタカナ | `btron3/os_spec/kernel/event.txt` |
| `IM_MASK` | `(ES_ALPH \| ES_KANA)` |  | `btron3/os_spec/kernel/event.txt` |
| `INPUT` | `2` | 入力モード (RAW, etc) | `brightv/device/console.txt` |
| `INS_CODE` | `0x114c` | 挿入 | `brightv/library/standard/keycode.txt` |
| `IPPROTO_IP` | `0x0000` | options for IP level | `brightv/diff/diff.txt` |
| `IPPROTO_TCP` | `0x0006` | options for TCP level | `brightv/diff/diff.txt` |
| `IP_OPTIONS` | `0x0001` | IP header options | `btron3/os_spec/shell/tcpip.txt` |
| `IXANY` | `0x02` | IXON 時に任意文字の受信で出力を再開 | `brightv/device/console.txt` |
| `IXOFF` | `0x04` | XON / XOFF 入力フロー制御 | `brightv/device/console.txt` |
| `IXON` | `0x01` | XON / XOFF 出力フロー制御 | `brightv/device/console.txt` |
| `InputModeCmd(mode)` | `(0x03000000 \| (mode))` |  | `brightv/device/kbpd.txt` |
| `KB_MAXTIME` | `10000` |  | `brightv/device/kbpd.txt` |
| `KCTMAX` | `4000` |  | `brightv/device/kbpd.txt` |
| `KCTSEL` | `64` |  | `brightv/device/kbpd.txt` |
| `KC_ASSIST` | `0x001b` | 補助 | `brightv/library/standard/keycode.txt` |
| `KC_BS` | `0x0008` | 一字消 | `brightv/library/standard/keycode.txt` |
| `KC_CAN` | `0x0018` | 取消 × | `brightv/library/standard/keycode.txt` |
| `KC_CAPN` | `0x1001` | ひら←→カタ切換 | `brightv/device/kbpd.txt` |
| `KC_CC_D` | `0x0101` | メインCCキー  ↓ | `brightv/device/kbpd.txt` |
| `KC_CC_L` | `0x0103` | メインCCキー  ← | `brightv/device/kbpd.txt` |
| `KC_CC_R` | `0x0102` | メインCCキー  → | `brightv/device/kbpd.txt` |
| `KC_CC_U` | `0x0100` | メインCCキー  ↑ | `brightv/device/kbpd.txt` |
| `KC_CMD` | `0x1005` | 命令 | `brightv/device/kbpd.txt` |
| `KC_CNV` | `0x001e` | 変換 | `brightv/device/kbpd.txt` |
| `KC_CNV0` | `0x1151` | 無変換 (ひらがな変換) | `brightv/library/standard/keycode.txt` |
| `KC_CNV1` | `0x1152` | SHIFT+無変換(カタカナ変換) | `brightv/library/standard/keycode.txt` |
| `KC_CR` | `0x000d` | 改行 | `brightv/library/standard/keycode.txt` |
| `KC_DEL` | `0x007f` | 削除 | `brightv/library/standard/keycode.txt` |
| `KC_EIJI` | `0x1000` | 英語←→日本語切換 | `brightv/device/kbpd.txt` |
| `KC_END` | `0x125e` | End | `brightv/device/kbpd.txt` |
| `KC_ENG0` | `0x1008` | 英語 | `brightv/device/kbpd.txt` |
| `KC_ENG0_H` | `0x1018` | 英語 & 半角 | `brightv/device/kbpd.txt` |
| `KC_ENG1` | `0x1009` | 英語CAPS | `brightv/device/kbpd.txt` |
| `KC_ENG1_H` | `0x1019` | 英語CAPS & 半角 | `brightv/device/kbpd.txt` |
| `KC_EXP` | `0x1004` | 拡張 | `brightv/device/kbpd.txt` |
| `KC_HAN` | `0x1150` | 全角←→半角切替 | `brightv/device/kbpd.txt` |
| `KC_HOME` | `0x1245` | Home | `brightv/device/kbpd.txt` |
| `KC_IEND` | `0x0004` | 入力終 ○ | `brightv/library/standard/keycode.txt` |
| `KC_JPN0` | `0x1006` | 日本語ひら | `brightv/device/kbpd.txt` |
| `KC_JPN0_Z` | `0x1016` | 日本語ひら & 全角 | `brightv/device/kbpd.txt` |
| `KC_JPN1` | `0x1007` | 日本語カタ | `brightv/device/kbpd.txt` |
| `KC_JPN1_Z` | `0x1017` | 日本語カタ & 全角 | `brightv/device/kbpd.txt` |
| `KC_KBSEL` | `0x100a` | かな←→ローマ字 | `brightv/device/kbpd.txt` |
| `KC_NL` | `0x000a` | 改段落 | `brightv/library/standard/keycode.txt` |
| `KC_NUL` | `0x0000` | 無効キー | `brightv/library/standard/keycode.txt` |
| `KC_PF1` | `0x1161` | PF1 | `brightv/library/standard/keycode.txt` |
| `KC_PF10` | `0x116a` | PF10 | `brightv/library/standard/keycode.txt` |
| `KC_PF11` | `0x116b` | PF11 | `brightv/library/standard/keycode.txt` |
| `KC_PF12` | `0x116c` | PF12 | `brightv/library/standard/keycode.txt` |
| `KC_PF2` | `0x1162` | PF2 | `brightv/library/standard/keycode.txt` |
| `KC_PF3` | `0x1163` | PF3 | `brightv/library/standard/keycode.txt` |
| `KC_PF4` | `0x1164` | PF4 | `brightv/library/standard/keycode.txt` |
| `KC_PF5` | `0x1165` | PF5 | `brightv/library/standard/keycode.txt` |
| `KC_PF6` | `0x1166` | PF6 | `brightv/library/standard/keycode.txt` |
| `KC_PF7` | `0x1167` | PF7 | `brightv/library/standard/keycode.txt` |
| `KC_PF8` | `0x1168` | PF8 | `brightv/library/standard/keycode.txt` |
| `KC_PF9` | `0x1169` | PF9 | `brightv/library/standard/keycode.txt` |
| `KC_PGDN` | `0x1247` | PageDown | `brightv/device/kbpd.txt` |
| `KC_PGUP` | `0x1246` | PageUp | `brightv/device/kbpd.txt` |
| `KC_PG_D` | `0x010d` | ページキー      ↓ | `brightv/library/standard/keycode.txt` |
| `KC_PG_L` | `0x010f` | ページキー      ← | `brightv/library/standard/keycode.txt` |
| `KC_PG_R` | `0x010e` | ページキー      → | `brightv/library/standard/keycode.txt` |
| `KC_PG_U` | `0x010c` | ページキー      ↑ | `brightv/library/standard/keycode.txt` |
| `KC_RCNV` | `0x001f` | 逆変換 | `brightv/device/kbpd.txt` |
| `KC_SC_D` | `0x0105` | サブCCキー    ↓ | `brightv/device/kbpd.txt` |
| `KC_SC_L` | `0x0107` | サブCCキー    ← | `brightv/device/kbpd.txt` |
| `KC_SC_R` | `0x0106` | サブCCキー    → | `brightv/device/kbpd.txt` |
| `KC_SC_U` | `0x0104` | サブCCキー    ↑ | `brightv/device/kbpd.txt` |
| `KC_SHT_L` | `0x1003` | 左シフト | `brightv/device/kbpd.txt` |
| `KC_SHT_R` | `0x1002` | 右シフト | `brightv/device/kbpd.txt` |
| `KC_SPACE` | `0x2121` | 空白 | `brightv/library/standard/keycode.txt` |
| `KC_SS_D` | `0x0109` | スクロールキー  ↓ | `brightv/library/standard/keycode.txt` |
| `KC_SS_L` | `0x010b` | スクロールキー  ← | `brightv/library/standard/keycode.txt` |
| `KC_SS_R` | `0x010a` | スクロールキー  → | `brightv/library/standard/keycode.txt` |
| `KC_SS_U` | `0x0108` | スクロールキー  ↑ | `brightv/library/standard/keycode.txt` |
| `KC_TAB` | `0x0009` | タブ | `brightv/library/standard/keycode.txt` |
| `KEYMAX` | `256` |  | `brightv/device/kbpd.txt` |
| `KID_IBM_EG` | `0x40` | IBM 101 (系) 英語キーボード | `brightv/device/kbpd.txt` |
| `KID_IBM_JP` | `0x41` | IBM 106 (系) 日本語キーボード | `brightv/device/kbpd.txt` |
| `KID_TRON_JP` | `0x01` | TRON 日本語キーボード | `brightv/device/kbpd.txt` |
| `KID_unknown` | `0x00` | 未定義キーボード | `brightv/device/kbpd.txt` |
| `KIN_KANA` | `0x0000` | かな入力モード | `btron3/os_spec/kernel/event.txt` |
| `KIN_ROMAN` | `0x0001` | ローマ字入力モード | `btron3/os_spec/kernel/event.txt` |
| `LDBL_DIG` | `DBL_DIG` |  | `brightv/library/standard/bmath.txt` |
| `LDBL_EPSILON` | `DBL_EPSILON` |  | `brightv/library/standard/bmath.txt` |
| `LDBL_MANT_DIG` | `DBL_MANT_DIG` |  | `brightv/library/standard/bmath.txt` |
| `LDBL_MAX` | `DBL_MAX` | 長倍精度実数の最大値 | `brightv/library/standard/bmath.txt` |
| `LDBL_MAX_10_EXP` | `DBL_MAX_10_EXP` | 長倍精度実数の10に対する累乗最大値 | `brightv/library/standard/bmath.txt` |
| `LDBL_MAX_EXP` | `DBL_MAX_EXP` | 長倍精度実数の2に対する累乗最大値 | `brightv/library/standard/bmath.txt` |
| `LDBL_MIN` | `DBL_MIN` | 長倍精度実数の最小値 | `brightv/library/standard/bmath.txt` |
| `LDBL_MIN_10_EXP` | `DBL_MIN_10_EXP` | 長倍精度実数の10に対する累乗最小値 | `brightv/library/standard/bmath.txt` |
| `LDBL_MIN_EXP` | `DBL_MIN_EXP` | 長倍精度実数の2に対する累乗最小値 | `brightv/library/standard/bmath.txt` |
| `LOCAL` | `static` | モジュール(*)内でのみ使われる | `btron3/shared_data/data_type.txt` |
| `LOG10B2` | `0.33219280948873623e+01` |  | `brightv/library/standard/bmath.txt` |
| `LOG10BE` | `0.23025850929940456e+01` |  | `brightv/library/standard/bmath.txt` |
| `LOG2B10` | `0.30102999566398119e+00` |  | `brightv/library/standard/bmath.txt` |
| `LOGEB2` | `1.4426950408889634074` |  | `brightv/library/standard/bmath.txt` |
| `LOG_MASK(pri)` | `(1 << (pri))` |  | `brightv/library/standard/bsyslog.txt` |
| `LOG_UPTO(pri)` | `((1 << ((pri) + 1)) - 1)` |  | `brightv/library/standard/bsyslog.txt` |
| `L_CONNM` | `8` | 接続名 | `btron3/os_spec/kernel/file.txt` |
| `L_DEVNM` | `8` | デバイス名の長さ | `brightv/device/common.txt` |
| `L_DEVPAR` | `88` | 出力デバイスパラメータ長さ | `btron3/os_spec/shell/printmgr.txt` |
| `L_DLNM` | `20` | デバイス所在名 | `btron3/os_spec/kernel/file.txt` |
| `L_FNM` | `20` | ファイル名 | `btron3/os_spec/kernel/file.txt` |
| `L_FSNM` | `20` | ファイルシステム名 | `btron3/os_spec/kernel/file.txt` |
| `L_HWPAR` | `160` | プリンタ固有パラメータ長さ | `btron3/os_spec/shell/printmgr.txt` |
| `L_NETPNAME` | `(40)` | 製品名長さ | `brightv/device/network.txt` |
| `L_PATHNM` | `256` | パス名 | `btron3/os_spec/kernel/file.txt` |
| `L_PRNM` | `20` | プリンタ名の長さ | `btron3/os_spec/shell/printmgr.txt` |
| `L_UNDEF` | `0x80000000` | 負の最大値 | `btron3/os_spec/shell/parts.txt` |
| `L_USRNM` | `(12+2)` | ユーザ(グループ)名 + 隠し名 | `btron3/os_spec/kernel/file.txt` |
| `L_VD` | `0` |  | `brightv/device/common.txt` |
| `MACHINE_ID` | `PCAT` | 機種 PC-AT 互換機 | `brightv/library/standard/basic.txt` |
| `MAX_CMAPENT` | `256` |  | `brightv/device/primgdrv.txt` |
| `MAX_PNLNUM` | `4` | 見出しのページ数 | `brightv/library/appl/libuse.txt` |
| `MAX_PTR_SIZE` | `64` | ポインタイメージデータサイズ | `btron3/os_spec/dp/pointer_func.txt` |
| `MAX_TUPLESZ` | `(255 + 2)` |  | `brightv/device/cardmgr.txt` |
| `MC_ATTR` | `0x1002` | 文字属性指定 | `btron3/os_spec/shell/parts.txt` |
| `MC_EMPHAS` | `0x1400` | 強調項目 | `btron3/os_spec/shell/parts.txt` |
| `MC_FIG` | `0x1001` | 図形データ | `btron3/os_spec/shell/parts.txt` |
| `MC_INACT` | `0x1800` | 不能項目 | `btron3/os_spec/shell/parts.txt` |
| `MC_IND` | `0x1100` | インジケータ有無 | `btron3/os_spec/shell/menu.txt` |
| `MC_KEY` | `0x10f0` | キーマクロの個数マスク | `btron3/os_spec/shell/menu.txt` |
| `MC_KEY1` | `0x1010` | キーマクロ 1 個 | `btron3/os_spec/shell/menu.txt` |
| `MC_KEY14` | `0x10e0` | キーマクロ 14 個 | `btron3/os_spec/shell/menu.txt` |
| `MC_KEY15` | `0x10f0` | キーマクロ 15 個 | `btron3/os_spec/shell/menu.txt` |
| `MC_KEY2` | `0x1020` | キーマクロ 2 個 | `btron3/os_spec/shell/menu.txt` |
| `MC_LINE` | `0x1004` | 区切線 | `btron3/os_spec/shell/menu.txt` |
| `MC_MENU(n)` | `(0x1004 + ((n)  0 かつ csize.y > 0 のときのみ有効` |  | `brightv/diff/diff.txt` |
| `MC_NFLD` | `0x0c00` | 数値フィールド指定 | `btron3/os_spec/shell/parts.txt` |
| `MC_NLEFT` | `0x0100` | 左揃え | `btron3/os_spec/shell/parts.txt` |
| `MC_NZERO` | `0x0200` | 前ゼロ表示 | `btron3/os_spec/shell/parts.txt` |
| `MC_SEL` | `0x1200` | インジケータ状態 | `btron3/os_spec/shell/menu.txt` |
| `MC_SFLD` | `0x0800` | 文字列フィールド指定 | `btron3/os_spec/shell/parts.txt` |
| `MC_STR` | `0x1000` | 文字列 | `btron3/os_spec/shell/parts.txt` |
| `MENU_DATA` | `8` | 標準メニュー定義 | `btron3/os_spec/shell/data.txt` |
| `MH_BREAK` | `((FUNCP)2)` | 処理中断 | `btron3/os_spec/kernel/message.txt` |
| `MH_NONE` | `((FUNCP)1)` | 無視 | `btron3/os_spec/kernel/message.txt` |
| `MH_TERM` | `((FUNCP)3)` | プロセス終了 | `btron3/os_spec/kernel/message.txt` |
| `MM_ABORT` | `MSGMASK(MS_ABORT)` | プロセス異常終了 | `btron3/os_spec/kernel/message.txt` |
| `MM_ALL` | `(0x7fffffff)` | 全マスク | `btron3/os_spec/kernel/message.txt` |
| `MM_EXIT` | `MSGMASK(MS_EXIT)` | プロセス正常終了 | `btron3/os_spec/kernel/message.txt` |
| `MM_MNG0` | `MSGMASK(MS_MNG0)` | 外殻予約 | `btron3/os_spec/kernel/message.txt` |
| `MM_MNG1` | `MSGMASK(MS_MNG1)` | 外殻予約 | `btron3/os_spec/kernel/message.txt` |
| `MM_MNG10` | `MSGMASK(MS_MNG10)` | 外殻予約 | `btron3/os_spec/kernel/message.txt` |
| `MM_MNG11` | `MSGMASK(MS_MNG11)` | 外殻予約 | `btron3/os_spec/kernel/message.txt` |
| `MM_MNG12` | `MSGMASK(MS_MNG12)` | 外殻予約 | `btron3/os_spec/kernel/message.txt` |
| `MM_MNG2` | `MSGMASK(MS_MNG2)` | 外殻予約 | `btron3/os_spec/kernel/message.txt` |
| `MM_MNG3` | `MSGMASK(MS_MNG3)` | 外殻予約 | `btron3/os_spec/kernel/message.txt` |
| `MM_MNG4` | `MSGMASK(MS_MNG4)` | 外殻予約 | `btron3/os_spec/kernel/message.txt` |
| `MM_MNG5` | `MSGMASK(MS_MNG5)` | 外殻予約 | `btron3/os_spec/kernel/message.txt` |
| `MM_MNG6` | `MSGMASK(MS_MNG6)` | 外殻予約 | `btron3/os_spec/kernel/message.txt` |
| `MM_MNG7` | `MSGMASK(MS_MNG7)` | 外殻予約 | `btron3/os_spec/kernel/message.txt` |
| `MM_MNG8` | `MSGMASK(MS_MNG8)` | 外殻予約 | `btron3/os_spec/kernel/message.txt` |
| `MM_MNG9` | `MSGMASK(MS_MNG9)` | 外殻予約 | `btron3/os_spec/kernel/message.txt` |
| `MM_NULL` | `(0)` | 空マスク | `btron3/os_spec/kernel/message.txt` |
| `MM_SYS1` | `MSGMASK(MS_SYS1)` | システム予約 | `btron3/os_spec/kernel/message.txt` |
| `MM_SYS2` | `MSGMASK(MS_SYS2)` | システム予約 | `btron3/os_spec/kernel/message.txt` |
| `MM_SYS3` | `MSGMASK(MS_SYS3)` | システム予約 | `btron3/os_spec/kernel/message.txt` |
| `MM_SYS4` | `MSGMASK(MS_SYS4)` | システム予約 | `btron3/os_spec/kernel/message.txt` |
| `MM_SYS5` | `MSGMASK(MS_SYS5)` | システム予約 | `btron3/os_spec/kernel/message.txt` |
| `MM_SYSEVT` | `MSGMASK(MS_SYSEVT)` | システムイベント(強制終了) | `btron3/os_spec/kernel/message.txt` |
| `MM_TERM` | `MSGMASK(MS_TERM)` | プロセスの強制終了 | `btron3/os_spec/kernel/message.txt` |
| `MM_TMOUT` | `MSGMASK(MS_TMOUT)` | タイムアウト | `btron3/os_spec/kernel/message.txt` |
| `MM_TYPE0` | `MSGMASK(MS_TYPE0)` | アプリケーションメッセージ | `btron3/os_spec/kernel/message.txt` |
| `MM_TYPE1` | `MSGMASK(MS_TYPE1)` |  | `btron3/os_spec/kernel/message.txt` |
| `MM_TYPE2` | `MSGMASK(MS_TYPE2)` |  | `btron3/os_spec/kernel/message.txt` |
| `MM_TYPE3` | `MSGMASK(MS_TYPE3)` |  | `btron3/os_spec/kernel/message.txt` |
| `MM_TYPE4` | `MSGMASK(MS_TYPE4)` |  | `btron3/os_spec/kernel/message.txt` |
| `MM_TYPE5` | `MSGMASK(MS_TYPE5)` |  | `btron3/os_spec/kernel/message.txt` |
| `MM_TYPE6` | `MSGMASK(MS_TYPE6)` |  | `btron3/os_spec/kernel/message.txt` |
| `MM_TYPE7` | `MSGMASK(MS_TYPE7)` |  | `btron3/os_spec/kernel/message.txt` |
| `MSGMASK(msgtype)` | `(1 << ((msgtype) - 1))` |  | `btron3/os_spec/kernel/message.txt` |
| `MSGSIZE(msgsz)` | `(offsetof(MESSAGE,msg_body) + msgsz)` |  | `btron3/os_spec/kernel/message.txt` |
| `MSG_DONTROUTE` | `0x04` | do not route | `btron3/os_spec/shell/tcpip.txt` |
| `MSG_OOB` | `0x01` | send or receive out of band data | `btron3/os_spec/shell/tcpip.txt` |
| `MSG_PEEK` | `0x02` | take data but leave it | `btron3/os_spec/shell/tcpip.txt` |
| `MS_ABORT` | `( 1)` | プロセス異常終了 | `btron3/os_spec/kernel/message.txt` |
| `MS_EXIT` | `( 2)` | プロセス正常終了 | `btron3/os_spec/kernel/message.txt` |
| `MS_MAX` | `(31)` | 最大メッセージタイプ | `btron3/os_spec/kernel/message.txt` |
| `MS_MIN` | `( 1)` | 最小メッセージタイプ | `btron3/os_spec/kernel/message.txt` |
| `MS_MNG0` | `(11)` | 外殻予約 | `btron3/os_spec/kernel/message.txt` |
| `MS_MNG1` | `(12)` | 外殻予約 | `btron3/os_spec/kernel/message.txt` |
| `MS_MNG10` | `(21)` | 外殻予約 | `btron3/os_spec/kernel/message.txt` |
| `MS_MNG11` | `(22)` | 外殻予約 | `btron3/os_spec/kernel/message.txt` |
| `MS_MNG12` | `(23)` | 外殻予約 | `btron3/os_spec/kernel/message.txt` |
| `MS_MNG2` | `(13)` | 外殻予約 | `btron3/os_spec/kernel/message.txt` |
| `MS_MNG3` | `(14)` | 外殻予約 | `btron3/os_spec/kernel/message.txt` |
| `MS_MNG4` | `(15)` | 外殻予約 | `btron3/os_spec/kernel/message.txt` |
| `MS_MNG5` | `(16)` | 外殻予約 | `btron3/os_spec/kernel/message.txt` |
| `MS_MNG6` | `(17)` | 外殻予約 | `btron3/os_spec/kernel/message.txt` |
| `MS_MNG7` | `(18)` | 外殻予約 | `btron3/os_spec/kernel/message.txt` |
| `MS_MNG8` | `(19)` | 外殻予約 | `btron3/os_spec/kernel/message.txt` |
| `MS_MNG9` | `(20)` | 外殻予約 | `btron3/os_spec/kernel/message.txt` |
| `MS_PARTS` | `6` | テキストモーメンタリスイッチ | `btron3/os_spec/shell/parts.txt` |
| `MS_PRINT` | `MS_MNG5` | 印刷メッセージタイプ | `btron3/os_spec/shell/printmgr.txt` |
| `MS_PROUT` | `MS_MNG4` | 出力メッセージ | `brightv/device/primgdrv.txt` |
| `MS_PRREQ` | `MS_MNG5` | 要求メッセージ | `brightv/device/primgdrv.txt` |
| `MS_SYS1` | `( 6)` | システム予約 | `btron3/os_spec/kernel/message.txt` |
| `MS_SYS2` | `( 7)` | システム予約 | `btron3/os_spec/kernel/message.txt` |
| `MS_SYS3` | `( 8)` | システム予約 | `btron3/os_spec/kernel/message.txt` |
| `MS_SYS4` | `( 9)` | システム予約 | `btron3/os_spec/kernel/message.txt` |
| `MS_SYS5` | `(10)` | システム予約 | `btron3/os_spec/kernel/message.txt` |
| `MS_SYSEVT` | `( 5)` | システムイベント(強制終了) | `btron3/os_spec/kernel/message.txt` |
| `MS_TERM` | `( 3)` | プロセス強制終了 | `btron3/os_spec/kernel/message.txt` |
| `MS_TMOUT` | `( 4)` | タイムアウト | `btron3/os_spec/kernel/message.txt` |
| `MS_TYPE0` | `(24)` | アプリケーションメッセージ | `btron3/os_spec/kernel/message.txt` |
| `MS_TYPE1` | `(25)` |  | `btron3/os_spec/kernel/message.txt` |
| `MS_TYPE2` | `(26)` |  | `btron3/os_spec/kernel/message.txt` |
| `MS_TYPE3` | `(27)` |  | `btron3/os_spec/kernel/message.txt` |
| `MS_TYPE4` | `(28)` |  | `btron3/os_spec/kernel/message.txt` |
| `MS_TYPE5` | `(29)` |  | `btron3/os_spec/kernel/message.txt` |
| `MS_TYPE6` | `(30)` |  | `btron3/os_spec/kernel/message.txt` |
| `MS_TYPE7` | `(31)` |  | `btron3/os_spec/kernel/message.txt` |
| `M_ACT` | `8` | 不能項目としない | `btron3/os_spec/shell/menu.txt` |
| `M_COMMON` | `0x00000001` | 共有 | `btron3/os_spec/kernel/memory.txt` |
| `M_FIXPOS` | `0x0002` | 起点項目は2番目のメイン項目 | `btron3/os_spec/shell/menu.txt` |
| `M_INACT` | `2` | 不能項目とする | `btron3/os_spec/shell/menu.txt` |
| `M_LASTPOS` | `0x0000` | 起点項目は最後に使用したメイン項目 | `btron3/os_spec/shell/menu.txt` |
| `M_NOSEL` | `4` | 選択状態としない | `btron3/os_spec/shell/menu.txt` |
| `M_RESIDENT` | `0x00004000` | 常駐 | `btron3/os_spec/kernel/memory.txt` |
| `M_SEL` | `1` | 選択状態とする | `btron3/os_spec/shell/menu.txt` |
| `M_STAT` | `0` | 変更しない | `btron3/os_spec/shell/menu.txt` |
| `M_SYSTEM` | `0x00000003` | システムのみ | `btron3/os_spec/kernel/memory.txt` |
| `NB_PARTS` | `3` | 数値ボックス | `btron3/os_spec/shell/parts.txt` |
| `NEWLINE` | `3` | 出力改行変換 (0:変換しない,1: する) | `brightv/device/console.txt` |
| `NFDBITS` | `(sizeof(long) * 8)` | bits per mask | `brightv/diff/diff.txt` |
| `NOCLR` | `8` | 表示をクリアしない | `btron3/os_spec/shell/window.txt` |
| `NOMSG` | `0x0010` | メッセージは取り出さない | `btron3/os_spec/shell/window.txt` |
| `NOWAIT` | `0x0001` | 待たない | `btron3/os_spec/kernel/message.txt` |
| `NP` | `1` | ダミー | `btron3/os_spec/dp/figure_func.txt` |
| `NS` | `1` | ダミー | `btron3/os_spec/dp/basic_concept.txt` |
| `NULL` | `((void *)0)` |  | `brightv/library/standard/bstring.txt` |
| `NULL_ITEM` | `0` | 無し | `btron3/os_spec/shell/panel.txt` |
| `NX` | `1` | ダミー | `btron3/os_spec/dp/basic_concept.txt` |
| `N_CREATE` | `0x0000` | グローバル名データの新規生成 | `btron3/os_spec/kernel/gname.txt` |
| `N_FORCE` | `0x0002` | グローバル名データの強制生成 | `btron3/os_spec/kernel/gname.txt` |
| `N_GRP` | `4` | 所属グループ数 | `btron3/os_spec/kernel/file.txt` |
| `N_MODIFY` | `0x0001` | グローバル名データの変更 | `btron3/os_spec/kernel/gname.txt` |
| `OBJ_DEV` | `0x2000      デバイス実身` |  | `brightv/library/standard/tad.txt` |
| `OBJ_EJECT` | `0x1000      取り外し可能デバイス実身` |  | `brightv/library/standard/tad.txt` |
| `OBJ_EXEC` | `0x8000      実行可能ファイル` |  | `brightv/library/standard/tad.txt` |
| `OK` | `(0)` | 正常(OK) | `btron3/shared_data/data_type.txt` |
| `O_NDELAY` | `0x04` | non-blocking | `btron3/os_spec/shell/tcpip.txt` |
| `PANEL_DATA` | `10` | パネル定義 | `btron3/os_spec/shell/data.txt` |
| `PARTS_DATA` | `7` | パーツ定義データ | `btron3/os_spec/shell/data.txt` |
| `PARTS_ITEM` | `7` | コントロールパーツ | `btron3/os_spec/shell/panel.txt` |
| `PASTEREQ` | `0x12` |  | `btron3/os_spec/shell/omgr.txt` |
| `PAT_DATA` | `3` | パターンデータ | `btron3/os_spec/shell/data.txt` |
| `PAT_ITEM` | `3` | パターン | `btron3/os_spec/shell/panel.txt` |
| `PA_PARTS` | `7` | ピクトグラムオルタネートスイッチ | `btron3/os_spec/shell/parts.txt` |
| `PCAT` | `0x00000010` | PC-AT 互換機 | `brightv/library/standard/basic.txt` |
| `PDIN_XMAX` | `4096` |  | `brightv/device/kbpd.txt` |
| `PDIN_YMAX` | `3072` |  | `brightv/device/kbpd.txt` |
| `PD_ABS` | `0x0100` |  | `brightv/device/kbpd.txt` |
| `PD_ACMSK` | `0x0e00` |  | `brightv/device/kbpd.txt` |
| `PD_MAXTIME` | `10000` |  | `brightv/device/kbpd.txt` |
| `PD_REL` | `0x0000` | 相対座標タイプ | `btron3/os_spec/kernel/event.txt` |
| `PD_REV` | `0x1000` | 左右反転 | `btron3/os_spec/kernel/event.txt` |
| `PD_SCMSK` | `0x00f0` | スキャン速度 (マスク) | `btron3/os_spec/kernel/event.txt` |
| `PD_SNMSK` | `0x000f` | 感度 (マスク) | `btron3/os_spec/kernel/event.txt` |
| `PF_INET` | `2` | TCP/IP and related | `btron3/os_spec/shell/tcpip.txt` |
| `PF_UNSPEC` | `0` | unspecified protocol family | `btron3/os_spec/shell/tcpip.txt` |
| `PI` | `3.14159265358979323846` |  | `brightv/library/standard/bmath.txt` |
| `PICT_DATA` | `2` | ピクトグラムイメージ | `btron3/os_spec/shell/data.txt` |
| `PICT_ITEM` | `2` | ピクトグラム | `btron3/os_spec/shell/panel.txt` |
| `PI_LINK` | `0x00010000` | プログラムファイルのリンク | `brightv/diff/diff.txt` |
| `PI_NTSK` | `0x00020000` | プロセス内タスク数 | `brightv/diff/diff.txt` |
| `PI_TSKSTAT` | `0x00030000` | 各タスクの状態 | `brightv/diff/diff.txt` |
| `PLANES` | `(1)` | ダミー | `btron3/os_spec/dp/basic_concept.txt` |
| `PM_PARTS` | `8` | ピクトグラムモーメンタリスイッチ | `btron3/os_spec/shell/parts.txt` |
| `PNL_TEST` | `(DB_SPNL + 0)` | [テスト]のパネル | `brightv/library/appl/libuse.txt` |
| `PNL_UPDATE` | `(DB_SPNL + 2)` | [更新]確認のパネル | `brightv/library/appl/libuse.txt` |
| `POWER_SAVE` | `1` | 省電力関連機能あり | `brightv/library/standard/basic.txt` |
| `PPNT_RESP` | `0x100` |  | `brightv/device/primgdrv.txt` |
| `PRNT_FEEDFORM` | `0x08` | 改ページ | `brightv/device/primgdrv.txt` |
| `PRNT_FINISH` | `0x00` | 処理終了 | `brightv/device/primgdrv.txt` |
| `PRNT_GETINFO` | `0x05` | プリンタ情報取り出し | `brightv/device/primgdrv.txt` |
| `PRNT_GETPAR` | `0x03` | 固有パラメータ1取り出し | `brightv/device/primgdrv.txt` |
| `PRNT_GETPAR2` | `0x09` | 固有パラメータ2取り出し | `brightv/device/primgdrv.txt` |
| `PRNT_OUTDATA` | `0x10` | プリンタ出力データ | `brightv/device/primgdrv.txt` |
| `PRNT_SETBUF` | `0x06` | 出力バッファ設定 | `brightv/device/primgdrv.txt` |
| `PRNT_SETINFO` | `0x04` | プリンタ情報設定 | `brightv/device/primgdrv.txt` |
| `PRNT_SETKIND` | `0x01` | プリンタ機種設定 | `brightv/device/primgdrv.txt` |
| `PRNT_SETPAR` | `0x02` | 固有パラメータ設定 | `brightv/device/primgdrv.txt` |
| `PR_A4` | `0x00a4` | A4 縦方向 | `btron3/os_spec/shell/printmgr.txt` |
| `PR_A4R` | `0x80a4` | A4 横方向 | `btron3/os_spec/shell/printmgr.txt` |
| `PR_A5` | `0x00a5` | A5 縦方向 | `btron3/os_spec/shell/printmgr.txt` |
| `PR_A5R` | `0x80a5` | A5 横方向 | `btron3/os_spec/shell/printmgr.txt` |
| `PR_ACTIVE` | `1` | 出力状態: 出力中 | `btron3/os_spec/shell/printmgr.txt` |
| `PR_B4` | `0x00b4` | B4 縦方向 | `btron3/os_spec/shell/printmgr.txt` |
| `PR_B4R` | `0x80b4` | B4 横方向 | `btron3/os_spec/shell/printmgr.txt` |
| `PR_B5` | `0x00b5` | B5 縦方向 | `btron3/os_spec/shell/printmgr.txt` |
| `PR_B5R` | `0x80b5` | B5 横方向 | `btron3/os_spec/shell/printmgr.txt` |
| `PR_CANCEL` | `0x01` | 設定取り消し | `btron3/os_spec/shell/printmgr.txt` |
| `PR_COLOR` | `0x0010` | カラー印刷 | `brightv/device/primgdrv.txt` |
| `PR_DEL` | `0x02` | 登録ファイルの削除あり | `btron3/os_spec/shell/printmgr.txt` |
| `PR_DELETE` | `2` | 出力状態: 削除中 | `btron3/os_spec/shell/printmgr.txt` |
| `PR_DRAFT` | `0x0001` | ドラフト印刷 | `brightv/device/primgdrv.txt` |
| `PR_EXPVOBJ` | `0x02` | 仮身展開 | `btron3/os_spec/shell/printmgr.txt` |
| `PR_FILEOUT` | `0x05` | ファイル出力 | `btron3/os_spec/shell/printmgr.txt` |
| `PR_FINE` | `0x0002` | 高密度印刷 | `brightv/device/primgdrv.txt` |
| `PR_HWPAR` | `0x0200` | 固有パラメータ1有 | `brightv/device/primgdrv.txt` |
| `PR_HWPAR2` | `0x0800` | 固有パラメータ2有 | `brightv/device/primgdrv.txt` |
| `PR_MSG` | `0x01` | 印刷終了メッセージあり | `btron3/os_spec/shell/printmgr.txt` |
| `PR_NODEL` | `0x00` | 登録ファイルの削除なし | `btron3/os_spec/shell/printmgr.txt` |
| `PR_NOMSG` | `0x00` | 印刷終了メッセージなし | `btron3/os_spec/shell/printmgr.txt` |
| `PR_NORM` | `0x00` | 通常印刷 | `btron3/os_spec/shell/printmgr.txt` |
| `PR_PREVIEW` | `0x03` | 画面表示 | `btron3/os_spec/shell/printmgr.txt` |
| `PR_PRINT` | `0x04` | 印刷 | `btron3/os_spec/shell/printmgr.txt` |
| `PR_PRNVOBJ` | `0x01` | 仮身印刷 | `btron3/os_spec/shell/printmgr.txt` |
| `PR_ROTATE` | `0x0100` | 縦横逆転サポート | `brightv/device/primgdrv.txt` |
| `PR_SET` | `0x02` | 設定 | `btron3/os_spec/shell/printmgr.txt` |
| `PR_SETHWPAR` | `0x0400` | 固有パラメータ1要設定 | `brightv/device/primgdrv.txt` |
| `PR_WAIT` | `0` | 出力状態: 出力待ち | `btron3/os_spec/shell/printmgr.txt` |
| `PTR_DATA` | `1` | ポインタイメージ | `btron3/os_spec/shell/data.txt` |
| `PTR_ITEM` | `1` | ポインタ | `btron3/os_spec/shell/panel.txt` |
| `P_ABS` | `0x0000` | 優先度の絶対指定 (≧0) | `btron3/os_spec/kernel/proc.txt` |
| `P_ACT` | `0x0010` | 可能状態 | `btron3/os_spec/shell/parts.txt` |
| `P_APPEND` | `0x0080` | 追加選択モード | `btron3/os_spec/shell/parts.txt` |
| `P_AREA` | `4` | エリア移動 | `btron3/os_spec/shell/parts.txt` |
| `P_BLINK` | `0x2000` | 点滅状態 | `btron3/os_spec/shell/parts.txt` |
| `P_BREAK` | `0x2000` | 処理の中断 | `btron3/os_spec/shell/parts.txt` |
| `P_BUT` | `3` | ボックス外でのプレス | `btron3/os_spec/shell/parts.txt` |
| `P_CHANGE` | `0x1000` | 変化あり | `btron3/os_spec/shell/parts.txt` |
| `P_COPY` | `5` | ボックス外への複写 | `btron3/os_spec/shell/parts.txt` |
| `P_DBL` | `2` | 有効なダブルクリック | `btron3/os_spec/shell/parts.txt` |
| `P_DCLICK` | `0x0080` | ダブルクリック動作指定 | `btron3/os_spec/shell/parts.txt` |
| `P_DISABLE` | `0x0800` | 操作禁止／操作許可 | `btron3/os_spec/shell/parts.txt` |
| `P_DISP` | `0x4000` | 表示状態／消去状態 | `btron3/os_spec/shell/parts.txt` |
| `P_DNUM` | `0x0080` | 実数  (64ビット) | `btron3/os_spec/shell/parts.txt` |
| `P_DOUBLE` | `0x0040` | 2 段組み配置 | `btron3/os_spec/shell/parts.txt` |
| `P_DOWN` | `1` | 下方向 への移動 | `btron3/os_spec/shell/parts.txt` |
| `P_EMPHAS` | `0x0200` | 強調枠／通常枠 | `btron3/os_spec/shell/parts.txt` |
| `P_ENABLE` | `0x0008` | 操作許可 | `btron3/os_spec/shell/parts.txt` |
| `P_END` | `2` | 入力終で終了 | `btron3/os_spec/shell/parts.txt` |
| `P_ERASE` | `0x0000` | 消去 | `btron3/os_spec/shell/parts.txt` |
| `P_EVENT` | `0x6020` | 他のイベント発生で中断 | `btron3/os_spec/shell/parts.txt` |
| `P_FIX0` | `0x0000` | 固定小数点表現 - 0 | `btron3/os_spec/shell/parts.txt` |
| `P_FIX1` | `0x0010` | 固定小数点表現 - 1 | `btron3/os_spec/shell/parts.txt` |
| `P_FRAME` | `0x0004` | 枠有り | `btron3/os_spec/shell/parts.txt` |
| `P_HALIGN` | `0x0020` | 垂直／水平配置 | `btron3/os_spec/shell/parts.txt` |
| `P_INACT` | `0x1000` | 不能状態／可能状態 | `btron3/os_spec/shell/parts.txt` |
| `P_JUMP` | `8` | ジャンプ移動 | `btron3/os_spec/shell/parts.txt` |
| `P_LEFT` | `2` | 左方向 への移動 | `btron3/os_spec/shell/parts.txt` |
| `P_LNUM` | `0x0000` | 整数  (32ビット) | `btron3/os_spec/shell/parts.txt` |
| `P_MENU` | `0x6010` | メニュー指定で中断 | `btron3/os_spec/shell/parts.txt` |
| `P_MOVE` | `4` | ボックス外への移動 | `btron3/os_spec/shell/parts.txt` |
| `P_NL` | `1` | 改段落／改行で終了 | `btron3/os_spec/shell/parts.txt` |
| `P_NODISP` | `0x1000` | 消去状態に | `btron3/os_spec/shell/parts.txt` |
| `P_NOFRAME` | `0x0400` | 枠無し／枠有り | `btron3/os_spec/shell/parts.txt` |
| `P_NONOB` | `0x0080` | トンボ表示無し | `btron3/os_spec/shell/parts.txt` |
| `P_NORMAL` | `0x0002` | 通常枠 | `btron3/os_spec/shell/parts.txt` |
| `P_NOSEL` | `0x0080` | 0 選択可能 | `btron3/os_spec/shell/parts.txt` |
| `P_NUMBER` | `0x0040` | 番号付け指定 | `btron3/os_spec/shell/parts.txt` |
| `P_PARTDISP` | `0x8000` | 表示属性指定 | `btron3/os_spec/shell/parts.txt` |
| `P_PRESS` | `0x0040` | プレス動作指定 | `btron3/os_spec/shell/parts.txt` |
| `P_RDISP` | `0x8000` | 再表示 | `btron3/os_spec/shell/parts.txt` |
| `P_READY` | `0x4000` | 実行可能状態 | `btron3/os_spec/kernel/proc.txt` |
| `P_REL` | `0x0001` | 優先度の相対指定 | `btron3/os_spec/kernel/proc.txt` |
| `P_RIGHT` | `3` | 右方向 への移動 | `btron3/os_spec/shell/parts.txt` |
| `P_RUN` | `0x8000` | 実行状態 | `btron3/os_spec/kernel/proc.txt` |
| `P_SBAR` | `0x0040` | スクロールバー指定 | `btron3/os_spec/shell/parts.txt` |
| `P_SGL` | `1` | 有効なシングルクリック | `btron3/os_spec/shell/parts.txt` |
| `P_SIZE` | `(MAX_PTR_SIZE * MAX_PTR_SIZE) / 8` |  | `btron3/os_spec/dp/pointer_func.txt` |
| `P_SMASK` | `0x0fff` | 終了／中断状態のマスク | `btron3/os_spec/shell/parts.txt` |
| `P_SMOOTH` | `0` | スムース移動 | `btron3/os_spec/shell/parts.txt` |
| `P_TAB` | `0` | タブで終了 | `btron3/os_spec/shell/parts.txt` |
| `P_TASK` | `0x0002` | タスクを対象とする | `btron3/os_spec/kernel/proc.txt` |
| `P_TYPE` | `0x001f` | タイプマスク | `btron3/os_spec/shell/parts.txt` |
| `P_UP` | `0` | 上方向 への移動 | `btron3/os_spec/shell/parts.txt` |
| `P_WAIT` | `0x2000` | 待ち状態 | `btron3/os_spec/kernel/proc.txt` |
| `PrntMsgSz` | `(sizeof(PrntMsg) - sizeof(W) * 2)` |  | `brightv/device/primgdrv.txt` |
| `RAW` | `1` | 1 文字ずつの生入力 | `brightv/device/console.txt` |
| `RAWEVT` | `0x0020` | メニュー用(絶対座標) | `btron3/os_spec/shell/window.txt` |
| `RCVBUFSZ` | `0x83` | 入力バッファサイズ: GET のみ | `brightv/device/console.txt` |
| `RCVTMO` | `0x82` | 受信タイムアウト(ミリ秒) -1: なし | `brightv/device/console.txt` |
| `RELNM_LEN` | `32` | 続柄名のバイト長 | `btron3/os_spec/shell/omgr.txt` |
| `RM_DATABOX` | `0x00000400  データボックスレコード` |  | `brightv/library/standard/tad.txt` |
| `RM_DFUSEN` | `0x00000040  指定付箋レコード` |  | `brightv/library/standard/tad.txt` |
| `RM_DICT` | `0x00001000  辞書データレコード` |  | `brightv/library/standard/tad.txt` |
| `RM_FFUSEN` | `0x00000080  機能付箋レコード` |  | `brightv/library/standard/tad.txt` |
| `RM_FONT` | `0x00000800  フォントデータレコード` |  | `brightv/library/standard/tad.txt` |
| `RM_LINK` | `0x00000001  リンクレコード` |  | `brightv/library/standard/tad.txt` |
| `RM_MFUSEN` | `0x00000100  実行機能付箋レコード` |  | `brightv/library/standard/tad.txt` |
| `RM_PROG` | `0x00000200  実行プログラムレコード` |  | `brightv/library/standard/tad.txt` |
| `RM_SFUSEN` | `0x00000020  設定付箋レコード` |  | `brightv/library/standard/tad.txt` |
| `RM_SYSDATA` | `0x00008000  システムデータレコード` |  | `brightv/library/standard/tad.txt` |
| `RM_SYSRSV1` | `0x00002000  システム予約 1` |  | `brightv/library/standard/tad.txt` |
| `RM_SYSRSV2` | `0x00004000  システム予約 2` |  | `brightv/library/standard/tad.txt` |
| `RM_TADCMT` | `0x00000004  TAD注釈レコード` |  | `brightv/library/standard/tad.txt` |
| `RM_TADDATA` | `0x00000002  TAD主レコード` |  | `brightv/library/standard/tad.txt` |
| `RM_TADRSV` | `0x00000010  予約レコード` |  | `brightv/library/standard/tad.txt` |
| `RM_TADSUB` | `0x00000008  TAD補助レコード` |  | `brightv/library/standard/tad.txt` |
| `RS_PORT` | `2` | 標準 RS ポート | `brightv/device/console.txt` |
| `RT_DATABOX` | `10   データボックスレコード` |  | `brightv/library/standard/tad.txt` |
| `RT_DFUSEN` | `6   指定付箋レコード` |  | `brightv/library/standard/tad.txt` |
| `RT_DICT` | `12   辞書レコード` |  | `brightv/library/standard/tad.txt` |
| `RT_FFUSEN` | `7   機能付箋レコード` |  | `brightv/library/standard/tad.txt` |
| `RT_FONT` | `11   フォントレコード` |  | `brightv/library/standard/tad.txt` |
| `RT_LINK` | `0   リンクレコード` |  | `brightv/library/standard/tad.txt` |
| `RT_MFUSEN` | `8   実行機能付箋レコード` |  | `brightv/library/standard/tad.txt` |
| `RT_PROG` | `9   実行プログラムレコード` |  | `brightv/library/standard/tad.txt` |
| `RT_SFUSEN` | `5   設定付箋レコード` |  | `brightv/library/standard/tad.txt` |
| `RT_SYSDATA` | `15   システムデータレコード` |  | `brightv/library/standard/tad.txt` |
| `RT_SYSRSV1` | `13   システム予約 1` |  | `brightv/library/standard/tad.txt` |
| `RT_SYSRSV2` | `14   システム予約 2` |  | `brightv/library/standard/tad.txt` |
| `RT_TADCMT` | `2   TAD注釈レコード` |  | `brightv/library/standard/tad.txt` |
| `RT_TADDATA` | `1   TAD主レコード` |  | `brightv/library/standard/tad.txt` |
| `RT_TADRSV` | `4   予約レコード` |  | `brightv/library/standard/tad.txt` |
| `RT_TADSUB` | `3   TAD補助レコード` |  | `brightv/library/standard/tad.txt` |
| `ResumeKBPD` | `(0x10000001)` |  | `brightv/device/kbpd.txt` |
| `SB_PARTS` | `4` | シリアルボックス | `btron3/os_spec/shell/parts.txt` |
| `SEL_POLYRGN` | `0x4000` | 多角形選択領域 | `btron3/os_spec/shell/omgr.txt` |
| `SEM_EXCL` | `0x4000` | 排他制御用セマフォ | `btron3/os_spec/kernel/taskcomm.txt` |
| `SEM_SYNC` | `0x0000` | 同期用セマフォ | `btron3/os_spec/kernel/taskcomm.txt` |
| `SET_BUZ_BEEP` | `0x0100` | 音色：標準の警告音 | `btron3/os_spec/kernel/event.txt` |
| `SET_BUZ_KEYOFF` | `0x0104` | 音色：キーOFFのクリック音 | `btron3/os_spec/kernel/event.txt` |
| `SET_BUZ_KEYON` | `0x0103` | 音色：キーONのクリック音 | `btron3/os_spec/kernel/event.txt` |
| `SET_BUZ_PDOFF` | `0x0102` | 音色：ボタンOFFのクリック音 | `btron3/os_spec/kernel/event.txt` |
| `SET_BUZ_PDON` | `0x0101` | 音色：ボタンONのクリック音 | `btron3/os_spec/kernel/event.txt` |
| `SET_BUZ_VOLUME` | `0x0110` | 音量 | `btron3/os_spec/kernel/event.txt` |
| `SHARED_IRQ` | `(0x100)` |  | `brightv/device/hwresmgr.txt` |
| `SIOCATMARK` | `(0x40000000 \| (4 << 16) \| ('s' << 8) \| 7)` |  | `brightv/diff/diff.txt` |
| `SIZE_DFUSENSEG(dlen)` | `(66 + dlen)` |  | `brightv/library/standard/tad.txt` |
| `SIZE_FUSENSEG(dlen)` | `(96 + dlen)` |  | `brightv/library/standard/tad.txt` |
| `SIZE_VOBJSEG(dlen)` | `(30 + dlen)` |  | `brightv/library/standard/tad.txt` |
| `SNDBUFSZ` | `0x84` | 出力バッファサイズ: GET のみ | `brightv/device/console.txt` |
| `SNDTMO` | `0x81` | 送信タイムアウト(ミリ秒) -1: なし | `brightv/device/console.txt` |
| `SOCK_DGRAM` | `2` | datagram socket | `btron3/os_spec/shell/tcpip.txt` |
| `SOCK_RAW` | `3` | raw-protocol interface | `btron3/os_spec/shell/tcpip.txt` |
| `SOCK_STREAM` | `1` | stream socket | `btron3/os_spec/shell/tcpip.txt` |
| `SOL_SOCKET` | `0xffff` | options for socket level | `btron3/os_spec/shell/tcpip.txt` |
| `SO_BROADCAST` | `0x0020` | permit sending of broadcast msgs | `btron3/os_spec/shell/tcpip.txt` |
| `SO_DEBUG` | `0x0001` | turn on debugging info recording | `btron3/os_spec/shell/tcpip.txt` |
| `SO_DONTROUTE` | `0x0010` | just use interface addresses | `btron3/os_spec/shell/tcpip.txt` |
| `SO_ERROR` | `0x1007` | get error status and clear | `btron3/os_spec/shell/tcpip.txt` |
| `SO_KEEPALIVE` | `0x0008` | keep connections alive | `btron3/os_spec/shell/tcpip.txt` |
| `SO_LINGER` | `0x0080` | linger on close if data present | `btron3/os_spec/shell/tcpip.txt` |
| `SO_OOBINLINE` | `0x0100` | leave received OOB data in line | `btron3/os_spec/shell/tcpip.txt` |
| `SO_RCVBUF` | `0x1002` | receive buffer size | `btron3/os_spec/shell/tcpip.txt` |
| `SO_REUSEADDR` | `0x0004` | allow local address reuse | `btron3/os_spec/shell/tcpip.txt` |
| `SO_SNDBUF` | `0x1001` | send buffer size | `btron3/os_spec/shell/tcpip.txt` |
| `SO_TYPE` | `0x1008` | get socket type | `btron3/os_spec/shell/tcpip.txt` |
| `SPEC_BTRON` | `0x2000` | BTRON | `brightv/library/standard/basic.txt` |
| `SPEC_CTRON` | `0x3000` | CTRON | `brightv/library/standard/basic.txt` |
| `SPEC_ITRON` | `0x1000` | ITRON | `brightv/library/standard/basic.txt` |
| `SPEC_MAGIC(n)` | `( (n) & 0xf000 )` |  | `brightv/library/standard/basic.txt` |
| `SPEC_TRON` | `0x0000` | TRON 共通 (TAD等) | `brightv/library/standard/basic.txt` |
| `SPEC_VER(n)` | `( (n) & 0x0fff )` |  | `brightv/library/standard/basic.txt` |
| `SPEC_mBTRON` | `0x6000` | μBTRON | `brightv/library/standard/basic.txt` |
| `SPEC_mITRON` | `0x5000` | μITRON | `brightv/library/standard/basic.txt` |
| `SQRT2` | `1.41421356237309504880` |  | `brightv/library/standard/bmath.txt` |
| `SS_PARTS` | `10` | スクロールセレクタ | `btron3/os_spec/shell/parts.txt` |
| `S_UNDEF` | `0x8000` | 負の最大値 | `btron3/os_spec/shell/parts.txt` |
| `ScanRateCmd(rate)` | `(0x01000000 \| (rate))` |  | `brightv/device/kbpd.txt` |
| `SenseCmd(sense)` | `(0x02000000 \| (sense))` |  | `brightv/device/kbpd.txt` |
| `SuspendKBPD` | `(0x10000000)` |  | `brightv/device/kbpd.txt` |
| `TADREQ` | `0x14` |  | `btron3/os_spec/shell/omgr.txt` |
| `TA_CONFORM` | `0x00040000` | 保護レベル継承 | `brightv/itron/itron386.txt` |
| `TA_INTGATE` | `0x00000000` | 割込ゲート使用 | `brightv/itron/itron386.txt` |
| `TA_RNG0` | `0x00000000` | リングレベル0 で動作させる | `brightv/itron/general.txt` |
| `TA_RNG1` | `0x01000000` | リングレベル1 で動作させる | `brightv/itron/general.txt` |
| `TA_RNG2` | `0x02000000` | リングレベル2 で動作させる | `brightv/itron/general.txt` |
| `TA_RNG3` | `0x03000000` | リングレベル3 で動作させる | `brightv/itron/general.txt` |
| `TA_SSTKSZ` | `0x00010000` | システムスタックサイズを指定 | `brightv/itron/general.txt` |
| `TA_TASKSPACE` | `0x00100000` | タスク固有空間を指定 | `brightv/itron/itron386.txt` |
| `TA_TRAPGATE` | `0x00080000` | トラップゲート使用 | `brightv/itron/itron386.txt` |
| `TA_USERSTACK` | `0x00080000` | ユーザースタックを指定 | `brightv/itron/itron386.txt` |
| `TB_PARTS` | `1` | テキストボックス | `btron3/os_spec/shell/parts.txt` |
| `TCPU_FPU` | `0x00010000` | FPU 付 | `brightv/itron/itron386.txt` |
| `TCPU_G100` | `0x00000009` | Gmicro/100 | `brightv/itron/itron386.txt` |
| `TCPU_G200` | `0x0000000a` | Gmicro/200 | `brightv/itron/itron386.txt` |
| `TCPU_G300` | `0x0000000b` | Gmicro/300 | `brightv/itron/itron386.txt` |
| `TCPU_G400` | `0x00000018` | Gmicro/400 | `brightv/itron/itron386.txt` |
| `TCPU_G500` | `0x00000019` | Gmicro/500 | `brightv/itron/itron386.txt` |
| `TCPU_Pentium` | `0x00000065` | Pentium シリーズ | `brightv/itron/itron386.txt` |
| `TCPU_PentiumPro` | `0x00000066` | PentiumPro シリーズ | `brightv/itron/itron386.txt` |
| `TCPU_i386` | `0x00000063` | i386 SX/DX シリーズ | `brightv/itron/itron386.txt` |
| `TCPU_i486` | `0x00000064` | i486 SX/DX シリーズ | `brightv/itron/itron386.txt` |
| `TCPU_ix86` | `0x00000060` | Intel x86 系 | `brightv/itron/itron386.txt` |
| `TCPU_unknown` | `0` | CPU 未定義 | `brightv/itron/itron386.txt` |
| `TCP_MAXSEG` | `0x0002` | maximum segment size | `brightv/diff/diff.txt` |
| `TCP_NODELAY` | `0x0001` | immediate send() | `brightv/diff/diff.txt` |
| `TC_CR` | `0x000d      CR(改行コード)` |  | `brightv/library/standard/tad.txt` |
| `TC_ESC` | `0xff80      Escape` |  | `brightv/library/standard/tad.txt` |
| `TC_FDLM` | `0xff21` | '／'  パス名の区切り | `btron3/os_spec/kernel/file.txt` |
| `TC_FF` | `0x000c      FF(改ページコード)` |  | `brightv/library/standard/tad.txt` |
| `TC_FOWN` | `0xff23` | '≡'  自分自身(作業ファイル) | `btron3/os_spec/kernel/file.txt` |
| `TC_FSEP` | `0xff22` | '：'  出現順の区切り | `btron3/os_spec/kernel/file.txt` |
| `TC_LANG` | `0xfe00      言語指定コード` |  | `brightv/library/standard/tad.txt` |
| `TC_NC` | `0x000b      NC(改コラムコード)` |  | `brightv/library/standard/tad.txt` |
| `TC_NL` | `0x000a      NL(改段落コード)` |  | `brightv/library/standard/tad.txt` |
| `TC_NULL` | `0x0000      NULL(無効コード)` |  | `brightv/library/standard/tad.txt` |
| `TC_SP` | `0x0020      Space(スペースコード)` |  | `brightv/library/standard/tad.txt` |
| `TC_SPEC` | `0xff00      特殊コード` |  | `brightv/library/standard/tad.txt` |
| `TC_TAB` | `0x0009      TAB(タブコード)` |  | `brightv/library/standard/tad.txt` |
| `TERM_ALL` | `0x0001` | 子プロセスまで含めた強制終了 | `btron3/os_spec/kernel/proc.txt` |
| `TERM_NRM` | `0x0000` | 指定プロセスのみの強制終了 | `btron3/os_spec/kernel/proc.txt` |
| `TEST_CARDID` | `0x12345678` | テスト用特殊カード ID | `brightv/device/cardmgr.txt` |
| `TEST_MENU` | `(DB_MENU + 0)` | メニュー | `brightv/library/appl/libuse.txt` |
| `TEXTSEG_STRUCT` | `"hhhhhhhhhhhh"` |  | `brightv/library/standard/tad.txt` |
| `TEXT_DATA` | `6` | 文字列データ | `btron3/os_spec/shell/data.txt` |
| `TEXT_ITEM` | `6` | 文字列 (1行) | `btron3/os_spec/shell/panel.txt` |
| `TIP_AUTO` | `0` | 自動変換モード | `btron3/os_spec/shell/tip.txt` |
| `TIP_CAR` | `0x4` | カレット位置が変化した | `btron3/os_spec/shell/tip.txt` |
| `TIP_CL` | `0x8` | 対象文節、または区切り位置が変化した | `btron3/os_spec/shell/tip.txt` |
| `TIP_CNV` | `0x2` | 未確定文節が変更された | `btron3/os_spec/shell/tip.txt` |
| `TIP_CNVMD` | `0x0000` | 変換モード | `btron3/os_spec/shell/tip.txt` |
| `TIP_DIC_COMMON` | `0x00000001` | 基本辞書 | `btron3/os_spec/shell/tip.txt` |
| `TIP_DIC_SINGLE` | `0x00000002` | 単漢字辞書 | `btron3/os_spec/shell/tip.txt` |
| `TIP_DIC_USR` | `0x00000004` | ユーザ辞書 | `btron3/os_spec/shell/tip.txt` |
| `TIP_DIRMD` | `0x0100` | 直接モード | `btron3/os_spec/shell/tip.txt` |
| `TIP_EVENT` | `0x80000000` | TIP 特殊イベント指定 | `brightv/diff/diff.txt` |
| `TIP_KANA` | `0x0000` | かな入力モード | `btron3/os_spec/shell/tip.txt` |
| `TIP_MANUAL` | `1` | 指定変換モード | `btron3/os_spec/shell/tip.txt` |
| `TIP_OUT` | `0x1` | 確定文節が発生した | `btron3/os_spec/shell/tip.txt` |
| `TIP_ROMAN1` | `0x0001` | ローマ字入力モード(n) | `btron3/os_spec/shell/tip.txt` |
| `TIP_ROMAN2` | `0x0002` | ローマ字入力モード(nn) | `btron3/os_spec/shell/tip.txt` |
| `TIP_YOMIMOD` | `0x80000000` | 文節の M フラグ | `btron3/os_spec/shell/tip.txt` |
| `TK_0` | `(TK_NU_0+0)` |  | `brightv/library/standard/tcode.txt` |
| `TK_1` | `(TK_NU_0+1)` |  | `brightv/library/standard/tcode.txt` |
| `TK_2` | `(TK_NU_0+2)` |  | `brightv/library/standard/tcode.txt` |
| `TK_3` | `(TK_NU_0+3)` |  | `brightv/library/standard/tcode.txt` |
| `TK_4` | `(TK_NU_0+4)` |  | `brightv/library/standard/tcode.txt` |
| `TK_5` | `(TK_NU_0+5)` |  | `brightv/library/standard/tcode.txt` |
| `TK_6` | `(TK_NU_0+6)` |  | `brightv/library/standard/tcode.txt` |
| `TK_7` | `(TK_NU_0+7)` |  | `brightv/library/standard/tcode.txt` |
| `TK_8` | `(TK_NU_0+8)` |  | `brightv/library/standard/tcode.txt` |
| `TK_9` | `(TK_NU_0+9)` |  | `brightv/library/standard/tcode.txt` |
| `TK_A` | `(TK_AL_A+0)` |  | `brightv/library/standard/tcode.txt` |
| `TK_AL_A` | `0x2341` | 'A' | `brightv/library/standard/tcode.txt` |
| `TK_AL_X` | `0x2358` | 'X' | `brightv/library/standard/tcode.txt` |
| `TK_AL_Z` | `0x235a` | 'Z' | `brightv/library/standard/tcode.txt` |
| `TK_AL_a` | `0x2361` | 'a' | `brightv/library/standard/tcode.txt` |
| `TK_AL_x` | `0x2378` | 'x' | `brightv/library/standard/tcode.txt` |
| `TK_AL_z` | `0x237a` | 'z' | `brightv/library/standard/tcode.txt` |
| `TK_AND` | `0x2175` | & | `brightv/library/standard/tcode.txt` |
| `TK_ASTR` | `0x2176` | * | `brightv/library/standard/tcode.txt` |
| `TK_ATMK` | `0x2177` | @ | `brightv/library/standard/tcode.txt` |
| `TK_B` | `(TK_AL_A+1)` |  | `brightv/library/standard/tcode.txt` |
| `TK_BS` | `0x0008` | BS | `brightv/library/standard/tcode.txt` |
| `TK_BSLH` | `0x216f` | \ | `brightv/library/standard/tcode.txt` |
| `TK_C` | `(TK_AL_A+2)` |  | `brightv/library/standard/tcode.txt` |
| `TK_CAN` | `0x0018` | CAN | `brightv/library/standard/tcode.txt` |
| `TK_CMMA` | `0x2124` | , | `brightv/library/standard/tcode.txt` |
| `TK_CNV` | `0x001e` | 変換 | `brightv/library/standard/tcode.txt` |
| `TK_COLN` | `0x2127` | : | `brightv/library/standard/tcode.txt` |
| `TK_D` | `(TK_AL_A+3)` |  | `brightv/library/standard/tcode.txt` |
| `TK_DEL` | `0x007f` | DEL | `brightv/library/standard/tcode.txt` |
| `TK_DICT` | `0x0083` | 補助 | `brightv/library/standard/tcode.txt` |
| `TK_DKON` | `0x212b` | 濁音 | `brightv/library/standard/tcode.txt` |
| `TK_DLLR` | `0x2170` | $ | `brightv/library/standard/tcode.txt` |
| `TK_DQOT` | `0x2149` | " | `brightv/library/standard/tcode.txt` |
| `TK_E` | `(TK_AL_A+4)` |  | `brightv/library/standard/tcode.txt` |
| `TK_END` | `0x0004` | CR | `brightv/library/standard/tcode.txt` |
| `TK_EQAL` | `0x2161` | = | `brightv/library/standard/tcode.txt` |
| `TK_EXCL` | `0x212a` | ! | `brightv/library/standard/tcode.txt` |
| `TK_EXOR` | `0x2130` | ^ | `brightv/library/standard/tcode.txt` |
| `TK_F` | `(TK_AL_A+5)` |  | `brightv/library/standard/tcode.txt` |
| `TK_G` | `(TK_AL_A+6)` |  | `brightv/library/standard/tcode.txt` |
| `TK_GTTN` | `0x2164` | > | `brightv/library/standard/tcode.txt` |
| `TK_H` | `(TK_AL_A+7)` |  | `brightv/library/standard/tcode.txt` |
| `TK_HK_E` | `0x2473` | ひらがな最後 | `brightv/library/standard/tcode.txt` |
| `TK_HK_S` | `0x2421` | ひらがな先頭 | `brightv/library/standard/tcode.txt` |
| `TK_HNDK` | `0x212c` | 半濁音 | `brightv/library/standard/tcode.txt` |
| `TK_I` | `(TK_AL_A+8)` |  | `brightv/library/standard/tcode.txt` |
| `TK_J` | `(TK_AL_A+9)` |  | `brightv/library/standard/tcode.txt` |
| `TK_K` | `(TK_AL_A+10)` |  | `brightv/library/standard/tcode.txt` |
| `TK_KK_E` | `0x2576` | カナカナ最後 | `brightv/library/standard/tcode.txt` |
| `TK_KK_S` | `0x2521` | カナカナ先頭 | `brightv/library/standard/tcode.txt` |
| `TK_KSP` | `0x2121` |  | `brightv/library/standard/tcode.txt` |
| `TK_L` | `(TK_AL_A+11)` |  | `brightv/library/standard/tcode.txt` |
| `TK_LABR` | `0x214e` | [ | `brightv/library/standard/tcode.txt` |
| `TK_LCBR` | `0x2150` | { | `brightv/library/standard/tcode.txt` |
| `TK_LPAR` | `0x214a` | ( | `brightv/library/standard/tcode.txt` |
| `TK_LSTN` | `0x2163` | < | `brightv/library/standard/tcode.txt` |
| `TK_M` | `(TK_AL_A+12)` |  | `brightv/library/standard/tcode.txt` |
| `TK_MINS` | `0x215d` | - | `brightv/library/standard/tcode.txt` |
| `TK_N` | `(TK_AL_A+13)` |  | `brightv/library/standard/tcode.txt` |
| `TK_NL` | `0x000a` | NL | `brightv/library/standard/tcode.txt` |
| `TK_NULL` | `0x0000` | NULL | `brightv/library/standard/tcode.txt` |
| `TK_NU_0` | `0x2330` | '0' | `brightv/library/standard/tcode.txt` |
| `TK_NU_9` | `0x2339` | '9' | `brightv/library/standard/tcode.txt` |
| `TK_O` | `(TK_AL_A+14)` |  | `brightv/library/standard/tcode.txt` |
| `TK_OR` | `0x2143` | \| | `brightv/library/standard/tcode.txt` |
| `TK_P` | `(TK_AL_A+15)` |  | `brightv/library/standard/tcode.txt` |
| `TK_PCNT` | `0x2173` | % | `brightv/library/standard/tcode.txt` |
| `TK_PLUS` | `0x215c` | + | `brightv/library/standard/tcode.txt` |
| `TK_PROD` | `0x2125` | . | `brightv/library/standard/tcode.txt` |
| `TK_Q` | `(TK_AL_A+16)` |  | `brightv/library/standard/tcode.txt` |
| `TK_QSTN` | `0x2129` | ? | `brightv/library/standard/tcode.txt` |
| `TK_R` | `(TK_AL_A+17)` |  | `brightv/library/standard/tcode.txt` |
| `TK_RABR` | `0x214f` | ] | `brightv/library/standard/tcode.txt` |
| `TK_RCBR` | `0x2151` | } | `brightv/library/standard/tcode.txt` |
| `TK_RCNV` | `0x001f` | 逆変換 | `brightv/library/standard/tcode.txt` |
| `TK_RPAR` | `0x214b` | ) | `brightv/library/standard/tcode.txt` |
| `TK_RQOT` | `0x212e` | ` | `brightv/library/standard/tcode.txt` |
| `TK_S` | `(TK_AL_A+18)` |  | `brightv/library/standard/tcode.txt` |
| `TK_SCLN` | `0x2128` | ; | `brightv/library/standard/tcode.txt` |
| `TK_SHRP` | `0x2174` | # | `brightv/library/standard/tcode.txt` |
| `TK_SLSH` | `0x213f` | / | `brightv/library/standard/tcode.txt` |
| `TK_SP0` | `0x0020` |  | `brightv/library/standard/tcode.txt` |
| `TK_SP1` | `0x00a0` |  | `brightv/library/standard/tcode.txt` |
| `TK_SQOT` | `0x2147` | ' | `brightv/library/standard/tcode.txt` |
| `TK_T` | `(TK_AL_A+19)` |  | `brightv/library/standard/tcode.txt` |
| `TK_TAB` | `0x0009` | TAB | `brightv/library/standard/tcode.txt` |
| `TK_TILD` | `0x2131` | ~ | `brightv/library/standard/tcode.txt` |
| `TK_U` | `(TK_AL_A+20)` |  | `brightv/library/standard/tcode.txt` |
| `TK_USCR` | `0x2132` | _ | `brightv/library/standard/tcode.txt` |
| `TK_V` | `(TK_AL_A+21)` |  | `brightv/library/standard/tcode.txt` |
| `TK_W` | `(TK_AL_A+22)` |  | `brightv/library/standard/tcode.txt` |
| `TK_X` | `(TK_AL_A+23)` |  | `brightv/library/standard/tcode.txt` |
| `TK_Y` | `(TK_AL_A+24)` |  | `brightv/library/standard/tcode.txt` |
| `TK_Z` | `(TK_AL_A+25)` |  | `brightv/library/standard/tcode.txt` |
| `TK_a` | `(TK_AL_a+0)` |  | `brightv/library/standard/tcode.txt` |
| `TK_b` | `(TK_AL_a+1)` |  | `brightv/library/standard/tcode.txt` |
| `TK_c` | `(TK_AL_a+2)` |  | `brightv/library/standard/tcode.txt` |
| `TK_d` | `(TK_AL_a+3)` |  | `brightv/library/standard/tcode.txt` |
| `TK_e` | `(TK_AL_a+4)` |  | `brightv/library/standard/tcode.txt` |
| `TK_f` | `(TK_AL_a+5)` |  | `brightv/library/standard/tcode.txt` |
| `TK_g` | `(TK_AL_a+6)` |  | `brightv/library/standard/tcode.txt` |
| `TK_h` | `(TK_AL_a+7)` |  | `brightv/library/standard/tcode.txt` |
| `TK_i` | `(TK_AL_a+8)` |  | `brightv/library/standard/tcode.txt` |
| `TK_j` | `(TK_AL_a+9)` |  | `brightv/library/standard/tcode.txt` |
| `TK_k` | `(TK_AL_a+10)` |  | `brightv/library/standard/tcode.txt` |
| `TK_l` | `(TK_AL_a+11)` |  | `brightv/library/standard/tcode.txt` |
| `TK_m` | `(TK_AL_a+12)` |  | `brightv/library/standard/tcode.txt` |
| `TK_n` | `(TK_AL_a+13)` |  | `brightv/library/standard/tcode.txt` |
| `TK_o` | `(TK_AL_a+14)` |  | `brightv/library/standard/tcode.txt` |
| `TK_p` | `(TK_AL_a+15)` |  | `brightv/library/standard/tcode.txt` |
| `TK_q` | `(TK_AL_a+16)` |  | `brightv/library/standard/tcode.txt` |
| `TK_r` | `(TK_AL_a+17)` |  | `brightv/library/standard/tcode.txt` |
| `TK_s` | `(TK_AL_a+18)` |  | `brightv/library/standard/tcode.txt` |
| `TK_t` | `(TK_AL_a+19)` |  | `brightv/library/standard/tcode.txt` |
| `TK_u` | `(TK_AL_a+20)` |  | `brightv/library/standard/tcode.txt` |
| `TK_v` | `(TK_AL_a+21)` |  | `brightv/library/standard/tcode.txt` |
| `TK_w` | `(TK_AL_a+22)` |  | `brightv/library/standard/tcode.txt` |
| `TK_x` | `(TK_AL_a+23)` |  | `brightv/library/standard/tcode.txt` |
| `TK_y` | `(TK_AL_a+24)` |  | `brightv/library/standard/tcode.txt` |
| `TK_z` | `(TK_AL_a+25)` |  | `brightv/library/standard/tcode.txt` |
| `TNULL` | `((TC) 0)` | 文字列終端の文字コード値 | `btron3/shared_data/data_type.txt` |
| `TR_CONT` | `0x4000` | 接続フラグ | `btron3/os_spec/shell/tray.txt` |
| `TR_FIG` | `0x3` | 図形レコード | `btron3/os_spec/shell/tray.txt` |
| `TR_TEXT` | `0x2` | 文章レコード | `btron3/os_spec/shell/tray.txt` |
| `TR_VOBJ` | `0x1` | 仮身レコード | `btron3/os_spec/shell/tray.txt` |
| `TSC_JPN1` | `0x0022` | 日本1 | `brightv/library/lang/tlang.txt` |
| `TSC_JPN2` | `0x0023` | 日本2 | `brightv/library/lang/tlang.txt` |
| `TSC_MAX` | `0x003f` |  | `brightv/library/lang/tlang.txt` |
| `TSC_MIN` | `0x0021` |  | `brightv/library/lang/tlang.txt` |
| `TSC_SYS` | `0x0021` | システム | `brightv/library/lang/tlang.txt` |
| `TS_DFUSEN` | `0xE7    指定付箋セグメント` |  | `brightv/library/standard/tad.txt` |
| `TS_FAPPL` | `0xBF    図形アプリケーション指定付箋` |  | `brightv/library/standard/tad.txt` |
| `TS_FATTR` | `0xB4    図形修飾セグメント` |  | `brightv/library/standard/tad.txt` |
| `TS_FDEF` | `0xB1    データ定義セグメント` |  | `brightv/library/standard/tad.txt` |
| `TS_FFUSEN` | `0xE8    機能付箋セグメント` |  | `brightv/library/standard/tad.txt` |
| `TS_FGRP` | `0xB2    グループ定義セグメント` |  | `brightv/library/standard/tad.txt` |
| `TS_FIG` | `0xE3    図形開始セグメント` |  | `brightv/library/standard/tad.txt` |
| `TS_FIGEND` | `0xE4    図形終了セグメント` |  | `brightv/library/standard/tad.txt` |
| `TS_FMAC` | `0xB3    マクロ定義/参照セグメント` |  | `brightv/library/standard/tad.txt` |
| `TS_FMEMO` | `0xBE    図形メモ指定付箋` |  | `brightv/library/standard/tad.txt` |
| `TS_FPAGE` | `0xB5    図形ページ割り付け指定付箋` |  | `brightv/library/standard/tad.txt` |
| `TS_FPRIM` | `0xB0    図形要素セグメント` |  | `brightv/library/standard/tad.txt` |
| `TS_IMAGE` | `0xE5    画像セグメント` |  | `brightv/library/standard/tad.txt` |
| `TS_INFO` | `0xE0    管理情報セグメント` |  | `brightv/library/standard/tad.txt` |
| `TS_SFUSEN` | `0xE9    設定付箋セグメント` |  | `brightv/library/standard/tad.txt` |
| `TS_TAPPL` | `0xAF    文章アプリケーション指定付箋` |  | `brightv/library/standard/tad.txt` |
| `TS_TATTR` | `0xA4    文字割り付け指定付箋` |  | `brightv/library/standard/tad.txt` |
| `TS_TCHAR` | `0xA3    特殊文字指定付箋` |  | `brightv/library/standard/tad.txt` |
| `TS_TEXT` | `0xE1    文章開始セグメント` |  | `brightv/library/standard/tad.txt` |
| `TS_TEXTEND` | `0xE2    文章終了セグメント` |  | `brightv/library/standard/tad.txt` |
| `TS_TFONT` | `0xA2    文字指定付箋` |  | `brightv/library/standard/tad.txt` |
| `TS_TMEMO` | `0xAE    文章メモ指定付箋` |  | `brightv/library/standard/tad.txt` |
| `TS_TPAGE` | `0xA0    文章ページ割付け指定付箋` |  | `brightv/library/standard/tad.txt` |
| `TS_TRULER` | `0xA1    行書式指定付箋` |  | `brightv/library/standard/tad.txt` |
| `TS_TSTYLE` | `0xA5    文字修飾指定付箋` |  | `brightv/library/standard/tad.txt` |
| `TS_TVAR` | `0xAD    変数参照指定付箋` |  | `brightv/library/standard/tad.txt` |
| `TS_VOBJ` | `0xE6    仮身セグメント` |  | `brightv/library/standard/tad.txt` |
| `TXT_CNV` | `0x100` | 未確定文字列が残っている | `brightv/library/appl/text.txt` |
| `TXT_DISP` | `0x8000` | 変換領域の再表示を行う | `brightv/library/appl/text.txt` |
| `TXT_EVT` | `8` | 未確定文字列を確定せずにリターン | `brightv/library/appl/text.txt` |
| `TXT_FIX` | `0x0000` | 固定領域表示 | `brightv/library/appl/text.txt` |
| `TXT_INS` | `0x1000` | 挿入表示 | `brightv/library/appl/text.txt` |
| `TXT_KEY` | `4` | 不要なキーイベントの場合リターン | `brightv/library/appl/text.txt` |
| `TXT_NOHAN` | `0x100` | 半角未サポート | `brightv/library/appl/text.txt` |
| `TXT_NOIMG` | `0x200` | イメージの復帰ができなかった | `brightv/library/appl/text.txt` |
| `TXT_NONE` | `0` | 状態の変化なし | `brightv/library/appl/text.txt` |
| `TXT_NOSCRL` | `0x10` | スクロールが必要でもリターンしない | `brightv/library/appl/text2.txt` |
| `TXT_NOWAIT` | `1` | イベントの取り出しを行わない | `brightv/library/appl/text.txt` |
| `TXT_OUT` | `1` | 確定文字列が発生した | `brightv/library/appl/text.txt` |
| `TXT_OVER` | `0x2000` | オーバーライト表示 | `brightv/library/appl/text.txt` |
| `TXT_POS` | `2` | 表示領域の終了位置が変化でリターン | `brightv/library/appl/text.txt` |
| `TXT_SCRL` | `0x10` | 表示全体のスクロールが必要である | `brightv/library/appl/text.txt` |
| `TXT_SCRLCAR` | `0x20` | カレット位置でスクロール要否を判定 | `brightv/library/appl/text2.txt` |
| `TXT_WAIT` | `0` | イベントの取り出しを行う | `brightv/library/appl/text.txt` |
| `T_FOREVER` | `(-1)` | 永久待ち | `btron3/os_spec/kernel/taskcomm.txt` |
| `T_NOWAIT` | `( 0)` | 待たない | `btron3/os_spec/kernel/taskcomm.txt` |
| `USB_BULK` | `2` |  | `brightv/device/usbmgr.txt` |
| `USB_CLEAR_FEATURE` | `1` |  | `brightv/device/usbmgr.txt` |
| `USB_CONFIGURATION` | `2` |  | `brightv/device/usbmgr.txt` |
| `USB_CONTROL` | `0` |  | `brightv/device/usbmgr.txt` |
| `USB_DEVICE` | `1` |  | `brightv/device/usbmgr.txt` |
| `USB_ENDPOINT` | `5` |  | `brightv/device/usbmgr.txt` |
| `USB_GET_CONFIGURATION` | `8` |  | `brightv/device/usbmgr.txt` |
| `USB_GET_DESCRIPTOR` | `6` |  | `brightv/device/usbmgr.txt` |
| `USB_GET_INTERFACE` | `10` |  | `brightv/device/usbmgr.txt` |
| `USB_GET_STATUS` | `0` |  | `brightv/device/usbmgr.txt` |
| `USB_INTERFACE` | `4` |  | `brightv/device/usbmgr.txt` |
| `USB_INTERRUPT` | `3` |  | `brightv/device/usbmgr.txt` |
| `USB_ISOCHRONOUS` | `1` |  | `brightv/device/usbmgr.txt` |
| `USB_MOUNT` | `1` | デバイス接続 | `brightv/device/usbmgr.txt` |
| `USB_NONE` | `0` | 対象デバイスでない | `brightv/device/usbmgr.txt` |
| `USB_OK` | `(EC_OK    15.6 USBイベント` |  | `brightv/device/usbmgr.txt` |
| `USB_OWN` | `1` | 対象デバイス | `brightv/device/usbmgr.txt` |
| `USB_SET_ADDRESS` | `5` |  | `brightv/device/usbmgr.txt` |
| `USB_SET_CONFIGURATION` | `9` |  | `brightv/device/usbmgr.txt` |
| `USB_SET_DESCRIPTOR` | `7` |  | `brightv/device/usbmgr.txt` |
| `USB_SET_FEATURE` | `3` |  | `brightv/device/usbmgr.txt` |
| `USB_SET_INTERFACE` | `11` |  | `brightv/device/usbmgr.txt` |
| `USB_STRING` | `3` |  | `brightv/device/usbmgr.txt` |
| `USB_SYNCH_FRAME` | `12` |  | `brightv/device/usbmgr.txt` |
| `USB_UNMOUNT` | `2` | デバイス切断 | `brightv/device/usbmgr.txt` |
| `USER_DATA` | `64` | ユーザ定義データ | `btron3/os_spec/shell/data.txt` |
| `USE_PIC_PID` | `0` | PIC/PID 利用しない | `brightv/library/standard/basic.txt` |
| `USE_ROM_MODULE` | `0` | ROM 化しない | `brightv/library/standard/basic.txt` |
| `VIRTUAL_ADDRESS` | `1` | 仮想記憶 | `brightv/library/standard/basic.txt` |
| `VL_PARTS` | `11` | ボリューム | `btron3/os_spec/shell/parts.txt` |
| `VM_APLDEL` | `13` | アプリ登録が削除された | `btron3/os_spec/shell/omgr.txt` |
| `VM_APLREG` | `12` | アプリ登録された | `btron3/os_spec/shell/omgr.txt` |
| `VM_ATTACH` | `11` | 接続状態となった | `btron3/os_spec/shell/omgr.txt` |
| `VM_CLOSE` | `2` | 仮身が閉じられた | `btron3/os_spec/shell/omgr.txt` |
| `VM_DETACH` | `6` | 切断状態となった | `btron3/os_spec/shell/omgr.txt` |
| `VM_DISK` | `15` | ディスク状態が表示された | `btron3/os_spec/shell/omgr.txt` |
| `VM_DISP` | `7` | 仮身の表示属性が変更された | `btron3/os_spec/shell/omgr.txt` |
| `VM_EXREQ` | `10` | 付箋起動が要求された | `btron3/os_spec/shell/omgr.txt` |
| `VM_GABAGE` | `16` | ディスク整理が起動された | `btron3/os_spec/shell/omgr.txt` |
| `VM_INFO` | `14` | 管理情報が表示された | `btron3/os_spec/shell/omgr.txt` |
| `VM_NAME` | `3` | 実身名が変更された | `btron3/os_spec/shell/omgr.txt` |
| `VM_NEW` | `5` | 新版が作成された | `btron3/os_spec/shell/omgr.txt` |
| `VM_NONE` | `0` | 何も変更されなかった | `btron3/os_spec/shell/omgr.txt` |
| `VM_OPEN` | `1` | 仮身が開かれた | `btron3/os_spec/shell/omgr.txt` |
| `VM_PASTE` | `9` | 仮身へ埋め込まれた | `btron3/os_spec/shell/omgr.txt` |
| `VM_REFMT` | `8` | 再フォーマットされた | `btron3/os_spec/shell/omgr.txt` |
| `VM_RELN` | `4` | 続柄が変更された | `btron3/os_spec/shell/omgr.txt` |
| `VOBJSEG_STRUCT` | `"hhhhhhwwwwh"` |  | `brightv/library/standard/tad.txt` |
| `VOID` | `void` | 空の型 | `btron3/shared_data/data_type.txt` |
| `V_ADJUST` | `0x40` | 全体表示の長さ | `btron3/os_spec/shell/omgr.txt` |
| `V_ADJUST1` | `0x50` | 名前表示の長さ | `btron3/os_spec/shell/omgr.txt` |
| `V_ADJUST2` | `0x60` | 続柄まで表示の長さ | `btron3/os_spec/shell/omgr.txt` |
| `V_ADJUST3` | `0x70` | 日付以外表示の長さ | `btron3/os_spec/shell/omgr.txt` |
| `V_AUTEXE` | `0x4000` | 自動起動 | `btron3/os_spec/shell/omgr.txt` |
| `V_CHECK` | `0` | 大きさのチェック | `btron3/os_spec/shell/omgr.txt` |
| `V_CHKDUP` | `0x9999` | 重複状態のチェック | `btron3/os_spec/shell/omgr.txt` |
| `V_CHKREF` | `0x8888` | 参照状態のチェック | `btron3/os_spec/shell/omgr.txt` |
| `V_CLOSE` | `0x30` | 仮身を閉じる | `btron3/os_spec/shell/omgr.txt` |
| `V_DETACH` | `0x1000` | 虚身状態 | `btron3/os_spec/shell/omgr.txt` |
| `V_DISP` | `1` | 表示エリアクリア | `btron3/os_spec/shell/omgr.txt` |
| `V_DISPALL` | `2` | 表示エリア表示 | `btron3/os_spec/shell/omgr.txt` |
| `V_DISPAREA` | `3` | 表示エリアのみ表示 | `btron3/os_spec/shell/omgr.txt` |
| `V_ERAORG` | `4` | 元を消去する | `btron3/os_spec/shell/omgr.txt` |
| `V_ERASE` | `0` | 消去する | `btron3/os_spec/shell/omgr.txt` |
| `V_FIXDEF` | `0x0010` | 固定デフォールトアプリ | `btron3/os_spec/shell/omgr.txt` |
| `V_FRAM` | `1` | 仮身枠 | `btron3/os_spec/shell/omgr.txt` |
| `V_GETSTS` | `0x8000` | 現在の状態の取り出し | `btron3/os_spec/shell/omgr.txt` |
| `V_HIDDEN` | `0x8000` | 隠蔽仮身 | `btron3/os_spec/shell/omgr.txt` |
| `V_INPROC` | `0x2000` | 処理中状態 | `btron3/os_spec/shell/omgr.txt` |
| `V_LBHD` | `6` | ハンドル (左下) | `btron3/os_spec/shell/omgr.txt` |
| `V_LINKFL` | `0x0800` | リンクファイル | `btron3/os_spec/shell/omgr.txt` |
| `V_LTHD` | `4` | ハンドル (左上) | `btron3/os_spec/shell/omgr.txt` |
| `V_NAME` | `3` | 実身名 | `btron3/os_spec/shell/omgr.txt` |
| `V_NODISP` | `0` | 表示しない | `btron3/os_spec/shell/omgr.txt` |
| `V_NOEXPND` | `0x0200` | 印刷時展開なし | `btron3/os_spec/shell/omgr.txt` |
| `V_NOFDISP` | `0x0080` | 仮身枠表示なし | `btron3/os_spec/shell/omgr.txt` |
| `V_NOFRAME` | `8` | 境界線なし | `btron3/os_spec/shell/omgr.txt` |
| `V_NOIMG` | `0x0020` | イメージ保存なし | `btron3/os_spec/shell/omgr.txt` |
| `V_NONAME` | `0x0001` | 実身名の表示なし | `btron3/os_spec/shell/omgr.txt` |
| `V_NOPICT` | `0x0040` | ピクトグラム表示なし | `btron3/os_spec/shell/omgr.txt` |
| `V_NORELN` | `0x0002` | 続柄名の表示なし | `btron3/os_spec/shell/omgr.txt` |
| `V_NOTIME` | `0x0008` | 変更日時の表示なし | `btron3/os_spec/shell/omgr.txt` |
| `V_NOTYPE` | `0x0004` | データタイプの表示なし | `btron3/os_spec/shell/omgr.txt` |
| `V_OPEN` | `0x20` | 仮身を開く | `btron3/os_spec/shell/omgr.txt` |
| `V_OPENED` | `0x0100` | オープン仮身 | `btron3/os_spec/shell/omgr.txt` |
| `V_PICT` | `2` | ピクトグラム | `btron3/os_spec/shell/omgr.txt` |
| `V_PURGE` | `0xffff` | 保存表示イメージを廃棄 | `btron3/os_spec/shell/omgr.txt` |
| `V_RBHD` | `7` | ハンドル (右下) | `btron3/os_spec/shell/omgr.txt` |
| `V_RELN` | `8` | 続柄 | `btron3/os_spec/shell/omgr.txt` |
| `V_RTHD` | `5` | ハンドル (右上) | `btron3/os_spec/shell/omgr.txt` |
| `V_SIZE` | `0x10` | 変形する | `btron3/os_spec/shell/omgr.txt` |
| `V_WORK` | `0` | 表示エリア内 | `btron3/os_spec/shell/omgr.txt` |
| `WAIEVT` | `0x0004` | メッセージおよびイベント待ち | `btron3/os_spec/kernel/message.txt` |
| `WAIT` | `0x0000` | 待つ | `btron3/os_spec/kernel/message.txt` |
| `WA_BBAR` | `0x0040` | 下スクロールバー有 | `btron3/os_spec/shell/window.txt` |
| `WA_BGDSP` | `0x0200` | 入力不可状態の時も表示を更新 | `btron3/os_spec/shell/window.txt` |
| `WA_FMOVE` | `0x4000` | オープン時、画面内に強制移動 | `btron3/os_spec/shell/window.txt` |
| `WA_FRAME` | `0x2000` | 外枠座標でウィンドウ枠を指定 | `btron3/os_spec/shell/window.txt` |
| `WA_FRONT` | `0x0001` | 前面ウィンドウ | `btron3/os_spec/shell/window.txt` |
| `WA_FULL` | `0x0400` | 全面モード | `btron3/os_spec/shell/window.txt` |
| `WA_GROUP` | `0x10000` | グループ主ウィンドウ | `brightv/diff/diff.txt` |
| `WA_HHDL` | `0x0008` | 左右変形可 | `btron3/os_spec/shell/window.txt` |
| `WA_LBAR` | `0x0080` | 左スクロールバー有 | `btron3/os_spec/shell/window.txt` |
| `WA_NOFRAME` | `0x40000` | タイトルバー無、変形不可のとき枠なし | `brightv/diff/diff.txt` |
| `WA_NORMAL` | `0x007c` | 通常 (変形可、右/下スクロールバー) | `btron3/os_spec/shell/window.txt` |
| `WA_RBAR` | `0x0020` | 右スクロールバー有 | `btron3/os_spec/shell/window.txt` |
| `WA_SFULL` | `0x0800` | フルスクリーン | `btron3/os_spec/shell/window.txt` |
| `WA_SIZE` | `0x0004` | 変形可 | `btron3/os_spec/shell/window.txt` |
| `WA_STD` | `0` | 正規座標でウィンドウ枠を指定 | `btron3/os_spec/shell/window.txt` |
| `WA_SUBW` | `0x0002` | 従属ウィンドウ | `btron3/os_spec/shell/window.txt` |
| `WA_THIN` | `0x20000` | スクロールバー無、変形可のとき細枠 | `brightv/diff/diff.txt` |
| `WA_TITL` | `0x0100` | タイトルバー無 | `btron3/os_spec/shell/window.txt` |
| `WA_VHDL` | `0x0010` | 上下変形可 | `btron3/os_spec/shell/window.txt` |
| `WA_WORK` | `0x1000` | 作業領域座標でウィンドウ枠を指定 | `btron3/os_spec/shell/window.txt` |
| `WF_AND` | `0x0000` | イベントフラグ AND 待ち | `btron3/os_spec/kernel/taskcomm.txt` |
| `WF_OR` | `0x0002` | イベントフラグ OR  待ち | `btron3/os_spec/kernel/taskcomm.txt` |
| `WHITE0` | `((PAT*)_WHITE0)` |  | `btron3/os_spec/dp/basic_concept.txt` |
| `WI_GSBGC(n)` | `(5000 + (n))` | R BG COLOR | `brightv/diff/diff.txt` |
| `WS_PARTS` | `9` | スイッチセレクタ | `btron3/os_spec/shell/parts.txt` |
| `WTNULL` | `0` | 文字列終端 | `brightv/library/lang/wtstring.txt` |
| `W_ACK` | `0x40` | 要求に対する ACK 応答 | `btron3/os_spec/shell/window.txt` |
| `W_BBAR` | `9` | 下スクロールバー | `btron3/os_spec/shell/window.txt` |
| `W_CLICK` | `2` |  | `btron3/os_spec/shell/window.txt` |
| `W_CLOSED` | `129` | ウィンドウのクローズ | `btron3/os_spec/shell/window.txt` |
| `W_DCLICK` | `3` |  | `btron3/os_spec/shell/window.txt` |
| `W_DELETE` | `2` | ウィンドウのクローズ要求 | `btron3/os_spec/shell/window.txt` |
| `W_FINISH` | `3` | 処理の終了要求 | `btron3/os_spec/shell/window.txt` |
| `W_FRAM` | `1` | ウィンドウ枠 | `btron3/os_spec/shell/window.txt` |
| `W_FULL` | `1` | 全面モード | `btron3/os_spec/shell/window.txt` |
| `W_HOLD` | `2` | 座標系を移動する(イメージを固定) | `btron3/os_spec/shell/window.txt` |
| `W_LBAR` | `10` | 左スクロールバー | `btron3/os_spec/shell/window.txt` |
| `W_LBHD` | `6` | ハンドル (左下) | `btron3/os_spec/shell/window.txt` |
| `W_LTHD` | `4` | ハンドル (左上) | `btron3/os_spec/shell/window.txt` |
| `W_MOVE` | `0` | 座標系を固定する(イメージを移動) | `btron3/os_spec/shell/window.txt` |
| `W_MOVEC` | `1` | 座標系を固定する(イメージを移動) | `btron3/os_spec/shell/window.txt` |
| `W_NAK` | `0x80` | 要求に対する NACK 応答 | `btron3/os_spec/shell/window.txt` |
| `W_NOOVL` | `2` | オーバーラップ時はスクロールしない | `btron3/os_spec/shell/window.txt` |
| `W_NORM` | `0` | 通常モード | `btron3/os_spec/shell/window.txt` |
| `W_OPENED` | `5` | 子ウィンドウのオープン通知 | `btron3/os_spec/shell/window.txt` |
| `W_PASTE` | `1` | データの貼り込み要求 | `btron3/os_spec/shell/window.txt` |
| `W_PICT` | `2` | ピクトグラム | `btron3/os_spec/shell/window.txt` |
| `W_PRESS` | `0` |  | `btron3/os_spec/shell/window.txt` |
| `W_QPRESS` | `1` |  | `btron3/os_spec/shell/window.txt` |
| `W_RBAR` | `8` | 右スクロールバー | `btron3/os_spec/shell/window.txt` |
| `W_RBHD` | `7` | ハンドル (右下) | `btron3/os_spec/shell/window.txt` |
| `W_RDSET` | `4` | 再表示領域を設定する | `btron3/os_spec/shell/window.txt` |
| `W_REDISP` | `0` | ウィンドウの再表示要求 | `btron3/os_spec/shell/window.txt` |
| `W_RTHD` | `5` | ハンドル (右上) | `btron3/os_spec/shell/window.txt` |
| `W_SCRL` | `1` | 座標系を移動する | `btron3/os_spec/shell/window.txt` |
| `W_SWITCH` | `128` | 入力受付状態の移行 | `btron3/os_spec/shell/window.txt` |
| `W_TITL` | `3` | タイトル部 | `btron3/os_spec/shell/window.txt` |
| `W_VALID` | `1` | スクロール表示は正当 | `btron3/os_spec/shell/window.txt` |
| `W_WORK` | `0` | 作業領域内 | `btron3/os_spec/shell/window.txt` |
| `XB_PARTS` | `2` | シークレットテキストボックス | `btron3/os_spec/shell/parts.txt` |
| `bmR_CLASS` | `0x20` |  | `brightv/device/usbmgr.txt` |
| `bmR_DEVICE` | `0x00` |  | `brightv/device/usbmgr.txt` |
| `bmR_ENDPOINT` | `0x02` |  | `brightv/device/usbmgr.txt` |
| `bmR_IN` | `0x80` |  | `brightv/device/usbmgr.txt` |
| `bmR_INTERFACE` | `0x01` |  | `brightv/device/usbmgr.txt` |
| `bmR_OTHER` | `0x03` |  | `brightv/device/usbmgr.txt` |
| `bmR_OUT` | `0x00` |  | `brightv/device/usbmgr.txt` |
| `bmR_STANDARD` | `0x00` |  | `brightv/device/usbmgr.txt` |
| `bmR_VENDOR` | `0x40` |  | `brightv/device/usbmgr.txt` |
| `h_addr` | `h_addr_list[0]` | address, for backward compatiblity | `btron3/os_spec/shell/tcpip.txt` |
| `isAZone(c)` | `inZone(c, 0x2121, 0x7e7e)` | A ゾーン内 | `brightv/library/lang/tlang.txt` |
| `isBZone(c)` | `inZone(c, 0x8021, 0xfd7e)` | B ゾーン内 | `brightv/library/lang/tlang.txt` |
| `isCZone(c)` | `inZone(c, 0x2180, 0x7efd)` | C ゾーン内 | `brightv/library/lang/tlang.txt` |
| `isDZone(c)` | `inZone(c, 0x8080, 0xfdfd)` | D ゾーン内 | `brightv/library/lang/tlang.txt` |
| `ret_int()` | `{ return; }` |  | `brightv/itron/general.txt` |
| `ret_tmr()` | `{ return; }` |  | `brightv/itron/general.txt` |
| `ret_wup(tskid)` | `{ wup_tsk(tskid); return; }` |  | `brightv/itron/general.txt` |
| `wtcchar(wtc)` | `((WTC)(wtc) & 0x0000ffff)` |  | `brightv/library/lang/wtstring.txt` |
| `wtclang(wtc)` | `((WTC)(wtc) >> 16)` |  | `brightv/library/lang/wtstring.txt` |

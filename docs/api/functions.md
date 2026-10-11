# API 一覧（仕様書からの抽出）

`tools/spec/extract_api.py` が仕様文書から機械的に抽出したもの。**手で編集しない。**

抽出元の仕様文書そのものはリポジトリに含めない（`docs/design-issues.md` §1）。
ここにあるのはインタフェースの事実（関数名・シグネチャ・概要）だけで、解説文は含まない。

関数 1000 件 / セクション 67 件


## 周辺核

### 1.9 時計管理

`btron3/os_spec/kernel/clk.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `get_tim` | `ERR get_tim(STIME* time, TIMEZONE* tz)` | システム時間の取得 |
| `set_tim` | `ERR set_tim(STIME time, TIMEZONE* tz)` | システム時間の設定 |
| `get_tod` | `ERR get_tod(DATE_TIM* dt, STIME time, Bool local)` | 日時の取得／変換 |
| `set_tod` | `ERR set_tod(DATE_TIM* dt, STIME* time, Bool local)` | 日時の設定／変換 |

### 1.8 デバイス管理

`btron3/os_spec/kernel/device.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `opn_dev` | `WERR opn_dev(TC* dev, W o_mode, W* error)` | デバイスのオープン |
| `cls_dev` | `ERR cls_dev(W dd, W eject, W* error)` | デバイスのクローズ |
| `rea_dev` | `ERR rea_dev(W dd, W start, B* buf, W size, W* a_size, W* error)` | デバイスのデータ読み込み |
| `wri_dev` | `ERR wri_dev(W dd, W start, B* buf, W size, W* a_size, W* error)` | デバイスへのデータ書き込み |
| `chg_dmd` | `ERR chg_dmd(TC* dev, W mode)` | デバイスのアクセスモード変更 |
| `dev_sts` | `ERR dev_sts(TC* dev, DEV_STATE* buf)` | デバイスの管理情報の取得 |
| `get_dev` | `WERR get_dev(TC* dev, W devno)` | デバイス名の取得 |
| `lst_dev` | `WERR lst_dev(DEV_INFO* dev, W ndev)` | 登録済みデバイスの取得 |
| `sus_dev` | `WERR sus_dev(UW mode)` | サスペンド要求 |

### 1.7 イベント管理

`btron3/os_spec/kernel/event.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `get_evt` | `WERR get_evt(W t_mask, EVENT* evt, W opt)` | イベントの取得 |
| `put_evt` | `ERR put_evt(EVENT* evt, W opt)` | イベントの発生 |
| `clr_evt` | `ERR clr_evt(W t_mask, W last_mask)` | イベントのクリア |
| `get_pdp` | `WERR get_pdp(PNT* pos)` | PD位置の取得 |
| `set_pdp` | `WERR set_pdp(PNT pos)` | PD位置の設定 |
| `get_etm` | `ERR get_etm(UW* time)` | イベントタイマー値の取得 |
| `get_kmp` | `ERR get_kmp(KeyMap keymap)` | キー状態の取得 |
| `chg_emk` | `WERR chg_emk(W mask)` | システムイベントマスクの変更 |
| `set_krp` | `ERR set_krp(W offset, W interval)` | 自動リピート間隔の設定 |
| `get_krp` | `ERR get_krp(W* offset, W* interval)` | 自動リピート間隔の取得 |
| `set_krm` | `ERR set_krm(KeyMap keymap)` | 自動リピート対象キーの設定 |
| `get_krm` | `ERR get_krm(KeyMap keymap)` | 自動リピート対象キーの取得 |
| `get_kid` | `ERR get_kid(KBD_ID* id)` | キーボードIDの取得 |
| `get_ktb` | `WERR get_ktb(KeyTab* keytab)` | 文字コード変換表の取得 |
| `set_ktb` | `ERR set_ktb(KeyTab* keytab)` | 文字コード変換表の設定 |
| `chg_pda` | `WERR chg_pda(W atr)` | ポインティングデバイス属性の変更 |
| `sig_buz` | `ERR sig_buz(W type)` | ブザー音の発生 |
| `ctl_buz` | `ERR ctl_buz(W kind, UW *val)` | ブザー音の制御 |
| `req_evt` | `ERR req_evt(W t_mask)` | イベントメッセージ要求 |
| `las_evt` | `WERR las_evt(W t_mask)` | 最終イベント発生からの経過時間の取得 |

### 1.6 ファイル管理

`btron3/os_spec/kernel/file.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `get_lnk` | `WERR get_lnk(TC *path, LINK *lnk, W mode)` | ファイルのリンク獲得 |
| `chg_wrk` | `ERR chg_wrk(LINK *lnk)` | 作業ファイル変更 |
| `cre_fil` | `WERR cre_fil(LINK *lnk, TC *name, A_MODE *mode, UH atype, W opt)` | ファイル生成 |
| `cre_lnk` | `ERR cre_lnk(LINK *lnk, F_LINK *ref, W opt)` | リンクファイルの生成 |
| `gen_fil` | `WERR gen_fil(LINK *lnk, TC *name, F_STATE *stat, F_LINK *ref, W opt)` | ファイルの直接生成 |
| `opn_fil` | `WERR opn_fil(LINK *lnk, W o_mode, TC *pwd)` | ファイルのオープン |
| `cls_fil` | `ERR cls_fil(W fd)` | ファイルのクローズ |
| `del_fil` | `WERR del_fil(LINK *org, LINK *lnk, W force)` | ファイルの削除 |
| `see_rec` | `ERR see_rec(W fd, W offset, W mode, W *recnum)` | 現在レコード移動 |
| `fnd_rec` | `WERR fnd_rec(W fd, W mode, UW typemask, UH subtype, W *recnum)` | レコード検索 |
| `fnd_lnk` | `ERR fnd_lnk(W fd, W mode, LINK *lnk, UH subtype, W *recnum)` | リンクレコード検索 |
| `rea_rec` | `WERR rea_rec(W fd, W offset, B *buf, W size, W *r_size, UH *subtype)` | レコード読み込み |
| `wri_rec` | `ERR wri_rec(W fd, W offset, B *buf, W size, W *r_size, UH *subtype, UW units)` | レコード書き込み |
| `ins_rec` | `ERR ins_rec(W fd, B *buf, W size, W type, UH subtype, UW units)` | レコード挿入 |
| `apd_rec` | `ERR apd_rec(W fd, B *buf, W size, W type, UH subtype, UW units)` | レコード追加 |
| `del_rec` | `WERR del_rec(W fd)` | レコード削除 |
| `trc_rec` | `ERR trc_rec(W fd, W size)` | レコードサイズ縮小 |
| `xch_fil` | `ERR xch_fil(W fd_1, W fd_2)` | ファイルの内容交換 |
| `loc_rec` | `ERR loc_rec(W fd, W mode)` | レコードロック |
| `chk_fil` | `WERR chk_fil(LINK *lnk, W mode, TC *pwd)` | ファイルアクセス権チェック |
| `chg_fmd` | `ERR chg_fmd(LINK *lnk, A_MODE *mode)` | ファイルアクセスモード変更 |
| `chg_fat` | `ERR chg_fat(LINK *lnk, W attr)` | ファイルのアクセス属性変更 |
| `chg_fpw` | `ERR chg_fpw(W fd, TC *pwd)` | ファイルパスワード変更 |
| `chg_fnm` | `ERR chg_fnm(LINK *lnk, TC *name)` | ファイル名変更 |
| `chg_ftm` | `ERR chg_ftm(LINK *lnk, F_TIME *times)` | ファイル日時変更 |
| `fil_sts` | `WERR fil_sts(LINK *lnk, TC *name, F_STATE *stat, F_LOCATE *locat)` | ファイル情報の取得 |
| `ofl_sts` | `WERR ofl_sts(W fd, TC *name, F_STATE *stat, F_LOCATE *locat)` | ファイル情報の取得 |
| `lnk_sts` | `WERR lnk_sts(LINK *lnk, F_LINK *stat)` | リンクファイル情報の取得 |
| `syn_lnk` | `WERR syn_lnk(LINK *lnk, W opt)` | リンクファイルの同期 |
| `get_dfm` | `ERR get_dfm(DA_MODE *mode)` | デフォールトアクセスモードの取得 |
| `set_dfm` | `ERR set_dfm(DA_MODE *mode)` | デフォールトアクセスモードの設定 |
| `att_fls` | `ERR att_fls(TC *dev, TC *name, LINK *lnk, UW mode)` | ファイルシステムの接続 |
| `det_fls` | `ERR det_fls(TC *dev, W eject)` | ファイルシステムの切断 |
| `syn_fls` | `ERR syn_fls(VOID)` | ファイルシステムの同期 |
| `fls_sts` | `WERR fls_sts(TC *dev, FS_STATE *buff)` | ファイルシステム管理情報の取得 |
| `chg_fls` | `ERR chg_fls(TC *dev, TC *fs_name, TC *fs_locate)` | ファイルシステム情報の変更 |
| `get_nlk` | `WERR get_nlk(LINK *lnk)` | リンクの順次取得 |
| `lst_fls` | `WERR lst_fls(F_ATTACH *buff, W cnt)` | ファイルシステムの取得 |
| `map_rec` | `WERR map_rec(W fd, W offset, B **addr, W size, W mode)` | レコードのマップ |
| `ump_rec` | `ERR ump_rec(W fd, W mapid)` | レコードのアンマップ |
| `chg_fsm` | `WERR chg_fsm( TC *dev, UW mode )` | ファイルシステム接続モードの変更 |

### 1.4 グローバル名管理

`btron3/os_spec/kernel/gname.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `cre_nam` | `WERR cre_nam(TC* name, W data, W opt)` | グローバル名データ生成 |
| `del_nam` | `ERR del_nam(TC* name)` | グローバル名データ削除 |
| `get_nam` | `ERR get_nam(TC* name, W* data)` | グローバル名データ取得 |

### 1.5 メモリ管理

`btron3/os_spec/kernel/memory.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `get_mbk` | `ERR get_mbk( VP *adr, W nblk, UW atr )` | メモリブロックの獲得 |
| `rel_mbk` | `ERR rel_mbk( VP adr )` | メモリブロックの解放 |
| `mbk_sts` | `ERR mbk_sts( M_STATE *sts )` | メモリ状態参照 |
| `malloc` | `void* malloc( size_t size )` | 非常駐ローカルメモリの獲得(ライブラリ) |
| `calloc` | `void* calloc( size_t nelem, size_t elsize )` | 非常駐ローカルメモリの獲得(ライブラリ) |
| `realloc` | `void* realloc( void *ptr, size_t size )` | 非常駐ローカルメモリの再獲得(ライブラリ) |
| `free` | `void free( void *ptr )` | 非常駐ローカルメモリの解放(ライブラリ) |
| `Smalloc` | `void* Smalloc( size_t size )` | 非常駐共有メモリの獲得(ライブラリ) |
| `Scalloc` | `void* Scalloc( size_t nelem, size_t elsize )` | 非常駐共有メモリの獲得(ライブラリ) |
| `Srealloc` | `void* Srealloc( void *ptr, size_t size )` | 非常駐共有メモリの再獲得(ライブラリ) |
| `Sfree` | `void Sfree( void *ptr )` | 非常駐共有メモリの解放(ライブラリ) |
| `Kmalloc` | `void* Kmalloc( size_t size )` | 常駐システムメモリの獲得(ライブラリ) |
| `Kcalloc` | `void* Kcalloc( size_t nelem, size_t elsize )` | 常駐システムメモリの獲得(ライブラリ) |
| `Krealloc` | `void* Krealloc( void *ptr, size_t size )` | 常駐システムメモリの再獲得(ライブラリ) |
| `Kfree` | `void Kfree( void *ptr )` | 常駐システムメモリの解放(ライブラリ) |
| `Vmalloc` | `void* Vmalloc( size_t size )` | 非常駐システムメモリの獲得(ライブラリ) |
| `Vcalloc` | `void* Vcalloc( size_t nelem, size_t elsize )` | 非常駐システムメモリの獲得(ライブラリ) |
| `Vrealloc` | `void* Vrealloc( void *ptr, size_t size )` | 非常駐システムメモリの再獲得(ライブラリ) |
| `Vfree` | `void Vfree( void *ptr )` | 非常駐システムメモリの解放(ライブラリ) |

### 1.2 メッセージ管理

`btron3/os_spec/kernel/message.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `snd_msg` | `ERR snd_msg(W pid, MESSAGE* msg, W opt)` | メッセージ送信 |
| `rcv_msg` | `WERR rcv_msg(W t_mask, MESSAGE* msg, W msgsz, W opt)` | メッセージ受信 |
| `clr_msg` | `ERR clr_msg(W t_mask, W last_mask)` | メッセージクリア |
| `req_tmg` | `ERR req_tmg(W time, W code)` | タイムアウトメッセージ要求 |
| `can_tmg` | `ERR can_tmg()` | タイムアウトメッセージ取り消し |
| `def_msg` | `ERR def_msg(W t_mask, FP msghdr)` | メッセージハンドラ定義 |
| `ret_msg` | `ERR ret_msg(W ret)` | メッセージハンドラ終了 |
| `brk_msg` | `ERR brk_msg()` | イベント発生通知 |

### 1.1 プロセス / タスク管理

`btron3/os_spec/kernel/proc.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `cre_prc` | `WERR cre_prc(LINK* lnk, W pri, MESSAGE* msg)` | プロセス生成 / 実行 |
| `ext_prc` | `VOID ext_prc(W code)` | プロセス終了 |
| `ter_prc` | `ERR ter_prc(W pid, W code, W opt)` | 他プロセス強制終了 |
| `chg_pri` | `WERR chg_pri(W id, W pri, W opt)` | プロセス / タスク優先度変更 |
| `prc_sts` | `WERR prc_sts(W pid, P_STATE* buff, TC* name)` | プロセス状態取得 |
| `chg_usr` | `ERR chg_usr(P_USER* buff)` | 自プロセスユーザ情報変更 |
| `get_usr` | `WERR get_usr(W pid, P_USER* buff)` | ユーザ情報取得 |
| `get_inf` | `ERR get_inf(W pid, P_INFO* buff)` | プロセス統計情報取得 |
| `cre_tsk` | `WERR cre_tsk(FP entry, W pri, W arg)` | サブタスク生成 / 実行 |
| `ext_tsk` | `VOID ext_tsk()` | 自タスク終了 |
| `ter_tsk` | `ERR ter_tsk(W tskid)` | 他タスク強制終了 |
| `slp_tsk` | `ERR slp_tsk(W time)` | タスク起床待ち |
| `wup_tsk` | `ERR wup_tsk(W tskid)` | タスク起床 |
| `can_wup` | `WERR can_wup(W tskid)` | タスク起床要求のキャンセル |
| `dly_tsk` | `ERR dly_tsk(W time)` | タスク遅延 |
| `get_tid` | `W get_tid()` | 自タスク ID の取得 |

### 1.10 システム管理機能

`btron3/os_spec/kernel/system.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `def_exc` | `ERR def_exc(W exckind, FP exchdr)` | 例外処理ハンドラ定義 |
| `ret_exc` | `VOID ret_exc(W ret)` | 例外処理ハンドラ終了 |
| `get_ver` | `ERR get_ver(T_VER* version)` | バージョンの取得 |
| `get_cnf` | `WERR get_cnf(TC* name, VP value, W len)` | コンフィグレーション情報の取得 |

### 1.3 プロセス/タスク間同期通信管理

`btron3/os_spec/kernel/taskcomm.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `cre_sem` | `WERR cre_sem(W cnt, UW opt)` | セマフォの生成 |
| `del_sem` | `ERR del_sem(ID id)` | セマフォの削除 |
| `sig_sem` | `ERR sig_sem(ID id)` | セマフォへ資源の返却 |
| `wai_sem` | `ERR wai_sem(ID id, TMOUT tmo)` | セマフォから資源の獲得 |
| `cre_flg` | `WERR cre_flg(W ptn, UW opt)` | イベントフラグの生成 |
| `del_flg` | `ERR del_flg(ID id)` | イベントフラグの削除 |
| `set_flg` | `ERR set_flg(ID id, W ptn)` | イベントフラグをセット |
| `clr_flg` | `ERR clr_flg(ID id, W ptn)` | イベントフラグをクリア |
| `wai_flg` | `WERR wai_flg(ID id, W ptn, UW mode, TMOUT tmo)` | イベントフラグ待ち |
| `cre_mbf` | `WERR cre_mbf(W bufsz, W max, UW opt)` | メッセージバッファの生成 |
| `del_mbf` | `ERR del_mbf(ID id)` | メッセージバッファの削除 |
| `snd_mbf` | `ERR snd_mbf(ID id, VP msg, W sz, TMOUT tmo)` | メッセージバッファへ送信 |
| `rcv_mbf` | `WERR rcv_mbf(ID id, VP msg, TMOUT tmo)` | メッセージバッファから受信 |
| `cre_por` | `WERR cre_por(W maxcmsz, W maxrmsz, UW opt)` | ランデブポートの生成 |
| `del_por` | `ERR del_por(ID id)` | ランデブポートの削除 |
| `cal_por` | `WERR cal_por(ID id, UW calptn, VP msg, W cmsz, TMOUT tmo)` | ランデブ呼出 |
| `acp_por` | `WERR acp_por(ID id, UW acpptn, W *rdv, VP msg, TMOUT tmo)` | ランデブ受付 |
| `fwd_por` | `ERR fwd_por(ID id, UW calptn, W rdv, VP msg, W cmsz)` | ランデブ回送 |
| `rpl_rdv` | `ERR rpl_rdv(W rdv, VP msg, W rmsz)` | ランデブ返答 |


## ディスプレイプリミティブ

### 2.3 基本関数

`btron3/os_spec/dp/basic_func.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `gini_dsp` | `ERR gini_dsp (W mode)` | Initialize Display Primitive |
| `gfin_dsp` | `ERR gfin_dsp ( )` | Finish Display Primitive |
| `gcls_env` | `ERR gcls_env ( GID gid )` | Delete Drawing Environment |
| `gopn_dev` | `GID gopn_dev ( TC *dev, B *par )` | Open Devide Drawing Environment |
| `gopn_mem` | `GID gopn_mem ( TC *dev, BMP *bmap, B *par )` | Open Memory Drawing Environment |
| `gnew_env` | `GID gnew_env ( GID gid )` | Open New Drawing Environment |
| `gini_env` | `ERR gini_env ( GID gid )` | Initialize Drawing Environment |
| `gget_spc` | `ERR gget_spc ( TC *dev, DEVSPEC *buf )` | Get Device Specification |
| `gget_dev` | `ERR gget_dev ( W gid, TC dev[11] )` | Get Device Name |
| `gget_dsp` | `ERR gget_dsp ( W gid, DEVSPEC *buf )` | Get Device Specification |
| `gget_csp` | `ERR gget_csp ( W gid, CSPEC *buf )` | Get Color Specification |
| `gset_bcs` | `ERR gset_bcs ( GID gid, CSPEC *cspec )` | Set Bitmap Convert Color Specification |
| `gget_bcs` | `ERR gget_bcs ( GID gid, CSPEC *cspec )` | Get Bitmap Convert Color Specification |
| `gset_col` | `W gset_col ( GID gid, UW p, COLOR *cv, UW cnt )` | Set Color Map Entries |
| `gget_col` | `W gget_col ( GID gid, UW p, COLOR OUT *cv, UW cnt )` | Get Color Map Entries |
| `gcnv_col` | `W gcnv_col ( GID gid, COLOR cv, PIXVAL OUT *pixv )` | Convert Color to Pixel Value |
| `gset_msk` | `ERR gset_msk ( GID gid, PIXVAL pixv )` | Set Mask Pixel Value |
| `gget_msk` | `ERR gget_msk ( GID gid, PIXVAL OUT *pixv )` | Get Mask Pixel Value |
| `gget_clp` | `ERR gget_clp ( GID gid, GRGN *clip, W csize, W *a_size )` | Get Clipping Region |
| `gset_clp` | `ERR gset_clp ( GID gid, GRGN *clip )` | Set Clipping Region |
| `gget_fra` | `ERR gget_fra ( GID gid, RECT *rp )` | Get Frame Rectangle |
| `gset_fra` | `ERR gset_fra ( GID gid, RECT r )` | Set Frame Rectangle |
| `gget_vis` | `ERR gget_vis ( GID gid, RECT *rp )` | Get Visible Rectangle |
| `gset_vis` | `ERR gset_vis ( GID gid, RECT r )` | Set Visible Rectangle |
| `gget_for` | `ERR gget_for ( GID gid, RLIST *rlp )` | Get Foreground Rectangle List |
| `gset_for` | `ERR gset_for ( GID gid, RLIST *rlp )` | Set Foreground Rectangle List |
| `gsiz_pat` | `ERR gsiz_pat ( GID gid, UW kind, UW hsize, UW vsize, W *size )` | Get Pattern Image Size |
| `gcre_pat` | `ERR gcre_pat ( GID gid, PAT *def, UB *loc, UB **pat, W *size、W cvt )` | Create Pattern Image |
| `gset_lmk` | `ERR gset_lmk ( GID gid, UW nbytes, UB *mask )` | Set Line Mask |
| `gget_lmk` | `W gget_lmk ( GID gid, UB *mask )` | Get Line Mask |
| `gget_bmp` | `ERR gget_bmp ( GID gid, BMP *bmap )` | Get Bitmap |
| `gget_bnd` | `ERR gget_bnd ( GID gid, RECT *rp )` | Get Bitmap Bounds |
| `gmov_bnd` | `ERR gmov_bnd ( GID gid, W dh, W dv )` | Move Bitmap Bounds |
| `gmov_cor` | `ERR gmov_cor ( GID gid, W dh, W dv )` | Move Coordination |
| `gcnv_abs` | `ERR gcnv_abs ( GID gid, PNT *pp )` | Convert to Absolute Coordination |
| `gcnv_rel` | `ERR gcnv_rel ( GID gid, PNT *pp )` | Convert to Relative Coordination |
| `gloc_env` | `ERR gloc_env ( GID gid W lock )` | Lock Drawing Environment |

### 2.5 文字(列)描画関数

`btron3/os_spec/dp/character_func.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `gset_chp` | `ERR gset_chp ( GID gid, W x, W y, Bool abs )` | 文字描画位置の設定 |
| `gget_chp` | `ERR gget_chp ( GID gid, W *x, W *y )` | 文字描画位置の取り出し |
| `gset_chc` | `ERR gset_chc ( GID gid, COLOR fgc, COLOR bgc )` | 文字描画カラーの設定 |
| `gget_chc` | `ERR gget_chc ( GID gid, COLOR *fgc, COLOR *bgc )` | 文字描画カラーの取り出し |
| `gset_chd` | `ERR gset_chd ( GID gid, W dir, CGAP *gap )` | 文字描画方向／間隔の設定 |
| `gget_chd` | `W gget_chd ( GID gid, CGAP OUT *gap )` | 文字描画方向／間隔の取り出し |
| `gset_fon` | `ERR gset_fon ( GID gid, FSSPEC *fnt )` | 文字フォントの指定 |
| `gget_fon` | `ERR gget_fon ( GID gid, FSSPEC *fnt, FNTINFO *inf )` | 文字フォント情報の取り出し |
| `gset_scr` | `ERR gset_scr ( GID gid, SCRIPT script)` | 文字セット(SCRIPT)の指定 |
| `gget_scr` | `ERR gget_scr ( GID gid, SCRIPT *script)` | 文字セット(SCRIPT)の取り出し |
| `gget_chw` | `WERR gget_chw ( GID gid, TC ch )` | 文字幅の取り出し |
| `gget_chh` | `WERR gget_chh ( GID gid, TC ch )` | 文字高さの取り出し |
| `gget_stw` | `WERR gget_stw ( GID gid, TC *str, W len, CGAP *gap, W *pos )` | 文字列幅の取り出し |
| `gget_sth` | `WERR gget_sth ( GID gid, TC *str, W len, CGAP *gap, W *pos )` | 文字列高さの取り出し |
| `gget_str` | `WERR gget_str ( GID gid, TC *str, W len, CGAP *gap, PNT *chp, RECT *cr, RECT *dr, W *alen)` | 文字列描画領域の取り出し |
| `gdra_chr` | `WERR gdra_chr ( GID gid, TC ch, DCM mode )` | 文字の描画 |
| `gdra_chp` | `WERR gdra_chp ( GID gid, W x, W y, TC ch, DCM mode )` | 文字の描画(位置指定) |
| `gdra_str` | `WERR gdra_str ( GID gid, TC *str, W len, DCM mode )` | 文字列の描画 |
| `gdra_stp` | `WERR gdra_stp ( GID gid, W x, W y, TC *str, W len, DCM mode )` | 文字列の描画(位置指定) |
| `gcop_chr` | `ERR gcop_chr ( GID gid, SIZE asize, SIZE isize, UB *img, DCM mode )` | 文字イメージの描画 |

### 2.4 図形描画関数

`btron3/os_spec/dp/figure_func.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `gdra_lin` | `ERR gdra_lin ( GID gid, PNT p1, PNT p2, LATTR attr, PAT *pat, DCM mode )` | 直線の描画 |
| `gtst_pnt` | `Bool gtst_pnt ( GID gid, PNT p, PIXVAL *val )` | 点の状態の取り出し |
| `gdra_pnt` | `ERR gdra_pnt ( GID gid, PNT p, PIXVAL val, DCM mode )` | 点の描画 |
| `gfil_pnt` | `ERR gfil_pnt ( GID gid, PNT p, PAT *pat, DCM mode )` | 点のパターン描画 |
| `gfra_rec` | `ERR gfra_rec ( GID gid, RECT r, LATTR attr, PAT *pat, DEG angle, DCM mode )` | 長方形の枠の描画 |
| `gfil_rec` | `ERR gfil_rec ( GID gid, RECT r, PAT *pat, DEG angle, DCM mode )` | 長方形の塗り潰し |
| `gfra_rrc` | `ERR gfra_rrc ( GID gid, RECT r, UW rh, UW rv, LATTR attr, PAT *pat, DEG angle, DCM mode )` | 角丸長方形の枠の描画 |
| `gfil_rrc` | `ERR gfil_rrc ( GID gid, RECT r, UW rh, UW rv, PAT *pat, DEG angle, DCM mode )` | 角丸長方形の塗り潰し |
| `gfra_ovl` | `ERR gfra_ovl ( GID gid, RECT r, LATTR attr, PAT *pat, DEG angle, DCM mode )` | 楕円(円)の枠の描画 |
| `gfil_ovl` | `ERR gfil_ovl ( GID gid, RECT r, PAT *pat, DEG angle, DCM mode )` | 楕円(円)の塗り潰し |
| `gdra_arc` | `ERR gdra_arc ( GID gid, RECT r, PNT sp, PNT ep, LATTR attr, PAT *pat, DEG angle, DCM mode )` | 弧の描画 |
| `gfra_sec` | `ERR gfra_sec ( GID gid, RECT r, PNT sp, PNT ep, LATTR attr, PAT *pat, DEG angle, DCM mode )` | 扇形の枠の描画 |
| `gfil_sec` | `ERR gfil_sec ( GID gid, RECT r, PNT sp, PNT ep, PAT *pat, DEG angle, DCM mode )` | 扇形の塗り潰し |
| `gfra_cho` | `ERR gfra_cho ( GID gid, RECT r, PNT sp, PNT ep, LATTR attr, PAT *pat, DEG angle, DCM mode )` | 弓形の枠の描画 |
| `gfil_cho` | `ERR gfil_cho ( GID gid, RECT r, PNT sp, PNT ep, PAT *pat, DEG angle, DCM mode )` | 弓形の塗り潰し |
| `gdra_pln` | `ERR gdra_pln ( GID gid, POLY *p, LATTR attr, PAT *pat, DCM mode )` | 直線列の描画 |
| `gfra_pol` | `ERR gfra_pol ( GID gid, POLY *p, LATTR attr, PAT *pat, DCM mode )` | 多角形の枠の描画 |
| `gfil_pol` | `ERR gfil_pol ( GID gid, POLY *p, PAT *pat, DCM mode )` | 多角形の塗り潰し |
| `gfil_gen` | `ERR gfil_gen ( GID gid, GRGN *rgn, PAT *pat, DCM mode )` | 任意領域の塗り潰し |
| `gfil_rgn` | `Bool gfil_rgn ( GID gid, PIXVAL val, PNT p, PAT *pat, DCM mode )` | 任意閉領域の塗り潰し |
| `gdra_spl` | `ERR gdra_spl ( GID gid, UW np, PNT *pt, LATTR attr, PAT *pat, DCM mode )` | スプライン曲線の描画 |
| `gfil_spl` | `ERR gfil_spl ( GID gid, UW np, PNT *pt, PAT *pat, DCM mode )` | 閉スプライン曲線領域の塗り潰し |
| `gcop_bmp` | `ERR gcop_bmp ( GID srcid, RECT *sr, GID dstid, RECT *dr, GRGN *mask, DCM mode )` | ビットマップ間のデータ転送 |
| `gsav_bmp` | `ERR gsav_bmp ( GID gid, RECT *gr, BMP *bmap, RECT *br, GRGN *mask, DCM mode )` | ビットマップの保存 |
| `grst_bmp` | `ERR grst_bmp ( GID gid, RECT *gr, BMP *bmap, RECT *br, GRGN *mask, DCM mode )` | ビットマップの回復 |
| `grst_mbm` | `ERR grst_mbm ( GID gid, RECT *gr, BMP *bmap, RECT *br, UB *mask, DCM mode )` | ビットマップの回復 |
| `grot_bmp` | `ERR grot_bmp ( GID srcid, GID dstid, RECT *rp, PNT *dp, DEG angle, GRGN *mask, DCM mode )` | ビットマップの回転 |
| `gscr_rec` | `W gscr_rec ( GID gid, RECT r, W dh, W dv RLIST *rlp, PAT *pat )` | 長方形領域のスクロール |
| `gsiz_cbm` | `ERR gsiz_cbm ( GID gid, RECT *rp, W compac, W *size )` | 圧縮ビットマップのサイズ計算 |
| `gcom_bmp` | `Bool gcom_bmp ( GID gid, RECT *rp, CBMP *cbmp, W *stat )` | ビットマップの圧縮 |
| `gunc_bmp` | `ERR gunc_bmp ( GID gid, RECT *dr, CBMP *cbmp, RECT *sr, DCM mode )` | 圧縮ビットマップの復元 |
| `gpic_lin` | `Bool gpic_lin ( PNT pt, PNT p0, PNT p1, UW width )` | 直線のピッキング |
| `gpic_rec` | `Bool gpic_rec ( PNT pt, RECT r, DEG angle, UW width )` | 長方形のピッキング |
| `gpic_rrc` | `Bool gpic_rrc ( PNT pt, RECT r, UW rh, UW rv, DEG angle, UW width )` | 角丸長方形のピッキング |
| `gpic_ovl` | `Bool gpic_ovl ( PNT pt, RECT r, DEG angle, UW width )` | 楕円(円)のピッキング |
| `gpic_arc` | `Bool gpic_arc ( PNT pt, RECT r, PNT sp, PNT ep, DEG angle, UW width )` | 弧のピッキング |
| `gpic_sec` | `Bool gpic_sec ( PNT pt, RECT r, PNT sp, PNT ep, DEG angle, UW width )` | 扇形のピッキング |
| `gpic_cho` | `Bool gpic_cho ( PNT pt, RECT r, PNT sp, PNT ep, DEG angle, UW width )` | 弓形のピッキング |
| `gpic_pln` | `Bool gpic_pln ( PNT pt, POLY *poly, UW width )` | 直線列のピッキング |
| `gpic_pol` | `Bool gpic_pol ( PNT pt, POLY *poly, UW width )` | 多角形のピッキング |
| `gpic_gen` | `Bool gpic_gen ( PNT pt, GRGN *rgn )` | 任意図形のピッキング |
| `gpic_spl` | `Bool gpic_spl ( PNT p, UW np, PNT *pt, UW width )` | スプライン曲線のピッキング |

### 2.6 ポインタ制御関数

`btron3/os_spec/dp/pointer_func.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `gini_ptr` | `ERR gini_ptr ( )` | ポインタの初期化 |
| `gdsp_ptr` | `ERR gdsp_ptr ( W req )` | ポインタの表示／消去 |
| `gset_ptr` | `ERR gset_ptr ( PTRSTL style, PTRIMG *img, COLOR fgcol, COLOR bgcol )` | ポインタ形状／カラーの設定 |
| `gmov_ptr` | `ERR gmov_ptr ( PNT pos )` | ポインタ表示位置の移動 |
| `gmot_ptr` | `ERR gmot_ptr ( W sts )` | ポインタ動作の設定 |
| `gget_ptr` | `W gget_ptr ( PTRSTS *sts, PTRIMG *img )` | ポインタ状態／形状の取り出し |


## 外殻

### 3.6 データマネージャ

`btron3/os_spec/shell/data.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `dopn_dat` | `W dopn_dat(LINK *lnk)` | データボックスのオープン |
| `dcls_dat` | `ERR dcls_dat(W fid)` | データボックスのクローズ |
| `dget_dtp` | `ERR dget_dtp(W type, W dnum, void **datap)` | データ項目の参照 |
| `dget_siz` | `W dget_siz(B *addr)` | データ項目のサイズ取出し |
| `dget_num` | `W dget_num(B *addr, W *type)` | データ番号／タイプIDの取出し |
| `ddef_dat` | `ERR ddef_dat(W type, W dnum, void *ptr, W size)` | システムデータ項目の再定義 |
| `ddef_ldt` | `ERR ddef_ldt(W type, W dnum, void *ptr, W size)` | ローカルデータ項目の登録 |

### 3.9 フォントマネージャ

`btron3/os_spec/shell/font_mgr.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `fdef_fnt` | `WERR fdef_fnt(FLOC loc, W spec)` | フォントデータの登録 |
| `fdel_fnt` | `ERR fdel_fnt(FID fid)` | フォントデータの削除 |
| `fdel_loc` | `WERR fdel_loc(FLOC loc, W spec)` | フォントデータの所在による削除 |
| `fget_def` | `WERR fget_def(FID fid, FDEF *def)` | フォント定義データの取り出し |
| `fget_not` | `WERR fget_not(FID fid, B *buff, UW len)` | フォント注釈データの取り出し |
| `flst_fon` | `WERR flst_fon(FID fid, W mode, FLIST *buff, UW len)` | フォントの一覧取り出し |
| `fopn_fon` | `WERR fopn_fon()` | フォントセットの生成 |
| `fcls_fon` | `ERR fcls_fon(W fdesc)` | フォントセットの削除 |
| `fset_fon` | `ERR fset_fon(W fdesc, FSSPEC *spec)` | フォントセットの設定 |
| `fget_fon` | `ERR fget_fon(W fdesc, FSSPEC *spec)` | フォントセットの取り出し |
| `fset_ang` | `ERR fset_ang(W fdesc, W ang);` | 回転角度の設定 |
| `fget_ang` | `WERR fget_ang(W fdesc);` | 回転角度の取り出し |
| `fget_fam` | `WERR fget_fam(W fdesc, W script, FNTINFO *inf);` | 文字フォント情報の取り出し |
| `fget_img` | `WERR fget_img(W fdesc, FDATA *cimg, W size, W script, TC ch, UW mode)` | 文字イメージ情報の取り出し |

### 3.2 メニューマネージャ

`btron3/os_spec/shell/menu.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `mcre_men` | `MNID mcre_men(W nitem, MENUITEM *item, MENUDISP *attr)` | 標準メニューの登録 |
| `mopn_men` | `MNID mopn_men(W dnum)` | 標準メニューのデータボックスからの登録 |
| `mdel_men` | `ERR mdel_men(W mid)` | 標準メニューの削除 |
| `msel_men` | `W msel_men(W mid, PNT pos)` | 標準メニューの選択動作 |
| `mfnd_key` | `W mfnd_key(W mid, TC ch)` | キーマクロの検索 |
| `mget_itm` | `W mget_itm(W mid, W pnum, MENUITEM *item)` | 標準メニュー項目の取出し |
| `mset_itm` | `ERR mset_itm(W mid, W pnum, MENUITEM *item)` | 標準メニュー項目の設定 |
| `mchg_atr` | `W mchg_atr(W mid, W selnum, UW mode)` | 標準メニューの項目属性の変更 |
| `mchg_dsp` | `W mchg_dsp(MENUDISP *attr, W posattr)` | 標準メニューのデフォルト表示属性の変更 |
| `mcre_gmn` | `MNID mcre_gmn(GMENU *gm)` | 汎用メニューの登録 |
| `mopn_gmn` | `MNID mopn_gmn(W dnum)` | 汎用メニューのデータボックスからの登録 |
| `mdel_gmn` | `ERR mdel_gmn(W mid)` | 汎用メニューの削除 |
| `msel_gmn` | `W msel_gmn(W mid, PNT pos)` | 汎用メニューの選択動作 |
| `mchg_gat` | `W mchg_gat(W mid, W num, UW mode)` | 汎用メニューの項目属性の変更 |
| `mchg_dtm` | `W mchg_dtm(W time)` | 標準メニューのディレイタイムの変更 |

### 3.8 実身／仮身マネージャ

`btron3/os_spec/shell/omgr.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `oreg_vob` | `VID oreg_vob(VLINK *vlnk, VP vseg, W wid, UW disp)` | 仮身の登録 |
| `b_odup_vob` | `VID b_odup_vob(W org)` | 仮身の複製 |
| `odel_vob` | `W odel_vob(W vid, W clr)` | 仮身の登録削除 |
| `ocls_wnd` | `ERR ocls_wnd(W wid)` | ウィンドウのクローズ処理 |
| `odsp_vob` | `W odsp_vob(W vid, RECT *r, UW disp)` | 仮身の表示 |
| `odsp_vor` | `W odsp_vor(W vid, RECT *r, UW disp, PNT scal)` | 仮身の倍率表示 |
| `odra_vob` | `ERR odra_vob(VLINK *vlnk, VP vseg, W wid, UW disp)` | 仮身の一時的表示 |
| `odra_vor` | `ERR odra_vor(VLINK *vlnk, VP vseg, W wid, UW disp, PNT scal)` | 仮身の一時的倍率表示 |
| `ofnd_vob` | `W ofnd_vob(W wid, PNT pos, W *vid)` | 仮身の位置検索 |
| `omov_vob` | `ERR omov_vob(W vid, W wid, RECT *newr, UW disp)` | 仮身の移動 |
| `orsz_vob` | `ERR orsz_vob(W vid, RECT *newr, UW mode)` | 仮身の変形 |
| `ochg_chs` | `W ochg_chs(W vid, W chsz, RECT *newr, UW mode)` | 仮身の文字サイズの変更 |
| `ochg_col` | `ERR ochg_col(W vid, COLOR frcol, COLOR chcol, COLOR tbcol, COLOR bgcol, UW disp)` | 仮身表示色の変更 |
| `ochg_nam` | `W ochg_nam(W vid, TC *name)` | 実身名の変更 |
| `ochg_rel` | `W ochg_rel(W vid, W index)` | 続柄の変更 |
| `onew_obj` | `VID onew_obj(W vid, VLINK *vlnk)` | 新版作成 |
| `ocre_obj` | `W ocre_obj(W vid, TC *name, W *nvid, LINK *lnk, W copy)` | 新規実身の生成 |
| `odsp_inf` | `ERR odsp_inf(W vid)` | 管理情報の表示 |
| `odsk_inf` | `ERR odsk_inf(W vid, W used, W gabage)` | ディスク情報の表示 |
| `oget_vob` | `W oget_vob(W vid, VLINK *vlnk, VP vseg, UW size, UW *rsize)` | 仮身の取り出し |
| `ochg_sts` | `W ochg_sts(W vid, UW mode)` | 仮身の状態の変更 |
| `osta_prc` | `ERR osta_prc(W vid, W wid)` | 仮身の実行処理の開始 |
| `oend_prc` | `GID oend_prc(W vid, VP dat, W update)` | 仮身の実行処理の終了 |
| `oend_req` | `ERR oend_req(W vid, W stat)` | 仮身の要求処理の終了 |
| `oreq_prc` | `W oreq_prc(W wid, W req, B *resp)` | 仮身要求イベントの送信 |
| `orsp_prc` | `ERR orsp_prc(EVENT *evt, B *resp, UW size, W status)` | 仮身要求イベントの応答 |
| `oput_dat` | `ERR oput_dat(W vid)` | 仮身への貼り込み |
| `oupd_fil` | `ERR oupd_fil(W vid, LINK *lnk, W update)` | ファイル更新の通知 |
| `oset_tmf` | `ERR oset_tmf(LINK *lnk)` | 小物メニューファイルの設定 |
| `oget_men` | `ERR oget_men(W vid, TC **emenu, TC **tmenu, TC **vmenu, TC **fmenu)` | 仮身のメニュー項目の取り出し |
| `oget_vmn` | `ERR oget_vmn(W vid, TC **emenu, TC **tmenu, TC **vmenu, TC **omenu, TC **dmenu, TC **fmenu)` | 仮身のメニュー項目の分割取り出し |
| `oexe_vmn` | `W oexe_vmn(W vid, W item, VP buf)` | 仮身／付箋操作メニューの実行 |
| `ochg_env` | `W ochg_env(LINK *lnk, W wid)` | 実行環境ファイルの変更 |
| `oget_env` | `WID oget_env(LINK *lnk)` | 実行環境ファイルの取り出し |
| `oreg_apg` | `ERR oreg_apg(LINK *lnk)` | アプリケーションプログラムの登録 |
| `odel_apg` | `ERR odel_apg(LINK *lnk)` | アプリケーションプログラムの登録削除 |
| `oset_sea` | `W oset_sea(W cnt, LINK *lnk)` | 検索対象ファイルシステムの設定 |
| `oget_sea` | `W oget_sea(W cnt, LINK *lnk)` | 検索対象ファイルシステムの取り出し |
| `ofnd_apg` | `W ofnd_apg(VLINK *vlnk, LINK *lnk, TC *dtype, W cnt)` | アプリケーションプログラムの検索 |
| `oexe_apg` | `W oexe_apg(W vid, W item)` | アプリケーションプログラムの実行 |
| `odet_fls` | `ERR odet_fls(TC *dev, W eject)` | ファイルシステムの切断 |
| `ofdt_fls` | `ERR ofdt_fls(TC *dev, W eject)` | ファイルシステムの切断(強制) |
| `oatt_fls` | `ERR oatt_fls(TC *dev, TC *name, LINK *lnk, W ronly)` | ファイルシステムの接続 |
| `ofat_fls` | `ERR ofat_fls(TC *dev, TC *name, LINK *lnk, W ronly)` | ファイルシステムの接続(強制) |
| `odet_vob` | `W odet_vob(W vid, TC *dev)` | 仮身の切断 |
| `oatt_vob` | `W oatt_vob(W vid, W ronly)` | 仮身の接続 |
| `oprc_dev` | `ERR oprc_dev(EVENT *evt, TC *dev, W ronly)` | デバイス挿入イベント処理 |
| `ofmt_vob` | `W ofmt_vob(W vid)` | 仮身の再フォーマット処理 |
| `ocnv_vob` | `W ocnv_vob(W org, W vid, LINK *lnk)` | 仮身の変換 |
| `oopn_obj` | `W oopn_obj(W vid, LINK *lnk, UW omode, TC *pwd)` | 実身のオープン |
| `odel_obj` | `W odel_obj(LINK *lnk, W mode)` | 実身の削除 |
| `oget_fsn` | `ERR oget_fsn(W vid, W fd, VP dat, UW size)` | 付箋固有データの取り出し |
| `oput_fsn` | `ERR oput_fsn(W vid, W fd, VP dat)` | 付箋固有データの格納 |
| `ocre_chd` | `W ocre_chd(W vid, TC *name, W *nvid)` | 子実身の生成 |
| `otad_vob` | `W otad_vob(W vid, RECT *r, UW disp, LINK *lnk)` | 仮身の表示用TADデータ作成処理 |
| `aset_sel` | `W aset_sel(W period)` | 選択枠のチラツキ間隔設定 |
| `adsp_sel` | `ERR adsp_sel(GID gid, SEL_RGN *selp, W mode)` | 選択枠の表示 |
| `adsp_slt` | `ERR adsp_slt(GID gid, SEL_LIST *selp, W mode, W dh, W dv)` | 選択枠リストの表示 |
| `apnl_men` | `W apnl_men(W pnid, W pid, EVENT *epv)` | パネルメニュー処理 |
| `achg_bgc` | `W achg_bgc(W *mask, COLOR *color, COLOR bgc)` | ウィンドウ背景色変更処理 |

### 3.4 パネルマネージャ

`btron3/os_spec/shell/panel.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `pcre_pnl` | `PNID pcre_pnl(PANEL *pnl, PNT *p)` | パネルの生成 |
| `popn_pnl` | `PNID popn_pnl(W dnum, PNT *p)` | パネルのデータボックスからの生成 |
| `pdel_pnl` | `ERR pdel_pnl(W pnid)` | パネルの削除 |
| `pact_pnl` | `W pact_pnl(W pnid, EVENT *ev, W *itemno)` | パネルの標準動作 |
| `pget_itm` | `W pget_itm(W pnid, W itemno, PNL_ITEM *item)` | パネル項目の取出し |
| `pset_itm` | `W pset_itm(W pnid, W itemno, PNL_ITEM *item)` | パネル項目の設定 |
| `psel_box` | `W psel_box(W pnid, W itemno)` | パネルの入力ボックスの選択 |
| `pget_gid` | `GID pget_gid(W pnid)` | 描画環境IDの取出し |
| `pact_err` | `W pact_err(W kind, TC *arg1, TC *arg2, TC *arg3)` | 標準エラー警告パネル処理 |
| `pdsp_msg` | `W pdsp_msg(TC *msg)` | システムメッセージパネルの表示 |
| `pdsp_tim` | `ERR pdsp_tim(UW stat)` | システムメッセージパネルの時刻表示 |
| `pact_msg` | `ERR pact_msg(WEVENT* ev)` | システムメッセージパネルのイベント処理 |

### 3.3 パーツマネージャ

`btron3/os_spec/shell/parts.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `ccre_par` | `PAID ccre_par(W wid, PARTS *parts)` | パーツの登録 |
| `ccre_tbx` | `PAID ccre_tbx(W wid, UW ty, RECT *r, W txsize, TC *text, PARTDISP *atr)` | パーツの登録 |
| `ccre_xbx` | `PAID ccre_xbx(W wid, UW ty, RECT *r, W txsize, TC *text, PARTDISP *atr)` | パーツの登録 |
| `ccre_nbx` | `PAID ccre_nbx(W wid, UW ty, RECT *r, UW fmt, W *val, PARTDISP *atr)` | パーツの登録 |
| `ccre_sbx` | `PAID ccre_sbx(W wid, UW ty, RECT *r, TC *fmt, W nfld, W *cv, PARTDISP *atr)` | パーツの登録 |
| `ccre_asw` | `PAID ccre_asw(W wid, UW ty, RECT *r, W cv, TC *name, PARTDISP *atr)` | パーツの登録 |
| `ccre_msw` | `PAID ccre_msw(W wid, UW ty, RECT *r, TC *name, PARTDISP *atr)` | パーツの登録 |
| `ccre_pas` | `PAID ccre_pas(W wid, UW ty, RECT *r, W cv, W onp, W offp, PARTDISP *atr)` | パーツの登録 |
| `ccre_pms` | `PAID ccre_pms(W wid, UW ty, RECT *r, W onp, PARTDISP *atr)` | パーツの登録 |
| `ccre_sel` | `PAID ccre_sel(W wid, UW ty, RECT *r, W cv, TC *name, PARTDISP *atr)` | パーツの登録 |
| `ccre_scr` | `PAID ccre_scr(W wid, UW ty, RECT *r, W cv, TC *name, PARTDISP *atr)` | パーツの登録 |
| `ccre_vol` | `PAID ccre_vol(W wid, UW ty, RECT *r, W *val, W patnum, PARTDISP *atr)` | パーツの登録 |
| `copn_par` | `PAID copn_par(W wid, W dnum, PNT *pos)` | パーツのデータボックスからの登録 |
| `cdel_par` | `ERR cdel_par(W pid, W opt)` | パーツの削除 |
| `cdel_pwd` | `ERR cdel_pwd(W wid, W opt)` | ウィンドウ内パーツの削除 |
| `cdsp_par` | `ERR cdsp_par(W pid, UW vis)` | パーツの表示制御 |
| `cdsp_pwd` | `ERR cdsp_pwd(W wid, RECT *r, UW vis)` | ウィンドウ内パーツの表示制御 |
| `cchk_par` | `W cchk_par(W pid, PNT pos)` | パーツの位置チェック |
| `cfnd_par` | `W cfnd_par(W wid, PNT pos, W *pid)` | ウィンドウ内パーツの位置サーチ |
| `cact_par` | `W cact_par(W pid, WEVENT *ev)` | パーツの動作実行 |
| `cget_val` | `W cget_val(W pid, W size, W *value)` | パーツの値の取出し |
| `cset_val` | `W cset_val(W pid, W size, W *value)` | パーツの値の設定 |
| `cget_dat` | `ERR cget_dat(W pid, W *par)` | パーツのデータ取出し |
| `cset_dat` | `ERR cset_dat(W pid, W par)` | パーツのデータ設定 |
| `ccut_txt` | `W ccut_txt(W pid, W size, TC *buff, W cut)` | パーツの選択領域の取出し |
| `cins_txt` | `W cins_txt(W pid, PNT pos, TC *buff)` | パーツの選択領域の挿入 |
| `cget_pos` | `ERR cget_pos(W pid, RECT *r)` | パーツの位置の取出し |
| `cset_pos` | `ERR cset_pos(W pid, RECT *r)` | パーツの位置の設定 |
| `cget_sts` | `W cget_sts(W pid, W *wid)` | パーツの状態の取出し |
| `cchg_par` | `ERR cchg_par(W pid, UW cmd)` | パーツの状態の変更 |
| `cchg_pwd` | `ERR cchg_pwd(W wid, UW cmd)` | ウィンドウ内パーツの状態の変更 |
| `cidl_par` | `ERR cidl_par(W wid, PNT *pos)` | パーツの定常処理 |
| `cexe_par` | `W cexe_par(W pid, WEVENT *ev)` | パーツの単イベント実行 |

### 3.11 印刷マネージャ

`btron3/os_spec/shell/printmgr.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `rset_par` | `WERR rset_par(TC *prname, PR_PAR *prpar, LINK *fout)` | 印刷パラメータ設定 |
| `rprn_fil` | `WERR rprn_fil(TC *prname, LINK *lnk, PR_PAR *prpar, UW mode)` | 印刷の実行 |
| `rout_fil` | `ERR rout_fil(TC *prname, LINK *lnk, PR_PAR *prpar, LINK *fout)` | ファイル出力の実行 |
| `rpvw_fil` | `ERR rpvw_fil(TC *prname, LINK *lnk, PR_PAR *prpar)` | 印刷の画面表示(プレビュー) |
| `rcan_ent` | `WERR rcan_ent(TC *prname, W prid)` | 印刷登録の取り消し |
| `rget_ent` | `WERR rget_ent(TC *prname, W prid, PR_STAT *stat, W cnt)` | 印刷登録状態の取り出し |
| `rlst_prn` | `WERR rlst_prn(PR_INFO *info, W cnt)` | プリンタ一覧の取り出し |
| `rget_cnf` | `WERR rget_cnf(TC *prname, PR_CONF *cnf)` | プリンタ構成情報の取り出し |
| `rset_cnf` | `ERR rset_cnf(TC *prname, PR_CONF *cnf)` | プリンタ構成情報の設定/登録/削除 |

### 3.10 TCP/IPマネージャ

`btron3/os_spec/shell/tcpip.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `so_start` | `ERR so_start (W arg)` | TCP/IPの初期化 |
| `so_finish` | `ERR so_finish (W arg)` | TCP/IPの終了宣言 |
| `so_accept` | `WERR so_accept (W s, SOCKADDR *nam, W *namlen)` | ソケットへの接続受け付け |
| `so_bind` | `ERR so_bind (W s, SOCKADDR *nam, W namlen)` | ソケットへの名前のバインド |
| `so_close` | `ERR so_close (W s)` | ソケットを閉じる |
| `so_connect` | `ERR so_connect (W s, SOCKADDR *nam, W namlen)` | ソケットへの接続の開始 |
| `so_fcntl` | `WERR so_fcntl (W s, W cmd, W arg)` | ソケットの制御 |
| `so_gethostbyname` | `ERR so_gethostbyname (B *nam, HOSTENT *hp, B *buf)` | 名前によるホスト情報の取得 |
| `so_gethostbyaddr` | `ERR so_gethostbyaddr (B *addr, W type, W len, HOSTENT *hp, B *buf)` | アドレスによるホスト情報の取得 |
| `so_getpeername` | `ERR so_getpeername (W s, SOCKADDR *nam, W *namlen)` | 接続相手の名前の取得 |
| `so_getsockname` | `ERR so_getsockname (W s, SOCKADDR *nam, W *namlen)` | ソケット名の獲得 |
| `so_getsockopt` | `ERR so_getsockopt (W s, W level, W optnam, B *optval, W *optlen)` | ソケットオプションの取得 |
| `so_ioctl` | `WERR so_ioctl (W s, W request, W arg)` | デバイスの制御 |
| `so_listen` | `ERR so_listen (W s, W backlog)` | ソケット接続のためのlisten |
| `so_read` | `WERR so_read (W s, B *buf, W len)` | ソケットからの受信 |
| `so_recv` | `WERR so_recv (W s, B *buf, W len, W flags)` | ソケットからの受信 |
| `so_recvfrom` | `WERR so_recvfrom (W s, B *buf, W len, W flags, SOCKADDR *from, W *flen)` | ソケットからの受信 |
| `so_recvmsg` | `WERR so_recvmsg (W s, MSGHDR *msg, W flags)` | ソケットからの受信 |
| `so_select` | `WERR so_select (W nfds, fd_set *rfds, fd_set *wfds, fd_set *efds, TIMEVAL *tmout)` | ソケット状態の検査 |
| `so_write` | `WERR so_write (W s, B *buf, W len)` | ソケットからのメッセージ送信 |
| `so_send` | `WERR so_send (W s, B *buf, W len, W flags)` | ソケットからのメッセージ送信 |
| `so_sendto` | `WERR so_sendto (W s, B *buf, W len, W flags, SOCKADDR *to, W tolen)` | ソケットからのメッセージ送信 |
| `so_sendmsg` | `WERR so_sendmsg (W s, MSGHDR *msg, W flags)` | ソケットからのメッセージ送信 |
| `so_shutdown` | `ERR so_shutdown (W s, W how)` | 全二重接続の部分シャットダウン |
| `so_socket` | `WERR so_socket (W domain, W type, W protocol)` | 通信用の終点の作成 |
| `so_sethostname` | `ERR so_sethostname (B *name, W nlen)` | ホスト名の設定 |

### 3.7 テキスト入力プリミティブ

`btron3/os_spec/shell/tip.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `ichg_mod` | `WERR ichg_mod(W mode)` | テキスト入力モードの変更 |
| `ichg_lrn` | `WERR ichg_lrn(UW kind, W stat)` | 学習辞書の保存状態変更 |
| `iopn_tip` | `WERR iopn_tip(TIPREC *tip, W mode)` | テキスト入力ポートのオープン |
| `icls_tip` | `ERR icls_tip(W tipid)` | テキスト入力ポートのクローズ |
| `iput_key` | `WERR iput_key(W tipid, EVENT *evt)` | テキスト入力ポートへのキー入力 |
| `iput_str` | `ERR iput_str(W tipid, TC *str)` | テキスト入力ポートへの文字列入力 |
| `ichg_blk` | `WERR ichg_blk(W intvl)` | カレットのブリンク周期の取出し／設定 |
| `idsp_car` | `ERR idsp_car(CARET *car, W mode)` | カレットの表示 |

### 3.5 トレーマネージャ

`btron3/os_spec/shell/tray.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `topn_tra` | `ERR topn_tra(void)` | トレーのオープン |
| `tcls_tra` | `ERR tcls_tra(void)` | トレーのクローズ |
| `tpsh_dat` | `W tpsh_dat(TRAYREC *data, W nrec, TC *name)` | トレーへのデータ格納 |
| `tpop_dat` | `W tpop_dat(TRAYREC *data, W size, W *a_size, W rec, TC *name)` | トレーからのデータ取り出し |
| `tdel_dat` | `W tdel_dat(void)` | トレーデータの削除 |
| `tsel_dat` | `W tsel_dat(W pos)` | トレーデータの選択 |
| `tmov_dat` | `W tmov_dat(W pos)` | トレーデータの位置変更 |
| `tget_sts` | `W tget_sts(UW *size, UW *rsize)` | トレーの状態取り出し |
| `tset_dat` | `W tset_dat(TRAYREC *data, W nrec)` | 一時トレーへのデータ格納 |
| `tget_dat` | `W tget_dat(TRAYREC *data, W size, W *a_size, W rec)` | 一時トレーからのデータ取り出し |

### 3.1 ウィンドウマネージャ

`btron3/os_spec/shell/window.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `wopn_wnd` | `WID wopn_wnd(UW attr, UW par, RECT *r, RECT *org, W pict, TC *tit, PAT *bgpat, WDDISP * atr)` | ウィンドウの生成 |
| `wopn_iwd` | `WID wopn_iwd(W gid)` | 内部ウィンドウのオープン |
| `wopn_pwd` | `WID wopn_pwd(RECT *r)` | パネルウィンドウのオープン |
| `wcls_wnd` | `ERR wcls_wnd(W wid, W opt)` | ウィンドウの削除 |
| `wchg_wnd` | `W wchg_wnd(W wid, RECT *r, W mode)` | ウィンドウの全面/通常モード切換 |
| `wmov_wnd` | `W wmov_wnd(W wid, RECT *newr)` | ウィンドウの移動 |
| `wmov_drg` | `W wmov_drg(WEVENT *evt, RECT *newr)` | ウィンドウのドラッグ移動 |
| `wrsz_wnd` | `W wrsz_wnd(W wid, RECT *newr)` | ウィンドウの変形 |
| `wrsz_drg` | `W wrsz_drg(WEVENT *evt, RECT *limit, RECT *newr)` | ウィンドウのドラッグ変形 |
| `wscr_wnd` | `W wscr_wnd(W wid, RECT *r, W dh, W dv, W mode)` | ウィンドウのスクロール |
| `wsta_dsp` | `W wsta_dsp(W wid, RECT *r, RLIST *rlst)` | ウィンドウの再表示開始 |
| `wend_dsp` | `W wend_dsp(W wid)` | ウィンドウの再表示終了 |
| `wchk_dsp` | `W wchk_dsp(W wid)` | ウィンドウの再表示状態の取出し |
| `wsta_drg` | `GID wsta_drg(W wid, W lock)` | ドラッグ処理の開始 |
| `wend_drg` | `ERR wend_drg(void)` | ドラッグ処理の終了 |
| `wget_drg` | `W wget_drg(PNT *pos, WEVENT *evt)` | PDドラッグ位置の取出し |
| `wget_evt` | `W wget_evt(WEVENT *evt, W mode)` | ウィンドウイベントの取出し |
| `wugt_evt` | `ERR wugt_evt(WEVENT *evt)` | ウィンドウイベントの戻し |
| `wpas_evt` | `ERR wpas_evt(WEVENT *evt)` | ウィンドウイベントの回送 |
| `wswi_wnd` | `ERR wswi_wnd(W wid, WEVENT *evt)` | ウィンドウの入力受付状態の切換 |
| `wsnd_evt` | `W wsnd_evt(WEVENT *evt)` | 要求イベントの送信 |
| `wrsp_evt` | `ERR wrsp_evt(WEVENT *evt, W nak)` | 要求イベントへの応答 |
| `wwai_rsp` | `W wwai_rsp(WEVENT *evt, W cmd, UW tmout)` | 要求イベントへの応答待ち |
| `wreq_dsp` | `ERR wreq_dsp(W wid)` | ウィンドウの再表示要求 |
| `wfnd_wnd` | `W wfnd_wnd(PNT *gpos, PNT *lpos, W *wid)` | ウィンドウの位置サーチ |
| `wget_act` | `WID wget_act(W *pid)` | 入力受付状態のウィンドウの取出し |
| `wget_tit` | `W wget_tit(W wid, W *pict, TC *title)` | ウィンドウタイトルの取出し |
| `wset_tit` | `ERR wset_tit(W wid, W pict, TC *title, W mode)` | ウィンドウタイトルの変更 |
| `wget_sts` | `W wget_sts(W wid, WDSTAT *stat, WDDISP *atr)` | ウィンドウ状態の取出し |
| `wget_gid` | `GID wget_gid(W wid)` | ウィンドウの描画環境IDの取出し |
| `wset_bgp` | `ERR wset_bgp(W wid, PAT *pat)` | ウィンドウの背景パターンの設定 |
| `wget_bgp` | `W wget_bgp(W wid, PAT *pat, W size)` | ウィンドウの背景パターンの取出し |
| `wera_wnd` | `ERR wera_wnd(W wid, RECT *r)` | ウィンドウの背景パターンでの塗り潰し |
| `wchg_dsp` | `ERR wchg_dsp(WDDISP *atr, PAT *bgpat)` | ウィンドウのデフォールト表示属性の変更 |
| `wget_bar` | `ERR wget_bar(W wid, W *rbar, W *bbar, W *lbar)` | スクロールバーのパーツIDの取出し |
| `wget_dat` | `ERR wget_dat(W wid, W *dat)` | ユーザデータの取出し |
| `wset_dat` | `ERR wset_dat(W wid, W dat)` | ユーザデータの設定 |
| `wget_wrk` | `ERR wget_wrk(W wid, RECT *r)` | ウィンドウ作業領域の取出し |
| `wset_wrk` | `ERR wset_wrk(W wid, RECT *r)` | ウィンドウ作業領域の変更 |
| `wget_org` | `ERR wget_org(W wid, W *parent, RECT *org)` | ウィンドウの生成元の取出し |
| `wset_org` | `ERR wset_org(W wid, W parent, RECT *org)` | ウィンドウの生成元の設定 |
| `wlst_wnd` | `W wlst_wnd(W wid, W size, W *wids)` | ウィンドウのリストアップ |
| `wget_dmn` | `ERR wget_dmn(TC **dmenu)` | 表示管理メニューの取り出し |
| `wexe_dmn` | `W wexe_dmn(W item)` | 表示管理メニューの実行 |
| `wdef_fep` | `ERR wdef_fep(W onoff)` | フロントエンドプロセスの登録 |
| `wchk_dck` | `W wchk_dck(UW first)` | ダブルクリック操作のチェック |
| `wchg_dck` | `W wchg_dck(W time)` | ダブルクリック間隔の変更 |
| `wchg_ful` | `ERR wchg_ful(void)` | フルスクリーンモードの設定 |
| `wcnv_rec` | `ERR wcnv_rec(UW attr, RECT *r, UW opt)` | 枠座標の変換 |
| `wget_inf` | `ERR wget_inf(W item, VP buf, W size)` | ウィンドウ情報レコードの取り出し |
| `wget_inf` | `ERR wset_inf(W item, VP buf, W size)` | ウィンドウ情報レコードの取り出し |


## 中心核 (I-right/V)

### 3.1 オブジェクトID

`brightv/itron/general.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `cre_tsk` | `` | タスク生成 |
| `del_tsk` | `` | タスク削除 |
| `sta_tsk` | `` | タスク起動 |
| `ext_tsk` | `` | 自タスク終了 |
| `exd_tsk` | `` | 自タスクの終了と削除 |
| `ter_tsk` | `` | 他タスク強制終了 |
| `dis_dsp` | `` | ディスパッチ禁止 |
| `ena_dsp` | `` | ディスパッチ許可 |
| `chg_pri` | `` | タスク優先度変更 |
| `rot_rdq` | `` | タスクのレディキュー回転 |
| `rel_wai` | `` | 他タスクの待ち状態解除 |
| `get_tid` | `` | 自タスクのタスクID参照 |
| `ref_tsk` | `` | タスク状態参照 |
| `sus_tsk` | `` | 他タスクを強制待ち状態へ移行 |
| `wup_tsk` | `` | 他タスクの起床 |
| `can_wup` | `` | 他タスクの起床要求を無効化 |
| `cre_sem` | `` | セマフォ生成 |
| `del_sem` | `` | セマフォ削除 |
| `sig_sem` | `` | セマフォ資源返却 |
| `ref_sem` | `` | セマフォ状態参照 |
| `cre_flg` | `` | イベントフラグ生成 |
| `del_flg` | `` | イベントフラグ削除 |
| `ref_flg` | `` | イベントフラグ状態参照 |
| `cre_mbx` | `` | メイルボックス生成 |
| `del_mbx` | `` | メイルボックス削除 |
| `snd_mbx` | `` | メイルボックスへの送信 |
| `ref_mbx` | `` | メイルボックス状態参照 |
| `cre_mbf` | `` | メッセージバッファ生成 |
| `del_mbf` | `` | メッセージバッファ削除 |
| `ref_mbf` | `` | メッセージバッファ状態参照 |
| `cre_por` | `` | ランデブ用のポート生成 |
| `del_por` | `` | ランデブ用のポート削除 |
| `fwd_por` | `` | ポートに対するランデブ回送 |
| `rpl_rdv` | `` | ランデブ返答 |
| `ref_por` | `` | ポート状態参照 |
| `def_int` | `` | 割込みハンドラ定義 |
| `ret_int` | `` | 割込みハンドラから復帰 |
| `ret_wup` | `` | 割込みハンドラ復帰とタスク起床 |
| `loc_cpu` | `` | 割込みとディスパッチの禁止 |
| `unl_cpu` | `` | 割込みとディスパッチの許可 |
| `dis_int` | `` | 割込み禁止 |
| `ena_int` | `` | 割込み許可 |
| `chg_ims` | `` | 割込みマスク変更 |
| `ref_ims` | `` | 割込みマスク参照 |
| `cre_mpl` | `` | 可変長メモリプール生成 |
| `del_mpl` | `` | 可変長メモリプール削除 |
| `rel_blk` | `` | 可変長メモリブロック返却 |
| `ref_mpl` | `` | 可変長メモリプール状態参照 |
| `cre_mpf` | `` | 固定長メモリプール生成 |
| `del_mpf` | `` | 固定長メモリプール削除 |
| `rel_blf` | `` | 固定長メモリブロック返却 |
| `rel_mpf` | `` | 固定長メモリプール状態参照 |
| `set_tim` | `` | システムクロック設定 |
| `get_tim` | `` | システムクロック参照 |
| `dly_tsk` | `` | タスク遅延 |
| `def_cyc` | `` | 周期起動ハンドラ定義 |
| `act_cyc` | `` | 周期起動ハンドラ活性制御 |
| `ref_cyc` | `` | 周期起動ハンドラ状態参照 |
| `def_alm` | `` | アラームハンドラ定義 |
| `ref_alm` | `` | アラームハンドラ状態参照 |
| `ret_tmr` | `` | タイマハンドラから復帰 |
| `get_ver` | `` | バージョン参照 |
| `ref_chg` | `` | コンフィグレーション情報参照 |
| `def_svc` | `` | 拡張SVCハンドラ定義 |
| `def_exc` | `` | 例外ハンドラ定義 |
| `E_OACV` | `` | オブジェクトアクセス権違反 |


## ライブラリ

### 2.15 色変換

`brightv/library/appl/cnvcol.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `SetDevCSPEC` | `ERR SetDevCSPEC( CPSEC *cs, DEV_SPEC *ds, COLOR *colmap )` | DEV_SPECからCPSECを設定 |
| `SetImageCSPEC` | `ERR SetImageCSPEC(CSPEC *cs, UH attr, UH info[4], COLOR *colmap)` | 画像セグメントからCSPEC設定 |
| `toRGB` | `COLOR toRGB(COLOR pix, CSPEC *cspec)` | ピクセル値をRGBに変換 |
| `toPixel` | `COLOR toPixel(COLOR rgb, CSPEC *cspec)` | RGB値をピクセル値に変換 |
| `NearestColor` | `COLOR NearestColor(COLOR rgb, CSPEC *cspec)` | 最も近い色に変換 |
| `OperateColor` | `COLOR OperateColor(COLOR col, CSPEC *cspec, ColorOpe ope, ...)` | 色の演算 |
| `InitConvColor` | `ERR InitConvColor(CNVCOL *cp, CSPEC *dst, CSPEC *src, CSPEC *env)` | 高速色変換の初期設定 |
| `ConvColor` | `COLOR ConvColor(CNVCOL *cp, COLOR pix)` | 高速色変換 |
| `EndConvColor` | `void EndConvColor(CNVCOL *cp)` | 高速色変換の終了 |
| `ConvColorBmp` | `ERR ConvColorBmp(BMP *dst, CSPEC *dspc, BMP *src, CSPEC *sspc, CSPEC *env)` | ビットマップの色と形式変換 |
| `InitOpeColor` | `ERR InitOpeColor(OPECOL *op, CSPEC *env, ColorOpe ope, ...)` | 高速色演算の初期設定 |
| `OpeColor` | `COLOR OpeColor(OPECOL *op, COLOR pix)` | 高速色演算 |
| `EndOpeColor` | `void EndOpeColor(OPECOL *op)` | 高速色演算の終了 |
| `OpeColorBmp` | `ERR OpeColorBmp(BMP *dst, BMP *src, CSPEC *env, ColorOpe ope, ...)` | ビットマップの色演算 |
| `RGBtoHLS` | `UW RGBtoHLS(UW rgb)` | RGBからHLSへの変換 |
| `HLStoRGB` | `UW HLStoRGB(UW hls)` | HLSからRGBへの変換 |
| `toStdRGBColor` | `COLOR toStdRGBColor(COLOR pix)` | ピクセル値からRGBカラーへの強制変換 |
| `GetStdCSPEC` | `ERR GetStdCSPEC(CSPEC *cs, W planes, UW pixbits)` | システム標準CSPECの取り出し |

### 2.1 型変換

`brightv/library/appl/conv.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `toPNT` | `PNT toPNT(W x, W y)` | PNT型への代入 |
| `toSIZE` | `SIZE toSIZE(W h, W v)` | SIZE型への代入 |
| `toRECT` | `RECT toRECT(W l, W t, W r, W b)` | RECT型への代入 |
| `PNTtoUW` | `UW PNTtoUW(PNT p)` | PNTからUWへの変換 |
| `UWtoPNT` | `PNT UWtoPNT(UW w)` | UWからPNTへの変換 |
| `isSamePNT` | `int isSamePNT(PNT a, PNT b)` | 点の同値チェック |
| `isSameSIZE` | `int isSameSIZE(SIZE a, SIZE b)` | SIZEの比較 |

### 2.5 データボックス

`brightv/library/appl/data.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `opendbox` | `W opendbox(LINK *lnk, W dtyp, W dnum)` | データボックスのオープン |
| `opendatabox` | `W opendatabox(LINK *lnk, W dtyp, W dnum, W eflg)` | データボックスのオープン(2) |
| `closedbox` | `VOID closedbox(void)` | データボックスのクローズ |
| `getdbox` | `B* getdbox(W dnum)` | データ検索 |
| `ptrdbox` | `B* ptrdbox(W offset)` | データオフセットのポインタ変換 |
| `setdbox` | `W setdbox(B *ptr)` | メモリ内データボックス処理 |

### 2.2 ディスプレイ・ライブラリ定義

`brightv/library/appl/disp.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `rectwidth` | `int rectwidth(RECT r)` | 長方形の幅 |
| `rectheight` | `int rectheight(RECT r)` | 長方形の高さ |
| `setpt` | `void setpt(PNT p, W h, W v)` | PNTへの値設定 |
| `movept` | `void movept(PNT p, W dh, W dv)` | 点の移動 |
| `setrect` | `void setrect(RECT x, W l, W t, W r, W b)` | 長方形の座標セット |
| `equalrect` | `int equalrect(RECT a, RECT b)` | 長方形の比較 |
| `emptyrect` | `int emptyrect(RECT r)` | 長方形の空チェック |
| `sectrect` | `int sectrect(RECT a, RECT b)` | 長方形の共通部分チェック |
| `inrect` | `int inrect(RECT r, PNT p)` | 点の長方形への包含チェック |
| `nullrect` | `void nullrect(RECT r)` | 長方形の空化 |
| `genrect` | `void genrect(RECT *rp, W h0, W v0, W h1, W v1)` | 長方形の生成 |
| `gensquare` | `void gensquare(RECT *rp, W h0, W v0, W h1, W v1)` | 正方形の生成 |
| `moverect` | `RECT* moverect(RECT *rp, W dh, W dv)` | 長方形の移動 |
| `posrect` | `RECT* posrect(RECT *rp, PNT p)` | 長方形の位置合わせ |
| `sizerect` | `RECT* sizerect(RECT *rp, W dh, W dv)` | 長方形の変形 |
| `andrect` | `RECT* andrect(RECT *dr, RECT *r1, RECT *r2)` | 長方形のAND演算 |
| `andrect2` | `RECT* andrect2(RECT *dr, RECT *r2)` | 長方形のAND演算(2) |
| `andrectlist` | `RECT* andrectlist(RECT *drp, RLIST *rlp)` | 長方形リストのAND演算 |
| `orrect` | `RECT* orrect(RECT *drp, RECT *r1, RECT *r2)` | 長方形のOR演算 |
| `orrect2` | `RECT* orrect2(RECT *drp, RECT *r2)` | 長方形のOR演算(2) |
| `orrectlist` | `RECT* orrectlist(RECT *drp, RLIST *rlp)` | 長方形リストのOR演算 |
| `xorrect` | `W xorrect(RLIST *rlp, RECT *r1, RECT *r2)` | 長方形のXOR演算 |
| `xorrectlist` | `W xorrectlist(RLIST *drlp, RECT *rp, RLIST *srlp)` | 長方形リストのXOR演算 |
| `genrectlist` | `RLIST* genrectlist(W n, RLIST *rlp)` | 長方形リストの生成 |

### 2.14 エンディアン変換

`brightv/library/appl/endian.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `swapH` | `UH swapH( UH x )` | ハーフワード内バイトスワップ |
| `swapW` | `UW swapW( UW x )` | ワード内のバイト順入れ替え |
| `swap3B` | `UW swap3B( UW x )` | 3バイトデータのエンディアン変換 |
| `isConvEndian` | `Bool isConvEndian(void)` | エンディアン変換の必要性を調べる |
| `ConvEndianTC` | `W ConvEndianTC(TC *to, TC *from, W max)` | 文字列のエンディアン変換 |
| `ConvEndianHs` | `void ConvEndianHs(UH *to, UH *from, W len)` | ハーフワード列のエンディアン変換 |
| `ConvEndianStruct` | `W ConvEndianStruct(VP to, VP from, char *form, W max)` | 構造体のエンディアン変換 |
| `ConvEndianTAD` | `WERR ConvEndianTAD(VP to, VP from, W max, Bool toBig)` | TADデータ要素のエンディアン変換 |
| `ConvEndianTADs` | `WERR ConvEndianTADs(VP to, VP from, W max, Bool toBig)` | TADデータ列のエンディアン変換 |
| `ConvEndianW` | `UW ConvEndianW( UW x )` | ワードデータのCPUエンディアンへの変換 |
| `ConvEndianH` | `UH ConvEndianH( UH x )` | ハーフワードデータのCPUエンディアンへの変換 |
| `ConvEndian3B` | `UW ConvEndian3B( UW x )` | 3 byteデータのCPUエンディアンへの変換 |
| `GetMisalignW` | `UW GetMisalignW(UB d[4])` | 任意アドレスのワード読みだし |
| `SetMisalignW` | `void SetMisalignW(UB d[4], UW x)` | 任意アドレスのワード書き込み |
| `GetMisalign3B` | `UW GetMisalign3B( UB d[3] )` | 任意アドレスの3byteデータの読みだし |
| `SetMisalign3B` | `void SetMisalign3B( UB d[3], UW x )` | 任意アドレスの3byteデータの書き込み |
| `GetMisalignH` | `UH GetMisalignH( UB d[2] )` | 任意アドレスのハーフワードデータの読みだし |
| `SetMisalignH` | `void SetMisalignH( UB d[2], UH x )` | 任意アドレスのハーフワードデータの書き込み |

### 2.3 ウィンドウイベント

`brightv/library/appl/event.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `evt_loop` | `W evt_loop(WFUNCPTR wf, WINFOPTR wi)` | イベントループ処理 |
| `unget_wevt` | `W unget_wevt(void)` | ウィンドウ・イベントの戻し |
| `evt_proc` | `W evt_proc(void)` | イベント処理 |
| `update_sbar` | `W update_sbar(W ix, W cmd, W *ptr)` | スクロールバーの表示更新処理 |
| `chk_scroll` | `PNT chk_scroll(W type, W diff, W unit, RECT view, RECT frame)` | 表示スクロール量のチェック処理 |

### 2.16 付箋固有データ処理

`brightv/library/appl/fsndata.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `get_fsn_data` | `ERR get_fsn_data(M_EXECREQ *cmd, VP fsn, W len)` | 付箋固有データの取り出し |
| `put_fsn_data` | `ERR put_fsn_data(M_EXECREQ *cmd, VP fsn, VP oldfsn, Bool endprc)` | 付箋固有データの設定 |

### 2.8 屑ファイル処理

`brightv/library/appl/gabage.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `reggabage` | `VOID reggabage(LINK *lnk, W on)` | 屑ファイルの登録／削除 |
| `delgabage` | `VOID delgabage(void)` | 屑ファイルの削除 |

### 2.9 レイアウト用紙設定

`brightv/library/appl/layout.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `initform` | `VOID initform(void)` | 用紙設定の初期化 |
| `changeform` | `W changeform(W undo)` | レイアウト用紙設定パネル処理 |
| `loadform` | `W loadform(B *fsn, W len, PNT unit)` | レイアウト用紙設定処理 |
| `saveform` | `W saveform(FUNCP wfn, W txt)` | TAD用紙関連付箋のセーブ |

### 2.17 画像形式変換ライブラリ

`brightv/library/appl/libimg.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `libimg_rea_bmp` | `ERR libimg_rea_bmp( IMG_BMP *dst, IMG_COMPACT *compact )` | 画像をBMP構造体に変換 |
| `libimg_wri_bmp` | `ERR libimg_wri_bmp( IMG_BMP *src, IMG_COMPACT *compact )` | BMP構造体を指定画像形式に変換 |
| `libimg_rea_seg` | `WERR libimg_rea_seg(IMAGESEG* imgseg, W imgsegsz, IMG_TADINFO* info)` | TAD画像セグメントからのCBMPデータ読み込み |
| `libimg_rea_seg` | `WERR libimg_wri_seg(IMAGESEG* imgseg, IMG_TADINFO* info);` | CBMPデータからTAD画像セグメントへの書き込み |

### 2.12 メモリ演算関数

`brightv/library/appl/memory.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `copybytes` | `VOID copybytes(B *dst, B *src, UW len, W mode)` | メモリ間演算つきコピー |
| `shiftbytes` | `VOID shiftbytes(B *p, UW len, W cnt)` | バイト列の論理シフト |

### 2.7 メニュー

`brightv/library/appl/menu.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `openmenu` | `W openmenu(W men)` | 標準メニューのオープン |
| `closemenu` | `VOID closemenu(void)` | 標準メニューのクローズ |
| `selmenu` | `W selmenu(W vid, FUNCP mfunc[])` | 標準メニューの選択処理 |
| `inactmenu` | `VOID inactmenu(W mask, W mode)` | 標準メニューの不能状態の一括設定 |
| `indmenu` | `VOID indmenu(W ino, W param, W mode)` | 標準メニューのインジケータ設定 |
| `opengmenu` | `W opengmenu(W men)` | 汎用メニューのオープン |
| `closegmenu` | `VOID closegmenu(W men)` | 汎用メニューのクローズ |
| `selgmenu` | `W selgmenu(W men, PNT pos, W *ppar)` | 汎用メニューの選択処理 |
| `chggmenu` | `VOID chggmenu(W men, W ino, W param, W mode)` | 汎用メニューの状態変更処理 |
| `keymenu` | `W keymenu(void)` | キーメニュー処理関数の実行 |
| `chgmitem` | `VOID chgmitem(W no, TC *ptr)` | メニュー項目変更処理 |

### 2.18 その他

`brightv/library/appl/other.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `itostr` | `TC* itostr(TC *s, W v, W n)` | 整数の文字列変換 |
| `datetostr` | `VOID datetostr(TC *s, W time)` | 時刻の文字列変換 |
| `cnvpath` | `TC* cnvpath(TC *name)` | ファイルパス特殊文字の変換 |
| `shadowrect_frame` | `VOID shadowrect_frame(W gid, RECT r, PAT *pat, W width, W shadow)` | 影付き長方形の描画 |
| `bmalloc` | `W bmalloc(BMP *bmp)` | ビットマップのプレーン領域獲得 |
| `bfree` | `VOID bfree(BMP *bmp)` | ビットマップのプレーン領域解放 |
| `bexecv` | `WERR bexecv( TC *name, TC *argv[] )` | 外部プログラム実行 |
| `reboot` | `W reboot( W mode )` | システム再起動 |
| `cre_tagpnl` | `ERR cre_tagpnl( ID wid, W ntags, TC *name[], RECT *frame, RECT *inner, PAT *frpat, PAT *inpat, VOID **tagpnl )` | 見出しパネルの生成 |
| `del_tagpnl` | `VOID del_tagpnl( VOID* tagpnl )` | 見出しパネルの削除 |
| `dsp_tagpnl` | `VOID dsp_tagpnl( VOID* tagpnl, RECT *rp )` | 見出しパネルの表示 |
| `inact_tagpnl` | `VOID inact_tagpnl( VOID *tagpnl, UW inact )` | 見出しパネルの不能状態の設定 |
| `num_tagpnl` | `W num_tagpnl( VOID* tagpnl )` | 見出しパネルの選択見出し番号の取り出し |
| `sw_tagpnl` | `W sw_tagpnl( VOID *tagpnl, W num, RECT* dspr )` | 見出しパネルの切り替え |
| `pres_tagpnl` | `W pres_tagpnl( VOID *tagpnl, WEVENT *wevt )` | 見出しパネルのプレスイベント処理 |

### 2.6 パネル

`brightv/library/appl/panel.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `panel` | `W panel(W pnl)` | 標準パネルの表示／実行 |
| `bgpanel` | `W bgpanel(W pnl)` | バックグラウンド時パネル表示 |
| `errpanel` | `W errpanel(W pnl, W err)` | エラーパネル表示 |
| `initstdpnl` | `VOID initstdpnl(void)` | パネル初期化 |
| `clsstdpnl` | `W clsstdpnl(void)` | パネル削除 |
| `opnstdpnl` | `W opnstdpnl(W pnl, FUNCP fn)` | 標準パネル生成／表示 |
| `exstdpnl` | `W exstdpnl(W pnid, FUNCP fn)` | パネルの実行処理 |
| `dostdpnl` | `W dostdpnl(W pnl, FUNCP fn)` | 標準パネル生成、実行 |
| `rectstdpnl` | `VOID rectstdpnl(W ino, RECT *rp)` | パネル項目の領域獲得 |
| `pidstdpnl` | `W pidstdpnl(W ino)` | パネル項目のパーツID獲得 |
| `ptrstdpnl` | `TC* ptrstdpnl(W ino)` | パネルの項目へのポインタ獲得 |
| `itemstdpnl` | `W itemstdpnl(W ino)` | パネル項目番号獲得 |
| `offtoptr` | `VOID offtoptr(UW *ptr)` | データオフセットのポインタへの変換 |
| `adjscalr` | `VOID adjscalr(RECT *rp)` | 実座標値変換 |
| `sysmsg` | `VOID sysmsg(W msg)` | システムメッセージパネル表示 |
| `chgbgcol` | `W chgbgcol(W *mask, COLOR *color, COLOR bgcol)` | ウィンドウ背景色パネル処理 |

### 2.10 印刷処理

`brightv/library/appl/print.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `initprpara` | `VOID initprpara(void)` | 印刷パラメータ初期化 |
| `setprpara` | `VOID setprpara(H *form, W h_unit, W v_unit)` | 仮身展開 : \| なし |
| `doprint` | `VOID doprint(LINK *lnk, FUNCP tmpfn)` | 印刷処理 |
| `cord_to_mm` | `W cord_to_mm(W val, W unit)` | unitからmmへの変換 |

### 2.11 スクリーン情報

`brightv/library/appl/screen.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `getscreen` | `W getscreen(void)` | スクリーン情報の獲得 |
| `centering` | `VOID centering(RECT *rp)` | 長方形のセンタリング |
| `setpointer` | `VOID setpointer(W ptr, PTRIMG *img)` | ポインタ形状の変更 |

### 2.4 テキスト入力ポート

`brightv/library/appl/text.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `open_text` | `W open_text(W wid, W mode, TEXTPORT **text)` | テキスト入力ポートのオープン |
| `close_text` | `VOID close_text(TEXTPORT *text)` | テキスト入力ポートのクローズ |
| `flush_text` | `W flush_text(TEXTPORT *text)` | テキスト入力ポートのフラッシュ |
| `read_text` | `W read_text(TEXTPORT *text, WEVENT *evt)` | テキスト入力ポートの読み込み |
| `redisp_text` | `W redisp_text(TEXTPORT *text)` | テキスト入力ポートの再表示 |

### 2.4 テキスト入力ポート

`brightv/library/appl/text2.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `open_text` | `W open_text(W wid, W mode, TEXTPORT **text)` | テキスト入力ポートのオープン |
| `reopen_text` | `W reopen_text(TEXTPORT *text, W mode)` | テキスト入力ポートの再オープン |
| `close_text` | `VOID close_text(TEXTPORT *text)` | テキスト入力ポートのクローズ |
| `flush_text` | `W flush_text(TEXTPORT *text)` | テキスト入力ポートのフラッシュ |
| `read_text` | `W read_text(TEXTPORT *text, WEVENT *evt)` | テキスト入力ポートの読み込み |
| `redisp_text` | `W redisp_text(TEXTPORT *text)` | テキスト入力ポートの再表示 |

### 3.2 mtstring.h

`brightv/library/lang/mtstring.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `mtc_unique` | `W mtc_unique( TC *dst, TC *src, W len )` | 一意表現への変換 |
| `tc_chcmp` | `W tc_chcmp( TC c1, TC c2 )` | 文字コード比較 |
| `mtc_strcmp` | `W mtc_strcmp( TC *s1, TC *s2 )` | 文字列比較 |
| `mtc_strcat` | `TC* mtc_strcat( TC *dst, TC *src )` | 文字列の連結 |
| `mtc_strjoin` | `W mtc_strjoin( TC *dst, W len, TC *s1, ... )` | 複数文字列の連結 |
| `mtc_strapd` | `W mtc_strapd( TC *dst, TC *apd, W len )` | 文字列の追加 |
| `mtc_strlen` | `W mtc_strlen( TC *str )` | 文字数を数える |
| `mtc_strchr` | `TC* mtc_strchr( TC *str, TC ch, W lang )` | 文字の検索 |
| `mtc_strchr2` | `TC* mtc_strchr2( TC *str, W *slang, TC ch, W clang )` | 一意表現でない文字列を対象とした文字の検索 |
| `mtc_strpos` | `TC mtc_strpos( TC *str, W n, W *lang )` | 文字位置を得る |
| `mtc_poslang` | `W mtc_poslang( TC *str, TC *p )` | 文字位置の言語(スクリプト)を得る |

### 3.1 tlang.h

`brightv/library/lang/tlang.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `isValidTLANG` | `Bool isValidTLANG( W lang )` | TLANG の値の有効性確認 |
| `isZone` | `Bool inZone( TC c, TC lt, TC rb )` | ゾーン判定 |
| `isTLANG` | `W isTLANG( TC *tp, W len, TC **ntp );` | 言語(スクリプト)指定コード判定 |
| `isTLANGch` | `W isTLANGch( TC ch, W *n )` | 言語(スクリプト)指定コード判定 |
| `TLANGtoTC` | `W TLANGtoTC( TC *tp, W max, TLANG lang )` | 言語指定コード(TLANG)を TC 列へ変換 |

### 3.3 wtstring.h

`brightv/library/lang/wtstring.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `tctowtc` | `W tctowtc( WTC *dst, TC *src, W len )` | TC 文字列から WTC 文字列への変換 |
| `wtctotc` | `W wtctotc( TC *dst, WTC *src, W len )` | WTC 文字列から TC 文字列への変換 |
| `wtc_strcmp` | `W wtc_strcmp( WTC *s1, WTC *s2 )` | 文字列の比較 |
| `wtc_strcat` | `WTC* wtc_strcat( WTC *dst, WTC *src )` | 文字列の連結 |
| `wtc_strcpy` | `WTC* wtc_strcpy( WTC *dst, WTC *src )` | 文字列の複写 |
| `wtc_strset` | `WTC* wtc_strset( WTC *str, WTC ch, W len )` | 文字列の設定 |
| `wtc_strlen` | `W wtc_strlen( WTC *str )` | 文字列長の取り出し |
| `wtc_strchr` | `WTC* wtc_strchr( WTC *str, WTC ch, WTC mask )` | 文字列検索 |

### 1.3 bassert.h

`brightv/library/standard/bassert.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `assert` | `void assert(int exp)` | プログラムの検証 |

### 1.4 bctype.h

`brightv/library/standard/bctype.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `isalnum` | `int isalnum(int c)` | 英数文字の判定 |
| `isalpha` | `int isalpha(int c)` | 英文字の判定 |
| `iscntrl` | `int iscntrl(int c)` | 制御文字の判定 |
| `isdigit` | `int isdigit(int c)` | 10進数字の判定 |
| `isgraph` | `int isgraph(int c)` | 表示文字の判定 |
| `islower` | `int islower(int c)` | 英小文字の判定 |
| `isprint` | `int isprint(int c)` | 表示文字の判定 |
| `ispunct` | `int ispunct(int c)` | 区切り文字の判定 |
| `isspace` | `int isspace(int c)` | 空白類文字の判定 |
| `isupper` | `int isupper(int c)` | 英大文字の判定 |
| `isxdigit` | `int isxdigit(int c)` | 16進数字の判定 |
| `tolower` | `int tolower(int c)` | 英小文字への変換 |
| `toupper` | `int toupper(int c)` | 英大文字への変換 |
| `isascii` | `int isascii(int c)` | ASCII文字の判定 |
| `toascii` | `int toascii(int c)` | ASCII 文字への変換 |

### 1.19 bitop.h

`brightv/library/standard/bitop.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `BCLR` | `VOID BCLR(VP base, W offset)` | ビットのクリア |
| `BSET` | `VOID BSET(VP base, W offset)` | ビットのセット |
| `BNOT` | `VOID BNOT(VP base, W offset)` | ビットの反転 |
| `BTST` | `W BTST(VP base, W offset)` | ビットのテスト |
| `BVSCH0` | `W BVSCH0(VP base, W offset, W bitwidth)` | ビット0のサーチ |
| `BVSCH1` | `W BVSCH1(VP base, W offset, W bitwidth)` | ビット1のサーチ |
| `BVCLR` | `VOID BVCLR(VP base, W offset, W bitwidth)` | ビット列のクリア |
| `BVSET` | `VOID BVSET(VP base, W offset, W bitwidth)` | ビット列のセット |

### 1.5 bmath.h

`brightv/library/standard/bmath.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `acos` | `double acos(double x)` | 逆余弦関数 |
| `asin` | `double asin(double x)` | 逆正弦関数 |
| `atan` | `double atan(double x)` | 逆正接関数 |
| `atan2` | `double atan2(double y, double x)` | 逆正接関数 |
| `atof` | `double atof(char *s)` | 文字列の倍精度浮動小数点数への変換 |
| `ceil` | `double ceil(double x)` | 最近接最小値 |
| `cos` | `double cos(double x)` | 余弦関数 |
| `cosh` | `double cosh(double x)` | 双曲線余弦関数 |
| `exp` | `double exp(double x)` | 指数関数 |
| `fabs` | `double fabs(double x)` | 絶対値 |
| `floor` | `double floor(double x)` | 最近接最大値 |
| `frexp` | `double frexp(double value, int *eptr)` | 指数分解 |
| `hypot` | `double hypot(double x, double y)` | ユークリッド距離 |
| `j0` | `double j0(double x)` | ベッセル関数 |
| `j1` | `double j1(double x)` | ベッセル関数 |
| `jn` | `double jn(int n, double x)` | ベッセル関数 |
| `ldexp` | `double ldexp(double x, int n)` | 指数積載 |
| `log` | `double log(double x)` | 自然対数関数 |
| `log10` | `double log10(double x)` | 10を底とする対数関数 |
| `modf` | `double modf(double vaule, double* iptr)` | 整数部と小数部への分割 |
| `pow` | `double pow(double x, double y)` | 累乗関数 |
| `sin` | `double sin(double x)` | 正弦関数 |
| `sinh` | `double sinh(double x)` | 双曲線正弦関数 |
| `sqrt` | `double sqrt(double x)` | 平方根 |
| `tan` | `double tan(double x)` | 正接関数 |
| `tanh` | `double tanh(double x)` | 双曲線正接関数 |
| `tc_atof` | `double tc_atof(TC *tc)` | TRON数字列からdoubleへの変換 |
| `tc_strtod` | `double tc_strtod(TC *tc, TC **ptr)` | TRON数字列からdoubleへの変換 |

### 1.6 bsetjmp.h

`brightv/library/standard/bsetjmp.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `setjmp` | `int setjmp(jmp_buf env)` | 大域ジャンプ用の環境セーブ |
| `longjmp` | `void longjmp(jmp_buf env, int rval)` | スタックの復帰 |

### 1.7 bstdio.h

`brightv/library/standard/bstdio.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `getchar` | `int getchar(void)` | 標準入力 |
| `putchar` | `int putchar(int c)` | 標準出力 |
| `gets` | `char* gets(char *s)` | 文字列の読み込み |
| `puts` | `int puts(char *s)` | 文字列の標準出力への書き込み |
| `printf` | `int printf(const char *format, ...)` | 書式付き標準出力 |
| `sprintf` | `int sprintf(char *sp, const char *format, ...)` | 書式付き出力 |
| `vprintf` | `int vprintf(const char *format, va_list ap)` | 書式付き出力 |
| `vsprintf` | `int vsprintf(char *buf, const char *format, va_list ap)` | 書式付き出力 |
| `perror` | `void perror(const char *prompt)` | システムエラーメッセージ |
| `panic` | `void panic(const char *prompt)` | システムエラー |

### 1.8 bstdlib.h

`brightv/library/standard/bstdlib.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `exit` | `void exit(int status)` | プログラム終了 |
| `abort` | `void abort(void)` | プログラム終了 |
| `atexit` | `int atexit(void (*func)(void))` | プログラム終了時実行関数の登録 |
| `atoi` | `int atoi(const char *str)` | 文字列の整数への変換 |
| `atol` | `long atol(const char *str)` | 文字列のlongへの変換 |
| `strtol` | `long strtol(const char *str, char **tail, int radix)` | 文字列の整数への変換 |
| `strtoul` | `unsigned long strtoul(const char *str, char **tail, int radix)` | 文字列の非負整数への変換 |
| `calloc` | `void* calloc(size_t nmemb, size_t size)` | 初期化付きメモリ獲得 |
| `free` | `void free(void *ptr)` | メモリブロック解放 |
| `malloc` | `void* malloc(size_t size)` | メモリ獲得 |
| `realloc` | `void* realloc(void *ptr, size_t size)` | メモリ再獲得 |
| `malloctest` | `void malloctest(int mode)` | mallocデバッグ情報設定 |
| `Scalloc` | `void * Scalloc(size_t nmemb, size_t size)` | 共有メモリの獲得／解放 |
| `rand` | `int rand(void)` | 乱数 |
| `srand` | `void srand(unsigned int seed)` | 乱数の初期値設定 |
| `div` | `div_t div(int num, int den);` | 除算 |
| `ldiv` | `ldiv_t ldiv(long int num, long int den)` | 除算 |

### 1.9 bstring.h

`brightv/library/standard/bstring.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `memcmp` | `int memcmp(const void *s1, const void *s2, size_t len)` | メモリ内容の比較 |
| `strcmp` | `int strcmp(const char *s1, const char *s2)` | 文字列の比較 |
| `strncmp` | `int strncmp(const char *s1, const char *s2, size_t len)` | 文字列の比較(2) |
| `strcasecmp` | `int strcasecmp(const char *s1, const char *s2)` | 文字列の比較(大文字小文字を同一視) |
| `strncasecmp` | `int strncasecmp(const char *s1, const char *s2, size_t len)` | 文字列の比較(2)(大文字小文字を同一視) |
| `strcat` | `char* strcat(char *dest, const char *src)` | 文字列連結 |
| `strncat` | `char* strncat(char *dest, const char *src, size_t len)` | 文字列連結 |
| `memmove` | `void* memmove(void *dest, const void *src, size_t len)` | メモリ内容の移動 |
| `memcpy` | `void* memcpy(void *dest, const void *src, size_t len)` | メモリ内容のコピー |
| `strcpy` | `char* strcpy(char *dest, const char *src)` | 文字列のコピー |
| `strncpy` | `char* strncpy(char *dest, const char *src, size_t len)` | 文字列のコピー |
| `strdup` | `char* strdup(const char *src)` | 文字列の複製 |
| `memccpy` | `void* memccpy(void *dest, const void *src, int c, size_t len)` | メモリ内容のコピー |
| `memset` | `void* memset(void *mem, int ch, size_t len)` | メモリ内容の初期化 |
| `strlen` | `size_t strlen(const char *str)` | 文字列の長さ |
| `strerror` | `char* strerror(int error)` | エラーメッセージの獲得 |
| `memchr` | `void* memchr(const void *mem, int ch, size_t len)` | バイト検索 |
| `strchr` | `char* strchr(const char *str, int ch)` | 文字列中の文字検索 |
| `strrchr` | `char* strrchr(const char *str, int ch)` | 文字列中の文字検索 |
| `strcspn` | `size_t strcspn(const char *s1, const char *s2)` | 文字列中の特定文字列の長さ |
| `strspn` | `size_t strspn(const char *s1, const char *s2)` | 文字列中の特定文字列の長さ |
| `strpbrk` | `char* strpbrk(const char *s1, const char *s2)` | 文字列中の文字検索 |
| `strstr` | `char* strstr(const char *s1, const char *s2)` | 文字列中の文字列検索 |
| `strtok` | `char* strtok(char *s, const char *delm)` | 文字列をトークンに分解 |
| `strtok_r` | `char* strtok_r(char *s, const char *delim, char **last)` | 文字列をトークンに分解 |
| `ffs` | `int ffs(int i)` | セットされた最初のビット位置の検索 |
| `index` | `char* index(const char *str, int ch)` | 文字列中の文字検索 |
| `rindex` | `char* rindex(const char *str, int ch)` | 文字列中の文字検索 |
| `bzero` | `void bzero(void *mem, size_t len)` | メモリ領域の0クリア |
| `bcopy` | `void bcopy(const void *src, void *dest, size_t len)` | メモリ内容のコピー |
| `bcmp` | `int bcmp(const void *s1, const void *s2, size_t len)` | メモリ内容の比較 |

### 1.10 bsyslog.h

`brightv/library/standard/bsyslog.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `syslog` | `void syslog(int priority, const char *format, ...);` | システムログの出力 |
| `setlogmask` | `int setlogmask(int maskpri);` | ログマスクの設定 |

### 1.11 datetime.h

`brightv/library/standard/datetime.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `DATEtoTIME` | `void DATEtoTIME( STIME *time, DATE_TIM *date )` | カレンダー日付から通算秒数への変換 |
| `TIMEtoDATE` | `void TIMEtoDATE( DATE_TIM *date, STIME time )` | 通算秒数からカレンダー日付への変換 |
| `GMTtoLT` | `STIME GMTtoLT( STIME time, TIMEZONE *tz )` | ローカル時間補正 |
| `LTtoGMT` | `STIME LTtoGMT( STIME time, TIMEZONE *tz )` | 標準時間補正 |

### 1.23 dynload.h

`brightv/library/standard/dynload.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `b_dlsearch` | `ERR b_dlsearch( TC *path, LINK *lnk, W mode )` | 共有オブジェクトファイルの検索 |
| `b_dlopen` | `WERR b_dlopen( LINK *lnk, W flag )` | 共有オブジェクトのロード |
| `b_dlsym` | `ERR b_dlsym( W handle, const char *symbol, UW *val )` | 共有オブジェクトのシンボル検索 |
| `b_dladdr` | `WERR b_dladdr( VP addr, DLINFO *info )` | 共有オブジェクトのアドレス情報の取得 |
| `b_dlclose` | `ERR b_dlclose( W handle )` | 共有オブジェクトのアンロード |

### 1.21 libwin.h

`brightv/library/standard/libwin.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `RectGRgn` | `GRGN* RectGRgn(RECT r)` | 長方形からGRGNへの変換 |
| `AndRect` | `RECT* AndRect(RECT *dst, RECT *s1, RECT *s2)` | 長方形の共通部分 |
| `PtInGRgn` | `Answer PtInGRgn(PNT pnt, GRGN *rgn)` | 点の包含検査 |
| `DoesIncludeGRgn` | `Answer DoesIncludeGRgn(GRGN *sub, GRGN *obj)` | 領域の包含検査 |
| `IsIncludedGRgn` | `Answer IsIncludedGRgn(GRGN *sub, GRGN *obj)` | 領域の包含検査 |
| `IsSameShapeGRgn` | `Answer IsSameShapeGRgn(GRGN *rgn1, GRGN *rgn2)` | 領域の形状一致検査 |
| `IsSameGRgn` | `Answer IsSameGRgn(GRGN *rgn1, GRGN *rgn2)` | 領域の一致検査 |
| `IsSameShapeGRgnWithDisp` | `Answer IsSameShapeGRgnWithDisp(GRGN *rgn1, GRGN *rgn2, Displacement *disp)` | 領域の形状検査及び変位の検出 |
| `MinimumRectOfGRgn` | `RECT MinimumRectOfGRgn(GRGN *rgn)` | 領域を含む長方形 |
| `IsEmptyGRgn` | `Answer IsEmptyGRgn(GRGN *rgn)` | 領域の空判定 |
| `CopyGRgn` | `GRGN* CopyGRgn(GRGN *rgn)` | 領域の複製 |
| `CopyAndNormalizeGRgn` | `GRGN* CopyAndNormalizeGRgn(GRGN *rgn)` | 領域の正規化複製 |
| `CreateGRgnWithList` | `GRGN* CreateGRgnWithList(W sy, W bx, ...)` | GRGN生成 |
| `CreateGRgnWithNumberAndList` | `GRGN* CreateGRgnWithNumberAndList(W sy, W nr, W bx, ...)` | GRGN生成 |
| `CreateGRgnWithVector` | `GRGN* CreateGRgnWithVector (W sy, W bx, HRGN *hr[])` | GRGN生成 |
| `CreateGRgnWithNumberAndVector` | `GRGN* CreateGRgnWithNumberAndVector(W sy, W nr, W bx, HRGN *hr[])` | GRGN生成 |
| `CreateHRgnWithNumberAndList` | `HRGN* CreateHRgnWithNumberAndList(W nh, ...)` | HRGN生成 |
| `CreateHRgnWithVector` | `HRGN* CreateHRgnWithVector(W hv[])` | HRGN生成 |
| `CreateHRgnWithNumberAndVector` | `HRGN* CreateHRgnWithNumberAndVector(W nh, W hv[])` | HRGN生成 |
| `DuplicateGRgn` | `Status DuplicateGRgn(GRGN *new, GRGN *org, W len)` | GRGNの複写 |
| `GRgnToMask` | `void GRgnToMask(GRGN *rgn, BMP *bmp)` | GRGNのビットマスクへの変換 |
| `PtrToOffsetGRgn` | `void PtrToOffsetGRgn(GRGN *rgn)` | GRGNポインタからオフセットへの変換 |
| `OffsetToPtrGRgn` | `void OffsetToPtrGRgn(GRGN *rgn)` | GRGNオフセットからポインタへの変換 |
| `DisposeGRgn` | `Status DisposeGRgn(GRGN *rgn)` | GRGN解放 |
| `CopyAndMoveGRgn` | `GRGN* CopyAndMoveGRgn(GRGN *rgn, Displacement *disp)` | 領域を平行移動した領域の生成 |
| `MoveGRgn` | `Status MoveGRgn(GRGN *rgn, Displacement *disp)` | 領域の平行移動 |
| `MaskToGRgn` | `GRGN* MaskToGRgn(BMP *bmp)` | ビットマスクからGRGNへの変換 |
| `NormalizeGRgn` | `Status NormalizeGRgn(GRGN *grgn)` | GRGNの正規化 |
| `UnionGRgn` | `GRGN* UnionGRgn(GRGN *rgn1, GRGN *rgn2)` | 領域の和 |
| `SectGRgn` | `GRGN* SectGRgn(GRGN *rgn1, GRGN *rgn2)` | 領域の共通部分 |
| `DiffGRgn` | `GRGN* DiffGRgn(GRGN *rgn1, GRGN *rgn2)` | 領域の差 |
| `XorGRgn` | `GRGN* XorGRgn(GRGN *rgn1, GRGN *rgn2)` | 領域の排他的論理和 |
| `PolyToGRgn` | `GRGN* PolyToGRgn(POLY *poly)` | 多角形からGRGNへの変換 |
| `PolyToOutline` | `GRGN* PolyToOutline(BMP *bmp, POLY *poly, COLOR bgcol, W frame)` | 閉領域の抽出 |
| `ResizeGRgn` | `Status ResizeGRgn(GRGN *rgn, RECT *frame)` | 領域の拡大・縮小 |
| `CopyAndResizeGRgn` | `GRGN* CopyAndResizeGRgn(GRGN *rgn, RECT *frame)` | 領域の拡大・縮小 |
| `RotateGRgn` | `GRGN* RotateGRgn(GRGN *rgn, DEG ang)` | 領域の90度単位の回転 |
| `SizeGRgn` | `W SizeGRgn(GRGN *rgn)` | GRGNのサイズ |

### 1.20 queue.h

`brightv/library/standard/queue.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `isQueEmpty` | `Bool isQueEmpty(QUEUE *que)` | 空キューのチェック |
| `QueInit` | `VOID QueInit(QUEUE *que)` | キューの初期化 |
| `QueInsert` | `VOID QueInsert(QUEUE *entry, QUEUE *que)` | キューへのエントリ挿入 |
| `QueRemove` | `VOID QueRemove(QUEUE *entry)` | キューからのエントリ削除 |
| `QueRemoveNext` | `QUEUE *QueRemoveNext(QUEUE *que)` | キューの先頭のエントリ取り出し |
| `QueSearch` | `QUEUE *QueSearch(QUEUE *start, QUEUE *end, W val, W offset)` | キューエントリのサーチ: EQ |
| `QueSearchNE` | `Inline QUEUE *QueSearchNE(QUEUE *start, QUEUE *end, W val, W offset)` | キューエントリのサーチ: NE |
| `QueSearchGE` | `QUEUE *QueSearchGE(QUEUE *start, QUEUE *end, W val, W offset)` | キューエントリのサーチ: GE |
| `QueSearchGEU` | `QUEUE *QueSearchGEU(QUEUE *start, QUEUE *end, W val, W offset)` | キューエントリのサーチ: GEU |
| `QueSearchRevLTU` | `QUEUE *QueSearchRevLTU(QUEUE *start, QUEUE *end, W val, W offset)` | キューエントリのサーチ: RevLTU |

### 1.17 tctype.h

`brightv/library/standard/tctype.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `tc_isdigit` | `int tc_isdigit(TC c)` | TRON コード文字判定関数 |
| `tc_toupper` | `TC tc_toupper(TC c)` | TRON コード英小文字の大文字化 |
| `tc_tolower` | `TC tc_tolower(TC c)` | TRONコード英大文字の小文字化 |

### 1.18 tstring.h

`brightv/library/standard/tstring.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `tc_strcmp` | `W tc_strcmp(TC *s1, TC *s2)` | TRON文字列の比較 |
| `tc_strncmp` | `W tc_strncmp(TC *s1, TC *s2, W len)` | TRON文字列の比較 |
| `tc_strcat` | `TC* tc_strcat(TC *dest, TC *src)` | TRON文字列の連結 |
| `tc_strncat` | `TC* tc_strncat(TC *dest, TC *src, W len)` | TRON文字列の連結 |
| `tc_strcpy` | `TC* tc_strcpy(TC *dest, TC *src)` | TRON文字列のコピー |
| `tc_strncpy` | `TC* tc_strncpy(TC *dest, TC *src, W len)` | TRON文字列のコピー |
| `tc_strset` | `TC* tc_strset(TC *str, TC ch, W len)` | TRON文字の初期化 |
| `tc_strlen` | `W tc_strlen(TC *str)` | TRON文字列の長さ獲得 |
| `tc_strchr` | `TC* tc_strchr(TC *str, int ch)` | TRON文字列中の文字検索 |
| `tc_strrchr` | `TC* tc_strrchr(TC *str, int ch)` | TRON文字列中の文字検索 |
| `tc_atoi` | `int tc_atoi(const TC *str)` | TRON文字列の整数変換 |
| `tc_atol` | `long tc_atol(const TC *str)` | TRON文字列の整数変換 |
| `tc_strtol` | `long tc_strtol(const TC *str, TC **tail, int base)` | TRON文字列の整数変換 |
| `euctotc` | `W euctotc(TC *tc, B *euc)` | EUCからTRON文字への変換 |
| `tctoeuc` | `W tctoeuc(B *euc, TC tc)` | TRON文字からEUCへの変換 |
| `eucstotcs` | `W eucstotcs(TC *tcstr, B* eucstr)` | EUC文字列からTRON文字列への変換 |
| `tcstoeucs` | `W tcstoeucs(B* eucstr, TC* tcstr)` | TRON文字列からEUC文字列への変換 |
| `tc_gets` | `TC* tc_gets(TC *str)` | TRON文字列入力 |
| `tc_puts` | `W tc_puts(TC *str)` | TRON文字列出力 |
| `sjtotc` | `W sjtotc(TC *tc, UB *sj)` | Shift-JIS文字からTRON文字への変換 |
| `tctosj` | `W tctosj(UB *sj, TC *tc)` | TRON文字からShift-JIS文字への変換 |
| `sjstotcs` | `W sjstotcs(TC *tcstr, UB* sjstr)` | Shift-JIS文字列からTRON文字列への変換 |
| `tcstosjs` | `W tcstosjs(UB* sjstr, TC* tcstr)` | TRON文字列からShift-JIS文字列への変換 |


## デバイス

### 2.1 カードマネージャの位置付け

`brightv/device/cardmgr.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `pcRegClient` | `ERR pcRegClient(ID portid, W kind)` | クライアント登録／登録削除 |
| `pcGetTuple` | `WERR pcGetTuple(ID cardid, W tuple, W order, UB* buf)` | ER_PAR   \| パラメータエラー ( portid, kind) |
| `pcMap` | `WERR pcMap(ID cardid, W offset, W len, UW attr, VP *addr)` | ER_ID  \| カード ID が不正 |
| `pcReMap` | `ERR pcReMap(ID cardid, ID mapid)` | R_LIMIT  \| マップ数が制限を超えた |
| `pcUnMap` | `ERR pcUnMap(ID mapid)` | ER_ID \| カード ID または マップ ID が不正 |
| `pcReadMem` | `ERR pcReadMem(ID cardid, W offset, W len, UW attr, VP buf)` | ER_ID \| マップ ID が不正 |
| `pcWriteMem` | `ERR pcWriteMem(ID cardid, W offset, W len, UW attr, VP buf)` | ER_ID  \| カード ID が不正 |
| `pcGetStat` | `WERR pcGetStat(ID cardid)` | ER_ID  \| カード ID が不正 |
| `pcDefInt` | `WERR pcDefInt(ID cardid, T_DINT *dint, IntVector vec, UW par)` | ER_ID  \| カード ID が不正 |
| `pcPowerCtl` | `ERR pcPowerCtl(ID cardid, UW power)` | ER_PAR  \| パラメータエラー ( dint->intatr, vec が不正) |
| `pcSuspend` | `VOID pcSuspend(Bool start)` | ER_PAR   \| パラメータエラー ( power ) |
| `pcResume` | `VOID pcResume(Bool start)` | カードのレジューム制御 |

### 10.1 コンソールの概要

`brightv/device/console.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `console_in` | `W console_in(W port, B *buf, UW len)` | コンソール入力 |
| `console_out` | `ERR console_out(W port, B *buf, UW len)` | コンソール出力 |
| `console_ctl` | `W console_ctl(W port, W req, W arg)` | コンソール制御 |
| `console_get` | `W console_get(W port, B *buf, UW len, W tmout)` | コンソールの出力データの読み込み |
| `console_put` | `ERR console_put(W port, B *buf, UW len, W tmout)` | コンソールの入力データの書き込み |
| `console_conf` | `ERR console_conf(W req, UW *arg)` | コンソールのコンフィグレーション操作 |
| `_PutString` | `int _PutString(char *buf)` | コンソールへの文字列出力 |
| `_PutChar` | `int _PutChar(int c)` | コンソールへの1文字出力 |
| `_GetString` | `int _GetString(char *buf)` | コンソールからの 1 行入力 |
| `_GetChar` | `int _GetChar()` | コンソールからの 1 文字入力 |
| `cons_ioctl` | `int cons_ioctl(int req, int arg)` | コンソールの制御 |
| `RS_putchar` | `int RS_putchar(int c)` | 標準 RS ポートへの 1 文字出力 |
| `RS_getchar` | `int RS_getchar()` | 標準 RS ポートからの 1 文字入力 |
| `RS_ioctl` | `int RS_ioctl(int req, int arg)` | 標準 RS ポートの制御 |
| `cons_put` | `W cons_put(W port, B *buf, UW len, W tmout)` | コンソールの入力バッファへの書き込み |
| `cons_get` | `W cons_get(W port, B *buf, UW len, W tmout)` | コンソールの出力バッファからの読み込み |
| `cons_conf` | `W cons_conf(W req, UW *arg)` | コンソールのコンフィギュレーション |

### 3.1 概要

`brightv/device/hwresmgr.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `lstHwRes` | `WERR lstHwRes(UW rid, HwResList *lst, W cnt)` | 登録されているハードウェアリソース一覧の取り出し |
| `getHwRes` | `WERR getHwRes(UW rid, UW *start, UW *end, W cnt)` | 登録されているハードウェアリソースの取り出し |
| `rsvHwRes` | `ERR rsvHwRes(UW rid, UW start, UW end)` | ハードウェアリソースの予約 |
| `relHwRes` | `ERR relHwRes(UW rid)` | 登録されているハードウェアリソースの解放 |
| `defIntHdr` | `ERR defIntHdr(W intno, FP inthdr, UW par)` | ( 共有 ) 割り込みハンドラの登録 |

### 15.1 USBマネージャの位置付け

`brightv/device/usbmgr.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `usbRequestDevice` | `ERR usbRequestDevice(W did, UB *request, UB *data, W len, W *rlen)` | デバイスリクエストを発行 |
| `usbDescriptorDevice` | `ERR usbDescriptorDevice(W did, UB *data, W len, W *rlen)` | USB_ERR_PAR \| requestがNULLである |
| `usbConfigDevice` | `WERR usbConfigDevice(W did, W cfg)` | USB_ERR_DEVICE \| didが不正（didで指定したデバイスは存在しない） |
| `usbConfigInterface` | `WERR usbConfigInterface(W iid, W alt)` | USB_ERR_INTERFACE \| 指定したコンフィギュレーションは存在しない |
| `usbStallPipe` | `WERR usbStallPipe(W pid, W stl)` | USB_ERR_BUSY \| クローズしていないパイプが存在している |
| `usbSyncPipe` | `WERR usbSyncPipe(W pid)` | USB_ERR_ABORT \| 通信がキャンセルされた |
| `usbOpenDevice` | `ERR usbOpenDevice(W did)` | USB_ERR_STALL \| STALLが発生した |
| `usbCloseDevice` | `ERR usbCloseDevice(W did)` | USB_ERR_DEVICE \| didが不正（デバイスが存在しない） |
| `usbOpenInterface` | `WERR usbOpenInterface(W did, W ifno)` | USB_ERR_DEVICE \| didが不正（デバイスが存在しない） |
| `usbCloseInterface` | `ERR usbCloseInterface(W iid)` | USB_ERR_BUSY \| インターフェースは既にオープンされている |
| `usbOpenPipe` | `WERR usbOpenPipe(W iid, W epadr, usbIoMode mode, W mbfid)` | USB_ERR_INTERFACE \| iidが不正（iidで指定したインターフェースは存在しない） |
| `usbClosePipe` | `ERR usbClosePipe(W pid)` | USB_ERR_ENDPOINT \| epadrが不正（指定したアドレスのエンドポイントは存在しない） |
| `usbIoPipe` | `ERR usbIoPipe(W pid, UB *buf, W len, W *rlen)` | USB_ERR_ENDPOINT \| pidが不正（そのパイプは存在しない） |
| `usbCancelDevice` | `ERR usbCancelDevice(W did)` | USB_ERR_BUSY \| 送信要求を受け付けることができない（帯域が不足した） |
| `usbAlivePipe` | `ERR usbAlivePipe(W pid)` | USB_ERR_DEVICE \| didが不正（デバイスが存在しない） |
| `usbRegistDevice` | `ERR usbRegistDevice(ID portid, usbEventPattern *pattern)` | USB_ERR_DEVICE \| デバイスは存在しない |
| `usbRegistInterface` | `ERR usbRegistInterface(ID portid, usbEventPattern *pattern)` | USB_ERR_DEVICE \| 指定したポートに対するデバイス接続イベントの登録が行われていない（pattern = NULLの時に発生） |
| `usbSuspend` | `VOID usbSuspend(Bool start)` | USB_ERR_NOMEM \| これ以上インターフェースイベントを登録できない |
| `usbResetDevice` | `ERR usbResetDevice(W did)` | デバイスのリセット（ソフトウェアによるデバイスの切り離し・再接続） |
| `usbGetHubInfo` | `WERR usbGetHubInfo(W *report, W size)` | USB_ERR_DEVICE \| didが不正（デバイスが存在しない） |


## BTRON3 仕様への制限・補足

### 2.2 既存のコード体系との関連

`brightv/diff/diff.txt`

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `prc_inf` | `WERR prc_inf( W pid, W item, VP buf, W len )` | プロセスの各種情報の取得 (追加システムコール) |
| `req_emg` | `WERR req_emg( W pid, W t_mask )` | プロセス終了通知  (追加システムコール) |
| `lod_mod` | `WERR lod_mod( LINK *lnk, P_DYNLDINF *info )` | 共有オブジェクトのロード (追加システムコール) |
| `unl_mod` | `ERR unl_mod( W loadid )` | 共有オブジェクトのアンロード (追加システムコール) |
| `get_mbk` | `ERR get_mbk( VP *adr, W nblk, UW atr )` | メモリブロックの獲得(オプションの追加) |
| `syn_fil` | `ERR syn_fil( W fd )` | ファイル単位の同期 (追加システムコール) |
| `chg_pda` | `WERR chg_pda( W atr )` | ポインティングデバイス属性の変更(属性の拡張) |
| `gget_col` | `W gget_col ( GID gid, UW p, COLOR *cv, UW cnt )` |  |
| `wugt_evt2` | `ERR wugt_evt2(WEVENT *evt)` | ウィンドウ・イベントの戻し (2) |
| `iput_key` | `WERR iput_key( W tipid, EVENT *evt )` | テキスト入力ポートへのキー入力(仕様変更) |
| `ipup_sel` | `WERR ipup_sel( W tipid, W pwid, RECT *r )` | 候補選択ポップアップのオープン (新規追加) |
| `ichg_mod` | `WERR ichg_mod( W mode )` | テキスト入力モードの変更 (仕様変更) |
| `ichg_lrn` | `WERR ichg_lrn( UW kind, W stat )` | 学習辞書の保存状態変更 (廃止予定) |
| `iopn_tip` | `WERR iopn_tip( TIPREC *tip, W mode )` | テキスト入力ポートのオープン (仕様変更) |


# μT-Kernel 3.0 API 一覧（仕様書からの抽出）

`tools/spec/extract_mtk3.py` が機械的に抽出したもの。**手で編集しない。**

中心核の基準仕様（`docs/design-issues.md` D10 / 詳細 D10）。
出典は TRON Forum の μT-Kernel 3.0 仕様書（IEEE 2050-2018）。

> 仕様書そのものはリポジトリに含めない。TRON Forum の明示的な許諾なく
> 複製できないため（規律 R15 / §1）。ここにあるのはインタフェースの事実
> （関数名・シグネチャ・一行概要）だけで、解説文は含まない。

> **T-License 2.2 の実装コード（`tron-forum/mtkernel_3`）は GPLv2・LGPL の
> いずれとも非互換。仕様を読んで独自実装することだけを行う（規律 R15）。**

関数 240 件（シグネチャ取得済み 240 件）


## μT-Kernel/DS

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `td_flg_que` | `INT ct = td_flg_que(ID flgid, ID list[], INT nent)` | イベントフラグの待ち行列の参照 |
| `td_get_otm` | `ER ercd = td_get_otm(SYSTIM *tim, UW *ofs)` | システム稼働時間参照 |
| `td_get_otm_u` | `ER ercd = td_get_otm_u(SYSTIM_U *tim_u, UW *ofs)` | システム稼働時間参照(マイクロ秒単位) |
| `td_get_reg` | `ER ercd = td_get_reg(ID tskid, T_REGS *pk_regs, T_EIT *pk_eit, T_CREGS *pk_cregs)` | タスクレジスタの参照 |
| `td_get_tim` | `ER ercd = td_get_tim(SYSTIM *tim, UW *ofs)` | システム時刻参照(TRON表現) |
| `td_get_tim_u` | `ER ercd = td_get_tim_u(SYSTIM_U *tim_u, UW *ofs)` | システム時刻参照(TRON表現、マイクロ秒単位) |
| `td_get_utc` | `ER ercd = td_get_utc(SYSTIM *tim, UW *ofs)` | システム時刻参照 |
| `td_get_utc_u` | `ER ercd = td_get_utc_u(SYSTIM_U *tim_u, UW *ofs)` | システム時刻参照(マイクロ秒単位) |
| `td_hok_dsp` | `ER ercd = td_hok_dsp(CONST TD_HDSP *hdsp)` | タスクディスパッチのフックルーチン定義 |
| `td_hok_int` | `ER ercd = td_hok_int(CONST TD_HINT *hint)` | 割込みハンドラのフックルーチン定義 |
| `td_hok_svc` | `ER ercd = td_hok_svc(CONST TD_HSVC *hsvc)` | システムコール・拡張SVCのフックルーチン定義 |
| `td_lst_alm` | `INT ct = td_lst_alm(ID list[], INT nent)` | アラームハンドラIDのリスト参照 |
| `td_lst_cyc` | `INT ct = td_lst_cyc(ID list[], INT nent)` | 周期ハンドラIDのリスト参照 |
| `td_lst_flg` | `INT ct = td_lst_flg(ID list[], INT nent)` | イベントフラグIDのリスト参照 |
| `td_lst_mbf` | `INT ct = td_lst_mbf(ID list[], INT nent)` | メッセージバッファIDのリスト参照 |
| `td_lst_mbx` | `INT ct = td_lst_mbx(ID list[], INT nent)` | メールボックスIDのリスト参照 |
| `td_lst_mpf` | `INT ct = td_lst_mpf(ID list[], INT nent)` | 固定長メモリプールIDのリスト参照 |
| `td_lst_mpl` | `INT ct = td_lst_mpl(ID list[], INT nent)` | 可変長メモリプールIDのリスト参照 |
| `td_lst_mtx` | `INT ct = td_lst_mtx(ID list[], INT nent)` | ミューテックスIDのリスト参照 |
| `td_lst_sem` | `INT ct = td_lst_sem(ID list[], INT nent)` | セマフォIDのリスト参照 |
| `td_lst_ssy` | `INT ct = td_lst_ssy(ID list[], INT nent)` | サブシステムIDのリスト参照 |
| `td_lst_tsk` | `INT ct = td_lst_tsk(ID list[], INT nent)` | タスクIDのリスト参照 |
| `td_mbx_que` | `INT ct = td_mbx_que(ID mbxid, ID list[], INT nent)` | メールボックスの待ち行列の参照 |
| `td_mpf_que` | `INT ct = td_mpf_que(ID mpfid, ID list[], INT nent)` | 固定長メモリプールの待ち行列の参照 |
| `td_mpl_que` | `INT ct = td_mpl_que(ID mplid, ID list[], INT nent)` | 可変長メモリプールの待ち行列の参照 |
| `td_mtx_que` | `INT ct = td_mtx_que(ID mtxid, ID list[], INT nent)` | ミューテックスの待ち行列の参照 |
| `td_rdy_que` | `INT ct = td_rdy_que(PRI pri, ID list[], INT nent)` | タスクの優先順位の参照 |
| `td_ref_alm` | `ER ercd = td_ref_alm(ID almid, TD_RALM *ralm)` | アラームハンドラ状態参照 |
| `td_ref_alm_u` | `ER ercd = td_ref_alm_u(ID almid, TD_RALM_U *ralm_u)` | アラームハンドラ状態参照(マイクロ秒単位) |
| `td_ref_cyc` | `ER ercd = td_ref_cyc(ID cycid, TD_RCYC *rcyc)` | 周期ハンドラ状態参照 |
| `td_ref_cyc_u` | `ER ercd = td_ref_cyc_u(ID cycid, TD_RCYC_U *rcyc_u)` | 周期ハンドラ状態参照(マイクロ秒単位) |
| `td_ref_dsname` | `ER ercd = td_ref_dsname(UINT type, ID id, UB *dsname)` | DSオブジェクト名称の参照 |
| `td_ref_flg` | `ER ercd = td_ref_flg(ID flgid, TD_RFLG *rflg)` | イベントフラグ状態参照 |
| `td_ref_mbf` | `ER ercd = td_ref_mbf(ID mbfid, TD_RMBF *rmbf)` | メッセージバッファ状態参照 |
| `td_ref_mbx` | `ER ercd = td_ref_mbx(ID mbxid, TD_RMBX *rmbx)` | メールボックス状態参照 |
| `td_ref_mpf` | `ER ercd = td_ref_mpf(ID mpfid, TD_RMPF *rmpf)` | 固定長メモリプール状態参照 |
| `td_ref_mpl` | `ER ercd = td_ref_mpl(ID mplid, TD_RMPL *rmpl)` | 可変長メモリプール状態参照 |
| `td_ref_mtx` | `ER ercd = td_ref_mtx(ID mtxid, TD_RMTX *rmtx)` | ミューテックス状態参照 |
| `td_ref_sem` | `ER ercd = td_ref_sem(ID semid, TD_RSEM *rsem)` | セマフォ状態参照 |
| `td_ref_ssy` | `ER ercd = td_ref_ssy(ID ssid, TD_RSSY *rssy)` | サブシステム定義情報の参照 |
| `td_ref_sys` | `ER ercd = td_ref_sys(TD_RSYS *pk_rsys)` | システム状態参照 |
| `td_ref_tex` | `ER ercd = td_ref_tex(ID tskid, TD_RTEX *pk_rtex)` | タスク例外の状態参照 |
| `td_ref_tsk` | `ER ercd = td_ref_tsk(ID tskid, TD_RTSK *rtsk)` | タスク状態参照 |
| `td_rmbf_que` | `INT ct = td_rmbf_que(ID mbfid, ID list[], INT nent)` | メッセージバッファの受信待ち行列の参照 |
| `td_sem_que` | `INT ct = td_sem_que(ID semid, ID list[], INT nent)` | セマフォの待ち行列の参照 |
| `td_set_dsname` | `ER ercd = td_set_dsname(UINT type, ID id, CONST UB *dsname)` | DSオブジェクト名称の設定 |
| `td_set_reg` | `ER ercd = td_set_reg(ID tskid, CONST T_REGS *pk_regs, CONST T_EIT *pk_eit, CONST T_CREGS *pk_cregs)` | タスクレジスタの設定 |
| `td_smbf_que` | `INT ct = td_smbf_que(ID mbfid, ID list[], INT nent)` | メッセージバッファの送信待ち行列の参照 |


## μT-Kernel/OS

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `tk_can_wup` | `INT wupcnt = tk_can_wup(ID tskid)` | タスクの起床要求を無効化 |
| `tk_chg_pri` | `ER ercd = tk_chg_pri(ID tskid, PRI tskpri)` | タスク優先度変更 |
| `tk_clr_flg` | `ER ercd = tk_clr_flg(ID flgid, UINT clrptn)` | イベントフラグのクリア |
| `tk_cre_alm` | `ID almid = tk_cre_alm(CONST T_CALM *pk_calm)` | アラームハンドラの生成 |
| `tk_cre_cyc` | `ID cycid = tk_cre_cyc(CONST T_CCYC *pk_ccyc)` | 周期ハンドラの生成 |
| `tk_cre_cyc_u` | `ID cycid = tk_cre_cyc_u(CONST T_CCYC_U *pk_ccyc_u)` | 周期ハンドラの生成(マイクロ秒単位) |
| `tk_cre_flg` | `ID flgid = tk_cre_flg(CONST T_CFLG *pk_cflg)` | イベントフラグ生成 |
| `tk_cre_mbf` | `ID mbfid = tk_cre_mbf(CONST T_CMBF *pk_cmbf)` | メッセージバッファ生成 |
| `tk_cre_mbx` | `ID mbxid = tk_cre_mbx(CONST T_CMBX *pk_cmbx)` | メールボックス生成 |
| `tk_cre_mpf` | `ID mpfid = tk_cre_mpf(CONST T_CMPF *pk_cmpf)` | 固定長メモリプール生成 |
| `tk_cre_mpl` | `ID mplid = tk_cre_mpl(CONST T_CMPL *pk_cmpl)` | 可変長メモリプール生成 |
| `tk_cre_mtx` | `ID mtxid = tk_cre_mtx(CONST T_CMTX *pk_cmtx)` | ミューテックス生成 |
| `tk_cre_sem` | `ID semid = tk_cre_sem(CONST T_CSEM *pk_csem)` | セマフォ生成 |
| `tk_cre_tsk` | `ID tskid = tk_cre_tsk(CONST T_CTSK *pk_ctsk)` | タスク生成 |
| `tk_def_int` | `ER ercd = tk_def_int(UINT intno, CONST T_DINT *pk_dint)` | 割込みハンドラ定義 |
| `tk_def_ssy` | `ER ercd = tk_def_ssy(ID ssid, CONST T_DSSY *pk_dssy)` | サブシステム定義 |
| `tk_def_tex` | `ER ercd = tk_def_tex(ID tskid, CONST T_DTEX *pk_dtex)` | タスク例外ハンドラの定義 |
| `tk_del_alm` | `ER ercd = tk_del_alm(ID almid)` | アラームハンドラの削除 |
| `tk_del_cyc` | `ER ercd = tk_del_cyc(ID cycid)` | 周期ハンドラの削除 |
| `tk_del_flg` | `ER ercd = tk_del_flg(ID flgid)` | イベントフラグ削除 |
| `tk_del_mbf` | `ER ercd = tk_del_mbf(ID mbfid)` | メッセージバッファ削除 |
| `tk_del_mbx` | `ER ercd = tk_del_mbx(ID mbxid)` | メールボックス削除 |
| `tk_del_mpf` | `ER ercd = tk_del_mpf(ID mpfid)` | 固定長メモリプール削除 |
| `tk_del_mpl` | `ER ercd = tk_del_mpl(ID mplid)` | 可変長メモリプール削除 |
| `tk_del_mtx` | `ER ercd = tk_del_mtx(ID mtxid)` | ミューテックス削除 |
| `tk_del_sem` | `ER ercd = tk_del_sem(ID semid)` | セマフォ削除 |
| `tk_del_tsk` | `ER ercd = tk_del_tsk(ID tskid)` | タスク削除 |
| `tk_dis_dsp` | `ER ercd = tk_dis_dsp(void)` | ディスパッチ禁止 |
| `tk_dis_tex` | `ER ercd = tk_dis_tex(ID tskid, UINT texptn)` | タスク例外の禁止 |
| `tk_dis_wai` | `INT tskwait = tk_dis_wai(ID tskid, UW waitmask)` | タスク待ち状態の禁止 |
| `tk_dly_tsk` | `ER ercd = tk_dly_tsk(RELTIM dlytim)` | タスク遅延 |
| `tk_dly_tsk_u` | `ER ercd = tk_dly_tsk_u(RELTIM_U dlytim_u)` | タスク遅延(マイクロ秒単位) |
| `tk_ena_dsp` | `ER ercd = tk_ena_dsp(void)` | ディスパッチ許可 |
| `tk_ena_tex` | `ER ercd = tk_ena_tex(ID tskid, UINT texptn)` | タスク例外の許可 |
| `tk_ena_wai` | `ER ercd = tk_ena_wai(ID tskid)` | タスク待ち禁止の解除 |
| `tk_end_tex` | `INT texcd = tk_end_tex(BOOL enatex)` | タスク例外ハンドラの終了 |
| `tk_evt_ssy` | `ER ercd = tk_evt_ssy(ID ssid, INT evttyp, INT info)` | イベント処理関数呼出 |
| `tk_exd_tsk` | `void tk_exd_tsk(void)` | 自タスクの終了と削除 |
| `tk_ext_tsk` | `void tk_ext_tsk(void)` | 自タスク終了 |
| `tk_frsm_tsk` | `ER ercd = tk_frsm_tsk(ID tskid)` | 強制待ち状態のタスクを強制再開 |
| `tk_get_cpr` | `ER ercd = tk_get_cpr(ID tskid, INT copno, T_COPREGS *pk_copregs)` | コプロセッサのレジスタの取得 |
| `tk_get_mpf` | `ER ercd = tk_get_mpf(ID mpfid, void **p_blf, TMO tmout)` | 固定長メモリブロック獲得 |
| `tk_get_mpf_u` | `ER ercd = tk_get_mpf_u(ID mpfid, void **p_blf, TMO_U tmout_u)` | 固定長メモリブロック獲得(マイクロ秒単位) |
| `tk_get_mpl` | `ER ercd = tk_get_mpl(ID mplid, SZ blksz, void **p_blk, TMO tmout)` | 可変長メモリブロック獲得 |
| `tk_get_mpl_u` | `ER ercd = tk_get_mpl_u(ID mplid, SZ blksz, void **p_blk, TMO_U tmout_u)` | 可変長メモリブロック獲得(マイクロ秒単位) |
| `tk_get_otm` | `ER ercd = tk_get_otm(SYSTIM *pk_tim)` | システム稼働時間参照 |
| `tk_get_otm_u` | `ER ercd = tk_get_otm_u(SYSTIM_U *tim_u, UW *ofs)` | システム稼働時間参照(マイクロ秒単位) |
| `tk_get_reg` | `ER ercd = tk_get_reg(ID tskid, T_REGS *pk_regs, T_EIT *pk_eit, T_CREGS *pk_cregs)` | タスクレジスタの取得 |
| `tk_get_tid` | `ID tskid = tk_get_tid(void)` | 実行状態タスクのタスクID参照 |
| `tk_get_tim` | `ER ercd = tk_get_tim(SYSTIM *pk_tim)` | システム時刻参照(TRON表現) |
| `tk_get_tim_u` | `ER ercd = tk_get_tim_u(SYSTIM_U *tim_u, UW *ofs)` | システム時刻参照(TRON表現、マイクロ秒単位) |
| `tk_get_utc` | `ER ercd = tk_get_utc(SYSTIM *pk_tim)` | システム時刻参照 |
| `tk_get_utc_u` | `ER ercd = tk_get_utc_u(SYSTIM_U *tim_u, UW *ofs)` | システム時刻参照(マイクロ秒単位) |
| `tk_loc_mtx` | `ER ercd = tk_loc_mtx(ID mtxid, TMO tmout)` | ミューテックスのロック |
| `tk_loc_mtx_u` | `ER ercd = tk_loc_mtx_u(ID mtxid, TMO_U tmout_u)` | ミューテックスのロック(マイクロ秒単位) |
| `tk_ras_tex` | `ER ercd = tk_ras_tex(ID tskid, INT texcd)` | タスク例外を発生 |
| `tk_rcv_mbf` | `INT msgsz = tk_rcv_mbf(ID mbfid, void *msg, TMO tmout)` | メッセージバッファから受信 |
| `tk_rcv_mbf_u` | `INT msgsz = tk_rcv_mbf_u(ID mbfid, void *msg, TMO_U tmout_u)` | メッセージバッファから受信(マイクロ秒単位) |
| `tk_rcv_mbx` | `ER ercd = tk_rcv_mbx(ID mbxid, T_MSG **ppk_msg, TMO tmout)` | メールボックスから受信 |
| `tk_rcv_mbx_u` | `ER ercd = tk_rcv_mbx_u(ID mbxid, T_MSG **ppk_msg, TMO_U tmout_u)` | メールボックスから受信(マイクロ秒単位) |
| `tk_ref_alm` | `ER ercd = tk_ref_alm(ID almid, T_RALM *pk_ralm)` | アラームハンドラ状態参照 |
| `tk_ref_alm_u` | `ER ercd = tk_ref_alm_u(ID almid, T_RALM_U *pk_ralm_u)` | アラームハンドラ状態参照(マイクロ秒単位) |
| `tk_ref_cyc` | `ER ercd = tk_ref_cyc(ID cycid, T_RCYC *pk_rcyc)` | 周期ハンドラ状態参照 |
| `tk_ref_cyc_u` | `ER ercd = tk_ref_cyc_u(ID cycid, T_RCYC_U *pk_rcyc_u)` | 周期ハンドラ状態参照(マイクロ秒単位) |
| `tk_ref_flg` | `ER ercd = tk_ref_flg(ID flgid, T_RFLG *pk_rflg)` | イベントフラグ状態参照 |
| `tk_ref_mbf` | `ER ercd = tk_ref_mbf(ID mbfid, T_RMBF *pk_rmbf)` | メッセージバッファ状態参照 |
| `tk_ref_mbx` | `ER ercd = tk_ref_mbx(ID mbxid, T_RMBX *pk_rmbx)` | メールボックス状態参照 |
| `tk_ref_mpf` | `ER ercd = tk_ref_mpf(ID mpfid, T_RMPF *pk_rmpf)` | 固定長メモリプール状態参照 |
| `tk_ref_mpl` | `ER ercd = tk_ref_mpl(ID mplid, T_RMPL *pk_rmpl)` | 可変長メモリプール状態参照 |
| `tk_ref_mtx` | `ER ercd = tk_ref_mtx(ID mtxid, T_RMTX *pk_rmtx)` | ミューテックス状態参照 |
| `tk_ref_sem` | `ER ercd = tk_ref_sem(ID semid, T_RSEM *pk_rsem)` | セマフォ状態参照 |
| `tk_ref_ssy` | `ER ercd = tk_ref_ssy(ID ssid, T_RSSY *pk_rssy)` | サブシステム定義情報の参照 |
| `tk_ref_sys` | `ER ercd = tk_ref_sys(T_RSYS *pk_rsys)` | システム状態参照 |
| `tk_ref_tex` | `ER ercd = tk_ref_tex(ID tskid, T_RTEX *pk_rtex)` | タスク例外の状態参照 |
| `tk_ref_tsk` | `ER ercd = tk_ref_tsk(ID tskid, T_RTSK *pk_rtsk)` | タスク状態参照 |
| `tk_ref_ver` | `ER ercd = tk_ref_ver(T_RVER *pk_rver)` | バージョン参照 |
| `tk_rel_mpf` | `ER ercd = tk_rel_mpf(ID mpfid, void *blf)` | 固定長メモリブロック返却 |
| `tk_rel_mpl` | `ER ercd = tk_rel_mpl(ID mplid, void *blk)` | 可変長メモリブロック返却 |
| `tk_rel_wai` | `ER ercd = tk_rel_wai(ID tskid)` | 他タスクの待ち状態解除 |
| `tk_ret_int` | `void tk_ret_int(void)` | 割込みハンドラから復帰 |
| `tk_rot_rdq` | `ER ercd = tk_rot_rdq(PRI tskpri)` | タスクの優先順位の回転 |
| `tk_rsm_tsk` | `ER ercd = tk_rsm_tsk(ID tskid)` | 強制待ち状態のタスクを再開 |
| `tk_set_cpr` | `ER ercd = tk_set_cpr(ID tskid, INT copno, CONST T_COPREGS *pk_copregs)` | コプロセッサのレジスタの設定 |
| `tk_set_flg` | `ER ercd = tk_set_flg(ID flgid, UINT setptn)` | イベントフラグのセット |
| `tk_set_pow` | `ER ercd = tk_set_pow(UINT powmode)` | 省電力モード設定 |
| `tk_set_reg` | `ER ercd = tk_set_reg(ID tskid, CONST T_REGS *pk_regs, CONST T_EIT *pk_eit, CONST T_CREGS *pk_cregs)` | タスクレジスタの設定 |
| `tk_set_tim` | `ER ercd = tk_set_tim(CONST SYSTIM *pk_tim)` | システム時刻設定(TRON表現) |
| `tk_set_tim_u` | `ER ercd = tk_set_tim_u(SYSTIM_U tim_u)` | システム時刻設定(TRON表現、マイクロ秒単位) |
| `tk_set_utc` | `ER ercd = tk_set_utc(CONST SYSTIM *pk_tim)` | システム時刻設定 |
| `tk_set_utc_u` | `ER ercd = tk_set_utc_u(SYSTIM_U tim_u)` | システム時刻設定(マイクロ秒単位) |
| `tk_sig_sem` | `ER ercd = tk_sig_sem(ID semid, INT cnt)` | セマフォ資源返却 |
| `tk_sig_tev` | `ER ercd = tk_sig_tev(ID tskid, INT tskevt)` | タスクイベントの送信 |
| `tk_slp_tsk` | `ER ercd = tk_slp_tsk(TMO tmout)` | 自タスクを起床待ち状態へ移行 |
| `tk_slp_tsk_u` | `ER ercd = tk_slp_tsk_u(TMO_U tmout_u)` | 自タスクを起床待ち状態へ移行(マイクロ秒単位) |
| `tk_snd_mbf` | `ER ercd = tk_snd_mbf(ID mbfid, CONST void *msg, INT msgsz, TMO tmout)` | メッセージバッファへ送信 |
| `tk_snd_mbf_u` | `ER ercd = tk_snd_mbf_u(ID mbfid, CONST void *msg, INT msgsz, TMO_U tmout_u)` | メッセージバッファへ送信(マイクロ秒単位) |
| `tk_snd_mbx` | `ER ercd = tk_snd_mbx(ID mbxid, T_MSG *pk_msg)` | メールボックスへ送信 |
| `tk_sta_alm` | `ER ercd = tk_sta_alm(ID almid, RELTIM almtim)` | アラームハンドラの動作開始 |
| `tk_sta_alm_u` | `ER ercd = tk_sta_alm_u(ID almid, RELTIM_U almtim_u)` | アラームハンドラの動作開始(マイクロ秒単位) |
| `tk_sta_cyc` | `ER ercd = tk_sta_cyc(ID cycid)` | 周期ハンドラの動作開始 |
| `tk_sta_tsk` | `ER ercd = tk_sta_tsk(ID tskid, INT stacd)` | タスク起動 |
| `tk_stp_alm` | `ER ercd = tk_stp_alm(ID almid)` | アラームハンドラの動作停止 |
| `tk_stp_cyc` | `ER ercd = tk_stp_cyc(ID cycid)` | 周期ハンドラの動作停止 |
| `tk_sus_tsk` | `ER ercd = tk_sus_tsk(ID tskid)` | 他タスクを強制待ち状態へ移行 |
| `tk_ter_tsk` | `ER ercd = tk_ter_tsk(ID tskid)` | 他タスク強制終了 |
| `tk_unl_mtx` | `ER ercd = tk_unl_mtx(ID mtxid)` | ミューテックスのアンロック |
| `tk_wai_flg` | `ER ercd = tk_wai_flg(ID flgid, UINT waiptn, UINT wfmode, UINT *p_flgptn, TMO tmout)` | イベントフラグ待ち |
| `tk_wai_flg_u` | `ER ercd = tk_wai_flg_u(ID flgid, UINT waiptn, UINT wfmode, UINT *p_flgptn, TMO_U tmout_u)` | イベントフラグ待ち(マイクロ秒単位) |
| `tk_wai_sem` | `ER ercd = tk_wai_sem(ID semid, INT cnt, TMO tmout)` | セマフォ資源獲得 |
| `tk_wai_sem_u` | `ER ercd = tk_wai_sem_u(ID semid, INT cnt, TMO_U tmout_u)` | セマフォ資源獲得(マイクロ秒単位) |
| `tk_wai_tev` | `INT tevptn = tk_wai_tev(UINT waiptn, TMO tmout)` | タスクイベント待ち |
| `tk_wai_tev_u` | `INT tevptn = tk_wai_tev_u(UINT waiptn, TMO_U tmout_u)` | タスクイベント待ち(マイクロ秒単位) |
| `tk_wup_tsk` | `ER ercd = tk_wup_tsk(ID tskid)` | 他タスクの起床 |


## μT-Kernel/SM

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `tk_cls_dev` | `ER ercd = tk_cls_dev(ID dd, UINT option)` | デバイスのクローズ |
| `tk_def_dev` | `ID devid = tk_def_dev(CONST UB *devnm, CONST T_DDEV *ddev, T_IDEV *idev)` | デバイスの登録 |
| `tk_evt_dev` | `INT retcode = tk_evt_dev(ID devid, INT evttyp, void *evtinf)` | デバイスにドライバ要求イベントを送信 |
| `tk_get_cfn` | `INT ct = tk_get_cfn(CONST UB *name, W *val, INT max)` | システム構成情報から数値列取得 |
| `tk_get_cfs` | `INT rlen = tk_get_cfs(CONST UB *name, UB *buf, INT max)` | システム構成情報から文字列取得 |
| `tk_get_dev` | `ID pdevid = tk_get_dev(ID devid, UB *devnm)` | デバイスのデバイス名取得 |
| `tk_lst_dev` | `INT remcnt = tk_lst_dev(T_LDEV *ldev, INT start, INT ndev)` | 登録済みデバイス一覧の取得 |
| `tk_opn_dev` | `ID dd = tk_opn_dev(CONST UB *devnm, UINT omode)` | デバイスのオープン |
| `tk_oref_dev` | `ID devid = tk_oref_dev(ID dd, T_RDEV *rdev)` | デバイスのデバイス情報取得 |
| `tk_rea_dev` | `ID reqid = tk_rea_dev(ID dd, W start, void *buf, SZ size, TMO tmout)` | デバイスの読込み開始 |
| `tk_rea_dev_du` | `ID reqid = tk_rea_dev_du(ID dd, D start_d, void *buf, SZ size, TMO_U tmout_u)` | デバイスの読込み開始(64ビットマイクロ秒単位) |
| `tk_ref_dev` | `ID devid = tk_ref_dev(CONST UB *devnm, T_RDEV *rdev)` | デバイスのデバイス情報取得 |
| `tk_ref_idv` | `ER ercd = tk_ref_idv(T_IDEV *idev)` | デバイス初期情報の取得 |
| `tk_srea_dev` | `ER ercd = tk_srea_dev(ID dd, W start, void *buf, SZ size, SZ *asize)` | デバイスの同期読込み |
| `tk_srea_dev_d` | `ER ercd = tk_srea_dev_d(ID dd, D start_d, void *buf, SZ size, SZ *asize)` | デバイスの同期読込み(64ビット) |
| `tk_sus_dev` | `INT dissus = tk_sus_dev(UINT mode)` | デバイスのサスペンド |
| `tk_swri_dev` | `ER ercd = tk_swri_dev(ID dd, W start, CONST void *buf, SZ size, SZ *asize)` | デバイスの同期書込み |
| `tk_swri_dev_d` | `ER ercd = tk_swri_dev_d(ID dd, D start_d, CONST void *buf, SZ size, SZ *asize)` | デバイスの同期書込み(64ビット) |
| `tk_wai_dev` | `ID creqid = tk_wai_dev(ID dd, ID reqid, SZ *asize, ER *ioer, TMO tmout)` | デバイスの要求完了待ち |
| `tk_wai_dev_u` | `ID creqid = tk_wai_dev_u(ID dd, ID reqid, SZ *asize, ER *ioer, TMO_U tmout_u)` | デバイスの要求完了待ち(マイクロ秒単位) |
| `tk_wri_dev` | `ID reqid = tk_wri_dev(ID dd, W start, CONST void *buf, SZ size, TMO tmout)` | デバイスの書込み開始 |
| `tk_wri_dev_du` | `ID reqid = tk_wri_dev_du(ID dd, D start_d, CONST void *buf, SZ size, TMO_U tmout_u)` | デバイスの書込み開始(64ビットマイクロ秒単位) |


## ユーティリティ / システムライブラリ

| 関数 | シグネチャ | 概要 |
|---|---|---|
| `CheckInt` | `BOOL rasint = CheckInt(UINT intno)` |  |
| `ClearInt` | `void ClearInt(UINT intno)` |  |
| `ControlCache` | `SZ rlen = ControlCache(void *addr, SZ len, UINT mode)` |  |
| `CreateLock` | `ER ercd = CreateLock(FastLock *lock, CONST UB *name)` |  |
| `CreateMLock` | `ER ercd = CreateMLock(FastMLock *lock, CONST UB *name)` |  |
| `DI` | `DI(UINT intsts)` |  |
| `DefinePhysicalTimerHandler` | `ER ercd = DefinePhysicalTimerHandler(UINT ptmrno, CONST T_DPTMR *pk_dptmr)` |  |
| `DeleteLock` | `void DeleteLock(FastLock *lock)` |  |
| `DeleteMLock` | `ER ercd = DeleteMLock(FastMLock *lock)` |  |
| `DisableInt` | `void DisableInt(UINT intno)` |  |
| `EI` | `EI(UINT intsts)` |  |
| `EnableInt` | `void EnableInt(UINT intno)` |  |
| `EndOfInt` | `void EndOfInt(UINT intno)` |  |
| `GetCpuIntLevel` | `INT level = GetCpuIntLevel(void)` |  |
| `GetCtrlIntLevel` | `INT level = GetCtrlIntLevel(void)` |  |
| `GetPhysicalTimerConfig` | `ER ercd = GetPhysicalTimerConfig(UINT ptmrno, T_RPTMR *pk_rptmr)` |  |
| `GetPhysicalTimerCount` | `ER ercd = GetPhysicalTimerCount(UINT ptmrno, UW *p_count)` |  |
| `Kcalloc` | `void* Kcalloc(size_t nmemb, size_t size)` |  |
| `Kfree` | `void Kfree(void *ptr)` |  |
| `Kmalloc` | `void* Kmalloc(size_t size)` |  |
| `Krealloc` | `void* Krealloc(void *ptr, size_t size)` |  |
| `Lock` | `void Lock(FastLock *lock)` |  |
| `MLock` | `ER ercd = MLock(FastMLock *lock, INT no)` |  |
| `MLockTmo` | `ER ercd = MLockTmo(FastMLock *lock, INT no, TMO tmout)` |  |
| `MLockTmo_u` | `ER ercd = MLockTmo_u(FastMLock *lock, INT no, TMO_U tmout_u)` |  |
| `MUnlock` | `ER ercd = MUnlock(FastMLock *lock, INT no)` |  |
| `SetCacheMode` | `SZ rlen = SetCacheMode(void *addr, SZ len, UINT mode)` |  |
| `SetCpuIntLevel` | `void SetCpuIntLevel(INT level)` |  |
| `SetCtrlIntLevel` | `void SetCtrlIntLevel(INT level)` |  |
| `SetIntMode` | `void SetIntMode(UINT intno, UINT mode)` |  |
| `SetOBJNAME` | `void SetOBJNAME(void *exinf, CONST UB *name)` |  |
| `StartPhysicalTimer` | `ER ercd = StartPhysicalTimer(UINT ptmrno, UW limit, UINT mode)` |  |
| `StopPhysicalTimer` | `ER ercd = StopPhysicalTimer(UINT ptmrno)` |  |
| `Unlock` | `void Unlock(FastLock *lock)` |  |
| `WaitNsec` | `void WaitNsec(UW nsec)` |  |
| `WaitUsec` | `void WaitUsec(UW usec)` |  |
| `abortfn` | `ER ercd = abortfn(ID tskid, T_DEVREQ *devreq, INT nreq, void *exinf)` |  |
| `closefn` | `ER ercd = closefn(ID devid, UINT option, void *exinf)` |  |
| `enter` | `void *enter(FN fncd, TD_CALINF *calinf, ... )` |  |
| `eventfn` | `INT retcode = eventfn(INT evttyp, void *evtinf, void *exinf)` |  |
| `exec` | `void exec(ID tskid)` |  |
| `execfn` | `ER ercd = execfn(T_DEVREQ *devreq, TMO tmout, void *exinf)` |  |
| `in_b` | `UB data = in_b(INT port)` |  |
| `in_d` | `UD data = in_d(INT port)` |  |
| `in_h` | `UH data = in_h(INT port)` |  |
| `in_w` | `UW data = in_w(INT port)` |  |
| `isDI` | `BOOL disint = isDI(UINT intsts)` |  |
| `leave` | `void leave(FN fncd, INT ret, void *exinf)` |  |
| `low_pow` | `void low_pow(void)` |  |
| `off_pow` | `void off_pow(void)` |  |
| `openfn` | `ER ercd = openfn(ID devid, UINT omode, void *exinf)` |  |
| `out_b` | `void out_b(INT port, UB data)` |  |
| `out_d` | `void out_d(INT port, UD data)` |  |
| `out_h` | `void out_h(INT port, UH data)` |  |
| `out_w` | `void out_w(INT port, UW data)` |  |
| `stop` | `void stop(ID tskid, UINT tskstat)` |  |
| `waitfn` | `INT creqno = waitfn(T_DEVREQ *devreq, INT nreq, TMO tmout, void *exinf)` |  |


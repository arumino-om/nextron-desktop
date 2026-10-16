# マネージャの利用側スタブ (LGPL 予定)
#
# サーバ本体は src/subsystems/。ここにあるのは「ランデブで要求を出す」
# 側だけで、アプリケーションに結合される。

set(BTRON_SRCS_APPL
  ${CMAKE_CURRENT_LIST_DIR}/font/fontcli.c
  ${CMAKE_CURRENT_LIST_DIR}/mgr/mgrcli.c)

# ウィンドウマネージャの利用側。カーネルにも入る（検査が呼ぶ）。
set(BTRON_SRCS_APPL_WINDOW
  ${CMAKE_CURRENT_LIST_DIR}/window/wincli.c)

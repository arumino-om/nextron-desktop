# 検査区分 (W0-1)
#
# **列挙しないのは意図的である。** 区分を足す作業がこのファイルを
# 触らずに済むようにする（規律 R29）。登録はリンカのセクションに
# 集まるので、ファイルを置けばそれだけで走る。
# CONFIGURE_DEPENDS を付けてあるので ninja が構成からやり直す。

file(GLOB BTRON_SRCS_TESTS CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_LIST_DIR}/*.c")

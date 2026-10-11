"""仕様文書の所在解決と共通定義。

仕様文書 (btron3.doc / brightv.r4.doc) は著作権者が別にあり
リポジトリにはコミットしない (docs/design-issues.md §1)。
各自のローカルに置いたものを探しに行く。

探索順 (先に見つかったものを使う。どれを使ったかは呼び出し側が表示する):
  1. 環境変数 BTRON_DOC / BRIGHTV_DOC
  2. リポジトリルート直下
  3. git worktree の本体チェックアウト直下
     (worktree で作業していて、本体側に置いてある場合)
  4. ~/Downloads 直下
"""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

# 変換結果の置き場。.gitignore 済み。
WORK = REPO / ".spec-work"
TEXT_DIR = WORK / "text"
FIG_DIR = WORK / "fig"

# 抽出した「事実」の置き場。これはコミットする。
API_DIR = REPO / "docs" / "api"


def _main_worktree() -> Path | None:
    """git worktree の本体チェックアウトのルートを返す。"""
    try:
        common_dir = subprocess.run(
            ["git", "rev-parse", "--path-format=absolute", "--git-common-dir"],
            cwd=REPO,
            capture_output=True,
            text=True,
            check=True,
        ).stdout.strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return None
    return Path(common_dir).parent if common_dir.endswith("/.git") else None


def _candidates(name: str, env: str):
    v = os.environ.get(env)
    if v:
        yield Path(v).expanduser()
    yield REPO / name
    main = _main_worktree()
    if main is not None:
        yield main / name
    yield Path.home() / "Downloads" / name


def find_doc(name: str, env: str) -> Path:
    for c in _candidates(name, env):
        if c.is_dir() and (c / "index.html").exists():
            return c.resolve()
    sys.exit(
        f"エラー: {name} が見つかりません。\n"
        f"  環境変数 {env} にパスを設定するか、{REPO} 直下に置いてください。\n"
        f"  (リポジトリにはコミットしません。docs/design-issues.md §1 を参照)"
    )


def btron3() -> Path:
    return find_doc("btron3.doc", "BTRON_DOC")


def brightv() -> Path:
    return find_doc("brightv.r4.doc", "BRIGHTV_DOC")


# 仕様文書は EUC-JP
ENCODING = "euc_jp"

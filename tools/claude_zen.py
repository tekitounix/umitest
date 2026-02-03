#!/usr/bin/env python3
"""
claude-zen - claude コマンドで claudish + Zen free モデルを使うエイリアス管理

Usage (シェル関数経由で使用):
    claude-zen          - インタラクティブにモデル選択・有効化/無効化
    claude-zen status   - 現在の状態を表示

Setup (.zshrc に追加):
    claude-zen() {
      local a=$(alias claude 2>/dev/null | sed "s/.*='\\(.*\\)'/\\1/")
      eval "$(CLAUDE_ALIAS="$a" python3 ~/work/umi/tools/claude_zen.py "$@")"
    }
"""

import os
import re
import sys

ZEN_FREE_MODELS = [
    "zen@gpt-5-nano",
    "zen@kimi-k2.5-free",
    "zen@grok-code",
    "zen@glm-4.7-free",
    "zen@minimax-m2.1-free",
    "zen@big-pickle",
    "zen@trinity-large-preview-free",
]

CONFIG_DIR = os.path.join(os.path.expanduser("~"), ".config", "claude-zen")
DEFAULT_FILE = os.path.join(CONFIG_DIR, "default_model.txt")
ZSHRC = os.path.join(os.path.expanduser("~"), ".zshrc")

# .zshrc に追加するマーカー
MARKER_START = "# >>> claude-zen alias >>>"
MARKER_END = "# <<< claude-zen alias <<<"


def msg(text):
    """ユーザー向けメッセージ (stderr)"""
    print(text, file=sys.stderr)


def cmd(text):
    """シェルで実行するコマンド (stdout)"""
    print(text)


def ensure_config():
    os.makedirs(CONFIG_DIR, exist_ok=True)


def load_default():
    ensure_config()
    if os.path.exists(DEFAULT_FILE):
        with open(DEFAULT_FILE) as f:
            m = f.read().strip()
            if m in ZEN_FREE_MODELS:
                return m
    # デフォルト未設定時は先頭を使う
    default = ZEN_FREE_MODELS[0]
    with open(DEFAULT_FILE, "w") as f:
        f.write(default)
    return default


def save_default(model):
    ensure_config()
    with open(DEFAULT_FILE, "w") as f:
        f.write(model)


def read_zshrc():
    if os.path.exists(ZSHRC):
        with open(ZSHRC, "r") as f:
            return f.read()
    return ""


def write_zshrc(content):
    with open(ZSHRC, "w") as f:
        f.write(content)


def remove_alias_block(content):
    """既存の claude-zen ブロックを削除"""
    pattern = re.compile(
        rf"^{re.escape(MARKER_START)}.*?{re.escape(MARKER_END)}\n?",
        re.MULTILINE | re.DOTALL,
    )
    return pattern.sub("", content)


def get_alias_block(model):
    """エイリアスブロックを生成"""
    return f"""{MARKER_START}
alias claude='claudish --model {model}'
{MARKER_END}
"""


def is_enabled():
    """エイリアスが有効かどうか"""
    content = read_zshrc()
    return MARKER_START in content


def get_current_alias_model():
    """現在設定されているエイリアスのモデルを取得 (.zshrc から)"""
    content = read_zshrc()
    match = re.search(r"alias claude='claudish --model ([^']+)'", content)
    if match:
        return match.group(1)
    return None


def get_shell_alias():
    """実際のシェルで有効なエイリアスを取得 (環境変数経由)"""
    alias_val = os.environ.get("CLAUDE_ALIAS", "")
    if alias_val:
        # "claudish --model zen@xxx" 形式から抽出
        match = re.search(r"claudish --model (\S+)", alias_val)
        if match:
            return match.group(1)
    return None


def enable_alias(model):
    content = read_zshrc()

    # 既存のブロックを削除してから追加
    content = remove_alias_block(content)

    # 末尾に改行がなければ追加
    if content and not content.endswith("\n"):
        content += "\n"

    content += get_alias_block(model)
    write_zshrc(content)
    save_default(model)


def disable_alias():
    content = read_zshrc()

    if MARKER_START not in content:
        return False

    content = remove_alias_block(content)
    write_zshrc(content)
    return True


def show_status():
    model = load_default()
    zshrc_enabled = is_enabled()
    zshrc_model = get_current_alias_model()
    shell_model = get_shell_alias()

    msg("claude-zen status:")
    msg(f"  Default model: {model}")
    msg(f"  .zshrc: {'enabled' if zshrc_enabled else 'disabled'}" + (f" ({zshrc_model})" if zshrc_model else ""))
    if shell_model:
        msg(f"  Shell:  claude -> claudish --model {shell_model}")
    else:
        msg(f"  Shell:  (alias not set)")
    
    # 不一致警告
    if zshrc_model and shell_model and zshrc_model != shell_model:
        msg(f"  ⚠ .zshrc と現在のシェルで設定が異なります")


def interactive():
    """インタラクティブモード"""
    current = load_default()
    zshrc_enabled = is_enabled()
    zshrc_model = get_current_alias_model()
    shell_model = get_shell_alias()

    msg("claude-zen - Zen free モデルでclaudeエイリアスを設定")
    msg("")

    # 現在の状態
    if shell_model:
        msg(f"現在: claude -> claudish --model {shell_model}")
    elif zshrc_model:
        msg(f"現在: 未反映 (.zshrc: {zshrc_model})")
    else:
        msg("現在: 無効")
    msg("")

    # 選択肢を表示
    msg("番号を選択してください:")
    msg("  0. 無効化 (disable)")
    for i, m in enumerate(ZEN_FREE_MODELS, start=1):
        marker = " ← current" if m == shell_model else ""
        msg(f"  {i}. {m}{marker}")
    msg("")

    try:
        choice = input("選択 [0-{}]: ".format(len(ZEN_FREE_MODELS))).strip()
        if not choice:
            msg("キャンセル")
            return
        idx = int(choice)
    except (ValueError, EOFError, KeyboardInterrupt):
        msg("\nキャンセル")
        return

    if idx == 0:
        # 無効化
        if disable_alias():
            msg("")
            msg("✓ 無効化しました")
            # 親シェルで unalias を実行
            cmd("unalias claude 2>/dev/null || true")
        else:
            msg("既に無効です")
    elif 1 <= idx <= len(ZEN_FREE_MODELS):
        model = ZEN_FREE_MODELS[idx - 1]
        enable_alias(model)
        msg("")
        msg(f"✓ 設定: alias claude='claudish --model {model}'")
        # 親シェルでエイリアスを即時設定
        cmd(f"alias claude='claudish --model {model}'")
    else:
        msg("無効な選択")
        sys.exit(1)


def print_help():
    msg("claude-zen - claude コマンドで claudish + Zen free モデルを使う")
    msg("")
    msg("Usage:")
    msg("  claude-zen          - インタラクティブにモデル選択・有効化/無効化")
    msg("  claude-zen status   - 現在の状態を表示")
    msg("")
    msg("Setup (.zshrc に追加):")
    msg('  claude-zen() {')
    msg('    local a=$(alias claude 2>/dev/null | sed "s/.*=\'\\(.*\\)\'/\\1/")')
    msg('    eval "$(CLAUDE_ALIAS="$a" python3 ~/work/umi/tools/claude_zen.py "$@")"')
    msg('  }')


def main():
    args = sys.argv[1:]

    if not args:
        interactive()
        return

    cmd = args[0]

    if cmd in ("-h", "--help", "help"):
        print_help()
    elif cmd == "status":
        show_status()
    else:
        print(f"Unknown command: {cmd}")
        print("Run 'claude-zen --help' for usage.")
        sys.exit(1)


if __name__ == "__main__":
    main()

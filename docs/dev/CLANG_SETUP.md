# Clang ツールセットアップガイド

clang-tidy / clang-format / clangd と ARM embedded ツールチェーンとの互換性に関する設定ガイド。

正本: [CODING_RULE.md](../../lib/docs/standards/CODING_RULE.md)（.clang-format / .clang-tidy / .clangd の設定ファイル）
評価レポート: [CODE_QUALITY_NOTES.md](CODE_QUALITY_NOTES.md)

---

## 1. 問題: arm-embedded ツールチェーンとの互換性

clang-arm 21.1.0/21.1.1 の multilib.yaml に含まれる `IncludeDirs` キーが、clang-tidy 20.x で認識されない。

### エラーメッセージ

```
error: unknown key 'IncludeDirs' in multilib.yaml
```

または

```
error: no multilib found matching flags: --target=thumbv7em-unknown-none-eabihf ...
```

---

## 2. 解決策

### 方法1: clang-tidy をアップデート（推奨）

```bash
# macOS
brew upgrade llvm

# Ubuntu/Debian
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 21
```

LLVM 21.x 以降では multilib.yaml の IncludeDirs がサポートされています。

### 方法2: ツールチェーンを変更

embedded ターゲットで `gcc-arm` を使用するように変更：

```lua
-- lib/yourlib/target/stm32f4/xmake.lua
set_values("embedded.toolchain", "gcc-arm")
```

gcc-arm は multilib.yaml を使用しないため、clang-tidy との競合は発生しません。

### 方法3: 組み込みターゲットを除外

```bash
# ホストターゲットのみチェック
xmake check clang.tidy
```

デフォルトではホストターゲットのみがチェック対象です。

---

## 3. multilib.yaml ワークアラウンド

方法1-3 が適用できない場合の一時的な切り替え手順。

### clang-tidy チェック前

```bash
# multilib.yaml を patched バージョンに切り替え
MV_FILE="$HOME/.xmake/packages/c/clang-arm/21.1.1/*/lib/clang-runtimes/multilib.yaml"
cp "${MV_FILE}.tidy" "$MV_FILE"
```

### clang-tidy 実行

```bash
xmake check clang.tidy
```

### 元に戻す（ビルド前に必ず実行）

```bash
# 元の multilib.yaml を復元
MV_FILE="$HOME/.xmake/packages/c/clang-arm/21.1.1/*/lib/clang-runtimes/multilib.yaml"
cp "${MV_FILE}.orig" "$MV_FILE" 2>/dev/null || xmake require --force clang-arm@21.1.1
```

### 恒久対策

- arm-embedded パッケージに自動切り替え機能を追加予定
- または clang-arm 22.x での修正を待つ

---

## 関連コミット

- arm-embedded-xmake-repo: 警告メッセージを追加

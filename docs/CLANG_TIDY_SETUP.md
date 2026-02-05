# Clang-Tidy セットアップガイド

## 問題: arm-embedded ツールチェーンとの互換性

clang-arm 21.1.0/21.1.1 の multilib.yaml に含まれる `IncludeDirs` キーが、clang-tidy 20.x で認識されない。

## エラーメッセージ

```
error: unknown key 'IncludeDirs' in multilib.yaml
```

または

```
error: no multilib found matching flags: --target=thumbv7em-unknown-none-eabihf ...
```

## 解決策

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

## 関連コミット

- arm-embedded-xmake-repo: 警告メッセージを追加

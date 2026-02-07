# Clang ツール設定ガイド

**場所**: `lib/docs/CLANG_TOOLING.md`  
**関連**: `CLAUDE.md`, `CODING_STYLE.md`

---

## 概要

UMI プロジェクトの clang ツール設定戦略を説明します。目標は、コード品質を維持しながら、組み込み特有のパターン（STM32 レジスタ型、ISR）に対応することです。

**原則**: グローバルな例外ではなく、**特定のディレクトリに局所的な設定ファイル**を使用する。これにより、アプリケーションコードでの意図しない警告抑制を防ぎます。

---

## セットアップ

### 初期設定

```bash
# 設定ファイルを生成（.clangd, .clang-format, .clang-tidy, .vscode/settings.json）
xmake coding --init --force

# compile_commands.json を生成
xmake project -k compile_commands
```

### 生成されるファイル

| ファイル | 目的 |
|----------|------|
| `.clangd` | 言語サーバー設定（C++23、ARMフラグ除去、ClangTidy設定） |
| `.clang-format` | コードフォーマット規則 |
| `.clang-tidy` | 静的解析チェック |
| `.vscode/settings.json` | clangd CLI オプション（`--query-driver` 等） |
| `compile_commands.json` | コンパイルコマンドデータベース |

---

## 設定ファイル詳細

### .clangd

```yaml
CompileFlags:
  Add:
    - "-std=c++23"    # ヘッダファイル用（compile_commands.json にエントリがない場合）
  Remove:
    - -mfpu=*
    - -mfloat-abi=*
    - -mcpu=*
    - -mthumb
    - --specs=*
    - -fno-exceptions
    - -fno-rtti

Diagnostics:
  Suppress:
    - pp_file_not_found
  ClangTidy:
    Add:
      - bugprone-*
      - clang-analyzer-*
      - performance-*
      - modernize-*
      - readability-*
      - misc-*
    Remove:
      - bugprone-dynamic-static-initializers
      - bugprone-easily-swappable-parameters
      - clang-analyzer-core.FixedAddressDereference
      - modernize-use-trailing-return-type
      - modernize-use-std-print
      - readability-magic-numbers
      - readability-uppercase-literal-suffix
      - readability-identifier-length
      - misc-non-private-member-variables-in-classes
    CheckOptions:
      readability-identifier-naming.FunctionCase: lower_case
      readability-identifier-naming.MethodCase: lower_case
      readability-identifier-naming.VariableCase: lower_case
      readability-identifier-naming.TypeCase: CamelCase
      readability-identifier-naming.EnumConstantCase: UPPER_CASE
      # ... 他の命名規則
```

**重要**: `CompileFlags.Add` に `-std=c++23` を追加する理由：
- `compile_commands.json` には `.cc` ファイルのみ含まれる
- ヘッダファイル (`.hh`) はエントリがないため、clangd はフォールバックコマンドを使用
- フォールバックコマンドには C++ 標準が指定されていないため、`consteval` 等でエラーが発生
- `.clangd` の `Add` は全てのファイルに適用されるため、ヘッダでも C++23 が有効になる

### .vscode/settings.json

clangd のコマンドライン引数は `.clangd` ファイルでは設定できません。VSCode の settings.json で設定します：

```json
{
  "clangd.arguments": [
    "--log=error",
    "--clang-tidy",
    "--header-insertion=never",
    "--all-scopes-completion",
    "--query-driver=~/.xmake/packages/c/clang-arm/*/*/bin/clang++,~/.xmake/packages/g/gcc-arm/*/*/bin/arm-none-eabi-g++"
  ]
}
```

**`--query-driver` の役割**: ARM クロスコンパイラのシステムヘッダパスを clangd に教える。glob パターン（`*/*`）を使用することでパッケージ更新後も再設定不要。

---

## 局所オーバーライド

特定のディレクトリに `.clang-tidy` または `.clangd` を配置して、親設定をオーバーライドします：

```
lib/umi/port/mcu/.clangd     # 組み込み特有の例外
lib/umi/port/.clang-tidy     # 組み込み特有のチェック除外
```

### 例: `lib/umi/port/mcu/.clangd`

```yaml
Diagnostics:
  ClangTidy:
    CheckOptions:
      # STM32 レジスタ用の ALL_CAPS 型名
      readability-identifier-naming.TypeIgnoredRegexp: '^[A-Z][A-Z0-9_]*$'
      readability-identifier-naming.ClassIgnoredRegexp: '^[A-Z][A-Z0-9_]*$'
      readability-identifier-naming.StructIgnoredRegexp: '^[A-Z][A-Z0-9_]*$'
      readability-identifier-naming.TypeAliasIgnoredRegexp: '.*_t$|^[A-Z][A-Z0-9_]*$'
```

---

## チェック除外リファレンス

### グローバル除外

| チェック | 理由 |
|-------|--------|
| `bugprone-easily-swappable-parameters` | DSP 関数（lerp、set_adsr）で誤検出が発生する |
| `readability-magic-numbers` | オーディオ定数は多く、文脈依存である |
| `readability-uppercase-literal-suffix` | `1.0f` スタイルを優先する |
| `readability-identifier-length` | DSP で短い名前（x、y、t）が一般的である |
| `modernize-use-trailing-return-type` | プロジェクトのスタイルではない |
| `modernize-use-std-print` | printf は意図的に使用されている |

### 局所除外（`lib/umi/port/.clang-tidy`）

| チェック | 理由 |
|-------|--------|
| `performance-no-int-to-ptr` | レジスタアクセスで `reinterpret_cast<uintptr_t>(addr)` が必要である |
| `readability-identifier-naming.TypeIgnoredRegexp` | STM32 型（GPIO、RCC、DMA）は ALL_CAPS である |

---

## IWYU Pragmas（Include What You Use）

clangd は、インクルード関連の診断に **IWYU（Include What You Use）** プラグマを使用します。

### 使用する場合

**IWYU プラグマは、アンブレラヘッダー**（ユーザー利便性のために他のヘッダーをエクスポートする統合ヘッダー）で許可されます。

```cpp
/// @file mmio.hh
/// @brief UMI メモリマップト I/O ライブラリ - 統合ヘッダー

#pragma once

#include "register.hh"           // IWYU pragma: export
#include "transport/bitbang_i2c.hh" // IWYU pragma: export
#include "transport/bitbang_spi.hh" // IWYU pragma: export
// ...
```

**使用可能なプラグマ：**

| プラグマ | 使用ケース |
|--------|----------|
| `// IWYU pragma: export` | アンブレラヘッダー - インクルードをエクスポートとしてマーク |
| `// IWYU pragma: keep` | 必要だが直接使用されていない特定のインクルード |
| `// IWYU pragma: private; include "public.h"` | 直接インクルードすべきではない内部ヘッダー |

---

## トラブルシューティング

### 「unknown type name 'consteval'」

**原因**: ヘッダファイルに `compile_commands.json` エントリがなく、C++23 が適用されていない。

**解決策**: `.clangd` の `CompileFlags.Add` に `-std=c++23` を追加：
```yaml
CompileFlags:
  Add:
    - "-std=c++23"
```

### 「エラーが多すぎます。停止します」

**原因**: clangd がファイルを解析できない。

**解決策**:
1. `compile_commands.json` を再生成: `xmake project -k compile_commands`
2. clangd を再起動: VSCode で `Cmd+Shift+P` → "clangd: Restart language server"

### 「Unknown argument: '--query-driver=...'」

**原因**: `--query-driver` を `.clangd` の `CompileFlags.Add` に入れている。

**解決策**: `--query-driver` は clangd の CLI 引数であり、コンパイラフラグではない。
`.vscode/settings.json` の `clangd.arguments` に設定する：
```json
{
  "clangd.arguments": [
    "--query-driver=~/.xmake/packages/c/clang-arm/*/*/bin/clang++,~/.xmake/packages/g/gcc-arm/*/*/bin/arm-none-eabi-g++"
  ]
}
```

### 標準ヘッダーが見つからない

**原因**: クロスコンパイラのヘッダーが見つからない。

**解決策**: `--query-driver` で ARM ツールチェーンのパスを指定する。

---

## xmake coding コマンド

```bash
# 設定ファイル生成
xmake coding --init                  # 全ファイル生成
xmake coding --init --clangd         # .clangd + settings.json のみ
xmake coding --init --force          # 既存ファイルを上書き

# コードフォーマット
xmake coding --format                # 全ソースをフォーマット
xmake coding --format -n             # dry-run（変更確認のみ）

# 静的解析
xmake coding --check                 # clang-tidy 実行
xmake coding --check --fix           # 自動修正付き

# 設定確認
xmake coding --info                  # 現在の設定を表示
```

---

## 関連項目

- `CODING_STYLE.md` - 命名規則とスタイル規則
- `CLAUDE.md` - プロジェクトのコーディングガイドライン
- `.refs/arm-embedded-xmake-repo/docs/ARM_EMBEDDED_STATUS.md` - arm-embedded パッケージの状態

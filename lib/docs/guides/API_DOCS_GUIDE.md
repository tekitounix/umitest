# Doxygen 運用ガイド

[Docs Home](../INDEX.md)

## 概要

UMI ライブラリでは Doxygen を使って API リファレンスを自動生成する。
各ライブラリは独自の `Doxyfile` を持ち、ローカル生成と CI 自動生成の両方に対応する。

---

## ローカル生成

### 前提条件

```bash
# macOS
brew install doxygen

# Ubuntu
sudo apt-get install -y doxygen
```

### 生成コマンド

ライブラリディレクトリ内で直接実行:

```bash
cd lib/<libname>
doxygen Doxyfile
open build/doxygen/html/index.html
```

または、モノレポルートから xmake 経由で実行:

```bash
xmake doxygen -P lib/<libname> -o build/doxygen lib/<libname>
```

### 出力先

```
lib/<libname>/build/doxygen/html/index.html
```

`build/` は `.gitignore` で除外されるため、生成物はコミットしない。

---

## Doxyfile 設定方針

### 最小版（Phase 2）

新規ライブラリの初期段階では、10行程度の最小 Doxyfile で十分:

```
PROJECT_NAME           = "<libname>"
PROJECT_NUMBER         = "0.1.0"
OUTPUT_DIRECTORY       = build/doxygen
INPUT                  = README.md include
FILE_PATTERNS          = *.hh *.md
RECURSIVE              = YES
EXTRACT_ALL            = NO
WARN_IF_UNDOCUMENTED   = YES
GENERATE_LATEX         = NO
```

### 拡充版（Phase 3）

品質が成熟したら、以下を追加:

```
# 入力範囲の拡大
INPUT                  = README.md docs include examples tests
EXCLUDE                = build

# 品質ゲート
WARN_IF_INCOMPLETE_DOC = YES

# UMI 統一テーマ
HTML_COLORSTYLE        = AUTO_LIGHT
HTML_COLORSTYLE_HUE    = 220
HTML_COLORSTYLE_SAT    = 100
HTML_COLORSTYLE_GAMMA  = 80

# UX
HTML_CODE_FOLDING      = YES
HTML_COPY_CLIPBOARD    = YES

# コードスタイル
TAB_SIZE               = 4

# graphviz 不要
HAVE_DOT               = NO
SOURCE_BROWSER         = NO
```

### umibench フル版との違い

umibench の Doxyfile はデフォルト値を含む完全版（約3000行）。
他のライブラリでは非デフォルト値のみ明示する差分方式を採用し、可読性を優先する。

---

## CI ワークフロー

Doxygen 生成は `.github/workflows/doxygen.yml` で統合管理されます。
matrix strategy で全ライブラリを一括処理します。

### 構成

- **build ジョブ**: 各ライブラリの Doxyfile から `cd lib/<lib> && doxygen` で HTML を生成
- **deploy ジョブ**: main ブランチへの push 時のみ GitHub Pages にデプロイ

### トリガー

- `push`: main, develop ブランチの `lib/**` パス変更時
- `pull_request`: 同上
- `workflow_dispatch`: 手動実行

### Doxygen コマンド（CI 内部）

```bash
cd lib/<libname> && doxygen
```

> **注**: `xmake doxygen` ではなく、直接 `doxygen` を使用しています。
> 各ライブラリの `Doxyfile` が `OUTPUT_DIRECTORY = build/doxygen` を指定しているため、
> ライブラリディレクトリ内で実行するだけで正しく出力されます。

---

## Doxygen コメント規約

詳細は [API Comment Rule](../standards/API_COMMENT_RULE.md) を参照。最低限のルール:

### ファイルヘッダ

```cpp
// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief 1行の説明。
/// @author Shota Moriguchi @tekitounix
```

### クラス・関数

```cpp
/// @brief 短い説明。
/// @tparam T テンプレートパラメータ。
/// @param name パラメータ説明。
/// @return 戻り値の説明。
```

### 品質ゲート

`WARN_IF_UNDOCUMENTED = YES` により、Doxygen コメントのないpublic API は警告される。
Phase 3 では全警告をゼロにすることを目指す。

---

## トラブルシューティング

### xmake doxygen が失敗する

```
error: xmake doxygen: unknown command
```

xmake の doxygen プラグインは標準で含まれる。`xmake update` で最新版に更新。

### Doxyfile が見つからない

`doxygen` コマンドはカレントディレクトリの `Doxyfile` を読む。
ライブラリディレクトリ内で実行するか、`doxygen lib/<libname>/Doxyfile` で明示指定する。

### HTML が生成されない

`OUTPUT_DIRECTORY` と `INPUT` パスが正しいか確認。
相対パスは Doxyfile の場所からの相対。

---

## Reference

- [Getting Started](GETTING_STARTED.md) — Phase 2 で Doxyfile を作成
- [API Comment Rule](../standards/API_COMMENT_RULE.md) — コメント記法
- [Library Spec §4.2](../standards/LIBRARY_SPEC.md) — CI Doxygen workflow 仕様

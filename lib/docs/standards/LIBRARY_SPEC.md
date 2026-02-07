# UMI ライブラリ標準仕様

**Version**: 2.0.0
**適用対象**: `lib/` 配下の全ライブラリ

---

## 1. 設計原則

| 原則 | 説明 |
|------|------|
| **ソースツリー最小** | ライブラリ固有の情報だけを各ディレクトリに置く |
| **共通事項は集約** | 規約・ガイド・ポリシーは `lib/docs/` に一元化 |
| **リリースは自動注入** | LICENSE, VERSION 等はアーカイブ生成時に自動で含める |
| **バージョンは git タグ** | VERSION ファイルは不要。`git describe` で動的取得 |
| **変更履歴は GitHub** | CHANGELOG ファイルは不要。GitHub Release Notes で自動生成 |

---

## 2. 標準ディレクトリ構造

### 2.1 モノレポルート（集約ファイル）

```
umi/
├── LICENSE                       # MIT (全ライブラリ共通)
├── xmake.lua                     # set_version() は git describe で動的取得
├── release_config.lua            # リリース対象・パッケージ定義
├── tools/release.lua             # xmake release タスク
└── lib/docs/                     # 共通規約・ガイド
    ├── INDEX.md
    ├── Doxyfile.base
    ├── standards/
    │   ├── CODING_RULE.md
    │   ├── API_COMMENT_RULE.md
    │   └── LIBRARY_SPEC.md       # 本ドキュメント
    └── guides/
        ├── GETTING_STARTED.md
        ├── RELEASE_GUIDE.md
        ├── TESTING_GUIDE.md
        ├── BUILD_GUIDE.md
        ├── API_DOCS_GUIDE.md
        ├── CODE_QUALITY_GUIDE.md
        └── DEBUGGING_GUIDE.md
```

### 2.2 各ライブラリ

```
lib/<libname>/
├── README.md                     # [必須] 概要・Quick Start・API・Examples
├── Doxyfile                      # [必須] @INCLUDE base + 固有設定 (PROJECT_NAME/NUMBER)
├── xmake.lua                     # [必須] ビルド定義
│
├── docs/                         # [必須]
│   ├── DESIGN.md                 # [必須] 設計・API仕様・テスト戦略 (統合ドキュメント)
│   └── ja/                       # [任意]
│       └── README.md             # [任意] 日本語 README
│
├── include/<libname>/            # [必須] 公開ヘッダ
│   └── <libname>.hh              # [必須] 統合ヘッダ
│
├── tests/                        # [必須] テスト
│   ├── xmake.lua                 # テストビルド定義
│   ├── test_*.cc                 # テストソース
│   └── compile_fail/             # [任意] コンパイル失敗テスト
│
├── examples/                     # [任意] サンプルコード
│
└── platforms/                    # [条件付き] マルチプラットフォーム時
    ├── host/
    │   └── <libname>/platform.hh
    ├── wasm/
    │   ├── <libname>/platform.hh
    │   └── xmake.lua
    └── arm/cortex-m/<board>/
        ├── <libname>/platform.hh
        ├── xmake.lua
        └── renode/*.resc
```

### 2.3 ライブラリに置かないもの

以下はモノレポルートまたはリリースパイプラインが管理する:

| ファイル | 管理方法 |
|---------|---------|
| `LICENSE` | ルート `/LICENSE` に1つ。アーカイブ生成時に自動コピー |
| `VERSION` | git タグ (`vX.Y.Z`)。アーカイブ生成時に `--ver` から動的生成 |
| `CHANGELOG.md` | GitHub Release Notes (`generate_release_notes: true`) で自動生成 |
| `RELEASE.md` | `lib/docs/guides/RELEASE_GUIDE.md` に集約 |
| `.gitignore` | ルート `.gitignore` でカバー |
| `.github/workflows/` | ルート `.github/workflows/` で統合管理 |

---

## 3. namespace 対応表

| ライブラリ | namespace | include 例 |
|-----------|-----------|-----------|
| `umitest` | `umi::test` | `#include <umitest/test.hh>` |
| `umibench` | `umi::bench` | `#include <umibench/bench.hh>` |
| `umimmio` | `umi::mmio` | `#include <umimmio/mmio.hh>` |
| `umirtm` | `umi::rt` | `#include <umirtm/rtm.hh>` |
| `umiport` | `umi::port` | `#include <umiport/stm32f4/uart_output.hh>` |

命名規則:
- ライブラリ名: `umi` + 機能名（小文字）
- namespace: `umi::` + 機能名
- ディレクトリ/ヘッダ: `lib/<libname>/include/<libname>/`

---

## 4. テンプレート

### 4.1 README.md

```markdown
# <libname>

[日本語](docs/ja/README.md)

<1行の説明>

## Why <libname>

- 特徴1
- 特徴2
- 特徴3

## Quick Start

```cpp
#include <<libname>/<libname>.hh>

int main() {
    // 最小コード例
    return 0;
}
```

## Build and Test

```bash
xmake test
```

## Public API

- Entrypoint: `include/<libname>/<libname>.hh`
- (主要型・関数の簡易リスト)

## Examples

- [`examples/minimal.cc`](examples/minimal.cc) — 最小例

## Documentation

- [Design & API](docs/DESIGN.md)
- [Common Guides](../docs/INDEX.md)
- API docs: `doxygen Doxyfile` → `build/doxygen/html/index.html`

## License

MIT — See [LICENSE](../../LICENSE)
```

### 4.2 docs/DESIGN.md

旧 USAGE.md, TESTING.md, EXAMPLES.md, PLATFORMS.md, INDEX.md の内容を統合する
ライブラリ唯一の設計ドキュメント:

```markdown
# <libname> Design

## 1. Vision

## 2. Non-Negotiable Requirements

## 3. Architecture

ディレクトリ構成、プラットフォーム対応状況を記載。

## 4. Programming Model

使い方の概要、主要パターン。

## 5. API Specification

公開 API の詳細。API Reference Map を含む。

## 6. Test Strategy

テスト環境マトリクス（Host/WASM/ARM）、実行方法、品質ゲート。

## 7. Design Principles
```

### 4.3 Doxyfile

```
@INCLUDE               = ../docs/Doxyfile.base
PROJECT_NAME           = "<libname>"
PROJECT_NUMBER         = "0.0.0-dev"
PROJECT_BRIEF          = "<1行の説明>"
```

`PROJECT_NUMBER` は `xmake release` 実行時に自動更新される。

---

## 5. xmake.lua 標準構造

### 5.1 基本構成

```lua
-- 単体repo判定
local standalone_repo = os.projectdir() == os.scriptdir()

if standalone_repo then
    set_project("<libname>")
    set_xmakever("2.8.0")
    set_languages("c++23")
    add_rules("mode.debug", "mode.release")
    add_rules("plugin.compile_commands.autoupdate", {outputdir = ".", lsp = "clangd"})
    set_warnings("all", "extra", "error")
    add_requires("umitest", {optional = true})
end

-- 依存追加ヘルパー
function <libname>_add_umitest_dep()
    if standalone_repo then
        add_packages("umitest")
    else
        add_deps("umitest")
    end
end

-- 公開ターゲット（ヘッダオンリー）
target("<libname>")
    set_kind("headeronly")
    add_headerfiles("include/(<libname>/**.hh)")
    add_includedirs("include", {public = true})
target_end()

-- ホストプラットフォーム
target("<libname>_host")
    set_kind("headeronly")
    add_deps("<libname>")
target_end()

-- テスト・プラットフォーム
includes("tests")
```

### 5.2 tests/xmake.lua

```lua
target("test_<libname>")
    add_rules("host.test")
    add_tests("default")
    set_default(true)
    set_kind("binary")
    add_files("test_*.cc")
    add_deps("<libname>_host")
    <libname>_add_umitest_dep()
target_end()
```

---

## 6. バージョン管理

### 6.1 統一バージョン

全ライブラリは同一バージョン。git タグ (`vX.Y.Z`) が唯一のバージョンソース。

```lua
-- ルート xmake.lua でのバージョン取得
local function get_version()
    local ok, ver = pcall(os.iorunv, "git", {"describe", "--tags", "--abbrev=0", "--match=v*"})
    if ok and ver then
        return ver:match("v(.+)") or "0.0.0-dev"
    end
    return "0.0.0-dev"
end
set_version(get_version())
```

### 6.2 リリースフロー

```bash
xmake release --ver=X.Y.Z
```

リリースタスクが自動で行うこと:
1. テスト実行
2. ルート xmake.lua の `set_version()` 更新
3. 各ライブラリの Doxyfile `PROJECT_NUMBER` 更新
4. アーカイブ生成（LICENSE, VERSION はルートから自動注入）
5. xmake-repo パッケージ定義の自動更新
6. git commit + tag (`vX.Y.Z`)

詳細は [Release Guide](../guides/RELEASE_GUIDE.md) を参照。

---

## 7. CI/CD

モノレポでは統合ワークフローで全ライブラリをまとめて処理する:

| ワークフロー | 内容 |
|-------------|------|
| `ci.yml` | テスト・ビルド（host / WASM / ARM matrix） |
| `doxygen.yml` | Doxygen 生成・GitHub Pages デプロイ（matrix） |
| `release.yml` | タグトリガーによる自動リリース |

---

## 8. 著作権・著者表記規約

### LICENSE ファイル（権利帰属先）

```
Copyright (c) <year> SYNTHERNET (@tekitounix)
```

### ソースファイル（短縮形）

```cpp
// SPDX-License-Identifier: MIT
// Copyright (c) <year>, tekitounix
/// @file
/// @brief 1行の説明。
/// @author Shota Moriguchi @tekitounix
```

| 表記場所 | 記載内容 | 役割 |
|---------|---------|------|
| LICENSE | `SYNTHERNET (@tekitounix)` | 権利帰属先 |
| ソース先頭 | `tekitounix` | 著作権短縮 |
| `@author` | `Shota Moriguchi @tekitounix` | コード寄与 |

---

## 9. パッケージ化

`release_config.lua` でライブラリごとのパッケージ設定を定義。
`xmake release` が xmake-repo パッケージ定義を自動生成する。

詳細は [Release Guide](../guides/RELEASE_GUIDE.md) を参照。

---

## 10. 参照

- [umibench](../umibench/) — リファレンス実装
- [umitest](../umitest/) — テストフレームワーク
- [umimmio](../umimmio/) — MMIO 抽象
- [umirtm](../umirtm/) — RTT 互換デバッグモニタ
- [umiport](../umiport/) — プラットフォーム共有インフラ (WIP)
- [Coding Rule](CODING_RULE.md) — コーディング規約
- [API Comment Rule](API_COMMENT_RULE.md) — API コメント規約
- [Getting Started](../guides/GETTING_STARTED.md) — 新規ライブラリ作成ガイド

---

*Last updated: 2026-02-07*

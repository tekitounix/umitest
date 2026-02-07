# Getting Started — 新規ライブラリ段階的作成ガイド

[Docs Home](../INDEX.md)

## 方針

umibench をそのままコピーするのではなく、**段階的に育てる**アプローチを取る。
各 Phase 終了時点でビルド・テスト可能な状態を維持する。

全 Phase 完了時の最終形は [Library Standard](../standards/LIBRARY_SPEC.md) を参照。

---

## Phase 1: Minimum Viable Library

**目標**: `xmake test` が通る最小構成。

### 1.1 ディレクトリ作成

```
lib/<libname>/
├── README.md
├── LICENSE
├── VERSION
├── xmake.lua
├── include/<libname>/
│   └── <libname>.hh       # 統合ヘッダ（最小でも1つ）
└── tests/
    ├── xmake.lua
    └── test_<libname>.cc   # テスト1件以上
```

### 1.2 LICENSE

```
MIT License

Copyright (c) <year> SYNTHERNET (@tekitounix)

Permission is hereby granted, free of charge, to any person obtaining a copy
...
```

著作権表記規約の詳細は [Library Standard §5.5](../standards/LIBRARY_SPEC.md) を参照。

### 1.3 VERSION

```
0.1.0
```

### 1.4 xmake.lua（最小版）

standalone_repo 判定パターンが重要。モノレポ内とスタンドアロンの両方で動作する。

```lua
-- standalone_repo 判定: lib/<libname>/ 単体で動作させるかどうか
local standalone_repo = os.projectdir() == os.scriptdir()
<LIBNAME>_STANDALONE_REPO = standalone_repo

if standalone_repo then
    set_project("<libname>")
    set_version("0.1.0")
    set_xmakever("2.8.0")
    set_languages("c++23")
    add_rules("mode.debug", "mode.release")
    add_rules("plugin.compile_commands.autoupdate", {outputdir = ".", lsp = "clangd"})
    set_warnings("all", "extra", "error")

    -- 依存パッケージ（単体repo時のみ）
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

-- 公開ターゲット: ヘッダオンリー
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

-- テスト
includes("tests")
```

### 1.5 tests/xmake.lua（最小版）

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

### 1.6 最小テスト

```cpp
// SPDX-License-Identifier: MIT
// Copyright (c) <year>, tekitounix
#include <umitest/test.hh>
#include <<libname>/<libname>.hh>

int main() {
    umi::test::Suite s("<libname>");

    s.run("smoke test", [](auto& ctx) -> bool {
        ctx.assert_true(true, "library loads");
        return true;
    });

    return s.summary();
}
```

### 1.7 README.md（最小版）

```markdown
# <libname>

<1行の説明>

## Build and Test

    xmake test

## License

MIT — See [LICENSE](LICENSE)
```

### Phase 1 検証

```bash
xmake test    # test_<libname> がパスすること
```

---

## Phase 2: Documentation

**目標**: 設計意図が文書化され、他の開発者が理解できる状態。

### 追加するファイル

```
lib/<libname>/
├── README.en.md              # 英語版（READMEが日本語なら）
├── Doxyfile                  # 最小 Doxygen 設定
├── .gitignore
├── docs/
│   ├── INDEX.md              # ドキュメント入口
│   ├── DESIGN.md             # 設計思想（8章構成）
│   └── TESTING.md            # テスト戦略
```

### Doxyfile（最小版）

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

Doxygen 運用の詳細は [API Docs ガイド](API_DOCS_GUIDE.md) を参照。

### .gitignore

> **モノレポ注記**: umi モノレポ内ではルートの `.gitignore` がカバーするため不要。
> standalone リポジトリとして切り出す場合のみ作成する。

```
build/
.xmake/
compile_commands.json
```

### docs/INDEX.md テンプレート

```markdown
# <libname> Documentation

## Read in This Order

1. [Design](DESIGN.md)
2. [Testing](TESTING.md)

## API Reference Map

- Public entrypoint: `include/<libname>/<libname>.hh`

## Local Doxygen Generation

    cd lib/<libname>
    doxygen Doxyfile
    open build/doxygen/html/index.html
```

### docs/DESIGN.md テンプレート

```markdown
# <libname> Design

## 1. Vision
## 2. Non-Negotiable Requirements
## 3. Current Layout
## 4. Growth Layout
## 5. Programming Model
## 6. API Specification
## 7. Test Strategy
## 8. Design Principles
```

全章を埋める必要はない。最低限 §1 Vision と §2 Requirements を書く。

### docs/TESTING.md テンプレート

```markdown
# <libname> Test Strategy

## Test Environment

| 環境 | ステータス |
|------|-----------|
| Host (macOS/Linux) | Active |
| WASM (Emscripten + Node) | — |
| ARM build | — |
| ARM execution (Renode) | — |

## Running Tests

    xmake test

## Quality Gates

- All tests pass on host
- No compiler warnings (-Wall -Wextra -Werror)
```

### README.md 拡充

Phase 1 の最小 README に以下を追加:

- Release Status セクション
- Why セクション（特徴 3 点）
- Quick Start コード例
- Documentation リンク
- Public Headers 一覧

テンプレートは [Library Standard §5.1](../standards/LIBRARY_SPEC.md) を参照。

### Phase 2 検証

```bash
xmake test                # テスト通過確認
doxygen Doxyfile          # Doxygen 生成エラーなし
```

---

## Phase 3: Quality

**目標**: テストカバレッジ充実、ドキュメント完成、Doxygen コメント網羅。

### 追加・拡充するファイル

```
lib/<libname>/
├── examples/
│   ├── example_basic.cc      # 基本使用例
│   ├── example_advanced.cc   # 応用例
│   └── example_edge.cc       # エッジケース
├── tests/
│   └── compile_fail/         # コンパイル失敗テスト
│       └── fail_*.cc
├── docs/
│   ├── GETTING_STARTED.md    # 入門ガイド
│   ├── USAGE.md              # 詳細使用法
│   ├── EXAMPLES.md           # サンプル解説
│   └── ja/                   # 日本語版
│       ├── README.md
│       ├── INDEX.md
│       └── ...
```

### ヘッダの Doxygen コメント充実

```cpp
/// @file
/// @brief 1行の説明。
/// @author Shota Moriguchi @tekitounix

/// @brief クラス/関数の説明。
/// @tparam T テンプレートパラメータ説明。
/// @param name パラメータ説明。
/// @return 戻り値の説明。
/// @warning 注意事項。
/// @note 補足情報。
/// @pre 事前条件。
```

スタイル詳細は [API Comment Rule](../standards/API_COMMENT_RULE.md) を参照。

### compile-fail テスト追加

`tests/xmake.lua` に以下を追加:

```lua
target("test_<libname>_compile_fail")
    set_kind("phony")
    set_default(false)
    add_tests("<test_name>")
    on_test(function()
        -- Library Standard §3.2 参照
    end)
target_end()
```

### Doxyfile 拡充

最小版から以下を追加:

```
INPUT                  = README.md docs include examples tests
WARN_IF_INCOMPLETE_DOC = YES
HTML_COLORSTYLE        = AUTO_LIGHT
HTML_COLORSTYLE_HUE    = 220
HTML_COLORSTYLE_SAT    = 100
HTML_COLORSTYLE_GAMMA  = 80
HTML_CODE_FOLDING      = YES
HTML_COPY_CLIPBOARD    = YES
EXCLUDE                = build
TAB_SIZE               = 4
HAVE_DOT               = NO
```

### Phase 3 検証

```bash
xmake test                          # 全テスト通過
xmake test "test_<libname>_compile_fail/*"  # compile-fail テスト通過
doxygen Doxyfile                    # 警告ゼロ
```

---

## Phase 4: Release Ready

**目標**: CI 完備、パッケージ登録済み、単体リポジトリとして公開可能。

### 追加するファイル

```
lib/<libname>/
├── CHANGELOG.md
├── RELEASE.md
├── .github/                       # standalone時のみ（モノレポではルートCIがカバー）
│   └── workflows/
│       ├── <libname>-ci.yml
│       └── <libname>-doxygen.yml
├── platforms/                      # マルチプラットフォーム時
│   ├── host/<libname>/platform.hh
│   ├── wasm/
│   │   ├── <libname>/platform.hh
│   │   └── xmake.lua
│   └── arm/cortex-m/<board>/
│       ├── <libname>/platform.hh
│       └── xmake.lua
```

### CHANGELOG.md

```markdown
# Changelog

## [Unreleased]

## [0.1.0] - <date>

### Added
- 初期リリース
```

### RELEASE.md

```markdown
# Release Policy

## Current Release Line

| Version | Status | Date |
|---------|--------|------|
| 0.1.0   | beta   | <date> |

## Versioning

Semantic Versioning (SemVer). 0.x はベータ: マイナーバージョンで破壊的変更あり。

## Release Checklist

1. VERSION 更新
2. CHANGELOG.md 更新
3. `xmake test` 全テスト通過
4. git tag v<version>
```

### CI ワークフロー

テンプレートは [Library Standard §4](../standards/LIBRARY_SPEC.md) を参照。

### arm-embedded-xmake-repo パッケージ登録

テンプレートは [Library Standard §6](../standards/LIBRARY_SPEC.md) を参照。

### Phase 4 検証

```bash
xmake test                          # 全テスト通過（Host/WASM/ARM）
# CI: GitHub Actions 全ジョブ green
```

---

## Phase 対応表

各ファイルがどの Phase で必要になるかの一覧:

| ファイル | Phase 1 | Phase 2 | Phase 3 | Phase 4 |
|---------|:-------:|:-------:|:-------:|:-------:|
| `README.md` | **必須** (最小) | 拡充 | — | — |
| `LICENSE` | **必須** | — | — | — |
| `VERSION` | **必須** | — | — | — |
| `xmake.lua` | **必須** | — | — | 拡充 |
| `include/<lib>/` | **必須** | — | Doxygen充実 | — |
| `tests/` | **必須** (1件) | — | 充実 | — |
| `Doxyfile` | — | **最小** | 拡充 | — |
| `.gitignore` | — | standalone時 | — | — |
| `docs/INDEX.md` | — | **必須** | — | — |
| `docs/DESIGN.md` | — | **必須** (§1-2) | 全章 | — |
| `docs/TESTING.md` | — | **必須** | 拡充 | — |
| `README.en.md` | — | 推奨 | — | — |
| `examples/` | — | — | **3件+** | — |
| `compile_fail/` | — | — | 推奨 | — |
| `docs/ja/` | — | — | 推奨 | — |
| `docs/USAGE.md` | — | — | 推奨 | — |
| `CHANGELOG.md` | — | — | — | **必須** |
| `RELEASE.md` | — | — | — | **必須** |
| `.github/workflows/` | — | — | — | standalone時 |
| `platforms/` | — | — | — | 条件付き |

---

## Reference

- [Library Spec](../standards/LIBRARY_SPEC.md) — 完全仕様
- [Coding Rule](../standards/CODING_RULE.md) — コーディング規約
- [API Comment Rule](../standards/API_COMMENT_RULE.md) — API コメント規約
- [API Docs Guide](API_DOCS_GUIDE.md) — API ドキュメント生成・運用
- [umibench](../../umibench/) — リファレンス実装（Phase 4 完了済み）

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
├── xmake.lua
├── include/<libname>/
│   └── <libname>.hh       # 統合ヘッダ（最小でも1つ）
└── tests/
    ├── xmake.lua
    └── test_<libname>.cc   # テスト1件以上
```

> **NOTE**: LICENSE, VERSION, CHANGELOG.md は per-lib に置かない。
> ルート `/LICENSE` と git タグで一元管理する。
> 詳細は [Library Standard §2.3](../standards/LIBRARY_SPEC.md) 参照。

### 1.2 xmake.lua（最小版）

standalone_repo 判定パターンが重要。モノレポ内とスタンドアロンの両方で動作する。

```lua
-- standalone_repo 判定: lib/<libname>/ 単体で動作させるかどうか
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

### 1.3 tests/xmake.lua（最小版）

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

### 1.4 最小テスト

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

### 1.5 README.md（最小版）

```markdown
# <libname>

<1行の説明>

## Build and Test

    xmake test

## License

MIT — See [LICENSE](../../../LICENSE)
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
├── Doxyfile                  # @INCLUDE base + 固有設定
└── docs/
    └── DESIGN.md             # 設計・API仕様・テスト戦略（統合ドキュメント）
```

### Doxyfile

```
@INCLUDE               = ../docs/Doxyfile.base
PROJECT_NAME           = "<libname>"
PROJECT_NUMBER         = "0.0.0-dev"
PROJECT_BRIEF          = "<1行の説明>"
```

`PROJECT_NUMBER` は `xmake release` 実行時に自動更新される。
Doxygen 運用の詳細は [API Docs ガイド](API_DOCS_GUIDE.md) を参照。

### docs/DESIGN.md テンプレート

旧来の INDEX.md, TESTING.md, USAGE.md, EXAMPLES.md, PLATFORMS.md を統合する
ライブラリ唯一の設計ドキュメント:

```markdown
# <libname> Design

## 1. Vision
## 2. Non-Negotiable Requirements
## 3. Architecture
## 4. Programming Model
## 5. API Specification
## 6. Test Strategy
## 7. Design Principles
```

全章を埋める必要はない。最低限 §1 Vision と §2 Requirements を書く。

### README.md 拡充

Phase 1 の最小 README を以下のテンプレートに合わせて拡充:

```markdown
# <libname>

[日本語](docs/ja/README.md)

<1行の説明>

## Why <libname>

- 特徴1
- 特徴2
- 特徴3

## Quick Start

## Build and Test

    xmake test

## Public API

- Entrypoint: `include/<libname>/<libname>.hh`

## Examples

## Documentation

- [Design & API](docs/DESIGN.md)
- [Common Guides](../docs/INDEX.md)
- API docs: `doxygen Doxyfile` → `build/doxygen/html/index.html`

## License

MIT — See [LICENSE](../../../LICENSE)
```

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
└── docs/
    ├── DESIGN.md             # 全章を充実
    └── ja/                   # 日本語版
        └── README.md
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
        -- Library Standard §5.2 参照
    end)
target_end()
```

### Phase 3 検証

```bash
xmake test                          # 全テスト通過
xmake test "test_<libname>_compile_fail/*"  # compile-fail テスト通過
doxygen Doxyfile                    # 警告ゼロ
```

---

## Phase 4: Release Ready

**目標**: CI 完備、パッケージ登録済み。

### 追加するファイル

```
lib/<libname>/
└── platforms/                      # マルチプラットフォーム時
    ├── host/<libname>/platform.hh
    ├── wasm/
    │   ├── <libname>/platform.hh
    │   └── xmake.lua
    └── arm/cortex-m/<board>/
        ├── <libname>/platform.hh
        └── xmake.lua
```

### release_config.lua 登録

ルートの `release_config.lua` にライブラリを追加:

```lua
-- libs セクション
<libname> = {
    publish = true,
    headeronly = true
},

-- packages セクション
<libname> = {
    description = "...",
    main_header = "<libname>/<libname>.hh"
},
```

### Phase 4 検証

```bash
xmake test                          # 全テスト通過（Host/WASM/ARM）
xmake release --ver=X.Y.Z --dry-run  # リリース dry-run 正常
# CI: GitHub Actions 全ジョブ green
```

---

## Phase 対応表

各ファイルがどの Phase で必要になるかの一覧:

| ファイル | Phase 1 | Phase 2 | Phase 3 | Phase 4 |
|---------|:-------:|:-------:|:-------:|:-------:|
| `README.md` | **必須** (最小) | 拡充 | — | — |
| `xmake.lua` | **必須** | — | — | 拡充 |
| `include/<lib>/` | **必須** | — | Doxygen充実 | — |
| `tests/` | **必須** (1件) | — | 充実 | — |
| `Doxyfile` | — | **必須** | — | — |
| `docs/DESIGN.md` | — | **必須** (§1-2) | 全章 | — |
| `examples/` | — | — | **3件+** | — |
| `compile_fail/` | — | — | 推奨 | — |
| `docs/ja/README.md` | — | — | 推奨 | — |
| `platforms/` | — | — | — | 条件付き |
| `release_config.lua` | — | — | — | 登録 |

> 以下はモノレポルートまたはリリースパイプラインが管理するため、**per-lib に置かない**:
> LICENSE, VERSION, CHANGELOG.md, RELEASE.md, .gitignore, .github/workflows/

---

## Reference

- [Library Spec](../standards/LIBRARY_SPEC.md) — 完全仕様
- [Coding Rule](../standards/CODING_RULE.md) — コーディング規約
- [API Comment Rule](../standards/API_COMMENT_RULE.md) — API コメント規約
- [API Docs Guide](API_DOCS_GUIDE.md) — API ドキュメント生成・運用
- [umibench](../../umibench/) — リファレンス実装

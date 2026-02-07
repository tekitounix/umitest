# UMI ライブラリ標準仕様

**Version**: 1.0.0-draft  
**基準ライブラリ**: `umibench` (v0.1.0)  
**適用対象**: `lib/umitest`, `lib/umimmio`, `lib/umirtm`, `lib/umiport`, 今後の全ライブラリ

---

## 1. 目的と範囲

このドキュメントは、UMI エコシステム内の全ライブラリに適用される標準構造と実装パターンを定義します。

`umibench` を完成されたリファレンス実装とし、以下を統一します：
- ディレクトリ構造
- xmake.lua 構成
- ドキュメント構成
- CI/CD パイプライン
- パッケージ化手順
- 単体リポジトリ化対応

---

## 2. 標準ディレクトリ構造

```
lib/<libname>/                    # または単体repoのルート
├── README.md                     # [必須] 英語版メイン
├── LICENSE                       # [必須] MIT推奨
├── VERSION                       # [必須] セマンティックバージョン
├── CHANGELOG.md                  # [必須] 変更履歴
├── RELEASE.md                    # [必須] リリースポリシー
├── Doxyfile                      # [必須] Doxygen設定
├── xmake.lua                     # [必須] ビルド定義
├── .gitignore                    # [standalone時のみ] Git除外設定
├── .clang-format                 # [推奨] フォーマット設定
├── .clang-tidy                   # [推奨] 静的解析設定
├── .clangd                       # [推奨] LSP設定
│
├── docs/                         # [必須] ドキュメント
│   ├── INDEX.md                  # [必須] ドキュメント入口
│   ├── DESIGN.md                 # [必須] 設計思想・仕様
│   ├── GETTING_STARTED.md        # [推奨] 入門ガイド
│   ├── USAGE.md                  # [推奨] 詳細使用法
│   ├── EXAMPLES.md               # [推奨] サンプル集
│   ├── TESTING.md                # [必須] テスト戦略
│   ├── PLATFORMS.md              # [条件必須] マルチプラットフォーム時
│   └── ja/                       # [推奨] 日本語版
│       ├── README.md             # [推奨] 日本語README
│       ├── INDEX.md              # [推奨] 日本語ドキュメント入口
│       └── ...                   # 各種日本語版ドキュメント
│
├── include/<libname>/            # [必須] 公開ヘッダ
│   ├── <libname>.hh              # [必須] 統合ヘッダ
│   ├── <feature>/                # [任意] 機能別サブディレクトリ
│   └── ...
│
├── tests/                        # [必須] テスト
│   ├── xmake.lua                 # [必須] テストビルド定義
│   ├── test_*.cc                 # [必須] テストソース
│   ├── test_fixture.hh           # [任意] テスト用fixture
│   └── compile_fail/             # [任意] コンパイル失敗テスト
│       └── *.cc
│
├── examples/                     # [任意] サンプルコード
│   └── *.cc
│
├── platforms/                    # [条件必須] マルチプラットフォーム時
│   ├── host/                     # [条件必須] ホストプラットフォーム
│   │   └── <libname>/
│   │       └── platform.hh       # 論理パス解決用
│   ├── wasm/                     # [任意] WASMプラットフォーム
│   │   ├── <libname>/
│   │   │   └── platform.hh
│   │   └── xmake.lua
│   └── arm/                      # [任意] ARMプラットフォーム
│       └── cortex-m/
│           └── <board>/
│               ├── startup.cc
│               ├── syscalls.cc
│               ├── linker.ld
│               ├── <libname>/
│               │   └── platform.hh
│               ├── xmake.lua
│               └── renode/
│                   └── *.resc
│
└── .github/                          # [standalone時のみ] モノレポではルートCIがカバー
    └── workflows/
        ├── <libname>-ci.yml      # CIワークフロー
        └── <libname>-doxygen.yml # ドキュメント自動生成
```

### 2.1 namespace 対応表

| ライブラリ | namespace | include例 |
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

> **注**: umirtm は歴史的経緯で `namespace rt {` を使用しているコードがある。新規コードでは `umi::rt` を使用すること。

---

## 3. xmake.lua 標準構造

### 3.1 基本構成

```lua
-- 単体repo判定
local standalone_repo = os.projectdir() == os.scriptdir()
<LIBNAME>_STANDALONE_REPO = standalone_repo

if standalone_repo then
    -- 単体repo時の設定
    set_project("<libname>")
    set_version("x.y.z")
    set_xmakever("2.8.0")

    set_languages("c++23")
    add_rules("mode.debug", "mode.release")
    add_rules("plugin.compile_commands.autoupdate", {outputdir = ".", lsp = "clangd"})
    set_warnings("all", "extra", "error")

    -- 依存関係（単体repo時）
    add_requires("arm-embedded", {optional = true})
    add_requires("<依存lib>", {optional = true})
    add_requires("umitest", {optional = true})  -- test-only
end

-- 依存追加ヘルパー関数
function <libname>_add_<dep>_dep()
    if standalone_repo then
        add_packages("<dep>")
    else
        add_deps("<dep>")
    end
end

-- 公開ターゲット

-- 1. 共通コア（必須）
target("<libname>_common")
    set_kind("headeronly")
    add_headerfiles("include/(<libname>/**.hh)")
    add_includedirs("include", { public = true })
target_end()

-- 2. ホストプラットフォーム（必須）
target("<libname>_host")
    set_kind("headeronly")
    add_deps("<libname>_common")
    add_defines("<LIBNAME>_HOST")
    add_includedirs("platforms/host", { public = true })
target_end()

-- 3. WASMプラットフォーム（任意）
target("<libname>_wasm_platform")
    set_kind("headeronly")
    add_deps("<libname>_common")
    add_defines("<LIBNAME>_WASM")
    add_includedirs("platforms/wasm", { public = true })
target_end()

-- 4. 組み込みプラットフォーム（任意）
target("<libname>_embedded")
    set_kind("headeronly")
    add_deps("<libname>_common")
    <libname>_add_<dep>_dep()  -- umimmioなど
    add_defines("<LIBNAME>_EMBEDDED")
target_end()

-- サブディレクトリインクルード
includes("tests")
includes("platforms/arm/cortex-m/<board>")  -- 条件付き
includes("platforms/wasm")                   -- 条件付き
```

### 3.2 tests/xmake.lua 標準

```lua
-- Hostテスト
target("test_<libname>")
    add_rules("host.test")
    add_tests("default")
    set_default(true)
    set_kind("binary")
    add_files("test_*.cc")
    add_deps("<libname>_host")
    <libname>_add_umitest_dep()
target_end()

-- Compile-failテスト（必要時）
target("test_<libname>_compile_fail")
    set_kind("phony")
    set_default(false)
    add_tests("<test_name>")

    on_test(function()
        import("lib.detect.find_tool")
        local cxx = find_tool("c++") or find_tool("g++") or find_tool("clang++")
        assert(cxx and cxx.program, "no host C++ compiler found")

        local source = path.join(os.scriptdir(), "compile_fail", "<file>.cc")
        local include_dir = path.join(os.scriptdir(), "..", "include")
        local object = os.tmpfile() .. ".o"

        local ok = false
        try {
            function()
                os.iorunv(cxx.program, {"-std=c++23", "-I" .. include_dir, "-c", source, "-o", object})
                ok = true
            end
        }
        os.tryrm(object)

        if ok then
            raise("compile-fail test failed: compiled successfully")
        end
        return true
    end)
target_end()
```

### 3.3 platforms/wasm/xmake.lua 標準

```lua
local has_emscripten = os.getenv("EMSDK") ~= nil
    or os.isfile("/opt/homebrew/bin/emcc")
    or os.isfile("/usr/local/bin/emcc")
    or os.isfile("/usr/bin/emcc")

if has_emscripten then
target("<libname>_wasm")
    set_kind("binary")
    set_default(false)
    set_group("wasm")
    set_languages("c++23")
    set_plat("wasm")
    set_arch("wasm32")
    set_toolchains("emcc")
    set_filename("<libname>_wasm.js")
    add_tests("default")

    add_files("../../tests/test_*.cc")
    add_deps("<libname>_wasm_platform", "umitest")

    add_cxflags("-fno-exceptions", "-fno-rtti", {force = true})
    add_ldflags("-sEXPORTED_FUNCTIONS=['_main']", {force = true})

    on_run(function(target)
        local node = "node"
        -- パス検索ロジック...
        os.execv(node, {target:targetfile()})
    end)

    on_test(function(target)
        -- on_runと同様の処理
        return true
    end)
target_end()
end
```

### 3.4 platforms/arm/cortex-m/<board>/xmake.lua 標準

```lua
target("<libname>_<board>_renode")
    set_kind("binary")
    set_default(false)
    add_rules("embedded")
    
    set_values("embedded.mcu", "<mcu>")           -- 例: stm32f407vg
    set_values("embedded.optimize", "size")
    set_values("embedded.toolchain", "clang-arm")
    
    add_files("startup.cc")
    add_files("syscalls.cc")
    add_files("../../../../tests/test_*.cc")
    
    set_values("embedded.linker_script", path.join(os.scriptdir(), "linker.ld"))
    
    add_deps("<libname>_embedded")
    add_deps("umitest")
    
    add_includedirs("..", {public = false})
    add_includedirs(os.scriptdir(), {public = false})
    
    on_run(function(target)
        -- Renode実行設定
    end)
target_end()

-- GCC版も同様に定義
target("<libname>_<board>_renode_gcc")
    -- ...
target_end()
```

---

## 4. CI/CD 標準

### 4.1 モノレポ統合 CI（現行構成）

モノレポでは統合ワークフローで全ライブラリをまとめて処理する:

- `.github/workflows/ci.yml` — テスト・ビルド（host / WASM / ARM）
- `.github/workflows/doxygen.yml` — Doxygen 生成・デプロイ（matrix strategy）
- `.github/workflows/release.yml` — タグトリガーによる自動リリース

#### 必須ジョブ

| ジョブ | 内容 |
|--------|------|
| **host-tests** | `xmake test`（ubuntu + macos matrix） |
| **wasm-tests** | Emscripten + Node.js で `xmake test "*_wasm/*"` |
| **arm-build** | GCC クロスビルド確認 |

#### 手動ジョブ

| ジョブ | トリガー |
|--------|---------|
| **renode-smoke** | `workflow_dispatch` + `run_renode=true` |

### 4.2 standalone 用テンプレート（切り出し時）

standalone リポジトリとして切り出す場合は、以下のテンプレートを参考に
`<libname>/.github/workflows/` に配置する:

**CI テスト**: `xmake test` で host テストを実行。
**Doxygen**: `cd lib/<libname> && doxygen` で生成し GitHub Pages にデプロイ。

詳細は実際のモノレポ統合 CI (`.github/workflows/ci.yml`, `doxygen.yml`) を参照。
```

---

## 5. ドキュメント標準

### 5.1 README.md 標準セクション

```markdown
# <libname>

English | [日本語](docs/ja/README.md)

## Release Status

- Current version: `x.y.z`
- Stability: [stable|beta|alpha]
- Versioning policy: [RELEASE.md](RELEASE.md)
- Changelog: [CHANGELOG.md](CHANGELOG.md)

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

## Documentation

- Documentation index: [docs/INDEX.md](docs/INDEX.md)
- Getting started: [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md)
- Detailed usage: [docs/USAGE.md](docs/USAGE.md)
- Testing: [docs/TESTING.md](docs/TESTING.md)
- Design: [docs/DESIGN.md](docs/DESIGN.md)

## License

MIT — Copyright (c) <year> SYNTHERNET (@tekitounix)

See [LICENSE](LICENSE) for details.
```

### 5.2 docs/INDEX.md 標準構造

```markdown
# <libname> Documentation

[日本語](ja/INDEX.md)

## Read in This Order

1. [Getting Started](GETTING_STARTED.md)
2. [Usage](USAGE.md)
3. [Platforms](PLATFORMS.md)  -- マルチプラットフォーム時
4. [Examples](EXAMPLES.md)
5. [Testing](TESTING.md)
6. [Design](DESIGN.md)

## API Reference Map

- Public entrypoint: `include/<libname>/<libname>.hh`
- Core API:
  - `include/<libname>/...`

## Local Generation

```bash
xmake doxygen -P . -o build/doxygen .
```

Generated entrypoint:

- `build/doxygen/html/index.html`

## Release Metadata

- Version file: `VERSION`
- Changelog: `CHANGELOG.md`
- Release policy: `RELEASE.md`
```

### 5.3 docs/TESTING.md 標準構造

umibench/docs/TESTING.md を参照し、以下を含める：

- テスト環境のマトリクス（Host/WASM/ARMビルド/ARM実行）
- テスト実行コマンド
- テストフレームワーク（umitest）の使用方法
- CIで自動化されるものと手動のものの区別

### 5.4 docs/DESIGN.md 標準構造

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

### 5.5 著作権・著者表記規約

UMIプロジェクトでは、**権利帰属先**（copyright holder）と**コード寄与者**（author）を明確に区別する。

#### LICENSE ファイル（権利帰属先）

```
Copyright (c) <year> SYNTHERNET (@tekitounix)
```

- **SYNTHERNET** は屋号（trade name）。法的な権利帰属先として使用
- 年号は最終更新年（例: `2026`）
- ライセンス種別は MIT を標準とする

#### ソースファイル著作権コメント（短縮形）

```cpp
// SPDX-License-Identifier: MIT
// Copyright (c) <year>, tekitounix
```

- ファイル先頭に記載（Doxygen `@file` ブロックの前）
- ハンドル名 `tekitounix` を使用（簡潔な識別のため）

#### @author Doxygen タグ（コード寄与者）

```cpp
/// @author Shota Moriguchi @tekitounix
```

- 個人名 + ハンドル名で記載
- `@author` は**そのコードを書いた個人**を示す（複数可）
- 権利帰属先（SYNTHERNET）とは役割が異なる

#### 使い分けの指針

| 表記場所       | 記載内容                              | 役割       |
| -------------- | ------------------------------------- | ---------- |
| LICENSE        | `SYNTHERNET (@tekitounix)`            | 権利帰属先 |
| ソース先頭     | `tekitounix`                          | 著作権短縮 |
| `@author` タグ | `Shota Moriguchi @tekitounix`         | コード寄与 |
| README License | `MIT ([LICENSE](LICENSE))` のみ       | 参照       |

---

## 6. パッケージ化標準

### 6.1 arm-embedded-xmake-repo への追加

`.refs/arm-embedded-xmake-repo/packages/u/<libname>/xmake.lua`:

```lua
package("<libname>")
    set_homepage("https://github.com/tekitounix/<libname>")
    set_description("UMI <description>")
    set_license("MIT")
    
    set_kind("library", {headeronly = true})
    
    -- バージョン定義
    add_versions("dev", "git:../../lib/<libname>")  -- 開発時
    -- add_versions("1.0.0", "https://github.com/.../archive/v1.0.0.tar.gz")
    
    -- 設定オプション
    add_configs("backend", {
        description = "Target backend",
        default = "host",
        values = {"host", "wasm", "embedded"}
    })
    
    -- 依存関係
    on_load(function(package)
        if package:config("backend") == "embedded" then
            package:add("deps", "arm-embedded")
            package:add("deps", "<依存lib>")
        end
    end)
    
    on_install(function(package)
        os.cp("include", package:installdir())
    end)
    
    on_test(function(package)
        -- インストール検証テスト
    end)
package_end()
```

### 6.2 単体repo時の依存解決

単体repoでは以下の優先順位で依存を解決：

1. ローカル xmake repo（開発時）
2. GitHub 経由の xmake repo（CI/ユーザー環境）

```lua
-- xmake.lua 内
if standalone_repo then
    -- ローカルrepoがあれば使用
    local local_repo = path.join(os.scriptdir(), "..", ".refs", "arm-embedded-xmake-repo")
    if os.isdir(local_repo) then
        add_repositories("arm-embedded " .. local_repo)
    else
        add_repositories("arm-embedded https://github.com/tekitounix/arm-embedded-xmake-repo.git")
    end
    
    add_requires("<libname>")
end
```

---

## 7. バージョン管理標準

### 7.1 VERSION ファイル

セマンティックバージョンのみを記載：

```
0.1.0
```

### 7.2 CHANGELOG.md 標準フォーマット

```markdown
# Changelog

## [Unreleased]

### Added
- 新機能

### Changed
- 変更

### Fixed
- バグ修正

## [0.1.0] - 2026-02-07

### Added
- 初期ベータリリース
- Host/WASM/Embedded サポート
```

### 7.3 RELEASE.md 標準内容

umibench/RELEASE.md を参照：

- Current Release Line
- Versioning Rules (SemVer)
- Changelog Rules
- Release Checklist
- CI Scope

---

## 8. 実装チェックリスト

### 8.1 新規ライブラリ作成時

- [ ] ディレクトリ構造作成（第2章準拠）
- [ ] xmake.lua 作成（第3章準拠）
- [ ] tests/xmake.lua 作成（第3.2章準拠）
- [ ] README.md 作成（第5.1章準拠）
- [ ] docs/INDEX.md 作成（第5.2章準拠）
- [ ] docs/DESIGN.md 作成（第5.4章準拠）
- [ ] docs/TESTING.md 作成（第5.3章準拠）
- [ ] .github/workflows/<libname>-ci.yml 作成（第4.1章準拠）
- [ ] arm-embedded-xmake-repo package 追加（第6章準拠）
- [ ] ホストテスト動作確認
- [ ] CI パス確認

### 8.2 既存ライブラリ移行時（umimmio, umitest）

- [ ] 現在の構造を本標準と比較・差分洗出し
- [ ] xmake.lua 単体repo対応追加
- [ ] 依存関係関数の一貫性修正
- [ ] docs/ 構成見直し
- [ ] CI ワークフロー追加・更新
- [ ] arm-embedded-xmake-repo package 追加
- [ ] 全テストパス確認

---

## 9. 参照

- [umibench](../umibench/) - リファレンス実装
- [umitest](../umitest/) - テストフレームワーク
- [umimmio](../umimmio/) - MMIO抽象
- [umirtm](../umirtm/) - RTT互換デバッグモニタ
- [umiport](../umiport/) - プラットフォーム共有インフラ
- [arm-embedded-xmake-repo](../../.refs/arm-embedded-xmake-repo/) - パッケージリポジトリ

---

*Last updated: 2026-02-07*  
*Next review: umibench v1.0.0 リリース時*

# モノレポ統合 + ライブラリ公開ガイド

**Version**: 2.0.0
**適用対象**: `umi` モノレポ内の公開ライブラリ群（umitest, umimmio, umirtm, umibench, umiport）

---

## 1. 方針と背景

### 1.1 現状

```
umi/                                 # モノレポ (single source of truth)
├── lib/umibench/                    # ベンチマークフレームワーク
├── lib/umimmio/                     # MMIO レジスタ抽象化
├── lib/umirtm/                      # RTT デバッグモニタ
├── lib/umitest/                     # テストフレームワーク
├── lib/umiport/                     # 共有プラットフォーム基盤 (STM32F4 startup, linker, UART)
├── lib/umi/                         # メインフレームワーク（分離対象外）
├── lib/docs/                        # ライブラリ共通ドキュメント
├── .refs/arm-embedded-xmake-repo/   # xmake パッケージリポジトリ（手動 clone）
├── .github/workflows/               # CI（モノレポルート）
└── xmake.lua                        # 統合ビルド
```

各ライブラリの現在の装備：

| 装備 | umibench | umitest | umimmio | umirtm | umiport |
|------|----------|---------|---------|--------|---------|
| `VERSION` | 0.1.0 | 0.1.0 | 0.1.0 | 0.1.0 | 0.1.0 |
| `CHANGELOG.md` | o | o | o | o | - |
| `RELEASE.md` | o | o | o | o | - |
| `LICENSE` | MIT | MIT | MIT | MIT | MIT |
| `README.md` | o | o | o | o | o |
| `Doxyfile` | 2953行(フルデフォルト) | 38行 | 38行 | 38行 | - |
| `.github/workflows/` | CI + Doxygen | CI + Doxygen | CI + Doxygen | CI + Doxygen | - |
| `.gitignore` | o | o | o | o | o(簡易版) |
| `xmake.lua` standalone判定 | o | o | o | o | - |
| `platforms/` | o | o | o | o | - |

### 1.2 選択した戦略

**モノレポ統合開発 + xmake パッケージ配布**

- 開発は `umi` リポジトリのみ。独立 git リポジトリは作らない
- 外部ユーザーへの配布は xmake パッケージリポジトリ (`synthernet-xmake-repo`) 経由
- パッケージリポジトリは git submodule で管理（§1.4参照）
- 独立リポジトリ化は将来的に `git subtree split` で対応可能（§5参照）
- **ライブラリ自体は submodule にしない**

### 1.3 なぜライブラリを submodule にしないか

| 問題 | 影響 |
|---|---|
| 原子的コミット不可 | umitest API変更 + 他ライブラリのテスト修正が複数リポジトリにまたがる |
| ダイヤモンド依存 | umibench と umirtm が両方 umitest を submodule 参照 → バージョン衝突 |
| sha 固定のフラジリティ | `git submodule update --init --recursive` 忘れでビルド壊れ |
| ブランチ作業の複雑化 | feature branch で submodule 更新 → merge 時にコンフリクト |
| 一人開発のコスト | 管理オーバーヘッドに対してメリットがない |

### 1.4 パッケージリポジトリの管理

#### リポジトリ名称

| 候補 | 評価 | 理由 |
|---|---|---|
| `arm-embedded-xmake-repo` (現名) | x | ARM に限定される名前。UMI ライブラリや coding-rules が入る時点で不正確 |
| `umi-xmake-repo` | - | arm-embedded/coding-rules は UMI 固有ではない |
| `synthernet-xmake-repo` | **採用** | 屋号（ブランド）レベル。全プロダクトを包含 |

**`synthernet-xmake-repo`** を採用する。xmake からは `add_repositories("synthernet ...")` で参照。

#### 現状と移行先

**現状:** `.refs/arm-embedded-xmake-repo/` — gitignore、手動 clone

**移行先:** `xmake-repo/synthernet/` — git submodule

| 観点 | 現状 (.refs/ 手動) | 移行先 (submodule) |
|---|---|---|
| 再現性 | 手動 clone 必要 | `git submodule update --init` で自動 |
| CI | GitHub URL フォールバック | `actions/checkout` の `submodules: true` |
| 配置の意図 | 参考資料と混在 | xmake 専用ディレクトリで明確 |
| 開発時 | 直接編集可能 | 直接編集可能 |

パッケージリポジトリは「自分が開発する密結合なライブラリ」ではなく
「独立したインフラを固定バージョンで参照する」用途であり、submodule の正しい使い方。

#### セットアップ手順

```bash
# 1. GitHub でリポジトリをリネーム
#    arm-embedded-xmake-repo → synthernet-xmake-repo

# 2. submodule として追加
git submodule add https://github.com/user/synthernet-xmake-repo.git xmake-repo/synthernet

# 3. .refs/ の旧クローンは削除可能（.refs/ 自体は他の参考資料用に残す）
```

#### xmake.lua の参照パス

```lua
-- ルート xmake.lua
-- submodule を優先、なければ GitHub URL にフォールバック
if os.isdir(path.join(os.projectdir(), "xmake-repo/synthernet")) then
    add_repositories("synthernet " .. path.join(os.projectdir(), "xmake-repo/synthernet"))
else
    add_repositories("synthernet https://github.com/user/synthernet-xmake-repo.git")
end
```

フォールバックにより submodule を init していない環境でも動作する。

#### CI での利用

```yaml
steps:
  - uses: actions/checkout@v4
    with:
      submodules: true    # submodule を自動取得
```

#### dev バージョンのパス

submodule の配置により、パッケージ定義内の dev パスが変わる:

```lua
-- xmake-repo/synthernet/packages/u/umimmio/xmake.lua
-- 旧: add_versions("dev", "git:../../../lib/umimmio")
add_versions("dev", "git:../../../../lib/umimmio")
-- (xmake-repo/synthernet/packages/u/ → lib/ で4階層)
```

---

## 2. バージョン管理戦略

### 2.1 統一バージョン vs 個別バージョン

**推奨: 統一バージョン管理**

現状は各ライブラリが個別に `VERSION` ファイル（全て `0.1.0`）を持つが、モノレポ統合では以下の理由から統一バージョンが適切：

- 依存ライブラリ（umitest）の API が変わるとき、全テストが同時に更新される
- リリースは `umi` リポジトリ単位で行う
- ユーザーに「umimmio 0.3.0 は umitest 0.2.0 と互換」のような組み合わせを考えさせない

### 2.2 バージョン管理の実装

```
umi/
├── VERSION                       # ルートバージョン: "0.2.0" など（新規作成）
├── lib/umibench/VERSION          # ルート VERSION と同期（自動化推奨）
├── lib/umimmio/VERSION           #   〃
├── lib/umirtm/VERSION            #   〃
├── lib/umitest/VERSION           #   〃
└── lib/umiport/VERSION           #   〃
```

**現状:** ルート `VERSION` ファイルは存在しない。`xmake.lua` で `set_version("0.2.0")` と直接指定。

**同期ルール:**

1. ルート `VERSION` が正（single source of truth）
2. `lib/*/VERSION` はリリース時にルートからコピー
3. `CHANGELOG.md` は各ライブラリ個別に維持（変更の無いライブラリは "No changes" と記録）

### 2.3 git タグ規約

```bash
# 統一リリースタグ
git tag v0.2.0

# ライブラリ個別タグ（パッケージ配布用）
git tag umibench/v0.2.0
git tag umimmio/v0.2.0
git tag umirtm/v0.2.0
git tag umitest/v0.2.0
git tag umiport/v0.2.0
```

個別タグは xmake パッケージの `add_versions` で参照する（§4参照）。

---

## 3. 独立リポジトリ向けフォーマットの整理

### 3.1 残すもの（モノレポでも価値がある）

| ファイル | 理由 |
|---|---|
| `include/<lib>/` | 公開ヘッダ。パッケージインストール時にそのままコピー |
| `tests/` | テストコード。CI で個別実行可能 |
| `examples/` | ユーザー向けサンプル。ドキュメントとして有用 |
| `docs/` | 設計文書。ライブラリの自己説明性を維持 |
| `platforms/` | マルチターゲット定義 |
| `VERSION` | パッケージ配布時に必要 |
| `CHANGELOG.md` | パッケージ配布時に同梱 |
| `LICENSE` | パッケージ配布時に同梱 |
| `README.md` | パッケージ配布時に同梱。GitHub ディレクトリ表示で有用 |

### 3.2 各ファイルのモノレポ適合性

#### 判定一覧

| ファイル | モノレポで動作するか | 判定 | 理由 |
|---|---|---|---|
| `.github/workflows/` | モノレポ内では動かない | 削除 | 下記参照 |
| `.gitignore` | 冗長 | 削除 | 下記参照 |
| `Doxyfile` | 機能する | 維持+改善 | 下記参照 |
| `LICENSE` | git無関係 | 維持 | パッケージ配布に必須。法的に各ライブラリ単位で存在すべき |
| `README.md` | git無関係 | 維持 | パッケージ配布・GitHub ディレクトリ表示で有用 |
| `CHANGELOG.md` | git無関係 | 維持 | ライブラリごとの変更追跡。パッケージ配布に同梱 |
| `VERSION` | git無関係 | 維持 | パッケージ定義から参照 |
| `RELEASE.md` | git無関係 | 統合 | 下記参照 |
| `docs/` | git無関係 | 維持 | 設計文書はライブラリに紐づく |
| `examples/` | git無関係 | 維持 | |
| `tests/` | 動作する | 維持 | |
| `platforms/` | 動作する | 維持 | |
| `xmake.lua` | 動作する | 維持 | standalone判定でデュアルモード動作 |

#### `.github/workflows/` → ルートに統合、ライブラリ内は削除

GitHub Actions はリポジトリルートの `.github/` しか認識しないため、
**現在の `lib/*/.github/workflows/*.yml` はモノレポ内では完全に無視される**。

**現在の状態:**

| 場所 | ファイル | 用途 |
|---|---|---|
| `.github/workflows/ci.yml` | ルート | モノレポ統合CI（動作する） |
| `.github/workflows/umibench-ci.yml` | ルート | umibench 個別CI（動作する） |
| `.github/workflows/*-doxygen.yml` | ルート | Doxygen生成（動作する、4ファイル） |
| `lib/umibench/.github/workflows/` | ライブラリ内 | CI + Doxygen（動作しない） |
| `lib/umitest/.github/workflows/` | ライブラリ内 | CI + Doxygen（動作しない） |
| `lib/umimmio/.github/workflows/` | ライブラリ内 | CI + Doxygen（動作しない） |
| `lib/umirtm/.github/workflows/` | ライブラリ内 | CI + Doxygen（動作しない） |

**対応:**
1. ルートの `.github/workflows/` を正として整理・統合
2. `lib/*/.github/` ディレクトリを削除

**統合後のルート CI 構成:**

```
.github/workflows/
├── ci.yml                          # 全ライブラリテスト + WASM + ARM ビルド
├── doxygen.yml                     # 全ライブラリ Doxygen 生成・デプロイ
└── release.yml                     # リリース自動化
```

**`.github/workflows/ci.yml`:**

```yaml
name: UMI CI

on:
  push:
    branches: [main, develop]
    paths:
      - 'lib/**'
      - 'tests/**'
      - 'xmake.lua'
  pull_request:

jobs:
  host-tests:
    runs-on: ${{ matrix.os }}
    strategy:
      matrix:
        os: [ubuntu-latest, macos-latest]
    steps:
      - uses: actions/checkout@v4
      - uses: xmake-io/github-action-setup-xmake@v1
      - run: xmake test

  wasm-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: xmake-io/github-action-setup-xmake@v1
      - uses: mymindstorm/setup-emsdk@v14
      - run: |
          source "$EMSDK/emsdk_env.sh"
          xmake test "*_wasm/*"

  arm-build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: xmake-io/github-action-setup-xmake@v1
      - run: sudo apt-get install -y gcc-arm-none-eabi libnewlib-arm-none-eabi
      - run: |
          xmake build umibench_stm32f4_renode_gcc
          xmake build umimmio_stm32f4_renode_gcc
          xmake build umirtm_stm32f4_renode_gcc
          xmake build umitest_stm32f4_renode_gcc

  doxygen:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: sudo apt-get install -y doxygen
      - run: |
          for lib in umibench umimmio umirtm umitest; do
            cd lib/$lib && mkdir -p build/doxygen && doxygen && cd ../..
          done
```

#### `.gitignore` → 削除

**現状:** 全4ライブラリの `.gitignore` は同一内容（`build/`, `.xmake/`, `.cache/`, `.clangd`, `compile_commands.json`, `.DS_Store`, `*~`, `*.swp`）。umiport は簡易版（`build/`, `.xmake/`, `compile_commands.json`）。

ルートの `.gitignore` に `build/`, `.xmake/`, `.cache/`, `.clangd`, `compile_commands.json` 等が既にある。
git の `.gitignore` パターンは **スラッシュなしの `build/` は全階層でマッチする**ため、
`lib/umibench/build/` もルートの定義でカバー済み。

**対応:**
1. ルートの `.gitignore` に不足パターンがないか確認
2. ルートに `.build/` パターンを追加（現在は `lib/**/.build/` で限定的）
3. `lib/*/.gitignore` を全て削除

#### `Doxyfile` → 維持 + `@INCLUDE` による共通化

**現状:**
- umimmio, umirtm, umitest: 38行のコンパクトな Doxyfile（差分のみ記述）
- umibench: 2953行のフルデフォルト Doxyfile（`doxygen -g` 生成そのまま）
- 4ファイルの実質的な差分は `PROJECT_NAME`, `PROJECT_NUMBER`, `PROJECT_BRIEF` の3項目のみ

**推奨構成:**

```
lib/
├── docs/Doxyfile.base              # 共通設定（テーマ、出力形式、品質ゲート等）
├── umitest/Doxyfile                # @INCLUDE + 3行のプロジェクト固有設定
├── umimmio/Doxyfile                # @INCLUDE + 3行
├── umirtm/Doxyfile                 # @INCLUDE + 3行
└── umibench/Doxyfile               # @INCLUDE + 3行（2953行から正規化）
```

**`lib/docs/Doxyfile.base`:**

現在の3ライブラリ（umimmio/umirtm/umitest）で共通の設定を抽出:

```doxyfile
# UMI ライブラリ共通 Doxyfile 設定
# 各ライブラリの Doxyfile から @INCLUDE ../docs/Doxyfile.base で参照

DOXYFILE_ENCODING      = UTF-8
OUTPUT_DIRECTORY       = build/doxygen

# Input
INPUT                  = README.md docs include examples tests
FILE_PATTERNS          = *.hh *.cc *.md
RECURSIVE              = YES
USE_MDFILE_AS_MAINPAGE = README.md
EXCLUDE                = build

# Extraction
EXTRACT_ALL            = NO

# Warnings (quality gates)
WARN_IF_UNDOCUMENTED   = YES
WARN_IF_INCOMPLETE_DOC = YES

# Output
GENERATE_HTML          = YES
GENERATE_LATEX         = NO

# UMI unified theme
HTML_COLORSTYLE        = AUTO_LIGHT
HTML_COLORSTYLE_HUE    = 220
HTML_COLORSTYLE_SAT    = 100
HTML_COLORSTYLE_GAMMA  = 80

# UX
HTML_CODE_FOLDING      = YES
HTML_COPY_CLIPBOARD    = YES

# Code style
TAB_SIZE               = 4
SOURCE_BROWSER         = NO
HAVE_DOT               = NO
```

**各ライブラリの `Doxyfile`（例: umimmio）:**

```doxyfile
@INCLUDE = ../docs/Doxyfile.base
PROJECT_NAME           = "umimmio"
PROJECT_NUMBER         = 0.1.0
PROJECT_BRIEF          = "UMI Memory-mapped I/O library"
```

**利点:**
- テーマ変更やオプション追加が1箇所で済む
- 各ライブラリの Doxyfile が4行になり意図が明確
- `cd lib/umimmio && doxygen` で個別生成が引き続き動作

**umibench の Doxyfile**: 2953行のフルデフォルトを削除し、上記の4行形式に正規化する。
カスタマイズされた設定は全て他3ライブラリと同一であり、フルデフォルトを保持する理由がない。

#### `RELEASE.md` → ルートに統合

**現状:** 各ライブラリに個別の RELEASE.md が存在する。構造は同一（Release Line, Versioning Rules, Changelog Rules, Release Checklist, CI Scope）だが、微妙に異なる:

- umitest: semver、CI Scope は host tests のみ
- umibench: 0.x.y の pre-release 規約あり、CI Scope に WASM + compile-fail を含む

**対応:**
- 各ライブラリの CI Scope の差異は統合 CI（§3.2）で吸収される
- バージョニング規約は統一バージョン管理（§2.1）に統合
- ルートに統一 `RELEASE.md` を作成し、各ライブラリの `RELEASE.md` を削除

### 3.3 standalone_repo 判定の変更

現在の `standalone_repo` 判定は残す。これにより:

- モノレポ内: `includes("lib/umibench")` → `standalone_repo = false` → `add_deps()` 使用
- パッケージ配布後の外部利用: ユーザーが `add_requires("umibench")` → standalone xmake.lua が有効

**変更不要。** 現在の設計が正しい。

---

## 4. xmake パッケージ配布

### 4.1 現状のパッケージ定義

現在の `.refs/arm-embedded-xmake-repo/packages/u/*/xmake.lua` は `"dev"` バージョンのみ:

```lua
add_versions("dev", "git:../../../lib/umibench")
```

これはローカル開発用。外部配布には以下が必要:
- バージョン付きソースアーカイブの URL
- ソースハッシュ（sha256）

### 4.2 パッケージ定義のバージョン対応

`synthernet-xmake-repo/packages/u/umimmio/xmake.lua` の完成形:

```lua
package("umimmio")
    set_homepage("https://github.com/user/umi")
    set_description("UMI type-safe memory-mapped I/O abstraction library")
    set_license("MIT")

    set_kind("library", {headeronly = true})

    -- リリースバージョン（GitHub release のアーカイブから取得）
    -- sha256 はリリースCI で自動計算・更新
    add_versions("0.2.0", "<sha256>")
    add_urls("https://github.com/user/umi/releases/download/umimmio/v$(version)/umimmio-$(version).tar.gz")

    -- 開発版（ローカル参照）
    add_versions("dev", "git:../../../../lib/umimmio")

    on_install(function(package)
        os.cp("include", package:installdir())
    end)

    on_test(function(package)
        assert(package:check_cxxsnippets({test = [[
            #include <umimmio/mmio.hh>
            void test() {}
        ]]}, {configs = {languages = "c++23"}}))
    end)
package_end()
```

### 4.3 umiport のパッケージ

umiport は他の4ライブラリとは性質が異なる。公開ヘッダ（`include/umiport/`）だけでなく、
startup.cc, syscalls.cc, linker.ld 等のソースファイルも配布する必要がある:

```lua
package("umiport")
    set_homepage("https://github.com/user/umi")
    set_description("UMI shared platform infrastructure for embedded targets")
    set_license("MIT")

    set_kind("library", {headeronly = false})

    add_versions("0.2.0", "<sha256>")
    add_urls("https://github.com/user/umi/releases/download/umiport/v$(version)/umiport-$(version).tar.gz")

    add_versions("dev", "git:../../../../lib/umiport")

    on_install(function(package)
        os.cp("include", package:installdir())
        os.cp("src", package:installdir())
        os.cp("renode", package:installdir())
    end)
package_end()
```

### 4.4 リリースアーカイブの自動生成

各ライブラリのリリース用アーカイブは、モノレポから必要なファイルだけを抽出して作る。

**`tools/package_lib.sh`:**

```bash
#!/bin/bash
# Usage: tools/package_lib.sh <libname> <version>
# Example: tools/package_lib.sh umimmio 0.2.0

set -euo pipefail

LIBNAME="$1"
VERSION="$2"
ARCHIVE_NAME="${LIBNAME}-${VERSION}"
STAGING_DIR="build/packages/${ARCHIVE_NAME}"

rm -rf "$STAGING_DIR"
mkdir -p "$STAGING_DIR"

# 共通ファイル
cp -r "lib/${LIBNAME}/include"      "$STAGING_DIR/" 2>/dev/null || true
cp    "lib/${LIBNAME}/VERSION"      "$STAGING_DIR/"
cp    "lib/${LIBNAME}/LICENSE"       "$STAGING_DIR/"
cp    "lib/${LIBNAME}/README.md"     "$STAGING_DIR/"
cp    "lib/${LIBNAME}/CHANGELOG.md"  "$STAGING_DIR/" 2>/dev/null || true

# platforms/ を持つライブラリはコピー
if [ -d "lib/${LIBNAME}/platforms" ]; then
    cp -r "lib/${LIBNAME}/platforms" "$STAGING_DIR/"
fi

# umiport 固有: src/ と renode/ もコピー
if [ -d "lib/${LIBNAME}/src" ]; then
    cp -r "lib/${LIBNAME}/src" "$STAGING_DIR/"
fi
if [ -d "lib/${LIBNAME}/renode" ]; then
    cp -r "lib/${LIBNAME}/renode" "$STAGING_DIR/"
fi

# アーカイブ生成
cd build/packages
tar czf "${ARCHIVE_NAME}.tar.gz" "${ARCHIVE_NAME}"
sha256sum "${ARCHIVE_NAME}.tar.gz" > "${ARCHIVE_NAME}.tar.gz.sha256"

echo "Created: build/packages/${ARCHIVE_NAME}.tar.gz"
echo "SHA256:  $(cat "${ARCHIVE_NAME}.tar.gz.sha256")"
```

### 4.5 リリース CI（GitHub Actions）

```yaml
# .github/workflows/release.yml
name: Release Libraries

on:
  push:
    tags:
      - 'v*'               # 統一タグ
      - '*/v*'             # ライブラリ個別タグ (umimmio/v0.2.0)

permissions:
  contents: write

jobs:
  release:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Parse tag
        id: tag
        run: |
          TAG="${GITHUB_REF#refs/tags/}"
          if [[ "$TAG" == v* ]]; then
            # 統一リリース: v0.2.0
            echo "version=${TAG#v}" >> "$GITHUB_OUTPUT"
            echo "libs=umitest umimmio umirtm umibench umiport" >> "$GITHUB_OUTPUT"
            echo "unified=true" >> "$GITHUB_OUTPUT"
          else
            # 個別リリース: umimmio/v0.2.0
            LIB="${TAG%%/*}"
            VER="${TAG##*/v}"
            echo "version=$VER" >> "$GITHUB_OUTPUT"
            echo "libs=$LIB" >> "$GITHUB_OUTPUT"
            echo "unified=false" >> "$GITHUB_OUTPUT"
          fi

      - name: Build archives
        run: |
          for lib in ${{ steps.tag.outputs.libs }}; do
            bash tools/package_lib.sh "$lib" "${{ steps.tag.outputs.version }}"
          done

      - name: Create GitHub Release
        uses: softprops/action-gh-release@v2
        with:
          files: |
            build/packages/*.tar.gz
            build/packages/*.sha256
          generate_release_notes: true

      - name: Show package repo update instructions
        run: |
          echo "=== Update synthernet-xmake-repo ==="
          for lib in ${{ steps.tag.outputs.libs }}; do
            VER="${{ steps.tag.outputs.version }}"
            SHA=$(cat "build/packages/${lib}-${VER}.tar.gz.sha256" | awk '{print $1}')
            echo ""
            echo "xmake-repo/synthernet/packages/u/${lib}/xmake.lua:"
            echo "  add_versions(\"${VER}\", \"${SHA}\")"
          done
```

### 4.6 外部ユーザーの利用方法

```lua
-- 外部ユーザーの xmake.lua
add_repositories("synthernet https://github.com/user/synthernet-xmake-repo.git")

add_requires("umimmio 0.2.0")
add_requires("umitest 0.2.0")

target("my_app")
    set_kind("binary")
    add_packages("umimmio")
    add_packages("umitest")
```

---

## 5. git subtree による独立リポジトリ公開（オプション）

xmake パッケージ配布だけでは不十分な場合（GitHub で独立リポジトリとして見せたい等）、
`git subtree split` で read-only ミラーを生成できる。

### 5.1 ミラー用リポジトリの初回セットアップ

```bash
# GitHub で空リポジトリ作成: user/umimmio (read-only mirror)

# remote 追加
git remote add umimmio-mirror git@github.com:user/umimmio.git
git remote add umirtm-mirror  git@github.com:user/umirtm.git
git remote add umitest-mirror git@github.com:user/umitest.git
git remote add umibench-mirror git@github.com:user/umibench.git
```

### 5.2 手動ミラー同期

```bash
# lib/umimmio/ のコミット履歴を独立ブランチに分割
git subtree split --prefix=lib/umimmio -b umimmio-split

# ミラーリポジトリに push
git push umimmio-mirror umimmio-split:main

# ブランチ削除（後始末）
git branch -D umimmio-split
```

### 5.3 全ライブラリ一括ミラー同期スクリプト

**`tools/sync_mirrors.sh`:**

```bash
#!/bin/bash
# Usage: tools/sync_mirrors.sh [--dry-run]
# 全独立ライブラリのミラーリポジトリを同期

set -euo pipefail

DRY_RUN="${1:-}"
LIBS=(umitest umimmio umirtm umibench)

for lib in "${LIBS[@]}"; do
    echo "=== Syncing ${lib} ==="

    BRANCH="${lib}-split"
    REMOTE="${lib}-mirror"
    PREFIX="lib/${lib}"

    # subtree split
    git subtree split --prefix="${PREFIX}" -b "${BRANCH}"

    if [ "$DRY_RUN" = "--dry-run" ]; then
        echo "[dry-run] Would push ${BRANCH} to ${REMOTE}:main"
    else
        # ミラーに force push（read-only mirror なので安全）
        git push "${REMOTE}" "${BRANCH}:main" --force
    fi

    # 一時ブランチ削除
    git branch -D "${BRANCH}"

    echo "=== Done: ${lib} ==="
    echo
done
```

**注:** umiport はミラー対象外（モノレポ内部インフラであり、単独での利用ニーズがない）。

### 5.4 CI による自動ミラー同期

```yaml
# .github/workflows/sync-mirrors.yml
name: Sync Library Mirrors

on:
  push:
    branches: [main]
    paths:
      - 'lib/umitest/**'
      - 'lib/umimmio/**'
      - 'lib/umirtm/**'
      - 'lib/umibench/**'

jobs:
  sync:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0  # subtree split に完全履歴が必要

      - name: Configure git
        run: |
          git config user.name "github-actions[bot]"
          git config user.email "github-actions[bot]@users.noreply.github.com"

      - name: Sync mirrors
        env:
          DEPLOY_KEY_UMIMMIO: ${{ secrets.DEPLOY_KEY_UMIMMIO }}
          # 各ミラー用の deploy key...
        run: bash tools/sync_mirrors.sh
```

### 5.5 ミラーリポジトリの README

ミラーリポジトリの README.md 先頭に以下を追加:

```markdown
> **Note**: This is a read-only mirror of [`umi/lib/umimmio`](https://github.com/user/umi/tree/main/lib/umimmio).
> Issues and PRs should be filed on the [main repository](https://github.com/user/umi).
```

これは各ライブラリの README.md 自体には書かない（モノレポ内では意味がないため）。
ミラー同期スクリプトで先頭に自動挿入する。

---

## 6. リリースフロー

### 6.1 統一リリース手順

```bash
# 1. バージョン決定
NEW_VERSION="0.3.0"

# 2. ルート VERSION 更新
echo "$NEW_VERSION" > VERSION

# 3. 各ライブラリの VERSION 同期
for lib in lib/umitest lib/umimmio lib/umirtm lib/umibench lib/umiport; do
    echo "$NEW_VERSION" > "${lib}/VERSION"
done

# 4. 各ライブラリの CHANGELOG.md 更新
#    [Unreleased] → [0.3.0] - 2026-MM-DD

# 5. コミット
git add -A
git commit -m "release: v${NEW_VERSION}"

# 6. タグ（統一 + 個別）
git tag "v${NEW_VERSION}"
git tag "umitest/v${NEW_VERSION}"
git tag "umimmio/v${NEW_VERSION}"
git tag "umirtm/v${NEW_VERSION}"
git tag "umibench/v${NEW_VERSION}"
git tag "umiport/v${NEW_VERSION}"

# 7. push
git push origin main --tags

# 8. CI が自動で:
#    - テスト実行
#    - アーカイブ生成
#    - GitHub Release 作成
#    - (設定済みなら) ミラー同期
```

### 6.2 リリース自動化スクリプト

**`tools/release.sh`:**

```bash
#!/bin/bash
# Usage: tools/release.sh <version>
# Example: tools/release.sh 0.3.0

set -euo pipefail

VERSION="$1"
DATE=$(date +%Y-%m-%d)
LIBS=(umitest umimmio umirtm umibench umiport)

echo "=== Releasing v${VERSION} ==="

# 事前チェック
git diff --quiet || { echo "ERROR: uncommitted changes"; exit 1; }
git diff --cached --quiet || { echo "ERROR: staged changes"; exit 1; }

# テスト実行
echo "Running tests..."
xmake test || { echo "ERROR: tests failed"; exit 1; }

# VERSION ファイル更新
echo "$VERSION" > VERSION
for lib in "${LIBS[@]}"; do
    echo "$VERSION" > "lib/${lib}/VERSION"
done

# Doxyfile の PROJECT_NUMBER 更新
for lib in "${LIBS[@]}"; do
    DOXYFILE="lib/${lib}/Doxyfile"
    if [ -f "$DOXYFILE" ]; then
        sed -i.bak "s/^PROJECT_NUMBER.*/PROJECT_NUMBER         = ${VERSION}/" "$DOXYFILE"
        rm -f "${DOXYFILE}.bak"
    fi
done

# xmake.lua の set_version 更新
sed -i.bak "s/^set_version(\".*\")/set_version(\"${VERSION}\")/" xmake.lua
rm -f "xmake.lua.bak"

# CHANGELOG.md の [Unreleased] → [VERSION] 置換
for lib in "${LIBS[@]}"; do
    CHANGELOG="lib/${lib}/CHANGELOG.md"
    if [ -f "$CHANGELOG" ]; then
        sed -i.bak "s/## \[Unreleased\]/## [Unreleased]\n\n## [${VERSION}] - ${DATE}/" "$CHANGELOG"
        rm -f "${CHANGELOG}.bak"
    fi
done

# アーカイブ生成 (sha256 確認用)
for lib in "${LIBS[@]}"; do
    bash tools/package_lib.sh "$lib" "$VERSION"
done

# コミット
git add -A
git commit -m "release: v${VERSION}"

# タグ
git tag "v${VERSION}"
for lib in "${LIBS[@]}"; do
    git tag "${lib}/v${VERSION}"
done

echo ""
echo "=== Release v${VERSION} prepared ==="
echo "Run the following to publish:"
echo "  git push origin main --tags"
echo ""
echo "Package archives:"
ls -la build/packages/*.tar.gz
echo ""
echo "SHA256 hashes (for xmake package repo):"
cat build/packages/*.sha256
```

---

## 7. ディレクトリ構造の最終形

```
umi/
├── VERSION                           # ルートバージョン（正）
├── RELEASE.md                        # 統一リリースポリシー
├── xmake.lua                         # 統合ビルド定義
│
├── .github/workflows/
│   ├── ci.yml                        # 統合CI（全ライブラリ: host + WASM + ARM）
│   ├── doxygen.yml                   # 全ライブラリ Doxygen 生成・デプロイ
│   ├── release.yml                   # リリース自動化
│   └── sync-mirrors.yml              # ミラー同期（オプション）
│
├── tools/
│   ├── package_lib.sh                # ライブラリアーカイブ生成
│   ├── release.sh                    # リリース手順自動化
│   └── sync_mirrors.sh              # ミラー同期（オプション）
│
├── lib/
│   ├── docs/
│   │   ├── Doxyfile.base             # 全ライブラリ共通 Doxygen 設定
│   │   ├── INDEX.md                  # ライブラリ開発ガイド索引
│   │   ├── standards/                # コーディング規約・ライブラリ標準
│   │   └── guides/                   # 作成ガイド・Doxygen運用・テスト
│   │
│   ├── umitest/                      # テストフレームワーク
│   │   ├── include/umitest/          # 公開ヘッダ
│   │   ├── tests/                    # テスト
│   │   ├── examples/                 # サンプル
│   │   ├── docs/                     # ドキュメント
│   │   ├── platforms/                # マルチターゲット (STM32F4)
│   │   ├── xmake.lua                 # ビルド定義（standalone対応維持）
│   │   ├── Doxyfile                  # @INCLUDE ../docs/Doxyfile.base + 3行
│   │   ├── VERSION                   # ルートと同期
│   │   ├── LICENSE                   # MIT
│   │   ├── README.md                 # パッケージ同梱用
│   │   └── CHANGELOG.md              # 変更履歴
│   │
│   ├── umimmio/                      # MMIO レジスタ抽象化（同上の構造）
│   ├── umirtm/                       # RTT デバッグモニタ（同上の構造）
│   ├── umibench/                     # ベンチマークフレームワーク（同上の構造）
│   │
│   ├── umiport/                      # 共有プラットフォーム基盤
│   │   ├── include/umiport/          # 公開ヘッダ (stm32f4/uart_output.hh)
│   │   ├── src/stm32f4/              # startup.cc, syscalls.cc, linker.ld
│   │   ├── renode/                   # stm32f4_test.repl
│   │   ├── VERSION                   # ルートと同期
│   │   ├── LICENSE                   # MIT
│   │   └── README.md                 # パッケージ同梱用
│   │
│   └── umi/                          # メインフレームワーク（分離対象外）
│
├── xmake-repo/
│   └── synthernet/                   # git submodule (synthernet-xmake-repo)
│       └── packages/u/
│           ├── umitest/xmake.lua     # バージョン付き配布定義
│           ├── umimmio/xmake.lua
│           ├── umirtm/xmake.lua
│           ├── umibench/xmake.lua
│           └── umiport/xmake.lua
│
└── .refs/                            # gitignore（参考資料・サードパーティ）
```

**削除済み/削除予定のファイル:**

| ファイル | 理由 |
|---|---|
| `lib/*/.github/workflows/` | ルートに統合（モノレポ内では動作しない） |
| `lib/*/.gitignore` | ルートの `.gitignore` でカバー済み |
| `lib/*/RELEASE.md` | ルートの `RELEASE.md` に統合 |
| `lib/*/README.en.md` | 将来的に README.md を英語化し、不要になる可能性 |

---

## 8. パッケージリポジトリ更新フロー

### 8.1 リリース時の更新手順

リリースでアーカイブとsha256が確定したら:

```bash
cd xmake-repo/synthernet

# パッケージ定義にバージョン追加
# packages/u/umimmio/xmake.lua に以下を追加:
#   add_versions("0.3.0", "<sha256>")

git add -A
git commit -m "feat: add v0.3.0 packages (umitest, umimmio, umirtm, umibench, umiport)"
git push origin main

# umi リポジトリ側で submodule 参照を更新
cd ../..  # umi ルートに戻る
git add xmake-repo/synthernet
git commit -m "chore: update synthernet-xmake-repo"
```

### 8.2 dev バージョンの扱い

`dev` バージョンはモノレポ内での開発時にのみ使用:

```lua
-- umi/xmake.lua（モノレポルート）
-- パッケージリポジトリをローカル参照
add_repositories("synthernet xmake-repo/synthernet")

-- lib/ 内を直接 includes（dev バージョンは使わない）
includes("lib/umimmio")
includes("lib/umitest")
```

`dev` バージョンは、ライブラリを独立リポジトリとして開発・テストする場合の
フォールバック用。通常のモノレポ開発では `includes()` + `add_deps()` パスを使用。

---

## 9. 移行チェックリスト

### 9.1 即座に実行（低コスト・高効果）

- [ ] ルート `VERSION` ファイルを作成（`0.2.0`）
- [ ] `lib/*/.gitignore` を全て削除（ルートでカバー済み確認後）
- [ ] `lib/*/RELEASE.md` を全て削除、ルート `RELEASE.md` を作成
- [ ] `lib/docs/Doxyfile.base` を作成
- [ ] 各ライブラリの Doxyfile を `@INCLUDE` 形式に変更（umibench は2953行→4行）
- [ ] `lib/*/.github/` を全て削除
- [ ] ルートの `.github/workflows/` を統合CI（ci.yml + doxygen.yml）に整理

### 9.2 パッケージリポジトリ移行

- [ ] `arm-embedded-xmake-repo` → `synthernet-xmake-repo` にリネーム（GitHub）
- [ ] `xmake-repo/synthernet` として git submodule 追加
- [ ] ルート `xmake.lua` の参照パスを更新
- [ ] 各パッケージ定義に `add_urls()` を追加
- [ ] umiport のパッケージ定義を追加

### 9.3 リリース自動化

- [ ] `tools/package_lib.sh` を作成
- [ ] `tools/release.sh` を作成
- [ ] `.github/workflows/release.yml` をルートに作成

### 9.4 初回リリース

- [ ] `tools/release.sh` でリリース実行
- [ ] GitHub Release にアーカイブがアップロードされることを確認
- [ ] `synthernet-xmake-repo` にバージョン付き sha256 を追加
- [ ] 外部環境から `add_requires("umimmio 0.2.0")` で取得できることを検証

### 9.5 オプション（外部公開ニーズが発生した場合のみ）

- [ ] ミラーリポジトリ作成（GitHub）
- [ ] `tools/sync_mirrors.sh` を作成
- [ ] `.github/workflows/sync-mirrors.yml` を作成
- [ ] deploy key の設定

---

## 10. 判断フローチャート

```
新しいリリースを出す
  │
  ├─ tools/release.sh <version> を実行
  │    ├─ テスト実行
  │    ├─ VERSION 同期（ルート + 全ライブラリ）
  │    ├─ Doxyfile PROJECT_NUMBER 更新
  │    ├─ xmake.lua set_version 更新
  │    ├─ CHANGELOG 更新
  │    ├─ アーカイブ生成（5ライブラリ分）
  │    ├─ コミット + タグ（統一 + 個別）
  │    └─ push → CI が GitHub Release 作成
  │
  ├─ synthernet-xmake-repo を更新
  │    ├─ sha256 をパッケージ定義に追加
  │    ├─ push (submodule 内)
  │    └─ umi 側で submodule 参照を commit
  │
  └─ (オプション) ミラー同期
       └─ tools/sync_mirrors.sh
```

---

## 11. FAQ

### Q: 一部のライブラリだけリリースしたい場合は？

統一バージョンでも、CHANGELOG に "No changes" と明記すればよい。
パッケージとしては同じバージョンが配布される（ヘッダ内容は同一で問題ない）。

本当にライブラリ個別のバージョニングが必要になったら、その時点で個別タグ
（`umimmio/v0.4.0` のみ、他は v0.3.0 のまま）に切り替える。

### Q: 外部ユーザーが umitest を使わずに umimmio だけ使えるか？

使える。`add_requires("umimmio")` の on_install は `include/` のみコピーするため、
umitest への依存は一切発生しない。

### Q: umi 本体（umi.core, umi.dsp 等）もパッケージ配布する？

現時点では不要。umi 本体は embedded kernel と密結合しており、
単独パッケージとして利用するユースケースがまだない。
需要が出た時点で `umi-core`, `umi-dsp` 等として切り出す。

### Q: umiport もミラー同期する？

しない。umiport はモノレポ内部インフラ（STM32F4 のstartup/linker/UART出力の共有基盤）であり、
単独リポジトリとしての利用ニーズがない。xmake パッケージとしては配布する（ARM embedded プロジェクトで利用可能にするため）。

### Q: subtree split の履歴が汚い場合は？

`git subtree split` は該当ディレクトリの変更だけを抽出するが、
コミットメッセージにモノレポ全体の文脈が残る。
気になる場合は `--squash` オプション、または `git filter-repo` を使用して
クリーンな履歴を生成する。ただし read-only ミラーなので通常は気にしなくてよい。

---

*Last updated: 2026-02-07*

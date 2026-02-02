# UMI Test Strategy

## 概要

UMIのテスト戦略は3つの環境をカバーします:

| 環境 | 目的 | ツール | CI 自動化 |
|------|------|--------|-----------|
| **Host** | ユニットテスト、高速イテレーション | xmake test | CI で自動実行 |
| **WASM** | ブラウザ/Node.js での動作確認 | Node.js, Emscripten | CI で自動実行 |
| **ARM ビルド** | クロスコンパイル検証 | arm-embedded | CI で自動実行 |
| **ARM 実行** | 実機相当のエミュレーション | Renode, Robot Framework | ローカルのみ |
| **実機** | ハードウェアでの動作確認 | pyOCD, GDB | ローカルのみ |

---

## 1. ユニットテスト (`tests/`, `lib/<name>/test/`)

### テスト実行

```bash
# 全テスト実行（host.test ルール対象のみ）
xmake test

# 個別実行
xmake run test_<libname>
```

### テストフレームワーク

共通の軽量フレームワーク `tests/test_common.hh` を使用（外部依存なし、組み込み向け）:

```cpp
#include "test_common.hh"
using umi::test::check;

SECTION("My Feature");
check(result == expected, "description");
CHECK_EQ(a, b, "values match");
CHECK_NEAR(actual, expected, "float comparison");

TEST_SUMMARY();  // 結果サマリー出力
```

### テストターゲットの追加方法

`xmake test` にテストを含めるには、`host.test` ルール（`arm-embedded` パッケージ提供）を使用する:

```lua
-- lib/<libname>/test/xmake.lua
target("test_<libname>")
    add_rules("host.test")          -- xmake test の対象になる
    set_default(true)               -- xmake build でもビルド対象
    add_files("test_<topic>.cc")
    add_includedirs("$(projectdir)/lib/<libname>/include")
    add_includedirs("$(projectdir)/tests")  -- test_common.hh
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()
```

`host.test` ルールは以下を自動設定する:

- `set_kind("binary")`
- ホストプラットフォーム/アーキテクチャ
- `set_group("test")`
- `set_languages("c++23")`（未指定時）
- `TESTING` マクロ定義

---

## 2. WASM テスト

### ビルドと実行

WASM ターゲットは `set_plat("wasm")` と `set_toolchains("emcc")` をターゲットに直接指定しているため、プラットフォーム切り替えは不要:

```bash
# ビルド
xmake build <libname>_wasm

# Node.js テスト
node lib/<libname>/test/test_<topic>_wasm.mjs
```

### WASM テストの構成

C API をエクスポートし、Node.js から呼び出す方式:

```cpp
// test_mock_wasm.cc
#include <emscripten/emscripten.h>

extern "C" {
EMSCRIPTEN_KEEPALIVE
float umimock_constant(float value) { ... }
}
```

```javascript
// test_mock_wasm.mjs
const wasm = await createModule();
assertApprox(wasm._umimock_constant(0.5), 0.5, 0.001, 'constant');
```

xmake.lua でのターゲット定義:

```lua
target("<libname>_wasm")
    set_kind("binary")
    set_plat("wasm")
    set_arch("wasm32")
    set_toolchains("emcc")
    set_filename("<libname>_wasm.js")
    add_files("lib/<libname>/test/test_<topic>_wasm.cc")
    add_ldflags("-sEXPORTED_FUNCTIONS=[...]", {force = true})
    add_ldflags("-sMODULARIZE=1", {force = true})
target_end()
```

---

## 3. ARM テスト

### ARM クロスコンパイル（CI 自動化可能）

ARM ビルドの検証。ツールチェーンがあれば CI で実行できる:

```bash
xmake build <libname>_renode
```

### Renode エミュレーション（ローカルのみ）

Renode を使った ARM エミュレーション。CI では不安定なためローカル実行を推奨:

```bash
# ビルド
xmake build renode_test

# 対話的実行
xmake renode

# 自動テスト
xmake renode-test

# Robot Framework テスト
xmake robot
```

xmake.lua でのターゲット定義:

```lua
target("<libname>_renode")
    add_rules("embedded")
    set_values("embedded.mcu", "<MCU>")           -- 例: stm32f407vg
    set_values("embedded.linker_script", <LINKER>) -- リンカスクリプト変数
    add_deps("umi.embedded.full")
    add_files("lib/<libname>/test/test_<topic>.cc")
    add_includedirs("lib/<libname>/include")
target_end()
```

### テスト項目

| 項目 | 内容 |
|------|------|
| 起動シーケンス | ベクタテーブル → main |
| Kernel動作 | タスク切り替え、タイマー |
| UART出力 | テスト結果のログ出力 |
| リアルタイム制約 | デッドライン検証 |

---

## 4. CI/CD パイプライン

### GitHub Actions (`.github/workflows/ci.yml`)

#### CI で自動化されるもの

| ジョブ | 内容 | 実行コマンド |
|--------|------|-------------|
| **host-tests** | Host ユニットテスト (ubuntu, macos) | `xmake test` |
| **host-tests** | カバレッジ計測 (ubuntu, lcov) | `xmake f -m debug --coverage=y` |
| **wasm-tests** | WASM ビルド + Node.js テスト | `xmake build <name>_wasm` + `node ...mjs` |
| **arm-build** | ARM クロスコンパイル確認 | `xmake build <name>_renode` |

#### CI で自動化されないもの（ローカルのみ）

| 項目 | 理由 | 実行方法 |
|------|------|---------|
| **Renode エミュレーション** | Renode 環境のセットアップが不安定 | `xmake renode` / `xmake robot` |
| **実機テスト** | ハードウェアが必要 | `xmake flash-kernel` + pyOCD/GDB |
| **対話的デバッグ** | 人間の操作が必要 | `xmake renode` (対話モード) |

### 実行タイミング

- **push**: master, main, develop ブランチへのプッシュ時
- **PR**: 上記ブランチへのPR作成/更新時

---

## 5. テスト設計方針

### 仕様ベース vs 実装ベース

| アプローチ | 用途 | 例 |
|-----------|------|-----|
| **仕様ベース** | API契約のテスト | SpscQueue の FIFO順序保証 |
| **実装ベース** | 内部ロジック検証 | FPU save/restore の呼び出し回数 |

基本は仕様ベースでAPI契約を検証し、クリティカルな内部動作は実装ベースで補完します。

### 組み込み制約

全テストは組み込み制約下でビルド:

```
-fno-exceptions -fno-rtti
```

---

## 6. ローカル開発フロー

```bash
# 1. 開発中の高速テスト
xmake build test_<libname> && xmake run test_<libname>

# 2. 全テスト
xmake test

# 3. WASMビルド確認
xmake build <libname>_wasm

# 4. ARM ビルド確認
xmake build <libname>_renode
```

---

## 参考

- [CODING_STYLE.md](CODING_STYLE.md) - コーディングスタイル
- [LIBRARY_STRUCTURE.md](LIBRARY_STRUCTURE.md) - ライブラリ構造規約
- [DEBUG_GUIDE.md](DEBUG_GUIDE.md) - デバッグガイド

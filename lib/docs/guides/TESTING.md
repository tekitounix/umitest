# UMI Test Strategy

## 概要

UMIのテスト戦略は以下の環境をカバーします:

| 環境 | 目的 | ツール | CI 自動化 |
|------|------|--------|-----------|
| **Host** | ユニットテスト、高速イテレーション | xmake test | CI で自動実行 |
| **WASM** | ブラウザ/Node.js での動作確認 | Node.js, Emscripten | CI で自動実行 |
| **ARM ビルド** | クロスコンパイル検証 | arm-embedded | CI で自動実行 |
| **ARM 実行** | 実機相当のエミュレーション | Renode, Robot Framework | ローカルのみ |
| **実機** | ハードウェアでの動作確認 | pyOCD, GDB | ローカルのみ |

---

## 1. ユニットテスト (`tests/`, `lib/<name>/tests/`)

### テスト実行

```bash
# 全テスト実行（add_tests() 登録ターゲット）
xmake test

# 個別実行
xmake run test_<libname>

# パターンで絞り込み
xmake test "test_<libname>/*"
```

### テストフレームワーク

`lib/umitest` を使用（マクロゼロ、C++23 `std::source_location` ベース、ヘッダオンリー、組み込み対応）:

```cpp
#include <umitest/test.hh>
using namespace umi::test;

bool test_my_feature(TestContext& t) {
    t.assert_eq(result, expected);
    t.assert_near(actual, expected);
    return true;
}

int main() {
    Suite s("my_lib");
    s.section("My Feature");
    s.run("basic check", test_my_feature);

    // インラインチェックも可能
    s.check(condition);
    s.check_eq(a, b);
    s.check_near(actual, expected);

    return s.summary();
}
```

### テストターゲットの追加方法

`xmake test` の対象化は主に次の2通り:

- `host.test` ルールを使う
- `add_tests()` で明示登録する

典型的な host ユニットテスト:

```lua
-- lib/<libname>/tests/xmake.lua
target("test_<libname>")
    add_rules("host.test")          -- xmake test の対象になる
    add_tests("default")            -- 明示的にテストエントリ登録
    set_default(true)               -- xmake build でもビルド対象
    add_files("test_<topic>.cc")
    add_deps("umitest")             -- テストフレームワーク
    add_includedirs("$(projectdir)/lib/<libname>/include")
    add_cxxflags("-fno-exceptions", "-fno-rtti", {force = true})
target_end()
```

`host.test` ルールは以下を自動設定する:

- `set_kind("binary")`
- ホストプラットフォーム/アーキテクチャ
- `set_group("host/test")`（未指定時）
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
node lib/<libname>/tests/test_<topic>_wasm.mjs
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
    add_files("lib/<libname>/tests/test_<topic>_wasm.cc")
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

### Renode エミュレーション（主にローカル、CIは任意）

Renode を使った ARM エミュレーション。CI でも実行可能だが、環境依存があるため任意ジョブ扱いを推奨:

```bash
# 対話的実行
xmake emulator.run -t <libname>_renode

# 自動テスト（Robot）
xmake emulator.test -r <robot-file>
```

xmake.lua でのターゲット定義:

```lua
target("<libname>_renode")
    add_rules("embedded")
    set_values("embedded.mcu", "<MCU>")           -- 例: stm32f407vg
    set_values("embedded.linker_script", <LINKER>) -- リンカスクリプト変数
    add_deps("umi.embedded.full")
    add_files("lib/<libname>/tests/test_<topic>.cc")
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

### GitHub Actions（ライブラリ別ワークフロー）

各ライブラリは専用ワークフローを持つことを推奨。
`umibench` は `.github/workflows/umibench-ci.yml` を使用。

#### umibench で CI 自動化されるもの

| ジョブ | 内容 | 実行コマンド |
|--------|------|-------------|
| **host-tests** | Host ユニット + compile-fail (ubuntu, macos) | `xmake test "test_umibench/*"` + `xmake test "test_umibench_compile_fail/*"` |
| **wasm-tests** | WASM ビルド + Node.js 実行 | `xmake test "umibench_wasm/*"` |
| **arm-build** | ARM GCC クロスビルド確認 | `xmake build umibench_stm32f4_renode_gcc` |

#### 任意・手動ジョブ（umibench）

| 項目 | 理由 | 実行方法 |
|------|------|---------|
| **Renode エミュレーション** | エミュレータ/環境依存が強い | `workflow_dispatch` + `run_renode=true`（`renode-smoke`） |
| **実機テスト** | ハードウェアが必要 | `xmake flash -t <target>` + pyOCD/GDB |
| **対話的デバッグ** | 人間の操作が必要 | `xmake debugger -t <target>` |

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

組み込み向けライブラリでは、必要に応じて以下を適用:

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

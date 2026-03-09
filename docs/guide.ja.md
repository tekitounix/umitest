# テスト記述ガイド

umitest を使ったテストの書き方と xmake でのテストターゲット定義。

## xmake テストターゲット定義

### 最小パターン

```lua
target("test_<mylib>")
    set_kind("binary")
    set_default(false)
    add_files("test_*.cc")
    add_deps("<mylib>", "umitest")
    add_tests("default")
target_end()
```

- `set_kind("binary")` — テストは実行可能バイナリ
- `set_default(false)` — デフォルトビルド対象から除外
- `add_files("test_*.cc")` — glob でテストファイルを収集
- `add_deps` — テスト対象ライブラリと umitest のみ。不要な依存を追加しない
- `add_tests("default")` — xmake テスト検出に登録
- `target_end()` — ターゲットスコープの明示的終了

### compile_fail テスト付き

```lua
    for _, f in ipairs(os.files(path.join(os.scriptdir(), "compile_fail", "*.cc"))) do
        add_tests("fail_" .. path.basename(f),
            {files = path.join("compile_fail", path.filename(f)), build_should_fail = true})
    end
```

`{files = ..., build_should_fail = true}` はメインバイナリとは独立してコンパイルされる。
メインターゲットのビルドには影響しない（xmake 標準動作）。

### smoke テスト付き

```lua
    for _, f in ipairs(os.files(path.join(os.scriptdir(), "smoke", "*.cc"))) do
        add_tests("smoke_" .. path.basename(f),
            {files = path.join("smoke", path.filename(f)), build_should_pass = true})
    end
```

各ヘッダが単独でコンパイルできることを検証する。`build_should_pass = true` は独立コンパイル。

## テストファイル構造

```
lib/<name>/tests/
  xmake.lua           -- テストターゲット定義
  test_main.cc         -- 単一コンパイル単位（全 .hh を include）
  test_<feature>.hh    -- 機能別ヘッダオンリーテスト（inline run_<feature>_tests）
  compile_fail/        -- コンパイル失敗テスト（型制約の検証）
    <name>.cc
  smoke/               -- 単一ヘッダコンパイルテスト
    <name>.cc
```

### 命名規則

- テストファイル: `test_<feature>.hh`（ヘッダオンリー、test_main.cc から include）
- compile_fail: `compile_fail/<constraint_name>.cc`
- smoke: `smoke/<header_name>.cc`
- テスト名: `test_<lib>/default`, `test_<lib>/fail_<name>`, `test_<lib>/smoke_<name>`

## C++ テストコードの記述

### エントリポイント

```cpp
#include "test_feature.hh"

int main() {
    umi::test::Suite suite("test_<mylib>");
    <mylib>::test::run_feature_tests(suite);
    return suite.summary();
}
```

### テスト関数（ヘッダオンリー）

```cpp
#pragma once
#include <umitest/test.hh>

namespace <mylib>::test {

inline void run_feature_tests(umi::test::Suite& suite) {
    suite.section("feature");

    suite.run("basic", [](auto& t) {
        // ソフトチェック（失敗しても続行）
        t.eq(actual, expected);
        t.lt(a, b);
        t.near(actual, expected, tolerance);
        t.is_true(condition);

        // 致命的チェック（失敗時に false を返し早期リターン）
        if (!t.require_eq(actual, expected)) return;
    });
}

} // namespace <mylib>::test
```

### soft と fatal の使い分け

全チェックに soft 版と fatal 版（`require_*`）がある。使い分けの原則:

- **soft** (`eq`, `lt`, `is_true` 等) — 後続のチェックに影響しない独立した命題に使う。失敗しても残りのチェックが全て実行され、1 回の実行で全ての問題を把握できる
- **fatal** (`require_eq`, `require_true` 等) — 後続のチェックの前提条件に使う。失敗時に `false` を返すので `if (!...) return;` で早期リターンする

```cpp
suite.run("parse result", [](auto& t) {
    auto result = parse(input);
    // result が無効なら後続は無意味 → fatal
    if (!t.require_true(result.has_value())) return;
    // ここからは result が有効であることが保証されている → soft
    t.eq(result->name, "test");
    t.gt(result->size, 0);
});
```

前提条件が崩れた状態で soft check を続けるとクラッシュや無意味な失敗が連鎖する。fatal check で前提を守ることで、失敗メッセージが常に意味のある情報を持つ。

### note によるコンテキスト付加

ループ内でテストする場合、どのイテレーションで失敗したかを `note()` で残す:

```cpp
suite.run("all entries valid", [](auto& t) {
    for (int i = 0; i < count; ++i) {
        auto guard = t.note("entry index");
        t.is_true(entries[i].valid());
        t.gt(entries[i].size(), 0);
    }
});
```

`note()` は RAII ガードを返す。スコープを抜けるとノートが自動で除去される。失敗時のメッセージに `note:` 行として表示される。

### テスト名は仕様を語る

テスト名は「何を検証しているか」を端的に示す。実装の詳細ではなく振る舞いを記述する:

```cpp
// 良い例: 振る舞いが明確
suite.run("near rejects NaN", ...);
suite.run("require_eq returns false on mismatch", ...);

// 悪い例: 実装詳細や曖昧
suite.run("test1", ...);
suite.run("check function", ...);
```

### compile_fail テスト

**コンパイルに失敗すべき**コードを書く。型制約が不正な使用を正しく拒否することを検証する。

```cpp
// compile_fail/eq_incomparable.cc
#include <umitest/check.hh>

struct A {};
struct B {};

void should_fail() {
    umi::test::check_eq(A{}, B{});  // A と B は比較不可能 → コンパイルエラー
}
```

## 新テスト追加

1. `tests/test_<feature>.hh` を作成 — `test_main.cc` から include し `main()` に呼び出しを追加
2. compile-fail テストは `tests/compile_fail/` に `.cc` ファイルを追加 — `os.files()` glob で自動検出
3. smoke テストは `tests/smoke/` に `.cc` ファイルを追加 — `os.files()` glob で自動検出

xmake.lua の編集は不要。

## テスト実行

全コマンドはプロジェクトルートから実行:

```bash
xmake test                              # 全テスト
xmake test 'test_<mylib>/*'            # 特定ライブラリ
xmake test 'test_<mylib>/default'      # セルフテストのみ
xmake test 'test_<mylib>/fail_*'       # compile_fail のみ
```

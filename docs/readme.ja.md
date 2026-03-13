# umitest

[English](../README.md) | 日本語

C++23 向けのマクロ不要・ヘッダオンリーのテストフレームワーク。
`std::source_location` により、テスト関数を通常の C++ コードとして記述できる。

## 特徴

- **マクロ不要** — アサーションはすべて通常の関数呼び出し
- **ヘッダオンリー** — `#include <umitest/test.hh>` だけで使える
- **組み込み対応** — 例外・RTTI なしで動作。フレームワーク内部はヒープ未使用。Hosted stdlib が必要。stdio 以外のターゲットにはカスタム Reporter で対応。
- **Reporter 分離** — `BasicSuite<R>` でテストロジックと出力形式を分離
- **コンパイル時契約検証** — 型制約により不正な比較をビルド時に拒否
- **セルフテスト** — umitest 自身を使ってテストするため、退行が即座に検出可能

## クイックスタート

```cpp
#include <umitest/test.hh>
using namespace umi::test;

int main() {
    Suite suite("example");
    suite.run("add", [](auto& t) {
        t.eq(1 + 1, 2);
        t.is_true(true);
    });
    return suite.summary();
}
```

## インストール

外部プロジェクト:

```lua
add_repositories("synthernet https://github.com/tekitounix/synthernet-xmake-repo.git main")
add_requires("umitest")
add_packages("umitest")
```

## ビルドとテスト

```bash
xmake test 'test_umitest/*'
```

## Public API

エントリポイント: `include/umitest/test.hh`

コア型: `Suite` (`BasicSuite<StdioReporter>`), `TestContext`, `format_value()`

### TestContext チェック

| ソフトチェック | 致命的チェック | チェック内容 |
|------------|-------------|--------|
| `eq(a, b)` | `require_eq(a, b)` | `a == b` |
| `ne(a, b)` | `require_ne(a, b)` | `a != b` |
| `lt(a, b)` | `require_lt(a, b)` | `a < b` |
| `le(a, b)` | `require_le(a, b)` | `a <= b` |
| `gt(a, b)` | `require_gt(a, b)` | `a > b` |
| `ge(a, b)` | `require_ge(a, b)` | `a >= b` |
| `near(a, b, eps)` | `require_near(a, b, eps)` | `|a - b| <= eps` |
| `is_true(c)` | `require_true(c)` | 真偽値 true |
| `is_false(c)` | `require_false(c)` | 真偽値 false |

#### 例外チェック

例外が有効な場合のみ使用可能（`-fno-exceptions` ではこれらのメソッドは除外される）。

| ソフトチェック | 致命的チェック | チェック内容 |
|------------|-------------|--------|
| `throws<E>(fn)` | `require_throws<E>(fn)` | `fn()` が `E` をスロー |
| `throws(fn)` | `require_throws(fn)` | `fn()` が何らかの例外をスロー |
| `nothrow(fn)` | `require_nothrow(fn)` | `fn()` が例外をスローしない |

注: ジェネリックラムダ (`auto& t`) 内では `t.template throws<E>(fn)` と記述する。

#### 文字列チェック

| ソフトチェック | 致命的チェック | チェック内容 |
|------------|-------------|--------|
| `str_contains(s, sub)` | `require_str_contains(s, sub)` | `s` が `sub` を含む |
| `str_starts_with(s, pre)` | `require_str_starts_with(s, pre)` | `s` が `pre` で始まる |
| `str_ends_with(s, suf)` | `require_str_ends_with(s, suf)` | `s` が `suf` で終わる |

ソフトチェックは失敗しても続行する。致命的チェック (`require_*`) は失敗時に `false` を返し、早期リターンに使う:

```cpp
if (!t.require_true(ptr != nullptr)) return;
t.eq(ptr->value, 42);
```

#### コンテキストノート

`note(msg)` はコンテキスト文字列を RAII ガード付きスタックに積む。失敗時に診断出力にノートが表示される:

```cpp
auto guard = t.note("processing header");
t.eq(header.version, 2);
```

### フリー関数 (check.hh)

`static_assert` やカスタムロジック用の `constexpr bool` 関数:

`check_eq`, `check_ne`, `check_lt`, `check_le`, `check_gt`, `check_ge`, `check_true`, `check_false`,
`check_str_contains`, `check_str_starts_with`, `check_str_ends_with`

`check_near` を含むすべてのチェック関数は `constexpr` である。

### Reporter

`BasicSuite<R>` は `ReporterLike` コンセプトを満たす Reporter でパラメータ化:

- `StdioReporter` — ANSI カラー付き stdout 出力（`Suite` のデフォルト）
- `PlainReporter` — エスケープコードなしのプレーンテキスト
- `NullReporter` — サイレント、フレームワーク自体のテスト用

> **組み込みに関する注意:** `Suite` (= `BasicSuite<StdioReporter>`) および `PlainSuite` は `<cstdio>` に依存し、hosted stdlib が必要。フリースタンディングや stdio のないターゲットでは、カスタム Reporter を実装して `BasicSuite<YourReporter>` を使用すること。`NullReporter` を出発点にできる。

## テストの書き方

### テストファイル構造

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

### xmake テストターゲット定義

```lua
target("test_<mylib>")
    set_kind("binary")
    set_default(false)
    add_files("test_*.cc")
    add_deps("<mylib>", "umitest")
    add_tests("default")
target_end()
```

compile_fail テスト付き:

```lua
    for _, f in ipairs(os.files(path.join(os.scriptdir(), "compile_fail", "*.cc"))) do
        add_tests("fail_" .. path.basename(f),
            {files = path.join("compile_fail", path.filename(f)), build_should_fail = true})
    end
```

smoke テスト付き:

```lua
    for _, f in ipairs(os.files(path.join(os.scriptdir(), "smoke", "*.cc"))) do
        add_tests("smoke_" .. path.basename(f),
            {files = path.join("smoke", path.filename(f)), build_should_pass = true})
    end
```

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
        t.eq(actual, expected);
        if (!t.require_eq(actual, expected)) return;
    });
}

} // namespace <mylib>::test
```

### soft と fatal の使い分け

- **soft** (`eq`, `lt`, `is_true` 等) — 独立した命題。失敗しても残りのチェックが全て実行される
- **fatal** (`require_eq`, `require_true` 等) — 前提条件。失敗時に `false` を返すので `if (!...) return;` で早期リターン

```cpp
suite.run("parse result", [](auto& t) {
    auto result = parse(input);
    if (!t.require_true(result.has_value())) return;
    t.eq(result->name, "test");
    t.gt(result->size, 0);
});
```

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

### 命名規則

- テストファイル: `test_<feature>.hh`（ヘッダオンリー、test_main.cc から include）
- compile_fail: `compile_fail/<constraint_name>.cc`
- smoke: `smoke/<header_name>.cc`
- テスト名: `test_<lib>/default`, `test_<lib>/fail_<name>`, `test_<lib>/smoke_<name>`

### 新テスト追加

1. `tests/test_<feature>.hh` を作成 — `test_main.cc` から include し `main()` に呼び出しを追加
2. compile-fail: `tests/compile_fail/` に `.cc` ファイルを追加 — glob で自動検出
3. smoke: `tests/smoke/` に `.cc` ファイルを追加 — glob で自動検出

xmake.lua の編集は不要。

### テスト実行

```bash
xmake test                              # 全テスト
xmake test 'test_<mylib>/*'            # 特定ライブラリ
xmake test 'test_<mylib>/default'      # セルフテストのみ
xmake test 'test_<mylib>/fail_*'       # compile_fail のみ
```

## サンプル

- [`examples/minimal.cc`](../examples/minimal.cc) — 最短の完全なテスト
- [`examples/assertions.cc`](../examples/assertions.cc) — 全アサーションメソッド
- [`examples/check_style.cc`](../examples/check_style.cc) — セクションと構造化テスト
- [`examples/constexpr.cc`](../examples/constexpr.cc) — `static_assert` によるコンパイル時チェック

## ライセンス

MIT — [LICENSE](../LICENSE) を参照

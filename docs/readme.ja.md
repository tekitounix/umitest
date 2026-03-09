# umitest

[English](../README.md)

C++23 向けのマクロ不要・ヘッダオンリーのテストフレームワーク。
`std::source_location` により、テスト関数を通常の C++ コードとして記述できる。

## 特徴

- マクロ不要 — アサーションはすべて通常の関数呼び出し
- ヘッダオンリー — `#include <umitest/test.hh>` だけで使える
- 組み込み対応 — 例外・ヒープ・RTTI 不要
- Reporter 分離 — `BasicSuite<R>` でテストロジックと出力形式を分離
- コンパイル時契約検証 — 型制約により不正な比較をビルド時に拒否
- セルフテスト — umitest 自身を使ってテストするため、退行が即座に検出可能

## クイックスタート

```cpp
#include <umitest/test.hh>
using namespace umi::test;

int main() {
    Suite s("example");
    s.run("add", [](auto& t) {
        t.eq(1 + 1, 2);
        t.is_true(true);
    });
    return s.summary();
}
```

## ビルドとテスト

プロジェクトルートから実行する:

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
| `near(a, b, eps)` | `require_near(a, b, eps)` | `\|a - b\| <= eps` |
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

`check_near` は例外で、`std::abs` と `std::isnan` が現行規格で `constexpr` でないため `constexpr` ではない。

### Reporter

`BasicSuite<R>` は `ReporterLike` コンセプトを満たす Reporter でパラメータ化:

- `StdioReporter` — ANSI カラー付き stdout 出力（`Suite` のデフォルト）
- `PlainReporter` — エスケープコードなしのプレーンテキスト
- `NullReporter` — サイレント、フレームワーク自体のテスト用

## 設計判断

### 絶対要件

1. **マクロ不要** — `std::source_location::current()` をデフォルト引数として使用。`ASSERT_EQ` や `TEST_CASE` は存在しない。
2. **ヘッダオンリー** — 静的ライブラリなし、リンク時登録なし、コード生成なし。
3. **ヒープアロケーションなし** — 全内部状態はスタックまたは静的ストレージ。ベアメタル互換。
4. **例外なし** — アサーションはスローしない。TestContext が失敗状態を内部追跡。

### 依存境界

レイヤリングは厳格:

1. `umitest` は C++23 標準ライブラリヘッダーのみに依存。
2. 他の umi ライブラリへの依存なし。
3. 他の umi ライブラリがテストに `umitest` を使用（テスト時のみ）。

### 使用方法

umitest は**ライブラリ**であり、プロジェクトではない。`set_project()` は持たず、単体ビルドは存在しない。

UMI モノレポ内:

```lua
add_deps("umitest")
```

外部プロジェクト:

```lua
add_requires("umitest")
add_packages("umitest")
```

## Examples

- [`examples/minimal.cc`](examples/minimal.cc) — 最短の完全なテスト
- [`examples/assertions.cc`](examples/assertions.cc) — 全アサーションメソッド
- [`examples/check_style.cc`](examples/check_style.cc) — セクションと構造化テスト
- [`examples/constexpr.cc`](examples/constexpr.cc) — `static_assert` によるコンパイル時チェック

## ライセンス

MIT — [LICENSE](../../../LICENSE) を参照

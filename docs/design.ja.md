# umitest 設計

[English](design.md) | 日本語

内部設計の判断とアーキテクチャ上の制約。

---

## 1. 絶対要件

1. **マクロ不要** — `std::source_location::current()` をデフォルト引数として使用。`ASSERT_EQ` や `TEST_CASE` は存在しない。
2. **ヘッダオンリー** — 静的ライブラリなし、リンク時登録なし、コード生成なし。
3. **ヒープアロケーションなし** — 全内部状態はスタックまたは静的ストレージ。ベアメタル互換。
4. **アサーションは例外をスローしない** — TestContext が失敗状態を内部追跡。

---

## 2. 依存境界

レイヤリングは厳格:

1. `umitest` は C++23 標準ライブラリヘッダーのみに依存。
2. 他の umi ライブラリへの依存なし。
3. 他の umi ライブラリがテストに `umitest` を使用（テスト時のみ）。

---

## 3. ソースロケーションキャプチャ

全チェックメソッドは最後のパラメータとして `std::source_location loc = std::source_location::current()` を受け取る。
コンパイラがコールサイトのファイルと行番号を自動で埋める — マクロ展開不要。

これが `ASSERT_EQ(a, b)` マクロを不要にする根本メカニズム。
従来のフレームワークではマクロが展開時に `__FILE__` と `__LINE__` をキャプチャする。
umitest はデフォルト引数評価で同じことを実現する（C++20/23 の標準動作）。

---

## 4. TestContext アーキテクチャ

### 4.1 統一テンプレートパターン

全チェックメソッドは `<bool Fatal>` でパラメータ化されたプライベートテンプレートに委譲する:

```cpp
template <bool Fatal>
bool bool_check(bool passed, const char* kind, std::source_location loc);

template <bool Fatal, typename A, typename B>
bool compare_check(bool passed, const A& a, const char* kind, const B& b, std::source_location loc);
```

公開 API は薄い転送レイヤ:

- `eq(a, b, loc)` → `compare_check<false>(...)`
- `require_eq(a, b, loc)` → `compare_check<true>(...)`

ソフト/致命的バリアントのコード重複を排除。`Fatal` フラグは `FailureView::is_fatal` を通じて Reporter 側のフォーマットに反映される。

### 4.2 失敗コールバック

TestContext は Reporter を知らない。コンストラクタで関数ポインタを受け取る:

```cpp
using FailCallback = void (*)(const FailureView& failure, void* ctx);
```

`BasicSuite::run()` が Reporter に転送するラムダを注入する:

```cpp
TestContext ctx(test_name,
    [](const FailureView& fv, void* p) { static_cast<BasicSuite*>(p)->reporter.report_failure(fv); },
    this);
```

TestContext を Reporter テンプレートパラメータから切り離す設計。TestContext は具象クラス（テンプレートではない）で、バイナリサイズが小さくコンパイルも速い。

### 4.3 ヒープ不使用・固定容量

- 失敗メッセージは `std::array<char, 256>` をスタック上に確保 — `std::string` なし、アロケーションなし。
- ノートスタックは `std::array<const char*, 4>` + 深度カウンタ。
- `BoundedWriter` はバッファサイズ 0 を含む全サイズで NUL 終端とトランケーション安全性を保証。

### 4.4 致命的チェックの戻り値

致命的チェック (`require_*`) は `[[nodiscard]]` で `bool` を返す。想定パターン:

```cpp
if (!t.require_true(ptr != nullptr)) return;
t.eq(ptr->value, 42);
```

意図的な設計: umitest は例外、`setjmp`/`longjmp`、その他の非ローカル制御フローを使わない。テスト関数が早期リターンの責任を持つ。ベアメタル互換性と制御フローの明示性を維持する。

---

## 5. Reporter 抽象化

### 5.1 ReporterLike コンセプト

```cpp
template <typename R>
concept ReporterLike =
    std::move_constructible<R> && requires(R r, const char* s, const FailureView& fv, const SummaryView& sv) {
        r.section(s);
        r.test_begin(s);
        r.test_pass(s);
        r.test_fail(s);
        r.report_failure(fv);
        r.summary(sv);
    };
```

Reporter はコンセプトによるダックタイピング — 基底クラスなし、vtable なし。`BasicSuite<R>` が唯一のテンプレート。その他の型 (TestContext, FailureView, SummaryView) はすべて具象型。

### 5.2 FailureView / SummaryView

Reporter は文字列ではなく構造化ビューを受け取る。フォーマットとロジックを分離:

- `FailureView`: テスト名、ソースロケーション、致命的フラグ、チェック種別、LHS/RHS 値、追加情報、アクティブノート。
- `SummaryView`: Suite 名、合格/失敗数、アサーション統計。

ビューは Reporter 呼び出し中のみ有効 — 保持が必要な場合はコピーする必要がある。

### 5.3 組み込み Reporter

| Reporter | 用途 |
|----------|------|
| `StdioReporter` | ANSI カラー付き stdout 出力（`<cstdio>` 使用、デフォルト `Suite`） |
| `PlainReporter` | エスケープコードなしのプレーンテキスト |
| `NullReporter` | サイレント — フレームワーク自身のテスト用 |

各 Reporter はクラス定義直後に `static_assert(ReporterLike<...>)` を持つ。

---

## 6. チェック関数レイヤ (check.hh)

### 6.1 二層設計

チェックは二層に分離:

1. **フリー関数** (`check_eq`, `check_lt` 等) — 純粋な `constexpr bool` 関数。副作用なし、状態なし。`static_assert` で使用可能。
2. **TestContext メソッド** (`eq`, `lt` 等) — フリー関数を呼び出し、失敗時にフォーマットと報告を行う。

この分離により、コンパイル時契約検証とランタイムテストがまったく同じ比較ロジックを共有する。

### 6.2 型制約

| コンセプト | 目的 |
|-----------|------|
| `std::equality_comparable_with<A, B>` | `eq`/`ne` のゲート |
| `OrderableNonPointer<A, B>` | `lt`/`le`/`gt`/`ge` のゲート — ポインタを拒否 |
| `std::floating_point<T>` | `near` のゲート |
| `std::invocable<F>` | `throws`/`nothrow` のゲート |

**ポインタ排除**: `OrderableNonPointer` は `!std::is_pointer_v<std::decay_t<T>>` でポインタ型を明示的に拒否。C++ ではポインタの順序比較は処理系定義であり、アドレスの `<` 比較はほぼ確実にバグ。

**char ポインタのルーティング**: `eq(const char*, const char*)` は非テンプレートオーバーロードで、内容比較（`std::string_view` 経由）を行う。テンプレートオーバーロードは `excluded_char_pointer_v` で char ポインタペアを除外し、曖昧な解決を防止。

### 6.3 符号混合整数の安全性

両オペランドが `SafeCmpInteger`（integral、非 bool、非 char）を満たす場合、`std::cmp_equal`、`std::cmp_less` 等を使用。符号なし/符号ありの比較を未定義動作なしで処理:

```cpp
t.eq(-1, 0u);  // std::cmp_equal 使用 → false（正しい）
               // safe cmp なし: -1 == 0u がラップ → true（誤り）
```

---

## 7. 値フォーマット (format.hh)

### 7.1 BoundedWriter

全フォーマットは `BoundedWriter` を経由する。安全なバッファライタで以下を保証:

- `size == 0`: 書き込みなし、メモリアクセスなし。
- `size == 1`: NUL ターミネータのみ書き込み。
- `size >= 2`: 通常動作、NUL 常時維持。
- トランケーションは UB を起こさない。`truncated()` で検出可能。

`BoundedWriter` は `constexpr` — 整数・文字列フォーマットはコンパイル時に動作。

### 7.2 format_value ディスパッチ

`format_value<T>` は `if constexpr` チェーン（オーバーロード解決や ADL ではなく）でフォーマットを選択:

| 型 | フォーマット |
|----|-------------|
| `bool` | `"true"` / `"false"` |
| `nullptr_t` | `"nullptr"` |
| `char` | `'c' (N)` — エスケープ文字 + 数値 |
| `std::string` / `std::string_view` | `"クォートしエスケープ"` |
| `const char*` | `"クォートしエスケープ"` or `"(null)"` |
| 符号なし整数 | 10進数 |
| 符号あり整数 | 10進数 |
| 浮動小数点 | `std::to_chars` による最短ラウンドトリップ |
| enum | 基底整数値 |
| ポインタ | `0xhex` |
| 不明 | `"(?)"` |

浮動小数点フォーマットは `std::to_chars` で最短ラウンドトリップ表現を使用。NaN、無限大、負のゼロを特別処理。整数値には `.0` を付加して明確化。

### 7.3 stdio 非依存

`format_value` は `printf`、`snprintf`、その他の `<cstdio>` 関数を使用しない。浮動小数点には `std::to_chars`（`<charconv>`）のみ使用。フリースタンディング環境でも使用可能。

`StdioReporter` が `<cstdio>` を使用する唯一のコンポーネントで、リーフ — 何も依存しない。

---

## 8. 例外ポータビリティ

例外関連メソッド (`throws`, `nothrow`, `require_throws`, `require_nothrow`) は条件付きコンパイル:

```cpp
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
```

例外無効時 (`-fno-exceptions`) はこれらのメソッドが存在しない — 呼び出しはランタイムエラーではなくコンパイルエラー。意図的な設計: `fno-exceptions` ターゲット（Cortex-M、WASM）では例外テストは無意味。

---

## 9. 現在のレイアウト

```text
lib/umitest/
├── README.md
├── LICENSE
├── xmake.lua
│
├── docs/
│   ├── readme.ja.md
│   ├── design.md / design.ja.md
│
├── include/umitest/
│   ├── test.hh            # アンブレラヘッダ（Suite, PlainSuite エイリアス）
│   ├── suite.hh           # BasicSuite<R> テンプレート
│   ├── context.hh         # TestContext（全チェックメソッド）
│   ├── check.hh           # constexpr bool フリー関数 + コンセプト
│   ├── format.hh          # BoundedWriter + format_value
│   ├── failure.hh         # FailureView, SummaryView, op_for_kind
│   ├── reporter.hh        # ReporterLike コンセプト
│   └── reporters/
│       ├── stdio.hh       # StdioReporter（ANSI カラー）
│       ├── plain.hh       # PlainReporter（エスケープコードなし）
│       └── null.hh        # NullReporter（サイレント）
│
├── tests/
│   ├── xmake.lua
│   ├── test_main.cc
│   ├── test_check.hh           # check_* フリー関数テスト
│   ├── test_context.hh         # TestContext ソフト/フェイタルチェックテスト
│   ├── test_exception.hh       # throws/nothrow テスト
│   ├── test_format.hh          # format_value テスト
│   ├── test_reporter.hh        # ReporterLike コンセプト + FailureView/SummaryView テスト
│   ├── test_string.hh          # str_contains/starts_with/ends_with テスト
│   ├── test_suite.hh           # Suite/BasicSuite 統合テスト
│   ├── smoke/
│   │   └── standalone.cc
│   └── compile_fail/           # 23 件のコンパイル失敗テスト
│       ├── .clangd
│       └── *.cc
│
└── examples/
    ├── minimal.cc
    ├── assertions.cc
    ├── check_style.cc
    └── constexpr.cc
```

---

## 10. 設計原則

1. ゼロコスト — 仮想ディスパッチなし、RTTI なし、ヒープアロケーションなし。
2. コンパイル時安全性 — 型制約が不正な比較をランタイム前に拒否。
3. 組み込み最優先 — Cortex-M で `-fno-exceptions -fno-rtti` でも動作。
4. 単一ソースオブトゥルース — `check_*` 関数は `static_assert` とランタイムチェックの両方で使用。
5. Reporter 非依存 — テストロジックは出力形式から分離。

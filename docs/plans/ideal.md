# umitest 真の理想設計 v2

**ステータス**: draft
**前提**: 後方互換不要。現行実装を完全に無視し、あるべき姿を定義する。

---

## 0. テストとは何か

テストとは**命題の真偽を判定し、結果を報告する行為**である。

```
命題: 1 + 1 == 2
判定: true
報告: PASS
```

テストフレームワークの責務は以下の 3 つに分解される:

| 責務 | 定義 |
|------|------|
| **判定** | 式を評価し true/false を得る。失敗時は診断情報を生成する |
| **集計** | テストケース単位で pass/fail を数える |
| **報告** | 集計結果と診断情報を人間または CI に伝える |

これ以上でも以下でもない。テスト名、セクション、色は**報告の装飾**であり、本質ではない。

### テストが「完了」したと言える条件

1. 全命題が判定され、pass/fail が確定している
2. fail した命題について、原因特定に十分な情報が得られている
3. 全体の成否がプロセス終了コード（0/1）に反映されている

---

## 1. 現行設計の根本問題

### 1.1 判定ロジックの重複

`BasicSuite::check_eq` と `TestContext::check_eq` は同一の判定（`detail::safe_eq` → `build_fail_message`）を行い、集計方法だけが異なる:

- Suite: `passed++` / `failed++`（カウンタ直接操作）
- Context: `failed = true`（フラグ設定）

これは**判定という単一の責務が 2 箇所にコピーされている**ことを意味する。

根本原因: Suite の「インライン check」と run() 内の「構造化 check」を別系統として設計したため。

### 1.2 run() の戻り値型が未制約

計画 D10 は「ラムダは void を返す」と規定するが、`template <typename F>` は戻り値型を制約していない。全テストが `return true;` を書いており、D10 は未達成。

### 1.3 TestContext の public API 過剰

`clear_failed()` と `has_failed()` が public。テストコードから状態をリセットでき、テスト意味論を破壊できる。

### 1.4 mark_failed() デッドコード

`TestContext::mark_failed()` はどこからも呼ばれない。

### 1.5 Legacy API 残存

`format_value(char*, size_t, T&)` — BoundedWriter 統一後に不要な旧 API。

### 1.6 ANSI カラーが format.hh に存在

色定数はフォーマッタの責務ではなく、reporter の責務。format.hh が reporter の実装詳細を知っている。

---

## 2. 設計原則

### P1: 一つの責務は一つの場所に

判定ロジックは 1 箇所。集計方法は呼び出し側が決める。

### P2: 外部依存ゼロ

C++23 標準ライブラリのみ。xmake 標準機能のみ。カスタムパッケージ、プラグイン、ルール不要。

### P3: 全てが constexpr 検証可能

check 関数（`check_eq`, `check_true` 等）は constexpr bool 純粋関数。
`static_assert(check_eq(1 + 1, 2))` でコンパイル時に正しさを証明できる。
C 文字列比較も `std::string_view` ベースで constexpr（`static_assert(check_eq("hello", "hello"))`）。
BoundedWriter、`format_value`（整数・bool・文字列等のリテラル型）、比較ヘルパーも constexpr 対応。

**非 constexpr の例外**:
- `check_near`: `std::abs`, `std::isnan` が constexpr でない（IEEE 754 浮動小数点の制約）
- `format_near_extra`: `std::snprintf` が constexpr でない（浮動小数点診断の制約）
- `format_value` の浮動小数点特殊化: 同上

これらは言語・標準ライブラリの制約であり、設計上の妥協ではない。

### P4: 最小 API

ユーザーが覚える型は 2 つ: `Suite` と `TestContext`。

- Suite: `section()`, `run()`, `summary()` の 3 メソッド
- TestContext:
  - soft check: `eq`, `ne`, `lt`, `le`, `gt`, `ge`, `near`, `is_true`, `is_false` の 9 メソッド（失敗しても継続）
  - fatal check: soft check と 1:1 対応する `require_*` 系（`require_eq`, `require_ne`, `require_lt`, `require_le`, `require_gt`, `require_ge`, `require_near`, `require_true`, `require_false`）。失敗時は呼び出し側が early return
  - note: `note("context")` で文脈情報をスコープベースで蓄積

### P5: ゼロオーバーヘッド

vtable なし。ヒープ確保なし。例外なし。embedded で直接使える。

---

## 3. アーキテクチャ

### 3.1 レイヤ構造

```
Layer 0: check（判定のみ）— constexpr bool 純粋関数
  └── 比較ヘルパー (safe_eq, safe_lt, ... — mixed-sign 安全: std::cmp_*)

Layer 0.5: format（値のフォーマット）— 失敗パスでのみ使用
  ├── BoundedWriter
  └── フォーマッタ (format_value)

Layer 1: 集計 + 診断統合
  ├── FailureView / SummaryView（構造化された失敗・集計情報）
  ├── TestContext（soft/fatal check, note stack, 失敗時に FailureView 構築）
  └── BasicSuite（テストケースカウンタ + assertion 補助統計、TestContext 生成）

Layer 2: 報告
  ├── ReporterLike concept（FailureView / SummaryView を受け取る）
  ├── StdioReporter / PlainReporter
  └── NullReporter
```

**核心**: Layer 0 の check 関数は**集計も報告も診断も知らない**。判定のみを行い、bool を返す。Layer 1 (TestContext) が失敗時のみ Layer 0.5 (format) で値をフォーマットし、FailureView を構築して reporter に渡す。reporter は構造化データを自由にレンダリングする。成功パスでは Layer 0 の bool 返却だけで完結する。

### 3.2 ヘッダファイル構成

```
umitest/include/umitest/
├── test.hh              # umbrella
├── suite.hh             # BasicSuite<R> (Suite alias は test.hh で定義)
├── context.hh           # TestContext (NoteGuard は nested class)
├── check.hh             # check 関数群（判定のみ、constexpr bool）
├── failure.hh           # FailureView, SummaryView
├── format.hh            # BoundedWriter, format_value（色定数なし）
├── reporter.hh          # ReporterLike concept
└── reporters/
    ├── stdio.hh         # StdioReporter（ANSI色付き）
    ├── plain.hh         # PlainReporter（色なし）
    └── null.hh          # NullReporter
```

### 3.3 include 関係

```
test.hh → suite.hh, reporters/stdio.hh, reporters/plain.hh (convenience: Suite / PlainSuite alias 定義)
suite.hh → context.hh, reporter.hh, <concepts>, <utility>
context.hh → check.hh, format.hh, failure.hh, <algorithm>, <array>, <span>
check.hh → <cmath>, <concepts>, <string_view>, <type_traits>, <utility> (比較ヘルパー + is_char_pointer_v + check_near + std::cmp_*)
failure.hh → <source_location>, <span>
format.hh → <array>, <cmath>, <concepts>, <cstddef>, <cstdint>, <cstdio>, <string>, <string_view>, <type_traits>
reporter.hh → failure.hh
reporters/stdio.hh → reporter.hh, <cstdio>, <string_view> (op_for_kind 内部実装)
reporters/plain.hh → reporter.hh, <cstdio>, <string_view> (op_for_kind 内部実装)
reporters/null.hh → reporter.hh
```

**注目**: `suite.hh` は reporter の具体型に依存しない。`BasicSuite<R>` のみを提供。
`using Suite = BasicSuite<StdioReporter>;` は `test.hh`（convenience header）で定義。

**`<cstdio>` の依存経路**: `suite.hh` → `context.hh` → `format.hh` → `<cstdio>` であるため、
`suite.hh` を include すると `<cstdio>` は常に入る（`format_value` の浮動小数点特殊化が `snprintf` を使用）。
ただし embedded で `BasicSuite<MyReporter>` を使う場合、**reporter 側が `<cstdio>` に依存しない**。
`StdioReporter` / `PlainReporter` を避ければ、reporter の出力先を UART 等に自由に差し替えられる（§8.2 embedded 設計指針参照）。
`format.hh` 由来の `<cstdio>` は `snprintf` のためだけに必要であり、
テスト成功パスでは呼ばれない（失敗パスの診断生成のみ）。

---

## 4. check.hh — 判定の単一実装

### 4.1 設計方針: 判定と診断の分離

**問題**: 以前の設計では CheckResult が 256B のメッセージバッファを常に内包していた。
成功パス（テストの大半）でも 256B のスタック確保+搬送コストが発生し、P5（ゼロオーバーヘッド）に反する。

**解決**: **check 関数は `bool` のみ返す。診断メッセージの生成は失敗時に呼び出し側が行う。**

```
成功パス: check_eq(a, b) → true    コスト: 比較のみ
失敗パス: check_eq(a, b) → false → 呼び出し側が format_value() で診断情報を生成
```

これにより:
- 成功パスのコストが比較演算のみ（P5 完全達成）
- check 関数は constexpr 対応可能（P3 達成）
- 256B バッファは失敗パスでのみスタックに確保される

### 4.2 check 関数群

**判定のみ。メッセージ生成も集計も報告もしない。**

#### C 文字列の比較規約

`detail::safe_eq` は `const char*` を**内容比較**する。
ポインタの一致ではなく、指す文字列の内容が等しいかを判定する。

**前提条件 (precondition)**: 引数は `nullptr` または **null 終端された C 文字列**へのポインタでなければならない。
null 終端されていないバッファを渡すと `std::string_view(ptr)` の構築で**未定義動作**となる。
この契約は `check_eq`, `check_ne` の `const char*` オーバーロード、および `TestContext::eq` 等に
`const char*` を渡す全てのケースに適用される。`format_value(buf, size, const char* value)` も同様。

**nullptr と std::string/string_view の混在**: `const char* p = nullptr; check_eq(std::string{"hello"}, p)` は
テンプレート経路に入る（`equality_comparable_with<std::string, const char*>` 成立、片側のみ char なので
`excluded_char_pointer_v` 不成立）。`safe_eq` が `const char*` 側の nullptr を検出して安全に `false` を返す。
`std::string::operator==(nullptr)` の UB に到達しない。
**注意**: `check_eq(std::string{"hello"}, nullptr)` はキャストなしの `nullptr`（型は `std::nullptr_t`）であり、
`equality_comparable_with<std::string, std::nullptr_t>` が不成立のため no matching function になる。
`const char*` に明示キャストするか、`const char*` 変数を経由する必要がある。
`check_eq((const char*)nullptr, (const char*)nullptr)` は non-template 経路で `safe_eq(const char*, const char*)`
が処理する（both nullptr → true）。
**nullptr_t と const char\* の混在**: `const char* p = nullptr; check_eq(nullptr, p)` はテンプレート経路に入る
（`is_char_pointer_v<std::nullptr_t>` = false なので `excluded_char_pointer_v` は不成立）。
`safe_eq` の char pointer ブランチで `is_null_pointer_v<A>` = true なので nullptr guard をスキップし、
`nullptr == p` で well-defined に比較される（true）。

```cpp
/// @brief Detect char-related types (char*, const char*, char[N], const char[N], char8_t* 系).
/// decay_t を適用してから判定することで、配列型も捕捉する。
/// char8_t* も含める: テンプレートから除外するために使用。(D42 改訂)
template <typename T>
constexpr bool is_char_pointer_v =
    std::is_same_v<std::decay_t<T>, char*> ||
    std::is_same_v<std::decay_t<T>, const char*> ||
    std::is_same_v<std::decay_t<T>, char8_t*> ||
    std::is_same_v<std::decay_t<T>, const char8_t*>;
// volatile char* は対象外。volatile char* → const char* の暗黙変換は不可能であり、
// non-template overload にマッチしない。そもそも volatile char* のテストは非現実的。

/// @brief Detect char8_t pointer types specifically.
/// char8_t* は未対応であり、片側でも含まれればコンパイルエラーにする (D42)。
template <typename T>
constexpr bool is_char8_pointer_v =
    std::is_same_v<std::decay_t<T>, char8_t*> ||
    std::is_same_v<std::decay_t<T>, const char8_t*>;

/// @brief Template exclusion predicate for check_eq/ne/lt etc.
/// テンプレートから除外するケース:
/// 1. 両側がともに char 関連型 → const char* non-template overload に寄せる
/// 2. 片側でも char8_t* → 未対応のためコンパイルエラー (D42)
///
/// char* / const char* が片側のみの場合（例: std::string vs const char*）は
/// テンプレートを通す。safe_eq が nullptr guard で UB を回避する。
///
/// 両側 char を除外する理由:
/// - char* vs char*: テンプレート版は a == b でアドレス比較 → 危険
/// - char[N] vs char[N]: 同上（const ref で decay しない）
///
/// 片側のみ char を除外しない理由:
/// - std::string vs const char*: safe_eq 経由で内容比較。片側除外すると no matching function
template <typename A, typename B>
constexpr bool excluded_char_pointer_v =
    (is_char_pointer_v<A> && is_char_pointer_v<B>)
    || is_char8_pointer_v<A> || is_char8_pointer_v<B>;

/// @brief Compare values for equality. Mixed-sign integer safe.
/// Uses std::cmp_equal for non-bool integral types to avoid signed/unsigned mismatch.
/// e.g. safe_eq(-1, 4294967295u) → false (mathematically correct).
/// bool is excluded: std::cmp_equal rejects bool ([utility.intcmp]/1).
/// bool の == は well-defined (true==true, false==false) なので else 分岐で正しく処理される。
template <typename A, typename B>
constexpr bool safe_eq(const A& a, const B& b) {
    if constexpr (std::integral<A> && !std::same_as<A, bool>
               && std::integral<B> && !std::same_as<B, bool>) {
        return std::cmp_equal(a, b);  // <utility>: C++20 safe integer comparison
    } else if constexpr (is_char_pointer_v<A> || is_char_pointer_v<B>) {
        // 片側が char 関連型の混在比較（例: std::string vs const char*、nullptr_t vs const char*）。
        // 両側 char の場合は excluded_char_pointer_v で check_eq テンプレートから除外済みなので、
        // ここに到達するのは片側のみ char のケース。
        //
        // nullptr guard は「相手側の operator==(nullptr) が UB になる型」のときだけ必要。
        // - std::string::operator==(nullptr) → UB（null dereference）
        // - std::string_view::operator==(nullptr) → 同上
        // - std::nullptr_t == const char* → well-defined（ポインタと nullptr の比較）
        // - const char* == const char* → ここには来ない（both で除外済み）
        //
        // したがって、char pointer 側が nullptr のとき:
        // - 相手が std::is_null_pointer_v → guard 不要（a == b で安全に比較可能）
        // - 相手がクラス型等 → false を返す（UB 回避）
        if constexpr (is_char_pointer_v<A>) {
            if constexpr (!std::is_null_pointer_v<B>) {
                if (a == nullptr) return false;
            }
        }
        if constexpr (is_char_pointer_v<B>) {
            if constexpr (!std::is_null_pointer_v<A>) {
                if (b == nullptr) return false;
            }
        }
        return a == b;
    } else {
        return a == b;
    }
}

/// @brief Compare C strings by content. constexpr in C++23.
/// @pre a and b are either nullptr or pointers to null-terminated strings.
///      Passing a non-null-terminated buffer is undefined behavior.
///
/// アドレス比較 (a == b) を shortcut に使ってはならない。
/// 定数式において、異なるオブジェクトへのポインタの等価比較は未規定 ([expr.eq]/2)。
/// nullptr との比較のみ定数式で well-defined。
constexpr bool safe_eq(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) return a == b;
    return std::string_view(a) == std::string_view(b);
}
```

`detail::format_value` も `const char*` を文字列として出力する（`"hello"` 形式）。

この規約により `ctx.eq("hello", ptr)` は `ptr` が `const char*` でも `char*` でも内容比較になる。
テンプレート版は `detail::is_char_pointer_v` で char pointer 型を除外するため、
`char*` は non-template overload に暗黙変換 (`char*` → `const char*`) で到達する。
`char*` / `const char*` 以外のポインタ型は `==` 演算子（アドレス比較）を使用する。

**なぜ `std::strcmp` ではなく `std::string_view`**: `std::strcmp` は constexpr でないため
`static_assert(check_eq("hello", "hello"))` が不可能だった。C++23 では `std::string_view` の
比較演算子が constexpr であるため、これに置き換えることで P3（全てが constexpr 検証可能）を
C 文字列比較でも達成する。

**char 関連型の除外と const char\* オーバーロード**:

テンプレート版 `check_eq` は `excluded_char_pointer_v<A, B>` で以下の場合に除外する:
1. **両側がともに char 関連型** → `const char*` non-template overload に寄せる
2. **片側でも char8_t\*** → 未対応のためコンパイルエラー (D42)

片側だけ char 関連型（char\* / const char\* のみ）の場合（例: `std::string` vs `const char*`）は
テンプレートを通す。

1. **`excluded_char_pointer_v` によるテンプレート除外**: 両方が `char*`/`const char*`/`char[N]`/`const char[N]`
   の場合、または片側でも `char8_t*`/`const char8_t*` の場合、テンプレート版の制約で拒否される
2. **`const char*` 非テンプレートオーバーロード**: 両側 char で除外された場合、
   暗黙変換・decay で non-template overload にマッチし、内容比較になる
3. **char8_t\* で除外された場合**: non-template overload にもマッチしない → コンパイルエラー (D42)
4. **片側のみ char 関連（char\*/const char\* のみ）**: `check_eq(std::string("x"), ptr)` はテンプレートを通り、
   `safe_eq` 内で内容比較。ptr が nullptr の場合は `safe_eq` が早期に false を返す（UB 回避）

オーバーロード解決のパス:
- `check_eq(charptr, charptr)`: 両方 char → テンプレート除外 → non-template → 内容比較
- `check_eq(arr, arr)`: 両方 char[N] → テンプレート除外 → decay → non-template → 内容比較
- `check_eq("hello", "hello")`: 両方 const char[N] → テンプレート除外 → decay → non-template → 内容比較
- `check_eq(string, ptr)`: 片方のみ char → テンプレート → safe_eq → 内容比較
- `check_eq(string, "hello")`: 同上（リテラル側は `const char[N]` だが片側なので通る）
- `check_eq(string, nullptr)`: `equality_comparable_with<string, nullptr_t>` 不成立 → no matching function
- `check_eq(nullptr, pnull)`: nullptr_t vs const char\* → テンプレート → safe_eq → nullptr guard スキップ → true
- `check_eq(u8ptr, nullptr)`: 片側 char8_t\* → テンプレート除外 → non-template にもマッチしない → コンパイルエラー
- `check_eq(u8string, u8ptr)`: 片側 char8_t\* → 同上 → コンパイルエラー
- `static_assert(check_eq("a", "a"))`: non-template → constexpr 内容比較（`string_view`）

```cpp
namespace umi::test {

/// @brief Check boolean condition. constexpr.
constexpr bool check_true(bool cond) { return cond; }
constexpr bool check_false(bool cond) { return !cond; }

/// @brief Check equality. constexpr for constexpr-comparable types.
/// 汎用版: std::equality_comparable_with で型安全性を保証。
/// char pointer 型は除外し、non-template の const char* overload に fallback させる。
/// これにより char* pa; check_eq(pa, pb) が確実に内容比較になる。
template <typename A, typename B>
    requires (std::equality_comparable_with<A, B>
              && !detail::excluded_char_pointer_v<std::remove_cvref_t<A>, std::remove_cvref_t<B>>)
constexpr bool check_eq(const A& a, const B& b) {
    return detail::safe_eq(a, b);
}

/// @brief Check equality for C strings. constexpr content comparison.
/// 非テンプレートオーバーロード: 文字列リテラル (const char[N]) は
/// const char* に decay してこのオーバーロードに優先マッチする。
/// テンプレート版のアドレス比較を回避し、内容比較を保証する。
constexpr bool check_eq(const char* a, const char* b) {
    return detail::safe_eq(a, b);
}

/// @brief Check inequality.
template <typename A, typename B>
    requires (std::equality_comparable_with<A, B>
              && !detail::excluded_char_pointer_v<std::remove_cvref_t<A>, std::remove_cvref_t<B>>)
constexpr bool check_ne(const A& a, const B& b) {
    return !detail::safe_eq(a, b);
}

/// @brief Check inequality for C strings.
constexpr bool check_ne(const char* a, const char* b) {
    return !detail::safe_eq(a, b);
}

/// @brief Check less-than. Uses std::cmp_less for mixed-sign integer safety.
/// bool is excluded: std::cmp_less rejects bool ([utility.intcmp]/1).
/// bool の < は well-defined (false < true) なので else 分岐で正しく処理される。
/// ポインタ型は除外: ポインタの < は同一配列内以外で未規定 ([expr.rel]/4)。
/// const char* の大小比較はアドレス順であり文字列順ではないため、誤用を構造的に防止する。
/// decay_t を使用: 配列型 (const char[N]) は decay 後に const char* になるため、
/// std::is_pointer_v<A> では捕捉できない。decay_t で統一的に除外する。
template <typename A, typename B>
    requires (std::totally_ordered_with<A, B>
              && !std::is_pointer_v<std::decay_t<A>>
              && !std::is_pointer_v<std::decay_t<B>>)
constexpr bool check_lt(const A& a, const B& b) {
    if constexpr (std::integral<A> && !std::same_as<A, bool>
               && std::integral<B> && !std::same_as<B, bool>) {
        return std::cmp_less(a, b);  // <utility>: signed/unsigned 安全比較
    } else {
        return a < b;
    }
}

template <typename A, typename B>
    requires (std::totally_ordered_with<A, B>
              && !std::is_pointer_v<std::decay_t<A>>
              && !std::is_pointer_v<std::decay_t<B>>)
constexpr bool check_le(const A& a, const B& b) {
    if constexpr (std::integral<A> && !std::same_as<A, bool>
               && std::integral<B> && !std::same_as<B, bool>) {
        return std::cmp_less_equal(a, b);
    } else {
        return a <= b;
    }
}

template <typename A, typename B>
    requires (std::totally_ordered_with<A, B>
              && !std::is_pointer_v<std::decay_t<A>>
              && !std::is_pointer_v<std::decay_t<B>>)
constexpr bool check_gt(const A& a, const B& b) {
    if constexpr (std::integral<A> && !std::same_as<A, bool>
               && std::integral<B> && !std::same_as<B, bool>) {
        return std::cmp_greater(a, b);
    } else {
        return a > b;
    }
}

template <typename A, typename B>
    requires (std::totally_ordered_with<A, B>
              && !std::is_pointer_v<std::decay_t<A>>
              && !std::is_pointer_v<std::decay_t<B>>)
constexpr bool check_ge(const A& a, const B& b) {
    if constexpr (std::integral<A> && !std::same_as<A, bool>
               && std::integral<B> && !std::same_as<B, bool>) {
        return std::cmp_greater_equal(a, b);
    } else {
        return a >= b;
    }
}

/// @brief Check approximate equality. Floating-point only.
/// @pre eps >= 0. Negative eps is a precondition violation — unconditionally returns false
///      (NaN check and eps < 0 check run before exact equality fast-path).
/// Uses common_type to avoid precision loss (e.g. long double → double).
template <std::floating_point A, std::floating_point B>
bool check_near(const A& a, const B& b,
                std::common_type_t<A, B> eps = static_cast<std::common_type_t<A, B>>(0.001)) {
    using C = std::common_type_t<A, B>;
    auto ca = static_cast<C>(a);
    auto cb = static_cast<C>(b);
    if (std::isnan(ca) || std::isnan(cb) || std::isnan(eps)) return false;
    if (eps < C{0}) return false;  // precondition: eps >= 0
    if (ca == cb) return true;     // exact equality (IEEE 754), independent of eps
    return std::abs(ca - cb) <= eps;
}

} // namespace umi::test
```

### 4.3 診断メッセージ生成 (format.hh)

失敗時のみ呼び出される診断メッセージ生成関数群。check.hh とは独立。

```cpp
namespace umi::test::detail {

/// @brief Capacity for diagnostic format buffers (lhs, rhs).
/// Used by TestContext for stack-allocated formatting.
constexpr std::size_t fail_message_capacity = 256;

/// @brief Format a value into a fixed buffer.
/// @pre size > 0 (size == 0 では何も書けず NUL 終端も不可能)
/// @post buf is null-terminated. 出力が size-1 を超える場合は切り詰められるが、常に NUL 終端される。
///
/// constexpr 可否は型による:
///   - bool: constexpr。BoundedWriter で "true" / "false" を書き込む
///   - 整数型: constexpr。BoundedWriter で十進文字列に変換（snprintf 不使用）
///   - const char*: constexpr。quoted string 形式 ("hello")。nullptr は "(null)"
///   - char[N] (配列型): const char* に decay して上記と同じ（constexpr）
///   - std::string: 非 constexpr。quoted string 形式 ("hello")。.data() + .size() で走査し、
///     埋め込み NUL は \0、制御文字は \xNN にエスケープ
///   - std::string_view: 非 constexpr。quoted string 形式。.data() + .size() で走査し、
///     埋め込み NUL は \0、制御文字は \xNN にエスケープ（std::string と同一ロジック）
///   - std::nullptr_t: constexpr。"nullptr"
///   - 浮動小数点型: 非 constexpr。snprintf "%g" / "%Lg"
///   - ポインタ型 (void*, T*): 非 constexpr。snprintf "%p"
///   - その他: constexpr。"(unprintable)" fallback
template <typename T>
constexpr void format_value(char* buf, std::size_t size, const T& value);

/// @brief Format near-comparison extra info: "eps=..., diff=..."
/// Eps type matches common_type_t<A, B> to preserve precision.
template <std::floating_point A, std::floating_point B>
void format_near_extra(char* buf, std::size_t size,
                       const A& a, const B& b, std::common_type_t<A, B> eps);

} // namespace umi::test::detail
```

全ての診断フォーマット処理は format.hh に集約する。
TestContext は `detail::format_value` と `detail::format_near_extra` を呼ぶだけで、
`std::snprintf` を直接使わない。

### 4.4 設計の本質

check 関数は**真の純粋関数**。入力（値2つ）→ 出力（bool）。副作用なし。source_location すら受け取らない。

これにより:
- **constexpr 完全対応**: `static_assert(check_eq(1 + 1, 2))` が可能
- **ゼロオーバーヘッド**: 成功パスは比較のみ。バッファ確保なし
- **判定ロジックの単一実装**: TestContext が同じ check 関数を呼び出す
- **公開型の安全性**: CheckResult は廃止。bool は UB の余地がない

---

## 5. TestContext — 構造化テストの集計

### 5.1 設計

check 関数が bool を返すため、TestContext が**失敗時のみ**診断情報の構築と報告を行う。
成功パスでは check 関数の bool 返却のみ — バッファ確保も構造体構築もなし。

TestContext は 3 つの機能を持つ:
1. **soft check** — 失敗しても継続（`eq`, `is_true` 等）
2. **fatal check** — 失敗時に呼び出し側が early return（`require_eq`, `require_true` 等）
3. **note stack** — スコープベースの文脈情報蓄積

### 5.2 FailureView — 構造化された失敗情報

reporter に生文字列ではなく構造化された失敗情報を渡す。
これにより human-readable と machine-readable の両方の reporter を統一的に実装できる。

```cpp
/// @brief Structured failure information passed to reporter.
/// Lifetime: valid only during the report_failure() call.
/// Reporter が保持したい場合は自前でコピーすること。
struct FailureView {
    const char* test_name;       // 所属するテストケース名
    std::source_location loc;
    bool is_fatal;               // true = require_* による前提条件チェック
    const char* kind;            // "eq", "ne", "lt", "le", "gt", "ge", "near", "true", "false"
    const char* lhs;             // formatted left operand (NUL-terminated, format_value @post)
    const char* rhs;             // formatted right operand (NUL-terminated, 比較系のみ。nullptr = 未使用)
    const char* extra;           // eps/diff (near のみ。nullptr = 未使用)
    std::span<const char* const> notes;  // active notes (NoteGuard の RAII スコープ内で有効)
};
```

**op フィールドの除去 (D39)**: 以前の設計では `kind` ("eq") と `op` ("==") を別フィールドで持っていたが、
kind → op は一意に決まるため独立変数ではない。2 つ持つと `kind="eq"` なのに `op="<"` を渡す
不整合のリスクが生まれる。reporter 側で kind → op を導出すれば不整合は構造的に不可能。

**全フィールド `const char*` (D40, D43)**: kind/lhs/rhs/extra の全てが `const char*`。
- kind は常に文字列リテラル ("eq", "ne" 等) なので NUL 終端が保証される
- lhs/rhs/extra は `format_value` の `@post` で NUL 終端が保証される
- `const char*` なら reporter が `%s` / `puts()` で全フィールドを直接使える
- `std::string_view` だと `%.*s` が必要で `%s` 誤用バグのリスクがある
- 未使用時は `nullptr` を設定する（boolean check では lhs/rhs/extra が不要）

**ライフタイム制約**: FailureView のメンバが指す文字列（lhs, rhs, extra）は
`report_failure()` 呼び出し中のスタック上バッファに格納される。
FailureView 自体は呼び出し後に無効になる。reporter が情報を保持する必要がある場合は
自前でコピーすること（ログ蓄積 reporter、JSON 収集 reporter、self-test 用 recording reporter 等）。

**notes の型が `const char*`** である理由: `std::string_view` は `std::string` 等の一時オブジェクトからの
暗黙変換を許し、dangling を容易に生む。`const char*` は `.c_str()` を明示的に呼ばなければ
一時文字列を渡せないため、誤用の表面積が小さい。ただし完全な型安全ではなく、
規約（「NoteGuard のスコープ内で有効なポインタのみ渡すこと」）による保護に留まる。

**検討した代替案と却下理由**:
- `const char(&)[N]` テンプレート: リテラルのみ受理できるが、呼び出し側ローカルバッファ (`const char*`)
  による動的文脈 (§5.4) の正当な用途を構造的に排除してしまう
- `std::string_view`: 暗黙変換により dangling がさらに容易になり悪化
- Lifetime annotation: C++23 に存在しない（Rust の borrow checker 相当の機構がない）

**結論**: dangling を型レベルで完全に防ぐことは C++23 の言語機能では不可能。
`const char*` + 規約が、誤用の表面積を最小化しつつ正当な用途を許す最適解である。

**設計の意図**:
- `fail_message(const char*, loc)` → `report_failure(FailureView)` に置換
- reporter が自由にレンダリング方法を選べる（plain text, JSON, colored text）
- notes が自動的に failure に含まれるため、失敗時の文脈情報が常に利用可能

### 5.3 Note — スコープベースの文脈情報

カスタムメッセージを各 check メソッドに引数として追加する代わりに、
スコープベースの note stack を提供する。

NoteGuard は TestContext の **nested class** として定義する（§5.5 参照）。
これにより:
- forward declaration 不要（定義順問題が構造的に消滅）
- `note()` のインライン定義が自然に書ける
- NoteGuard が TestContext のスコープに閉じ、API surface が縮小
- `TestContext::NoteGuard` としてのみアクセス可能（ユーザーが直接構築する型ではないため適切）

使用例:

```cpp
s.run("parse and use", [](auto& t) {
    auto note = t.note("while parsing user header");
    if (!t.require_true(result.has_value())) return;
    t.eq(result->version, 2);
    t.eq(result->name, "test");
});
// note は RAII でスコープ終了時に自動的に pop される
```

**設計根拠**:
- `eq(a, b, "msg")` を全メソッドに追加すると API が倍増する
- 文脈は 1 個の assertion ではなく、複数の assertion にまたがることが多い
- Catch2 の `INFO` と同等の機能を、マクロなしで実現
- note stack は固定長 `std::array<const char*, 4>` — embedded でも安全
- overflow 時（depth >= 4）は超過分の note を配列に書かず、既存の note を保持する。
  push/pop の対称性が維持され、RAII スコープと完全に整合する。
  4 段を超えるネストは実用上稀であり、超過分は FailureView に含まれないが
  depth カウンタは正確に維持されるため、pop 後に depth < 4 に戻れば正常動作する
- `const char*` は `string_view` より誤用の表面積が小さい（暗黙変換なし）。完全な型安全ではなく規約による保護

### 5.4 動的文脈の注入 — 呼び出し側ローカルバッファパターン

literal note だけでは `case=42` や `input=<value>` のような動的文脈を積めない。
survey S2（カスタムメッセージ）を完全に代替するには、動的文脈の手段が必要。

**結論**: TestContext にバッファスロットを持たせる `note_buf()` は**却下** (D21)。
呼び出し側がローカルバッファを確保し、既存の `note()` に渡すパターンで十分:

```cpp
s.run("table-driven", [](auto& t) {
    for (int i = 0; i < n; ++i) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "case=%d", i);
        auto guard = t.note(buf);  // buf は guard より先に宣言 → lifetime 安全
        t.eq(results[i], expected[i]);
    }
});
```

**`note_buf()` を却下した理由**:
- TestContext サイズが 128B 増加（`std::array<char, 64> × 2`）。embedded で不要なコスト
- ring buffer の上書き問題: 3 個目の動的 note が 1 個目の内容を破壊する設計上の欠陥
- 呼び出し側ローカルバッファなら上記 2 つの問題が**構造的に存在しない**:
  - バッファは呼び出し側のスタックフレームに確保 → TestContext のサイズ増加なし
  - 各バッファが独立した変数 → 上書き問題なし
  - lifetime は C++ の変数宣言順で保証（buf が guard より先に宣言されるため、
    guard の破壊時に buf は有効）

**API 追加なし**。既存の `note(const char*)` がそのまま使える。
note() は nullptr 防御付き (D35) で、ローカルバッファの `const char*` も受理する。

### 5.5 TestContext 実装

```cpp
class TestContext {
  public:
    // FailCallback は C スタイルの type-erased callback。
    // C++23 の std::move_only_function は内部でヒープ確保の可能性があり P5 に反する。
    // TestContext をテンプレート化すれば type erasure 不要だが、
    // ユーザーが TestContext& を型引数なしで受け取れなくなり P4（最小 API）に反する。
    // function pointer + void* はゼロオーバーヘッドかつインライン化可能で、
    // TestContext を reporter 型から独立させる（context.hh が reporter.hh に依存しない）。
    using FailCallback = void (*)(const FailureView& failure, void* ctx);

    /// @brief RAII note guard. Pops note on destruction. Move-only.
    /// Nested class: 定義順問題なし、TestContext のスコープに閉じる。
    class NoteGuard {
      public:
        NoteGuard(const NoteGuard&) = delete;
        NoteGuard& operator=(const NoteGuard&) = delete;
        NoteGuard(NoteGuard&& other) noexcept : ctx(other.ctx), active(other.active) {
            other.active = false;
        }
        NoteGuard& operator=(NoteGuard&&) = delete;
        ~NoteGuard() { if (active) ctx.pop_note(); }

      private:
        friend class TestContext;
        NoteGuard(TestContext& ctx) : ctx(ctx), active(true) {}
        TestContext& ctx;
        bool active;
    };

    // Non-copyable, non-movable. run() ごとに新規構築される唯一のインスタンス。
    TestContext(const TestContext&) = delete;
    TestContext& operator=(const TestContext&) = delete;
    TestContext(TestContext&&) = delete;
    TestContext& operator=(TestContext&&) = delete;

    /// @brief true if no check has failed.
    [[nodiscard]] bool ok() const { return !failed; }

    // -- Note --

    /// @brief Push a context note. Returns RAII guard that pops on destruction.
    /// @param msg null-terminated string that must outlive the NoteGuard scope.
    ///      Typically a string literal or pointer to a persistent buffer.
    ///      nullptr を渡した場合は "(null)" が積まれる（UB にはならない）。
    [[nodiscard]] NoteGuard note(const char* msg) {
        push_note(msg ? msg : "(null)");
        return NoteGuard(*this);
    }

    // -- Soft checks (失敗しても継続) --

    bool is_true(bool cond,
                 std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_true(cond)) return true;
        report_bool_fail("true", loc);
        return false;
    }

    bool is_false(bool cond,
                  std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_false(cond)) return true;
        report_bool_fail("false", loc);
        return false;
    }

    template <typename A, typename B>
        requires (std::equality_comparable_with<A, B>
                  && !detail::excluded_char_pointer_v<std::remove_cvref_t<A>, std::remove_cvref_t<B>>)
    bool eq(const A& a, const B& b,
            std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_eq(a, b)) return true;
        report_compare_fail(a, "eq", b, loc);
        return false;
    }

    // const char* オーバーロード（char*/const char*/リテラルを内容比較に寄せる）
    bool eq(const char* a, const char* b,
            std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_eq(a, b)) return true;
        report_compare_fail(a, "eq", b, loc);
        return false;
    }

    template <typename A, typename B>
        requires (std::equality_comparable_with<A, B>
                  && !detail::excluded_char_pointer_v<std::remove_cvref_t<A>, std::remove_cvref_t<B>>)
    bool ne(const A& a, const B& b,
            std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_ne(a, b)) return true;
        report_compare_fail(a, "ne", b, loc);
        return false;
    }

    bool ne(const char* a, const char* b,
            std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_ne(a, b)) return true;
        report_compare_fail(a, "ne", b, loc);
        return false;
    }

    template <typename A, typename B>
        requires (std::totally_ordered_with<A, B>
                  && !std::is_pointer_v<std::decay_t<A>>
                  && !std::is_pointer_v<std::decay_t<B>>)
    bool lt(const A& a, const B& b,
            std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_lt(a, b)) return true;
        report_compare_fail(a, "lt", b, loc);
        return false;
    }

    template <typename A, typename B>
        requires (std::totally_ordered_with<A, B>
                  && !std::is_pointer_v<std::decay_t<A>>
                  && !std::is_pointer_v<std::decay_t<B>>)
    bool le(const A& a, const B& b,
            std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_le(a, b)) return true;
        report_compare_fail(a, "le", b, loc);
        return false;
    }

    template <typename A, typename B>
        requires (std::totally_ordered_with<A, B>
                  && !std::is_pointer_v<std::decay_t<A>>
                  && !std::is_pointer_v<std::decay_t<B>>)
    bool gt(const A& a, const B& b,
            std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_gt(a, b)) return true;
        report_compare_fail(a, "gt", b, loc);
        return false;
    }

    template <typename A, typename B>
        requires (std::totally_ordered_with<A, B>
                  && !std::is_pointer_v<std::decay_t<A>>
                  && !std::is_pointer_v<std::decay_t<B>>)
    bool ge(const A& a, const B& b,
            std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_ge(a, b)) return true;
        report_compare_fail(a, "ge", b, loc);
        return false;
    }

    template <std::floating_point A, std::floating_point B>
    bool near(const A& a, const B& b,
              std::common_type_t<A, B> eps = static_cast<std::common_type_t<A, B>>(0.001),
              std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_near(a, b, eps)) return true;
        report_near_fail(a, b, eps, loc);
        return false;
    }

    // -- Fatal checks (失敗時に呼び出し側が early return) --

    bool require_true(bool cond,
                      std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_true(cond)) return true;
        report_bool_fail("true", loc, true);
        return false;
    }

    bool require_false(bool cond,
                       std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_false(cond)) return true;
        report_bool_fail("false", loc, true);
        return false;
    }

    template <typename A, typename B>
        requires (std::equality_comparable_with<A, B>
                  && !detail::excluded_char_pointer_v<std::remove_cvref_t<A>, std::remove_cvref_t<B>>)
    bool require_eq(const A& a, const B& b,
                    std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_eq(a, b)) return true;
        report_compare_fail(a, "eq", b, loc, true);
        return false;
    }

    // const char* オーバーロード（eq と同じ理由。char pointer を内容比較に寄せる）
    bool require_eq(const char* a, const char* b,
                    std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_eq(a, b)) return true;
        report_compare_fail(a, "eq", b, loc, true);
        return false;
    }

    template <typename A, typename B>
        requires (std::equality_comparable_with<A, B>
                  && !detail::excluded_char_pointer_v<std::remove_cvref_t<A>, std::remove_cvref_t<B>>)
    bool require_ne(const A& a, const B& b,
                    std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_ne(a, b)) return true;
        report_compare_fail(a, "ne", b, loc, true);
        return false;
    }

    // const char* オーバーロード
    bool require_ne(const char* a, const char* b,
                    std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_ne(a, b)) return true;
        report_compare_fail(a, "ne", b, loc, true);
        return false;
    }

    template <typename A, typename B>
        requires (std::totally_ordered_with<A, B>
                  && !std::is_pointer_v<std::decay_t<A>>
                  && !std::is_pointer_v<std::decay_t<B>>)
    bool require_lt(const A& a, const B& b,
                    std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_lt(a, b)) return true;
        report_compare_fail(a, "lt", b, loc, true);
        return false;
    }

    template <typename A, typename B>
        requires (std::totally_ordered_with<A, B>
                  && !std::is_pointer_v<std::decay_t<A>>
                  && !std::is_pointer_v<std::decay_t<B>>)
    bool require_le(const A& a, const B& b,
                    std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_le(a, b)) return true;
        report_compare_fail(a, "le", b, loc, true);
        return false;
    }

    template <typename A, typename B>
        requires (std::totally_ordered_with<A, B>
                  && !std::is_pointer_v<std::decay_t<A>>
                  && !std::is_pointer_v<std::decay_t<B>>)
    bool require_gt(const A& a, const B& b,
                    std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_gt(a, b)) return true;
        report_compare_fail(a, "gt", b, loc, true);
        return false;
    }

    template <typename A, typename B>
        requires (std::totally_ordered_with<A, B>
                  && !std::is_pointer_v<std::decay_t<A>>
                  && !std::is_pointer_v<std::decay_t<B>>)
    bool require_ge(const A& a, const B& b,
                    std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_ge(a, b)) return true;
        report_compare_fail(a, "ge", b, loc, true);
        return false;
    }

    template <std::floating_point A, std::floating_point B>
    bool require_near(const A& a, const B& b,
                      std::common_type_t<A, B> eps = static_cast<std::common_type_t<A, B>>(0.001),
                      std::source_location loc = std::source_location::current()) {
        ++checked;
        if (check_near(a, b, eps)) return true;
        report_near_fail(a, b, eps, loc, true);
        return false;
    }

    // -- 構築・集計 (BasicSuite::run() 専用。ユーザー API ではない) --
    // public なのは friend 宣言と constrained template の再宣言不整合を回避するため (D30)。
    // ユーザーが直接構築する用途はなく、P4 の「ユーザーが知るべき API」には含まない。

    /// @pre name != nullptr (null-terminated, outlives TestContext)
    /// @pre cb != nullptr (失敗時に無条件で呼ばれる。nullptr は UB)
    explicit TestContext(const char* name, FailCallback cb, void* ctx)
        : test_name(name), fail_cb(cb), fail_ctx(ctx) {}

    [[nodiscard]] int checked_count() const { return checked; }
    [[nodiscard]] int failed_count() const { return fail_count; }

  private:

    // -- Note stack --
    static constexpr int max_notes = 4;
    std::array<const char*, max_notes> note_stack{};
    int note_depth = 0;

    void push_note(const char* msg) {
        if (note_depth < max_notes) {
            note_stack[static_cast<std::size_t>(note_depth)] = msg;
        }
        // depth >= max_notes: 配列は更新しない（古い note を保持）
        ++note_depth;
    }
    void pop_note() { --note_depth; }

    std::span<const char* const> active_notes() const {
        auto n = std::min(note_depth, max_notes);
        return {note_stack.data(), static_cast<std::size_t>(n)};
    }

    // -- Failure reporting --

    void report_bool_fail(const char* kind, std::source_location loc,
                          bool fatal = false) {
        FailureView fv{test_name, loc, fatal, kind, nullptr, nullptr, nullptr, active_notes()};
        fail_cb(fv, fail_ctx);
        failed = true;
        ++fail_count;
    }

    template <typename A, typename B>
    void report_compare_fail(const A& a, const char* kind,
                             const B& b, std::source_location loc,
                             bool fatal = false) {
        std::array<char, detail::fail_message_capacity> lhs_buf;
        std::array<char, detail::fail_message_capacity> rhs_buf;
        detail::format_value(lhs_buf.data(), lhs_buf.size(), a);
        detail::format_value(rhs_buf.data(), rhs_buf.size(), b);
        FailureView fv{test_name, loc, fatal, kind,
                       lhs_buf.data(), rhs_buf.data(), nullptr, active_notes()};
        fail_cb(fv, fail_ctx);
        failed = true;
        ++fail_count;
    }

    template <std::floating_point A, std::floating_point B>
    void report_near_fail(const A& a, const B& b, std::common_type_t<A, B> eps,
                          std::source_location loc, bool fatal = false) {
        std::array<char, detail::fail_message_capacity> lhs_buf;
        std::array<char, detail::fail_message_capacity> rhs_buf;
        std::array<char, 64> extra_buf;
        detail::format_value(lhs_buf.data(), lhs_buf.size(), a);
        detail::format_value(rhs_buf.data(), rhs_buf.size(), b);
        detail::format_near_extra(extra_buf.data(), extra_buf.size(), a, b, eps);
        FailureView fv{test_name, loc, fatal, "near",
                       lhs_buf.data(), rhs_buf.data(), extra_buf.data(), active_notes()};
        fail_cb(fv, fail_ctx);
        failed = true;
        ++fail_count;
    }

    const char* test_name;
    FailCallback fail_cb;
    void* fail_ctx;
    bool failed = false;
    int checked = 0;
    int fail_count = 0;
};
```

### 5.6 fatal check の設計判断

`require_eq` と `eq` は同一の check 関数を呼び出すが、失敗時に `FailureView.is_fatal = true` を設定する。
reporter はこのフラグで soft 失敗と fatal 失敗を区別して表示できる:

```cpp
s.run("parse", [](auto& t) {
    // require: 失敗したら即 return
    if (!t.require_true(result.has_value())) return;
    // soft: 失敗しても次の check に進む
    t.eq(result->name, "test");
    t.eq(result->version, 2);
});
```

**なぜマクロや例外を使わないか**:
- `if (!t.require_*(...)) return;` は制御フローが完全に可視
- 隠れた `longjmp` や例外スローによる「魔法の中断」は UMI の思想に反する
- 冗長さは欠点ではなく特徴 — 制御フローの明示性を保証する

**なぜ require_* と soft check を別名にするか**:
- `require_*` は「これは前提条件である」という意図の表明
- reporter は将来的に `require` 失敗を soft 失敗と区別して表示できる
- コードの読み手に「ここが失敗したら後続は意味がない」と伝える

### 5.7 成功パスのコスト分析

- `ctx.eq(1, 1)` → `++checked` → `check_eq(1, 1)` → `true` → 即 return
- バッファ確保なし、FailureView 構築なし、コールバック呼び出しなし
- 追加コストは `++checked` のインクリメントのみ

**P5 との整合**: `++int` は厳密にはゼロではないが、Cortex-M で 1 サイクルの store 命令であり
実用上ゼロオーバーヘッドと見なせる。この微小コストと引き換えに得られる価値:
- **空テスト検出**: `checked_count() == 0` のテストケースは「何も検証していない」ことを意味する。
  reporter が警告を出す、CI が空テストを拒否する等の品質保証に使える
- **デバッグ効率**: `assertions: 48 checked, 0 failed` は「十分な数の検証が行われた」確認に有用
- P5 の定義は「vtable なし。ヒープ確保なし。例外なし」であり、`++int` は P5 の範囲内

### 5.8 clear_failed / has_failed の除去

`clear_failed()` と `has_failed()` は除去。`ok()` のみが公開 API。

TestContext の構築は `BasicSuite::run()` 内でのみ行われ、毎回新しいインスタンスが作られるため、状態リセットは不要。

### 5.9 API 命名: check_* vs 短縮名

2つの選択肢:

**案A: `ctx.check_eq(a, b)`** — 現行と同じ。冗長だが明確。
**案B: `ctx.eq(a, b)`** — 短い。`ctx.` が文脈を与えるため曖昧さなし。

推奨: **案B**。理由:
- `ctx.` プレフィックスが「これはテスト検証である」という文脈を十分に与える
- `check_` は free function（check.hh）の名前空間で使い、メソッドでは省略
- テストコードの可読性が向上する: `ctx.eq(a, b)` vs `ctx.check_eq(a, b)`

ただし free function は `umi::test::check_eq` のまま維持。理由: 名前空間なしの `eq` は汎用的すぎる。

---

## 6. BasicSuite — カウンタ管理と報告

### 6.1 集計モデル

**Suite のカウンタはテストケース単位**。1回の `run()` = 1テストケース。

インライン check（`suite.eq()` 等）は存在しない。Suite の公開 API は `run()` のみ。

理由:
- `run()` と inline check が同一カウンタを共有すると、summary の値が「テストケース数」と「命題数」の混合値になる
- summary の「5/5 passed」が「5テストケースが通った」なのか「5個の eq が通った」なのか不明になる
- 集計単位の一貫性は、テスト結果の信頼性の基盤

**「テスト名なしの簡易 check」が必要なら、匿名 run を使う:**

```cpp
s.run("basic arithmetic", [](auto& ctx) {
    ctx.eq(1 + 1, 2);
    ctx.eq(2 * 3, 6);
});
```

これで集計単位は常に「テストケース」で統一される。

### 6.2 設計

```cpp
template <ReporterLike R>
class BasicSuite {
  public:
    /// @pre name は null-terminated で、BasicSuite の生存期間中有効であること。
    ///      典型的には文字列リテラル。std::string::c_str() 等の短命ポインタは UB。
    explicit BasicSuite(const char* name, R reporter = R{})
        : suite_name(name), reporter(std::move(reporter)) {}

    void section(const char* title) { reporter.section(title); }

    /// @brief Run a structured test.
    /// @pre test_name は null-terminated で、run() 呼び出し中（reporter コールバック含む）
    ///      有効であること。典型的には文字列リテラル。
    /// @note fn は lvalue として呼び出される（`fn(ctx)` であり `std::move(fn)(ctx)` ではない）。
    ///      rvalue-only callable（`operator()() &&` のみ定義）は制約不成立で拒否される。
    ///      通常のラムダ・関数ポインタ・関数オブジェクトは全て lvalue 呼び出し可能なため影響なし。
    template <typename F>
        requires requires(F& f, TestContext& ctx) {
            { f(ctx) } -> std::same_as<void>;
        }
    void run(const char* test_name, F&& fn) {
        reporter.test_begin(test_name);
        TestContext ctx(
            test_name,
            [](const FailureView& fv, void* p) {
                static_cast<BasicSuite*>(p)->reporter.report_failure(fv);
            },
            this);
        fn(ctx);
        // assertion 補助統計の集約
        total_checked += ctx.checked_count();
        total_failed_checks += ctx.failed_count();
        if (ctx.ok()) {
            reporter.test_pass(test_name);
            passed++;
        } else {
            reporter.test_fail(test_name);
            failed++;
        }
    }

    [[nodiscard]] int summary() {
        SummaryView sv{suite_name, passed, failed,
                       total_checked, total_failed_checks};
        reporter.summary(sv);
        return failed > 0 ? 1 : 0;
    }

    /// @brief Access the reporter (for self-test / recording reporter pattern, §16).
    [[nodiscard]] const R& get_reporter() const { return reporter; }

  private:
    const char* suite_name;
    R reporter;
    int passed = 0;
    int failed = 0;
    int total_checked = 0;
    int total_failed_checks = 0;
};

// Suite alias は test.hh で定義（suite.hh は stdio に依存しない）
```

```cpp
// test.hh (convenience header)
#include <umitest/suite.hh>
#include <umitest/reporters/stdio.hh>
#include <umitest/reporters/plain.hh>

namespace umi::test {
using Suite = BasicSuite<StdioReporter>;
using PlainSuite = BasicSuite<PlainReporter>;
} // namespace umi::test
```

### 6.3 設計の核心

**Suite はテストケースの集計と報告のみを行う。判定は TestContext → check 関数に完全委譲。**

判定ロジックの重複は構造的に不可能:
- `check_eq` → `bool` を返す（check.hh、constexpr 純粋関数）
- `TestContext::eq` → `check_eq` を呼び、失敗時のみメッセージ生成+報告
- `Suite::run()` → `ctx.ok()` でテストケースの pass/fail を判定

Suite 自身は check 関数を直接呼ばない。インライン check メソッドは存在しない。

### 6.4 run() の F の制約

```cpp
template <typename F>
    requires std::invocable<F, TestContext&>
void run(const char* test_name, F&& fn);
```

2 つの選択肢:

**案A**: `std::invocable<F, TestContext&>` — 引数型のみ制約、戻り値は無制約
**案B**: `requires { { fn(ctx) } -> std::same_as<void>; }` — 戻り値も void に制約

案B の検証: `[](auto& ctx) { ctx.eq(1, 1); }` は最後の式が `bool` を返すが、
ラムダの戻り値型推論は**最後の式の型ではなく、return 文の有無**で決まる。
return 文がないラムダの推論型は `void` であるため、案B でも自然な記述は受理される。

ただし `[](auto& ctx) -> bool { ... return ctx.eq(1, 1); }` のような
明示的に非 void を返すラムダは案B で拒否される。

**D5 の選択: 案B（void 強制）**。理由:
- 明示的に非 void を返すラムダを拒否することで、テスト関数が「副作用で検証する」意味論を強制
- `return ctx.eq(...)` を書いてしまう移行ミスをコンパイル時に検出
- 自然な記述 `[](auto& ctx) { ctx.eq(1, 1); }` は問題なく受理される

```cpp
template <typename F>
    requires requires(F& f, TestContext& ctx) {
        { f(ctx) } -> std::same_as<void>;
    }
void run(const char* test_name, F&& fn);
```

制約の `F&` は意図的。`fn(ctx)` は lvalue 呼び出しであるため、制約も lvalue (`F&`) で検査する。
`F&&` で制約すると rvalue-only callable が制約を通過するが実際の呼び出しで失敗する不整合が生じる。

---

## 7. format.hh — 純粋フォーマッタ

### 7.1 変更点

現行からの変更:
- ANSI 色定数を除去（reporters/stdio.hh に移動）
- Legacy API `format_value(char*, size_t, T&)` を除去
- `UMI_TEST_NO_COLOR` マクロを除去（format.hh の責務ではない）

format.hh は**値を文字列に変換する純粋な関数群**のみを提供する。

### 7.2 BoundedWriter

変更なし。現行の設計は理想的:
- `size==0` 安全
- constexpr 対応
- truncation 検出可能

---

## 8. reporter — 報告の分離

### 8.1 SummaryView — 構造化された集計情報

```cpp
/// @brief Structured summary passed to reporter.
struct SummaryView {
    const char* suite_name;
    int cases_passed;
    int cases_failed;
    int assertions_checked;   // 補助統計
    int assertions_failed;    // 補助統計
};
```

### 8.2 ReporterLike concept

FailureView と SummaryView を受け取る形に変更:

```cpp
template <typename R>
concept ReporterLike = std::move_constructible<R> &&
    requires(R r, const char* s, const FailureView& fv, const SummaryView& sv) {
        r.section(s);
        r.test_begin(s);     // テストケース開始（failure の所属先を確立）
        r.test_pass(s);
        r.test_fail(s);
        r.report_failure(fv);
        r.summary(sv);
    };
// std::move_constructible: BasicSuite がコンストラクタで reporter を std::move で値保持するため必須。
// move assignment / swap は不要（BasicSuite は reporter を構築後に代入しない）。
// std::movable は move_constructible + assignable_from + swappable であり過剰制約。
```

**Embedded reporter の設計指針**:

ReporterLike は出力先を抽象化しない。reporter が出力先を直接保持し、フォーマットと出力を一体化する。
出力先の差し替えは reporter 型の差し替えで行う — これが concept ベース設計の意図である。

embedded 向け reporter は、コンストラクタで出力先（umipal の UART 抽象、umirtm の RTT API 等）を受け取り、
ReporterLike の 6 メソッドを実装する。FailureView の全フィールドが `const char*`（NUL 終端）であるため、
`puts()` 相当の関数で直接出力でき、`printf` や `<cstdio>` への依存なしに P5 を達成できる。

umitest 自身は具体的な embedded reporter を提供しない。
出力先はプロジェクトごとに異なり（UART, RTT, SWO, semihosting 等）、
それぞれの HAL/PAL 抽象は umitest の責務外である。

### 8.3 StdioReporter

ANSI 色定数をここに移動。FailureView を自由にレンダリング:

```cpp
class StdioReporter {
    static constexpr const char* green = "\033[32m";
    static constexpr const char* red = "\033[31m";
    static constexpr const char* cyan = "\033[36m";
    static constexpr const char* reset_code = "\033[0m";

  public:
    void section(const char* title) const { ... }
    void test_begin(const char* /*name*/) const { /* stdio: no-op (結果時に出力) */ }
    void test_pass(const char* name) const { ... }
    void test_fail(const char* name) const { ... }

    void report_failure(const FailureView& fv) const {
        // fv.test_name, kind, lhs, rhs, extra, notes を自由にレンダリング
        // op は kind から導出（PlainReporter::op_for_kind 参照）
        // 例: "  FAIL [integer equality]: expected 1 == 2"
        //     "    at test.cc:42"
        //     "    note: while parsing user header"
    }

    void summary(const SummaryView& sv) const {
        int total = sv.cases_passed + sv.cases_failed;
        // 例: "cases: 12/12 passed"
        //     "assertions: 48 checked, 0 failed"
    }
};
```

色の有無は reporter の実装詳細であり、format.hh が知る必要はない。

**C++23 `std::print` / `std::println`**: ホスト環境の reporter は `std::printf` の代わりに
`std::print` を使用できる。`std::print` は `<print>` ヘッダで提供され、
`std::format` ベースの型安全なフォーマットを行う。ただし:
- embedded 環境では `<print>` / `<format>` のサイズコストが大きい
- 本設計では reporter は差し替え可能なため、ホスト用 reporter のみ `std::print` を使い、
  embedded 用 reporter は HAL/PAL 経由の出力のままでよい
- PlainReporter / StdioReporter のリファレンス実装は `std::printf` で示すが、
  実装者は自由に `std::print` に置き換えて構わない

### 8.4 色の無効化

`UMI_TEST_NO_COLOR` マクロは除去する。代替:

1. **PlainReporter**: 色なし版の reporter を別途提供。`using PlainSuite = BasicSuite<PlainReporter>;`
2. **StdioReporter のコンストラクタ引数**: `StdioReporter(bool color = true)` で runtime 制御

推奨: **PlainReporter**。理由:
- ゼロオーバーヘッド（色の有無は型レベルで決定。runtime 分岐なし）
- マクロによるグローバル副作用を完全に排除
- `NO_COLOR` 環境変数対応が必要なら、`main()` で `Suite` か `PlainSuite` を選択するだけ

```cpp
// reporters/plain.hh
class PlainReporter {
  public:
    void section(const char* title) const { std::printf("\n[%s]\n", title); }
    void test_begin(const char* /*name*/) const { /* plain: no-op */ }
    void test_pass(const char* name) const { std::printf("  %s... OK\n", name); }
    void test_fail(const char* name) const { std::printf("  %s... FAIL\n", name); }

    void report_failure(const FailureView& fv) const {
        // kind → op マッピング（reporter の責務）
        const char* op = op_for_kind(fv.kind);
        // test_name を含めて failure の所属を明示
        if (fv.lhs != nullptr && fv.rhs != nullptr) {
            std::printf("  FAIL [%s]: expected %s %s %s\n",
                        fv.test_name, fv.lhs, op, fv.rhs);
        } else {
            std::printf("  FAIL [%s]: expected %s\n",
                        fv.test_name, fv.kind);
        }
        std::printf("    at %s:%u\n", fv.loc.file_name(), static_cast<unsigned>(fv.loc.line()));
        if (fv.is_fatal) {
            std::printf("    (fatal: test may have been aborted early)\n");
        }
        for (auto note : fv.notes) {
            std::printf("    note: %s\n", note);
        }
        if (fv.extra != nullptr) {
            std::printf("    (%s)\n", fv.extra);
        }
    }

    void summary(const SummaryView& sv) const {
        int total = sv.cases_passed + sv.cases_failed;
        std::printf("\n=================================\n");
        std::printf("cases: %d/%d passed\n", sv.cases_passed, total);
        std::printf("assertions: %d checked, %d failed\n",
                    sv.assertions_checked, sv.assertions_failed);
        std::printf("=================================\n");
    }

  private:
    /// @brief kind → operator string. Reporter の責務として kind から導出。
    /// kind と op は 1:1 対応であり、FailureView に両方持たせると不整合のリスクが生まれる。
    static constexpr const char* op_for_kind(const char* kind) {
        std::string_view k(kind);
        if (k == "eq") return "==";
        if (k == "ne") return "!=";
        if (k == "lt") return "<";
        if (k == "le") return "<=";
        if (k == "gt") return ">";
        if (k == "ge") return ">=";
        if (k == "near") return "≈";
        return "?";
    }
};
```

---

## 9. xmake.lua — 純粋な xmake 標準機能のみ

### 9.1 トップレベル

```lua
target("umitest")
    set_kind("headeronly")
    add_headerfiles("include/(umitest/**.hh)")
    add_includedirs("include", {public = true})
    -- set_languages は headeronly では効果なし。依存側で設定する

includes("tests")
```

変更点:
- `UMITEST_STANDALONE_REPO` グローバル変数を除去
- `add_requires("umibuild")` を除去
- standalone 検出ロジックを除去（テスト側で処理）

**library-spec.md との差異（例外規定）**:
library-spec は `<libname>_host` ターゲットと `<libname>_add_umitest_dep()` ヘルパーを要求するが、
umitest は以下の理由で例外とする:
1. **`umitest_host` 不要**: headeronly のみでホスト固有ソースがなく、`_host` ターゲットは空になる
2. **`umitest_add_umitest_dep()` 不可能**: 自分自身への依存ヘルパーは循環定義
3. **テストは `add_deps("umitest")` で直接依存**: standalone / monorepo の分岐も不要
   （umitest 自身のテストは monorepo 内でのみ実行される想定）

### 9.2 テスト

```lua
target("test_umitest")
    add_rules("host.test")
    add_tests("default")
    set_default(true)
    set_kind("binary")
    add_files("test_*.cc")
    add_deps("umitest")

target("test_umitest_compile_fail")
    set_kind("phony")
    set_default(false)
    add_tests("near_integer", "non_void_lambda", "context_copy",
              "eq_incomparable", "lt_unordered", "lt_pointer",
              "eq_char8t", "eq_char8t_nullptr", "eq_char8t_mixed",
              "ctx_eq_char8t", "ctx_eq_char8t_nullptr", "ctx_eq_char8t_mixed",
              "req_eq_char8t", "req_eq_char8t_nullptr",
              "ctx_eq_incomparable", "ctx_lt_unordered", "ctx_lt_pointer",
              "req_eq_incomparable", "req_lt_pointer", "req_near_integer")
    on_test(function()
        import("lib.detect.find_tool")

        local cxx = find_tool("c++") or find_tool("g++") or find_tool("clang++")
        assert(cxx and cxx.program, "no host C++ compiler found for compile-fail test")

        local includedir = path.join(os.scriptdir(), "..", "include")
        local testdir = path.join(os.scriptdir())
        local program = cxx.program

        -- Phase 1: smoke baseline（コンパイル成功が期待される）
        -- include パスやヘッダの健全性を保証し、compile-fail の偽陽性を防止
        local smoke_files = {"standalone", "eq_comparable", "lt_integer", "near_float"}
        for _, name in ipairs(smoke_files) do
            local src = path.join(testdir, "smoke", name .. ".cc")
            local ok = true
            try { function()
                os.iorunv(program, {"-std=c++23", "-I" .. includedir, "-fsyntax-only", src})
            end, catch { function(e)
                raise("smoke baseline '%s' failed to compile:\n%s", name, tostring(e))
            end } }
        end

        -- Phase 2: compile-fail（コンパイル失敗が期待される）
        local fail_cases = {"near_integer", "non_void_lambda", "context_copy",
                            "eq_incomparable", "lt_unordered", "lt_pointer",
                            "eq_char8t", "eq_char8t_nullptr", "eq_char8t_mixed",
                            "ctx_eq_char8t", "ctx_eq_char8t_nullptr", "ctx_eq_char8t_mixed",
                            "req_eq_char8t", "req_eq_char8t_nullptr",
                            "ctx_eq_incomparable", "ctx_lt_unordered", "ctx_lt_pointer",
                            "req_eq_incomparable", "req_lt_pointer", "req_near_integer"}
        for _, name in ipairs(fail_cases) do
            local src = path.join(testdir, "compile_fail", name .. ".cc")
            local ok = false
            try { function()
                os.iorunv(program, {"-std=c++23", "-I" .. includedir, "-fsyntax-only", src})
                ok = true
            end, catch { function() end } }
            if ok then
                raise("compile-fail test '%s' unexpectedly compiled", name)
            end
        end
    end)
```

**compile-fail テストの設計規約**:
- 各 `.cc` ファイルは 1 つの不正な呼び出しのみ含む
- `-fsyntax-only` でコンパイルし、**失敗が期待される**（成功したらテスト失敗）
- include path は umitest の `include/` のみ。外部依存なし
- テスト名と `.cc` ファイル名は 1:1 対応

**偽陽性対策 (negative pairing)**:
compile-fail テストは「コンパイル失敗なら pass」であるため、タイポや include 漏れ等の
無関係な理由で失敗しても通ってしまう。これを防ぐために、`smoke/` に **positive baseline** を
配置し、on_test 内で compile-fail の前に実行する（Phase 1 → Phase 2 の 2 段階）:
- `smoke/standalone.cc`: 全ヘッダの include 健全性
- `smoke/eq_comparable.cc`: 正しい型ペアで check_eq **と** ctx.eq **と** require_eq が通る（eq_incomparable / ctx_eq_incomparable / req_eq_incomparable の対）
- `smoke/lt_integer.cc`: 整数型で check_lt **と** ctx.lt **と** require_lt が通る（lt_pointer / ctx_lt_pointer / req_lt_pointer の対）
- `smoke/near_float.cc`: 浮動小数点で check_near **と** ctx.near **と** require_near が通る（near_integer / req_near_integer の対）

baseline がコンパイル失敗した場合は即座にエラーとなり、
後続の compile-fail テストは実行されない。これにより include 漏れや
タイポによる偽陽性のリスクを大幅に低減する（完全な防止ではなく、
baseline がカバーする API パスに限定される）。

全ての compile-fail テストに 1:1 の baseline を要求するのは過剰なため、
include やヘッダの健全性は `smoke/standalone.cc` が包括的にカバーし、
baseline は概念的に独立した制約（ポインタ除外等）を持つテストにのみ追加する。

変更点:
- `add_rules("host.test")` — xmake 標準ルールのみ使用
- カスタムルール・パッケージへの依存なし
- package smoke test は `test_umitest_compile_fail` と同じパターンで実現

---

## 10. テスト戦略

### 10.1 self-test の構造

```
tests/
├── test_main.cc           # エントリポイント
├── test_check.cc          # check 関数群の単体テスト（constexpr bool の検証）
├── test_format.cc         # フォーマッタの単体テスト（constexpr + runtime）
├── test_suite.cc          # Suite のカウンタ・ワークフローテスト
├── test_context.cc        # TestContext の集計テスト（soft/fatal/note）
├── test_reporter.cc       # FailureView / SummaryView のレンダリングテスト
├── compile_fail/
│   ├── near_integer.cc        # check_near(1, 2) / ctx.near(1, 2) は整数を拒否
│   ├── non_void_lambda.cc     # run() は void 以外を返すラムダを拒否
│   ├── context_copy.cc        # TestContext のコピーを拒否
│   ├── eq_incomparable.cc     # check_eq(42, "hello") — equality_comparable_with 不成立を拒否
│   ├── lt_unordered.cc        # check_lt(ptr, 42) — totally_ordered_with 不成立を拒否
│   ├── lt_pointer.cc          # check_lt("a", "b") — ポインタ/配列型の順序比較を拒否
│   ├── eq_char8t.cc           # check_eq(u8"a", u8"b") — 両側 char8_t* は未対応、コンパイルエラー (D42)
│   ├── eq_char8t_nullptr.cc   # check_eq(u8ptr, nullptr) — 片側 char8_t* でも拒否 (D42)
│   ├── eq_char8t_mixed.cc     # check_eq(std::u8string{}, u8ptr) — char8_t* 混在も拒否 (D42)
│   ├── ctx_eq_char8t.cc       # ctx.eq(u8"a", u8"b") — TestContext 経由でも両側 char8_t* を拒否 (D42)
│   ├── ctx_eq_char8t_nullptr.cc # ctx.eq(u8ptr, nullptr) — TestContext 経由でも片側 char8_t* を拒否 (D42)
│   ├── ctx_eq_char8t_mixed.cc # ctx.eq(std::u8string{}, u8ptr) — TestContext 経由でも char8_t* 混在を拒否 (D42)
│   ├── req_eq_char8t.cc       # require_eq(u8"a", u8"b") — fatal check でも同一制約 (D42)
│   ├── req_eq_char8t_nullptr.cc # require_eq(u8ptr, nullptr) — fatal check でも同一制約 (D42)
│   ├── ctx_eq_incomparable.cc # ctx.eq(42, "hello") — TestContext 経由でも同様に拒否
│   ├── ctx_lt_unordered.cc    # ctx.lt(ptr, 42) — TestContext 経由でも同様に拒否
│   ├── ctx_lt_pointer.cc     # ctx.lt("a", "b") — TestContext 経由でもポインタ/配列型の順序比較を拒否
│   ├── req_eq_incomparable.cc # require_eq(42, "hello") — fatal check でも同一制約
│   ├── req_lt_pointer.cc     # require_lt("a", "b") — fatal check でも同一制約
│   └── req_near_integer.cc   # require_near(1, 2) — fatal check でも同一制約
└── smoke/
    ├── standalone.cc      # ヘッダのみでコンパイル可能なことの検証
    ├── eq_comparable.cc   # check_eq + ctx.eq + require_eq の正常型ペア baseline（偽陽性対策）
    ├── lt_integer.cc      # check_lt + ctx.lt + require_lt の正常型ペア baseline（偽陽性対策）
    └── near_float.cc      # check_near + ctx.near + require_near の浮動小数点 baseline（偽陽性対策）
```

### 10.2 テストの書き方

```cpp
int main() {
    umi::test::Suite s("umitest");

    s.section("check_eq");
    s.run("integer equality", [](auto& ctx) {
        ctx.eq(1 + 1, 2);
        ctx.eq(0, 0);
    });

    s.run("string content comparison", [](auto& ctx) {
        // 別ストレージ同士: アドレス比較だと失敗するケース
        char a[] = "hello";
        char b[] = "hello";
        ctx.eq(a, b);  // 配列→decay→non-template overload→内容比較

        // char* と文字列リテラルの混在
        char* p = a;
        ctx.eq(p, "hello");  // char* + const char[6]→both char→non-template overload→内容比較
        ctx.eq("hello", p);  // const char[6] + char*→同上

        // std::string と const char* の混在（片側のみ char → テンプレートを通る）
        std::string s = "hello";
        ctx.eq(s, p);         // string vs char*→template→safe_eq→内容比較
        ctx.eq(s, "hello");   // string vs const char[6]→template→同上

        // string vs nullptr（safe_eq が nullptr ガード → false、UB なし）
        const char* null = nullptr;
        ctx.is_false(umi::test::check_eq(s, null));   // string vs nullptr → false
        ctx.is_false(umi::test::check_eq(null, s));   // nullptr vs string → false
    });

    s.section("check_near");
    s.run("inf equals inf", [](auto& ctx) {
        auto inf = std::numeric_limits<double>::infinity();
        ctx.near(inf, inf);
    });

    s.section("fatal check + note");
    s.run("parse and validate", [](auto& t) {
        auto result = parse(input);
        if (!t.require_true(result.has_value())) return;

        auto note = t.note("validating header fields");
        t.eq(result->version, 2);
        t.eq(result->name, "test");
    });

    return s.summary();
}
```

### 10.3 check 関数の単体テスト

check 関数は constexpr 純粋関数なので、コンパイル時にも検証可能:

```cpp
// コンパイル時検証（Suite/Context 不要）
static_assert(umi::test::check_eq(1 + 1, 2));
static_assert(!umi::test::check_eq(1, 2));
static_assert(umi::test::check_eq("hello", "hello"));  // C string: constexpr 内容比較
static_assert(!umi::test::check_eq("hello", "world"));
static_assert(umi::test::check_true(true));
static_assert(!umi::test::check_true(false));
static_assert(umi::test::check_false(false));
static_assert(!umi::test::check_false(true));

// ランタイム検証
s.run("check_eq basic", [](auto& ctx) {
    ctx.is_true(umi::test::check_eq(1, 1));
    ctx.is_true(!umi::test::check_eq(1, 2));
});
```

---

## 11. Public API まとめ

### 11.1 ユーザーが知るべき型

| 型 | 役割 |
|----|------|
| `Suite` | テストランナー。テストの登録・実行・集計・報告 |
| `TestContext` | `run()` 内で使う検証コンテキスト |
| `TestContext::NoteGuard` | `note()` の RAII ガード（nested class、直接構築しない） |

check 関数群（`check_eq`, `check_true` 等）は `bool` を返す constexpr 純粋関数。
`static_assert` で直接使えるため、上級者は TestContext なしでコンパイル時検証が可能。

**注**: TestContext のコンストラクタと集計アクセサは public だが、これは D30（friend 再宣言問題の回避）
のためであり、**ユーザー API ではない**。コンストラクタは `FailCallback` (非 null) を要求し、
`BasicSuite::run()` のみが正しく構築する。直接構築はサポートしない。

### 11.2 ユーザーが使うメソッド

**Suite**:

```cpp
suite.section("group");
suite.run("name", [](auto& ctx) { /* ctx で検証 */ });
suite.summary();
```

**TestContext — soft check（失敗しても継続）**:

```cpp
ctx.eq(a, b);
ctx.ne(a, b);
ctx.lt(a, b);
ctx.le(a, b);
ctx.gt(a, b);
ctx.ge(a, b);
ctx.near(a, b, eps);
ctx.is_true(cond);
ctx.is_false(cond);
```

**TestContext — fatal check（失敗時に early return）**:

```cpp
if (!ctx.require_true(cond)) return;
if (!ctx.require_false(cond)) return;
if (!ctx.require_eq(a, b)) return;
if (!ctx.require_ne(a, b)) return;
if (!ctx.require_lt(a, b)) return;
if (!ctx.require_le(a, b)) return;
if (!ctx.require_gt(a, b)) return;
if (!ctx.require_ge(a, b)) return;
if (!ctx.require_near(a, b, eps)) return;
```

**TestContext — note（スコープベースの文脈情報）**:

```cpp
auto note = ctx.note("while parsing header");
// note のスコープ内で失敗すると、この文脈が FailureView に含まれる
```

**動的文脈**: 呼び出し側ローカルバッファ + `note()` で対応。§5.4 参照。
`note_buf()` は却下 (D21)。追加 API なしで動的文脈を実現。

### 11.3 最小テスト

```cpp
#include <umitest/test.hh>

int main() {
    umi::test::Suite s("example");
    s.run("add", [](auto& ctx) { ctx.eq(1 + 1, 2); });
    return s.summary();
}
```

### 11.4 fatal check + note の使用例

```cpp
s.run("parse and validate", [](auto& t) {
    auto result = parse(input);
    if (!t.require_true(result.has_value())) return;

    auto note = t.note("validating parsed header");
    t.eq(result->version, 2);
    t.eq(result->name, "test");
});
```

### 11.5 summary 出力例

```
cases: 12/12 passed
assertions: 48 checked, 0 failed
```

正式な成否単位はテストケース。assertion 数は補助統計。

---

## 12. 設計決定記録

| # | 決定 | 選択 | 却下案 | 理由 |
|---|------|------|--------|------|
| D1 | 判定の実装箇所 | free function (check.hh) | Suite/Context のメソッド内 | DRY。判定は 1 箇所。集計は呼び出し側 |
| D2 | check の戻り値 | `bool`（constexpr 純粋関数） | CheckResult 値型 | 成功パスのゼロオーバーヘッド（P5）。constexpr 対応（P3）。診断メッセージは失敗パスで TestContext が遅延生成 |
| D3 | TestContext の API 名 | `ctx.eq()` | `ctx.check_eq()` | `ctx.` が文脈を与える。`check_` は冗長 |
| D4 | Suite の公開 API | `section`, `run`, `summary` のみ | Suite に eq/is_true 等を持たせる | D14 により Suite はインライン check を持たない |
| D5 | run() の F の制約 | `requires(F& f, ...) { f(ctx) } -> std::same_as<void>` (lvalue, D38) | `std::invocable` (戻り値無制約) | void を返さないラムダを拒否し移行ミスを検出。自然な記述 `[](auto& ctx) { ctx.eq(1,1); }` は void 推論で受理。lvalue 制約は D38 参照 |
| D6 | ANSI 色 | StdioReporter の private | format.hh | 色は報告の装飾。フォーマッタの責務ではない |
| D7 | clear_failed() | 除去 | public 維持 | TestContext は run() ごとに新規構築。リセット不要 |
| D8 | Legacy format API | 除去 | 維持 | 後方互換不要。BoundedWriter に統一 |
| D9 | standalone 検出 | 除去 | xmake.lua で条件分岐 | headeronly ライブラリに standalone 分岐は不要 |
| D10 | boolean check 名 | `is_true` / `is_false` | `check` / `check_true` | `eq`, `ne`, `lt` と同じ動詞なし省略パターンとの一貫性。`is_*` は述語として自然 |
| D11 | 診断バッファ | `std::array<char, 256>` を失敗パスでのみスタック確保 | CheckResult に常時内包 / 動的文字列 | 成功パスはバッファ確保すらしない。P5 完全達成。embedded でヒープ禁止 |
| D12 | reporter の色制御 | PlainReporter（型レベル切替） | マクロで全体制御 / runtime bool | マクロはグローバル副作用。runtime 分岐はオーバーヘッド。型で決定すればゼロコスト |
| D13 | check_near の NaN | NaN 同士も fail | NaN == NaN を pass | IEEE 754 準拠: NaN != NaN。「NaN であること」の検証は `is_true(std::isnan(v))` で行う。check_near は「近い値」の判定であり、NaN は「値」ではない |
| D14 | インライン check | 廃止（run() のみ） | Suite に eq/is_true 等を持たせる | 集計単位の一貫性。run() = テストケース単位で統一。inline check はカウンタの意味を壊す |
| D15 | reporter 注入 | コンストラクタ引数（デフォルトあり） | デフォルト構築のみ | UART、リングバッファ等の状態付き reporter を注入可能にする。デフォルト引数で既存コード互換 |
| D16 | fatal check | `require_*` メソッド + `if (!...) return;` | マクロ中断 / 例外 / longjmp | 制御フローが完全に可視。UMI の「魔法なし」思想。冗長さは特徴 |
| D17 | カスタムメッセージ | note stack（RAII スコープガード） | 各 check に msg 引数を追加 | API 倍増を回避。文脈は複数 assertion にまたがる。Catch2 INFO と同等の機能をマクロなしで実現 |
| D18 | reporter への failure 伝達 | FailureView 構造体（全フィールド `const char*`、op は reporter が kind から導出） | 生文字列 / kind+op 分離 / string_view | human-readable / machine-readable 両立。reporter が kind→op 導出 (D39)。全フィールド `const char*` で NUL 終端保証・`%s`/`puts()` 直接使用可能 (D40, D43) |
| D19 | summary の集計 | テストケース（主） + assertion 数（従） | テストケースのみ | 情報量を落とさず集計の意味を保つ。assertion 数は空テスト検出 (checked==0) とデバッグ効率に寄与する補助統計。`++int` のコストは P5 の範囲内 (§5.7) |
| D20 | note stack の容量と型 | 固定長 `std::array<const char*, 4>`、overflow は超過分を無視 | `string_view` / 動的配列 / シフト方式 / `const char(&)[N]` | embedded 安全。4 段で実用上十分。ヒープ確保なし (P5)。overflow 時は超過分を配列に書かず push/pop 対称性を維持（RAII 安全）。シフト方式は pop 時にスコープ整合性を破壊するため却下。`const char*` は `string_view` より誤用の表面積が小さい。`const char(&)[N]` はリテラル限定になり note_buf 等の正当用途を排除。dangling の完全防止は C++23 の言語限界（lifetime annotation 不在） |
| D21 | 動的文脈の note | 呼び出し側ローカルバッファ + `note()` | `note_buf()` (TestContext 内蔵バッファ) | `note_buf()` は ring buffer 上書き問題 + TestContext サイズ 128B 増加が構造的欠陥。ローカルバッファは両問題が存在せず、既存 API で動作する (§5.4) |
| D22 | Suite alias の配置 | `test.hh`（convenience header） | `suite.hh` | `suite.hh` が `reporters/stdio.hh` に依存すると、embedded で `<cstdio>` が強制される。`test.hh` に分離して依存を選択可能に |
| D23 | TestContext のコピー可能性 | Non-copyable, non-movable | デフォルト（コピー可能） | コピーで独立したカウンタが生まれ、コピー側の失敗がオリジナルに反映されない。run() ごとに唯一のインスタンスであるべき |
| D24 | failure のテスト識別 | FailureView に `test_name` + ReporterLike に `test_begin` | FailureView にテスト名なし | machine-readable reporter（JSON 等）が failure をテストケースに紐付けるために必須 |
| D25 | check_near の型制約と精度 | `std::floating_point` + `common_type_t<A, B>` + `eps >= 0` precondition | `std::is_arithmetic_v` + 全て double 変換 | 整数を拒否（意図しない挙動防止）。`common_type_t` で long double の精度を保持。eps も共通型。負の eps は早期 false を返す（exact equality fast-path との不整合を防止） |
| D26 | C 文字列の比較 | `safe_eq(const char*, const char*)` は `string_view` で constexpr 内容比較。アドレス比較ショートカット禁止（定数式で未規定 [expr.eq]/2） | `strcmp`（非 constexpr） / ポインタ比較 / `a == b` shortcut | `string_view` の `==` は C++23 で constexpr。`static_assert(check_eq("a", "a"))` を可能にし P3 完全達成。`a == b` shortcut は定数式でコンパイラが拒否するため使えない |
| D27 | check 関数の型制約 | テンプレート版: `equality_comparable_with` + `excluded_char_pointer_v` 除外 (D41) / `totally_ordered_with` + decay_t ポインタ除外 (D36) + `const char*` 非テンプレートオーバーロード | 無制約 template | 比較不可能な型ペアをコンパイル時に拒否。両側 char 関連型または片側でも char8_t\* の場合にテンプレートから除外 (D41/D42)。片側のみ char 関連（`std::string` vs `const char*`）はテンプレートを通す。順序比較は全ポインタ/配列を decay_t で除外 (D36) |
| D28 | NoteGuard の定義位置 | TestContext の nested class | 独立クラス + forward declaration | 定義順問題が構造的に消滅。API surface 縮小。`TestContext::NoteGuard` として自然にスコープが閉じる |
| D29 | FailCallback | `void (*)(const FailureView&, void*)` function pointer | `std::move_only_function` / TestContext テンプレート化 | `move_only_function` はヒープ確保の可能性 (P5違反)。テンプレート化は `TestContext&` を型引数なしで受け取れなくなる (P4違反)。function pointer + `void*` はゼロオーバーヘッドかつ reporter 型独立 |
| D30 | TestContext と BasicSuite の結合 | コンストラクタ・集計アクセサを public 化。friend 不要 | `template <typename> friend class BasicSuite` | friend 宣言は後続の constrained template 定義 (`template <ReporterLike R>`) と redeclaration mismatch を起こす (C++20 制約付きテンプレートの再宣言規則)。コンストラクタは `FailCallback` を要求するため意図しない直接構築の動機がなく、public でも API 汚染は実質なし |
| D31 | 異符号整数比較 | `std::cmp_equal` / `std::cmp_less` 等 (C++20 `<utility>`) で safe compare。bool は除外 (`!std::same_as<A, bool>`) | builtin `==` / `<` のまま | builtin 演算子は signed→unsigned 暗黙変換で `-1 == 4294967295u` が true、`-1 < 1u` が false になる。`std::cmp_*` は数学的に正しい比較を保証。constexpr 対応。`if constexpr` で非 bool 整数ペアのみ適用。bool は `std::cmp_*` が受け付けない ([utility.intcmp]/1) ため除外し、builtin `==` / `<` で処理 |
| D32 | ReporterLike の値制約 | `std::move_constructible<R>` を concept に含める | method shape のみ / `std::movable` | BasicSuite が reporter を `std::move` で構築時に値保持する。immovable 型は concept 不成立にし、instantiation failure の前に拒否。`std::movable` は move assignment + swappable を追加要求するが、BasicSuite は構築後に reporter を代入しないため過剰 |
| D33 | TestContext メソッドの型制約 | free function と同じ concept (`equality_comparable_with`, `totally_ordered_with`) + `const char*` オーバーロード | 制約なし template | generic code (`requires(T t) { t.eq(a, b); }`) から見た API 契約を正確にする。内部の check_eq 呼び出しへの丸投げでは SFINAE-unfriendly |
| D34 | format_value の constexpr 範囲 | bool・整数・const char*・char[N] は constexpr (BoundedWriter)。浮動小数点・ポインタは非 constexpr (snprintf) | 全て constexpr / 全て snprintf | BoundedWriter で十進変換すれば整数は constexpr 可能。浮動小数点の %g 相当を constexpr で再実装するのは過剰複雑 (P4違反)。NUL 終端は `@post` で保証 |
| D35 | note(nullptr) の扱い | nullptr 時は "(null)" を積む（防御的）。`@pre` は課さない | `@pre msg != nullptr` (Design by Contract) | note() は user-facing API。FailCallback の `@pre cb != nullptr` (internal API) とは異なり、ユーザーが誤って nullptr を渡す可能性がある。防御コスト（1分岐）は P5 に対して無視できる |
| D36 | ポインタ型の順序比較 | check_lt/le/gt/ge で `!std::is_pointer_v<std::decay_t<A>>` により除外。`decay_t` で配列型 (`const char[N]`) も捕捉 | `!std::is_pointer_v<A>` (配列型が漏れる) / `totally_ordered_with` のみ | ポインタの `<` は同一配列内以外で未規定 ([expr.rel]/4)。`const char*` の `<` はアドレス順であり文字列順ではないため、`ctx.lt("a", "b")` は誤用。配列型は `is_pointer_v` = false だが `decay_t` で `const char*` になるため、`decay_t` が必須 |
| D37 | const char\* overload の根拠 | 内容比較への寄せ（`excluded_char_pointer_v` による除外） | `equality_comparable_with` 不成立への対策 / 片側除外（string vs ptr が壊れる） | 両側 char 関連型は `excluded_char_pointer_v` でテンプレートから除外し、non-template の `const char*` overload に到達。片側のみ char 関連（`std::string` vs `const char*`）はテンプレートを通し、`string::operator==(const char*)` で内容比較。char8_t\* は片側でも除外し overload なしでコンパイルエラー (D42) |
| D38 | run() の callable 制約 | `requires(F& f, ...)` (lvalue) | `requires(F&& f, ...)` (forwarding ref) | `fn(ctx)` は lvalue 呼び出し。制約も lvalue で検査し、rvalue-only callable が制約を通過して呼び出しで失敗する不整合を防止 |
| D39 | FailureView の比較演算子 | `kind` のみ。`op` は reporter が kind→op マッピングで導出 | `kind` + `op` の 2 フィールド | kind→op は 1:1 対応であり独立変数ではない。2 つ持つと `kind="eq"` なのに `op="<"` を渡す不整合のリスク。reporter 側に op_for_kind() を置けば不整合は構造的に不可能 |
| D40 | FailureView の lhs/rhs/extra 型 | `const char*` (NUL 終端が型から自明) | `std::string_view` | `format_value` の `@post` で NUL 終端が保証される。`const char*` なら reporter が `%s` / `puts()` で直接使える。`string_view` だと `%.*s` が必要で、`%s` 誤用バグのリスク。未使用時は `nullptr` (empty string_view より意図が明確)。kind も同様に `const char*` に統一 (D43) |
| D41 | char 関連型のテンプレート除外 | `detail::is_char_pointer_v` trait（`decay_t` ベース）+ `excluded_char_pointer_v` で除外判定 | 片側除外（string vs ptr が壊れる）/ 特殊化ベース（配列型を捕捉できない） | `decay_t` で `char*`・`const char*`・`char[N]`・`const char[N]`・`char8_t*` 系を捕捉。`excluded_char_pointer_v` は「両側 char 関連 OR 片側 char8_t\*」で除外。片側のみ char（char\*/const char\*）の場合はテンプレートを通す。特殊化ベースでは配列型が漏れてアドレス比較になる |
| D42 | char8_t\* の扱い | `is_char8_pointer_v` trait で**片側でも**テンプレートから除外（`excluded_char_pointer_v` に統合）。non-template overload は提供しない → コンパイルエラー | trait 対象外（静かにポインタ比較になる） / `both_char_pointer_v` のみ（片側混在が漏れる） | trait 対象外だとテンプレート版に入りアドレス比較が静かに成立する。`both_char_pointer_v`（両側判定のみ）だと `check_eq(u8ptr, nullptr)` / `check_eq(u8string, u8ptr)` が generic 経路に漏れる。`is_char8_pointer_v` で片側でも除外し、overload なしでコンパイルエラーにすれば「未対応 = 静かなバグ」を構造的に防止 |
| D43 | FailureView 全フィールドの型統一 | kind/lhs/rhs/extra 全て `const char*` | kind のみ `std::string_view` | kind は常に文字列リテラル ("eq" 等) なので NUL 終端が保証される。`const char*` に統一すれば reporter が `%s` / `puts()` で全フィールドを直接使える。`%.*s` との使い分けが不要になり誤用の表面積がゼロ |
| D44 | fatal check の対称性 | soft check と 1:1 対応する `require_*` 系を全て提供 | `require_true` のみ / `require_true(!cond)` で代用 | API の対称性は P4（予測可能な API）の要件。`require_true(!cond)` は失敗時の kind が "true" になり実際の期待 ("false") と乖離する。`require_near` は `require_true(check_near(...))` だと FailureView の lhs/rhs/eps 診断情報が失われる。全 soft check に対応する require を提供することで、診断精度と API 一貫性を両立 |

---

## 13. 現行との差分まとめ

| 項目 | 現行 | v2 |
|------|------|-----|
| 判定ロジック | Suite と Context で重複 | check.hh に constexpr bool 純粋関数として単一実装 |
| check の戻り値 | bool（診断は Suite/Context 側で生成） | bool（変更なし）。診断は失敗パスでのみ FailureView として構築 |
| 診断バッファ | check 関数内で常時確保 | 失敗パスでのみ TestContext 内でスタック確保（成功パス: ゼロコスト） |
| reporter への failure 伝達 | `fail_message(const char*, loc)` 生文字列 | `report_failure(FailureView)` 構造化データ |
| API 名 | `check_eq`, `check_true`, `check` | `eq`, `is_true`, `is_false`（TestContext のみ） |
| fatal check | なし | soft check と 1:1 対応する `require_*` 系 + `if (!...) return;` (D44) |
| カスタムメッセージ | `is_true(cond, msg)` のみ | note stack（RAII スコープガード、`const char*`、全 check 共通） |
| 集計単位 | run() + inline check 混合 | run() のみ（テストケース単位で統一） |
| summary | テストケース数のみ | テストケース（主） + assertion 数（従） |
| ANSI 色 | format.hh | reporters/stdio.hh |
| clear_failed | public | 除去 |
| mark_failed | 存在（未使用） | 除去 |
| Legacy format API | 存在 | 除去 |
| run() の F | 無制約 template | `requires(F& f, ...) { f(ctx) } -> std::same_as<void>` (lvalue 制約, D38) |
| UMITEST_STANDALONE_REPO | 存在 | 除去 |
| UMI_TEST_NO_COLOR | マクロ | 除去（PlainReporter で型レベル切替） |
| Suite alias | suite.hh 内で定義 | test.hh（convenience header）で定義。suite.hh は stdio 非依存 |
| TestContext コピー | デフォルト（コピー可能） | Non-copyable, non-movable |
| failure のテスト識別 | なし | FailureView に test_name、ReporterLike に test_begin |
| check_near の型制約 | `std::is_arithmetic_v` | `std::floating_point`（整数を拒否） |
| note overflow | なし（固定長上限なし） | 固定長 4 段。超過分は配列に書かず無視（RAII 安全。push/pop 対称性を維持） |
| check_near の精度 | double 固定 | `common_type_t<A, B>` で精度保持 |
| C 文字列比較 | 未規定 | `safe_eq(const char*, const char*)` で constexpr 内容比較（`string_view`） |
| check 関数の型制約 | 無制約 template | `equality_comparable_with` + `excluded_char_pointer_v` 除外 (D41/D42) / `totally_ordered_with` + decay_t ポインタ除外 (D36) + `const char*` overload |
| NoteGuard | 独立クラス | TestContext::NoteGuard（nested class） |
| 異符号整数比較 | builtin 演算子（暗黙変換で不正確） | `std::cmp_equal` / `std::cmp_less` 等で数学的に正しい比較（bool 除外: builtin で処理） |
| TestContext の friend | `template <ReporterLike> friend class BasicSuite` | friend 除去。コンストラクタ・集計アクセサを public 化 |
| FailureView の op | kind と op を別フィールド | op 除去。reporter が kind から導出 (D39) |
| FailureView の全フィールド | `std::string_view` | `const char*` (kind/lhs/rhs/extra 全て NUL 終端、D40/D43) |
| 動的文脈の note | なし | 呼び出し側ローカルバッファ + `note()` (D21)。note_buf() は却下 |

---

## 14. survey 提案への対応

[survey-modern-test-frameworks.md](survey-modern-test-frameworks.md) の S1-S4 提案に対する v2 での対応状況:

| # | survey 提案 | v2 での対応 | 設計決定 |
|---|------------|------------|---------|
| S1 | `require_eq` / `require_true` 中断モード追加 | **採用**。`require_*` メソッド + `if (!...) return;` による明示的協調中断 | D16 |
| S2 | 比較系 check にカスタムメッセージ引数追加 | **代替採用**。各 check に msg 引数を追加する代わりに note stack を導入。文脈は複数 assertion にまたがるため、スコープベースの方が適切 | D17 |
| S3 | 式分解はホスト専用オプショナル層として将来追加可能と明記 | **維持**。コア API は「明示的 2 引数」。将来の `expr_check.hh` の余地あり。check 関数が constexpr bool を返す設計はマクロラッパーと互換 | D2 (既存) |
| S4 | 残り設計は妥当 | **survey 指摘範囲内で対応完了**。D1-D15 は survey で裏付け。D16-D44 で S1-S2 を統合し、FailureView 構造化 (D18, D39, D40, D43)、assertion 補助統計 (D19)、型安全性強化 (D27, D31, D33, D36, D41) 等を追加 | — |

---

## 15. 移行計画

### 15.1 方針

「後方互換不要」は v2 の設計制約であり、移行の段階性を排除するものではない。
現行テストコードの一括書き換えは現実的でないため、以下の段階を踏む。

### 15.2 段階

| Phase | 内容 | 完了条件 |
|-------|------|---------|
| 0 | v2 ヘッダ群を `include/umitest/` に実装。既存コードは未変更 | v2 の self-test (`test_umitest`) が全 pass |
| 1 | 新規テストを v2 API で記述。既存テストはそのまま | 新規テストが v2 で書かれている |
| 2 | 既存テストを v2 API に書き換え。1 ファイルずつ移行 | 全テストが v2 API |
| 3 | 旧 API コード除去 | 旧 `check_eq(ctx, ...)` 等のパターンがゼロ |

**Phase 0 の詳細**:
- v2 ヘッダは新しい include パス（変更なし: `<umitest/check.hh>` 等）
- 現行の `BasicSuite::check_eq` 等はそのまま残る
- v2 の self-test は v2 自身を使って書く（§16 bootstrap 問題参照）

**Phase 2 の移行パターン**:
```cpp
// Before (v1)
s.check_eq(a, b);             // Suite のインライン check
s.run("test", [](auto& ctx) {
    ctx.check_eq(a, b);       // 旧 API 名
});

// After (v2)
s.run("test", [](auto& ctx) {
    ctx.eq(a, b);             // v2 API 名。Suite のインライン check は廃止
});
```

---

## 16. Self-test の独立性（bootstrap 問題）

### 16.1 問題

umitest の self-test は umitest 自身を使って書かれる。
reporter のバグがテスト結果を黙殺する場合（例: `report_failure()` が何も出力しない）、
テストが全 pass と表示されてしまう。

### 16.2 対策

**Recording reporter によるメタテスト**:

```cpp
/// @brief Self-test 用 reporter。failure を記録し、後で検証可能にする。
class RecordingReporter {
  public:
    void section(const char*) {}
    void test_begin(const char*) {}
    void test_pass(const char*) { pass_count++; }
    void test_fail(const char*) { fail_count++; }

    void report_failure(const FailureView& fv) {
        if (recorded < max_records) {
            // const char* → std::string で deep copy（呼び出し後も有効）
            auto& r = records[recorded++];
            r.kind = fv.kind ? fv.kind : "";
            r.lhs  = fv.lhs  ? fv.lhs  : "";
            r.rhs  = fv.rhs  ? fv.rhs  : "";
        }
    }

    void summary(const SummaryView&) {}

    int pass_count = 0;
    int fail_count = 0;

    static constexpr int max_records = 16;
    struct Record { std::string kind, lhs, rhs; };
    std::array<Record, max_records> records{};
    int recorded = 0;
};
```

**使い方** (test_context.cc):
```cpp
// 「意図的に失敗するテスト」を inner suite で実行し、
// outer suite で結果を検証する二重構造
void test_failure_reporting(umi::test::Suite& outer) {
    outer.run("eq failure is recorded", [](auto& t) {
        RecordingReporter rec;
        umi::test::BasicSuite<RecordingReporter> inner("inner", std::move(rec));
        inner.run("deliberately fail", [](auto& ctx) {
            ctx.eq(1, 2);  // 意図的に失敗
        });
        // inner の reporter が failure を記録したことを outer で検証
        auto& r = inner.get_reporter();
        t.eq(r.fail_count, 1);
        t.eq(r.recorded, 1);
        t.eq(r.records[0].kind, std::string("eq"));
    });
}
```

**設計上の注意**:
- outer suite が StdioReporter で、inner suite が RecordingReporter。
  outer のテストが fail すれば StdioReporter が出力するため、検出可能
- 最悪ケース（StdioReporter 自体が壊れている）は、プロセス終了コードで検出。
  `summary()` が `failed > 0 ? 1 : 0` を返すため、silent failure でも exit code が非ゼロ
- ただし `summary()` と `ok()` が同時に壊れた場合は検出不能。
  これは「テストフレームワークの最小信頼基盤」であり、
  全テストフレームワークに共通する理論的限界

### 16.3 reporter accessor

RecordingReporter パターンのために BasicSuite に reporter の const 参照を返すアクセサが必要。
§6.2 の BasicSuite 実装に `get_reporter()` として織り込み済み。

これは self-test 専用であり、通常のユーザーコードでは使用しない。


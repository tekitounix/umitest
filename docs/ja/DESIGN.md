# umitest 設計

[English](../DESIGN.md)

## 1. ビジョン

`umitest` は C++23 向けのマクロ不要、ヘッダーオンリーのテストフレームワークです：

1. テストコードは `suite.run()` に渡す void ラムダとして記述。
2. プリプロセッサマクロなし — `std::source_location` が `__FILE__`/`__LINE__` を置換。
3. 外部ビルド依存ゼロ — インクルードして使うだけ。
4. ホスト、WASM、組み込みターゲットで変更なしに動作。
5. 出力は CI ログに適した、人間が読みやすいカラーターミナルテキスト。
6. Reporter パラメータ化 — `BasicSuite<R>` でテストロジックと出力形式を分離。

---

## 2. 絶対要件

### 2.1 マクロ不要

すべてのアサーションは通常の関数呼び出し。`ASSERT_EQ` や `TEST_CASE` マクロは存在しない。
ソースロケーションは `std::source_location::current()` をデフォルト引数として使用して取得。

### 2.2 ヘッダーオンリー

フレームワーク全体が `include/umitest/` 配下のヘッダーファイルで構成される。
静的ライブラリなし、リンク時登録なし、コード生成なし。

### 2.3 ヒープアロケーションなし

すべての内部状態はスタックまたは静的ストレージを使用。
動的アロケーションが利用できないベアメタル環境との互換性を保証。

### 2.4 例外なし

アサーションはスローしない。TestContext は `mark_failed()` を通じて内部で失敗状態を追跡。
`run()` に渡すラムダは `void` を返す必要がある。

### 2.5 依存関係の境界

レイヤリングは厳格：

1. `umitest` は C++23 標準ライブラリヘッダーのみに依存。
2. 他の umi ライブラリへの依存なし。
3. 他の umi ライブラリがテストに `umitest` を使用（テスト時のみ）。

依存グラフ：

```text
umibench/tests -> umitest
umimmio/tests  -> umitest
umirtm/tests   -> umitest
umitest/tests  -> umitest (self-test)
```

---

## 3. 現行レイアウト

```text
lib/umitest/
├── README.md
├── xmake.lua
├── docs/
│   ├── INDEX.md
│   ├── DESIGN.md
│   ├── TESTING.md
│   └── ja/
├── examples/
│   ├── minimal.cc
│   ├── assertions.cc
│   └── check_style.cc
├── include/umitest/
│   ├── test.hh          # アンブレラヘッダー (Suite = BasicSuite<StdioReporter>)
│   ├── suite.hh         # BasicSuite<R> テンプレート
│   ├── context.hh       # TestContext（ソフト + 致命的チェック）
│   ├── check.hh         # constexpr bool フリー関数
│   ├── format.hh        # 診断出力用 format_value
│   ├── failure.hh       # FailureView 構造体
│   ├── reporter.hh      # ReporterLike コンセプト
│   └── reporters/
│       ├── stdio.hh     # StdioReporter（ANSI カラー）
│       ├── plain.hh     # PlainReporter（カラーなし）
│       └── null.hh      # NullReporter（サイレント）
└── tests/
    ├── test_main.cc
    ├── test_fixture.hh
    ├── test_check.cc
    ├── test_context.cc
    ├── test_suite.cc
    ├── test_reporter.cc
    ├── test_format.cc
    ├── smoke/           # ベースラインコンパイルテスト
    ├── compile_fail/    # 制約違反テスト
    └── xmake.lua
```

---

## 4. 成長レイアウト

```text
lib/umitest/
├── include/umitest/
│   ├── test.hh
│   ├── suite.hh
│   ├── context.hh
│   ├── check.hh
│   ├── format.hh
│   └── matchers.hh       # 将来: 合成可能なマッチャー (contains, starts_with)
├── examples/
│   ├── minimal.cc
│   ├── assertions.cc
│   ├── check_style.cc
│   └── matchers.cc        # 将来: マッチャー使用デモ
└── tests/
    ├── test_main.cc
    ├── test_*.cc
    ├── smoke/
    ├── compile_fail/
    └── xmake.lua
```

注記：

1. パブリックヘッダーは `include/umitest/` 配下に配置。
2. 将来のマッチャーはオプトイン（個別ヘッダー）— 最小使用時に肥大化しない。
3. `BasicSuite<R>` と `TestContext` がユーザー向け型として唯一の 2 つであり続ける。

---

## 5. プログラミングモデル

### 5.0 API リファレンス

パブリックエントリポイント: `include/umitest/test.hh`

コア型：

- `umi::test::Suite` — `BasicSuite<StdioReporter>`、デフォルトのテストランナー
- `umi::test::PlainSuite` — `BasicSuite<PlainReporter>`、ANSI カラーなし
- `umi::test::TestContext` — テストラムダに渡されるアサーションコンテキスト
- `umi::test::format_value()` — stdio-free の値フォーマッタ

TestContext チェック（ソフトチェックは失敗時も継続、致命的チェックは失敗時に `false` を返す）：

| ソフトチェック | 致命的チェック | チェック内容 |
|------------|-------------|--------|
| `eq(a, b)` | `require_eq(a, b)` | `a == b` |
| `ne(a, b)` | `require_ne(a, b)` | `a != b` |
| `lt(a, b)` | `require_lt(a, b)` | `a < b` |
| `le(a, b)` | `require_le(a, b)` | `a <= b` |
| `gt(a, b)` | `require_gt(a, b)` | `a > b` |
| `ge(a, b)` | `require_ge(a, b)` | `a >= b` |
| `near(a, b, eps)` | `require_near(a, b, eps)` | `|a - b| < eps` |
| `is_true(c)` | `require_true(c)` | 真偽値 true |
| `is_false(c)` | `require_false(c)` | 真偽値 false |

フリー関数 (`check.hh`): `check_eq`, `check_ne`, `check_lt`, `check_le`, `check_gt`, `check_ge`, `check_near`, `check_true`, `check_false` — `static_assert` やカスタムロジック用の純粋な `constexpr bool`。

ヘッダー：

- `include/umitest/test.hh` — アンブレラ（suite + reporters をインクルード）
- `include/umitest/suite.hh` — `BasicSuite<R>` テンプレート
- `include/umitest/context.hh` — ソフト + 致命的チェックを持つ TestContext
- `include/umitest/check.hh` — constexpr フリー関数
- `include/umitest/format.hh` — 診断出力用 format_value
- `include/umitest/reporter.hh` — ReporterLike コンセプト
- `include/umitest/failure.hh` — FailureView 構造体

### 5.1 最小パス

最小フロー：

1. `Suite` を構築。
2. `suite.run("name", [](auto& t) { ... })` を void ラムダで呼び出し。
3. `main` から `suite.summary()` を返却。

### 5.2 テストスタイル

テストは `suite.run()` に渡す void ラムダとして記述：

```cpp
Suite s("foo");
s.run("test_foo", [](auto& t) {
    t.eq(1 + 1, 2);
    t.is_true(true);
});
```

致命的チェックによる早期リターンパターン：

```cpp
s.run("test_bar", [](auto& t) {
    if (!t.require_true(ptr != nullptr)) return;
    t.eq(ptr->value, 42);
});
```

### 5.3 上級パス

上級用途：

1. 出力の論理グルーピング用 `section()`、
2. 浮動小数点比較用 `near()` / `require_near()`、
3. コンテキスト注釈用 `note()`（RAII スコープ）、
4. ユーザー型向けカスタム `format_value` 特殊化、
5. 独立した統計のための単一テストバイナリ内複数 Suite、
6. `BasicSuite<MyReporter>` によるカスタム Reporter。

---

## 6. アサーションセマンティクス

### 6.1 ソフトチェック

TestContext のすべてのソフトチェックメソッド（`eq`, `ne`, `lt`, `le`, `gt`, `ge`, `near`, `is_true`, `is_false`）：

1. `bool` を返す — チェック成功なら `true`、失敗なら `false`。
2. 失敗時、`mark_failed()` を呼び出してコンテキストの失敗フラグをセット。
3. 失敗時、Reporter 経由でソースロケーションと比較値を報告。
4. throw、abort、longjmp しない。テスト実行は継続。

### 6.2 致命的チェック

TestContext のすべての `require_*` メソッド：

1. `bool` を返す — ソフトチェックと同じ。
2. 失敗時、追加でコンテキストを致命的失敗としてマーク。
3. ガードパターン用: `if (!t.require_true(precond)) return;`

### 6.3 値フォーマット

`format_value<T>` は失敗メッセージ用に値を人間が読みやすい文字列に変換。
対応型: 整数型、浮動小数点型、bool、char、const char*、std::string_view、std::nullptr_t、ポインタ型。

---

## 7. 出力モデル

### 7.1 人間が読みやすいレポート

ANSI カラーコード付きターミナル出力（StdioReporter）：

1. セクションヘッダーはシアン。
2. 成功結果は緑 (`OK`)。
3. 失敗結果は赤 (`FAIL`) + ソースロケーション。
4. サマリー行にパス/失敗の合計数。

### 7.2 終了コード規約

`summary()` は全テスト成功時に `0`、いずれか失敗時に `1` を返す。
CI パイプラインおよび `xmake test` と互換。

### 7.3 Reporter アーキテクチャ

`BasicSuite<R>` は `ReporterLike` を満たす Reporter でパラメータ化：

- `StdioReporter` — ANSI カラー付き stdout 出力（`Suite` のデフォルト）
- `PlainReporter` — エスケープコードなしのプレーンテキスト
- `NullReporter` — サイレント、フレームワーク自体のテスト用

---

## 8. テスト戦略

1. umitest はセルフテスト：`tests/` は umitest 自身を使って動作を検証。
2. テストファイルは関心事ごとに分割：check 関数、context、suite、reporter、format。
3. すべてのテストは `xmake test` でホスト上で実行。
4. テストはタイミングではなくセマンティクスの正しさに焦点。
5. CI は対応全プラットフォームでホストテストを実行。

### 8.1 テストレイアウト

- `tests/test_main.cc`: テストエントリポイント
- `tests/test_check.cc`: constexpr フリー関数（check_eq, check_lt, check_near 等）
- `tests/test_context.cc`: TestContext のソフト + 致命的チェック
- `tests/test_suite.cc`: BasicSuite ライフサイクル、run()、section()、summary()
- `tests/test_reporter.cc`: Reporter 出力検証
- `tests/test_format.cc`: 対応する全型の format_value
- `tests/smoke/`: ベースラインコンパイルテスト（4 ファイル）
- `tests/compile_fail/`: 制約違反テスト（20 ケース）

### 8.2 テスト実行

```bash
xmake test                    # 全ターゲット
xmake test 'test_umitest/*'  # umitest のみ
```

### 8.3 品質ゲート

- 全セルフテストがホストでパス
- Smoke ベースラインファイルが正常にコンパイル
- compile-fail テストが不正なコードをコンパイル時に拒否
- format_value テストが対応する全型をカバー
- セルフテスト：umitest は自身を使用 — フレームワークの退行が即座に検出可能

---

## 9. サンプル戦略

サンプルは学習段階を表す：

1. `minimal`: 最短の完全なテスト。
2. `assertions`: すべてのアサーションメソッドのデモ。
3. `check_style`: セクションと構造化テストパターン。

---

## 10. 短期改善計画

1. 文字列/コンテナチェック用の合成可能なマッチャーを追加。
2. ADL カスタマイゼーションポイントによるユーザー定義型向け `format_value` を拡張。
3. ベンチマーク統合サンプル（umitest + umibench の組み合わせ）を追加。

---

## 11. 設計原則

1. マクロゼロ — すべての機能は通常の C++ 関数で実現。
2. ヘッダーオンリー — インクルードして使うだけ、ビルドステップ不要。
3. 組み込み安全 — ヒープなし、例外なし、RTTI なし。
4. 明示的な失敗位置 — すべてのアサーションに `std::source_location`。
5. Reporter パラメータ化 — 出力形式はテストロジックから分離。
6. コンパイル時契約 — 型制約が誤用をビルド時に防止。

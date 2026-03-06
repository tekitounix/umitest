# umitest 設計

[English](../DESIGN.md)

## 1. ビジョン

`umitest` は C++23 向けのマクロ不要、ヘッダーオンリーのテストフレームワークです：

1. テストコードは `bool` を返す通常の C++ 関数として記述。
2. プリプロセッサマクロなし — `std::source_location` が `__FILE__`/`__LINE__` を置換。
3. 外部ビルド依存ゼロ — インクルードして使うだけ。
4. ホスト、WASM、組み込みターゲットで変更なしに動作。
5. 出力は CI ログに適した、人間が読みやすいカラーターミナルテキスト。

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

アサーションはスローしない。テスト関数は `bool` を返す。
TestContext は `mark_failed()` を通じて内部で失敗状態を追跡。

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
│   ├── test.hh          # アンブレラヘッダー
│   ├── suite.hh         # Suite クラス + TestContext 実装
│   ├── context.hh       # TestContext 宣言
│   └── format.hh        # 診断出力用 format_value
└── tests/
    ├── test_main.cc
    ├── test_fixture.hh
    ├── test_assertions.cc
    ├── test_format.cc
    ├── test_suite_workflow.cc
    ├── compile_fail/
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
    └── xmake.lua
```

注記：

1. パブリックヘッダーは `include/umitest/` 配下に配置。
2. 将来のマッチャーはオプトイン（個別ヘッダー）— 最小使用時に肥大化しない。
3. Suite と TestContext がユーザー向け型として唯一の 2 つであり続ける。

---

## 5. プログラミングモデル

### 5.0 API リファレンス

パブリックエントリポイント: `include/umitest/test.hh`

コア型：

- `umi::test::Suite` — テストランナーと統計
- `umi::test::TestContext` — 構造化テスト用アサーションコンテキスト
- `umi::test::format_value()` — stdio-free の値フォーマッタ

利用可能なアサーション（TestContext の `assert_*`、Suite の `check_*`）：

| メソッド | チェック内容 |
|--------|--------|
| `assert_eq` / `check_eq` | `a == b` |
| `assert_ne` / `check_ne` | `a != b` |
| `assert_lt` / `check_lt` | `a < b` |
| `assert_le` / `check_le` | `a <= b` |
| `assert_gt` / `check_gt` | `a > b` |
| `assert_ge` / `check_ge` | `a >= b` |
| `assert_near` / `check_near` | `\|a - b\| < eps` |
| `assert_true` / `check` | 真偽値条件 |

ヘッダー：

- `include/umitest/suite.hh` — Suite クラス + TestContext 実装
- `include/umitest/context.hh` — TestContext 宣言
- `include/umitest/format.hh` — 診断出力用 format_value

### 5.1 最小パス

最小フロー：

1. `Suite` を構築。
2. `TestContext&` を受け取り `bool` を返すテスト関数を定義。
3. `suite.run("name", fn)` を呼び出し。
4. `main` から `suite.summary()` を返却。

### 5.2 2 つのテストスタイル

**構造化スタイル** (`TestContext` 使用)：

```cpp
bool test_foo(TestContext& t) {
    t.assert_eq(1 + 1, 2);
    return true;
}

Suite s("foo");
s.run("test_foo", test_foo);
```

**インラインスタイル** (Suite の `check_*` を直接使用)：

```cpp
Suite s("bar");
s.section("arithmetic");
s.check_eq(1 + 1, 2);
s.check_ne(1, 2);
return s.summary();
```

### 5.3 上級パス

上級用途：

1. 出力の論理グルーピング用 `section()`、
2. 浮動小数点比較用 `check_near()` / `assert_near()`、
3. ユーザー型向けカスタム `format_value` 特殊化、
4. 独立した統計のための単一テストバイナリ内複数 Suite。

---

## 6. アサーションセマンティクス

### 6.1 TestContext アサーション

TestContext のすべての `assert_*` メソッド：

1. `bool` を返す — アサーション成功なら `true`、失敗なら `false`。
2. 失敗時、`mark_failed()` を呼び出してコンテキストの失敗フラグをセット。
3. 失敗時、ソースロケーションと比較値を stdout に出力。
4. throw、abort、longjmp しない。テスト実行は継続。

### 6.2 Suite インラインチェック

Suite のすべての `check_*` メソッド：

1. `bool` を返す — アサーションと同じセマンティクス。
2. `passed` または `failed` カウンターを直接インクリメント。
3. TestContext は関与しない。クイックチェック向けのシンプルな方法。

### 6.3 値フォーマット

`format_value<T>` は失敗メッセージ用に値を人間が読みやすい文字列に変換。
対応型: 整数型、浮動小数点型、bool、char、const char*、std::string_view、std::nullptr_t、ポインタ型。

---

## 7. 出力モデル

### 7.1 人間が読みやすいレポート

ANSI カラーコード付きターミナル出力：

1. セクションヘッダーはシアン。
2. 成功結果は緑 (`OK`)。
3. 失敗結果は赤 (`FAIL`) + ソースロケーション。
4. サマリー行にパス/失敗の合計数。

### 7.2 終了コード規約

`summary()` は全テスト成功時に `0`、いずれか失敗時に `1` を返す。
CI パイプラインおよび `xmake test` と互換。

---

## 8. テスト戦略

1. umitest はセルフテスト：`tests/` は umitest 自身を使って動作を検証。
2. テストファイルは関心事ごとに分割：アサーション、フォーマット、Suite ワークフロー。
3. すべてのテストは `xmake test` でホスト上で実行。
4. テストはタイミングではなくセマンティクスの正しさに焦点。
5. CI は対応全プラットフォームでホストテストを実行。

### 8.1 テストレイアウト

- `tests/test_main.cc`: テストエントリポイント
- `tests/test_assertions.cc`: すべての assert_* メソッド (eq, ne, lt, le, gt, ge, near, true)
- `tests/test_format.cc`: 対応する全型の format_value
- `tests/test_suite_workflow.cc`: Suite ライフサイクル、run()、check_*、summary()

### 8.2 テスト実行

```bash
xmake test                    # 全ターゲット
xmake test 'test_umitest/*'  # umitest のみ
```

### 8.3 品質ゲート

- 全アサーションテストがホストでパス
- format_value テストが対応する全型をカバー
- Suite ワークフローテストがパス/失敗カウントと終了コードセマンティクスを検証
- セルフテスト：umitest は自身を使用 — フレームワークの退行が即座に検出可能

---

## 9. サンプル戦略

サンプルは学習段階を表す：

1. `minimal`: 最短の完全なテスト。
2. `assertions`: すべてのアサーションメソッドのデモ。
3. `check_style`: セクションとインラインチェックスタイル。

---

## 10. 短期改善計画

1. 文字列/コンテナチェック用の合成可能なマッチャーを追加。
2. 該当する場合、読み出し専用アサーション用の compile-fail テストを追加。
3. ADL カスタマイゼーションポイントによるユーザー定義型向け `format_value` を拡張。
4. ベンチマーク統合サンプル（umitest + umibench の組み合わせ）を追加。

---

## 11. 設計原則

1. マクロゼロ — すべての機能は通常の C++ 関数で実現。
2. ヘッダーオンリー — インクルードして使うだけ、ビルドステップ不要。
3. 組み込み安全 — ヒープなし、例外なし、RTTI なし。
4. 明示的な失敗位置 — すべてのアサーションに `std::source_location`。
5. 2 つのスタイル、1 つのフレームワーク — 構造化 (TestContext) とインライン (Suite チェック) が共存。

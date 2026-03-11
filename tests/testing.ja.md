# umitest テストカバレッジ

umitest はテストフレームワーク自体であるため、テストはフレームワークの全契約が正しく動作することを証明する。

## テスト構成

| カテゴリ | 数 | 目的 |
|----------|-----|------|
| default（ランタイム） | 96 アサーション / 7 モジュール | 全 API の振る舞い検証 |
| compile_fail | 23 ファイル | 型制約が不正なコードを拒否することの証明 |
| smoke | 1 ファイル | 全公開ヘッダが単独でコンパイルできること |

## ランタイムテストモジュール (test_main.cc → test_*.hh)

| モジュール | 対象 | カバレッジ |
|-----------|------|-----------|
| test_check | check.hh フリー関数 | static_assert + ランタイム: check_true/false/eq/ne/lt/le/gt/ge/near + C 文字列 + 符号混合整数 |
| test_format | format.hh | 全型の format_value（bool, nullptr, char, int, unsigned, float, enum, pointer, string, string_view, char[], "(?)") + BoundedWriter エッジケース（size=0/1/truncation） |
| test_context | context.hh TestContext | 全ソフトチェック + 全フェイタルチェック（require_true/false/eq/ne/lt/le/gt/ge/near）各パス/フェイル + NoteGuard RAII |
| test_suite | suite.hh BasicSuite | パス/フェイルカウント、空テスト、複数失敗、セクション、summary 終了コード |
| test_reporter | reporters/*.hh | ReporterLike concept 充足（Stdio/Plain/Null）+ FailureView/SummaryView フィールド検証 + op_for_kind 全 16 マッピング |
| test_exception | context.hh 例外チェック | throws\<E\>/throws(any)/nothrow + require バリアント、全パス/フェイルパターン |
| test_string | check.hh + context.hh 文字列チェック | check_str_contains/starts_with/ends_with + ctx ソフト/フェイタル 全パス/フェイル |

## compile_fail テストマトリクス

7 型制約 × 3 レイヤー（フリー関数 / ctx メソッド / require メソッド）+ 2 構造的制約 = 23 ファイル。

| 制約 | free | ctx | require |
|------|------|-----|---------|
| eq_incomparable（比較不能型） | eq_incomparable | ctx_eq_incomparable | req_eq_incomparable |
| eq_char8t（char8_t ペア） | eq_char8t | ctx_eq_char8t | req_eq_char8t |
| eq_char8t_mixed（char8_t × string） | eq_char8t_mixed | ctx_eq_char8t_mixed | req_eq_char8t_mixed |
| eq_char8t_nullptr（char8_t × nullptr） | eq_char8t_nullptr | ctx_eq_char8t_nullptr | req_eq_char8t_nullptr |
| lt_unordered（順序なし型） | lt_unordered | ctx_lt_unordered | req_lt_unordered |
| lt_pointer（ポインタ順序） | lt_pointer | ctx_lt_pointer | req_lt_pointer |
| near_integer（整数に対する near） | near_integer | ctx_near_integer | req_near_integer |

| 構造的制約 | ファイル |
|-----------|---------|
| TestContext コピー不可 | ctx_copy |
| run() が非 void ラムダを拒否 | non_void_lambda |

### なぜ 3 レイヤーか

`check_eq`（フリー関数）、`ctx.eq`（ctx メソッド）、`ctx.require_eq`（require メソッド）は独立したテンプレートでそれぞれ固有の制約を持つ。一方のレイヤーの制約変更が他方に自動的に伝播するとは限らないため、各レイヤーに compile_fail テストが必要。

## Smoke テスト

`standalone.cc` は全 10 公開ヘッダを個別にインクルードし、それぞれが単独でコンパイルできることを検証する。

## 網羅性の根拠

1. **API 対称性**: 全ソフトチェックに対応する require_* があり、双方のパス/フェイルパスをテスト
2. **型制約マトリクス**: 7 制約 × 3 レイヤーで compile_fail を網羅
3. **フォーマッタカバレッジ**: format_value の全分岐（12 型 + フォールバック）をテスト
4. **エッジケース**: BoundedWriter size=0/1、check_near NaN/負イプシロン、safe_eq nullptr/符号混合
5. **メタテスト**: RecordingReporter で内部状態（is_fatal フラグ、kind 文字列）を検証

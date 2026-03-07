# テスト

[Docs Home](INDEX.md) | [English](../TESTING.md)

## テストレイアウト

- `tests/test_main.cc`: テストエントリポイント
- `tests/test_check.cc`: constexpr フリー関数（check_eq, check_lt, check_near 等）
- `tests/test_context.cc`: TestContext のソフト + 致命的チェック（eq, is_true, require_eq 等）
- `tests/test_suite.cc`: BasicSuite ライフサイクル、run()、section()、summary()
- `tests/test_reporter.cc`: Reporter 出力検証
- `tests/test_format.cc`: bool, char, int, float, enum, ポインタの format_value
- `tests/smoke/`: ベースラインコンパイルテスト（4 ケース — `build_should_pass`）
- `tests/compile_fail/`: 制約違反テスト（20 ケース — `build_should_fail`）

## テスト実行

```bash
xmake test
```

サブセット指定:

```bash
xmake test 'test_umitest/*'                   # 全テスト（default + smoke + compile-fail）
xmake test 'test_umitest/default'              # セルフテストのみ
xmake test 'test_umitest/smoke_*'              # smoke ベースラインのみ
xmake test 'test_umitest/fail_*'               # compile-fail のみ
```

## テスト戦略

umitest はテストフレームワーク自体であるため、そのテストは以下を検証します：

1. **check 関数の正しさ** — 各 check_* フリー関数が期待通りの bool を返す
2. **TestContext チェック** — ソフトチェック（eq, lt, near, is_true）と致命的チェック（require_*）
3. **Suite ワークフロー** — パスカウント、失敗カウント、セクショングルーピング、サマリー終了コード
4. **Reporter 出力** — StdioReporter, PlainReporter, NullReporter が期待通りの出力を生成
5. **診断フォーマット** — format_value がすべての型に対して人間が読みやすい出力を生成
6. **コンパイル時契約** — 型制約が不正な比較を拒否（20 `build_should_fail` テスト）
7. **Smoke ベースライン** — ヘッダーが単独で正常にコンパイル（4 `build_should_pass` テスト）

## リリース品質ゲート

- 全ホストセルフテストパス
- Smoke ベースラインファイルが正常にコンパイル
- 全 compile-fail 契約テストが不正なコードを拒否
- WASM クロスビルドとテストがパス

## 新テスト追加方法

1. `tests/test_<feature>.cc` を作成
2. `tests/xmake.lua` の `add_files()` に追加
3. compile-fail テストの場合、`tests/compile_fail/` 配下に追加し `add_tests("fail_<name>", {files = "compile_fail/<name>.cc", build_should_fail = true})` を登録
4. `xmake test 'test_umitest/*'` で実行

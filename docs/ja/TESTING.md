# テスト

[Docs Home](INDEX.md) | [English](../TESTING.md)

## テストレイアウト

- `tests/test_main.cc`: テストエントリポイント
- `tests/test_assertions.cc`: assert_eq, assert_ne, assert_true, assert_near, 比較
- `tests/test_suite_workflow.cc`: TestSuite のパス/失敗/混合/ラムダ/セクションパターン
- `tests/test_format.cc`: bool, char, int, float, enum, ポインタの format_value
- `tests/compile_fail/near_non_numeric.cc`: compile-fail ガード — assert_near が非数値型を拒否

## テスト実行

```bash
xmake test
```

サブセット指定:

```bash
xmake test 'test_umitest/*'
xmake test 'test_umitest_compile_fail/*'
```

## テスト戦略

umitest はテストフレームワーク自体であるため、そのテストは以下を検証します：

1. **アサーションの正しさ** — 各アサーションが期待通りのパス/失敗結果を生成
2. **Suite ワークフロー** — パスカウント、失敗カウント、混合 Suite、サブ Suite のネスト
3. **診断フォーマット** — format_value がすべての型に対して人間が読みやすい出力を生成
4. **compile-fail ガード** — assert_near がコンパイル時に非数値型を拒否

## リリース品質ゲート

- 全ホストテストパス
- compile-fail 契約テストパス (near_non_numeric)
- WASM クロスビルドとテストがパス

## 新テスト追加方法

1. `tests/test_<feature>.cc` を作成
2. `tests/xmake.lua` の `add_files()` に追加
3. compile-fail テストの場合、`tests/compile_fail/` 配下に追加し xmake.lua に登録
4. `xmake test "test_umitest/*"` で実行

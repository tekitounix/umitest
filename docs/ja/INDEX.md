# umitest ドキュメント

[English](../INDEX.md)

このページは GitHub と Doxygen の両方で使用される正規ドキュメントエントリです。

## 読む順序

1. [設計](DESIGN.md)
2. [テスト](TESTING.md)

## API リファレンスマップ

- パブリックエントリポイント: `include/umitest/test.hh`
- コアコンポーネント:
  - `include/umitest/suite.hh` — BasicSuite<R>、テスト登録と実行
  - `include/umitest/context.hh` — TestContext、ソフト + 致命的アサーションチェック
  - `include/umitest/check.hh` — constexpr bool フリー関数
  - `include/umitest/format.hh` — 診断出力用 format_value
  - `include/umitest/reporter.hh` — ReporterLike コンセプト
  - `include/umitest/failure.hh` — FailureView 構造体
  - `include/umitest/reporters/stdio.hh` — StdioReporter（ANSI カラー）
  - `include/umitest/reporters/plain.hh` — PlainReporter（カラーなし）
  - `include/umitest/reporters/null.hh` — NullReporter（サイレント）

## ローカル生成

```bash
xmake doxygen -P . -o build/doxygen .
```

生成エントリポイント:

- `build/doxygen/html/index.html`

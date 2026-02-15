# umitest ドキュメント

[English](../INDEX.md)

このページは GitHub と Doxygen の両方で使用される正規ドキュメントエントリです。

## 読む順序

1. [設計](DESIGN.md)
2. [テスト](TESTING.md)

## API リファレンスマップ

- パブリックエントリポイント: `include/umitest/test.hh`
- コアコンポーネント:
  - `include/umitest/suite.hh` — TestSuite、テスト登録と実行
  - `include/umitest/context.hh` — TestContext、アサーション状態の追跡
  - `include/umitest/format.hh` — 診断出力用 format_value

## ローカル生成

```bash
xmake doxygen -P . -o build/doxygen .
```

生成エントリポイント:

- `build/doxygen/html/index.html`

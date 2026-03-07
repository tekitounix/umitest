# umitest

[English](../../README.md)

C++23 向けのマクロ不要・ヘッダオンリーのテストフレームワークです。
`std::source_location` により、テスト関数を通常の C++ コードとして記述できます。

## 特徴

- マクロ不要 — アサーションはすべて通常の関数呼び出し
- ヘッダオンリー — `#include <umitest/test.hh>` だけで使える
- 組み込み対応 — 例外・ヒープ・RTTI 不要
- Reporter 分離 — `BasicSuite<R>` でテストロジックと出力形式を分離
- コンパイル時契約検証 — 型制約により不正な比較をビルド時に拒否
- セルフテスト — umitest 自身を使ってテストするため、フレームワークの退行が即座に検出可能

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

```bash
xmake test
```

## ドキュメント

- [設計 & API](../DESIGN.md)
- [共通ガイド](../../docs/INDEX.md)

## ライセンス

MIT — [LICENSE](../../../LICENSE) を参照

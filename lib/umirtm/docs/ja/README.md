# umirtm

[English](../../README.md)

C++23 向けのヘッダオンリー Real-Time Monitor ライブラリです。
SEGGER RTT 互換リングバッファ、組み込み printf、`{}` プレースホルダ print を提供します。全てヒープ割り当て不要です。

## 特徴

- RTT 互換 — 既存 RTT ビューア (J-Link, pyOCD, OpenOCD) で動作
- 3つの出力レイヤー — 生リングバッファ、printf、`{}` フォーマット print
- 軽量 printf — ヒープ不使用、コードサイズ制御のための設定可能な機能セット
- ヘッダオンリー — ビルド依存なし
- ホストテスト可能 — ユニットテストと共有メモリエクスポート用ホスト側ブリッジ付き

## クイックスタート

```cpp
#include <umirtm/rtm.hh>
#include <umirtm/print.hh>

int main() {
    rtm::init("MY_RTM");
    rtm::log<0>("hello\n");
    rt::println("value = {}", 42);
    return 0;
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

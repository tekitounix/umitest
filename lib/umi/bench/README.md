# bench - 軽量ベンチマーク支援

ヘッダオンリーのベンチマーク支援フレームワークです。ターゲットごとのタイマー実装は
`platform` の同名ヘッダ切り替えで選択します。

## 依存関係

- なし（テストは `umitest` を使用）

## 主要API

- `concept TimerLike`
- `Runner<Timer>`
- `InlineRunner<Timer>`
- `Baseline<Timer>`

## クイックスタート

```cpp
#include <umi/bench/bench.hh>
// xmake.lua で platform/host または platform/renode を add_includedirs

umi::bench::PlatformInlineRunner runner;
runner.calibrate<256>();
auto stats = runner.benchmark_corrected<128>([] {
    // 測定対象
});
```

## ビルド・テスト

```bash
xmake -P lib/umi/bench build test_bench
xmake -P lib/umi/bench run test_bench
```

# umimock - Mock signal generator for testing

LIBRARY_STRUCTURE.md 規約の検証用リファレンスライブラリ。

## 依存関係

なし（自己完結）

## 主要API

- `MockSignal` — 定数/ランプ信号を生成するモッククラス
- `concept Generatable` — generate() メソッドを持つ型の制約

## クイックスタート

```cpp
#include <umimock/mock.hh>

umi::mock::MockSignal sig(umi::mock::Shape::CONSTANT, 0.5f);
float sample = sig.generate();  // 0.5f
```

## ビルド・テスト

```bash
xmake build test_umimock
xmake run test_umimock
```

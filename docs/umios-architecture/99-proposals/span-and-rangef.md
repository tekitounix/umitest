# 付録: span + range-for による DSP バッファ処理

> **Status**: Accepted  
> **Date**: 2026-02-04

## 概要

`AudioContext` のバッファアクセス API を `std::span` ベースに統一し、イテレータパターン（特に range-based for）を推奨する設計決定を行った。本ドキュメントでは、Renode（Cortex-M4 エミュレータ）で実施したベンチマーク結果に基づき、この決定の根拠と推奨パターンを示す。

## 結論

1. **span と生ポインタの性能差はない**（ホットループで ±0.1%）
2. **range-for は index ループより 2.3 倍高速**（-Ofast 時）
3. **span + range-for の組み合わせが最良**

---

## 1. なぜ span を採用するか

### 1.1 設計上の利点

| 特性 | 生ポインタ | std::span |
|------|-----------|-----------|
| 長さ情報 | 別途管理が必要 | `.size()` で取得可能 |
| 未接続チェック | `== nullptr` | `.empty()` |
| range-for 対応 | ❌ 不可 | ✅ 可能 |
| 意図の明確さ | 所有/借用が不明 | ビューであることが明確 |
| 境界チェック | 手動のみ | `.at()` で自動 |

### 1.2 性能への影響

ベンチマーク結果から、span の採用による性能ペナルティは **実質ゼロ** であることが判明した。

---

## 2. ベンチマーク結果

### 2.1 環境

- **MCU**: STM32F407VG (Cortex-M4F)
- **エミュレータ**: Renode 1.15.3
- **ツールチェーン**: clang-arm 19.1.5
- **バッファサイズ**: 64 サンプル
- **最適化**: `-Ofast` / `-Os`

### 2.2 span vs 生ポインタ

| 測定項目 | -Ofast (ptr) | -Ofast (span) | 差分 |
|---------|-------------|---------------|------|
| **API呼び出し (inline)** | 3.5 cyc | 6.8 cyc | +3.3 cyc |
| **単純ループ (64サンプル)** | 572.6 cyc | 571.7 cyc | **-0.9 cyc** |
| **シンセ (osc+filter)** | 3586 cyc | 3589 cyc | **+3 cyc (+0.08%)** |
| **エフェクト (biquad)** | 1790 cyc | 1790 cyc | **±0 cyc** |

**分析**:
- API 呼び出し時に +3 サイクルのオーバーヘッドがあるが、process() 内で 2 回呼んでも +6〜7 サイクル
- 3586 サイクルのシンセ処理に対して **0.2% 未満**
- ホットループ（実際の DSP 計算）では **差は誤差範囲内**

### 2.3 イテレータパターン比較

| パターン | -Ofast | -Os | 説明 |
|---------|--------|-----|------|
| **range-for (span)** | **249.6 cyc** | 661.3 cyc | `for (auto& s : span)` |
| **std::transform** | 371.2 cyc | 769.4 cyc | `std::transform(in, out, fn)` |
| **pointer iterator** | 370.6 cyc | 755.9 cyc | `for (auto p = begin; p != end; ++p)` |
| **index loop** | 571.6 cyc | 766.2 cyc | `for (i = 0; i < size; ++i)` |

**分析**:
- **range-for が圧倒的に高速**（index ループの 2.3 倍、-Ofast 時）
- コンパイラが range-for をより効率的にベクトル化・最適化
- `-Os` ではパターン間の差は縮まるが、range-for が依然最速

### 2.4 リアルな DSP 処理（イテレータ版）

| 処理 | -Ofast (index) | -Ofast (range-for) | -Ofast (transform) |
|------|----------------|-------------------|-------------------|
| **乗算 (in→out)** | 371.3 cyc | 250.8 cyc | 371.2 cyc |
| **単純 gain** | 571.6 cyc | 249.6 cyc | - |
| **エフェクト (mono)** | 944.9 cyc | - | 941.8 cyc |
| **エフェクト (stereo)** | 1888.9 cyc | - | 1887.6 cyc |

---

## 3. 推奨パターン

### 3.1 単一バッファ処理（最も高速）

**range-for を使用**:

```cpp
void process(umi::AudioContext& ctx) {
    auto out = ctx.output(0);
    if (out.empty()) return;

    for (auto& sample : out) {
        sample = osc.next();  // 直接書き込み
    }
}
```

**利点**:
- 最速（index ループの 2.3 倍）
- コードが簡潔で意図が明確
- コンパイラが最適化しやすい

### 3.2 入力→出力の変換

**std::transform を使用**:

```cpp
void process(umi::AudioContext& ctx) {
    auto in  = ctx.input(0);
    auto out = ctx.output(0);
    if (out.empty()) return;

    std::transform(in.begin(), in.end(), out.begin(), [this](float s) {
        return filter.process(s);
    });
}
```

**利点**:
- 入出力関係が明確
- ラムダで処理を局所化
- range-for と同等の最適化

### 3.3 複数バッファの同期処理

**index ループを使用**:

```cpp
void process(umi::AudioContext& ctx) {
    auto out_l = ctx.output(0);
    auto out_r = ctx.output(1);
    if (out_l.empty()) return;

    for (uint32_t i = 0; i < out_l.size(); ++i) {
        float sample = osc.next();
        out_l[i] = sample;
        if (!out_r.empty()) out_r[i] = sample;
    }
}
```

**注意**: 複数バッファを同じインデックスで処理する場合のみ index ループを使用。

### 3.4 パターン選択フローチャート

```
バッファ処理
    │
    ├─ 単一バッファ？ ─────────────> range-for
    │
    ├─ 入力→出力の変換？ ─────────> std::transform
    │
    └─ 複数バッファ同期処理？ ────> index ループ
```

---

## 4. 禁止パターン

### 4.1 生ポインタでの直接ループ

```cpp
// ❌ 禁止: span を使わずに生ポインタを取り出す
float* ptr = ctx.output(0).data();
for (int i = 0; i < size; ++i) {
    ptr[i] = ...;
}
```

**問題点**:
- span の安全性を無効化
- サイズ情報を別途管理する必要がある
- 意図が不明確

### 4.2 不要な `size()` の繰り返し呼び出し

```cpp
// ❌ 非推奨: ループ条件で毎回 size() を呼ぶ
for (uint32_t i = 0; i < ctx.output(0).size(); ++i) {
    ctx.output(0)[i] = ...;  // 毎回 output(0) を呼ぶ
}
```

**修正**:
```cpp
// ✅ span を変数にキャッシュ
auto out = ctx.output(0);
for (uint32_t i = 0; i < out.size(); ++i) {
    out[i] = ...;
}
```

---

## 5. API 設計

### 5.1 AudioContext のバッファアクセス

```cpp
class AudioContext {
public:
    // 出力バッファ（書き込み用）
    std::span<sample_t> output(uint32_t channel);

    // 入力バッファ（読み取り用）
    std::span<const sample_t> input(uint32_t channel);

    // バッファサイズ
    uint32_t buffer_size() const;
};
```

### 5.2 未接続チェック

```cpp
auto out = ctx.output(0);
if (out.empty()) return;  // ✅ 推奨

// または
if (!out.empty()) {
    // 処理
}
```

---

## 6. ベンチマーク実行方法

```bash
# -Ofast 版のビルド・実行
xmake build bench_span_vs_ptr
/Applications/Renode.app/Contents/MacOS/Renode \
    lib/umi/dsp/bench/renode/bench_span_vs_ptr.resc

# -Os 版のビルド・実行
xmake build bench_span_vs_ptr_Os
/Applications/Renode.app/Contents/MacOS/Renode \
    lib/umi/dsp/bench/renode/bench_span_vs_ptr_Os.resc

# 結果確認
cat build/bench_span_vs_ptr_uart.log
cat build/bench_span_vs_ptr_Os_uart.log
```

ソースコード: [lib/umi/dsp/bench/bench_span_vs_ptr.cc](../../../lib/umi/dsp/bench/bench_span_vs_ptr.cc)

---

## 7. 参考: 最適化オプションの影響

| 最適化 | 特徴 | 推奨用途 |
|-------|------|---------|
| `-Ofast` | 最高速、range-for の利点最大 | リリースビルド |
| `-Os` | コードサイズ優先、パターン差縮小 | フラッシュ制約時 |

**注意**: `-Os` でもパターン間の相対的な優劣は変わらないため、range-for を一貫して使用することを推奨。

---

## 8. まとめ

| 項目 | 結論 |
|------|------|
| **span 採用** | ✅ 性能ペナルティなし、安全性向上 |
| **range-for 採用** | ✅ 最速パターン（2.3倍高速） |
| **index ループ** | 複数バッファ同期処理時のみ使用 |
| **std::transform** | 入力→出力変換に最適 |

この設計により、**安全性と性能を両立** した DSP バッファ処理が実現できる。

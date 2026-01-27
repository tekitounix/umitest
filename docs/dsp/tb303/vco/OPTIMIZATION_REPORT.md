# TB-303 WaveShaper 最適化レポート

## 概要

TB-303 VCOのWaveShaperをCortex-M4 (168MHz) でリアルタイム処理するための最適化過程を記録。
目標はサンプルあたり350サイクル以下（48kHz時、リアルタイム係数10x以上）を維持しながら、
Ebers-Moll BJTモデルの精度を保つこと。

## 回路概要

```
        +12V (V_CC)
          │
         ┌┴┐
         │ │ R4 = 22kΩ
         └┬┘
          │
       ┌──┴──┐
       │  E  │
       │     │ PNP Transistor (2SC1815 equivalent)
       │  B──┼──────────────┐
       │     │              │
       │  C  │              │
       └──┬──┘             ┌┴┐
          │                │ │ R2 = 100kΩ
         ┌┴┐               └┬┘
         │ │ R5 = 10kΩ      │
         └┬┘                │
          │                 │
       +5.33V              ┌┴┐
       (V_COLL)            │ │ R3 = 10kΩ
                           └┬┘
                            │
          Input ────┤├──────┴──── Output (v_c)
                   C1=10nF
```

## 最適化の歴史

### Phase 1: ベースライン実装 (364 cycles)

**WaveShaperSchur** - Schur縮約による4x4→2x2行列削減

- 4x4 Jacobian行列を2x2に縮約
- Newton-Raphson反復（2回）
- `std::exp()` による正確なダイオード評価

```
サイクル数: 364 cycles/sample
RT比率: 9.6x @168MHz
```

### Phase 2: 高速exp近似の試行（失敗）

試行した手法と結果:

| 手法 | サイクル | 精度(RMS) | 結果 |
|------|---------|----------|------|
| Schraudolph exp | 370 | 悪化 | ❌ |
| Meijer LUT exp | 413 | 悪化 | ❌ |
| BC遅延（1-sample） | 372 | 発振 | ❌ |

**学び**: 単純なexp近似では精度が劣化。別のアプローチが必要。

### Phase 3: Wright Omega関数の導入 (320 cycles)

**WaveShaperSchurLambertW** - Lambert W関数による明示解

ダイオード方程式の変形:
```
I = Is * (exp(V/Vt) - 1)
↓ Lambert W変形
I = Vt * W(Is/Vt * exp(V/Vt)) - Is
↓ Wright Omega形式
I = Vt * ω(ln(Is/Vt) + V/Vt) - Is
```

**omega_fast2**: Fukushima-style有理関数近似 + Newton補正1回

```cpp
inline float omega_fast2(float x) {
    if (x < -2.5f) return expf_approx(x);
    else if (x < 5.0f) {
        float t = x + 2.5f;
        float num = 0.0821f + t * (0.1978f + t * (0.1336f + t * (0.0291f + t * 0.00187f)));
        float den = 1.0f + t * (-0.0543f + t * 0.00738f);
        return num / den;
    }
    else return x - logf_approx(x) + logf_approx(x) / x;
}
```

```
サイクル数: 320 cycles/sample (-12%)
RT比率: 10.9x @168MHz
精度: RMS 20mV vs Reference
```

### Phase 4: 分離型ソルバの試行（部分成功）

**目標**: 方程式を分離してNewton反復を削減

試行した手法:

1. **完全BC遅延**: i_crを1サンプル遅延
   - 結果: 遷移部で発振 ❌

2. **対角Newton**: 3変数を独立に更新
   - 結果: RMS 3115mV、収束不良 ❌

3. **ハイブリッド2x2**: 状態適応型
   - 遷移時（v_eb < 0.4V）: v_b/v_eを2x2
   - 飽和時（v_eb >= 0.4V）: v_b/v_cを2x2
   - 結果: RMS 20mV、良好 ✅

```
サイクル数: 327 cycles/sample
RT比率: 10.7x @168MHz
```

### Phase 5: omega3単体評価 (322 cycles)

**発見**: omega3（多項式のみ、Newton補正なし）でも十分な精度

```cpp
inline float omega3(float x) {
    if (x < -3.341459552768620f) return expf(x);
    else if (x < 8.0f) {
        float y = x + 1.0f;
        return 0.6314f + y * (0.3632f + y * (0.04776f + y * (-0.001314f)));
    }
    else return x - logf(x);
}
```

マイクロベンチマーク:
```
omega_fast2: 39 cycles/call
omega3:      32 cycles/call (-18%)
```

WaveShaper全体:
```
SchurLambertW (omega_fast2): 320 cycles
SchurOmega3 (omega3 only):   322 cycles
```

**結論**: omega3は関数単体では高速だが、WaveShaper全体では差が小さい。
理由: omega関数呼び出しは全体の一部（2回×32〜39サイクル≈64〜78サイクル）であり、
残りの計算（Schur縮約、行列解法、ダンピング）がボトルネック。

## 最終ベンチマーク結果

### Cortex-M4 @ 168MHz, 48kHz サンプリング

| 実装 | サイクル/サンプル | RT比率 | 精度(RMS) | 備考 |
|------|------------------|--------|----------|------|
| **WaveShaperSchur** | 364 | 9.6x | baseline | オリジナル |
| WaveShaperSchurMo | 370 | 9.4x | - | mo::pow2版 |
| WaveShaperSchurUltra | 372 | 9.4x | - | BC遅延版 |
| WaveShaperSchurTable | 413 | 8.4x | - | Meijer LUT |
| WaveShaperWDF | 384 | 9.1x | - | chowdsp_wdf |
| WaveShaperWDFFull | 379 | 9.2x | - | Lambert W版WDF |
| **SchurLambertW** | **320** | **10.9x** | 20mV | omega_fast2 |
| **SchurOmega3** | **322** | **10.8x** | 20mV | omega3のみ |
| **Decoupled** | **327** | **10.7x** | 20mV | ハイブリッド2x2 |
| WaveShaperFast | 208 | 16.8x | 高誤差 | Forward Euler |
| SquareShaper | 174 | 20.1x | 高誤差 | PNP近似 |

### omega関数マイクロベンチマーク

| 関数 | サイクル/呼出 |
|------|--------------|
| fast_exp | 51 |
| omega4 | 56 |
| DiodeLambertW.current | 60 |
| diode_iv | 75 |
| omega_fast | 47 |
| **omega_fast2** | **39** |
| **omega3** | **32** |

## 結論と推奨

### 推奨実装: **WaveShaperSchurLambertW** (320 cycles)

理由:
1. **最高速**: 364→320 cycles (12%高速化)
2. **高精度**: RMS 20mV (リファレンス比)
3. **安定**: Newton収束が確実
4. **シンプル**: 状態分岐なし

### 代替実装

- **SchurOmega3** (322 cycles): omega3のみで同等精度。omega_fast2のlog演算が不要な環境向け。
- **Decoupled** (327 cycles): ハイブリッド2x2。将来の並列化に有利。

### 不採用の手法

- **LUT exp**: 精度劣化、速度も遅い
- **完全分離型**: 発振問題
- **ニューラルネット**: 学習データ生成コスト、組込み不向き
- **WDF**: 概念的にはエレガントだが速度で劣る

## ファイル構成

```
docs/dsp/tb303/vco/
├── code/
│   ├── tb303_waveshaper.hpp      # オリジナル（リファレンス）
│   └── tb303_waveshaper_fast.hpp # 高速版（omega関数含む）
├── test/
│   ├── compare_cpp_python.py     # 精度検証スクリプト
│   └── schur_lambertw_comparison.png
├── run_benchmark.sh              # 統合ベンチマーク
└── OPTIMIZATION_REPORT.md        # このドキュメント

tests/
└── bench_waveshaper.cc           # Cortex-M4ベンチマーク

tools/
├── renode/
│   ├── bench_waveshaper.resc     # Renodeスクリプト
│   └── stm32f4_test.repl         # プラットフォーム定義
└── python/
    └── bench_waveshaper_plot.py  # グラフ生成
```

## ベンチマーク実行方法

```bash
# 全て実行（ビルド→Renode→Python精度検証→グラフ生成）
./docs/dsp/tb303/vco/run_benchmark.sh

# 個別実行
./docs/dsp/tb303/vco/run_benchmark.sh --build-only
./docs/dsp/tb303/vco/run_benchmark.sh --renode-only
./docs/dsp/tb303/vco/run_benchmark.sh --python-only
./docs/dsp/tb303/vco/run_benchmark.sh --plot-only
```

## 参考文献

1. D'Angelo, S., Välimäki, V., "Generalized MOOG Ladder Filter: Part II", IEEE/ACM TASLP, 2014
2. Fukushima, T., "Numerical computation of the Lambert W function", J. Comp. Applied Math., 2020
3. Werner, K., et al., "Wave Digital Filter Modeling of Circuits with Multiple Nonlinearities", DAFx, 2015
4. Chowdhury, J., "chowdsp_wdf", https://github.com/Chowdhury-DSP/chowdsp_wdf

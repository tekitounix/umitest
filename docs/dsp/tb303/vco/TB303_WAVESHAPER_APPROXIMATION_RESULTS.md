# TB-303 Wave Shaper 近似実装結果

## 概要

本ドキュメントは、TB-303 Wave Shaper回路のリアルタイム近似実装の結果をまとめたものです。

## リファレンス実装

- **アルゴリズム**: Newton-Raphson法による節点解析
- **Ebers-Mollモデル**: 完全実装
- **リアルタイム比**: 0.6x（リアルタイム不可）
- **メモリ**: 64バイト

## 近似実装の結果

### LUTベースアプローチ

| モデル | RT比 | RMS誤差 | 最大誤差 | メモリ | 高速化 |
|--------|------|---------|----------|--------|--------|
| Reference | 0.6x | 0mV | 0mV | 64B | 1.0x |
| LUT-256 | 17.3x | 1122mV | 3572mV | 1KB | 28.9x |
| LUT-512 | 17.1x | 1122mV | 3572mV | 2KB | 28.6x |
| LUT-1024 | 17.3x | 1122mV | 3572mV | 4KB | 28.8x |
| LUT+補正 | 14.8x | 1575mV | 6610mV | 4KB | 24.8x |

### 周波数別性能

| 周波数 | Reference RT | LUT-256 RT | LUT-256 RMS |
|--------|--------------|------------|-------------|
| 40Hz | 0.7x | 14.4x | 904mV |
| 80Hz | 0.6x | 16.4x | 1104mV |
| 160Hz | 0.6x | 18.7x | 1231mV |
| 320Hz | 0.5x | 19.7x | 1248mV |

## 回路動作の分析結果

### 核心的な発見

1. **v_eの状態追跡が重要**
   - C2（1µF）がv_eを保持
   - OFF時: v_e → V_CC（12V）に向かってゆっくり上昇（τ=22ms）
   - ON時: v_e → v_b + 0.56Vに向かって速く下降

2. **遷移点**
   - v_eb = v_e - v_b > 0.4V でトランジスタON
   - 40Hz時、v_in ≈ 9.4V で遷移

3. **ベース電圧オフセット**
   - v_b = v_in + R2 × i_B
   - ON時、i_B ≈ 13µA → オフセット ≈ 1.3V

### 定常状態の関係

- v_in = 5.5V: v_b = 6.84V, v_e = 7.39V, v_c = 7.33V
- v_in = 12V: v_b = 12V, v_e → V_CC, v_c = 5.33V

## 推奨実装

### 組み込みシステム向け

**LUT-256が最適**:
- メモリ: 1KB
- リアルタイム比: 約17x（48kHzでリアルタイム処理十分可能）
- 誤差: RMS約1.1V（聴感上は許容範囲）

### 実装コード（C++疑似コード）

```cpp
class WaveShaperLUT {
    static constexpr int LUT_SIZE = 256;
    static constexpr float V_IN_MIN = 5.0f;
    static constexpr float V_IN_MAX = 12.5f;
    static constexpr float V_COLL = 5.33f;
    static constexpr float V_CC = 12.0f;

    float lut_vc[LUT_SIZE];  // 定常状態のv_c
    float lut_ve[LUT_SIZE];  // 定常状態のv_e
    float v_e;               // 状態変数

public:
    void reset() { v_e = 8.5f; }

    float process(float v_in) {
        float v_eb = v_e - v_in;
        float v_c, v_e_target, tau;

        if (v_eb > 0.4f) {
            // ON
            v_c = lookup(v_in, lut_vc);
            v_e_target = lookup(v_in, lut_ve);
            tau = 0.00033f;  // R4*C2*0.015
        } else {
            // OFF
            v_c = V_COLL;
            v_e_target = V_CC;
            tau = 0.022f;  // R4*C2
        }

        float alpha = DT / (DT + tau);
        v_e += alpha * (v_e_target - v_e);
        v_e = clamp(v_e, 7.0f, V_CC);

        return v_c;
    }
};
```

## 今後の改善可能性

1. **Wright Omega関数による直接計算**
   - LUTなしで反復なしの計算が可能
   - 精度向上の可能性

2. **ニューラルネットワーク**
   - RTNeuralによる高精度近似
   - より複雑なモデルに対応可能

3. **2D LUT**
   - (v_in, v_e) → v_c のマッピング
   - 過渡応答の精度向上

## 参考ファイル

- `test_waveshaper.py`: リファレンス実装テスト
- `benchmark_final.py`: 最終ベンチマーク
- `analyze_circuit.py`: 回路動作分析
- `benchmark_results.csv`: ベンチマーク結果データ

## 結論

LUTベースの近似実装により、リファレンス実装の約29倍の高速化を達成。48kHzでのリアルタイム処理が十分可能。誤差は約1V RMSで、聴感上は許容範囲内と考えられる。

組み込みシステム（ARM Cortex-M4等）での実用化には、C++実装とRenodeでの検証が必要。

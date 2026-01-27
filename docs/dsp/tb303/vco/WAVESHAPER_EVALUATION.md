# TB-303 WaveShaper 実装評価

## 概要

TB-303の波形整形回路をEbers-Moll BJTモデルでシミュレーション。
Newton-Raphson法にSchur補完（j22ピボット）を適用した反復ソルバー。

**基本原理**: あらゆる箇所を近似計算に置き換えてもニュートン反復を十分に行えば補正される

## テスト条件

- サンプルレート: 48kHz
- テスト周波数: 22.5, 55, 110, 220, 440, 880 Hz
- リファレンス: WaveShaperReference (100回反復, std::exp)
- ベンチマーク: 1秒分のオーディオ処理（3回平均）

## 実装一覧と評価

### 現行モデル

| モデル | 反復数 | RMS誤差 | SINAD | 速度 | 評価 | 用途 |
|--------|--------|---------|-------|------|------|------|
| **Turbo** | 2 | 86mV | 38.1dB | 252x | ★5 | **推奨**。2回目でE-B計算を再利用 |
| TurboLite | 2 | 86mV | 38.1dB | 249x | ★5 | Turboと同等（Jacobian要素再利用） |
| Newton2 | 2 | 90mV | 37.7dB | 218x | ★4 | 標準的な高品質実装 |
| Hybrid | 2 | 96mV | 37.2dB | 207x | ★4 | 1回目Fast + 2回目Full |
| Fast2 | 2 | 99mV | 36.8dB | 213x | ★4 | 緩和ダンピング適用 |
| Fast1 | 1 | 129mV | 34.5dB | 387x | ★3 | 最高速、品質妥協 |

### JacobianMode解説

```cpp
enum class JacobianMode { Full, Fast, Hybrid, Turbo };
```

| Mode | 特徴 |
|------|------|
| Full | 毎回フルヤコビアン計算 |
| Fast | 緩和ダンピング適用（収束安定化） |
| Hybrid | 1回目Fast、2回目以降Full |
| Turbo | 2回反復、2回目でE-B計算結果を再利用（3 exp呼び出し） |

## 推奨選択ガイド

```
リアルタイム処理:
  ├─ 標準      → Turbo 1x (252x realtime, SINAD 22dB @880Hz)
  ├─ バランス  → Turbo 2x (126x realtime, SINAD 27dB @880Hz)
  └─ 高品質    → Turbo 4x (63x realtime, SINAD 32dB @880Hz)

オフライン処理 → Reference (100回反復)
```

## ベンチマーク結果グラフ

![Comparison](test/waveshaper_comparison.png)

## 技術詳細

### Schur補完によるソルバー

2x2システムを1x1に縮約:
```
[j11 j12] [dx1]   [f1]
[j21 j22] [dx2] = [f2]

→ dx2 = (f2 - j21/j11 * f1) / (j22 - j21*j12/j11)
→ dx1 = (f1 - j12*dx2) / j11
```

### Turbo最適化

```cpp
// 1回目: 通常計算
auto [v_eb1, exp_eb1, ...] = schur_step<DiodeIV>(v_in, v_a, v_b, c);

// 2回目: E-B計算を再利用（v_aは大きく変化しない前提）
auto [v_eb2, _, ...] = schur_step_reuse_eb<DiodeIV>(v_in, v_a, v_b, c, v_eb1, exp_eb1);
```

---

## Newton反復 vs オーバーサンプリング分析

### 検証の動機

Turboアルゴリズムでは高速化のためNewton反復を2回に抑えている。
これによりエイリアシングが増加しているなら、反復を増やせばオーバーサンプリング(OS)なしでも
同等の品質が得られるか？

### テスト条件

- 周波数: 880Hz（高周波でエイリアシングが顕著）
- リファレンス: 8x OS + Reference(100回反復)
- 品質指標: SINAD（信号対ノイズ+歪み比、高いほど良い）
- コスト基準: Turbo 1x = 1.0

### 結果

| モデル | SINAD [dB] | コスト比 | 備考 |
|--------|------------|----------|------|
| Turbo 1x | 22.1 | 1.0x | 基準 |
| Newton2 1x | 22.7 | 1.2x | +0.6 dB |
| Newton3 1x | 25.4 | 1.7x | +3.3 dB |
| **Ref(100) 1x** | **24.9** | **48.8x** | 100回反復でも+2.8 dB止まり |
| Turbo 2x | 25.9 | 4.0x | +3.8 dB |
| **Turbo 4x** | **30.7** | **15.8x** | **+8.6 dB, 最高品質** |

### 分析

**1. Newton反復による改善 (1x OS)**
- Fast1 → Turbo: +9.5 dB（大幅改善）
- Turbo → Newton2: +0.6 dB（微小）
- Newton2 → Newton3: +2.7 dB
- Newton3 → Ref(100): -0.5 dB（収束済み、改善なし）

**2. オーバーサンプリングによる改善 (Turbo)**
- 1x → 2x: +3.8 dB（コスト4倍）
- 1x → 4x: +8.6 dB（コスト16倍）

**3. コスト効率 (SINAD向上 / コスト増)**
- Turbo → Newton3: **4.41 dB per 1x cost**（最も効率的）
- Turbo 1x → 4x: 0.58 dB per 1x cost

### 結論

1. **Newton反復ではエイリアシングは解決しない**
   - Ref(100)（100回反復）でもSINAD 24.9 dB
   - Turbo 4x の30.7 dBには到達不可能
   - 反復は収束精度を上げるが、ナイキスト超の折り返しには無効

2. **オーバーサンプリングが本質的に有効**
   - 4x OSでのみ30 dB超のSINADを達成
   - 非線形処理で生成される高調波の折り返しを防ぐにはOSが必須

3. **推奨構成**
   - **リアルタイム優先**: Newton3 1x（コスト1.7x、SINAD 25.4 dB）
   - **バランス**: Turbo 2x（コスト4x、SINAD 25.9 dB）
   - **高品質優先**: Turbo 4x（コスト16x、SINAD 30.7 dB）

```
品質-コスト トレードオフ:

SINAD [dB]
   31 |                                   ● Turbo 4x
   30 |
   29 |
   28 |
   27 |
   26 |     ● Newton3 1x    ● Turbo 2x
   25 |                  ● Ref(100) 1x
   24 |
   23 |  ● Newton2 1x
   22 |● Turbo 1x
      +----------------------------------------→ コスト比
         1x    2x         4x              16x

→ Newton反復増加は低コストで効果的だが上限あり
→ 高品質にはオーバーサンプリングが必須
```

---
*Generated: 2026-01-28*
*Test: compare_all_models.py, verify_newton_vs_os.py*

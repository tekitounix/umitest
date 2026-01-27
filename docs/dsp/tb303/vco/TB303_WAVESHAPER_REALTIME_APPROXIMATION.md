# TB-303 Wave Shaper リアルタイム近似実装計画

## 概要

現在のSPICE精度実装は、Newton-Raphsonソルバーを使用した完全なEbers-Mollモデルに基づいており、Python実装では**リアルタイム比0.66x**（リアルタイム不可）という結果が得られています。

本ドキュメントでは、組み込みシステム（ARM Cortex-M4等）でのリアルタイム処理を実現するための近似手法を調査・計画します。

## 現状の問題点

| 項目 | 現状値 | 目標値 |
|------|--------|--------|
| リアルタイム比 | 0.66x | >10x |
| サンプルあたり反復 | 3-10回 | 0回（明示的解法） |
| 演算/サンプル | ~100 FLOPs × 反復回数 | <50 FLOPs |
| メモリ | 64バイト（状態変数） | <64バイト |

## アナログモデリング手法の調査

### 1. Wave Digital Filters (WDF)

#### 概要
Alfred Fettweis（1970-80年代）により開発された回路モデリング手法。回路素子を独立して離散化でき、モジュラー構成が可能。

#### 主要研究
- **Jatin Chowdhury** ([chowdsp_wdf](https://github.com/Chowdhury-DSP/chowdsp_wdf)): C++テンプレートメタプログラミングによるWDFライブラリ
- **Kurt Werner** et al.: オペアンプ回路のWDFモデリング
- **RT-WDF**: 任意トポロジー対応のリアルタイムWDFライブラリ

#### BJTトランジスタのWDF実装

Ebers-Mollモデルを使用したBJTのWDFモデリングには以下の課題がある：

1. **暗黙的方程式**: Ebers-Mollモデルの超越方程式はWD領域で解析的に解けない
2. **反復ソルバー必要**: 修正Newton-Raphson法が必要（従来の問題と同様）

**解決策：ベクトルウェーブ変数アプローチ**（[DAFx-23](https://www.dafx.de/paper-archive/2023/DAFx23_paper_60.pdf)）
- ニューラルネットワークによる明示的WD散乱方程式の実装
- 反復なしでリアルタイム実行可能
- 精度は標準回路シミュレータに匹敵

#### WDFの利点と課題

| 利点 | 課題 |
|------|------|
| モジュラー設計 | 非線形素子の扱いが複雑 |
| 素子単位の離散化 | BJTには反復ソルバーが必要 |
| 安定性が高い | トポロジー制約あり |
| SIMDに適した構造 | R-Typeアダプタで行列演算必要 |

### 2. Topology-Preserving Transform (TPT) / Zero-Delay Feedback

#### 概要
**Vadim Zavalishin**の「[The Art of VA Filter Design](https://www.native-instruments.com/fileadmin/ni_media/downloads/pdf/VAFilterDesign_1.1.1.pdf)」で詳述。双線形変換の一般化として、遅延フリーフィードバック構造を実現。

#### 特徴
- 良好な振幅・位相応答
- 時変パラメータに適した動作
- 非線形性への多様なオプション

#### TB-303への適用可能性
TPTは主にフィルタ回路向けに設計されているが、ウェーブシェイパーの一部（特にC1/R2/R3によるハイシェルフフィルタ部分）には適用可能。

### 3. Nodal DK Method（状態空間モデル）

#### 概要
**David Yeh**により提案、**Martin Holters**により拡張された手法。回路図から非線形状態空間システムを系統的に導出。

#### 主要研究
- [NDKFramework](https://github.com/dstrub18/NDKFramework): JUCE拡張によるリアルタイムシミュレーション
- [joaorossi/dkmethod](https://github.com/joaorossi/dkmethod): JUCEモジュール実装

#### 課題
- 大きな行列の逆行列計算が必要
- 時変システムでは係数の再計算が負担

### 4. Wright Omega / Lambert W 関数近似

#### 概要
ダイオードの指数関数特性を解析的に解くためのLambert W関数を、Wright Omega関数として再定式化し高速近似する手法。

#### 主要研究
- **D'Angelo, Gabrielli, Turchet**: "[Fast Approximation of the Lambert W Function for Virtual Analog Modelling](https://www.dafx.de/paper-archive/2019/DAFx2019_paper_5.pdf)" (DAFx-19)
- [Companion page with code](https://www.dangelo.audio/dafx2019-omega.html)

#### 手法
```
y + ln(y) = x  →  y = ω(x)  (Wright Omega関数)
```

ダイオード電流-電圧関係を明示的に解くことで、反復なしの実装が可能。

#### 4つの近似手法

| 手法 | 精度 | 計算コスト | 安定性 |
|------|------|------------|--------|
| 線形近似 | 低 | 最小 | 高 |
| 有理近似 | 中 | 低 | 高 |
| 反復改良1回 | 高 | 中 | 高 |
| 反復改良2回 | 最高 | 中〜高 | 高 |

#### TB-303への適用
PNPトランジスタの2つのダイオード接合（E-B, C-B）それぞれにWright Omega近似を適用可能。

### 5. Anti-derivative Anti-aliasing (ADAA)

#### 概要
**Parker, Zavalishin, Le Bivic** (DAFx-16)により提案。非線形関数の反微分を使用してエイリアシングを抑制。

#### 原理
```
y[n] = (F₁(x[n]) - F₁(x[n-1])) / (x[n] - x[n-1])
```
ここで F₁ は非線形関数 f の反微分。

#### WDFとの組み合わせ
- **[DAFx-20](https://dafx2020.mdw.ac.at/proceedings/papers/DAFx2020_paper_35.pdf)**: "Antiderivative antialiasing in nonlinear wave digital filters"
- WDFの非線形素子にADAAを適用

#### 利点と課題

| 利点 | 課題 |
|------|------|
| オーバーサンプリング不要または低倍率 | 反微分の導出が必要 |
| 計算コスト効率が良い | p/2サンプルの遅延 |
| 静的・動的非線形性に対応 | 特異点の処理が必要 |

### 6. ニューラルネットワーク近似

#### 概要
回路の入出力関係をニューラルネットワークで学習し、推論時に明示的に計算。

#### RTNeural
**Jatin Chowdhury**による[RTNeural](https://github.com/jatinchowdhury18/RTNeural)はリアルタイムオーディオ向けNNインファレンスライブラリ。

特徴：
- C++ STL、Eigen、XSIMDバックエンド
- 組み込みデバイス対応
- PyTorch/TensorFlowからの重み読み込み

#### 応用例
- **Chow Centaur**: RNNによるギターペダルエミュレーション
- **ベクトルWDF BJTモデル**: NNによる明示的散乱方程式

#### 組み込みシステムでの考慮事項
- メモリ: 小規模ネットワーク（<10kB）が必要
- レイテンシ: ブロック処理でスループット向上
- 精度: float32で十分な場合が多い

## 推奨実装戦略

### Phase 1: 簡略化Ebers-Mollモデル + Wright Omega近似

**目標**: 反復ソルバーの排除

1. **E-B接合のみのアクティブモデル化**
   - 通常動作ではC-B接合は逆バイアス（カットオフ）
   - I_CR ≈ 0 として簡略化

2. **Wright Omega関数による明示的解法**
   ```cpp
   // ダイオード電流の明示的計算
   // i = Is * (exp(v/Vt) - 1)
   // → v = Vt * omega(i/Is + ln(i/Is + 1))
   ```

3. **事前計算テーブルの活用**
   - 入力電圧範囲を限定（5V〜12V）
   - 線形補間テーブル（512〜1024点）

### Phase 2: 状態空間モデルの簡略化

**目標**: 行列演算の排除

1. **コンデンサ結合の解析**
   - C1（10nF）: 高域強調、比較的高速
   - C2（1µF）: エミッタ平滑化、低速

2. **時定数分離**
   ```
   τ_C1 = R3 × C1 = 100µs (fc ≈ 1.6kHz)
   τ_C2 = R4 × C2 = 22ms  (fc ≈ 7Hz)
   ```

3. **マルチレートアプローチ**
   - C2の更新を低レート（1/16〜1/32）で実行
   - C1は毎サンプル更新

### Phase 3: ADAA統合（オプション）

**目標**: 低オーバーサンプリングでのエイリアシング抑制

1. **トランジスタ非線形性への1次ADAA適用**
2. **2xオーバーサンプリング併用**（96kHz動作）

### Phase 4: ニューラルネットワーク代替（最終手段）

**目標**: 最高精度での最速実装

1. **学習データ生成**: SPICE精度モデルから
2. **ネットワーク構成**:
   - 入力: v_in, v_c1_prev, v_c2_prev
   - 出力: v_out
   - 構造: Dense 8→16→8→1 または GRU 8
3. **RTNeuralによる推論**

## 実装優先度

| 優先度 | 手法 | 期待効果 | 実装難易度 |
|--------|------|----------|------------|
| 1 | Wright Omega + 簡略化Ebers-Moll | 5〜10x高速化 | 中 |
| 2 | マルチレートC2更新 | 追加2〜4x高速化 | 低 |
| 3 | LUT補間 | 追加2x高速化 | 低 |
| 4 | NN代替 | 最大50x高速化 | 高 |

## 提案するAPI設計

```cpp
namespace tb303 {

// リアルタイム向け近似実装
class WaveShaperRT {
public:
    // 初期化
    void reset();

    // サンプル処理（floatバージョン）
    float process(float v_in);

    // ブロック処理（SIMD最適化）
    void processBlock(const float* in, float* out, size_t n);

    // 精度/速度トレードオフ設定
    enum class Quality { Fast, Balanced, Accurate };
    void setQuality(Quality q);

private:
    // 状態変数（最小化）
    float v_c1_;    // C1電圧
    float v_c2_;    // C2電圧（低レート更新）

    // 事前計算テーブル
    static constexpr size_t LUT_SIZE = 512;
    float omega_lut_[LUT_SIZE];
};

} // namespace tb303
```

## 参考文献

### Wave Digital Filters
1. [chowdsp_wdf Library](https://github.com/Chowdhury-DSP/chowdsp_wdf) - Jatin Chowdhury
2. [Explicit Vector WDF Modeling of BJT Circuits](https://www.dafx.de/paper-archive/2023/DAFx23_paper_60.pdf) - DAFx-23
3. [Wave Digital Modeling of Nonlinear 3-terminal Devices](https://link.springer.com/article/10.1007/s00034-019-01331-7)

### Topology-Preserving Transform
4. [The Art of VA Filter Design](https://www.native-instruments.com/fileadmin/ni_media/downloads/pdf/VAFilterDesign_1.1.1.pdf) - Vadim Zavalishin

### Nodal DK Method
5. [NDKFramework](https://github.com/dstrub18/NDKFramework)
6. [Physical Modelling with Nodal DK Method](https://www.researchgate.net/publication/263013830_Simulation_Framework_for_Analog_Audio_Circuits_based_on_Nodal_DK_Method) - Holters & Zölzer

### Wright Omega / Lambert W
7. [Fast Approximation of the Lambert W Function](https://www.dafx.de/paper-archive/2019/DAFx2019_paper_5.pdf) - DAFx-19
8. [Implementation Code](https://www.dangelo.audio/dafx2019-omega.html)

### Anti-derivative Anti-aliasing
9. [ADAA in Nonlinear WDFs](https://dafx2020.mdw.ac.at/proceedings/papers/DAFx2020_paper_35.pdf) - DAFx-20
10. [Practical Considerations for ADAA](https://ccrma.stanford.edu/~jatin/Notebooks/adaa.html) - Jatin Chowdhury

### Neural Networks
11. [RTNeural Library](https://github.com/jatinchowdhury18/RTNeural) - Jatin Chowdhury
12. [RTNeural Paper](https://ccrma.stanford.edu/~jatin/rtneural/)

## 次のステップ

1. Wright Omega近似のC++実装とベンチマーク
2. 簡略化Ebers-Mollモデルの検証
3. SPICE精度モデルとの比較テスト
4. ARM Cortex-M4でのプロファイリング

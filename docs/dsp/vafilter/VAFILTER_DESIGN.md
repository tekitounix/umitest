# VAフィルター設計手法
## TPT/ZDF/BLT方式による仮想アナログフィルター実装ガイド

---

## 1. 概要

本ドキュメントは、KVR Audio DSPフォーラムで議論されてきたVA（Virtual Analog）フィルター設計手法をまとめたものである。主な情報源は以下の資料である：

- **Vadim Zavalishin著「The Art of VA Filter Design」**（Native Instruments）
- **Andy Simper（Cytomic）によるSVFおよびSKF技術文書群**
- **mystran（Teemu Voipio）「Cheap non-linear zero-delay filters」KVRスレッド（t=349859）**

これらの手法は、デジタル環境でアナログフィルターの特性を正確に再現するための業界標準的アプローチとなっている。特にmystranの手法は、非線形フィルターを効率的に実装するための重要な貢献であり、多くの商用プラグインで採用されている。

---

## 2. 離散化手法（Discretization Methods）

アナログフィルターをデジタル実装に変換するための数学的手法。

### 2.1 BLT（Bilinear Transform）

**双一次変換**。連続時間システムを離散時間システムに変換する標準的手法。

s領域からz領域への変換式：

```
s = (2/T) × (z-1)/(z+1)
```

この変換は周波数軸のワーピングを引き起こすため、カットオフ周波数のプリワーピングが必要となる。台形積分法と数学的に等価であり、以下の特性を持つ：

- 安定なアナログシステムは安定なデジタルシステムに変換される
- 周波数応答は保存されるが、周波数軸が非線形にワープする
- ωa = (2/T) × tan(ωd × T/2) の関係でアナログ周波数とデジタル周波数が対応

### 2.2 TPT（Topology-Preserving Transform）

**トポロジー保存変換**。アナログ回路のブロック図構造（トポロジー）を維持したまま、積分器を台形積分器に置き換えることでデジタル化する手法。

伝達関数レベルでBLTを適用する従来手法（Direct Form）との違い：

| 観点 | Direct Form BLT | TPT |
|------|-----------------|-----|
| 変換対象 | 伝達関数 H(s) | ブロック図（積分器） |
| 状態変数 | 抽象的 | 物理量に対応 |
| パラメータ変調 | 不連続・クリック発生 | 滑らか・安定 |
| 非線形拡張 | 困難 | 自然に組み込み可能 |

### 2.3 ZDF（Zero-Delay Feedback）

**ゼロ遅延フィードバック**。TPT実装において生じる問題を解決する手法。

従来のデジタルフィルター実装ではフィードバックループに1サンプル遅延（z⁻¹）が必要だったが、ZDF手法では暗黙的な方程式を代数的に解くことでこの遅延を排除する。

```
従来: y[n] = f(x[n], y[n-1])  ← 1サンプル遅延のフィードバック
ZDF:  y[n] = f(x[n], y[n])    ← 暗黙的方程式を解く
```

これにより、アナログフィルターに近い周波数応答と位相応答が得られる。

### 2.4 台形積分法 vs Backward Euler

**Andy Simper（Cytomic）** による比較：

| 特性 | 台形積分法（Trapezoidal） | Backward Euler |
|------|--------------------------|----------------|
| エネルギー保存 | 線形の場合エネルギー保存 | エネルギー散逸 |
| 自己発振 | 周波数に関係なく正確に追従 | 高周波で減衰、中心に向かって螺旋 |
| 安定性 | 急峻な非線形性で発散の可能性 | より安定だがダイナミクスに影響 |

> "Trapezoidal integration is energy preserving in the perfectly linear case. When stiff non-linearities are at play then these can push you over the edge and make things blow up. Backwards Euler dissipates energy, so it is more stable, but this will ruin things like self oscillation tracking over frequency."

**結論**: 音楽用途では台形積分法が推奨される。自己発振の正確な追従が重要なためである。

---

## 3. フィルタートポロジー（Filter Topologies）

アナログ回路に由来するフィルターの構造。離散化手法とは独立した概念。

### 3.1 1極フィルター（One-Pole Filter）

最も基本的なフィルター構造。RC回路で実現される。

- 1つの積分器で構成
- -6dB/oct のロールオフ
- LP/HP/AP出力が得られる

### 3.2 SVF（State Variable Filter）

**状態変数フィルター**。2つの積分器とフィードバックで構成される2次フィルター。

```
入力 → [HP] → 積分器1 → [BP] → 積分器2 → [LP]
         ↑___________________________|
              k × (resonance feedback)
```

特徴：
- LP、HP、BP、Notch、AP出力を同時に得られる
- レゾナンス制御が容易
- Oberheim SEM、EMS VCS3などで採用

### 3.3 SKF（Sallen-Key Filter）

**サレンキーフィルター**。オペアンプを用いた2次フィルター構造。

特徴：
- 入力混合によるマルチモード実現
- Korg MS-20（HP部）などで採用
- SVFとは異なる飽和特性

### 3.4 ラダーフィルター（Ladder Filter）

複数の1極フィルターをカスケード接続し、グローバルフィードバックをかける構造。

**Moogラダー（トランジスタラダー）：**
- 4つの1極LPFをカスケード
- -24dB/oct のロールオフ
- 各段はバッファで分離されている
- Minimoog、Prophet-5などで採用

**ダイオードラダー（TB-303タイプ）：**
- ダイオード対による非線形特性
- Moogラダーとは異なるレゾナンス特性
- **段間が結合している**（バッファなし）
- カットオフ電圧がオーディオ信号と混合
- 通常は自己発振しない（改造で可能）
- Roland TB-303、EMS VCS3などで採用
- 詳細は **Appendix C** を参照

### 3.5 トポロジーと離散化の関係

任意のフィルタートポロジーに対して、任意の離散化手法を適用できる：

| | ナイーブ（Euler） | BLT Direct Form | TPT/ZDF |
|---|---|---|---|
| 1極 | ○ | ○ | ○ |
| SVF | △（不安定） | ○ | ◎ |
| SKF | △（不安定） | ○ | ◎ |
| ラダー | △（不安定） | △（変調時問題） | ◎ |

◎：推奨、○：可能、△：問題あり

---

## 4. 台形積分器（Trapezoidal Integrator）

### 4.1 基本原理

台形積分法は、積分を台形の面積で近似する数値積分手法である。デジタルフィルターにおいて、アナログ積分器の最も優れた離散化手法として知られる。

**ナイーブ（オイラー）積分器の出力：**
```
y[n] = y[n-1] + x[n]
```

**台形積分器の出力：**
```
y[n] = y[n-1] + (x[n] + x[n-1])/2
```

### 4.2 TDF2（Transposed Direct Form II）積分器

Vadim Zavalishinの著書で推奨される形式。状態変数が1つで済み、ZDFの方程式が簡潔になる。

**実装（g = ωc×T/2、プリワープ済み）：**
```c
v = g * x + s;      // 出力計算
s = g * x + v;      // 状態更新
```

ここで、`s`は積分器の状態、`g`はプリワープ済みのカットオフ係数である。

### 4.3 カットオフ・プリワーピング

BLTによる周波数ワーピングを補償するため、希望するカットオフ周波数を事前に変換する：

```
g = tan(π × fc / fs)
```

ここで、`fc`はカットオフ周波数、`fs`はサンプリング周波数である。

---

## 5. TPT 1極フィルター実装

### 5.1 アナログプロトタイプ

RC回路による1極ローパスフィルターの微分方程式：

```
dy/dt = ωc × (x - y)
```

伝達関数：
```
H(s) = ωc / (s + ωc)
```

### 5.2 TPT実装

**係数計算：**
```c
g = tan(PI * fc / fs);
G = g / (1.0 + g);
```

**処理ループ：**
```c
// サンプルごとの処理
v = (x - s) * G;    // ZDF解決済み出力
lp = v + s;         // ローパス出力
hp = x - lp;        // ハイパス出力
s = v + lp;         // 状態更新
```

### 5.3 マルチモード出力

1極フィルターから以下の出力が得られる：
- **LP（ローパス）**: `lp`
- **HP（ハイパス）**: `hp = x - lp`
- **AP（オールパス）**: `ap = lp - hp`

---

## 6. TPT SVF実装（2極状態変数フィルター）

### 6.1 構造概要

SVFは2つの積分器を直列接続し、フィードバックでレゾナンスを制御する構造である。

```
HP → [積分器1] → BP → [積分器2] → LP
         ↑__________________|
               (feedback)
```

### 6.2 Andy Simper（Cytomic）方式

Cytomicの技術文書「SvfLinearTrapOptimised2.pdf」で詳述されている最適化実装。

**係数計算：**
```c
g = tan(PI * fc / fs);
k = 1.0 / Q;                      // Q: レゾナンス（0.5〜∞）
a1 = 1.0 / (1.0 + g * (g + k));
a2 = g * a1;
a3 = g * a2;
```

**処理ループ：**
```c
// 入力混合形式
v3 = x - ic2eq;
v1 = a1 * ic1eq + a2 * v3;
v2 = ic2eq + a2 * ic1eq + a3 * v3;

// 状態更新
ic1eq = 2.0 * v1 - ic1eq;
ic2eq = 2.0 * v2 - ic2eq;
```

### 6.3 出力の取得

```c
lp = v2;                    // ローパス
bp = v1;                    // バンドパス
hp = x - k * v1 - v2;       // ハイパス
notch = x - k * v1;         // ノッチ（バンドリジェクト）
peak = x - k * v1 - 2*v2;   // ピーク
ap = x - 2.0 * k * v1;      // オールパス
```

### 6.4 マルチモード出力の混合

係数m0, m1, m2を用いて任意の周波数応答を実現できる：

```
output = m0 * hp + m1 * bp + m2 * lp
```

**代表的な混合係数：**

| フィルタータイプ | m0 (HP) | m1 (BP) | m2 (LP) |
|------------------|---------|---------|---------|
| ローパス | 0 | 0 | 1 |
| ハイパス | 1 | 0 | 0 |
| バンドパス | 0 | 1 | 0 |
| ノッチ | 1 | 0 | 1 |
| オールパス | 1 | -k | 1 |
| ピーク（ブースト） | 1 | k×A | 1 |
| ローシェルフ | 1 | k×√A | A |
| ハイシェルフ | A | k×√A | 1 |

---

## 7. 手法の比較

| 特性 | ナイーブ実装 | Direct Form BLT | TPT/ZDF |
|------|--------------|-----------------|---------|
| 周波数応答精度 | 低い（高域で大きな誤差） | 高い（プリワープで正確） | 高い（プリワープで正確） |
| パラメータ変調 | 不安定になりやすい | クリック・ノイズ発生 | 安定・滑らか |
| 数値精度 | 低域で問題あり | 低域で問題あり | 単精度でも高精度 |
| 非線形拡張 | 困難 | 困難 | 自然に組み込み可能 |
| 計算コスト | 最小 | 低 | やや高い |
| 状態変数の直感性 | 低い | 低い | 高い（物理量に対応） |

---

## 8. TPTラダーフィルター実装

### 8.1 線形TPTラダー

セクション3.4で説明したラダートポロジーをTPT/ZDFで実装する。

### 8.2 TPTラダー実装の要点

1. 4つのTPT 1極フィルターを使用
2. グローバルフィードバックのZDF解決が必要
3. フィードバック係数 k = 4 × resonance（0〜1）
4. 非線形モデルでは各段に飽和関数（tanh等）を挿入

**線形ラダーの係数計算：**
```c
g = tan(PI * fc / fs);
G = g / (1.0 + g);
k = resonance * 4.0;

// ZDF解決のための係数
Gcomp = 1.0 / (1.0 + k * G*G*G*G);
```

### 8.3 ダイオードラダー（TB-303タイプ）実装

ダイオードを用いたラダー構造で、Moogラダーとは異なる非線形特性を持つ。

- 各段のダイオード対の特性をモデル化
- Newton-Raphson法などの反復解法が必要
- より「アシッド」なサウンド特性

---

## 9. 非線形モデリング：Cheap Non-linear Zero-Delay Filters

**mystran（Teemu Voipio）** が2012年にKVRフォーラムで発表した効率的な非線形ZDFフィルター実装手法。スレッド「Cheap non-linear zero-delay filters」（t=349859）が原典。

> "Ok, so I didn't want to pollute that other thread, so I'm starting a new one. In that other thread I mentioned you can linearize a filter around its known state and get reasonable results for not much overhead."

### 9.1 課題：非線形ZDFの暗黙的方程式

線形フィルターではZDF方程式は代数的に解けるが、非線形要素を含むと：

```
y[n+1] = s[n] + f × tanh(x[n] - k × y[n+1])
```

`y[n+1]` が非線形関数内に現れるため、Newton-Raphson等の反復解法が必要になり計算コストが高い。

### 9.2 核心的アイデア：等価トランスコンダクタンス

非線形関数 `tanh(x)` を、原点と既知の動作点を通る直線で近似：

```
T(x) = tanh(x) / x
```

この `T(x)` を「等価トランスコンダクタンス」として使用し、非線形項を線形項に置換する。

**正式名称**: Semi-implicit fixed-pivot trapezoidal method（Andy Simper命名）

### 9.3 半サンプル遅延による定式化

TPTでは状態 `s[n]` は `y[n+0.5]` を表す。この性質を利用：

```
入力の半サンプル遅延: x[n-0.5] = (x[n] + x[n-1]) / 2
```

1極非線形フィルターの場合：

```c
t = T(0.5*(x[n] + x[n-1]) - s[n]);  // 既知の値のみ
y = (s[n] + f*t*x[n]) / (1 + f*t);  // 線形方程式として解ける
s[n+1] = s[n] + 2*f*t*(x[n] - y);
```

### 9.4 mystranのオリジナル実装（トランジスタラダー）

KVRスレッドより（2012年、CC0ライセンス）：

```c
//// LICENSE TERMS: Copyright 2012 Teemu Voipio
//
// You can use this however you like for pretty much any purpose,
// as long as you don't claim you wrote it. There is no warranty.

// input delay and state for member variables
float zi;
float s[4] = { 0, 0, 0, 0 };

// tanh(x)/x approximation, flatline at very high inputs
// so might not be safe for very large feedback gains
// [limit is 1/15 so very large means ~15 or +23dB]
float tanhXdX(float x) {
    float a = x*x;
    // Pade-approx for tanh(sqrt(x))/sqrt(x)
    return ((a + 105.0f)*a + 945.0f) / ((15.0f*a + 420.0f)*a + 945.0f);
}

void process(float cutoff, float resonance, 
             float *in, float *out, int samples) {
    
    float f = tanf(M_PI * cutoff);
    float r = resonance * 4.0f;  // 0-4 for self-oscillation
    
    for (int i = 0; i < samples; i++) {
        float x = in[i];
        
        // input with half-sample delay
        float ih = 0.5f * (x + zi); zi = x;
        
        // evaluate transconductances from previous state
        float t0 = tanhXdX(ih - r*s[3]);
        float t1 = tanhXdX(s[0]);
        float t2 = tanhXdX(s[1]);
        float t3 = tanhXdX(s[2]);
        float t4 = tanhXdX(s[3]);
        
        // solve feedback as linear system
        float g0 = 1.0f / (1.0f + f*t1);
        float g1 = 1.0f / (1.0f + f*t2);
        float g2 = 1.0f / (1.0f + f*t3);
        float g3 = 1.0f / (1.0f + f*t4);
        
        float f3 = f*t3*g3;
        float f2 = f*t2*g2*f3;
        float f1 = f*t1*g1*f2;
        float f0 = f*t0*g0*f1;
        
        // solve for y3 (4th stage output)
        float y3 = (g3*s[3] + f3*(g2*s[2] + f2*(g1*s[1] 
                   + f1*(g0*s[0] + f0*ih)))) / (1.0f + r*f0);
        
        // solve remaining outputs
        float xx = t0*(ih - r*y3);
        float y0 = g0*s[0] + f*g0*xx;
        xx = t1*y0;
        float y1 = g1*s[1] + f*g1*xx;
        xx = t2*y1;
        float y2 = g2*s[2] + f*g2*xx;
        xx = t3*y2;
        y3 = g3*s[3] + f*g3*xx;
        
        // update state
        s[0] += 2.0f*f*(t0*(ih - r*y3) - t1*y0);
        s[1] += 2.0f*f*(t1*y0 - t2*y1);
        s[2] += 2.0f*f*(t2*y1 - t3*y2);
        s[3] += 2.0f*f*(t3*y2 - t4*y3);
        
        out[i] = y3;
    }
}
```

### 9.5 mystran自身による手法の説明

> "On paper it's less accurate than fitting a linear curve directly to the tangent of the tanh() for slowly changing signals, since we are fitting a linear curve from the origin to the known operating point, but unlike the tangent fitting method, this tolerates violations of the 'signal changes slowly' assumption much better; we might feed a bit too much or too little current, but most of the time the results are relatively sane (which cannot be said about tangent fitting, which can run into crazy paradoxes)."

> "note that without the correction step, the cost here is almost exactly the cost of solving a linear zero-delay filter + the cost of evaluating the non-linearities once."

### 9.6 Newton-Raphson法との比較

| アプローチ | 線形化の基準 | 反復 | 極端な設定での挙動 |
|-----------|-------------|------|-------------------|
| Newton-Raphson | 接線（f'(x)使用） | 必要 | 収束すれば正確 |
| mystran手法 | 原点-動作点を通る直線 | 不要 | やや不正確だが安定 |

### 9.7 ステレオ処理への拡張

mystranが提案した手法：

```c
// ピタゴラスノルムでトランスコンダクタンスを評価
float norm = sqrtf(L*L + R*R);
float t = tanhXdX(norm);
// 両チャンネルに同じtを適用
```

> "if you want to filter stereo signals, you can treat the signals and states as vectors, and evaluate 'transconductances' with the Pythagorean norm sqrt(L*L+R*R)... unlike a dual-mono implementation, distortion tends to localize at the input signals rather than the individual speakers."

### 9.8 Heun法による精度向上（Correction Step）

```c
// 1. Prediction
solve_linear_system();
store_predicted_state();

// 2. Correction（x[n+0.5]を使用してトランスコンダクタンス再計算）
recalculate_with_new_state();
solve_linear_system_again();

// 3. Average（2次精度を達成）
average_results();
```

> "In practice the correction step roughly doubles the CPU use. In cases where the prediction works well (and most of the time it does), it's probably better idea to double the oversampling instead."

### 9.9 適用範囲と商用実績

**最適条件**:
- オーバーサンプリング併用時
- tanh系の対称非線形性
- 適度なレゾナンス設定

**制限**:
- +23dB以上のフィードバックゲインで近似破綻
- 非対称非線形性（ダイオード等）には追加の工夫が必要

**商用採用例**:
- u-he製品（Urs Heckmann）
- Cytomic The Drop（Andy Simper）
- Musical Entropy Guitar Gadgets

**先行研究**:
Andy Simperによると、この手法（f(x)/xによる非線形性の処理）は回路シミュレータのソースコードに既に存在していた：

> "In emails with Mystran he has said that his method of using f(x)/x for non-linearities is already contained in the source code of certain circuit simulation packages, possibly as fallback methods for stability."

mystranによる追加情報：

> "In a book about circuit simulation methods there was a comparison between various iterative methods, including fixed-point, secant, this method and Newton. The author referred to the method mentioned here as the 'BLAS-3' method and basically said that it's not been studied too carefully, but suspected the convergence (with iteration) should be somewhere between linear and that of secant method."

**半サンプル遅延の周波数補正（Andy Simper）**:

高カットオフ周波数での周波数応答のブーストを補正：

```c
// 周波数依存の補間係数
m = min(1 - f/2, 0.5);

// 補正された半サンプル入力
x_half = m * x[n] + (1 - m) * x[n-1];

// トランスコンダクタンスの評価
t = T(x_half - s[n-1]);
```

---

### 9.10 代数的解法：Maximaによる連立方程式の解決

mystranとAndy Simperがスレッドで示した、CAS（Computer Algebra System）を使用した解法：

**Maximaでの例（4段ラダー）：**
```maxima
declare([s0,s1,s3,s4,in], mainvar);
solve([
    y0 = (s0 + f * t0 * (in - r*y3)) * g0,
    y1 = (s1 + f * t1 * y0) * g1,
    y2 = (s2 + f * t2 * y1) * g2,
    y3 = (s3 + f * t3 * y2) * g3
], [y0, y1, y2, y3]);
```

**Andy Simperによる手動代入法：**

```
y3 = g3*(s3 + f*t3*y2)
  → y2を代入
y3 = g3*(s3 + f*t3*g2*(s2 + f*t2*y1))
  → y1を代入
y3 = g3*(s3 + f*t3*g2*(s2 + f*t2*g1*(s1 + f*t1*y0)))
  → y0を代入
y3 = g3*(s3 + f*t3*g2*(s2 + f*t2*g1*(s1 + f*t1*g0*(s0 + f*t0*(in - r*y3)))))

→ y3について解く（右辺のy3を分離）
y3 = (g3*s3 + f3*s2 + f2*s1 + f1*s0 + f0*in) / (1 + r*f0)

where:
  f3 = f*t3*g2*g3
  f2 = f*t2*g1*f3
  f1 = f*t1*g0*f2
  f0 = f*t0*f1
```

### 9.11 「Cheap」アプローチの分類

KVRスレッドで議論された「cheap」な非線形ZDF実装には、主に2つのアプローチがある：

#### A. mystran手法（Semi-implicit fixed-pivot）

非線形項を `tanh(x)/x` で線形化し、ZDFシステム**内部**に組み込む：

```c
// 等価トランスコンダクタンスを計算
t = tanh(state_prev) / state_prev;

// 線形化されたZDFシステムを解く（非線形性が係数として含まれる）
y = solve_linear_system_with_transconductance(t);
```

#### B. 線形ZDF + 事後非線形（Straightforward Cheap）

線形ZDFを解いた**後**に非線形性を適用：

```c
// 線形ZDFシステムを解く
y_linear = solve_linear_zdf();

// フィードバック推定に非線形を適用
y = clip(x - k * y_linear);
```

#### 比較

| 特性 | mystran手法 | 線形ZDF + 事後非線形 |
|------|-------------|---------------------|
| 精度（緩やかな信号変化時） | より高い | やや低い |
| 精度（極端な設定時） | 劣化の可能性 | 単純だが一貫 |
| 計算コスト | 線形ソルバー + tanh/x評価 | 線形ソルバー + clip |
| カットオフ変調時の挙動 | より正確 | やや不正確 |

Z1202の見解（スレッドより）：

> "mystran's approach should give you better results at not so extreme parameter values, whereas at the more extreme settings a 'straightforward cheap' approach works better."

---

### 9.12 karrikuhのDiode Ladder実装

**karrikuh（Dominique Wurtz）** による TB-303/EMS VCS3 ダイオードラダーフィルター実装。「Diode ladder filter」スレッド（t=346155）で発表、MITライセンス。

この実装は**線形ZDF + 事後非線形**アプローチを採用：

```c
// 線形ZDFを解く
float y5 = (g*x + s) / (1.0f + g*k);

// 入力クリッピング（事後に適用）
const float y0 = clip(x - k*y5);
y5 = g*y0 + s;

// clip関数
static float clip(const float x) {
    return x / (1.0f + fabsf(x));
}
```

**特徴**：
- 台形積分法によるZDF実装
- Mathematicaで導出された代数的解
- 単一の非線形要素（入力クリッピングのみ）
- フィードバックパスにHPFを追加（TB-303の特性再現）
- 2倍以上のオーバーサンプリング推奨

**mystran手法との違い**：
karrikuhの実装は各段にtanh(x)/xを適用するmystran手法ではなく、線形ZDFを解いた後に単一のクリッパーを適用する簡略化されたアプローチ。ダイオードラダーの結合された段構造により、Mathematicaでの代数的解決が複雑になるため、この簡略化が選択された。

---

### 9.13 その他の飽和関数

アナログ回路の非線形性を再現するため、積分器の入力または出力に飽和関数を挿入する：

**代表的な飽和関数：**

```c
// tanh（最も一般的）
y = tanh(x);

// tanh近似（計算が軽い）
y = x / (1.0 + fabs(x));

// ソフトクリップ
y = x / sqrt(1.0 + x*x);

// ハードクリップ
y = fmax(-1.0, fmin(1.0, x));
```

**OTAの非線形性とtanhの妥当性**

aciddose（KVRスレッドPage 10）による指摘：

> "tanh isn't even a very reasonable approximation. the error in the code posted is so high that even with the proper functions and oversampling it still doesn't produce very accurate results."

ただし、mystranとAndy Simperは以下のように反論：

> "For something like CA3080 (or even LM13700 as long as you leave the diode linearization unconnected) tanh() is quite reasonably model, since the whole thing is just another long-tailed pair plus a few current mirrors."

**結論**: tanh()の妥当性はモデル対象のOTAチップに依存する。CA3080/LM13700系では妥当だが、CEM3320などではより詳細なモデリングが必要な場合がある。

**CEM3320の特性（Urs Heckmann、u-he）**:

> "If you read the original patent, a CEM 3320 model wouldn't have many tanh() terms. The distortion is asymmetric, but each stage is inverting, thus some distortion cancels out, but at low cutoffs you get considerable modulation bleed-through in a lowpass setting."

**aciddoseによるOTAモデリングの知見**:

- LM13600/LM13700、CA3080、BA662、IR3109 は類似のコア設計
- 入力に約-0.05のバイアスを加えると偶数次高調波が発生
- 各段のバッファ（JFET、NPNトランジスタ、オペアンプ等）も音に影響
- Roland OTAフィルター（SH-101、MC-202、Juno-60）のフィードバックは+12dBでダイオードクリップ

**代替の非線形関数（aciddose）**:

```c
// tanhよりも軽量で類似の高調波を生成
y = 1 - x * fabs(x);

// より柔軟な飽和関数
float tanhd(float x, float d, float s) {
    return 1.0f - s * (d + 1.0f) * x*x / (d + x*x);
}
```

### 9.14 反復解法（高精度が必要な場合）

ZDFフィードバックループに非線形要素を含む場合の解法：

**1. Newton-Raphson法**
```c
// 反復解法（収束が速いがヤコビアン計算が必要）
for (int i = 0; i < max_iter; i++) {
    f = compute_residual(y);
    df = compute_jacobian(y);
    y = y - f / df;
    if (fabs(f) < tolerance) break;
}
```

**2. 固定点反復法**
```c
// 実装が簡単だが収束が保証されない
for (int i = 0; i < max_iter; i++) {
    y_new = g(y);
    if (fabs(y_new - y) < tolerance) break;
    y = y_new;
}
```

**3. 線形予測による初期推定**
```c
// 前サンプルからの外挿で収束を加速
y_init = 2.0 * y_prev - y_prev2;
```

### 9.15 非線形SVFの実装例（反復法）

```c
// 積分器入力に飽和を適用
v1 = tanh(a1 * ic1eq + a2 * v3);
v2 = tanh(ic2eq + a2 * ic1eq + a3 * v3);

// または積分器状態に飽和を適用
ic1eq = tanh(2.0 * v1 - ic1eq);
ic2eq = tanh(2.0 * v2 - ic2eq);
```

---

## 10. 参考文献・リソース

### 10.1 主要文献

1. **Vadim Zavalishin, "The Art of VA Filter Design" rev. 2.1.2**
   - TPT/ZDF手法の包括的解説書
   - Native Instrumentsより無料公開
   - URL: https://www.native-instruments.com/fileadmin/ni_media/downloads/pdf/VAFilterDesign_2.1.2.pdf

2. **Andy Simper (Cytomic) Technical Papers**
   - SvfLinearTrapOptimised2.pdf - 最適化SVF実装
   - SkfLinearTrapOptimised2.pdf - Sallen-Key実装
   - SvfInputMixing.pdf - 入力混合SVF
   - URL: https://cytomic.com/technical-papers

### 10.2 関連KVRスレッド

- **"Cheap non-linear zero-delay filters" (t=349859)** - mystranによる非線形ZDF手法の詳細議論。Pivotal Linearization手法の原典。
- **"Diode ladder filter" (t=346155)** - karrikuh（Dominique Wurtz）によるTB-303/VCS3ダイオードラダーのZDF実装。
- "Book: The Art of VA Filter Design" (t=350246)
- "C++ port of Andy Simper's implementation of State Variable Filters" (t=474128)
- "Cytomic SVF analysis thread" (t=606008)
- "Fast Modulation of Filter Parameters" (t=523958)

### 10.3 実装コード

- **karrikuh Diode Ladder Filter**: https://pastebin.com/THe5JG5f （MIT License）
- **mystran Transistor Ladder**: KVR t=349859 first post （CC0 License）

### 10.4 実装例（オープンソース）

- **GitHub: FredAntonCorvest/Common-DSP** - Cytomic SVFのC++ポート
- **GitHub: mjarmy/dsp-lib** - SVFと他のDSPアルゴリズム
- **rs-met.com** - Robin SchmidtによるVAフィルター実装

---

## 11. 実装チェックリスト

**基本TPT/ZDF実装**:
- [ ] カットオフ係数に `tan(π×fc/fs)` のプリワーピングを適用
- [ ] TDF2積分器を使用（状態変数1つ/積分器）
- [ ] ZDF方程式を導出し、暗黙的なフィードバックを解決
- [ ] 状態更新は出力計算の後に行う
- [ ] 高レゾナンス時の安定性を確認（k < 2で安定）
- [ ] パラメータ変調テスト：急激なカットオフ変化でクリック・発散がないこと

**非線形実装（mystran手法）**:
- [ ] tanhXdX()関数の実装（Padé近似推奨）
- [ ] 入力の半サンプル遅延: `ih = 0.5*(x[n] + x[n-1])`
- [ ] 前サンプルの状態からトランスコンダクタンスを計算
- [ ] 線形ZDFソルバーを等価トランスコンダクタンスで解く
- [ ] 2〜4倍のオーバーサンプリング検討
- [ ] 高精度が必要な場合はHeun法の補正ステップを検討

---

## Appendix A: 完全な1極TPTフィルター実装

```c
typedef struct {
    float s;    // 状態
    float g;    // プリワープ済み係数
    float G;    // フィードバック係数
} OnePoleTPT;

void onepole_set_cutoff(OnePoleTPT* f, float fc, float fs) {
    f->g = tanf(M_PI * fc / fs);
    f->G = f->g / (1.0f + f->g);
}

void onepole_process(OnePoleTPT* f, float x, float* lp, float* hp) {
    float v = (x - f->s) * f->G;
    *lp = v + f->s;
    *hp = x - *lp;
    f->s = v + *lp;
}
```

---

## Appendix B: 完全なSVF実装

```c
typedef struct {
    float ic1eq, ic2eq;  // 積分器状態
    float a1, a2, a3;    // 係数
    float k;             // レゾナンス係数
} SVFTPT;

void svf_set_params(SVFTPT* f, float fc, float Q, float fs) {
    float g = tanf(M_PI * fc / fs);
    f->k = 1.0f / Q;
    f->a1 = 1.0f / (1.0f + g * (g + f->k));
    f->a2 = g * f->a1;
    f->a3 = g * f->a2;
}

void svf_process(SVFTPT* f, float x, 
                 float* lp, float* bp, float* hp) {
    float v3 = x - f->ic2eq;
    float v1 = f->a1 * f->ic1eq + f->a2 * v3;
    float v2 = f->ic2eq + f->a2 * f->ic1eq + f->a3 * v3;
    
    f->ic1eq = 2.0f * v1 - f->ic1eq;
    f->ic2eq = 2.0f * v2 - f->ic2eq;
    
    *lp = v2;
    *bp = v1;
    *hp = x - f->k * v1 - v2;
}
```

---

*本ドキュメントは、KVR Audio DSPフォーラムおよび関連技術文書の情報に基づいて作成されました。*

---

## Appendix C: Diode Ladder Filter 実装手法まとめ

### C.1 概要

**ダイオードラダーフィルター**は、TB-303やEMS VCS3に搭載されている4極ローパスフィルターであり、トランジスタラダー（Moog）とは異なる特性を持つ。

**主な違い（Moogトランジスタラダーとの比較）**:

| 特性 | Moogトランジスタラダー | ダイオードラダー（TB-303） |
|------|----------------------|--------------------------|
| 段間の結合 | バッファで分離 | 結合している |
| カットオフ制御 | 電流で直接制御 | 電圧がオーディオ信号と混合 |
| 自己発振 | 可能 | 通常は不可（改造で可能） |
| 非線形性の配置 | 各段に独立 | 全体で相互作用 |
| 数学的解決 | 比較的単純 | 複雑（全出力が全状態に依存）|

### C.2 主要な実装アプローチ

#### A. karrikuh（Dominique Wurtz）- ZDF + 単一非線形

**スレッド**: "Diode ladder filter" (t=346155)
**ライセンス**: MIT
**コード**: https://pastebin.com/THe5JG5f

**特徴**:
- 台形積分法によるZDF実装
- Mathematicaで代数的に解決
- 単一の入力クリッパー `x / (1 + abs(x))`
- フィードバックパスにHPF追加（TB-303特性）
- 2倍以上のオーバーサンプリング推奨

```c
// 線形ZDFを解く
float y5 = (g*x + s) / (1.0f + g*k);

// 入力クリッピング（事後に適用）
const float y0 = clip(x - k*y5);

static float clip(const float x) {
    return x / (1.0f + fabsf(x));
}
```

**制限**:
- tanh()使用時にレゾナンス周波数がシフトする問題
- anttoの指摘によりハードクリッパーに変更すると改善

---

#### B. antto - Naive実装 + ハードクリッパー

**スレッド**: "Diode ladder filter" (t=346155)、"Analog OTA filter modeling" (t=370838)

**特徴**:
- kunn transfer functionに基づく「naive」実装
- フィードバックに1サンプル遅延
- ハードクリッパー使用（tanh()ではなく）
- x0xb0x（TB-303クローン）との実機比較で調整
- 2つのHPFをフィードバックループに配置

**anttoの知見**:

> "Understanding the diode ladder is about understanding how multiple non-linearities in series are used to actually replace the 'cutoff coefficients' in transistor ladders. The 'cutoff coefficient' becomes an offset to the input signal (yes, the control voltage is basically mixed into the audio signal)."

```c
fb = hpf(x0) - hpf(k*y0);
y1 += a*2 * (shape(fb) - shape(y1-y2));
// ...
y0 = hpf(hpf(y4));  // 出力とフィードバック
```

**レゾナンス周波数シフト問題**:
- tanh()使用時に問題が発生
- ハードクリッパーで解決
- 実機ではこの問題が発生しない

---

#### C. Wolfen666 - mystran手法のVCS3適用

**スレッド**: "Cheap non-linear zero-delay filters" (t=349859, Page 18)

**特徴**:
- mystranの半サンプル遅延トランスコンダクタンス手法
- Federico FontanaとStefano Zambonの論文に基づく方程式
- 複数のtanh()非線形性を各段に適用
- MATLABで連立方程式を解決

```c
// VCS3ダイオードラダーの微分方程式
// dvc1/dt = f.(tanh((vin-vout)/(2*Vt)) + tanh((vc2-vc1)/(2*gamma)))
// dvc2/dt = f.(tanh((vc3-vc2)/(2*gamma)) - tanh((vc2-vc1)/(2*gamma)))
// dvc3/dt = f.(tanh((vc4-vc3)/(2*gamma)) - tanh((vc3-vc2)/(2*gamma)))
// dvc4/dt = f.(-tanh((vc4)/(6*gamma)) - tanh((vc4-vc3)/(2*gamma)))

// 各段のトランスコンダクタンス評価
float t0 = f*tanhXdX((ih - r * s[3])*g0inv)*g0inv;
float t1 = f*tanhXdX((s[1]-s[0])*g1inv)*g1inv;
float t2 = f*tanhXdX((s[2]-s[1])*g1inv)*g1inv;
float t3 = f*tanhXdX((s[3]-s[2])*g1inv)*g1inv;
float t4 = f*tanhXdX((s[3])*g2inv)*g2inv;

// 定数
Vt = 0.5f;
n = 1.836f;
gamma = Vt * n;
```

---

#### D. Open303プロジェクト

**URL**: https://sourceforge.net/projects/open303/
**関連スレッド**: KVR "Open303" (77+ pages)

**特徴**:
- オープンソースのTB-303エミュレーション
- Robin Schmidt (RS-Met) による基礎実装
- 最近のフォークでTPTダイオードラダーフィルターを追加
- DevilFish改造およびBehringer TD-3との比較で調整

---

### C.3 技術的課題と解決策

#### レゾナンス周波数シフト問題

**問題**: tanh()非線形性を使用すると、フィードバックゲイン増加時にレゾナンス周波数がシフトする

**原因**: uroshの分析によると、ダイオードの動的抵抗 `Vt/Id` が信号レベルに依存するため、カットオフ周波数の自己変調が発生

**解決策**:
1. ハードクリッパーを使用（antto）
2. 非線形性の配置を慎重に選択
3. より正確な回路モデリング

#### 段間結合の複雑さ

mystranの指摘：

> "if you do go transforming a diode ladder, don't get scared if the linear solution part isn't exactly quite as pretty (or simple) as with transistor ladders. Being unbuffered, every output will depend on every state, and the solutions don't really factor very nicely at all, so the cost of the linear solver is certainly higher there."

**解決戦略**:
- CASで解を求め、最短の解から順に計算
- 解けた出力を定数として残りの方程式に代入

#### フィードバックパスのHPF

**TB-303の特性**:
- フィードバックループにHPFが存在
- 低周波でのレゾナンスを減衰
- HPFカットオフは約100Hz（固定）

karrikuhの実装：
```c
void set_feedback_hpf_cutoff(const float fc) {
    const float K = fc * M_PI;
    ah = (K - 2.0f) / (K + 2.0f);
    bh = 2.0f / (K + 2.0f);
}
```

---

### C.4 実装選択のガイドライン

| 要件 | 推奨アプローチ |
|------|--------------|
| CPU効率重視 | karrikuh（単一非線形） |
| 精度重視 | Wolfen666（mystran手法） |
| TB-303忠実度 | antto + Open303参照 |
| 学習・実験 | karrikuh（コードが明快） |

**オーバーサンプリング**:
- 最低2倍、推奨4倍
- 非線形性が強い場合は8倍も検討

---

### C.5 参考資料

**KVRスレッド**:
- "Diode ladder filter" (t=346155) - karrikuh、antto
- "Cheap non-linear zero-delay filters" (t=349859) - mystran、Wolfen666
- "Analog OTA filter modeling" (t=370838) - urosh
- "Open303" - Robin Schmidt他

**学術論文**:
- Federico Fontana, Stefano Zambon: VCS3ダイオードラダーの解析

**オープンソース実装**:
- karrikuh: https://pastebin.com/THe5JG5f (MIT)
- Open303: https://sourceforge.net/projects/open303/
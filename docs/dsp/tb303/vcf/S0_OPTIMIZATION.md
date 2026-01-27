# Diode Ladder Filter s0 計算の最適化

## 概要

Karrikuh (Dominique Wurtz) による Diode Ladder Filter 実装において、状態変数 `s0` の計算式を数学的に等価な形に変形することで、Cortex-M4 で約 4% の性能向上を実現できる。

## 理論的背景

### 状態空間表現からの導出

Diode Ladder Filter の状態空間表現において、フィードバック経路の状態 `s0` は以下の行列演算から得られる:

```
s0 = [1 0 0 0] · (I - A)⁻¹ · z
```

ここで:
- `A`: システム行列 (4x4)
- `z`: 状態ベクトル [z[0], z[1], z[2], z[3]]ᵀ
- `(I - A)⁻¹`: 解析的に求めた逆行列

### 逆行列の解析解

双線形変換後のシステムにおいて、逆行列の第1行は:

```
[(I-A)⁻¹]₁ = c · [a³, a²b, a(b²-2a²), b(b²-3a²)]
```

ここで:
- `a = π·fc` (fc: 正規化カットオフ周波数)
- `b = 2a + 1`
- `c = 1 / det(I-A) = 1 / (2a⁴ - 4a²b² + b⁴)`

### 状態 s0 の完全形

```
s0 = c · [a³, a²b, a(b²-2a²), b(b²-3a²)] · [z[0], z[1], z[2], z[3]]ᵀ
```

## 数学的な式

行列・ベクトル積を展開した数学形式:

```
s0 = c · (a³·z[0] + a²b·z[1] + a(b²-2a²)·z[2] + b(b²-3a²)·z[3])
```

ここで:
- `a = π·fc` (fc: 正規化カットオフ周波数)
- `b = 2a + 1`
- `c = 1 / (2a⁴ - 4a²b² + b⁴)`

## Karrikuh 実装の式 (プログラム向け分解)

上記を `a²` を共通因子として分解し、コード化しやすくした形:

```cpp
// a³ → a²·a, a²b → a²·b として a² を事前計算で再利用
s0 = (a2*a*z[0] + a2*b*z[1] + z[2]*(b2-2*a2)*a + z[3]*(b2-3*a2)*b) * c;
```

## D因数分解による最適化

### 変形の導出

**Step 1: D の定義**
```
D = b² - 2a²
```

**Step 2: 分母 c の変形**

オリジナル:
```
c = 1 / (2a⁴ - 4a²b² + b⁴)
```

D² を展開:
```
D² = (b² - 2a²)²
   = b⁴ - 4a²b² + 4a⁴
```

したがって:
```
D² - 2a⁴ = b⁴ - 4a²b² + 4a⁴ - 2a⁴
         = b⁴ - 4a²b² + 2a⁴
         = 2a⁴ - 4a²b² + b⁴  ✓ (オリジナルと一致)
```

最適化後:
```
c = 1 / (D² - 2a⁴)
```

**Step 3: s0 の変形**

オリジナルを項ごとに整理:
```
s0 = c · (a³·z[0] + a²b·z[1] + (b²-2a²)·a·z[2] + (b²-3a²)·b·z[3])
```

`(b²-3a²) = (b²-2a²) - a² = D - a²` を利用:
```
s0 = c · (a³·z[0] + a²b·z[1] + D·a·z[2] + (D-a²)·b·z[3])
   = c · (a³·z[0] + a²b·z[1] + D·a·z[2] + D·b·z[3] - a²b·z[3])
   = c · (a²·(a·z[0] + b·z[1] - b·z[3]) + D·(a·z[2] + b·z[3]))
   = c · (a²·(a·z[0] + b·(z[1]-z[3])) + D·(a·z[2] + b·z[3]))
```

**最終形:**
```
term1 = a·z[0] + b·(z[1] - z[3])
term2 = a·z[2] + b·z[3]
s0 = c · (a²·term1 + D·term2)
```

## コード比較

### オリジナル実装
```cpp
float s0 = (a2 * a * z[0]
          + a2 * b * z[1]
          + z[2] * (b2 - 2.0f * a2) * a
          + z[3] * (b2 - 3.0f * a2) * b) * c;
```

### D因数分解実装
```cpp
float D = b2 - 2.0f * a2;
float term1 = a * z[0] + b * (z[1] - z[3]);
float term2 = a * z[2] + b * z[3];
float s0 = c * (a2 * term1 + D * term2);
```

## 生成アセンブリ比較 (Cortex-M4, clang -O3 -ffast-math)

### オリジナル (19命令)
```asm
vldr    s10, [r0, #8]     ; z[2]
vadd.f32 s14, s1, s1      ; 2*a2
vsub.f32 s14, s3, s14     ; b2 - 2*a2
vmul.f32 s10, s10, s14    ; z[2] * (b2-2a2)
vmov.f32 s14, #-3.0
vldr    s6, [r0]          ; z[0]
vldr    s8, [r0, #4]      ; z[1]
vldr    s12, [r0, #12]    ; z[3]
vmul.f32 s14, s1, s14     ; a2 * (-3)
vadd.f32 s14, s3, s14     ; b2 - 3a2
vmul.f32 s6, s6, s1       ; z[0] * a2
vmul.f32 s8, s8, s1       ; z[1] * a2
vmul.f32 s12, s12, s14    ; z[3] * (b2-3a2)
vadd.f32 s8, s12, s8
vadd.f32 s6, s10, s6
vmul.f32 s2, s8, s2       ; * b
vmul.f32 s0, s6, s0       ; * a
vadd.f32 s0, s2, s0       ; sum
vmul.f32 s0, s0, s4       ; * c
```

### D因数分解 (18命令)
```asm
vldr    s10, [r0, #4]     ; z[1]
vldr    s14, [r0, #12]    ; z[3]
vldr    s8, [r0]          ; z[0]
vldr    s12, [r0, #8]     ; z[2]
vsub.f32 s10, s10, s14    ; z[1] - z[3]
vadd.f32 s6, s1, s1       ; 2*a2
vmul.f32 s8, s8, s0       ; z[0] * a
vmul.f32 s10, s10, s2     ; (z[1]-z[3]) * b
vmul.f32 s0, s12, s0      ; z[2] * a
vmul.f32 s2, s14, s2      ; z[3] * b
vsub.f32 s6, s3, s6       ; D = b2 - 2a2
vadd.f32 s8, s10, s8      ; term1
vadd.f32 s0, s0, s2       ; term2
vmul.f32 s2, s8, s1       ; a2 * term1
vmul.f32 s0, s0, s6       ; D * term2
vadd.f32 s0, s2, s0       ; sum
vmul.f32 s0, s0, s4       ; * c
```

### 命令数の差

| 項目 | オリジナル | D因数分解 | 差分 |
|------|-----------|----------|------|
| 命令数 | 19 | 18 | -1 |
| vmov (定数ロード) | 1 | 0 | -1 |
| vmul | 7 | 7 | 0 |
| vadd/vsub | 6 | 6 | 0 |
| vldr | 4 | 4 | 0 |

D因数分解では `-3.0` 定数のロード (`vmov.f32 s14, #-3.0`) が不要になる。

## ベンチマーク結果 (Cortex-M4 @ Renode)

| 実装 | サイクル/iter | 合計 (100,000回) |
|------|--------------|------------------|
| オリジナル | 70 | 7,056,051 |
| D因数分解 | 67 | 6,720,050 |

**結果: D因数分解は 4.8% 高速 (1.05x)**

## 適用時の注意

1. **コンパイラ最適化との相互作用**
   - `-O3 -ffast-math` 環境での計測結果
   - 最適化レベルやコンパイラによって結果は異なる可能性がある

2. **数値精度**
   - 数学的に等価だが、浮動小数点演算の順序が異なるため微小な誤差が生じうる
   - 音声処理では通常問題にならないレベル

3. **分母 c の計算も最適化可能**
   ```cpp
   // オリジナル
   float c = 1.0f / (2.0f * a2 * a2 - 4.0f * a2 * b2 + b2 * b2);

   // D因数分解
   float D = b2 - 2.0f * a2;
   float sub_term = 2.0f * a2 * a2;  // g0計算でも使用
   float c = 1.0f / (D * D - sub_term);
   ```

## 参考

- オリジナル実装: [diode_ladder_filter.hh](code/diode_ladder_filter.hh) (Dominique Wurtz, MIT License)
- ベンチマークコード: [bench_diode_ladder.cc](/tests/bench_diode_ladder.cc)

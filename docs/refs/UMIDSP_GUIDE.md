# umidsp 設計・実装ガイド（API統合版）

このドキュメントは、DSP APIの利用者向け情報と、umidspライブラリの実装・設計指針を統合したガイドです。既存のAPI説明に加え、実装パターン（共有データと状態の分離、係数の事前計算、SPSC通信など）を一貫した形で整理します。

---

## 再構成版（要点）

### A. コア原則
- これは**状態を持つユニット**に対する原則。
- `operator()` を主インターフェース、`tick()` はエイリアス。
- 係数は `set_params(dt)` で事前計算。
- `dt` を使って除算を避ける。
- **純粋関数**はこの限りではなく、`constexpr` / `const fn` を優先。

**統一するべき形（推奨）**
- 係数は注入（共有/ダブルバッファで更新）。
- 状態はユニット内で保持（DSPスレッドが更新）。
- `process()` で処理（C++は `operator()` をエイリアス化）。

**この形に統一する理由**
- 係数共有により **更新コストを最小化** できる。
- 状態を内包することで **スレッド安全性** と **局所性** が高い。
- `process()` に統一すると **テスト/合成** が容易。

**複数出力を持つユニットの扱い**
- `process()` 内で状態更新し、出力はゲッター/参照で取得。

```cpp
class Svf {
public:
  void set_params(float cutoff, float resonance, float dt);
  void process(float input);  // 状態更新のみ
  float lp() const { return lp_; }
  float bp() const { return bp_; }
  float hp() const { return hp_; }
private:
  float lp_{}, bp_{}, hp_{};
};
```

```rust
struct Svf { lp: f32, bp: f32, hp: f32 }
impl Svf {
  fn set_params(&mut self, cutoff: f32, resonance: f32, dt: f32) { /* ... */ }
  fn process(&mut self, input: f32) { /* 更新 */ }
  fn lp(&self) -> f32 { self.lp }
  fn bp(&self) -> f32 { self.bp }
  fn hp(&self) -> f32 { self.hp }
}
```

### B. 構造分類
- 純粋関数: 状態なし・小関数。
- 状態を持つユニット: 係数 + 状態 + `process()`。
- 複合モデル: ユニットの合成と更新順序の固定。

### C. Data / State 分離
- 係数は共有・不変に近い値。
- 状態は各ユニットが所有し、DSP側で更新。

### D. 係数更新戦略
- 低頻度: `set_params()` でまとめて更新。
- オーディオレート: 軽い近似・補間で更新。

### E. スレッド共有
- 係数は Double Buffering。
- UI→DSPは SPSCリングバッファ。

### F. 実装スタイル
- C++はヘッダオンリー + `inline` が基本。
- Rustは `struct + impl` で同等。
- **可能な限り共通インターフェイスに統一**（`set_params` / `process` / `reset`）。

---

## 旧構成（詳細）

## 1. 評価（既存ドキュメントの位置づけ）

### DSP API（[docs/reference/API_DSP.md](API_DSP.md)）
- **強み**
  - `operator()` 優先、`tick()` はエイリアスという一貫したスタイル。
    - API利用者が「短く書く」「明示的に書く」のどちらも選べる。
  - `set_params(dt)` による係数事前計算を徹底し、実行時コストを抑える思想が明確。
    - パラメータ更新の頻度（低）とサンプル処理（高）の分離が強調されている。
  - `dt` を軸にした計算（除算回避）を明記。
    - Cortex-M系でのコスト意識が具体的で、設計の説得力が高い。
  - オシレータ/フィルタ/エンベロープのサンプルが揃っており、初見でも使い方を追える。
- **不足**
  - DSP部品の「内部構造（係数/状態/バッファ）」の説明が薄い。
    - どの値が共有され、どの値がボイス固有なのかが読み取りづらい。
  - スレッド間共有（UI/DSP）や更新タイミングに関する具体例が不足。
    - 係数更新と音声処理の競合回避が利用者側に委ねられている。
  - 典型的な「データ所有モデル（共有 vs 専有）」の記述がなく、拡張時の指針が不足。
  - パラメータ更新の推奨トリガやデバウンス方針が明文化されていない。
  - 新規DSP追加時のテンプレート/責務分担が未提示。

### 実装ガイド（[docs/GUIDELINE.md](../GUIDELINE.md)）
- **強み**
  - Data/State分離やDouble Bufferingなど、実装規約が具体的。
    - 共有/専有の責務分離が明確で、リアルタイム処理に適した構造。
  - 世代付きハンドル、SPSCバッファなど、音声システムの現実的要求に合致。
    - ロックフリー・Wait-free設計の意図が読み取れる。
  - 例が短く、実装への落とし込みがしやすい。
- **不足**
  - DSP APIとの結びつきが弱く、利用者は具体的な実装に落とし込みづらい。
    - `set_params()` / `operator()` の位置づけがガイド内で接続されていない。
  - `umidsp` をどう設計・拡張するかのストーリーが不足。
    - モジュール間の粒度設計や命名規則への踏み込みがない。
  - DSP特有の数式・係数計算の扱い（`dt` や安定化条件）の指針が不足。
  - 具体的なDSP部品（Biquad/SVF/ADSR）の適用例がないため、抽象度が高め。

---

## 2. 統合プラン（API + 実装規約）

### 目的
- **API利用者**が「書き方」「計算スタイル」「サンプル処理の意味」を理解できる。
- **実装者**が「内部データ構造」「スレッド分離」「係数事前計算」を明確に踏襲できる。

### 統合方針
1. **API設計原則 → 実装パターンへリンク**
   - `set_params(dt)` と Data/State 分離を結合して説明。
   - `operator()` と内部状態更新の関係を明示。

2. **オブジェクトの粒度を明文化**
   - `Coeffs`（共有）と `State`（ボイス/インスタンス専有）を基準にした分類。

3. **スレッド間共有モデルを標準化**
   - UIで係数更新 → ダブルバッファでDSPへ公開。
   - UI/DSP間の変更通知はSPSCリングバッファで行う。

4. **DSPコンポーネント拡張ガイドを追加**
   - 新規DSPを追加する際のテンプレートを提示。
   - `reset()` / `set_params()` / `operator()` の責務を整理。

---

## 3. umidsp実装ガイド（統合版）

### 3.1 基本インターフェース

- **設計原則**
  - `operator()` を主インターフェースとする。
  - `tick()` は読みやすさのための薄いラッパー。
  - 係数は `set_params()` で事前計算。
  - **純粋関数DSP**は `constexpr` / `const fn` を優先（`set_params()` を持たない）。

**純粋関数DSPの例（C++/Rust）**

```cpp
constexpr float lerp(float a, float b, float t) {
  return a + (b - a) * t;
}
```

```rust
const fn lerp(a: f32, b: f32, t: f32) -> f32 {
  a + (b - a) * t
}
```

**状態を引数に取る関数の例（C++/Rust）**

```cpp
struct OnePoleState { float z1 = 0.0f; };

inline float onepole_process(float a0, float b1, OnePoleState& s, float in) {
  float y = in * a0 + s.z1 * b1;
  s.z1 = y;
  return y;
}
```

```rust
#[derive(Clone, Copy, Default)]
struct OnePoleState { z1: f32 }

fn onepole_process(a0: f32, b1: f32, s: &mut OnePoleState, input: f32) -> f32 {
  let y = input * a0 + s.z1 * b1;
  s.z1 = y;
  y
}
```

```cpp
class DspObject {
public:
    void set_params(/* params */, float dt);
    float operator()(float input);
    float tick(float input) { return (*this)(input); }
    void reset();
};
```

```rust
trait DspObject {
  fn set_params(&mut self, dt: f32 /* , params */);
  fn process(&mut self, input: f32) -> f32;
  fn tick(&mut self, input: f32) -> f32 { self.process(input) }
  fn reset(&mut self);
}
```

---

### 3.2 Data / State 分離

- **共有データ（係数）**と**状態（内部バッファ）**を物理的に分離する。
  - 目的は「UI更新とDSP処理の競合回避」「更新コストの局所化」「テスト/デバッグ容易性」。

- **状態を持つユニットの標準形**として、以下の形に統一するのを推奨。

```cpp
struct FilterCoeffs { float a0, a1, b1, b2; };
struct FilterState { float z1 = 0.0f, z2 = 0.0f; };

class Biquad {
public:
  void set_params(float cutoff, float q, float dt) {
    float w = cutoff * dt;
    (void)w;
    // coeffs = ...
  }

  float operator()(float input) {
    float out = input * coeffs.a0 + state.z1 * coeffs.b1;
    state.z1 = out;
    return out;
  }

  void reset() { state = {}; }

private:
  FilterCoeffs coeffs{};
  FilterState state{};
};
```

**係数注入 + 状態プライベートの例（C++）**

```cpp
class BiquadInjected {
public:
  void bind_coeffs(const FilterCoeffs* shared) { coeffs = shared; }

  float operator()(float input) {
    const auto& c = *coeffs; // 共有係数を参照
    float out = input * c.a0 + state.z1 * c.b1;
    state.z1 = out;
    return out;
  }

  void reset() { state = {}; }

private:
  const FilterCoeffs* coeffs = nullptr; // 係数は注入
  FilterState state{};                  // 状態は内部保持
};
```

```cpp
struct FilterCoeffs {
    float a0, a1, b1, b2;
};

struct FilterState {
    float z1 = 0.0f;
    float z2 = 0.0f;
};
```

```rust
#[derive(Clone, Copy, Default)]
struct FilterCoeffs { a0: f32, a1: f32, b1: f32, b2: f32 }

#[derive(Clone, Copy, Default)]
struct FilterState { z1: f32, z2: f32 }
```

- `set_params()` は **Coeffs** に作用。
- `operator()` は **State** のみ更新。

#### 分離の具体的メリット（なぜ競合回避・更新コスト・テスト性が良くなるか）

- **競合回避が容易**
  - UIは係数だけ更新し、DSPは状態だけ更新する構造になるため、同一メモリへの同時書き込みを避けやすい。
  - 単一クラスで係数と状態が混在すると、`set_params()` と `operator()` が同時に同一オブジェクトを触る可能性がある。

- **更新コストの局所化**
  - 係数計算（高コスト）は `set_params()` に集約でき、ホットパス（`operator()`）には乗算のみが残る。
  - 単一クラスでも可能だが、分離により「係数はここでしか変わらない」という契約が明確になる。

- **デバッグ性・テスト容易性**
  - 係数生成（入力→係数）を単体テストしやすい。
  - 状態更新（固定係数での時間発展）も独立テストできる。
  - 問題の切り分けが容易になる。

#### 競合回避の例（C++）

```cpp
struct Coeffs { float a0, b1; };
struct State  { float z1 = 0.0f; };

// DSP: 状態のみ更新
float process(const Coeffs& c, State& s, float in) {
    float y = in * c.a0 + s.z1 * c.b1;
    s.z1 = y;
    return y;
}
```

#### テスト分離の例（Rust）

```rust
#[derive(Clone, Copy, Default)]
struct Coeffs { a0: f32, b1: f32 }
#[derive(Clone, Copy, Default)]
struct State  { z1: f32 }

fn coeffs_from_params(cutoff: f32, dt: f32) -> Coeffs {
    let a0 = cutoff * dt;
    Coeffs { a0, b1: 1.0 - a0 }
}

fn process(c: Coeffs, s: &mut State, input: f32) -> f32 {
    let y = input * c.a0 + s.z1 * c.b1;
    s.z1 = y;
    y
}
```

#### 基本要素ごとの分離例（C++）

**オシレーター（係数=周波数、状態=位相、更新は $dt$ ）**

```cpp
struct OscCoeffs { float freq_hz = 0.0f; };
struct OscState  { float phase = 0.0f; };

inline float osc_process(const OscCoeffs& c, OscState& s, float dt) {
  s.phase += c.freq_hz * dt;
  if (s.phase >= 1.0f) s.phase -= 1.0f;
  return std::sin(2.0f * 3.14159265f * s.phase);
}
```

**フィルター（係数=フィルタ係数、状態=遅延要素、係数計算に $dt$ ）**

```cpp
struct OnePoleCoeffs { float a0 = 0.0f, b1 = 0.0f; };
struct OnePoleState  { float z1 = 0.0f; };

inline OnePoleCoeffs onepole_set_params(float cutoff_hz, float dt) {
  OnePoleCoeffs c;
  float w = cutoff_hz * dt; // 例: 簡易1-pole
  c.a0 = w;
  c.b1 = 1.0f - w;
  return c;
}

inline float onepole_process(const OnePoleCoeffs& c, OnePoleState& s, float in) {
  float y = in * c.a0 + s.z1 * c.b1;
  s.z1 = y;
  return y;
}
```

**ディレイ（係数=固定遅延長、状態=リングバッファ、遅延時間→サンプル数に $dt$ ）**

```cpp
struct DelayCoeffs { size_t delay = 0; };
struct DelayState  { std::vector<float> buf; size_t idx = 0; };

inline DelayCoeffs delay_set_params(float delay_sec, float dt) {
  DelayCoeffs c;
  const float fs = 1.0f / dt;
  c.delay = static_cast<size_t>(delay_sec * fs);
  return c;
}

inline float delay_process(const DelayCoeffs& c, DelayState& s, float in) {
  if (s.buf.empty()) return in;
  size_t read = (s.idx + s.buf.size() - c.delay) % s.buf.size();
  float out = s.buf[read];
  s.buf[s.idx] = in;
  s.idx = (s.idx + 1) % s.buf.size();
  return out;
}

// 注意: std::vector を使う場合でも、初期化時に確保して以後は再確保しない前提なら可
// （リアルタイム処理中の再確保を避けること）
```

#### 例外・注意が必要な例（係数が毎サンプル変化するケース）

**可変ディレイ（遅延長が毎サンプル変化 → 係数が状態に近づく）**

```cpp
struct VarDelayState {
  std::vector<float> buf;
  size_t idx = 0;
};

inline float var_delay_process(VarDelayState& s, float in, float delay_samps) {
  // delay_samps が毎サンプル変化するため、係数事前計算の意味が薄い
  size_t d = static_cast<size_t>(delay_samps);
  size_t read = (s.idx + s.buf.size() - d) % s.buf.size();
  float out = s.buf[read];
  s.buf[s.idx] = in;
  s.idx = (s.idx + 1) % s.buf.size();
  return out;
}
```

### 3.3 係数計算の原則（dt優先）

- `dt` を使用し、除算を避ける。

```cpp
float w = cutoff * dt;     // ✅ 乗算のみ
float w = cutoff / sr;     // ❌ 除算コスト
```

```rust
let w = cutoff * dt; // ✅ 乗算のみ
let w = cutoff / sr; // ❌ 除算コスト
```

- `AudioContext::dt` を標準入力とする。

#### 係数が毎サンプル変化する場合（モジュレーション）

- **方針**
  - `set_params()` を「イベント/低頻度更新」に限定せず、
    **オーディオレートで係数更新**してよい。
  - ただし高コストの計算は避け、**軽い補間/近似**を使う。

**例: カットオフをオーディオレートで更新（C++）**

```cpp
float cutoff = cutoff_base;
for (uint32_t i = 0; i < frames; ++i) {
  cutoff += mod[i]; // audio-rate modulation
  auto c = onepole_set_params(cutoff, dt);
  out[i] = onepole_process(c, state, in[i]);
}
```

**例: 係数を線形補間して更新負荷を下げる（C++）**

```cpp
auto c0 = onepole_set_params(cutoff_start, dt);
auto c1 = onepole_set_params(cutoff_end, dt);
for (uint32_t i = 0; i < frames; ++i) {
  float t = static_cast<float>(i) / static_cast<float>(frames);
  OnePoleCoeffs c {
    c0.a0 + (c1.a0 - c0.a0) * t,
    c0.b1 + (c1.b1 - c0.b1) * t,
  };
  out[i] = onepole_process(c, state, in[i]);
}
```

#### 係数更新パターン別の構造（最も効率が出やすい形）

**1) モジュレーション前提（毎サンプル更新）**
- 係数は軽量化し、`process()` 直前で更新。

```cpp
for (uint32_t i = 0; i < frames; ++i) {
  cutoff += mod[i];
  auto c = onepole_set_params(cutoff, dt); // 軽い式のみ
  out[i] = onepole_process(c, state, in[i]);
}
```

**2) 演奏中の変更がまれ（イベント時のみ更新）**
- UI/イベント側で係数更新 → 共有係数に注入。

```cpp
// Event thread
shared_coeffs = onepole_set_params(cutoff, dt);

// DSP thread
const auto& c = shared_coeffs;
for (uint32_t i = 0; i < frames; ++i) {
  out[i] = onepole_process(c, state, in[i]);
}
```

**3) 演奏中に変更なし（固定パラメータ）**
- 係数は初期化時に一度だけ計算。

```cpp
const auto c = onepole_set_params(cutoff, dt);
for (uint32_t i = 0; i < frames; ++i) {
  out[i] = onepole_process(c, state, in[i]);
}
```

#### 複数パラメータの更新（個別 / 一括）

**個別に更新したい場合**（複数の係数を分割）

```cpp
struct FilterCoeffs {
  float a0, b1;
  float mix;   // 例: dry/wet
  float gain;
};

void update_gain(FilterCoeffs& c, float gain) { c.gain = gain; }
void update_mix(FilterCoeffs& c, float mix) { c.mix = mix; }
```

**まとめて更新したい場合**（`set_params` に集約）

```cpp
struct FilterParams { float cutoff, q, gain, mix; };

inline FilterCoeffs set_params(const FilterParams& p, float dt) {
  FilterCoeffs c;
  float w = p.cutoff * dt;
  c.a0 = w;
  c.b1 = 1.0f - w;
  c.gain = p.gain;
  c.mix = p.mix;
  return c;
}
```

#### 関数型っぽい更新（係数は値、状態は別）

**C++**

```cpp
struct OnePoleCoeffs { float a0, b1; };
struct OnePoleState  { float z1 = 0.0f; };
struct OnePoleParams { float cutoff; };

inline OnePoleCoeffs onepole_coeffs(const OnePoleParams& p, float dt) {
  float w = p.cutoff * dt;
  return { w, 1.0f - w };
}

inline float onepole_process(const OnePoleCoeffs& c, OnePoleState& s, float in) {
  float y = in * c.a0 + s.z1 * c.b1;
  s.z1 = y;
  return y;
}
```

**Rust**

```rust
#[derive(Clone, Copy, Default)]
struct OnePoleCoeffs { a0: f32, b1: f32 }

#[derive(Clone, Copy, Default)]
struct OnePoleState { z1: f32 }

#[derive(Clone, Copy, Default)]
struct OnePoleParams { cutoff: f32 }

fn onepole_coeffs(p: OnePoleParams, dt: f32) -> OnePoleCoeffs {
  let w = p.cutoff * dt;
  OnePoleCoeffs { a0: w, b1: 1.0 - w }
}

fn onepole_process(c: OnePoleCoeffs, s: &mut OnePoleState, input: f32) -> f32 {
  let y = input * c.a0 + s.z1 * c.b1;
  s.z1 = y;
  y
}
```

---

### 3.4 スレッド間共有（UI ↔ DSP）

- **Double Buffering** を標準とする。
- UIスレッドは裏バッファに書き込み、DSPは表バッファを参照。

```cpp
std::array<FilterCoeffs, 2> sharedPool;
std::atomic<uint8_t> activeIndex{0};

// UI: 裏バッファに係数更新
uint8_t next = activeIndex.load() ^ 1;
sharedPool[next] = newCoeffs;
activeIndex.store(next, std::memory_order_release);

// DSP: 表バッファを参照
uint8_t idx = activeIndex.load(std::memory_order_acquire);
const auto& coeffs = sharedPool[idx];
```

```rust
use std::sync::atomic::{AtomicU8, Ordering};

struct Shared {
  pool: [FilterCoeffs; 2],
  active: AtomicU8,
}

impl Shared {
  fn publish(&self, next: FilterCoeffs) {
    let cur = self.active.load(Ordering::Relaxed);
    let idx = (cur ^ 1) as usize;
    // SAFETY: 単一ライタ想定
    unsafe {
      let ptr = self.pool.as_ptr() as *mut FilterCoeffs;
      *ptr.add(idx) = next;
    }
    self.active.store(idx as u8, Ordering::Release);
  }

  fn current(&self) -> FilterCoeffs {
    let idx = self.active.load(Ordering::Acquire) as usize;
    self.pool[idx]
  }
}
```

---

### 3.5 SPSCリングバッファによる制御

- UI → DSPのコマンド送信はSPSCリングバッファを使う。
- ノートON/OFFやパラメータ変更を安全に伝える。

```cpp
struct Command {
    enum Type { NOTE_ON, NOTE_OFF, PARAM_SET } type;
    float value;
};
```

```rust
enum CommandType { NoteOn, NoteOff, ParamSet }

struct Command {
  kind: CommandType,
  value: f32,
}
```

---

### 3.6 新規DSPコンポーネント追加テンプレート

1. **Coeffs / State 分離**
2. `set_params()` で係数計算
3. `operator()` で状態更新
4. `reset()` で状態クリア

```cpp
struct MyFxCoeffs {
    float a, b;
};

struct MyFxState {
    float z = 0.0f;
};

class MyFx {
public:
    void set_params(float x, float dt) {
        coeffs.a = x * dt;
        coeffs.b = 1.0f - coeffs.a;
    }

    float operator()(float in) {
        state.z = in * coeffs.a + state.z * coeffs.b;
        return state.z;
    }

    void reset() { state = {}; }

private:
    MyFxCoeffs coeffs;
    MyFxState state;
};
```

```rust
#[derive(Clone, Copy, Default)]
struct MyFxCoeffs { a: f32, b: f32 }

#[derive(Clone, Copy, Default)]
struct MyFxState { z: f32 }

struct MyFx {
  coeffs: MyFxCoeffs,
  state: MyFxState,
}

impl MyFx {
  fn set_params(&mut self, x: f32, dt: f32) {
    self.coeffs.a = x * dt;
    self.coeffs.b = 1.0 - self.coeffs.a;
  }

  fn process(&mut self, input: f32) -> f32 {
    self.state.z = input * self.coeffs.a + self.state.z * self.coeffs.b;
    self.state.z
  }

  fn reset(&mut self) { self.state = MyFxState::default(); }
}
```

---

### 3.7 C++ / Rust での同等実装イメージ

- 基本方針は同じ（Coeffs/State分離、`set_params`で係数計算、`process`で状態更新）。
- `operator()` はRustでは `process()` / `tick()` などのメソッド名で置き換える。
- ダブルバッファは `AtomicU8` と2面バッファで再現可能。
- SPSCは `crossbeam` や `ringbuf` 等で同等の単方向キューを構成できる。

```cpp
struct FilterCoeffs { float a0, a1, b1, b2; };
struct FilterState { float z1 = 0.0f, z2 = 0.0f; };

class Biquad {
public:
  void set_params(float cutoff, float q, float dt) {
    float w = cutoff * dt;
    (void)w;
    // coeffs = ...
  }

  float operator()(float input) {
    float out = input * coeffs.a0 + state.z1 * coeffs.b1;
    state.z1 = out;
    return out;
  }

  void reset() { state = {}; }

private:
  FilterCoeffs coeffs{};
  FilterState state{};
};
```

```rust
#[derive(Clone, Copy, Default)]
struct FilterCoeffs { a0: f32, a1: f32, b1: f32, b2: f32 }

#[derive(Clone, Copy, Default)]
struct FilterState { z1: f32, z2: f32 }

struct Biquad {
  coeffs: FilterCoeffs,
  state: FilterState,
}

impl Biquad {
  fn set_params(&mut self, cutoff: f32, _q: f32, dt: f32) {
    let _w = cutoff * dt;
    // self.coeffs = ...
  }

  fn process(&mut self, input: f32) -> f32 {
    let out = input * self.coeffs.a0 + self.state.z1 * self.coeffs.b1;
    self.state.z1 = out;
    out
  }

  fn reset(&mut self) { self.state = FilterState::default(); }
}
```

---

  ### 3.8 追加要素（言語別の注意点と推奨）

  #### C++ で追加すべき配慮
  - **データ競合の回避**: 共有係数と状態が同一オブジェクトに混在しない設計を徹底。
  - **メモリアラインメント**: SIMDやキャッシュ効率を意識して、係数/状態の配置を固定化。
  - **所有権の明示**: `const` を活用して共有データの不変性を強制。
  - **例外禁止/無割込み**: リアルタイム処理では例外や動的確保を避ける。
  - **イテレータ利用の方針**: 最適化が効く範囲では使用してよいが、デフォルトは添字ループで統一。
    - ホットパスは「予測可能な最小オーバーヘッド」を優先。
    - 必要なら同等の処理でベンチを取り、差がないことを確認する。

  ```cpp
  // 共有係数はconst参照のみ許可
  void process(const FilterCoeffs& coeffs, FilterState& state, float* out, size_t n) {
    for (size_t i = 0; i < n; ++i) {
      float y = out[i] * coeffs.a0 + state.z1 * coeffs.b1;
      state.z1 = y;
      out[i] = y;
    }
  }
  ```

  #### Rust で追加すべき配慮
  - **unsafe最小化**: 共有バッファの書き換えは関数境界で隔離し、検証可能にする。
  - **所有権/借用の明確化**: `&mut` がDSPスレッド専有であることを型で保証。
  - **アトミック境界の整理**: `Acquire/Release` の範囲を最小にし、可視性を担保。
  - **no_std 対応の余地**: 組込み向けなら `core` ベースで設計。

  ```rust
  fn process(coeffs: &FilterCoeffs, state: &mut FilterState, out: &mut [f32]) {
    for x in out.iter_mut() {
      let y = *x * coeffs.a0 + state.z1 * coeffs.b1;
      state.z1 = y;
      *x = y;
    }
  }
  ```

  #### 共通の実践ルール
  - **計算と状態更新の分離**: 係数計算は低頻度、サンプル処理は最小コスト。
  - **スレッド境界の明示**: UI更新とDSP処理を混在させない。
  - **APIの一貫性**: `set_params` / `process` / `reset` の責務を固定。
  - **純粋関数はconstexpr化**: 状態を持たない処理は `constexpr` / `const fn` を優先し、コンパイル時計算できる形にする。
  - **定数は標準定義を使用**: C++は `std::numbers`、Rustは `std::f32::consts` などを基本とする。

  #### constexpr 対応の整理（現状前提）

  - **確実にコンパイル時計算できる範囲**
    - 数値リテラル、加減乗除、`std::numbers::*` などの定数。
    - 自前の簡単な補助関数（`lerp`, `clamp` 等）。

  - **環境依存・未保証の範囲**
    - `std::sin`, `std::cos`, `std::tan`, `std::exp`, `std::log`, `std::pow` などの数学関数。
    - 実装（libstdc++/libc++）、コンパイラ（gcc-arm/arm-clang）、標準バージョンに依存。

  **方針**: `constexpr` 前提で書くのは「自前の単純関数」と「標準定数」に限定し、
  `std::pow` などは**ランタイム前提**にする。必要ならテーブル化や近似関数で代替。

  ```cpp
  #include <numbers>

  constexpr float db_to_gain(float db) {
    // std::pow はconstexpr保証なしのためランタイムで扱う前提
    return std::pow(10.0f, db / 20.0f);
  }

  constexpr float kPi = std::numbers::pi_v<float>;
  ```

  ```rust
  const PI: f32 = std::f32::consts::PI;

  const fn lerp(a: f32, b: f32, t: f32) -> f32 {
    a + (b - a) * t
  }
  ```

  #### DSPの構造分類（純粋関数 / 状態を持つユニット / 複合モデル）

  - **純粋関数（状態なし・構造的に1つの関数）**
    - 例: `lerp`, `clamp`, `midi_to_freq`, `db_to_gain`（近似版）
    - `constexpr` / `const fn` を優先。

  **例: 純粋関数（C++/Rust）**

  ```cpp
  constexpr float clamp(float x, float lo, float hi) {
    return (x < lo) ? lo : (x > hi) ? hi : x;
  }
  ```

  ```rust
  const fn clamp(x: f32, lo: f32, hi: f32) -> f32 {
    if x < lo { lo } else if x > hi { hi } else { x }
  }
  ```

  - **状態を持つユニット（状態+係数+処理が1セット）**
    - 例: オシレーター（位相）、フィルター（遅延要素）、エンベロープ（現在値）
    - `set_params()` で係数、`process()` で状態更新。

  **例: 状態を持つユニット（C++/Rust）**

  ```cpp
  struct OnePoleCoeffs { float a0 = 0.0f, b1 = 0.0f; };
  struct OnePoleState  { float z1 = 0.0f; };

  inline float onepole_process(const OnePoleCoeffs& c, OnePoleState& s, float in) {
    float y = in * c.a0 + s.z1 * c.b1;
    s.z1 = y;
    return y;
  }
  ```

  ```rust
  #[derive(Clone, Copy, Default)]
  struct OnePoleCoeffs { a0: f32, b1: f32 }
  #[derive(Clone, Copy, Default)]
  struct OnePoleState { z1: f32 }

  fn onepole_process(c: OnePoleCoeffs, s: &mut OnePoleState, input: f32) -> f32 {
    let y = input * c.a0 + s.z1 * c.b1;
    s.z1 = y;
    y
  }
  ```

  - **複合モデル（複数ユニットの合成構造）**
    - 例: シンセボイス（OSC + ENV + FILTER）、ディレイライン（遅延 + フィードバック）
    - フィルター単体でも「カスケード」「非線形挿入」などでモデル化される。
    - 複数の係数・状態を束ね、更新順序を明示した「小さなパイプライン」を構成する。

  **例1: 2段カスケード・ローパス（C++）**

  ```cpp
  struct OnePoleCoeffs { float a0 = 0.0f, b1 = 0.0f; };
  struct OnePoleState  { float z1 = 0.0f; };

  inline float onepole_process(const OnePoleCoeffs& c, OnePoleState& s, float in) {
    float y = in * c.a0 + s.z1 * c.b1;
    s.z1 = y;
    return y;
  }

  struct TwoPoleCascade {
    OnePoleCoeffs c1, c2;
    OnePoleState  s1, s2;

    float process(float in) {
      float y1 = onepole_process(c1, s1, in);
      float y2 = onepole_process(c2, s2, y1);
      return y2;
    }
  };
  ```

  **例2: 非線形要素を挿入したフィルター（C++）**

  ```cpp
  inline float soft_clip(float x) {
    return x / (1.0f + std::abs(x));
  }

  struct SatLPF {
    OnePoleCoeffs c;
    OnePoleState  s;

    float process(float in) {
      float y = onepole_process(c, s, in);
      return soft_clip(y);
    }
  };
  ```

  **例3: モデルとしてのシンセボイス（C++）**

  ```cpp
  struct Voice {
    OscCoeffs osc_c;
    OscState  osc_s;
    OnePoleCoeffs flt_c;
    OnePoleState  flt_s;

    float process(float dt) {
      float osc = osc_process(osc_c, osc_s, dt);
      float out = onepole_process(flt_c, flt_s, osc);
      return out;
    }
  };
  ```

  **Rustでのモデル構成例**

  ```rust
  struct Voice {
    osc: Osc,
    flt: OnePole,
  }

  impl Voice {
    fn process(&mut self, dt: f32) -> f32 {
      let osc = self.osc.process(dt);
      self.flt.process(osc)
    }
  }
  ```

  ---

## 4. 既存ドキュメントとの関係

- DSP API → [docs/reference/API_DSP.md](API_DSP.md)
- 実装規約 → [docs/GUIDELINE.md](../GUIDELINE.md)

このガイドは上記を統合する「実装・利用の橋渡し」です。

---

## 5. 今後の拡張ポイント

- 各DSPモジュールの内部構造（Biquad / SVF / ADSR）の実装メモ追加。
- `umidsp` のベンチ計測指針やSIMD最適化方針の明文化。
- スレッドモデル（UI/DSP/Audio）に対する標準ワークフロー記述。

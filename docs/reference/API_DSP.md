# DSP モジュール

```cpp
#include <umidsp/oscillator.hh>
#include <umidsp/filter.hh>
#include <umidsp/envelope.hh>
```

---

## 設計原則

### 1. `operator()` をプライマリ、`tick` をエイリアス

```cpp
// 推奨: operator() - 短く、関数合成が自然
out[i] = filter(osc(freq));

// エイリアス: tick - 明示的に書きたい場合
out[i] = filter.tick(osc.tick(freq));
```

### 2. `set_params(dt)` で係数を事前計算

```cpp
// パラメータ変更時（低頻度）に係数を計算
filter.set_params(cutoff, resonance, ctx.dt());

// tick() / operator() は事前計算済み係数を使う（高速）
for (int i = 0; i < frames; ++i) {
    out[i] = filter(in[i]);  // 乗算のみ、exp/除算なし
}
```

### 3. `dt` を使う（`sr` での除算を避ける）

```cpp
// ✅ 乗算（約1サイクル）
float w = cutoff * dt;

// ❌ 除算（約14サイクル @ Cortex-M4）
float w = cutoff / sr;
```

`dt` は `AudioContext::dt` から取得（`1.0f / sample_rate` を事前計算済み）。

---

## 基本インターフェース

全 DSP オブジェクトは以下のパターンに従います：

```cpp
class DspObject {
public:
    // パラメータ設定（係数を事前計算）
    void set_params(/* params */, float dt);
    
    // サンプル処理（プライマリ）
    float operator()(float input);
    
    // サンプル処理（エイリアス）
    float tick(float input) { return (*this)(input); }
    
    // 状態リセット
    void reset();
};
```

---

## Oscillators

```cpp
umi::dsp::Sine sine;
umi::dsp::SawBL saw;      // バンドリミテッド
umi::dsp::SquareBL square;
umi::dsp::Triangle tri;

// 周波数設定（dt を使用）
float freq_norm = 440.0f * ctx.dt();

// サンプル生成
float sample = sine(freq_norm);     // operator()
float sample = sine.tick(freq_norm); // エイリアス
```

---

## Filters

### Biquad

```cpp
umi::dsp::Biquad bq;

// パラメータ設定（係数を事前計算）
bq.set_lowpass(cutoff, q, ctx.dt());

// サンプル処理
float out = bq(input);
```

### State Variable Filter

```cpp
umi::dsp::SVF svf;

// パラメータ設定（cutoff, resonance から g, k を計算）
svf.set_params(cutoff, resonance, ctx.dt());

// サンプル処理（複数出力）
svf(input);
float lp = svf.lp();
float hp = svf.hp();
float bp = svf.bp();
```

---

## Envelopes

```cpp
umi::dsp::ADSR env;

// パラメータ設定（係数を事前計算）
env.set_params(0.01f, 0.1f, 0.7f, 0.3f, ctx.dt());  // A, D, S, R, dt

// 制御
env.trigger();   // Note On
env.release();   // Note Off

// サンプル処理（引数なし - 係数は事前計算済み）
float val = env();
```

---

## 使用例

```cpp
void MyProcessor::process(umi::ProcessContext& ctx) {
    // パラメータ変更があれば係数を再計算
    if (param_changed_) {
        filter_.set_params(cutoff_, resonance_, ctx.dt());
        env_.set_params(a_, d_, s_, r_, ctx.dt());
        param_changed_ = false;
    }
    
    auto* out = ctx.output(0);
    for (uint32_t i = 0; i < ctx.frames(); ++i) {
        // operator() で簡潔に記述
        float osc_out = osc_(freq_norm_);
        float env_out = env_();
        float flt_out = filter_(osc_out * env_out);
        out[i] = flt_out * gain_;
    }
}
```

---

## ユーティリティ

```cpp
float freq = umi::dsp::midi_to_freq(69);      // A4 = 440Hz
float gain = umi::dsp::db_to_gain(-6.0f);     // ≈ 0.5
float soft = umi::dsp::soft_clip(x);
```

---

## 関連ドキュメント

- [../README.md](../README.md) - ドキュメント目次
- [API_APPLICATION.md](API_APPLICATION.md) - アプリケーションAPI
- [API_UI.md](API_UI.md) - UI API
- [API_KERNEL.md](API_KERNEL.md) - Kernel API

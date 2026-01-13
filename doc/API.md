# UMI-OS API リファレンス

このドキュメントでは、UMI-OS の主要な API を解説します。

---

## 目次

1. [Core モジュール (lib/core/)](#core-モジュール)
2. [DSP モジュール (lib/dsp/)](#dsp-モジュール)
3. [エラーハンドリング](#エラーハンドリング)

---

## Core モジュール

### AudioContext

オーディオ処理コールバックに渡されるコンテキスト。

```cpp
#include <umi/audio_context.hh>

struct AudioContext {
    std::span<const sample_t* const> inputs;   // 入力バッファ
    std::span<sample_t* const> outputs;        // 出力バッファ
    EventQueue<>& events;                      // イベントキュー
    uint32_t sample_rate;                      // サンプルレート (Hz)
    uint32_t buffer_size;                      // バッファサイズ
    float dt;                                  // 1サンプルあたりの時間 (秒)
    sample_position_t sample_position;         // 絶対サンプル位置
};
```

#### メソッド

| メソッド | 説明 |
|----------|------|
| `num_inputs()` | 入力チャンネル数 |
| `num_outputs()` | 出力チャンネル数 |
| `input(ch)` | チャンネル ch の入力バッファ (範囲外は nullptr) |
| `output(ch)` | チャンネル ch の出力バッファ (範囲外は nullptr) |
| `input_checked(ch)` | エラーハンドリング付き入力バッファ取得 |
| `output_checked(ch)` | エラーハンドリング付き出力バッファ取得 |
| `clear_outputs()` | 全出力バッファをゼロクリア |
| `passthrough()` | 入力を出力にコピー |

#### 使用例

```cpp
void MyProcessor::process(umi::AudioContext& ctx) {
    auto* out = ctx.output(0);
    if (!out) return;

    for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
        out[i] = generate_sample();
    }
}
```

---

### Processor Concepts

プロセッサの型制約を定義する C++20 concepts。

```cpp
#include <umi/processor.hh>

// 基本的なプロセッサ
template<typename P>
concept ProcessorLike = requires(P& p, AudioContext& ctx) {
    { p.process(ctx) } -> std::same_as<void>;
};

// 制御可能なプロセッサ
template<typename P>
concept Controllable = ProcessorLike<P> &&
    requires(P& p, ControlContext& ctx) {
        { p.control(ctx) } -> std::same_as<void>;
    };
```

#### AnyProcessor (型消去ラッパー)

```cpp
SineOscillator osc(48000);
umi::AnyProcessor any(osc);

any.process(ctx);           // 処理実行
any.has_control();          // control() メソッドがあるか
any.control(ctrl_ctx);      // 制御コールバック
```

---

### Event / EventQueue

サンプル精度のイベント処理。

```cpp
#include <umi/event.hh>

// イベントの作成
auto note_on = umi::Event::note_on(sample_pos, channel, note, velocity);
auto note_off = umi::Event::note_off(sample_pos, channel, note);

// イベントキュー
umi::EventQueue<64> queue;  // 最大64イベント
queue.push(note_on);
queue.push_midi(sample_pos, 0x90, 60, 127);
queue.push_param(param_id, sample_pos, value);

// イベントの取得
umi::Event e;
while (queue.pop_until(current_sample, e)) {
    // イベント処理
}
```

---

### ParamDescriptor

パラメータのメタデータ。

```cpp
#include <umi/processor.hh>

umi::ParamDescriptor freq{
    .id = 0,
    .name = "Frequency",
    .default_value = 440.0f,
    .min_value = 20.0f,
    .max_value = 20000.0f
};

float normalized = freq.normalize(440.0f);   // → 0.021...
float display = freq.denormalize(0.5f);      // → 10010.0
float clamped = freq.clamp(50000.0f);        // → 20000.0
```

---

## DSP モジュール

全て `namespace umi::dsp` 内で定義されています。

### 定数

```cpp
#include <dsp/dsp.hh>

umi::dsp::kPi;   // π (3.14159...)
umi::dsp::k2Pi;  // 2π (6.28318...)
```

---

### Oscillators

```cpp
#include <dsp/oscillator.hh>
```

#### Phase (位相アキュムレータ)

```cpp
umi::dsp::Phase phase;
float freq_norm = 440.0f / 48000.0f;  // 正規化周波数

float p = phase.tick(freq_norm);  // 位相を進める (0.0-1.0)
phase.reset();                     // リセット
phase.set(0.5f);                   // 位相を設定
```

#### Sine (正弦波)

```cpp
umi::dsp::Sine sine;
float sample = sine.tick(freq_norm);  // -1.0 to 1.0
```

#### SawNaive / SawBL (のこぎり波)

```cpp
umi::dsp::SawNaive saw_naive;  // ナイーブ実装 (エイリアシングあり)
umi::dsp::SawBL saw_bl;        // PolyBLEP (バンドリミテッド)

float sample = saw_bl.tick(freq_norm);
```

#### SquareNaive / SquareBL (矩形波)

```cpp
umi::dsp::SquareNaive square;
float sample = square.tick(freq_norm, 0.5f);  // pulse_width = 50%

umi::dsp::SquareBL square_bl;
float sample = square_bl.tick(freq_norm, 0.25f);  // 25% pulse
```

#### Triangle (三角波)

```cpp
umi::dsp::Triangle tri;
float sample = tri.tick(freq_norm);
```

---

### Filters

```cpp
#include <dsp/filter.hh>
```

#### OnePole (1次ローパス)

```cpp
umi::dsp::OnePole lp;
lp.set_cutoff(0.1f);  // 正規化カットオフ (0.0-0.5)

float out = lp.tick(input);
lp.reset();
```

#### Biquad (バイクワッドフィルタ)

```cpp
umi::dsp::Biquad bq;
bq.set_lowpass(0.1f, 0.707f);   // カットオフ, Q
bq.set_highpass(0.1f, 0.707f);
bq.set_bandpass(0.1f, 1.0f);
bq.set_notch(0.1f, 1.0f);

float out = bq.tick(input);
```

#### SVF (ステートバリアブルフィルタ)

```cpp
umi::dsp::SVF svf;
svf.set_params(0.1f, 0.5f);  // カットオフ, レゾナンス

svf.tick(input);
float lp = svf.lp();      // ローパス出力
float bp = svf.bp();      // バンドパス出力
float hp = svf.hp();      // ハイパス出力
float notch = svf.notch(); // ノッチ出力
```

---

### Envelopes

```cpp
#include <dsp/envelope.hh>
```

#### ADSR

```cpp
umi::dsp::ADSR env;
env.set_params(
    0.01f,  // Attack (秒)
    0.1f,   // Decay (秒)
    0.7f,   // Sustain (0.0-1.0)
    0.3f    // Release (秒)
);

env.trigger();  // ノートオン
float value = env.tick(dt);  // dt = 1.0 / sample_rate

env.release();  // ノートオフ

// 状態取得
auto state = env.state();  // Idle, Attack, Decay, Sustain, Release
```

#### Ramp (線形ランプ)

```cpp
umi::dsp::Ramp ramp;
ramp.set_target(1.0f, 0.1f);  // 目標値, 時間(秒)

float value = ramp.tick(dt);
```

---

### ユーティリティ関数

```cpp
#include <dsp/dsp.hh>

// MIDI → 周波数変換
float freq = umi::dsp::midi_to_freq(69);  // A4 = 440Hz

// 周波数正規化
float norm = umi::dsp::normalize_freq(440.0f, 48000.0f);

// クリッピング
float soft = umi::dsp::soft_clip(x);      // ソフトクリップ
float hard = umi::dsp::hard_clip(x, 1.0f); // ハードクリップ

// 補間
float v = umi::dsp::lerp(a, b, t);

// dB ⇔ ゲイン変換
float gain = umi::dsp::db_to_gain(-6.0f);  // ≈ 0.5
float db = umi::dsp::gain_to_db(0.5f);     // ≈ -6.0
```

---

## エラーハンドリング

```cpp
#include <umi/error.hh>
```

### Error 列挙型

```cpp
enum class Error : uint8_t {
    None,
    // リソースエラー
    OutOfMemory, OutOfTasks, OutOfTimers, OutOfBuffers,
    // 状態エラー
    InvalidTask, InvalidState, AlreadyRunning, NotRunning,
    // パラメータエラー
    InvalidParam, NullPointer, BufferTooSmall,
    // タイムアウト
    Timeout, WouldBlock,
    // ハードウェア
    HardwareFault, DmaError,
    // オーディオ
    BufferOverrun, BufferUnderrun, SampleRateError,
    // MIDI
    MidiParseError, MidiBufferFull,
};
```

### Result / Expected

C++23 `std::expected` を使用した Rust 風エラーハンドリング。

```cpp
using umi::Result;
using umi::Ok;
using umi::Err;

Result<int> divide(int a, int b) {
    if (b == 0) return Err(Error::InvalidParam);
    return Ok(a / b);
}

// 使用例
auto result = divide(10, 2);
if (result) {
    int value = *result;
} else {
    Error e = result.error();
}

// AudioContext での使用
auto out = ctx.output_checked(0);
if (!out) {
    // チャンネル範囲外
    return;
}
sample_t* buffer = *out;
```

### ユーティリティ

```cpp
bool recoverable = umi::is_recoverable(e);  // 再試行可能か
bool fatal = umi::is_fatal(e);              // 致命的か
const char* msg = umi::error_to_string(e);  // 文字列変換
```

---

## ビルドとテスト

```bash
# 全てビルド
xmake build -a

# テスト実行
xmake test

# DSP テストのみ
xmake run test_dsp
```

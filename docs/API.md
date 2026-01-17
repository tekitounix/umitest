# UMI-OS API リファレンス

このドキュメントでは、UMI-OS の主要な API を解説します。

---

## 目次

1. [Core モジュール (lib/core/)](#core-モジュール)
2. [Kernel モジュール](#kernel-モジュール)
3. [DSP モジュール (lib/dsp/)](#dsp-モジュール)
4. [エラーハンドリング](#エラーハンドリング)

---

## Core モジュール

### AudioContext

オーディオ処理コールバックに渡されるコンテキスト。

```cpp
#include <core/audio_context.hh>

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
#include <core/processor.hh>

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

### ControlContext

非リアルタイムの制御コールバックに渡されるコンテキスト。
`control()` メソッドで使用されます。

```cpp
#include <core/processor.hh>

struct ControlContext {
    float delta_time;              // 前回呼び出しからの経過時間 (秒)
    sample_position_t sample_pos;  // 現在のサンプル位置 (参考値)
};
```

#### 用途

| 用途 | 説明 |
|------|------|
| パラメータスムージング | 時間ベースの補間 |
| LFO/エンベロープ更新 | 低精度で十分な変調 |
| UI状態の同期 | メーター値の更新 |
| 非同期タスク完了処理 | ファイル読み込み完了など |

#### 使用例

```cpp
struct MySynth {
    void process(umi::AudioContext& ctx);       // リアルタイム
    void control(umi::ControlContext& ctx);     // 非リアルタイム
};

void MySynth::control(umi::ControlContext& ctx) {
    // パラメータの補間 (control()で実行、process()はロックフリー読み取り)
    smoothed_cutoff_ += (target_cutoff_ - smoothed_cutoff_)
                        * (1.0f - std::exp(-ctx.delta_time * 10.0f));
}
```

#### AudioContext vs ControlContext

| 項目 | AudioContext | ControlContext |
|------|--------------|----------------|
| スレッド | Audio Thread | Control Thread |
| 呼び出し頻度 | バッファごと | 可変 (10-60Hz) |
| メモリ確保 | 禁止 | 許可 |
| ブロッキング | 禁止 | 許可 |
| 精度 | サンプル精度 | バッファ精度以下 |

---

### Event / EventQueue

サンプル精度のイベント処理。

```cpp
#include <core/event.hh>

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
#include <core/processor.hh>

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

## Kernel モジュール

umios カーネルは組み込み向けリアルタイムタスクスケジューラです。

```cpp
#include <core/umi_kernel.hh>
```

---

### Kernel クラス

```cpp
template <std::size_t MaxTasks, std::size_t MaxTimers, class HW>
class umi::Kernel;

// 使用例 (STM32F4)
#include <port/board/stm32f4/hw_impl.hh>
using MyKernel = umi::board::stm32f4::Kernel<8, 8>;
MyKernel kernel;
```

#### Task Management

| メソッド | 説明 |
|----------|------|
| `create_task(cfg)` | タスクを作成 (`TaskId` を返す) |
| `resume_task(id)` | タスクを Ready 状態に |
| `suspend_task(id)` | タスクを Blocked 状態に |
| `delete_task(id)` | タスクを削除 |
| `current_task()` | 現在実行中のタスクID |
| `get_task_name(id)` | タスク名を取得 |
| `get_task_priority(id)` | 優先度を取得 |

#### タスク作成例

```cpp
umi::TaskConfig cfg{
    .entry = [](void*) { /* task code */ },
    .arg = nullptr,
    .prio = umi::Priority::User,
    .name = "my_task"
};
auto task_id = kernel.create_task(cfg);
kernel.resume_task(task_id);
```

---

### Priority (優先度)

```cpp
enum class Priority : uint8_t {
    Realtime = 0,  // オーディオ処理、DMA - 最高優先度
    Server   = 1,  // ドライバ、I/O
    User     = 2,  // アプリケーション (ラウンドロビン)
    Idle     = 3,  // バックグラウンド - 最低優先度
};
```

---

### Notification (タスク通知)

ISRやタスク間でイベントを通知するロックフリー機構。

```cpp
// 通知を送信 (ISR-safe)
kernel.notify(task_id, umi::KernelEvent::MidiReady);

// 非ブロッキング受信 (Realtime タスク向け)
uint32_t bits = kernel.wait(task_id, umi::KernelEvent::MidiReady);

// ブロッキング受信 (User タスク向け)
uint32_t bits = kernel.wait_block(task_id,
    umi::KernelEvent::AudioReady | umi::KernelEvent::MidiReady);
```

#### イベント定数

```cpp
namespace umi::KernelEvent {
    constexpr uint32_t AudioReady = 1 << 0;
    constexpr uint32_t MidiReady  = 1 << 1;
    constexpr uint32_t VSync      = 1 << 2;
}
```

---

### SpscQueue (ロックフリーキュー)

Single Producer Single Consumer キュー。ISR-task 間通信に最適。

```cpp
umi::SpscQueue<int, 64> queue;  // 64要素、2のべき乗

// Producer (ISR or task)
queue.try_push(42);

// Consumer (task)
if (auto val = queue.try_pop()) {
    process(*val);
}

// バッチ読み取り
std::array<int, 16> buf;
size_t n = queue.read_all(buf);
```

| メソッド | 説明 |
|----------|------|
| `try_push(item)` | アイテムを追加 (full なら false) |
| `try_pop()` | アイテムを取り出し (`optional`) |
| `peek()` | 先頭を覗く (消費しない) |
| `read_all(span)` | まとめて読み取り |
| `size_approx()` | 概算サイズ |
| `empty_approx()` | 空かどうか (概算) |

---

### TimerQueue (タイマー)

```cpp
// 1秒後にコールバック
kernel.call_later(1000000, {
    .fn = [](void* ctx) { /* callback */ },
    .ctx = nullptr
});

// 現在時刻 (マイクロ秒)
umi::usec now = kernel.time();
```

---

### LoadMonitor (CPU負荷監視)

```cpp
umi::LoadMonitor<HW, 8> load;  // 8サンプル移動平均

void audio_callback() {
    load.begin();
    // ... 処理 ...
    load.end(budget_cycles);

    float pct = umi::LoadMonitor<HW>::to_percent(load.instant());
}

load.instant();   // 瞬時負荷 (0-10000)
load.average();   // 平均負荷
load.peak();      // ピーク負荷
```

---

### Stopwatch (ストップウォッチ)

```cpp
umi::Stopwatch<HW> sw;
sw.start();
// ... 処理 ...
uint32_t cycles = sw.stop();
umi::usec us = sw.elapsed_usecs();
```

---

### Hardware Abstraction (HAL)

新しいボードに移植する場合、`Hw` 構造体を実装します。

```cpp
struct MyBoardHw {
    // タイマー
    static void set_timer_absolute(umi::usec target);
    static umi::usec monotonic_time_usecs();

    // クリティカルセクション
    static void enter_critical();
    static void exit_critical();

    // コンテキストスイッチ
    static void request_context_switch();

    // ... 他のメソッドは port/board/stm32f4/hw_impl.hh 参照
};

using Kernel = umi::Kernel<8, 8, umi::Hw<MyBoardHw>>;
```

詳細は [port/README.md](../port/README.md) を参照。

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
#include <core/error.hh>
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

# UMI API リファレンス

UMI の主要な API を解説します。

---

## 目次

1. [アプリケーション API](#アプリケーション-api)
2. [Kernel API (組込み)](#kernel-api-組込み)
3. [DSP モジュール](#dsp-モジュール)
4. [エラーハンドリング](#エラーハンドリング)

---

## アプリケーション API

統一 `main()` モデルで使用するAPI。組込み/Web共通。

```cpp
#include <umi/app.hh>
```

### ライフサイクル

| 関数 | 説明 |
|------|------|
| `umi::register_processor(proc)` | Processorをカーネルに登録 |
| `umi::wait_event()` | イベント待機（ブロッキング） |
| `umi::send_event(event)` | イベント送信 |
| `umi::log(msg)` | ログ出力 |
| `umi::get_time()` | 現在時刻取得（μs） |

### 最小実装例

```cpp
#include <umi/app.hh>

struct Volume {
    float gain = 1.0f;
    
    void process(umi::ProcessContext& ctx) {
        auto* out = ctx.output(0);
        auto* in = ctx.input(0);
        for (uint32_t i = 0; i < ctx.frames(); ++i) {
            out[i] = in[i] * gain;
        }
    }
};

int main() {
    static Volume vol;
    umi::register_processor(vol);
    
    while (umi::wait_event().type != umi::EventType::Shutdown) {}
    return 0;
}
```

---

### ProcessContext

オーディオ処理コールバック `process()` に渡されるコンテキスト。

```cpp
struct ProcessContext {
    uint32_t frames();              // バッファサイズ
    uint32_t sample_rate();         // サンプルレート
    float* output(uint32_t ch);     // 出力バッファ
    const float* input(uint32_t ch); // 入力バッファ
    
    // イベント
    auto events();                  // MIDIイベント等のイテレータ
    void send_to_control(Event e);  // Control Taskへイベント送信
};
```

#### 使用例

```cpp
void MyProcessor::process(umi::ProcessContext& ctx) {
    // MIDIイベント処理
    for (const auto& ev : ctx.events()) {
        if (ev.is_note_on()) {
            note_on(ev.note(), ev.velocity());
        }
    }
    
    // オーディオ生成
    auto* out = ctx.output(0);
    for (uint32_t i = 0; i < ctx.frames(); ++i) {
        out[i] = generate_sample();
    }
}
```

---

### Event

イベント型。MIDIメッセージ、パラメータ変更、UI入力など。

```cpp
namespace umi {
enum class EventType {
    Shutdown,
    MidiNoteOn, MidiNoteOff, MidiCC, MidiPitchBend,
    ParamChange,
    EncoderRotate, ButtonPress, ButtonRelease,
    DisplayUpdate, Meter,
};

struct Event {
    EventType type;
    union {
        struct { uint8_t ch, note, vel; } midi;
        struct { uint32_t id; float value; } param;
        struct { int id, delta; } encoder;
        struct { int id; } button;
    };
};
}
```

---

### パラメータ操作

```cpp
// Control Task から Processor のパラメータを変更
umi::set_param(PARAM_VOLUME, 0.8f);
float val = umi::get_param(PARAM_VOLUME);
```

---

### コルーチン (C++20)

```cpp
#include <umi/coro.hh>

umi::Task<void> my_task() {
    while (true) {
        auto ev = co_await umi::wait_event_async();
        if (ev.type == umi::EventType::Shutdown) co_return;
        handle(ev);
    }
}

umi::Task<void> display_task() {
    while (true) {
        co_await umi::sleep(33ms);  // 30fps
        update_display();
    }
}

int main() {
    static MyProcessor proc;
    umi::register_processor(proc);
    
    umi::Scheduler<4> sched;
    sched.spawn(my_task());
    sched.spawn(display_task());
    sched.run();
    
    return 0;
}
```

---

## Kernel API (組込み)

組込み環境でのカーネル直接操作。アプリケーションは通常 syscall 経由でアクセス。

```cpp
#include <umios/kernel/umi_kernel.hh>
```

### Syscall ABI

```cpp
namespace umi::syscall {
    constexpr uint32_t Exit = 0;          // アプリ終了
    constexpr uint32_t RegisterProc = 1;  // Processor登録
    constexpr uint32_t WaitEvent = 2;     // イベント待機
    constexpr uint32_t SendEvent = 3;     // イベント送信
    constexpr uint32_t Log = 10;          // ログ出力
    constexpr uint32_t GetTime = 11;      // 時刻取得
}
```

### 動的IRQ登録

```cpp
#include <umios/backend/cm/irq.hh>

// IRQハンドラをラムダで登録
umi::irq::init();
umi::irq::set_handler(irqn::DMA1_Stream5, +[]() {
    if (dma_i2s.transfer_complete()) {
        dma_i2s.clear_tc();
        g_kernel.notify(g_audio_task_id, Event::I2sReady);
    }
});
```

### Priority (優先度)

```cpp
enum class Priority : uint8_t {
    Realtime = 0,  // オーディオ処理 - 最高
    Server   = 1,  // ドライバ、I/O
    User     = 2,  // アプリケーション
    Idle     = 3,  // バックグラウンド - 最低
};
```

### Notification (タスク通知)

```cpp
// 通知を送信 (ISR-safe)
kernel.notify(task_id, Event::AudioReady);

// ブロッキング受信
uint32_t bits = kernel.wait(task_id, Event::AudioReady | Event::MidiReady);
```

### SpscQueue (ロックフリーキュー)

```cpp
umi::SpscQueue<int, 64> queue;

// Producer (ISR or task)
queue.try_push(42);

// Consumer (task)
if (auto val = queue.try_pop()) {
    process(*val);
}
```

---

## DSP モジュール

```cpp
#include <umidsp/oscillator.hh>
#include <umidsp/filter.hh>
#include <umidsp/envelope.hh>
```

### Oscillators

```cpp
umi::dsp::Sine sine;
umi::dsp::SawBL saw;      // バンドリミテッド
umi::dsp::SquareBL square;
umi::dsp::Triangle tri;

float freq_norm = 440.0f / sample_rate;
float sample = sine.tick(freq_norm);
```

### Filters

```cpp
// Biquad
umi::dsp::Biquad bq;
bq.set_lowpass(cutoff_norm, 0.707f);
float out = bq.tick(input);

// State Variable Filter
umi::dsp::SVF svf;
svf.set_params(cutoff_norm, resonance);
svf.tick(input);
float lp = svf.lp();
float hp = svf.hp();
```

### Envelopes

```cpp
umi::dsp::ADSR env;
env.set_params(0.01f, 0.1f, 0.7f, 0.3f);  // A, D, S, R

env.trigger();   // Note On
float val = env.tick(dt);
env.release();   // Note Off
```

### ユーティリティ

```cpp
float freq = umi::dsp::midi_to_freq(69);      // A4 = 440Hz
float gain = umi::dsp::db_to_gain(-6.0f);     // ≈ 0.5
float soft = umi::dsp::soft_clip(x);
```

---

## エラーハンドリング

```cpp
#include <umi/error.hh>

enum class Error : uint8_t {
    None,
    OutOfMemory, OutOfTasks,
    InvalidTask, InvalidState,
    InvalidParam, NullPointer,
    Timeout, WouldBlock,
    HardwareFault, DmaError,
    BufferOverrun, BufferUnderrun,
};

// Result型（C++23 std::expected）
umi::Result<int> divide(int a, int b) {
    if (b == 0) return umi::Err(Error::InvalidParam);
    return umi::Ok(a / b);
}

auto result = divide(10, 2);
if (result) {
    int value = *result;
}
```

---

## 共有メモリとハードウェアI/O

アプリケーションはカーネルと**共有メモリ**経由で通信します。
直接ハードウェアにアクセスすることはできません（MPUで保護）。

### アーキテクチャ

```
+------------------+     +------------------+     +------------------+
|  Hardware        |     |  Kernel + BSP    |     |  Application     |
|  (ADC, GPIO,     | --> |  (IRQ handlers,  | --> |  (main, process) |
|   Encoder, I2S)  |     |   DMA, drivers)  |     |                  |
+------------------+     +------------------+     +------------------+
                              |
                              v
                    +------------------+
                    |  Shared Memory   |
                    |  - AudioBuffer   |
                    |  - EventQueue    |
                    |  - ParamBlock    |
                    |  - HardwareState |
                    |  - DisplayBuffer |
                    +------------------+
```

### 共有メモリの実装

#### メモリ配置とハードウェア構成

共有メモリの物理配置はBSPが担当。APIは共通。

| ハードウェア構成 | 共有メモリ配置 | 注意点 |
|------------------|----------------|--------|
| 内蔵SRAMのみ | `.shared` セクション | サイズ制約 |
| 内蔵 + SDRAM | オーディオはSDRAM | キャッシュ設定 |
| CCM + SRAM | 高速データはCCM | DMA不可 |

#### BSPによる抽象化

```cpp
// lib/bsp/<board>/shared_memory.hh

// BSPがボード固有の配置を定義
namespace bsp {

// リンカスクリプトと連携
extern SharedRegion __shared_region __attribute__((section(".shared")));

// 初期化（キャッシュ、MPU設定含む）
void init_shared_memory() {
    // SDRAM初期化（必要なら）
    init_sdram();
    
    // MPU設定: 共有領域をnon-cacheable or write-through
    mpu::configure_region(MPU_REGION_SHARED, {
        .base = &__shared_region,
        .size = sizeof(SharedRegion),
        .access = mpu::RW_RW,
        .attributes = mpu::SHARED_DEVICE,  // キャッシュ無効
    });
}

}
```

#### リンカスクリプト例（SDRAM使用時）

```ld
/* boards/my_board/linker.ld */
MEMORY {
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1M
    SRAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 128K
    SDRAM (rwx) : ORIGIN = 0xD0000000, LENGTH = 8M
}

SECTIONS {
    /* オーディオバッファはSDRAMに配置 */
    .shared_audio (NOLOAD) : {
        . = ALIGN(32);  /* キャッシュライン境界 */
        *(.shared_audio)
    } > SDRAM
    
    /* イベント/パラメータは高速SRAMに配置 */
    .shared_fast (NOLOAD) : {
        *(.shared_fast)
    } > SRAM
}
```

#### DMAとの整合性

```cpp
// オーディオバッファはDMAと共有
// キャッシュコヒーレンシを保証

// 方法1: Non-cacheable領域（推奨）
// → MPUでSHARED_DEVICEに設定

// 方法2: 手動キャッシュ操作（パフォーマンス重視）
void before_dma_read() {
    SCB_InvalidateDCache_by_Addr(audio_buf, size);
}
void after_dma_write() {
    SCB_CleanDCache_by_Addr(audio_buf, size);
}
```

### 共有メモリ構造

```cpp
// カーネルが管理、アプリからはAPIでアクセス
struct SharedRegion {
    // オーディオバッファ（process()で直接アクセス）
    struct {
        float output[2][BUFFER_SIZE];
        float input[2][BUFFER_SIZE];
    } audio;
    
    // イベントキュー（wait_event()で取得）
    struct {
        Event buffer[64];
        std::atomic<uint32_t> head, tail;
    } events;
    
    // パラメータ（get_param/set_paramでアクセス）
    struct {
        std::atomic<float> values[MAX_PARAMS];
    } params;
    
    // ハードウェア状態（カーネルが更新、アプリは読み取り専用）
    struct {
        uint16_t adc[8];        // ADC値 (0-4095)
        int16_t encoders[4];    // エンコーダ累積値
        uint32_t buttons;       // ボタン状態ビットマップ
        uint32_t timestamp_us;  // 最終更新時刻
    } hw_state;
    
    // ディスプレイバッファ（アプリが書き込み、カーネルが転送）
    struct {
        uint8_t oled[128 * 64 / 8];  // 1bpp
        uint16_t lcd[320 * 240];     // RGB565
        uint32_t dirty;              // 更新フラグ
    } display;
    
    // Processor → Control Task メッセージ
    struct {
        umi::SpscQueue<Event, 32> queue;
    } proc_to_ctrl;
};
```

### カーネルによる共有メモリ更新

アプリは共有メモリを直接操作しません。カーネルがハードウェアイベントを検出し、共有メモリを更新します。

#### 更新フロー

```
Hardware IRQ
    │
    v
┌─────────────────────────────────────────────────────────┐
│  Kernel IRQ Handler                                     │
│  ┌─────────────────┐                                    │
│  │ 1. HW読み取り   │ ADC_DR, GPIO_IDR, TIM_CNT etc.    │
│  └────────┬────────┘                                    │
│           v                                             │
│  ┌─────────────────┐                                    │
│  │ 2. 共有メモリ   │ shared.hw_state.adc[ch] = value;  │
│  │    更新         │ shared.hw_state.encoders[id] += d;│
│  └────────┬────────┘                                    │
│           v                                             │
│  ┌─────────────────┐                                    │
│  │ 3. イベント     │ shared.events.push(event);        │
│  │    キュー追加   │                                    │
│  └────────┬────────┘                                    │
│           v                                             │
│  ┌─────────────────┐                                    │
│  │ 4. タスク通知   │ kernel.notify(ctrl_task, EVENT);  │
│  └─────────────────┘                                    │
└─────────────────────────────────────────────────────────┘
    │
    v
Control Task wakes up (wait_event returns)
```

#### カーネル側実装例

```cpp
// lib/umios/kernel/hw_driver.cc

// ADC変換完了IRQ
void ADC_IRQHandler() {
    uint16_t value = ADC1->DR;
    uint8_t ch = current_channel;
    
    // 共有メモリ更新（アトミック不要、カーネルのみが書き込む）
    g_shared.hw_state.adc[ch] = value;
    g_shared.hw_state.timestamp_us = get_time_us();
    
    // 閾値を超えた変化があればイベント発行
    int16_t diff = value - prev_adc[ch];
    if (std::abs(diff) > ADC_THRESHOLD) {
        prev_adc[ch] = value;
        
        Event ev{
            .type = EventType::AdcChange,
            .adc = { .channel = ch, .value = value / 4095.0f }
        };
        g_shared.events.push(ev);
        g_kernel.notify(g_ctrl_task_id, EVENT_HW);
    }
    
    // 次チャンネルへ
    start_next_adc_channel();
}

// エンコーダ用タイマーIRQ
void TIM_Encoder_IRQHandler() {
    int id = get_encoder_id();
    int16_t count = TIM->CNT;
    int16_t delta = count - prev_count[id];
    
    if (delta != 0) {
        // 共有メモリ更新
        g_shared.hw_state.encoders[id] += delta;
        prev_count[id] = count;
        
        // イベント発行
        Event ev{
            .type = EventType::EncoderRotate,
            .encoder = { .id = id, .delta = delta }
        };
        g_shared.events.push(ev);
        g_kernel.notify(g_ctrl_task_id, EVENT_HW);
    }
}

// GPIO外部割り込み（ボタン）
void EXTI_IRQHandler() {
    uint32_t pin = get_exti_pin();
    bool pressed = (GPIOA->IDR & pin) == 0;  // Active Low
    int btn_id = pin_to_button_id(pin);
    
    // デバウンス（ソフトウェア）
    if (get_time_us() - last_btn_time[btn_id] < DEBOUNCE_US) {
        EXTI->PR = pin;
        return;
    }
    last_btn_time[btn_id] = get_time_us();
    
    // 共有メモリ更新
    if (pressed) {
        g_shared.hw_state.buttons |= (1 << btn_id);
    } else {
        g_shared.hw_state.buttons &= ~(1 << btn_id);
    }
    
    // イベント発行
    Event ev{
        .type = pressed ? EventType::ButtonPress : EventType::ButtonRelease,
        .button = { .id = btn_id }
    };
    g_shared.events.push(ev);
    g_kernel.notify(g_ctrl_task_id, EVENT_HW);
    
    EXTI->PR = pin;  // Clear pending
}
```

### アプリからの共有メモリアクセス

アプリはAPIを通じて共有メモリにアクセスします。

#### 読み取り（イベント取得）

```cpp
// syscall経由でイベントキューから取得
Event umi::wait_event() {
    // SVC #2 → カーネルがevents.pop()してreturn
    return syscall(SYS_WAIT_EVENT);
}

// タイムアウト付き
Event umi::wait_event(uint32_t timeout_us) {
    return syscall(SYS_WAIT_EVENT, timeout_us);
}

// 非ブロッキング
std::optional<Event> umi::poll_event() {
    return syscall(SYS_POLL_EVENT);
}
```

#### 読み取り（ハードウェア状態）

```cpp
// 現在のハードウェア状態をスナップショット取得
HwState umi::get_hw_state() {
    // syscall経由で共有メモリをコピー
    // → アトミックな一貫性を保証
    return syscall(SYS_GET_HW_STATE);
}

// 使用例
auto hw = umi::get_hw_state();
float knob = hw.adc[0] / 4095.0f;
int enc = hw.encoders[0];
bool btn = hw.buttons & 0x01;
```

#### 書き込み（パラメータ）

```cpp
// Control Task → Processor へパラメータ送信
void umi::set_param(uint32_t id, float value) {
    // 共有メモリのatomic<float>に直接書き込み
    // process()はこれをアトミックに読む
    g_shared.params.values[id].store(value, std::memory_order_relaxed);
}

float umi::get_param(uint32_t id) {
    return g_shared.params.values[id].load(std::memory_order_relaxed);
}
```

#### 書き込み（ディスプレイ）

```cpp
// ディスプレイバッファに描画
auto& disp = umi::get_display();
disp.draw_text(0, 0, "Hello");

// dirty フラグを立ててカーネルに転送を依頼
disp.flush();
// → g_shared.display.dirty |= DIRTY_OLED;
// → syscall(SYS_DISPLAY_FLUSH);
```

### Processor ⇔ Control Task 間通信

process()からControl Taskへデータを送る仕組み。

#### ProcessContext経由のイベント送信

```cpp
void MyProcessor::process(umi::ProcessContext& ctx) {
    // オーディオ処理
    float peak = 0.0f;
    for (uint32_t i = 0; i < ctx.frames(); ++i) {
        float sample = generate();
        ctx.output(0)[i] = sample;
        peak = std::max(peak, std::abs(sample));
    }
    
    // メーターをControl Taskに送信（syscallではない！）
    // → proc_to_ctrl キューに直接push
    ctx.send_to_control(Event::meter(0, peak));
}
```

#### Control Task側での受信

```cpp
int main() {
    static Synth synth;
    umi::register_processor(synth);
    
    while (true) {
        auto ev = umi::wait_event();
        
        switch (ev.type) {
        case EventType::Meter:
            // Processorからのメーター値
            update_meter_display(ev.meter.channel, ev.meter.value);
            break;
            
        case EventType::ProcessorMessage:
            // Processorからの任意メッセージ
            handle_processor_message(ev.message);
            break;
        // ...
        }
    }
}
```

#### 実装詳細

```cpp
// ProcessContext::send_to_control() の実装
void ProcessContext::send_to_control(const Event& ev) {
    // ロックフリーキューにpush（ISRセーフ）
    // process()はリアルタイムスレッドなのでsyscall不可
    shared_->proc_to_ctrl.queue.try_push(ev);
}

// カーネルがproc_to_ctrl.queueを監視し、
// Control Taskのイベントキューにマージする
void Kernel::merge_processor_events() {
    Event ev;
    while (g_shared.proc_to_ctrl.queue.try_pop(ev)) {
        g_shared.events.push(ev);
    }
}
```

### 同期とメモリオーダリング

#### アクセスパターン別の同期方式

| データ | Writer | Reader | 同期方式 |
|--------|--------|--------|----------|
| `hw_state` | Kernel (IRQ) | App (main) | 単一writer、コピー取得 |
| `events` | Kernel | App | SpscQueue (lock-free) |
| `params` | App (main) | App (process) | atomic relaxed |
| `audio` | App (process) | Kernel (DMA) | ダブルバッファ |
| `display` | App (main) | Kernel (DMA) | dirty flag + flush |
| `proc_to_ctrl` | App (process) | Kernel | SpscQueue (lock-free) |

#### パラメータのアトミック性

```cpp
// Control Task (main) で設定
umi::set_param(PARAM_CUTOFF, new_cutoff);

// Processor (process) で読み取り
void MyProcessor::process(umi::ProcessContext& ctx) {
    // relaxed で十分：順序保証不要、最新値が見えればOK
    float cutoff = ctx.param(PARAM_CUTOFF);
    filter_.set_cutoff(cutoff);
}

// 内部実装
float ProcessContext::param(uint32_t id) const {
    return shared_->params.values[id].load(std::memory_order_relaxed);
}
```

#### オーディオバッファのダブルバッファリング

```cpp
// カーネルがダブルバッファを管理
// DMAがバッファAを転送中 → アプリはバッファBに書き込み

struct AudioDoubleBuffer {
    float buf[2][2][BUFFER_SIZE];  // [ping/pong][ch][samples]
    std::atomic<int> app_index{0};  // アプリが書き込むバッファ
    std::atomic<int> dma_index{1};  // DMAが転送するバッファ
    
    float* app_output(int ch) {
        return buf[app_index.load()][ch];
    }
    
    void swap() {
        int old = app_index.exchange(dma_index.load());
        dma_index.store(old);
    }
};
```

### Web/Desktop プラットフォームでの実装

組込みと同じAPIを提供するための各プラットフォーム実装。

#### Web (WASM + Asyncify)

```cpp
// wasm/shared_memory_web.cc

// 共有メモリはWebAssembly.Memoryの一部
static SharedRegion* g_shared = reinterpret_cast<SharedRegion*>(0x10000);

// JavaScriptからハードウェア状態を更新
extern "C" void umi_update_hw_state(int type, int id, int value) {
    switch (type) {
    case HW_ADC:
        g_shared->hw_state.adc[id] = value;
        push_event({EventType::AdcChange, {.adc = {id, value / 4095.0f}}});
        break;
    case HW_ENCODER:
        g_shared->hw_state.encoders[id] += value;
        push_event({EventType::EncoderRotate, {.encoder = {id, value}}});
        break;
    case HW_BUTTON:
        // ...
    }
}

// JavaScript側
class HardwareEmulator {
    constructor(wasmInstance) {
        this.update = wasmInstance.exports.umi_update_hw_state;
    }
    
    onKnobChange(id, value) {
        this.update(0 /*ADC*/, id, Math.floor(value * 4095));
    }
    
    onEncoderRotate(id, delta) {
        this.update(1 /*ENCODER*/, id, delta);
    }
}
```

#### Desktop (PortAudio + std::thread)

```cpp
// desktop/shared_memory_desktop.cc

// 共有メモリは通常のヒープ
static SharedRegion g_shared_storage;
static SharedRegion* g_shared = &g_shared_storage;

// MIDI入力スレッドがhw_stateを更新
void midi_input_thread() {
    while (running) {
        auto msg = midi_in.receive();
        if (msg.is_cc()) {
            // MIDIコントローラ値 → 仮想ADC
            g_shared->hw_state.adc[msg.cc_number()] = msg.cc_value() * 32;
            Event ev{EventType::AdcChange, {.adc = {msg.cc_number(), msg.cc_value() / 127.0f}}};
            g_shared->events.push(ev);
            cv.notify_one();  // wait_event()を起こす
        }
    }
}
```

---

## ハードウェア入力

### 入力デバイスの種類

| デバイス | データ型 | 更新方式 | 典型的な用途 |
|----------|----------|----------|--------------|
| ADC (ポット) | 0-4095 | ポーリング/閾値 | ノブ、スライダー |
| エンコーダ | 相対値 (delta) | IRQ | 無限回転ノブ |
| ボタン | ON/OFF | IRQ | スイッチ、タクト |
| タッチパッド | x, y, pressure | ポーリング | XY pad |
| ジャイロ/加速度 | 3軸 float | ポーリング | モーションコントロール |
| 距離センサ | mm | ポーリング | 非接触コントロール |

### 入力取得方法

#### 方法1: イベント駆動（推奨）

カーネルがハードウェア変化を検出してイベント通知。
**低CPU負荷、応答性良好。**

```cpp
int main() {
    static Synth synth;
    umi::register_processor(synth);
    
    while (true) {
        auto ev = umi::wait_event();
        
        switch (ev.type) {
        case umi::EventType::Shutdown:
            return 0;
            
        // エンコーダ（相対値）
        case umi::EventType::EncoderRotate:
            // id: エンコーダ番号, delta: 回転量（正=時計回り）
            adjust_param(ev.encoder.id, ev.encoder.delta);
            break;
            
        // ボタン
        case umi::EventType::ButtonPress:
            handle_button_down(ev.button.id);
            break;
        case umi::EventType::ButtonRelease:
            handle_button_up(ev.button.id);
            break;
            
        // ADC（閾値を超えて変化した場合のみ通知）
        case umi::EventType::AdcChange:
            // channel: ADCチャンネル, value: 0.0-1.0 正規化値
            set_param_from_knob(ev.adc.channel, ev.adc.value);
            break;
            
        // タッチ
        case umi::EventType::TouchBegin:
        case umi::EventType::TouchMove:
        case umi::EventType::TouchEnd:
            handle_touch(ev.touch.x, ev.touch.y, ev.touch.pressure);
            break;
        }
    }
}
```

#### 方法2: ポーリング（高精度用）

連続的な値の変化を高精度で追跡する場合。
**CPU負荷高め、レイテンシ制御可能。**

```cpp
int main() {
    static Synth synth;
    umi::register_processor(synth);
    
    // 前回値を保持
    std::array<uint16_t, 8> prev_adc{};
    
    while (true) {
        // タイムアウト付きイベント待機（1ms）
        auto ev = umi::wait_event(1000);  // 1000μs = 1ms
        
        if (ev.type != umi::EventType::Timeout) {
            // イベント処理
            handle_event(ev);
        }
        
        // 1msごとにADCをポーリング
        auto hw = umi::get_hw_state();
        for (int i = 0; i < 8; ++i) {
            int16_t diff = hw.adc[i] - prev_adc[i];
            if (std::abs(diff) > 8) {  // ノイズ除去
                float normalized = hw.adc[i] / 4095.0f;
                apply_adc_value(i, normalized);
                prev_adc[i] = hw.adc[i];
            }
        }
    }
}
```

#### 方法3: コルーチンで分離（推奨）

イベント処理とポーリングを別タスクに分離。

```cpp
// イベント処理タスク
umi::Task<void> event_task(Synth& synth) {
    while (true) {
        auto ev = co_await umi::wait_event_async();
        if (ev.type == umi::EventType::Shutdown) co_return;
        synth.handle_event(ev);
    }
}

// 高速ADCポーリングタスク（モジュレーション用）
umi::Task<void> modulation_task(Synth& synth) {
    while (true) {
        co_await umi::sleep(500us);  // 2kHz
        auto hw = umi::get_hw_state();
        synth.apply_modwheel(hw.adc[MOD_WHEEL_CH] / 4095.0f);
    }
}

// ディスプレイ更新タスク
umi::Task<void> display_task(Synth& synth) {
    while (true) {
        co_await umi::sleep(33ms);  // 30fps
        update_display(synth);
    }
}

int main() {
    static Synth synth;
    umi::register_processor(synth);
    
    umi::Scheduler<4> sched;
    sched.spawn(event_task(synth));
    sched.spawn(modulation_task(synth));
    sched.spawn(display_task(synth));
    sched.run();
    
    return 0;
}
```

### ADC値の処理

```cpp
// 正規化（0.0-1.0）
float normalize_adc(uint16_t raw) {
    return raw / 4095.0f;
}

// 対数スケール（周波数パラメータ用）
float adc_to_freq(uint16_t raw, float min_hz, float max_hz) {
    float norm = raw / 4095.0f;
    return min_hz * std::pow(max_hz / min_hz, norm);
}

// デッドゾーン付き（ピッチベンド用）
float adc_with_deadzone(uint16_t raw, float deadzone = 0.02f) {
    float norm = (raw / 4095.0f) * 2.0f - 1.0f;  // -1.0 to 1.0
    if (std::abs(norm) < deadzone) return 0.0f;
    return norm;
}

// ヒステリシス（チャタリング防止）
class AdcWithHysteresis {
    uint16_t value_ = 0;
    uint16_t threshold_ = 16;
public:
    bool update(uint16_t raw) {
        if (std::abs(raw - value_) > threshold_) {
            value_ = raw;
            return true;  // 変化あり
        }
        return false;
    }
    uint16_t value() const { return value_; }
};
```

---

## ハードウェア出力

### 出力デバイスの種類

| デバイス | インターフェース | 更新方式 | API |
|----------|------------------|----------|-----|
| 単色LED | GPIO | 即時 | `set_led()` |
| RGB LED | PWM / WS2812 | 即時 | `set_rgb()` |
| 7セグメント | GPIO/SPI | 即時 | `set_7seg()` |
| OLED (SSD1306等) | I2C/SPI | バッファ | `draw_*()` + `flush()` |
| LCD (ILI9341等) | SPI/Parallel | バッファ | `draw_*()` + `flush()` |
| モーター/サーボ | PWM | 即時 | `set_pwm()` |

### LED制御

```cpp
// 単色LED
umi::set_led(LED_POWER, true);    // ON
umi::set_led(LED_MIDI, false);    // OFF
umi::toggle_led(LED_STATUS);      // トグル

// LED点滅（コルーチン）
umi::Task<void> blink_task() {
    while (true) {
        umi::toggle_led(LED_STATUS);
        co_await umi::sleep(500ms);
    }
}
```

### RGB LED

```cpp
// 単一RGB LED
umi::set_rgb(LED_MAIN, 255, 0, 0);     // 赤
umi::set_rgb(LED_MAIN, 0, 255, 0);     // 緑
umi::set_rgb(LED_MAIN, 0, 0, 255);     // 青

// HSV指定
umi::set_hsv(LED_MAIN, 120, 255, 128); // 緑、彩度MAX、明るさ50%

// WS2812ストリップ（複数LED）
umi::RgbStrip strip(NUM_LEDS);
strip.set(0, {255, 0, 0});
strip.set(1, {0, 255, 0});
strip.fill({0, 0, 255});  // 全LED同色
strip.show();             // DMA転送開始

// エフェクト
umi::Task<void> rainbow_task(umi::RgbStrip& strip) {
    uint8_t hue = 0;
    while (true) {
        for (int i = 0; i < strip.size(); ++i) {
            strip.set_hsv(i, (hue + i * 10) % 256, 255, 128);
        }
        strip.show();
        hue += 2;
        co_await umi::sleep(20ms);
    }
}
```

### 7セグメント/数値表示

```cpp
// 数値表示
umi::display_number(channel, 440);     // "440"
umi::display_number(channel, 3.14f);   // "3.14"
umi::display_number(channel, -12);     // "-12"

// 文字表示（限定的）
umi::display_text(channel, "LOAD");    // "LoAd"

// フォーマット指定
umi::display_number(channel, 127, {
    .digits = 3,
    .leading_zeros = true,   // "127"
    .decimal_point = 0,      // なし
});
```

### OLED/LCDディスプレイ

```cpp
// ディスプレイコンテキスト取得
auto& disp = umi::get_display();

// 基本描画（バッファに描画）
disp.clear();
disp.fill(umi::Color::Black);

// テキスト
disp.set_font(umi::Font::Small);  // 6x8
disp.draw_text(0, 0, "UMI Synth");
disp.set_font(umi::Font::Large);  // 12x16
disp.draw_text(0, 16, "440 Hz");

// 図形
disp.draw_rect(10, 10, 50, 30, umi::Color::White);
disp.fill_rect(12, 12, 46, 26, umi::Color::Gray);
disp.draw_line(0, 0, 127, 63, umi::Color::White);
disp.draw_circle(64, 32, 20, umi::Color::White);

// ビットマップ
disp.draw_bitmap(0, 0, logo_data, 32, 32);

// バッファをハードウェアに転送
disp.flush();  // 非同期DMA転送
```

### LCDグラフィックス（カラー）

```cpp
auto& lcd = umi::get_lcd();  // RGB565

// 色指定
constexpr auto RED   = umi::rgb565(255, 0, 0);
constexpr auto GREEN = umi::rgb565(0, 255, 0);
constexpr auto BLUE  = umi::rgb565(0, 0, 255);

// 描画
lcd.fill(umi::Color::Black);
lcd.fill_rect(10, 10, 100, 50, RED);
lcd.draw_text(20, 20, "Hello", umi::Font::Large, GREEN);

// 波形表示
void draw_waveform(std::span<const float> samples) {
    auto& lcd = umi::get_lcd();
    int w = lcd.width();
    int h = lcd.height();
    int mid = h / 2;
    
    lcd.fill_rect(0, 0, w, h, umi::Color::Black);
    
    for (int x = 0; x < w && x < samples.size(); ++x) {
        int y = mid - static_cast<int>(samples[x] * mid);
        lcd.draw_pixel(x, y, GREEN);
    }
    lcd.flush();
}

// 部分更新（高速化）
lcd.set_window(0, 0, 100, 50);  // 更新領域を限定
lcd.fill(RED);
lcd.flush_window();
```

### ディスプレイ更新の最適化

```cpp
// ダブルバッファリング
auto& disp = umi::get_display();
disp.enable_double_buffer(true);

// フレームレート制御
umi::Task<void> display_task(Synth& synth) {
    while (true) {
        // 30fps
        co_await umi::sleep(33ms);
        
        auto& disp = umi::get_display();
        disp.clear();
        draw_ui(synth, disp);
        disp.swap();  // バッファ切り替え + DMA転送
    }
}

// ダーティ領域のみ更新
disp.mark_dirty(0, 0, 64, 16);   // 変更領域をマーク
disp.flush_dirty();              // マーク領域のみ転送
```

### PWM出力

```cpp
// 汎用PWM（0.0-1.0）
umi::set_pwm(PWM_CH0, 0.5f);  // 50%デューティ

// サーボモーター（角度指定）
umi::set_servo(SERVO_CH0, 90.0f);  // 90度

// モーター速度（-1.0 to 1.0）
umi::set_motor(MOTOR_CH0, 0.75f);   // 正転75%
umi::set_motor(MOTOR_CH0, -0.5f);   // 逆転50%
```

---

## ハードウェア出力の実装詳細

アプリのAPI呼び出しがどのように共有メモリを経由してハードウェアに反映されるかを解説します。

### 出力方式の分類

| 方式 | 特徴 | 用途 | 例 |
|------|------|------|-----|
| **即時syscall** | 即座にHW反映 | 低レイテンシ | LED, GPIO |
| **共有メモリ + flush** | バッファリング | 大量データ | Display |
| **共有メモリ + 定期転送** | カーネルがポーリング | 連続更新 | RGB Strip |

### 即時syscall方式（LED, GPIO）

`set_led()` はsyscallを発行し、カーネルが即座にGPIOを操作します。

```cpp
// ===== アプリ側 (lib/umi/hw_output.cc) =====

void umi::set_led(uint8_t id, bool on) {
    // syscall #20: SET_LED
    syscall(SYS_SET_LED, id, on ? 1 : 0);
}

void umi::toggle_led(uint8_t id) {
    syscall(SYS_TOGGLE_LED, id);
}

void umi::set_gpio(uint8_t pin, bool value) {
    syscall(SYS_SET_GPIO, pin, value ? 1 : 0);
}
```

```cpp
// ===== カーネル側 (lib/umios/kernel/syscall_handler.cc) =====

uint32_t handle_syscall(uint32_t num, uint32_t r0, uint32_t r1, uint32_t r2) {
    switch (num) {
    case SYS_SET_LED: {
        uint8_t id = r0;
        bool on = r1 != 0;
        
        // BSPのLED設定を取得
        auto& led = bsp::get_led(id);
        if (on) {
            led.port->BSRR = led.pin;      // Set
        } else {
            led.port->BSRR = led.pin << 16; // Reset
        }
        return 0;
    }
    
    case SYS_TOGGLE_LED: {
        uint8_t id = r0;
        auto& led = bsp::get_led(id);
        led.port->ODR ^= led.pin;
        return 0;
    }
    // ...
    }
}
```

```cpp
// ===== BSP側 (lib/bsp/my_board/leds.cc) =====

namespace bsp {

struct LedConfig {
    GPIO_TypeDef* port;
    uint16_t pin;
};

// ボード固有のLED配置
static constexpr LedConfig leds[] = {
    { GPIOA, GPIO_PIN_5 },   // LED0: PA5
    { GPIOB, GPIO_PIN_0 },   // LED1: PB0
    { GPIOB, GPIO_PIN_1 },   // LED2: PB1
};

const LedConfig& get_led(uint8_t id) {
    return leds[id % std::size(leds)];
}

}
```

### 呼び出しフロー（set_led）

```
Application                 Kernel                      Hardware
    │                          │                           │
    │  umi::set_led(0, true)   │                           │
    │─────────────────────────>│                           │
    │  SVC #0 (syscall)        │                           │
    │         ┌────────────────┤                           │
    │         │ SVC_Handler    │                           │
    │         │ switch(SYS_SET_LED)                        │
    │         │ bsp::get_led(0)│                           │
    │         │ GPIOA->BSRR = PIN_5                        │
    │         │                │──────────────────────────>│
    │         │                │  GPIO PA5 = HIGH          │
    │         └────────────────┤                           │
    │<─────────────────────────│                           │
    │  return                  │                           │
```

### RGB LED（PWM方式）

```cpp
// ===== アプリ側 =====

void umi::set_rgb(uint8_t id, uint8_t r, uint8_t g, uint8_t b) {
    // 3つのPWMチャンネルを設定
    syscall(SYS_SET_RGB, id, (r << 16) | (g << 8) | b);
}

void umi::set_hsv(uint8_t id, uint8_t h, uint8_t s, uint8_t v) {
    auto [r, g, b] = hsv_to_rgb(h, s, v);
    set_rgb(id, r, g, b);
}
```

```cpp
// ===== カーネル側 =====

case SYS_SET_RGB: {
    uint8_t id = r0;
    uint8_t r = (r1 >> 16) & 0xFF;
    uint8_t g = (r1 >> 8) & 0xFF;
    uint8_t b = r1 & 0xFF;
    
    auto& rgb = bsp::get_rgb_led(id);
    rgb.tim->CCR1 = r;  // Red PWM
    rgb.tim->CCR2 = g;  // Green PWM
    rgb.tim->CCR3 = b;  // Blue PWM
    return 0;
}
```

### RGB Strip（WS2812 - 共有メモリ + DMA）

WS2812はタイミングがシビアなのでDMA転送を使用。

```cpp
// ===== 共有メモリ構造 =====

struct SharedRegion {
    // ...
    struct {
        uint8_t data[MAX_LEDS * 3];   // GRB順
        uint16_t num_leds;
        std::atomic<bool> dirty;
    } rgb_strip;
};
```

```cpp
// ===== アプリ側 =====

class RgbStrip {
    uint8_t* data_;      // → shared.rgb_strip.data
    uint16_t num_leds_;
    
public:
    void set(int index, Color c) {
        // 共有メモリに直接書き込み（GRB順）
        data_[index * 3 + 0] = c.g;
        data_[index * 3 + 1] = c.r;
        data_[index * 3 + 2] = c.b;
    }
    
    void show() {
        // dirtyフラグを立てて転送を依頼
        g_shared->rgb_strip.dirty.store(true, std::memory_order_release);
        syscall(SYS_RGB_STRIP_SHOW);
    }
};
```

```cpp
// ===== カーネル側 =====

case SYS_RGB_STRIP_SHOW: {
    if (g_shared.rgb_strip.dirty.load(std::memory_order_acquire)) {
        // GRBデータ → PWMタイミングデータに変換
        convert_to_ws2812_timing(
            g_shared.rgb_strip.data,
            g_shared.rgb_strip.num_leds,
            dma_buffer
        );
        
        // DMA転送開始
        start_ws2812_dma(dma_buffer, dma_buffer_size);
        
        g_shared.rgb_strip.dirty.store(false);
    }
    return 0;
}

// WS2812タイミング: 0=T0H(0.4us)+T0L(0.85us), 1=T1H(0.8us)+T1L(0.45us)
void convert_to_ws2812_timing(const uint8_t* grb, uint16_t num_leds, uint16_t* out) {
    for (int i = 0; i < num_leds * 3; ++i) {
        uint8_t byte = grb[i];
        for (int bit = 7; bit >= 0; --bit) {
            *out++ = (byte & (1 << bit)) ? PWM_ONE : PWM_ZERO;
        }
    }
}
```

### ディスプレイ（共有メモリ + flush）

ディスプレイはフレームバッファを共有メモリに持ち、flush()でDMA転送。

```cpp
// ===== 共有メモリ構造 =====

struct SharedRegion {
    // ...
    struct {
        uint8_t buffer[2][128 * 64 / 8];  // ダブルバッファ
        std::atomic<int> front;            // 表示中バッファ
        std::atomic<int> back;             // 描画中バッファ
        std::atomic<uint32_t> dirty_rect;  // 更新領域
    } oled;
};
```

```cpp
// ===== アプリ側 =====

class OledDisplay {
    uint8_t* buffer_;  // → shared.oled.buffer[back]
    
public:
    void draw_pixel(int x, int y, bool on) {
        int byte_idx = x + (y / 8) * 128;
        int bit = y % 8;
        if (on) {
            buffer_[byte_idx] |= (1 << bit);
        } else {
            buffer_[byte_idx] &= ~(1 << bit);
        }
    }
    
    void draw_text(int x, int y, const char* text) {
        // フォントを使って描画...
    }
    
    void flush() {
        // 全画面dirty
        g_shared->oled.dirty_rect.store(0xFFFFFFFF);
        syscall(SYS_DISPLAY_FLUSH, DISPLAY_OLED);
    }
    
    void swap() {
        // バッファ切り替え + 転送
        int old_back = g_shared->oled.back.load();
        g_shared->oled.back.store(g_shared->oled.front.load());
        g_shared->oled.front.store(old_back);
        syscall(SYS_DISPLAY_FLUSH, DISPLAY_OLED);
    }
};
```

```cpp
// ===== カーネル側 =====

case SYS_DISPLAY_FLUSH: {
    DisplayType type = static_cast<DisplayType>(r0);
    
    switch (type) {
    case DISPLAY_OLED: {
        int front = g_shared.oled.front.load();
        uint8_t* data = g_shared.oled.buffer[front];
        
        // I2C/SPI DMA転送
        oled_driver.send_buffer_async(data, 128 * 64 / 8);
        break;
    }
    case DISPLAY_LCD: {
        // SPI DMA転送（RGB565）
        lcd_driver.send_buffer_async(
            g_shared.lcd.buffer,
            320 * 240 * 2
        );
        break;
    }
    }
    return 0;
}
```

### 呼び出しフロー（ディスプレイ）

```
Application                 Shared Memory              Kernel                 Hardware
    │                           │                         │                       │
    │  disp.draw_text(...)      │                         │                       │
    │────────────────────────>  │                         │                       │
    │  buffer[back]に書き込み   │                         │                       │
    │                           │                         │                       │
    │  disp.flush()             │                         │                       │
    │  dirty_rect = 0xFFFF      │                         │                       │
    │────────────────────────>  │                         │                       │
    │  syscall(DISPLAY_FLUSH)   │                         │                       │
    │───────────────────────────────────────────────────> │                       │
    │                           │  buffer[front]を読み取り│                       │
    │                           │<────────────────────────│                       │
    │                           │                         │ SPI DMA転送開始       │
    │                           │                         │──────────────────────>│
    │<──────────────────────────────────────────────────  │                       │
    │  return（DMA完了を待たない）                        │       非同期転送中    │
    │                           │                         │                       │
    │  次の描画開始             │                         │                       │
    │  buffer[back]に書き込み   │                         │                       │
    │────────────────────────>  │                         │                       │
```

### 定期転送方式（オプション）

syscallを呼ばずに、カーネルが定期的に共有メモリをチェックして転送する方式。

```cpp
// カーネルの定期タスク（1ms周期）
void Kernel::periodic_hw_update() {
    // RGB Stripのdirtyチェック
    if (g_shared.rgb_strip.dirty.load(std::memory_order_acquire)) {
        // DMA転送開始（前回の転送完了を待つ）
        if (!ws2812_dma_busy()) {
            start_ws2812_dma(...);
            g_shared.rgb_strip.dirty.store(false);
        }
    }
    
    // OLEDのdirtyチェック（30fps制限）
    if (g_shared.oled.dirty_rect.load() && oled_refresh_due()) {
        start_oled_dma(...);
        g_shared.oled.dirty_rect.store(0);
    }
}
```

### プラットフォーム別実装

```cpp
// ===== Web (WASM) =====

// set_led → JavaScriptコールバック
void umi::set_led(uint8_t id, bool on) {
    // WASMからJSを呼び出し
    EM_ASM({
        window.umiHardware.setLed($0, $1);
    }, id, on);
}

// JavaScript側
class UmiHardwareEmulator {
    setLed(id, on) {
        const led = document.querySelector(`#led-${id}`);
        led.classList.toggle('on', on);
    }
    
    setRgb(id, r, g, b) {
        const led = document.querySelector(`#rgb-led-${id}`);
        led.style.backgroundColor = `rgb(${r}, ${g}, ${b})`;
    }
}
```

```cpp
// ===== Desktop =====

// set_led → 何もしない（またはログ）
void umi::set_led(uint8_t id, bool on) {
    #ifdef DEBUG_HW
    printf("LED[%d] = %s\n", id, on ? "ON" : "OFF");
    #endif
}

// ディスプレイ → Windowに描画
void OledDisplay::flush() {
    // SDL/OpenGLウィンドウにテクスチャとして転送
    desktop_window.update_texture(buffer_, 128, 64);
}
```

### 実装選択ガイド

| 出力タイプ | 推奨方式 | 理由 |
|------------|----------|------|
| LED ON/OFF | 即時syscall | 低レイテンシが必要 |
| RGB LED単体 | 即時syscall (PWM) | 簡単、即座に反映 |
| RGB Strip | 共有メモリ + syscall | DMA転送が必要 |
| 7セグ | 即時syscall | 即座に反映 |
| OLED/LCD | 共有メモリ + flush | フレームバッファが必要 |
| PWM/サーボ | 即時syscall | 即座に反映 |

### process() 内でのハードウェアアクセス

**禁止**: process() 内でsyscallは使用できません。

代わりにProcessContextに渡されるデータを使用:

```cpp
void MyProcessor::process(umi::ProcessContext& ctx) {
    // ✅ OK: ctx経由でオーディオ/MIDIにアクセス
    auto* out = ctx.output(0);
    for (const auto& ev : ctx.events()) { ... }
    
    // ❌ NG: syscallは使えない
    // auto hw = umi::get_hw_state();  // 禁止！
    // umi::set_led(0, true);          // 禁止！
    
    // ✅ OK: パラメータはアトミックに読み取り可
    float cutoff = ctx.param(PARAM_CUTOFF);
    
    // ✅ OK: Control Taskにイベント送信（メーター等）
    if (frame_count % 1024 == 0) {
        ctx.send_to_control(umi::Event::meter(0, peak_level));
    }
}
```

### プラットフォーム対応

| API | 組込み | Web (WASM) | Desktop |
|-----|--------|------------|---------|
| `wait_event()` | syscall | Asyncify | std::thread |
| `get_hw_state()` | 共有メモリ | JS経由 | MIDI/OSC |
| `set_led()` | GPIO | CSS/Canvas | なし |
| `set_rgb()` | PWM/WS2812 | CSS | なし |
| `get_display()` | SPI/I2C | Canvas | Window |
| `ctx.output()` | DMAバッファ | AudioWorklet | PortAudio |

---

## 関連ドキュメント

- [ARCHITECTURE.md](ARCHITECTURE.md) - アーキテクチャ概要
- [UMIP_SPEC.md](UMIP_SPEC.md) - Processor仕様
- [UMIC_SPEC.md](UMIC_SPEC.md) - Controller仕様
- [UMIM_SPEC.md](UMIM_SPEC.md) - バイナリ形式
- [SECURITY_ANALYSIS.md](SECURITY_ANALYSIS.md) - セキュリティとメモリ保護

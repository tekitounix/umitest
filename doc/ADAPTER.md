# ホストアダプター設計

MCUで動くコードをVST/AU/WASMでも動かすための仕組み。

---

## 概要

```
┌─────────────────────────────────────────────────────────┐
│              AudioProcessor（ユーザーコード）             │
│                     100% ポータブル                      │
├─────────────────────────────────────────────────────────┤
│                   Host Adapter Layer                     │
│                                                         │
│  各ホストの違いを吸収し、同一インターフェースを提供        │
│                                                         │
│  ┌─────────────────────────────────────────────────┐   │
│  │              Adapter Interface                   │   │
│  │  - process() の呼び出し                          │   │
│  │  - MIDI イベントの変換と配信                      │   │
│  │  - パラメータの同期                              │   │
│  │  - 状態の保存/復元                               │   │
│  └─────────────────────────────────────────────────┘   │
│                                                         │
│  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────┐      │
│  │   MCU   │ │  VST3   │ │   AU    │ │  WASM   │      │
│  │ Adapter │ │ Adapter │ │ Adapter │ │ Adapter │      │
│  └─────────┘ └─────────┘ └─────────┘ └─────────┘      │
└─────────────────────────────────────────────────────────┘
```

---

## 各ホストの違い

| 項目 | MCU (UMI-OS) | VST3 | AU | WASM |
|------|-------------|------|-----|------|
| オーディオコール | ISR→Task notify | processBlock() | render() | JS callback |
| MIDI形式 | umi::midi::Event | Steinberg::Vst::Event | MIDIPacket | Uint8Array |
| パラメータ | 直接アクセス | IEditController | AudioUnitParameter | JS object |
| サンプルレート | 固定(48kHz等) | 可変 | 可変 | 可変 |
| バッファサイズ | 固定(64-256) | 可変 | 可変 | 固定(128等) |
| スレッド | RTOSタスク | ホスト管理 | ホスト管理 | シングル |

---

## Adapter Interface

各アダプターが実装すべきインターフェース。

```cpp
namespace umi::adapter {

template<typename Processor>
struct AdapterBase {
    Processor processor;
    
    // === オーディオ処理 ===
    // ホストから呼ばれる → processor.process() を呼ぶ
    void process_audio(float** out, const float** in,
                       std::size_t frames, std::size_t channels);
    
    // === MIDI ===
    // ホスト形式 → umi::midi::Event に変換 → processor.on_midi()
    void handle_midi(/* ホスト固有の形式 */);
    
    // === パラメータ ===
    void set_param(std::size_t index, float value);
    float get_param(std::size_t index) const;
    
    // === 状態 ===
    std::size_t save_state(std::byte* buf, std::size_t max) const;
    void load_state(const std::byte* buf, std::size_t len);
    
    // === ライフサイクル ===
    void prepare(float sample_rate, std::size_t max_frames);
    void reset();
};

}
```

---

## MCU Adapter (UMI-OS)

カーネルがタスクを管理し、`processor` のメソッドを呼び出す。

```cpp
// adapter/mcu/run.hh
namespace umi::mcu {

template<typename Processor>
[[noreturn]] void run(Processor& proc) {
    // カーネル初期化
    Kernel<HW, MaxTasks> kernel;
    
    // オーディオバッファ（静的確保）
    alignas(4) static float out_buf[2][256];
    alignas(4) static float in_buf[2][256];
    
    // MIDI キュー
    static SpscQueue<midi::Event, 64> midi_queue;
    
    // オーディオタスク（Realtime）
    kernel.create_task({
        .entry = [](void* ctx) {
            auto& p = *static_cast<Processor*>(ctx);
            float* out[2] = {out_buf[0], out_buf[1]};
            const float* in[2] = {in_buf[0], in_buf[1]};
            
            while (true) {
                kernel.wait(Event::AudioReady);
                
                // MIDIイベント処理
                while (auto ev = midi_queue.try_pop()) {
                    p.on_midi(*ev);
                }
                
                // オーディオ処理
                auto start = DWT::cycles();
                p.process(out, in, 256, 2);
                load_monitor.record(DWT::cycles() - start);
            }
        },
        .arg = &proc,
        .prio = Priority::Realtime,
        .name = "audio",
    });
    
    // MIDI Server（High）
    kernel.create_task({
        .entry = midi_server_task,
        .prio = Priority::High,
        .name = "midi",
    });
    
    // DMA ISR（notify のみ）
    HW::set_audio_callback([] {
        kernel.notify_from_isr(audio_task, Event::AudioReady);
    });
    
    kernel.start();  // 戻らない
}

}
```

**使用方法:**
```cpp
#include "my_synth.hh"
#include <umi/mcu/run.hh>

int main() {
    MySynth synth;
    umi::mcu::run(synth);
}
```

---

## VST3 Adapter

Steinberg VST3 SDK とのブリッジ。

```cpp
// adapter/vst3/processor.hh
namespace umi::vst3 {

template<typename Processor>
class Vst3Processor : public Steinberg::Vst::AudioEffect {
    Processor proc_;
    
public:
    // VST3 からのオーディオ処理コール
    tresult process(ProcessData& data) override {
        // MIDI イベント変換
        if (data.inputEvents) {
            for (int32 i = 0; i < data.inputEvents->getEventCount(); ++i) {
                Event vstEvent;
                data.inputEvents->getEvent(i, vstEvent);
                
                // VST3 Event → umi::midi::Event
                if (vstEvent.type == Event::kNoteOnEvent) {
                    proc_.on_midi(midi::Event{
                        .type = midi::Type::NoteOn,
                        .channel = static_cast<uint8_t>(vstEvent.noteOn.channel),
                        .data1 = static_cast<uint8_t>(vstEvent.noteOn.pitch),
                        .data2 = static_cast<uint8_t>(vstEvent.noteOn.velocity * 127),
                    });
                }
                // ... 他のイベント
            }
        }
        
        // オーディオ処理
        float* out[2] = {data.outputs[0].channelBuffers32[0],
                         data.outputs[0].channelBuffers32[1]};
        const float* in[2] = {data.inputs[0].channelBuffers32[0],
                              data.inputs[0].channelBuffers32[1]};
        
        proc_.process(out, in, data.numSamples, 2);
        
        return kResultOk;
    }
    
    // パラメータ
    tresult setParamNormalized(ParamID id, ParamValue value) override {
        auto& info = Processor::params[id];
        float denorm = info.min + value * (info.max - info.min);
        proc_.set_param(id, denorm);
        return kResultOk;
    }
    
    // 状態保存
    tresult getState(IBStream* state) override {
        std::array<std::byte, 4096> buf;
        auto size = proc_.save_state(buf.data(), buf.size());
        state->write(buf.data(), size, nullptr);
        return kResultOk;
    }
};

// ファクトリ生成マクロ不要 - テンプレートで生成
template<typename Processor>
auto create() {
    return new Vst3Processor<Processor>();
}

}
```

**使用方法:**
```cpp
#include "my_synth.hh"
#include <umi/vst3/processor.hh>

// VST3 エントリーポイント
IPluginFactory* GetPluginFactory() {
    return umi::vst3::create_factory<MySynth>("MySynth", "1.0.0");
}
```

---

## AU Adapter

Apple Audio Unit とのブリッジ。

```cpp
// adapter/au/processor.hh
namespace umi::au {

template<typename Processor>
class AUProcessor : public AUAudioUnit {
    Processor proc_;
    
public:
    - (AUInternalRenderBlock)internalRenderBlock {
        return ^AUAudioUnitStatus(
            AudioUnitRenderActionFlags* flags,
            const AudioTimeStamp* timestamp,
            AUAudioFrameCount frameCount,
            NSInteger outputBusNumber,
            AudioBufferList* outputData,
            const AURenderEvent* realtimeEvents,
            AURenderPullInputBlock pullInput
        ) {
            // MIDI イベント処理
            for (auto event = realtimeEvents; event; event = event->head.next) {
                if (event->head.eventType == AURenderEventMIDI) {
                    proc_.on_midi(convert_midi(event->MIDI));
                }
            }
            
            // オーディオ処理
            float* out[2] = {(float*)outputData->mBuffers[0].mData,
                             (float*)outputData->mBuffers[1].mData};
            
            proc_.process(out, nullptr, frameCount, 2);
            
            return noErr;
        };
    }
};

}
```

---

## WASM Adapter

WebAssembly + JavaScript とのブリッジ。

```cpp
// adapter/wasm/processor.hh
namespace umi::wasm {

template<typename Processor>
class WasmProcessor {
    Processor proc_;
    
public:
    // JS から呼ばれる
    void process(uintptr_t out_ptr, uintptr_t in_ptr, 
                 std::size_t frames, std::size_t channels) {
        auto* out = reinterpret_cast<float**>(out_ptr);
        auto* in = reinterpret_cast<const float**>(in_ptr);
        proc_.process(out, in, frames, channels);
    }
    
    void on_midi(uint8_t status, uint8_t data1, uint8_t data2) {
        proc_.on_midi(midi::Event{
            .type = static_cast<midi::Type>(status & 0xF0),
            .channel = static_cast<uint8_t>(status & 0x0F),
            .data1 = data1,
            .data2 = data2,
        });
    }
    
    void set_param(std::size_t index, float value) {
        proc_.set_param(index, value);
    }
};

// Emscripten バインディング
template<typename Processor>
void expose(const char* name) {
    emscripten::class_<WasmProcessor<Processor>>(name)
        .constructor()
        .function("process", &WasmProcessor<Processor>::process)
        .function("onMidi", &WasmProcessor<Processor>::on_midi)
        .function("setParam", &WasmProcessor<Processor>::set_param);
}

}
```

**JavaScript 側:**
```javascript
// AudioWorklet 内
class MySynthProcessor extends AudioWorkletProcessor {
    constructor() {
        super();
        this.synth = new Module.MySynth();
        
        this.port.onmessage = (e) => {
            if (e.data.type === 'midi') {
                this.synth.onMidi(e.data.status, e.data.data1, e.data.data2);
            }
        };
    }
    
    process(inputs, outputs, parameters) {
        const outL = outputs[0][0];
        const outR = outputs[0][1];
        
        // WASM メモリに直接書き込み
        this.synth.process(outPtrs, inPtrs, 128, 2);
        
        return true;
    }
}
```

---

## 移植性を保証する制約

AudioProcessor は以下を守る必要がある:

| 制約 | 理由 |
|------|------|
| **STL 最小限** | MCU では使えないものがある |
| **動的メモリ禁止** | `process()` 内で malloc/new しない |
| **浮動小数点のみ** | double は MCU で遅い場合がある |
| **グローバル状態禁止** | 複数インスタンス対応 |
| **スレッド依存禁止** | ホストがスレッド管理 |

### 使用可能なもの

```cpp
// OK
#include <array>
#include <cstdint>
#include <cmath>        // sinf, cosf など（MCU でも使える）
#include <span>         // C++20
#include <optional>     // C++17

// NG（process() 内では）
#include <vector>       // 動的メモリ
#include <string>       // 動的メモリ
#include <thread>       // スレッド
#include <mutex>        // スレッド
```

---

## パラメータの同期

```cpp
// AudioProcessor でのパラメータ定義
struct MySynth : umi::AudioProcessor<MySynth> {
    static constexpr auto params = std::array{
        umi::ParamInfo{"cutoff", "Cutoff", 20.f, 20000.f, 1000.f},
        umi::ParamInfo{"reso", "Resonance", 0.f, 1.f, 0.5f},
    };
    
    // パラメータ値（atomic でスレッドセーフ）
    std::array<std::atomic<float>, params.size()> param_values_{
        1000.f, 0.5f  // デフォルト値
    };
    
    void set_param(std::size_t i, float v) {
        param_values_[i].store(v, std::memory_order_relaxed);
    }
    
    float get_param(std::size_t i) const {
        return param_values_[i].load(std::memory_order_relaxed);
    }
    
    void process(...) {
        float cutoff = get_param(0);
        float reso = get_param(1);
        // ...
    }
};
```

各アダプターがホスト側のパラメータ変更を `set_param()` に変換。

---

## ファイル構成

```
adapter/
├── common/
│   └── adapter_base.hh    # 共通インターフェース
├── mcu/
│   ├── run.hh             # umi::mcu::run()
│   └── hw_bridge.hh       # HW抽象化
├── vst3/
│   ├── processor.hh       # Vst3Processor
│   ├── controller.hh      # Vst3Controller (UI)
│   └── factory.hh         # プラグイン登録
├── au/
│   ├── processor.hh       # AUProcessor
│   └── view.hh            # AU View (UI)
└── wasm/
    ├── processor.hh       # WasmProcessor
    └── bindings.hh        # Emscripten バインディング
```

---

## UIアダプター設計

### 概要

ハードウェアUI（エンコーダ/LED/LCD）とGUI（描画）を統一的に扱う。
アプリケーションは処理済みの状態を共有メモリから読み取るだけ。

```
┌─────────────────────────────────────────────────────────────────┐
│  AudioProcessor（ユーザー）                                      │
│  process() 内で共有メモリから読み取り                             │
│  ctx.params[i] / ctx.display.line(...)                          │
├─────────────────────────────────────────────────────────────────┤
│                    共有メモリ                                    │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  params[]  │  encoder_states[]  │  display_buffer[]     │   │
│  │  (処理済み) │  (フィルタ済み)     │  (dirty flags)        │   │
│  └─────────────────────────────────────────────────────────┘   │
│                    ↑↓ Server Tasks                              │
├─────────────────────────────────────────────────────────────────┤
│  Input Server (Normal)      │  Display Server (Low)             │
│  - ノイズフィルタ            │  - dirty確認                      │
│  - デバウンス               │  - LCD更新                        │
│  - バインディング適用        │  - LED PWM                        │
├─────────────────────────────────────────────────────────────────┤
│  Raw State（DMA / ISR更新）                                      │
│  adc_raw[] │ encoder_raw[] │ button_raw[]                       │
├─────────────────────────────────────────────────────────────────┤
│  Hardware Drivers（ISR / DMA / Timer）                           │
└─────────────────────────────────────────────────────────────────┘
```

### 3つのUIモード

| モード | 入力 | 出力 | 用途 |
|--------|------|------|------|
| **Headless** | MIDI CC/SysEx | Audio + MIDI | ペダル、1Uラック |
| **HardwareUI** | エンコーダ/ボタン/ADC | LED/LCD | シンセモジュール |
| **GUI** | マウス/タッチ | 描画 | VST/AU/WASM |

---

### ユーザーが書くコード

#### Level 1: 最小限（自動バインディング）

```cpp
struct MySynth : umi::AudioProcessor<MySynth> {
    static constexpr auto params = std::array{
        umi::Param{"freq", 20, 20000, 440},
        umi::Param{"reso", 0, 1, 0.5},
    };
    
    void process(float** out, const float** in,
                 std::size_t frames, std::size_t channels,
                 umi::Context& ctx) {
        // 共有メモリから読み取り（処理済み）
        float freq = ctx.params[0];
        float reso = ctx.params[1];
        // ...
    }
    
    // hw 省略 → 自動マッピング
    // Encoder N → param N, MIDI CC N → param N
};
```

#### Level 2: カスタムマッピング

```cpp
struct MySynth : umi::AudioProcessor<MySynth> {
    static constexpr auto params = /* ... */;
    
    // カスタムバインディング
    static constexpr auto hw = umi::bindings(
        umi::encoder(0) >> umi::param<0>,
        umi::encoder(1) >> umi::param<1>,
    );
};
```

#### Level 3: ページ切り替え

```cpp
struct MySynth : umi::AudioProcessor<MySynth> {
    static constexpr auto params = /* 8個 */;
    
    // 4ノブ × 2ページ
    static constexpr auto hw = umi::pages(
        umi::page("Main", 0, 1, 2, 3),
        umi::page("Mod",  4, 5, 6, 7)
    );
};
```

#### Level 4: フルカスタム（ページ + レイヤー + メニュー混在）

```cpp
struct MySynth : umi::AudioProcessor<MySynth> {
    static constexpr auto params = /* ... */;
    
    static constexpr auto hw = umi::HardwareUI{
        // 直接バインド（常時有効）
        umi::bind(umi::encoder(0), umi::param<0>),
        
        // ページ制御（一部のノブ）
        umi::paged(umi::encoder(2), umi::encoder(3), {
            {"Env", {2, 3}},
            {"FX",  {4, 5}},
        }),
        
        // Shift レイヤー
        umi::shift(umi::button(7), {
            umi::bind(umi::encoder(0), umi::param<6>),
        }),
        
        // メニュー（設定系）
        umi::menu(umi::encoder(4), {
            {"Audio", {{"Rate", umi::sys::sample_rate}}},
        }),
    };
};
```

---

### 共有メモリ構造

```cpp
namespace umi {

struct SharedState {
    // === 入力（Server → App）===
    std::array<std::atomic<float>, MaxParams> params;
    
    // === 出力（App → Server）===
    struct Display {
        std::array<std::array<char, 32>, 4> lines;
        std::atomic<std::uint8_t> dirty{0};
        
        template<typename... Args>
        void line(int n, std::format_string<Args...> fmt, Args&&... args) {
            auto result = std::format(fmt, std::forward<Args>(args)...);
            std::copy_n(result.begin(), 
                        std::min(result.size(), lines[n].size()),
                        lines[n].begin());
            dirty.fetch_or(1 << n, std::memory_order_release);
        }
    } display;
    
    struct LEDs {
        std::array<std::atomic<std::uint8_t>, MaxLEDs> values;
        
        void set(int n, float normalized) {
            values[n].store(static_cast<std::uint8_t>(normalized * 255));
        }
    } leds;
};

}
```

---

### Input Server Task

```cpp
template<typename App>
class InputServer {
    SharedState& shared_;
    RawState& raw_;
    using Bindings = decltype(resolve_bindings<App>());
    
    // フィルタ状態
    std::array<float, MaxEncoders> encoder_accum_{};
    std::array<std::uint8_t, MaxButtons> debounce_{};
    UIState ui_state_{};  // current_page, modifier_mask
    
public:
    void run() {  // Normal優先度
        while (true) {
            kernel_.wait(Event::InputReady, 1000_usec);
            
            process_encoders();
            process_buttons();
            process_adc();
        }
    }
    
private:
    void process_encoders() {
        for (std::size_t i = 0; i < encoder_count_; ++i) {
            int raw_delta = raw_.encoder_delta[i].exchange(0);
            if (raw_delta == 0) continue;
            
            // 加速処理
            float filtered = apply_acceleration(raw_delta);
            
            // バインディング解決（ページ/レイヤー考慮）
            auto param_idx = Bindings::resolve(i, ui_state_);
            if (!param_idx) continue;
            
            // パラメータ更新
            constexpr auto& info = App::params[*param_idx];
            float current = shared_.params[*param_idx].load(std::memory_order_relaxed);
            float step = (info.max - info.min) * 0.01f;
            float next = std::clamp(current + filtered * step, info.min, info.max);
            shared_.params[*param_idx].store(next, std::memory_order_relaxed);
        }
    }
    
    void process_buttons() {
        for (std::size_t i = 0; i < button_count_; ++i) {
            bool raw = raw_.button_state[i].load();
            
            // デバウンス（8回連続で安定したら確定）
            if (raw == last_button_[i]) {
                debounce_[i] = 0;
                continue;
            }
            if (++debounce_[i] < 8) continue;
            
            last_button_[i] = raw;
            debounce_[i] = 0;
            
            // バインディング適用
            Bindings::handle_button(i, raw, ui_state_, shared_);
        }
    }
    
    void process_adc() {
        for (std::size_t i = 0; i < adc_count_; ++i) {
            std::uint16_t raw = raw_.adc_values[i].load();
            
            // ノイズフィルタ（移動平均）
            adc_filtered_[i] = adc_filtered_[i] * 0.9f + raw * 0.1f;
            
            // デッドバンド（小さな変化は無視）
            if (std::abs(adc_filtered_[i] - adc_last_[i]) < 10) continue;
            adc_last_[i] = adc_filtered_[i];
            
            // パラメータ更新
            auto param_idx = Bindings::adc_to_param(i);
            if (!param_idx) continue;
            
            float normalized = adc_filtered_[i] / 4095.f;
            constexpr auto& info = App::params[*param_idx];
            float value = info.min + normalized * (info.max - info.min);
            shared_.params[*param_idx].store(value, std::memory_order_relaxed);
        }
    }
};
```

---

### Display Server Task

```cpp
template<typename App>
class DisplayServer {
    SharedState& shared_;
    
public:
    void run() {  // Low優先度
        while (true) {
            kernel_.wait(Event::None, 16000_usec);  // ~60fps
            
            update_lcd();
            update_leds();
        }
    }
    
private:
    void update_lcd() {
        auto dirty = shared_.display.dirty.exchange(0, std::memory_order_acquire);
        if (!dirty) return;
        
        for (int i = 0; i < 4; ++i) {
            if (dirty & (1 << i)) {
                lcd_.write_line(i, shared_.display.lines[i].data());
            }
        }
    }
    
    void update_leds() {
        // パラメータ値からLED更新（バインディングに従う）
        for (std::size_t i = 0; i < led_count_; ++i) {
            if (auto param_idx = Bindings::led_to_param(i)) {
                constexpr auto& info = App::params[*param_idx];
                float value = shared_.params[*param_idx].load(std::memory_order_relaxed);
                float normalized = (value - info.min) / (info.max - info.min);
                led_driver_.set_pwm(i, static_cast<std::uint8_t>(normalized * 255));
            } else {
                // 直接設定されたLED
                led_driver_.set_pwm(i, shared_.leds.values[i].load());
            }
        }
    }
};
```

---

### バインディング解決（コンパイル時）

```cpp
namespace umi {

template<typename... Bindings>
struct BindingList {
    // エンコーダ → パラメータ（ページ/レイヤー対応）
    static constexpr auto resolve(std::size_t enc_id, const UIState& state) 
        -> std::optional<std::size_t> 
    {
        // Shift レイヤー確認
        if constexpr (has_shift_layer<enc_id>) {
            if (state.modifier_mask & shift_mask<enc_id>) {
                return shift_param<enc_id>;
            }
        }
        
        // ページ確認
        if constexpr (is_paged<enc_id>) {
            auto page_params = page_param_list<enc_id>[state.current_page];
            auto offset = enc_id - page_start<enc_id>;
            if (offset < page_params.size()) {
                return page_params[offset];
            }
            return std::nullopt;
        }
        
        // 直接バインド
        return direct_binding<enc_id>;
    }
};

}
```

---

### UIState（実行時状態、最小限）

```cpp
struct UIState {
    std::uint8_t current_page{0};      // ページ番号
    std::uint8_t modifier_mask{0};      // Shift等のビットマスク
    std::uint8_t menu_depth{0};         // メニュー階層
    std::uint8_t menu_index[4]{};       // 各階層の選択位置
};
// 合計: 7 バイト
```

---

### データフロー（入力）

```
┌─────────────────────────────────────────────────────────────────┐
│  Hardware                                                        │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐                        │
│  │ Encoder  │ │  Button  │ │   ADC    │                        │
│  │  (EXTI)  │ │  (Timer) │ │  (DMA)   │                        │
│  └────┬─────┘ └────┬─────┘ └────┬─────┘                        │
│       │ ISR        │ poll       │ DMA完了                       │
├───────┼────────────┼────────────┼───────────────────────────────┤
│  Raw State                                                       │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐                        │
│  │ delta[]  │ │ state[]  │ │ values[] │                        │
│  │ atomic   │ │ atomic   │ │ atomic   │                        │
│  └────┬─────┘ └────┬─────┘ └────┬─────┘                        │
│       │            │            │                               │
├───────┴────────────┴────────────┴───────────────────────────────┤
│  Input Server Task (Normal)                                      │
│  - 加速処理    - デバウンス    - ノイズフィルタ                    │
│  - バインディング解決（ページ/レイヤー）                           │
│  - パラメータ更新                                                │
├─────────────────────────────────────────────────────────────────┤
│  Shared Memory: params[] (atomic<float>)                         │
├─────────────────────────────────────────────────────────────────┤
│  Audio Task (Realtime)                                           │
│  ctx.params[i] で読み取り                                        │
└─────────────────────────────────────────────────────────────────┘
```

### データフロー（出力）

```
┌─────────────────────────────────────────────────────────────────┐
│  Audio Task (Realtime)                                           │
│  ctx.display.line(0, "Freq: {}", freq);                         │
│  ctx.leds.set(0, normalized);                                   │
├─────────────────────────────────────────────────────────────────┤
│  Shared Memory: display / leds (atomic + dirty flags)            │
├─────────────────────────────────────────────────────────────────┤
│  Display Server Task (Low)                                       │
│  - dirty確認                                                     │
│  - LCD更新コマンド生成                                           │
│  - LED PWM値設定                                                 │
├─────────────────────────────────────────────────────────────────┤
│  Hardware Drivers                                                │
│  ┌──────────┐ ┌──────────┐                                      │
│  │ LCD SPI  │ │ LED PWM  │                                      │
│  │  (DMA)   │ │  (Timer) │                                      │
│  └──────────┘ └──────────┘                                      │
└─────────────────────────────────────────────────────────────────┘
```

---

### タスク構成

| タスク | 優先度 | 責務 | 周期 |
|--------|--------|------|------|
| **Audio Task** | Realtime | process() 呼び出し | DMA完了 |
| **MIDI Server** | High | MIDI受信 → 共有メモリ | イベント |
| **Input Server** | Normal | HW入力 → フィルタ → 共有メモリ | 1ms |
| **Display Server** | Low | 共有メモリ → HW出力 | 16ms |

---

### GUIアダプター（VST/AU/WASM）

デスクトップ環境では Server Task の代わりにホストがUIスレッドを管理。

```cpp
template<typename App>
class GUIAdapter {
    SharedState& shared_;
    
public:
    void render(Canvas& ctx) {
        // params から GUI を描画
        for (std::size_t i = 0; i < App::params.size(); ++i) {
            float value = shared_.params[i].load();
            if (ctx.knob(App::params[i].name, value, 
                         App::params[i].min, App::params[i].max)) {
                shared_.params[i].store(value);
            }
        }
    }
};
```

---

### ウィジェット対応表

| ウィジェット | Hardware | GUI | Headless (MIDI) |
|--------------|----------|-----|-----------------|
| `Knob` | エンコーダ + LED リング | 回転ノブ | CC (連続値) |
| `Slider` | ADC フェーダー | スライダー | CC (連続値) |
| `Selector` | ボタン + LCD | ドロップダウン | CC (離散値) |
| `Toggle` | スイッチ + LED | チェックボックス | CC (0/127) |

---

### ファイル構成

```
adapter/
├── common/
│   ├── adapter_base.hh
│   ├── param.hh           # Param, ParamInfo
│   └── shared_state.hh    # SharedState, UIState
├── ui/
│   ├── bindings.hh        # バインディング定義
│   ├── input_server.hh    # InputServer Task
│   └── display_server.hh  # DisplayServer Task
├── mcu/
│   ├── run.hh
│   └── raw_state.hh       # RawState (DMA/ISR用)
└── vst3/
    ├── processor.hh
    └── gui_adapter.hh     # GUIAdapter
```

# ホストアダプター設計

MCUで動くコードをVST/AU/CLAP/WASMでも動かすための仕組み。

---

## 設計原則

**process() が必要とする全ての外部情報は引数（Context）経由で渡す。**

```cpp
// ✅ 良い例: 全て引数経由
void process(umi::Context& ctx) {
    float freq = ctx.params[0];              // パラメータ
    while (auto ev = ctx.midi_in.pop_at(i))  // MIDI
    ctx.audio_out[0][i] = generate();        // オーディオ出力
}

// ❌ 悪い例: グローバル/シングルトン
void process(umi::Context& ctx) {
    float sr = GlobalConfig::sample_rate;        // NG
    auto& midi = MidiServer::instance().queue(); // NG
}
```

| 許可 | 禁止 |
|------|------|
| 引数 (Context) | グローバル変数 |
| メンバ変数（自身の状態） | シングルトン |
| constexpr 定数 | static mutable |
| | ハードウェア直接アクセス |

---

## Context 構造

### 型安全なバッファビュー

チャンネル数・フレーム数をバッファと一体化し、インターフェースを明確にする。

```cpp
namespace umi {

// 安全なオーディオバッファビュー（ゼロコスト抽象化）
template<typename T>
struct AudioBuffer {
    T** data;
    std::uint8_t channels;
    std::uint16_t frames;
    
    // 安全なチャンネルアクセス → span<T> を返す
    std::span<T> operator[](std::uint8_t ch) const {
        return {data[ch], frames};
    }
    
    auto channel_count() const { return channels; }
    auto frame_count() const { return frames; }
};

// MIDIポートバッファ（入力用）
struct MidiIn {
    MidiQueue* queues;    // ポートごとのキュー配列
    std::uint8_t ports;
    
    MidiQueue& operator[](std::uint8_t port) const {
        return queues[port];
    }
    
    // シングルポートの場合の便利メソッド
    std::optional<midi::Event> pop_at(std::size_t sample_pos) {
        return queues[0].pop_at(sample_pos);
    }
    
    auto port_count() const { return ports; }
};

// MIDIポートバッファ（出力用）
struct MidiOut {
    MidiOutQueue* queues;
    std::uint8_t ports;
    
    MidiOutQueue& operator[](std::uint8_t port) const {
        return queues[port];
    }
    
    void send(const midi::Event& ev, std::size_t sample_offset = 0) {
        queues[0].push(sample_offset, ev);
    }
    
    auto port_count() const { return ports; }
};

struct Context {
    // === オーディオ I/O（型安全）===
    AudioBuffer<float> audio_out;         // 出力バッファ
    AudioBuffer<const float> audio_in;    // 入力バッファ
    float sample_rate;                    // サンプルレート
    
    // === MIDI I/O（サンプル精度）===
    MidiIn midi_in;                       // MIDI入力（マルチポート対応）
    MidiOut midi_out;                     // MIDI出力（マルチポート対応）
    
    // === パラメータ ===
    std::span<const float> params;        // パラメータ値（読み取り専用）
    
    // === オプショナル（nullptr可）===
    Display* display{nullptr};            // 表示更新
    const Transport* transport{nullptr};  // DAWトランスポート情報
};

struct MidiQueue {
    struct TimedEvent {
        std::size_t sample_offset;        // バッファ内のサンプル位置
        midi::Event event;
    };
    
    // サンプル位置 i のイベントを取得
    std::optional<midi::Event> pop_at(std::size_t sample_pos);
    
    // アダプターが呼ぶ
    void push(std::size_t offset, const midi::Event& ev);
    void clear();
};

struct MidiOutQueue {
    void push(std::size_t sample_offset, const midi::Event& ev);
    void clear();
};

struct Transport {
    std::uint64_t position_samples;       // 再生位置（サンプル）
    float bpm;                            // テンポ
    bool is_playing;                      // 再生中か
};

}
```

### オーバーヘッドについて

`AudioBuffer` と `MidiIn/MidiOut` はゼロコスト抽象化です：

- `operator[]` はインライン展開され、生ポインタアクセスと同一コードを生成
- `span<T>` はポインタ + サイズのペアで、最適化により消去される
- Context全体のサイズ増加は約8バイト（チャンネル/ポート数のメタデータ分）

```cpp
// コンパイル後（-O2以上）は完全に同一
auto out = ctx.audio_out[ch];    // → float* ptr = data[ch];
for (auto& s : out) { ... }      // → for (i = 0; i < frames; i++) { ptr[i] = ...; }
```

### 動的構成変更（USB拡張など）

Contextは毎回の `process()` 呼び出し時に渡されるため、動的な構成変更に対応可能：

```cpp
// ホストアダプター側：USB Audio接続時
void on_usb_audio_connected(uint8_t extra_channels) {
    usb_channels = extra_channels;
    usb_connected = true;
    // 次の process() から反映
}

void audio_task() {
    if (usb_connected) {
        // 拡張構成
        float* combined_out[10];  // main(2) + usb(8)
        std::copy(main_out, main_out + 2, combined_out);
        std::copy(usb_out, usb_out + usb_channels, combined_out + 2);
        ctx.audio_out = {combined_out, uint8_t(2 + usb_channels), frames};
    } else {
        ctx.audio_out = {main_out, 2, frames};
    }
    app.process(ctx);
}

// アプリ側：チャンネル数を動的に扱う
void MyApp::process(Context& ctx) {
    // メインステレオ（常に存在）
    generate_stereo(ctx.audio_out[0], ctx.audio_out[1]);
    
    // 追加チャンネルがあれば使用
    for (uint8_t ch = 2; ch < ctx.audio_out.channel_count(); ++ch) {
        generate_aux(ctx.audio_out[ch], ch - 2);
    }
}
```

---

## AudioProcessor インターフェース

```cpp
namespace umi {

template<typename Derived>
struct AudioProcessor {
    // パラメータ定義（必須）
    static constexpr auto params = std::array<Param, 0>{};
    
    // ライフサイクル
    void prepare(float sample_rate, std::size_t max_buffer_size) {}
    void reset() {}
    
    // オーディオ処理（必須）
    void process(Context& ctx);
};

}
```

### 使用例

```cpp
struct MySynth : umi::AudioProcessor<MySynth> {
    static constexpr auto params = std::array{
        umi::Param{"freq", 20.f, 20000.f, 440.f},
        umi::Param{"reso", 0.f, 1.f, 0.5f},
    };
    
    // 内部状態（メンバ変数はOK）
    float phase_{0.f};
    float sample_rate_{48000.f};
    
    void prepare(float sr, std::size_t) {
        sample_rate_ = sr;
    }
    
    void process(umi::Context& ctx) {
        float freq = ctx.params[0];
        auto frames = ctx.audio_out.frame_count();
        
        for (std::size_t i = 0; i < frames; ++i) {
            // サンプル精度でMIDI処理
            while (auto ev = ctx.midi_in.pop_at(i)) {
                if (ev->type == umi::midi::Type::NoteOn) {
                    freq = midi_to_freq(ev->data1);
                }
            }
            
            // オーディオ生成（型安全なアクセス）
            float sample = std::sin(phase_ * 6.28318f);
            ctx.audio_out[0][i] = sample;
            ctx.audio_out[1][i] = sample;
            phase_ += freq / sample_rate_;
            if (phase_ > 1.f) phase_ -= 1.f;
        }
        
        // オプショナル: 表示更新
        if (ctx.display) {
            ctx.display->line(0, "Freq: {:.1f}", freq);
        }
    }
};
```

---

## 概要

```
┌─────────────────────────────────────────────────────────┐
│              AudioProcessor（ユーザーコード）             │
│                     100% ポータブル                      │
├─────────────────────────────────────────────────────────┤
│                   Host Adapter Layer                     │
│                                                         │
│  各ホストの違いを吸収し、Context を構築して process() へ  │
│                                                         │
│  ┌───────┐ ┌───────┐ ┌───────┐ ┌───────┐ ┌───────┐    │
│  │  MCU  │ │ VST3  │ │  AU   │ │ CLAP  │ │ WASM  │    │
│  └───────┘ └───────┘ └───────┘ └───────┘ └───────┘    │
└─────────────────────────────────────────────────────────┘
```

---

## 各ホストの違い

| 項目 | MCU | VST3 | AU | CLAP | WASM |
|------|-----|------|-----|------|------|
| オーディオコール | DMA完了通知 | processBlock() | render() | process() | AudioWorklet |
| バッファサイズ | 固定(64-256) | 可変 | 可変 | 可変 | 固定128 |
| MIDI形式 | ISR→Queue | Vst::Event | MIDIPacket | clap_event | postMessage |
| MIDIタイミング | タイムスタンプ | sampleOffset | timeStamp | time | バッファ単位 |
| パラメータ | 共有メモリ | IEditController | AUParameter | clap_param | SharedArrayBuffer |
| サンプルレート | 固定 | 可変 | 可変 | 可変 | 可変 |
| スレッド | RTOSタスク | ホスト管理 | ホスト管理 | ホスト管理 | AudioWorklet |

---

## MCU Adapter

カーネルがタスクを管理し、Context を構築して `process()` を呼び出す。

```cpp
// adapter/mcu/run.hh
namespace umi::mcu {

template<typename App>
[[noreturn]] void run(App& app) {
    Kernel kernel;
    
    // 静的バッファ
    alignas(4) static float out_buf[2][BufferSize];
    alignas(4) static float in_buf[2][BufferSize];
    static std::array<float, App::params.size()> param_values;
    static MidiQueue midi_queue;
    static MidiQueue midi_queue_swap;  // ダブルバッファ
    
    // 初期化
    app.prepare(SampleRate, BufferSize);
    for (std::size_t i = 0; i < App::params.size(); ++i) {
        param_values[i] = App::params[i].default_value;
    }
    
    // オーディオタスク（Realtime）
    kernel.create_task({
        .entry = [&app](void*) {
            float* out_ptrs[2] = {out_buf[0], out_buf[1]};
            const float* in_ptrs[2] = {in_buf[0], in_buf[1]};
            static MidiQueue midi_queue_single;
            
            while (true) {
                kernel.wait(Event::AudioReady);
                
                // Context 構築（型安全）
                std::swap(midi_queue, midi_queue_swap);
                midi_queue_single = midi_queue_swap;
                
                Context ctx{
                    .audio_out = {out_ptrs, 2, BufferSize},
                    .audio_in = {in_ptrs, 2, BufferSize},
                    .sample_rate = SampleRate,
                    .midi_in = {&midi_queue_single, 1},
                    .midi_out = {},  // 必要に応じて設定
                    .params = param_values,
                };
                
                // オーディオ処理
                auto start = DWT::cycles();
                app.process(ctx);
                load_monitor.record(DWT::cycles() - start);
                
                midi_queue_swap.clear();
            }
        },
        .prio = Priority::Realtime,
        .name = "audio",
    });
    
    // MIDI Server（High）- タイムスタンプ付きでキューイング
    kernel.create_task({
        .entry = [](void*) {
            while (true) {
                kernel.wait(Event::MidiReady);
                while (auto raw = midi_uart.pop()) {
                    auto ev = parse_midi(raw);
                    auto offset = timestamp_to_sample_offset(ev.timestamp);
                    midi_queue.push(offset, ev);
                }
            }
        },
        .prio = Priority::High,
        .name = "midi",
    });
    
    kernel.start();
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

template<typename App>
class Vst3Processor : public Steinberg::Vst::AudioEffect {
    App app_;
    std::array<float, App::params.size()> param_values_;
    MidiQueue midi_queue_;
    float sample_rate_{44100.f};
    
public:
    tresult initialize(FUnknown* context) override {
        for (std::size_t i = 0; i < App::params.size(); ++i) {
            param_values_[i] = App::params[i].default_value;
        }
        return AudioEffect::initialize(context);
    }
    
    tresult setBusArrangements(...) override { /* ... */ }
    
    tresult setupProcessing(ProcessSetup& setup) override {
        sample_rate_ = setup.sampleRate;
        app_.prepare(sample_rate_, setup.maxSamplesPerBlock);
        return kResultOk;
    }
    
    tresult process(ProcessData& data) override {
        midi_queue_.clear();
        
        // MIDI イベント変換（サンプルオフセット付き）
        if (data.inputEvents) {
            for (int32 i = 0; i < data.inputEvents->getEventCount(); ++i) {
                Event e;
                data.inputEvents->getEvent(i, e);
                
                if (e.type == Event::kNoteOnEvent) {
                    midi_queue_.push(e.sampleOffset, midi::Event{
                        midi::Type::NoteOn,
                        static_cast<uint8_t>(e.noteOn.channel),
                        static_cast<uint8_t>(e.noteOn.pitch),
                        static_cast<uint8_t>(e.noteOn.velocity * 127),
                    });
                } else if (e.type == Event::kNoteOffEvent) {
                    midi_queue_.push(e.sampleOffset, midi::Event{
                        midi::Type::NoteOff,
                        static_cast<uint8_t>(e.noteOff.channel),
                        static_cast<uint8_t>(e.noteOff.pitch),
                        0,
                    });
                }
            }
        }
        
        // パラメータ変更
        if (data.inputParameterChanges) {
            for (int32 i = 0; i < data.inputParameterChanges->getParameterCount(); ++i) {
                auto* queue = data.inputParameterChanges->getParameterData(i);
                ParamValue value;
                int32 offset;
                queue->getPoint(queue->getPointCount() - 1, offset, value);
                
                auto id = queue->getParameterId();
                auto& info = App::params[id];
                param_values_[id] = info.min + value * (info.max - info.min);
            }
        }
        
        // Context 構築（型安全）
        float* out_ptrs[2] = {data.outputs[0].channelBuffers32[0],
                              data.outputs[0].channelBuffers32[1]};
        const float* in_ptrs[2] = {data.inputs[0].channelBuffers32[0],
                                   data.inputs[0].channelBuffers32[1]};
        
        Context ctx{
            .audio_out = {out_ptrs, 2, static_cast<std::uint16_t>(data.numSamples)},
            .audio_in = {in_ptrs, 2, static_cast<std::uint16_t>(data.numSamples)},
            .sample_rate = sample_rate_,
            .midi_in = {&midi_queue_, 1},
            .midi_out = {},
            .params = param_values_,
        };
        
        app_.process(ctx);
        return kResultOk;
    }
};

}
```

---

## AU Adapter

Apple Audio Unit とのブリッジ。

```cpp
// adapter/au/processor.hh
namespace umi::au {

template<typename App>
class AUProcessor : public AUAudioUnit {
    App app_;
    std::array<float, App::params.size()> param_values_;
    MidiQueue midi_queue_;
    float sample_rate_;
    
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
            midi_queue_.clear();
            
            // MIDI イベント処理（サンプルオフセット付き）
            for (auto* event = realtimeEvents; event; event = event->head.next) {
                if (event->head.eventType == AURenderEventMIDI) {
                    auto offset = event->head.eventSampleTime - timestamp->mSampleTime;
                    midi_queue_.push(offset, convert_midi(event->MIDI));
                }
            }
            
            // Context 構築（型安全）
            float* out_ptrs[2] = {(float*)outputData->mBuffers[0].mData,
                                  (float*)outputData->mBuffers[1].mData};
            
            Context ctx{
                .audio_out = {out_ptrs, 2, static_cast<std::uint16_t>(frameCount)},
                .audio_in = {},  // 入力なし
                .sample_rate = sample_rate_,
                .midi_in = {&midi_queue_, 1},
                .midi_out = {},
                .params = param_values_,
            };
            
            app_.process(ctx);
            return noErr;
        };
    }
};

}
```

---

## CLAP Adapter

CLAP (Clever Audio Plugin) とのブリッジ。VST3 より単純でモダンな設計。

```cpp
// adapter/clap/processor.hh
namespace umi::clap {

template<typename App>
class ClapProcessor {
    App app_;
    std::array<float, App::params.size()> param_values_;
    MidiQueue midi_queue_;
    float sample_rate_{44100.f};
    
public:
    static const clap_plugin_descriptor* descriptor() {
        static clap_plugin_descriptor desc = {
            .clap_version = CLAP_VERSION,
            .id = App::id,
            .name = App::name,
            .vendor = App::vendor,
            // ...
        };
        return &desc;
    }
    
    bool init() {
        for (std::size_t i = 0; i < App::params.size(); ++i) {
            param_values_[i] = App::params[i].default_value;
        }
        return true;
    }
    
    bool activate(double sr, uint32_t min_frames, uint32_t max_frames) {
        sample_rate_ = static_cast<float>(sr);
        app_.prepare(sample_rate_, max_frames);
        return true;
    }
    
    clap_process_status process(const clap_process* p) {
        midi_queue_.clear();
        
        // イベント処理
        auto* in_events = p->in_events;
        for (uint32_t i = 0; i < in_events->size(in_events); ++i) {
            auto* header = in_events->get(in_events, i);
            
            if (header->type == CLAP_EVENT_NOTE_ON) {
                auto* ev = reinterpret_cast<const clap_event_note*>(header);
                midi_queue_.push(header->time, midi::Event{
                    midi::Type::NoteOn,
                    static_cast<uint8_t>(ev->channel),
                    static_cast<uint8_t>(ev->key),
                    static_cast<uint8_t>(ev->velocity * 127),
                });
            } else if (header->type == CLAP_EVENT_NOTE_OFF) {
                auto* ev = reinterpret_cast<const clap_event_note*>(header);
                midi_queue_.push(header->time, midi::Event{
                    midi::Type::NoteOff,
                    static_cast<uint8_t>(ev->channel),
                    static_cast<uint8_t>(ev->key),
                    0,
                });
            } else if (header->type == CLAP_EVENT_PARAM_VALUE) {
                auto* ev = reinterpret_cast<const clap_event_param_value*>(header);
                param_values_[ev->param_id] = static_cast<float>(ev->value);
            }
        }
        
        // Context 構築（型安全）
        float* out_ptrs[2] = {p->audio_outputs[0].data32[0],
                              p->audio_outputs[0].data32[1]};
        const float* in_ptrs[2] = {p->audio_inputs[0].data32[0],
                                   p->audio_inputs[0].data32[1]};
        
        Context ctx{
            .audio_out = {out_ptrs, 2, static_cast<std::uint16_t>(p->frames_count)},
            .audio_in = {in_ptrs, 2, static_cast<std::uint16_t>(p->frames_count)},
            .sample_rate = sample_rate_,
            .midi_in = {&midi_queue_, 1},
            .midi_out = {},
            .params = param_values_,
        };
        
        app_.process(ctx);
        return CLAP_PROCESS_CONTINUE;
    }
    
    // パラメータ情報
    uint32_t params_count() const { return App::params.size(); }
    
    bool params_get_info(uint32_t index, clap_param_info* info) const {
        if (index >= App::params.size()) return false;
        
        const auto& p = App::params[index];
        info->id = index;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE;
        std::strncpy(info->name, p.name, CLAP_NAME_SIZE);
        info->min_value = p.min;
        info->max_value = p.max;
        info->default_value = p.default_value;
        return true;
    }
};

}
```

**使用方法:**
```cpp
#include "my_synth.hh"
#include <umi/clap/processor.hh>

extern "C" const clap_plugin_entry clap_entry = {
    .clap_version = CLAP_VERSION,
    .init = [](const char*) { return true; },
    .deinit = []() {},
    .get_factory = [](const char* id) -> const void* {
        if (strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0) {
            return &umi::clap::factory<MySynth>;
        }
        return nullptr;
    }
};
```

---

## WASM AudioWorklet Adapter

WebAssembly + AudioWorklet とのブリッジ。

### C++ 側（Emscripten）

```cpp
// adapter/wasm/processor.hh
namespace umi::wasm {

template<typename App>
class WasmProcessor {
    App app_;
    std::array<float, App::params.size()> param_values_;
    MidiQueue midi_queue_;
    float sample_rate_{48000.f};
    
    // AudioWorklet との共有バッファ
    alignas(4) float audio_out_[2][128];
    alignas(4) float audio_in_[2][128];
    
public:
    WasmProcessor() {
        for (std::size_t i = 0; i < App::params.size(); ++i) {
            param_values_[i] = App::params[i].default_value;
        }
    }
    
    void prepare(float sr) {
        sample_rate_ = sr;
        app_.prepare(sr, 128);
    }
    
    // JS から呼ばれる
    float* get_audio_out(int ch) { return audio_out_[ch]; }
    float* get_audio_in(int ch) { return audio_in_[ch]; }
    float* get_param_buffer() { return param_values_.data(); }
    
    void process(int frames) {
        float* out_ptrs[2] = {audio_out_[0], audio_out_[1]};
        const float* in_ptrs[2] = {audio_in_[0], audio_in_[1]};
        
        Context ctx{
            .audio_out = {out_ptrs, 2, static_cast<std::uint16_t>(frames)},
            .audio_in = {in_ptrs, 2, static_cast<std::uint16_t>(frames)},
            .sample_rate = sample_rate_,
            .midi_in = {&midi_queue_, 1},
            .midi_out = {},
            .params = param_values_,
        };
        
        app_.process(ctx);
        midi_queue_.clear();
    }
    
    void note_on(int channel, int note, int velocity) {
        midi_queue_.push(0, midi::Event{
            midi::Type::NoteOn,
            static_cast<uint8_t>(channel),
            static_cast<uint8_t>(note),
            static_cast<uint8_t>(velocity),
        });
    }
    
    void note_off(int channel, int note) {
        midi_queue_.push(0, midi::Event{
            midi::Type::NoteOff,
            static_cast<uint8_t>(channel),
            static_cast<uint8_t>(note),
            0,
        });
    }
    
    // パラメータ情報
    static int get_param_count() { return App::params.size(); }
    static const char* get_param_name(int i) { return App::params[i].name; }
    static float get_param_min(int i) { return App::params[i].min; }
    static float get_param_max(int i) { return App::params[i].max; }
    static float get_param_default(int i) { return App::params[i].default_value; }
};

}  // namespace umi::wasm

// Emscripten バインディング
#include <emscripten/bind.h>

EMSCRIPTEN_BINDINGS(umi_synth) {
    using Proc = umi::wasm::WasmProcessor<MySynth>;
    
    emscripten::class_<Proc>("Processor")
        .constructor<>()
        .function("prepare", &Proc::prepare)
        .function("getAudioOut", &Proc::get_audio_out, emscripten::allow_raw_pointers())
        .function("getAudioIn", &Proc::get_audio_in, emscripten::allow_raw_pointers())
        .function("getParamBuffer", &Proc::get_param_buffer, emscripten::allow_raw_pointers())
        .function("process", &Proc::process)
        .function("noteOn", &Proc::note_on)
        .function("noteOff", &Proc::note_off)
        .class_function("getParamCount", &Proc::get_param_count)
        .class_function("getParamName", &Proc::get_param_name, emscripten::allow_raw_pointers())
        .class_function("getParamMin", &Proc::get_param_min)
        .class_function("getParamMax", &Proc::get_param_max)
        .class_function("getParamDefault", &Proc::get_param_default);
}
```

### AudioWorklet 側（JavaScript）

```javascript
// worklet/processor.js
class UmiProcessor extends AudioWorkletProcessor {
    constructor() {
        super();
        this.processor = null;
        this.paramBuffer = null;
        this.audioOut = [null, null];
        this.audioIn = [null, null];
        
        this.port.onmessage = (e) => this.handleMessage(e.data);
    }
    
    async handleMessage(msg) {
        if (msg.type === 'init') {
            await this.initWasm(msg.wasmModule);
        } else if (msg.type === 'param') {
            if (this.paramBuffer) {
                this.paramBuffer[msg.index] = msg.value;
            }
        } else if (msg.type === 'noteOn') {
            this.processor?.noteOn(msg.channel, msg.note, msg.velocity);
        } else if (msg.type === 'noteOff') {
            this.processor?.noteOff(msg.channel, msg.note);
        }
    }
    
    async initWasm(wasmModule) {
        const Module = await wasmModule();
        this.processor = new Module.Processor();
        this.processor.prepare(sampleRate);  // AudioWorklet グローバル
        
        // 共有バッファ取得
        const paramPtr = this.processor.getParamBuffer();
        this.paramBuffer = new Float32Array(
            Module.HEAPF32.buffer, paramPtr, Module.Processor.getParamCount());
        
        for (let ch = 0; ch < 2; ch++) {
            this.audioOut[ch] = new Float32Array(
                Module.HEAPF32.buffer, this.processor.getAudioOut(ch), 128);
            this.audioIn[ch] = new Float32Array(
                Module.HEAPF32.buffer, this.processor.getAudioIn(ch), 128);
        }
        
        // デフォルト値設定
        for (let i = 0; i < Module.Processor.getParamCount(); i++) {
            this.paramBuffer[i] = Module.Processor.getParamDefault(i);
        }
        
        this.port.postMessage({ type: 'ready' });
    }
    
    process(inputs, outputs, parameters) {
        if (!this.processor) return true;
        
        // 入力コピー
        const input = inputs[0];
        if (input?.length >= 2) {
            this.audioIn[0].set(input[0]);
            this.audioIn[1].set(input[1]);
        }
        
        // WASM 処理
        this.processor.process(128);
        
        // 出力コピー
        const output = outputs[0];
        output[0].set(this.audioOut[0]);
        output[1].set(this.audioOut[1]);
        
        return true;
    }
}

registerProcessor('umi-processor', UmiProcessor);
```

### メインスレッド側（JavaScript）

```javascript
// main.js
class UmiSynth {
    async init() {
        this.audioContext = new AudioContext();
        await this.audioContext.audioWorklet.addModule('worklet/processor.js');
        
        this.workletNode = new AudioWorkletNode(this.audioContext, 'umi-processor');
        this.workletNode.connect(this.audioContext.destination);
        
        const wasmModule = await import('./synth.js');
        this.workletNode.port.postMessage({ type: 'init', wasmModule: wasmModule.default });
        
        return new Promise(resolve => {
            this.workletNode.port.onmessage = (e) => {
                if (e.data.type === 'ready') resolve();
            };
        });
    }
    
    setParam(index, value) {
        this.workletNode.port.postMessage({ type: 'param', index, value });
    }
    
    noteOn(channel, note, velocity) {
        this.workletNode.port.postMessage({ type: 'noteOn', channel, note, velocity });
    }
    
    noteOff(channel, note) {
        this.workletNode.port.postMessage({ type: 'noteOff', channel, note });
    }
}

// 使用例
const synth = new UmiSynth();
await synth.init();
synth.noteOn(0, 60, 100);
```

---

## アダプター比較

| 項目 | MCU | VST3 | AU | CLAP | WASM |
|------|-----|------|-----|------|------|
| Context 構築 | カーネル | Adapter | Adapter | Adapter | Adapter |
| MIDI精度 | サンプル | サンプル | サンプル | サンプル | バッファ |
| パラメータ | 共有メモリ | IEditController | AUParameter | clap_param | postMessage |
| GUI | HW/なし | VSTGUI | Cocoa | 任意 | HTML |

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
| **引数経由のみ** | 外部状態へのアクセスは Context 経由 |

### 使用可能なもの

```cpp
// OK
#include <array>
#include <cstdint>
#include <cmath>        // sinf, cosf など
#include <span>         // C++20
#include <optional>     // C++17

// NG（process() 内では）
#include <vector>       // 動的メモリ
#include <string>       // 動的メモリ
#include <thread>       // スレッド
#include <mutex>        // スレッド
```

---

## ファイル構成

```
adapter/
├── common/
│   ├── context.hh         # Context
│   ├── audio_buffer.hh    # AudioBuffer<T>
│   ├── midi_buffer.hh     # MidiIn, MidiOut, MidiQueue
│   └── param.hh           # Param 定義
├── mcu/
│   └── run.hh             # umi::mcu::run()
├── vst3/
│   ├── processor.hh       # Vst3Processor
│   ├── controller.hh      # Vst3Controller
│   └── factory.hh         # プラグイン登録
├── au/
│   └── processor.hh       # AUProcessor
├── clap/
│   └── processor.hh       # ClapProcessor
└── wasm/
    └── processor.hh       # WasmProcessor + Emscripten bindings
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

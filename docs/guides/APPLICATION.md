# UMI ユースケース実装ガイド

**バージョン:** 3.0.0-draft

## 概要

UMIにおける代表的なユースケースと実装パターンを示します。

## アーキテクチャ

```
+-------------------------------------------------------------+
|                    Application (.umiapp / .umim)            |
|  +------------------------+  +--------------------------+   |
|  |  Processor Task        |  |  Control Task (main)     |   |
|  |  - process() DSP       |  |  - register_processor()  |   |
|  |  - リアルタイム        |  |  - wait_event() ループ   |   |
|  +------------------------+  +--------------------------+   |
|            ^                          |                     |
|            +-----共有メモリ-----------+                     |
+-------------------------------------------------------------+
```

---

## 1. エフェクター（最小構成）

### Processor + main

```cpp
// volume.cc
#include <umi/app.hh>

struct Volume {
    float volume = 1.0f;

    void process(umi::ProcessContext& ctx) {
        const float* in = ctx.input(0);
        float* out = ctx.output(0);
        if (!in || !out) return;

        for (uint32_t i = 0; i < ctx.frames(); ++i) {
            out[i] = in[i] * volume;
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

## 2. ディレイ（状態を持つエフェクト）

```cpp
// delay.cc
#include <umi/app.hh>
#include <array>

struct Delay {
    float time = 0.3f;
    float feedback = 0.5f;
    float mix = 0.5f;

    void process(umi::ProcessContext& ctx) {
        const float* in = ctx.input(0);
        float* out = ctx.output(0);
        if (!in || !out) return;

        size_t delay_samples = static_cast<size_t>(time * ctx.sample_rate());
        delay_samples = std::min(delay_samples, buffer_.size() - 1);

        for (uint32_t i = 0; i < ctx.frames(); ++i) {
            float delayed = buffer_[read_pos_];
            buffer_[write_pos_] = in[i] + delayed * feedback;
            out[i] = in[i] * (1.0f - mix) + delayed * mix;

            write_pos_ = (write_pos_ + 1) % buffer_.size();
            read_pos_ = (write_pos_ + buffer_.size() - delay_samples) % buffer_.size();
        }
    }

private:
    std::array<float, 96000> buffer_{};  // 最大2秒@48kHz
    size_t write_pos_ = 0;
    size_t read_pos_ = 0;
};

int main() {
    static Delay delay;
    umi::register_processor(delay);
    
    while (umi::wait_event().type != umi::EventType::Shutdown) {}
    return 0;
}
```

---

## 3. シンセサイザー（イベント処理）

```cpp
// synth.cc
#include <umi/app.hh>
#include <umidsp/oscillator.hh>
#include <umidsp/envelope.hh>

struct Synth {
    float attack = 0.01f;
    float decay = 0.1f;
    float sustain = 0.7f;
    float release = 0.3f;

    void process(umi::ProcessContext& ctx) {
        float* out = ctx.output(0);
        if (!out) return;

        // MIDIイベント処理
        for (const auto& e : ctx.events()) {
            if (e.is_note_on()) {
                freq_ = umi::dsp::midi_to_freq(e.note());
                env_.trigger();
            } else if (e.is_note_off()) {
                env_.release();
            }
        }

        // オーディオ生成
        float dt = 1.0f / ctx.sample_rate();
        float freq_norm = freq_ / ctx.sample_rate();
        
        for (uint32_t i = 0; i < ctx.frames(); ++i) {
            out[i] = osc_.tick(freq_norm) * env_.tick(dt);
        }
    }

private:
    umi::dsp::SawBL osc_;
    umi::dsp::ADSR env_;
    float freq_ = 440.0f;
};

int main() {
    static Synth synth;
    umi::register_processor(synth);
    
    while (umi::wait_event().type != umi::EventType::Shutdown) {}
    return 0;
}
```

---

## 4. UI状態を持つシンセ（MIDI Learn）

```cpp
// synth_with_ui.cc
#include <umi/app.hh>
#include "synth.hh"

int main() {
    static Synth synth;
    umi::register_processor(synth);
    
    // UI状態
    int selected_param = 0;
    bool midi_learn_active = false;
    std::array<int, 128> cc_mapping{};
    cc_mapping[74] = 4;  // CC74 → Cutoff
    
    while (true) {
        auto ev = umi::wait_event();
        
        switch (ev.type) {
        case umi::EventType::Shutdown:
            return 0;
            
        case umi::EventType::EncoderRotate:
            // パラメータ調整
            adjust_param(synth, selected_param, ev.encoder.delta);
            break;
            
        case umi::EventType::ButtonPress:
            if (ev.button.id == BTN_LEARN) {
                midi_learn_active = true;
            } else if (ev.button.id == BTN_PREV) {
                selected_param = std::max(0, selected_param - 1);
            } else if (ev.button.id == BTN_NEXT) {
                selected_param = std::min(4, selected_param + 1);
            }
            break;
            
        case umi::EventType::MidiCC:
            if (midi_learn_active) {
                cc_mapping[ev.midi.cc] = selected_param;
                midi_learn_active = false;
            } else if (cc_mapping[ev.midi.cc] >= 0) {
                apply_cc(synth, cc_mapping[ev.midi.cc], ev.midi.value);
            }
            break;
        }
    }
}
```

---

## 5. コルーチンによる実装

```cpp
// synth_coro.cc
#include <umi/app.hh>
#include <umi/coro.hh>
#include "synth.hh"

umi::Task<void> event_task(Synth& synth) {
    while (true) {
        auto ev = co_await umi::wait_event_async();
        if (ev.type == umi::EventType::Shutdown) co_return;
        synth.handle_event(ev);
    }
}

umi::Task<void> display_task(Synth& synth) {
    while (true) {
        co_await umi::sleep(33ms);
        update_display(synth);
    }
}

umi::Task<void> led_task() {
    while (true) {
        co_await umi::sleep(500ms);
        toggle_led();
    }
}

int main() {
    static Synth synth;
    umi::register_processor(synth);
    
    umi::Scheduler<4> sched;
    sched.spawn(event_task(synth));
    sched.spawn(display_task(synth));
    sched.spawn(led_task());
    sched.run();
    
    return 0;
}
```

---

## 6. モジュラー（CV対応）

### VCO

```cpp
struct VCO {
    float frequency = 440.0f;
    float fm_amount = 0.0f;

    void process(umi::ProcessContext& ctx) {
        const float* pitch_cv = ctx.input(0);
        const float* fm_cv = ctx.input(1);
        float* out = ctx.output(0);
        if (!out) return;

        for (uint32_t i = 0; i < ctx.frames(); ++i) {
            float pitch_mod = pitch_cv ? pitch_cv[i] : 0.0f;
            float fm_mod = fm_cv ? fm_cv[i] * fm_amount : 0.0f;
            float freq = frequency * std::pow(2.0f, pitch_mod + fm_mod);
            float freq_norm = freq / ctx.sample_rate();

            out[i] = osc_.tick(freq_norm);
        }
    }

private:
    umi::dsp::SawBL osc_;
};
```

### VCF

```cpp
struct VCF {
    float cutoff = 1000.0f;
    float resonance = 0.5f;
    float cv_amount = 1.0f;

    void process(umi::ProcessContext& ctx) {
        const float* in = ctx.input(0);
        const float* cv = ctx.input(1);
        float* out = ctx.output(0);
        if (!in || !out) return;

        for (uint32_t i = 0; i < ctx.frames(); ++i) {
            float mod = cv ? cv[i] * cv_amount : 0.0f;
            float freq = cutoff * std::pow(2.0f, mod);
            float freq_norm = freq / ctx.sample_rate();
            
            svf_.set_params(freq_norm, resonance);
            svf_.tick(in[i]);
            out[i] = svf_.lp();
        }
    }

private:
    umi::dsp::SVF svf_;
};
```

---

## ビルド

```lua
-- xmake.lua
target("my_synth")
    set_kind("binary")
    add_files("synth.cc")
    add_deps("umi_app")
    
    -- 組込み版
    if is_plat("cross") then
        set_extension(".umiapp")
        set_toolchains("arm-none-eabi")
    end
    
    -- WASM版
    if is_plat("wasm") then
        set_extension(".umim")
        set_toolchains("emscripten")
        add_ldflags("-sASYNCIFY")
    end
```

```bash
# 組込みビルド
xmake f -p cross -a cortex-m4
xmake build my_synth

# WASMビルド
xmake f -p wasm
xmake build my_synth
```

---

## 関連ドキュメント

- [UMIP_SPEC.md](UMIP_SPEC.md) - Processor仕様
- [UMIC_SPEC.md](UMIC_SPEC.md) - Controller仕様
- [API.md](API.md) - APIリファレンス

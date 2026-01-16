# UMI-OS ユースケース実装ガイド

**バージョン:** 2.0.0-draft
**ステータス:** ドラフト

## 概要

UMI-OSにおける代表的なユースケースと実装パターンを示します。

## アーキテクチャ

```
┌─────────────────────────────────────────────┐
│  アプリケーション層                           │
│  ┌─────────────────┐  ┌─────────────────┐   │
│  │  UMIP (Model)   │←─│  UMIC (Ctrl)    │   │
│  │  DSP処理        │  │  UIロジック      │   │
│  │  パラメータ=メンバ│  │  オプション      │   │
│  └─────────────────┘  └─────────────────┘   │
└─────────────────────────────────────────────┘
                    │
        ┌───────────┼───────────┐
        ▼           ▼           ▼
    ┌───────┐   ┌───────┐   ┌───────┐
    │ UMIM  │   │ 組込み │   │ VST3  │
    └───────┘   └───────┘   └───────┘
```

---

## 1. エフェクター（最小構成）

UMICなし、UMIPのみ。

### UMIP

```cpp
// volume.hh
struct Volume {
    float volume = 1.0f;

    void process(AudioContext& ctx) {
        const float* in = ctx.input(0);
        float* out = ctx.output(0);
        if (!in || !out) return;

        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            out[i] = in[i] * volume;
        }
    }
};
```

### メタデータ + エクスポート

```cpp
// volume_umim.cc
#include "volume.hh"
#include <umi/umim.hh>

constexpr umi::ParamMeta<Volume> params[] = {
    {&Volume::volume, "Volume", 0.0f, 1.0f, 1.0f},
};

UMIM_EXPORT(Volume, params);
```

---

## 2. ディレイ（状態を持つエフェクト）

### UMIP

```cpp
// delay.hh
struct Delay {
    float time = 0.3f;
    float feedback = 0.5f;
    float mix = 0.5f;

    void process(AudioContext& ctx) {
        const float* in = ctx.input(0);
        float* out = ctx.output(0);
        if (!in || !out) return;

        // サンプルレート変更時にバッファ再初期化
        if (sr_ != ctx.sample_rate) {
            sr_ = ctx.sample_rate;
            buffer_.resize(sr_ * 2);
            write_pos_ = 0;
            update_read_pos();
        }

        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            float delayed = buffer_[read_pos_];
            buffer_[write_pos_] = in[i] + delayed * feedback;
            out[i] = in[i] * (1.0f - mix) + delayed * mix;

            write_pos_ = (write_pos_ + 1) % buffer_.size();
            read_pos_ = (read_pos_ + 1) % buffer_.size();
        }
    }

private:
    void update_read_pos() {
        size_t samples = static_cast<size_t>(time * sr_);
        read_pos_ = (write_pos_ + buffer_.size() - samples) % buffer_.size();
    }

    uint32_t sr_ = 0;
    std::vector<float> buffer_;
    size_t write_pos_ = 0;
    size_t read_pos_ = 0;
};
```

### メタデータ

```cpp
constexpr umi::ParamMeta<Delay> params[] = {
    {&Delay::time,     "Time",     0.01f, 2.0f,  0.3f},
    {&Delay::feedback, "Feedback", 0.0f,  0.99f, 0.5f},
    {&Delay::mix,      "Mix",      0.0f,  1.0f,  0.5f},
};

UMIM_EXPORT(Delay, params);
```

---

## 3. シンセサイザー（イベント処理）

### UMIP

```cpp
// synth.hh
struct Synth {
    float attack = 0.01f;
    float decay = 0.1f;
    float sustain = 0.7f;
    float release = 0.3f;
    float cutoff = 1000.0f;

    void process(AudioContext& ctx) {
        float* out = ctx.output(0);
        if (!out) return;

        // イベント処理
        for (const auto& e : ctx.input_events) {
            if (e.type == EventType::Midi) {
                if (e.midi.is_note_on()) {
                    note_on(e.midi.note(), e.midi.velocity());
                } else if (e.midi.is_note_off()) {
                    note_off(e.midi.note());
                }
            }
        }

        // オーディオ生成
        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            out[i] = render_voices();
        }
    }

    void note_on(uint8_t note, uint8_t velocity);
    void note_off(uint8_t note);

private:
    float render_voices();
    // Voice pool...
};
```

### メタデータ

```cpp
constexpr umi::ParamMeta<Synth> params[] = {
    {&Synth::attack,  "Attack",  0.001f, 2.0f, 0.01f},
    {&Synth::decay,   "Decay",   0.001f, 2.0f, 0.1f},
    {&Synth::sustain, "Sustain", 0.0f,   1.0f, 0.7f},
    {&Synth::release, "Release", 0.001f, 5.0f, 0.3f},
    {&Synth::cutoff,  "Cutoff",  20.0f,  20000.0f, 1000.0f},
};

UMIM_EXPORT(Synth, params);
```

---

## 4. シンセ + UMIC（UI状態あり）

MIDI LearnやUI状態が必要な場合。

### UMIC

```cpp
// synth_controller.hh
struct SynthController {
    Synth& proc;

    // UI状態
    int page = 0;
    int selected_param = 0;
    bool midi_learn_active = false;
    std::array<int, 128> cc_mapping{};

    explicit SynthController(Synth& p) : proc(p) {
        cc_mapping[74] = 4;  // CC74 → Cutoff
    }

    void on_events(std::span<const Event> events) {
        for (const auto& e : events) {
            if (e.type != EventType::Midi) continue;

            if (midi_learn_active && e.midi.is_cc()) {
                cc_mapping[e.midi.cc_number()] = selected_param;
                midi_learn_active = false;
            }
        }
    }

    void on_encoder(int id, int delta) {
        // selected_param に応じてパラメータ変更
        // proc.cutoff += delta * 10.0f; など
    }

    void on_button(int id, bool pressed) {
        if (id == LEARN_BUTTON && pressed) {
            midi_learn_active = true;
        }
    }
};
```

### エクスポート

```cpp
UMIM_EXPORT_WITH_CONTROLLER(Synth, SynthController, params);
```

---

## 5. モジュラー（CV対応）

### VCO

```cpp
struct VCO {
    float frequency = 440.0f;
    float fm_amount = 0.0f;

    void process(AudioContext& ctx) {
        const float* pitch_cv = ctx.input(0);  // オプショナル
        const float* fm_cv = ctx.input(1);     // オプショナル
        float* out = ctx.output(0);
        if (!out) return;

        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            float pitch_mod = pitch_cv ? pitch_cv[i] : 0.0f;
            float fm_mod = fm_cv ? fm_cv[i] * fm_amount : 0.0f;
            float freq = frequency * std::pow(2.0f, pitch_mod + fm_mod);

            out[i] = generate(freq, ctx.dt);
        }
    }

private:
    float generate(float freq, float dt);
    float phase_ = 0.0f;
};
```

### VCF

```cpp
struct VCF {
    float cutoff = 1000.0f;
    float resonance = 0.5f;
    float cv_amount = 1.0f;

    void process(AudioContext& ctx) {
        const float* in = ctx.input(0);
        const float* cv = ctx.input(1);  // オプショナル
        float* out = ctx.output(0);
        if (!in || !out) return;

        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            float mod = cv ? cv[i] * cv_amount : 0.0f;
            float freq = cutoff * std::pow(2.0f, mod);
            out[i] = filter(in[i], freq);
        }
    }
};
```

---

## 6. MIDIエフェクター（アルペジエーター）

### UMIP

```cpp
struct Arpeggiator {
    float pattern = 0.0f;   // 0=up, 1=down, 2=updown, 3=random
    float octaves = 1.0f;

    void process(AudioContext& ctx) {
        // 入力イベント処理
        for (const auto& e : ctx.input_events) {
            if (e.port_id == 0 && e.midi.is_note_on()) {
                held_notes_.push_back(e.midi.note());
            } else if (e.port_id == 0 && e.midi.is_note_off()) {
                // ノート削除
            } else if (e.port_id == 1) {
                // クロック → ノート出力
                trigger_next_note(e.sample_pos, ctx.output_events);
            }
        }
    }

private:
    void trigger_next_note(uint32_t pos, EventQueue<>& out) {
        if (held_notes_.empty()) return;
        uint8_t note = get_next_note();
        out.push(Event::note_on(2, pos, 0, note, 100));
    }

    uint8_t get_next_note();
    std::vector<uint8_t> held_notes_;
    int step_ = 0;
};
```

---

## ユースケース比較

| 項目 | エフェクト | シンセ | モジュラー | MIDIエフェクト |
|------|-----------|--------|-----------|---------------|
| 入力 | Audio | MIDI | CV/Audio | MIDI |
| 出力 | Audio | Audio | CV/Audio | MIDI |
| UMIC | 不要 | オプション | 不要 | オプション |
| 複雑度 | 低 | 中〜高 | 低 | 中 |

---

## 共通パターン

### ヘッドレス（UMIC不要）

```cpp
struct Effect {
    float param = 1.0f;
    void process(AudioContext& ctx);
};

constexpr ParamMeta<Effect> params[] = {{&Effect::param, "Param", 0, 1, 1}};
UMIM_EXPORT(Effect, params);
```

### UI状態あり（UMIC必要）

```cpp
struct Processor { /* ... */ };

struct Controller {
    Processor& proc;
    int page = 0;
    explicit Controller(Processor& p) : proc(p) {}
    void on_encoder(int id, int delta);
};

UMIM_EXPORT_WITH_CONTROLLER(Processor, Controller, params);
```

---

## 7. プリセット管理

### 方式選択

| 方式 | 実装 | 用途 |
|------|------|------|
| ホスト管理（推奨） | UMIP のみ | DAW、汎用ホスト |
| モジュール内管理 | UMIP + UMIC | 専用ハードウェア |

### ホスト管理（推奨）

モジュールはパラメータを公開するだけ。プリセットはホストが管理。

```cpp
// モジュール側 - パラメータを公開するだけ
struct Synth {
    float cutoff = 1000.0f;
    float resonance = 0.5f;
    float attack = 0.01f;

    void process(AudioContext& ctx) { /* ... */ }
};

constexpr umi::ParamMeta<Synth> params[] = {
    {&Synth::cutoff,    "Cutoff",    20.0f, 20000.0f, 1000.0f},
    {&Synth::resonance, "Resonance", 0.0f,  1.0f,     0.5f},
    {&Synth::attack,    "Attack",    0.001f, 2.0f,    0.01f},
};

UMIM_EXPORT(Synth, params);
```

ホスト側で `umi_get/set_param()` を使ってプリセットを保存・復元。

```javascript
// JavaScript ホスト例
class PresetManager {
    constructor(plugin) {
        this.plugin = plugin;
        this.paramCount = plugin.exports.umi_get_param_count();
    }

    capture() {
        const values = [];
        for (let i = 0; i < this.paramCount; i++) {
            values.push(this.plugin.exports.umi_get_param(i));
        }
        return values;
    }

    apply(values) {
        for (let i = 0; i < values.length; i++) {
            this.plugin.exports.umi_set_param(i, values[i]);
        }
    }

    save(name) {
        return { name, values: this.capture() };
    }

    load(preset) {
        this.apply(preset.values);
    }
}
```

**メリット:**
- モジュールがシンプル（UMIC不要）
- ホストがUI/保存形式を統一可能
- ヘッドレス維持

### モジュール内管理

専用ハードウェアや独自プリセット形式が必要な場合。

```cpp
// UMIC でプリセット管理
struct SynthController {
    Synth& proc;
    int current_preset = 0;

    struct Preset {
        float cutoff, resonance, attack;
        const char* name;
    };

    static constexpr Preset presets[] = {
        {1000.0f, 0.5f, 0.01f, "Init"},
        {200.0f,  0.8f, 0.5f,  "Pad"},
        {4000.0f, 0.2f, 0.001f, "Lead"},
    };

    explicit SynthController(Synth& p) : proc(p) {
        load_preset(0);
    }

    void load_preset(int index) {
        if (index < 0 || index >= 3) return;
        current_preset = index;
        proc.cutoff = presets[index].cutoff;
        proc.resonance = presets[index].resonance;
        proc.attack = presets[index].attack;
    }

    // MIDI Program Change 対応
    void on_events(std::span<const Event> events) {
        for (const auto& e : events) {
            if (e.type == EventType::Midi &&
                e.midi.command() == MidiData::PROGRAM_CHANGE) {
                load_preset(e.midi.note() % 3);
            }
        }
    }

    // 状態保存
    size_t save_state(std::span<uint8_t> buffer) {
        if (buffer.size() < 1) return 0;
        buffer[0] = static_cast<uint8_t>(current_preset);
        return 1;
    }

    bool load_state(std::span<const uint8_t> data) {
        if (data.empty()) return false;
        load_preset(data[0]);
        return true;
    }
};
```

**用途:**
- 専用ハードウェアシンセ
- 独自プリセット形式
- Program Change でのプリセット切り替え

---

## ライセンス

CC0 1.0 Universal (パブリックドメイン)

---

**UMI-OS プロジェクト**

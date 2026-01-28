# Processor/Controller 分離設計

## 概要

Processorは信号処理（オーディオ + 入力イベント）に専念し、出力I/O（LED、ディスプレイ等）操作はControllerが担当する設計。

## 設計原則

### 入力と出力の分離

| 分類 | 例 | 処理場所 | 理由 |
|------|-----|---------|------|
| **入力イベント** | ボタン押下、ノブ変更、MIDI | Processor (ctx経由) | サンプル精度が必要な場合あり |
| **出力表示** | LED、ディスプレイ | Controller | 低レート更新で十分（~60Hz） |

### Processorの責務

| 処理 | 場所 | 理由 |
|------|------|------|
| オーディオ処理 | Processor | リアルタイム必須 |
| MIDIイベント | Processor | サンプル精度が必要 |
| パラメータ変更イベント | Processor | スムージング等 |
| ボタン押下イベント | Processor | トリガー処理 |

### Controllerの責務

| 処理 | 場所 | 理由 |
|------|------|------|
| LED更新 | Controller | 低レート更新で十分 |
| ディスプレイ更新 | Controller | 低レート更新で十分 |
| Processor状態の読み取り | Controller | UI反映 |

### プラグイン互換性

VST3/CLAP/AUv3 全てで、process() はオーディオ + イベントを処理する：

```cpp
// VST3
void process(ProcessData& data) {
    for (auto& event : data.inputParameterChanges) { ... }
    for (auto& event : data.inputEvents) { ... }  // MIDI
    for (int i = 0; i < data.numSamples; ++i) { ... }
}

// CLAP
void process(const clap_process_t* p) {
    for (auto& event : p->in_events) { ... }  // パラメータ + MIDI
    // オーディオ処理
}
```

よって `AudioContext` にイベントを含めるのは正しい。

---

## 現状 → 目標

### 現状

```cpp
void process(umi::AudioContext& ctx) {
    auto out = ctx.output(0);
    float val = umi::get_param(0);      // グローバル関数（問題）
    umi::syscall::led_set(0, true);     // process内でI/O（問題）
}
```

### 目標

```cpp
struct MyProcessor {
    float lfo_phase = 0.0f;
    float cutoff = 1000.0f;  // 内部状態として保持
    Filter filter;

    void process(umi::AudioContext& ctx) {
        // イベント処理（MIDI/パラメータ/ボタン）
        for (const auto& ev : ctx.input_events) {
            switch (ev.type) {
            case umi::EventType::Midi:
                handle_midi(ev);
                break;
            case umi::EventType::ParamChange:
                // パラメータ変更イベント（ノブ/スライダー）
                if (ev.param.id == PARAM_CUTOFF) {
                    cutoff = ev.param.value;
                }
                break;
            case umi::EventType::ButtonDown:
                // ボタン押下イベント
                gate = true;
                break;
            }
        }

        // オーディオ処理
        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            lfo_phase += ctx.dt * lfo_freq;
            if (lfo_phase >= 1.0f) lfo_phase -= 1.0f;

            ctx.output(0)[i] = filter.process(osc.next(), cutoff);
        }
        // LEDには触らない - Controllerの責務
    }
};

int main() {
    MyProcessor proc;
    auto* shared = umi::syscall::get_shared();

    umi::register_processor(proc);

    // Controller loop（~60Hz）
    while (true) {
        umi::syscall::wait_event_timeout(16000);  // 16ms

        // Processorの状態を読んでI/Oに反映
        shared->led_state.store(proc.lfo_phase < 0.5f ? 1 : 0);
    }
}
```

---

## AudioContext

`AudioContext` はオーディオバッファ + イベント + タイミング情報を含む：

```cpp
// lib/umios/core/audio_context.hh
struct AudioContext {
    // オーディオバッファ
    std::span<sample_t* const> outputs;
    std::span<const sample_t* const> inputs;

    // 入力イベント（MIDI/パラメータ変更/ボタン）
    std::span<const Event> input_events;

    // 出力イベント（MIDI out等）
    EventQueue<>& output_events;

    // タイミング
    uint32_t sample_rate;
    uint32_t buffer_size;
    float dt;
    sample_position_t sample_position;

    // ヘルパー
    sample_t* output(size_t ch) const;
    const sample_t* input(size_t ch) const;
    void clear_outputs();
    void passthrough();
};
```

**注意**: `get_param()` は削除。パラメータはイベントとして受け取る。

---

## process()シグネチャ

```cpp
void process(umi::AudioContext& ctx);
```

- `ctx`: オーディオバッファ + 入力イベント + タイミング

LED等の出力I/Oはprocess()では扱わない。Controllerが担当。

---

## イベントフロー

```
[Hardware/Host]
     |
     v
[Kernel/Controller] -- イベント生成 --> [Event Queue]
     |                                       |
     |                                       v
     |                              [AudioContext.input_events]
     |                                       |
     |                                       v
     |                              [Processor.process()]
     |                                       |
     |                                       v
     |                              [Processor内部状態更新]
     |                                       |
     v                                       |
[Controller] <-- 状態読み取り ---------------+
     |
     v
[LED/Display更新]
```

---

## LFO → LED の例

```cpp
struct MyProcessor {
    float lfo_phase = 0.0f;
    float lfo_freq = 1.0f;  // イベントで更新される

    void process(umi::AudioContext& ctx) {
        // パラメータ変更イベントを処理
        for (const auto& ev : ctx.input_events) {
            if (ev.type == umi::EventType::ParamChange &&
                ev.param.id == PARAM_LFO_FREQ) {
                lfo_freq = ev.param.value;
            }
        }

        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            lfo_phase += ctx.dt * lfo_freq;
            if (lfo_phase >= 1.0f) lfo_phase -= 1.0f;

            float lfo_val = std::sin(lfo_phase * 2.0f * PI);
            ctx.output(0)[i] = lfo_val;
        }
        // LEDには触らない - Processorの責務外
    }
};

int main() {
    MyProcessor proc;
    auto* shared = umi::syscall::get_shared();

    umi::register_processor(proc);

    // Controller loop（~60Hz）
    while (true) {
        umi::syscall::wait_event_timeout(16000);

        // Processorの状態を読んでLEDに反映
        bool led_on = proc.lfo_phase < 0.5f;
        shared->led_state.store(led_on ? 1 : 0, std::memory_order_relaxed);
    }
}
```

---

## スレッド安全性

### Processor状態へのアクセス

```cpp
struct MyProcessor {
    float lfo_phase;  // オーディオスレッドが書く、Controllerが読む
};
```

単純なfloatの読み書きは多くのプラットフォームでatomic。
厳密にはstd::atomicを使うか、バッファ境界でのみ読み取る。

```cpp
struct MyProcessor {
    std::atomic<float> lfo_phase_out{0.0f};  // Controller向けに公開
    float lfo_phase = 0.0f;                   // 内部処理用

    void process(AudioContext& ctx) {
        for (...) {
            lfo_phase += ...;
        }
        // バッファ末尾でatomicに公開
        lfo_phase_out.store(lfo_phase, std::memory_order_relaxed);
    }
};

// Controller
float phase = proc.lfo_phase_out.load(std::memory_order_relaxed);
```

---

## 変更点

### 削除するもの

| 項目 | 理由 |
|------|------|
| `umi::get_param()` | イベントで受け取る |
| `umi::set_param()` | Controller が shared memory 直接アクセス |
| `ctx.get_param()` | イベントで受け取る |
| process()内のLED/ボタン操作 | Controllerに移動 |

### 維持するもの

| 項目 | 理由 |
|------|------|
| `AudioContext` | プラグイン互換性、1構造体でバッファ+イベント |
| `ProcessorLike` concept | `process(AudioContext&)` |

---

## プラグイン互換性

### UMI-OS

```cpp
// Processorはイベントを処理
void process(AudioContext& ctx) {
    for (auto& ev : ctx.input_events) {
        if (ev.type == EventType::ParamChange) {
            params_[ev.param.id] = ev.param.value;
        }
    }
}

// Controllerが共有メモリを直接操作
shared->led_state.store(...);
```

### VST/AU

```cpp
// ProcessorはProcessData/clap_process_t経由でアクセス
void process(ProcessData& data) {
    for (auto& ev : data.inputParameterChanges) { ... }
}

// ControllerはホストAPIを呼ぶ
host->setParameterAutomated(PARAM_LED, value);
```

Processorコードは `AudioContext` ↔ `ProcessData` のアダプターで変換可能。
Controllerのみがプラットフォーム依存。

---

## 決定事項

| 項目 | 決定 |
|------|------|
| Processorの責務 | オーディオ + 入力イベント処理（MIDI/パラメータ/ボタン） |
| Controllerの責務 | 出力I/O（LED等）、Processor状態の反映 |
| process()シグネチャ | `void process(AudioContext&)` |
| パラメータアクセス | イベントとして受け取る（`ctx.get_param()`は削除） |
| グローバル関数 | 削除（`umi::get_param()` 等） |
| AudioBuffer/InputState | 不採用（AudioContextに統合） |

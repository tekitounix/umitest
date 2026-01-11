# UMI-OS API移行ガイド

## 概要

このドキュメントは、旧API（`core/*.hh`）から新API（`include/umi/*.hpp`）への移行ガイドです。

## 新旧API対応表

### ヘッダーファイル

| 旧ファイル | 新ファイル | 状態 |
|------------|------------|------|
| `core/umi_audio.hh` | `include/umi/processor.hpp` | 新規作成 |
| `core/umi_audio.hh` | `include/umi/audio_context.hpp` | 新規作成 |
| - | `include/umi/event.hpp` | 新規作成 |
| - | `include/umi/time.hpp` | 新規作成 |
| - | `include/umi/types.hpp` | 新規作成 |
| `core/umi_coro.hh` | `include/umi/coro.hpp` | 移動予定（Phase 2） |
| `core/umi_expected.hh` | `include/umi/error.hpp` | 拡張予定（Phase 2） |
| `core/umi_kernel.hh` | `core/kernel/*.hpp` | 分割予定（Phase 2） |

### Processor API

#### 旧API（AudioProcessor）

```cpp
// 旧: core/umi_audio.hh
template<typename HW>
class AudioProcessor {
    void process(float* output, size_t samples);
    void handle_midi(const MidiMessage& msg);
};
```

#### 新API（Concept-based設計）

継承不要。必要なメソッドを実装するだけ：

```cpp
// 最小実装（これだけでProcessorLikeを満たす）
class MySynth {
public:
    void process(AudioContext& ctx) {
        auto* out = ctx.output(0);
        for (size_t i = 0; i < ctx.buffer_size; ++i) {
            out[i] = /* generate audio */;
        }
    }
};

// オプション: control()があればControllableになる
class MyControllableSynth {
public:
    void process(AudioContext& ctx) { /* ... */ }
    void control(ControlContext& ctx) { /* パラメータ更新など */ }
};

// オプション: params()があればHasParamsになる
class MyParamSynth {
public:
    void process(AudioContext& ctx) { /* ... */ }
    std::span<const ParamDescriptor> params() const { return params_; }
private:
    static constexpr ParamDescriptor params_[] = {
        {0, "Volume", 0.8f, 0.0f, 1.0f},
        {1, "Cutoff", 1000.0f, 20.0f, 20000.0f},
    };
};
```

**使用方法:**

```cpp
// 組み込み（インライン化、vtable不要）
MySynth synth;
umi::process_once(synth, ctx);  // 直接呼び出し

// テスト/プラグイン（動的ディスパッチが必要な時）
umi::AnyProcessor any(synth);
any.process(ctx);  // 型消去経由
```

**設計原則:**
- 継承不要: 必要なメソッドがあればOK（ダックタイピング）
- 組み込み: 直接使用でvtable不要、完全インライン化
- テスト/プラグイン: `AnyProcessor`で動的ディスパッチ
- `-fno-rtti`対応: コンセプトはRTTI不要

### AudioContext

```cpp
struct AudioContext {
    span<const sample_t* const> inputs;   // 入力チャンネル
    span<sample_t* const> outputs;        // 出力チャンネル
    EventQueue& events;                    // サンプル精度イベント
    uint32_t sample_rate;
    uint32_t buffer_size;
    sample_position_t sample_position;     // 累積サンプル位置
};
```

### EventQueue

| 旧 | 新 |
|----|-----|
| `handle_midi()` callback | `ctx.events.pop()` でポーリング |
| サンプル精度なし | `sample_pos` でサンプル精度 |
| MIDI専用 | MIDI + Param + Raw対応 |

### 時間管理

| 操作 | 旧 | 新 |
|------|-----|-----|
| ms→samples | 手動計算 | `umi::time::ms_to_samples()` |
| samples→ms | 手動計算 | `umi::time::samples_to_ms()` |
| 位置追跡 | なし | `ctx.sample_position` |
| BPM計算 | なし | `umi::time::bpm_to_samples_per_beat()` |

## 移行手順

### 1. インクルードパスの追加

```lua
-- xmake.lua
add_includedirs("include", {public = true})
```

### 2. Processorクラスの継承

```cpp
// 旧
class MySynth : public AudioProcessor<HW> { ... };

// 新
class MySynth : public umi::Processor { ... };
```

### 3. process()の更新

```cpp
// 旧
void process(float* output, size_t samples) {
    for (size_t i = 0; i < samples; ++i) {
        output[i] = generate_sample();
    }
}

// 新
void process(umi::AudioContext& ctx) override {
    auto* out = ctx.output(0);
    for (size_t i = 0; i < ctx.buffer_size; ++i) {
        // イベント処理
        umi::Event e;
        while (ctx.events.pop_until(i, e)) {
            handle_event(e);
        }
        out[i] = generate_sample();
    }
}
```

### 4. MIDIハンドリングの更新

```cpp
// 旧
void handle_midi(const MidiMessage& msg) {
    if (msg.is_note_on()) { ... }
}

// 新
void process(umi::AudioContext& ctx) override {
    umi::Event e;
    while (ctx.events.pop(e)) {
        if (e.type == umi::EventType::Midi) {
            if (e.midi.is_note_on()) { ... }
        }
    }
}
```

## 維持される機能

以下の機能は変更なしで維持されます：

- `core/umi_coro.hh` - コルーチン実装
- `core/umi_monitor.hh` - モニタリング
- `core/umi_shell.hh` - デバッグシェル
- `port/*` - プラットフォーム抽象化レイヤー

## テスト

```bash
# 新APIテスト
xmake build test_processor
xmake run test_processor

# 既存テスト（互換性確認）
xmake test
```

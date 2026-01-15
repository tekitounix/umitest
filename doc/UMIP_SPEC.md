# UMI-Processor (UMIP) 仕様書

**バージョン:** 2.0.0-draft
**ステータス:** ドラフト

## 概要

UMI-Processor (UMIP) は、ヘッドレスなオーディオ処理ユニットの仕様です。

- **言語非依存** - C/C++, Rust, Zig等で実装可能
- **ヘッドレス** - UI状態を持たない純粋DSP
- **最小インターフェース** - `process()` のみ必須

## ヘッドレスとは

**UI状態（モード）を持たない**こと。

| 構成 | ヘッドレス | 備考 |
|------|----------|------|
| DSP処理のみ | ✓ | |
| ノブあり、1ノブ1パラメータ | ✓ | 状態なし（モードレス） |
| ページ切替、パラメータ選択 | ✗ | UI状態あり → UMIC必要 |
| MIDI Learn（モジュール内管理） | ✗ | 状態あり → UMIC必要 |
| MIDI Learn（ホスト/カーネル管理） | ✓ | モジュールは関与しない |

ホスト/カーネルがUI・プリセット・MIDI Learnを管理する場合、モジュールはヘッドレスでよい。

## MVCにおける位置づけ

UMIPは**Model**に相当。

```
┌─────────────────────────────────────────────────────────┐
│  アプリケーション層                                       │
│  ┌─────────────────┐  ┌─────────────────┐               │
│  │  UMIP (Model)   │←─│  UMIC (Ctrl)    │ ← オプション   │
│  │  純粋DSP        │  │  UIロジック      │               │
│  └─────────────────┘  └─────────────────┘               │
└─────────────────────────────────────────────────────────┘
                         │
           ┌─────────────┼─────────────┐
           ▼             ▼             ▼
       ┌───────┐    ┌───────┐    ┌───────┐
       │ UMIM  │    │ 組込み │    │ VST3  │
       │ WASM  │    │       │    │ CLAP  │
       └───────┘    └───────┘    └───────┘
```

## Processorインターフェース

### 必須

```cpp
struct Processor {
    void process(AudioContext& ctx);
};
```

### オプション

```cpp
struct Processor {
    // 初期化（サンプルレート依存の準備が必要な場合）
    void init(float sample_rate);

    // 状態保存（パラメータ以外の内部状態がある場合）
    size_t save_state(std::span<uint8_t> buffer);
    bool load_state(std::span<const uint8_t> data);
};
```

`init()` は `umi_create()` 時にアダプタから呼ばれる。バッファ事前確保等に使用。

### パラメータ

メンバ変数として定義。外部から直接アクセス。

```cpp
struct Volume {
    float volume = 1.0f;  // パラメータ

    void process(AudioContext& ctx) {
        // volume を直接使用
    }
};
```

`set_param()`/`get_param()` は不要。アダプタがメンバポインタ経由でアクセス。

## AudioContext

```cpp
struct AudioContext {
    std::span<const float* const> inputs;   // 入力バッファ
    std::span<float* const> outputs;        // 出力バッファ
    std::span<const Event> input_events;    // 入力イベント
    EventQueue<>& output_events;            // 出力イベント

    uint32_t sample_rate;
    uint32_t buffer_size;
    float dt;                               // 1.0 / sample_rate
    uint64_t sample_position;

    const float* input(size_t ch) const;    // nullptr if unavailable
    float* output(size_t ch) const;         // nullptr if unavailable
};
```

### 未接続ポートの扱い

ポートが未接続の場合、`input()`/`output()` は `nullptr` を返す。

```cpp
void process(AudioContext& ctx) {
    const float* cv = ctx.input(1);
    float mod = cv ? cv[i] : 0.0f;  // 未接続なら0
}
```

## ポート

入出力の接続点。

```cpp
struct PortDescriptor {
    port_id_t id;
    const char* name;
    PortKind kind;       // Continuous / Event
    PortDirection dir;   // In / Out
    uint32_t channels;   // Continuousのみ
    TypeHint type_hint;  // Eventのみ
};

enum class PortKind : uint8_t {
    Continuous,  // オーディオレート（Audio, CV）
    Event,       // 非同期（MIDI, Trigger）
};

enum class PortDirection : uint8_t {
    In,
    Out,
};

enum class TypeHint : uint8_t {
    None,
    MidiBytes,      // 標準MIDI
    ParamChange,    // パラメータ変更
    Trigger,        // ゲート/トリガー
};
```

### ポート構成例

```cpp
// シンセサイザー
constexpr PortDescriptor synth_ports[] = {
    {0, "midi_in",   Event,      In},
    {1, "audio_out", Continuous, Out},
};

// フィルター（CV変調対応）
constexpr PortDescriptor filter_ports[] = {
    {0, "audio_in",  Continuous, In},
    {1, "cutoff_cv", Continuous, In},   // オプショナル
    {2, "audio_out", Continuous, Out},
};
```

## パラメータメタデータ

Processor外部で静的に定義。テンプレートでProcessor型を指定。

```cpp
template<typename P>
struct ParamMeta {
    float P::* ptr;           // メンバポインタ
    const char* name;
    float min;
    float max;
    float default_value;
};

// 使用例
constexpr ParamMeta<Volume> volume_params[] = {
    {&Volume::volume, "Volume", 0.0f, 1.0f, 1.0f},
};

// 複数パラメータ
constexpr ParamMeta<Synth> synth_params[] = {
    {&Synth::cutoff,    "Cutoff",    20.0f, 20000.0f, 1000.0f},
    {&Synth::resonance, "Resonance", 0.0f,  1.0f,     0.5f},
};
```

アダプタはメンバポインタ経由で `processor.*ptr` としてアクセス。

## イベント

```cpp
struct Event {
    port_id_t port_id;
    uint32_t sample_pos;   // バッファ内位置 (0 ~ buffer_size-1)
    EventType type;

    union {
        MidiData midi;
        RawData raw;
    };
};
```

### sample_pos

| 用途 | 意味 |
|------|------|
| 入力イベント | イベント発生位置 |
| 出力イベント | イベント送信位置 |

- `input_events` は `sample_pos` 昇順でソート済み
- タイムスタンプ変換はアダプタの責務

## 実装例

### 最小（エフェクト）

```cpp
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

### イベント処理（シンセ）

```cpp
struct Synth {
    float cutoff = 1000.0f;

    void process(AudioContext& ctx) {
        // イベント処理
        for (const auto& e : ctx.input_events) {
            if (e.type == EventType::Midi) {
                if (e.midi.is_note_on()) note_on(e.midi);
                else if (e.midi.is_note_off()) note_off(e.midi);
            }
        }

        // オーディオ生成
        float* out = ctx.output(0);
        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            out[i] = generate();
        }
    }
};
```

### CV変調（フィルター）

```cpp
struct Filter {
    float cutoff = 1000.0f;  // ベース値（パラメータ）

    void process(AudioContext& ctx) {
        const float* in = ctx.input(0);
        const float* cv = ctx.input(1);  // CV入力（オプショナル）
        float* out = ctx.output(0);

        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            float freq = cutoff;
            if (cv) freq += cv[i] * 1000.0f;  // CV変調
            out[i] = filter(in[i], freq);
        }
    }
};
```

## リアルタイム制約

`process()` 内での禁止事項:

- メモリ確保（malloc, new）
- ファイルI/O
- ロック取得（mutex）
- 例外送出

## ライセンス

CC0 1.0 Universal (パブリックドメイン)

---

**UMI-OS プロジェクト**

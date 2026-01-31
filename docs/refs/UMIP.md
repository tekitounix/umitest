# UMI-Processor (UMIP) 仕様書

**バージョン:** 3.0.0-draft
**ステータス:** ドラフト

## 概要

UMI-Processor (UMIP) は、ヘッドレスなオーディオ処理ユニットの仕様です。

- **言語非依存** - C/C++, Rust, Zig等で実装可能
- **ヘッドレス** - UI状態を持たない純粋DSP
- **最小インターフェース** - `process()` のみ必須
- **統一API** - 組込み/Web/デスクトップで同一コード

## アプリケーション構造

UMIアプリケーションは**2つのタスク**で構成される:

```
┌─────────────────────────────────────────────────────────────┐
│                     Application                             │
├─────────────────────────────────────────────────────────────┤
│  ┌───────────────────────────────────────┐                 │
│  │         Processor (UMIP)              │ ← DSP処理       │
│  │  process(ctx) - カーネルから呼び出し   │                 │
│  └───────────────────────────────────────┘                 │
│                                                             │
│  ┌───────────────────────────────────────┐                 │
│  │         Control Task (main)           │ ← UI/イベント   │
│  │  wait_event() でブロッキング待ち       │                 │
│  │  コルーチン対応                        │                 │
│  └───────────────────────────────────────┘                 │
└─────────────────────────────────────────────────────────────┘
```

| タスク | 役割 | 呼び出し元 |
|--------|------|-----------|
| **Processor** | DSP処理（リアルタイム） | カーネル/ホスト |
| **Control** | UI、イベント処理 | main()（ユーザーコード） |

## 統一main()パターン

**組込み/Web/デスクトップで同一のアプリコード**:

```cpp
// my_app/main.cc - 全プラットフォーム共通

#include <umi/app.hh>
#include "my_synth.hh"

int main() {
    // 1. Processor を登録
    static MySynth synth;
    umi::register_processor(synth);
    
    // 2. Control Task として動作
    while (true) {
        umi::Event ev = umi::wait_event();
        
        if (ev.type == umi::EventType::Shutdown) break;
        
        switch (ev.type) {
        case umi::EventType::Midi:
            synth.handle_midi(ev.midi);
            break;
        case umi::EventType::ParamChange:
            synth.set_param(ev.param.id, ev.param.value);
            break;
        }
    }
    
    return 0;
}
```

### プラットフォーム別実行

| プラットフォーム | エントリ | Processor呼び出し | イベント待ち |
|------------------|----------|-------------------|-------------|
| **組込み** | `_start` → `main()` | カーネルタスク | syscall |
| **Web (WASM)** | `umi_init` → `main()` | AudioWorklet | Asyncify |
| **デスクトップ** | `main()` | オーディオスレッド | ホストAPI |

## 設計思想

### 組み込み基準設計

UMIは**組み込み（低リソース環境）を基準**に設計されています。

```
組み込み（STM32等） ⊂ デスクトップ ⊂ ブラウザ
```

最も制約の厳しい環境で動作するコードは、それ以上の環境すべてで動作します。

### ヘッドレス

**UIロジック/インタラクション処理を持たない**こと。

- DSP処理のみに専念
- パラメータはメンバ変数として公開
- UI状態管理はホスト/カーネルに委譲

| 構成 | ヘッドレス | 備考 |
|------|----------|------|
| DSP処理のみ | ✓ | |
| ノブあり、1ノブ1パラメータ | ✓ | モードレス＝ヘッドレス |
| ページ切替、パラメータ選択 | ✗ | UI状態あり → UMIC必要 |

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
    // 状態保存（パラメータ以外の内部状態がある場合）
    size_t save_state(std::span<uint8_t> buffer);
    bool load_state(std::span<const uint8_t> data);
};
```

サンプルレート依存の初期化は `process()` 内で行う（`ctx.sample_rate` を使用）。

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
    // バッファアクセス（std::span）
    std::span<const sample_t* const> inputs;
    std::span<sample_t* const> outputs;

    // イベント
    std::span<const Event> input_events;
    EventQueue<>& output_events;

    // タイミング
    uint32_t sample_rate;
    uint32_t buffer_size;
    float dt;                               // 1.0 / sample_rate（事前計算）
    uint64_t sample_position;               // 累積サンプル位置

    // ヘルパー関数
    const sample_t* input(size_t ch) const {
        return ch < inputs.size() ? inputs[ch] : nullptr;
    }
    sample_t* output(size_t ch) const {
        return ch < outputs.size() ? outputs[ch] : nullptr;
    }
};
```

`dt` は組み込み環境での除算コスト削減のため事前計算。
`sample_position` はDAWとの同期やLFO位相管理に使用。

### なぜstd::spanか

| 観点 | ポインタ配列 `T**` | `std::span<T*>` |
|------|-------------------|-----------------|
| サイズ情報 | 別フィールド必要 | 内包 |
| 境界チェック | なし | `.at()` で可能 |
| Range-for | 不可 | 可能 |
| オーバーヘッド | なし | なし（ゼロコスト） |

UMIPはC++仕様のため、型安全な`std::span`を採用。
C ABI互換が必要なUMIMでは、アダプタがポインタ配列に変換する。

### 未接続ポートの扱い

ポートが未接続の場合、`input()`/`output()` は `nullptr` を返す。

```cpp
void process(AudioContext& ctx) {
    const float* cv = ctx.input(1);
    for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
        float mod = cv ? cv[i] : 0.0f;  // 未接続なら0
        // ...
    }
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
    {0, "midi_in",   PortKind::Event,      PortDirection::In,  0, TypeHint::MidiBytes},
    {1, "audio_out", PortKind::Continuous, PortDirection::Out, 1, TypeHint::None},
};

// フィルター（CV変調対応）
constexpr PortDescriptor filter_ports[] = {
    {0, "audio_in",  PortKind::Continuous, PortDirection::In,  1, TypeHint::None},
    {1, "cutoff_cv", PortKind::Continuous, PortDirection::In,  1, TypeHint::None},  // オプショナル
    {2, "audio_out", PortKind::Continuous, PortDirection::Out, 1, TypeHint::None},
};
```

## パラメータメタデータ

Processor外部で静的に定義。テンプレートでProcessor型を指定。

```cpp
/// パラメータのスケーリングカーブ
enum class ParamCurve : uint8_t {
    Linear,     // 線形（デフォルト）
    Log,        // 対数（周波数向け）
    Exp,        // 指数（音量向け）
};

template<typename P>
struct ParamMeta {
    uint16_t id;              // 永続ID（プリセット/オートメーション互換用）
    float P::* ptr;           // メンバポインタ
    const char* name;
    float min;
    float max;
    float default_value;
    ParamCurve curve = ParamCurve::Linear;  // スケーリング
    const char* unit = "";                   // 単位 ("Hz", "dB", "%", "ms")
};

// 使用例
constexpr ParamMeta<Volume> volume_params[] = {
    {0, &Volume::volume, "Volume", 0.0f, 1.0f, 1.0f, ParamCurve::Linear, ""},
};

// 複数パラメータ
constexpr ParamMeta<Synth> synth_params[] = {
    {0, &Synth::cutoff,    "Cutoff",    20.0f, 20000.0f, 1000.0f, ParamCurve::Log, "Hz"},
    {1, &Synth::resonance, "Resonance", 0.0f,  1.0f,     0.5f,    ParamCurve::Linear, ""},
};
```

| フィールド | 説明 |
|-----------|------|
| `id` | 永続ID（プリセット・オートメーション互換用、配列順序と独立） |
| `ptr` | メンバポインタ（直接アクセス用） |
| `name` | パラメータ名 |
| `min` / `max` | 値域 |
| `default_value` | デフォルト値 |
| `curve` | UIスケーリング（Linear/Log/Exp） |
| `unit` | 単位（表示用） |

### idフィールドの重要性

`id` は以下の理由で必須:

1. **プリセット互換性**: パラメータの配列順序が変わっても、IDで正しく復元
2. **オートメーション**: DAWは id でオートメーションを永続化
3. **VST3/CLAP対応**: これらのフォーマットは明示的なパラメータIDを要求

アダプタはメンバポインタ経由で `processor.*ptr` としてアクセス。

## イベント

### 型定義

```cpp
using port_id_t = uint8_t;

enum class EventType : uint8_t {
    Midi,
    Raw,
};

struct MidiData {
    uint8_t bytes[3];
    uint8_t size;

    bool is_note_on() const;
    bool is_note_off() const;
    bool is_cc() const;
    uint8_t note() const;
    uint8_t velocity() const;
    uint8_t cc_number() const;
    uint8_t cc_value() const;
};

struct RawData {
    const uint8_t* data;
    uint16_t size;
};
```

### Event構造体

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
        float* out = ctx.output(0);
        if (!out) return;

        // イベント処理
        for (const auto& e : ctx.input_events) {
            if (e.type == EventType::Midi) {
                if (e.midi.is_note_on()) note_on(e.midi);
                else if (e.midi.is_note_off()) note_off(e.midi);
            }
        }

        // オーディオ生成
        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            out[i] = generate();
        }
    }

private:
    void note_on(const MidiData& m);
    void note_off(const MidiData& m);
    float generate();
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
        if (!in || !out) return;

        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            float freq = cutoff;
            if (cv) freq += cv[i] * 1000.0f;  // CV変調
            out[i] = apply_filter(in[i], freq);
        }
    }

private:
    float apply_filter(float sample, float freq);
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

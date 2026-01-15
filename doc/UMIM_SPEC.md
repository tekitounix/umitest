# UMI-Module (UMIM) 仕様書

**バージョン:** 2.0.0-draft
**拡張子:** `.umim`
**ステータス:** ドラフト

## 概要

UMI-Module (`.umim`) は、WebAssemblyベースのオーディオプラグインフォーマットです。

- **クロスプラットフォーム** - ブラウザ、Node.js、ネイティブ、組み込み
- **サンドボックス** - メモリ安全な隔離実行
- **シンプルAPI** - 約10関数

## UMIP / UMIC / UMIM の関係

| 仕様 | MVC | 役割 |
|------|-----|------|
| **UMIP** | Model | DSP処理（必須） |
| **UMIC** | Controller | UIロジック（オプション） |
| **UMIM** | ABI | WASMバイナリ形式 |

```
┌────────────────────────────────────┐
│  UMIP (Model)  +  UMIC (Ctrl)?    │
│          ↓  ビルド  ↓              │
│        UMIM (.umim)               │
└────────────────────────────────────┘
            │
    ┌───────┼───────┐
    ▼       ▼       ▼
 Browser  Native  Embedded
```

## コアAPI

### ライフサイクル

```c
void umi_create(float sample_rate);
void umi_destroy(void);  // オプション
```

`umi_create()` は Processor の `init()` メソッドを呼ぶ（存在する場合）。

### オーディオ処理

```c
void umi_process(const float* input, float* output, uint32_t frames);
```

### パラメータ

```c
uint32_t umi_get_param_count(void);
void umi_set_param(uint32_t index, float value);
float umi_get_param(uint32_t index);

// メタデータ（オプション）
const char* umi_get_param_name(uint32_t index);
float umi_get_param_min(uint32_t index);
float umi_get_param_max(uint32_t index);
float umi_get_param_default(uint32_t index);
```

### イベント

```c
void umi_send_event(uint32_t port, const uint8_t* data, uint8_t size, uint32_t sample_offset);
```

## 実装例

### 最小 UMIM（UMIC無し）

```cpp
// volume.cc
#include <umi/umim.hh>

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

constexpr umi::ParamMeta<Volume> params[] = {
    {&Volume::volume, "Volume", 0.0f, 1.0f, 1.0f},
};

UMIM_EXPORT(Volume, params);
```

マクロがメンバポインタ経由で `umi_set_param()` / `umi_get_param()` を自動生成。

### UMIC付き UMIM

```cpp
// synth_umim.cc
#include "synth.hh"            // UMIP
#include "synth_controller.hh"  // UMIC
#include <umi/umim.hh>

constexpr umi::ParamMeta<Synth> params[] = {
    {&Synth::cutoff, "Cutoff", 20.0f, 20000.0f, 1000.0f},
    {&Synth::resonance, "Resonance", 0.0f, 1.0f, 0.5f},
};

UMIM_EXPORT_WITH_CONTROLLER(Synth, SynthController, params);
```

## エクスポートマクロ

### UMIM_EXPORT(Processor, params)

UMICなしの場合。Processorとパラメータ配列からWASMエクスポートを生成。

```cpp
// 生成されるエクスポート:
// - umi_create(float sr)
// - umi_process(const float* in, float* out, uint32_t frames)
// - umi_set_param(uint32_t i, float v)  ← params[i].ptr経由でメンバアクセス
// - umi_get_param(uint32_t i)
// - umi_get_param_count()
// - umi_get_param_name(uint32_t i)
// - umi_get_param_min/max/default(uint32_t i)
```

### UMIM_EXPORT_WITH_CONTROLLER(Processor, Controller, params)

UMIC付きの場合。Controllerのイベント処理が追加される。

```cpp
// 追加で生成:
// - イベントをController→Processorの順で処理
// - Controller::on_events() が呼ばれる（あれば）
```

### 手動エクスポート

マクロを使わない場合の実装例。

```cpp
static Volume g_proc;
static float g_sample_rate = 48000.0f;

extern "C" {

void umi_create(float sr) {
    g_sample_rate = sr;
    // Processor::init() があれば呼ぶ
}

void umi_process(const float* in, float* out, uint32_t frames) {
    const float* inputs[] = {in};
    float* outputs[] = {out};
    EventQueue<> events;  // 空のイベントキュー

    AudioContext ctx{
        .inputs = {inputs, 1},
        .outputs = {outputs, 1},
        .input_events = {},
        .output_events = events,
        .sample_rate = static_cast<uint32_t>(g_sample_rate),
        .buffer_size = frames,
        .dt = 1.0f / g_sample_rate,
        .sample_position = 0,
    };
    g_proc.process(ctx);
}

void umi_set_param(uint32_t id, float v) {
    if (id == 0) g_proc.volume = v;
}

float umi_get_param(uint32_t id) {
    return (id == 0) ? g_proc.volume : 0.0f;
}

uint32_t umi_get_param_count() { return 1; }

}
```

## 必須エクスポート

| 関数 | 説明 |
|------|------|
| `umi_create` | 初期化 |
| `umi_process` | オーディオ処理 |
| `umi_get_param_count` | パラメータ数 |
| `umi_set_param` | パラメータ設定 |
| `umi_get_param` | パラメータ取得 |
| `malloc` / `free` | メモリ管理 |

## オプションエクスポート

| 関数 | 説明 |
|------|------|
| `umi_destroy` | 破棄 |
| `umi_get_param_name` | パラメータ名 |
| `umi_get_param_min/max/default` | パラメータ範囲 |
| `umi_send_event` | イベント送信 |
| `umi_get_name` | プラグイン名 |
| `umi_get_state_size` / `umi_get_state` / `umi_set_state` | 状態保存 |

## ホスト実装

### JavaScript (Web Audio)

```javascript
const response = await fetch('volume.umim');
const module = await WebAssembly.instantiate(await response.arrayBuffer());
const plugin = module.instance.exports;

plugin.umi_create(audioContext.sampleRate);

// AudioWorkletProcessor内
process(inputs, outputs) {
    plugin.umi_process(inputPtr, outputPtr, 128);
    return true;
}
```

### ネイティブ

```cpp
auto module = wasm_runtime_load("volume.umim");
auto instance = wasm_runtime_instantiate(module);

auto umi_create = wasm_lookup(instance, "umi_create");
auto umi_process = wasm_lookup(instance, "umi_process");

wasm_call(umi_create, sample_rate);
wasm_call(umi_process, input, output, frames);
```

## ビルド

```bash
# xmake
xmake build volume_umim

# 直接
emcc volume.cc -o volume.wasm \
    -sEXPORTED_FUNCTIONS="['_umi_create','_umi_process',...]" \
    -fno-exceptions -O3

mv volume.wasm volume.umim
```

## ケイパビリティベースI/O

モジュールはポートを宣言し、ホストが可能な範囲で接続。

```cpp
// モジュールが宣言（PortDescriptor は UMIP_SPEC 参照）
constexpr PortDescriptor ports[] = {
    {0, "audio_in",  Continuous, In},
    {1, "audio_out", Continuous, Out},
    {2, "cv_in",     Continuous, In},   // オプショナル
};

// モジュール側で未接続を許容
void process(AudioContext& ctx) {
    const float* cv = ctx.input(2);
    float mod = cv ? cv[i] : 0.0f;  // 未接続なら0
}
```

## リアルタイム制約

`umi_process()` 内での禁止事項:

- メモリ確保
- ファイルI/O
- ロック取得
- 例外送出

## ライセンス

CC0 1.0 Universal (パブリックドメイン)

---

**UMI-OS プロジェクト**

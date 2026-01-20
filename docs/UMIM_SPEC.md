# UMI-Module (UMIM) 仕様書

**バージョン:** 3.0.0-draft
**拡張子:** `.umim` (Web), `.umiapp` (組込み)
**ステータス:** ドラフト

## 概要

UMI-Module は、UMIアプリケーションのバイナリ配布形式です。

| 形式 | 拡張子 | 対象環境 | 実行方式 |
|------|--------|----------|----------|
| **WASM** | `.umim` | Web、Node.js | AudioWorklet |
| **Native** | `.umiapp` | 組込み | カーネルロード |

**アプリコードは共通**。ビルドターゲットでバイナリ形式が変わる。

## 統一アプリケーションモデル

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Code (共通)                  │
│                                                             │
│   int main() {                                              │
│       umi::register_processor(synth);                       │
│       while (umi::wait_event()) { ... }                     │
│   }                                                         │
├───────────────────────┬─────────────────────────────────────┤
│   Native (.umiapp)    │           WASM (.umim)              │
│   syscall ABI         │           WASM imports              │
│   Kernel + MPU        │           AudioWorklet + Asyncify   │
└───────────────────────┴─────────────────────────────────────┘
```

## UMIP / UMIC / UMIM の関係

| 仕様 | 役割 | 組込み | Web |
|------|------|--------|-----|
| **UMIP** | DSP処理（Processor） | ネイティブ | WASM |
| **UMIC** | UIロジック（Controller） | ネイティブ | WASM |
| **UMIM** | バイナリ形式 | ELF (.umiapp) | WASM (.umim) |

## Web版 (.umim)

### WASM エクスポート

```c
// ライフサイクル
void umi_init(void);      // main() を呼び出す
void umi_destroy(void);   // オプション

// オーディオ処理
void umi_process(float* input, float* output, uint32_t frames);

// イベント
void umi_push_event(uint32_t type, const uint8_t* data, uint32_t size);

// パラメータ
uint32_t umi_get_param_count(void);
void umi_set_param(uint32_t index, float value);
float umi_get_param(uint32_t index);
```

### AudioWorklet統合

```typescript
// umi-worklet.ts
class UmiProcessor extends AudioWorkletProcessor {
    private wasm: WebAssembly.Instance;
    
    async init(wasmBytes: ArrayBuffer) {
        this.wasm = await WebAssembly.instantiate(wasmBytes);
        (this.wasm.exports.umi_init as Function)();
    }
    
    process(inputs: Float32Array[][], outputs: Float32Array[][]): boolean {
        const input = inputs[0]?.[0] ?? new Float32Array(128);
        const output = outputs[0][0];
        
        (this.wasm.exports.umi_process as Function)(
            this.inputPtr, this.outputPtr, output.length
        );
        
        return true;
    }
}
```

## 組込み版 (.umiapp)

### バイナリヘッダ

```cpp
struct AppHeader {
    uint32_t magic;         // 0x414D4955 ("UMIA")
    uint32_t version;       // ABI バージョン
    uint32_t entry_offset;  // _start のオフセット
    uint32_t text_size;     // コードサイズ
    uint32_t data_size;     // 初期化データサイズ
    uint32_t bss_size;      // 未初期化データサイズ
    uint32_t stack_size;    // スタックサイズ
    uint32_t crc32;         // CRC検証
    uint8_t signature[64];  // Ed25519署名（製品アプリのみ）
};
```

### カーネルロード

```cpp
TaskId load_app(const uint8_t* image, size_t size) {
    auto* header = reinterpret_cast<const AppHeader*>(image);
    
    // 1. 検証
    if (!validate_header(header)) return {};
    
    // 2. メモリ確保・MPU設定
    setup_app_memory(header);
    
    // 3. Control Task として起動
    return kernel.create_task({
        .entry = app_entry,
        .prio = Priority::User,
    });
}
```

## 実装例

### 共通アプリコード

```cpp
// my_synth/main.cc - 組込み/Web共通

#include <umi/app.hh>
#include "synth.hh"

int main() {
    static Synth synth;
    umi::register_processor(synth);
    
    while (true) {
        auto ev = umi::wait_event();
        if (ev.type == umi::EventType::Shutdown) break;
        synth.handle_event(ev);
    }
    
    return 0;
}
```

### Processor

```cpp
// my_synth/synth.hh

class Synth {
public:
    void process(umi::ProcessContext& ctx) {
        for (const auto& ev : ctx.events()) {
            if (ev.is_note_on()) note_on(ev);
        }
        
        auto* out = ctx.output(0);
        for (uint32_t i = 0; i < ctx.frames(); ++i) {
            out[i] = generate();
        }
    }
    
    void handle_event(const umi::Event& ev) {
        if (ev.type == umi::EventType::ParamChange) {
            set_param(ev.param.id, ev.param.value);
        }
    }

private:
    // ...
};
```

## ビルド

```lua
-- xmake.lua

-- 共通コード
target("my_synth_common")
    set_kind("object")
    add_files("my_synth/*.cc")

-- 組込み版
target("my_synth_app")
    set_kind("binary")
    add_deps("my_synth_common", "umi_app_embedded")
    set_extension(".umiapp")
    set_toolchains("arm-none-eabi")

-- Web版
target("my_synth_wasm")
    set_kind("binary")
    add_deps("my_synth_common", "umi_app_wasm")
    set_extension(".umim")
    set_toolchains("emscripten")
    add_ldflags("-sASYNCIFY")
```

## リアルタイム制約

`process()` 内での禁止事項:

- メモリ確保（malloc, new）
- ファイルI/O
- ロック取得（mutex）
- 例外送出

## ライセンス

CC0 1.0 Universal (パブリックドメイン)
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
// - umi_create(void)
// - umi_process(const float* in, float* out, uint32_t frames, uint32_t sr)
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
static uint64_t g_sample_pos = 0;

extern "C" {

void umi_create(void) {
    // デフォルト初期化済み
}

void umi_process(const float* in, float* out, uint32_t frames, uint32_t sample_rate) {
    const float* inputs[] = {in};
    float* outputs[] = {out};
    EventQueue<> events;

    AudioContext ctx{
        .inputs = {inputs, 1},
        .outputs = {outputs, 1},
        .input_events = {},
        .output_events = events,
        .sample_rate = sample_rate,
        .buffer_size = frames,
        .dt = 1.0f / sample_rate,
        .sample_position = g_sample_pos,
    };
    g_proc.process(ctx);
    g_sample_pos += frames;
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

※ `malloc`/`free` はホストがWASMメモリを管理する場合に必要。モジュール内部での動的確保は`umi_process()`外で行うこと。

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

plugin.umi_create();

// AudioWorkletProcessor内
process(inputs, outputs) {
    plugin.umi_process(inputPtr, outputPtr, 128, sampleRate);
    return true;
}
```

### ネイティブ

```cpp
auto module = wasm_runtime_load("volume.umim");
auto instance = wasm_runtime_instantiate(module);

auto umi_create = wasm_lookup(instance, "umi_create");
auto umi_process = wasm_lookup(instance, "umi_process");

wasm_call(umi_create);
wasm_call(umi_process, input, output, frames, sample_rate);
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
    {0, "audio_in",  PortKind::Continuous, PortDirection::In,  1, TypeHint::None},
    {1, "audio_out", PortKind::Continuous, PortDirection::Out, 1, TypeHint::None},
    {2, "cv_in",     PortKind::Continuous, PortDirection::In,  1, TypeHint::None},  // オプショナル
};

// モジュール側で未接続を許容
void process(AudioContext& ctx) {
    const float* cv = ctx.input(2);
    for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
        float mod = cv ? cv[i] : 0.0f;  // 未接続なら0
        // ...
    }
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

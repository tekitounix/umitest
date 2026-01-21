# UMIM-Native: 組み込み向けDSPモジュール仕様

**バージョン:** 1.0.0-draft
**ステータス:** ドラフト
**拡張子:** `.umim`

## 概要

UMIM-Nativeは、組み込み環境でアプリケーションが動的にロード可能なDSPモジュールの仕様です。

### 背景

現在のUMI仕様:

| 形式 | 用途 | 実行主体 |
|------|------|----------|
| `.umim` (WASM) | Web向けDSPモジュール | AudioWorklet |
| `.umiapp` | 組み込みアプリケーション | カーネル |

組み込み環境では「アプリケーション」単位でしか動作できず、アプリ内でDSPモジュールを動的にロードする仕組みがありませんでした。

### 本仕様の目的

```
┌─────────────────────────────────────────────────────────────┐
│                        カーネル                               │
├─────────────────────────────────────────────────────────────┤
│               アプリケーション (.umiapp)                       │
│                                                               │
│   ┌──────────┐  ┌──────────┐  ┌──────────┐                  │
│   │  Module  │  │  Module  │  │  Module  │  ← 動的ロード     │
│   │  .umim   │  │  .umim   │  │  .umim   │                   │
│   │ (Synth)  │  │ (Effect) │  │ (Filter) │                   │
│   └────┬─────┘  └────┬─────┘  └────┬─────┘                   │
│        └─────────────┼─────────────┘                          │
│                      ▼                                        │
│                  DSP Chain                                    │
└─────────────────────────────────────────────────────────────┘
```

## 設計原則

### UMIP互換

UMIM-NativeはUMIP (UMI-Processor) 仕様と互換性を持つ:

| 要素 | UMIP | UMIM-Native |
|------|------|-------------|
| 処理関数 | `process(AudioContext&)` | `process(AudioContext&)` |
| パラメータID | `param_id_t` (uint32_t) | `param_id_t` (uint32_t) |
| イベント処理 | `input_events` span | `input_events` span |
| 型エイリアス | `sample_t`, `port_id_t` | 同一 |

### 技術選定

| 技術 | 採用理由 |
|------|----------|
| ELF共有オブジェクト | 標準フォーマット、ツール互換性 |
| Position Independent Code | 任意アドレスにロード可能 |
| 動的シンボル解決 | 関数テーブルのハードコーディング不要 |
| セクションベースのメタデータ | 自己記述的、拡張性 |

## バイナリ形式

### ファイル構造

```
.umim (ELF 32-bit LSB shared object, ARM)
├── ELF Header
├── Program Headers
│   ├── PT_LOAD     (text)
│   ├── PT_LOAD     (module_header)
│   ├── PT_LOAD     (data)
│   └── PT_DYNAMIC
└── Sections
    ├── .dynsym / .dynstr / .rel.dyn
    ├── .text / .rodata
    ├── .umim_header
    ├── .dynamic / .got
    └── .data / .bss
```

### モジュールヘッダ

```cpp
// SPDX-License-Identifier: MIT
// umi/module/header.hh

#pragma once

#include <umi/core/types.hh>      // sample_t, param_id_t, port_id_t
#include <umi/core/processor.hh>  // ParamDescriptor, PortDescriptor
#include <cstdint>
#include <cstddef>

namespace umi::module {

/// モジュールマジックナンバー: "UMIM"
inline constexpr uint32_t module_magic = 0x4D494D55;

/// API バージョン (major.minor: 0xMMmm)
inline constexpr uint16_t module_api_version = 0x0100;  // 1.0

/// モジュール名最大長
inline constexpr size_t module_name_max_len = 32;

/// 最大パラメータ数
inline constexpr size_t max_params = 32;

/// 最大ポート数
inline constexpr size_t max_ports = 8;

/// モジュールタイプ
enum class ModuleType : uint32_t {
    OSCILLATOR = 1,
    EFFECT     = 2,
    FILTER     = 3,
    MODULATOR  = 4,
    ANALYZER   = 5,
    UTILITY    = 6,
};

/// モジュールヘッダ（ELFセクションに配置）
struct alignas(8) ModuleHeader {
    // --- 識別情報 (16 bytes) ---
    uint32_t magic;                     ///< 0x4D494D55 ("UMIM")
    uint16_t header_size;               ///< ヘッダサイズ
    uint16_t api_version;               ///< API バージョン
    ModuleType type;                    ///< モジュールタイプ
    uint32_t flags;                     ///< フラグ（予約）

    // --- モジュール情報 (48 bytes) ---
    uint32_t vendor_id;                 ///< ベンダーID
    uint32_t module_id;                 ///< モジュールID
    uint32_t version;                   ///< バージョン (major.minor.patch)
    char name[module_name_max_len];     ///< モジュール名
    uint32_t reserved1;

    // --- I/O構成 (8 bytes) ---
    uint8_t num_ports;                  ///< ポート数
    uint8_t num_params;                 ///< パラメータ数
    uint8_t reserved2[6];

    // --- ポート・パラメータへのオフセット ---
    // 可変長データはヘッダ直後に配置
    // ports: PortDescriptor[num_ports]
    // params: ParamDescriptor[num_params]
};

/// ヘッダ配置属性
#define UMI_MODULE_HEADER __attribute__((used, section(".umim_header")))

} // namespace umi::module
```

## モジュールAPI

### 設計方針：C ABI境界

**問題**: C++の`AudioContext&`や`std::span`をPIC境界で直接渡すと：
- `std::span`のメモリレイアウトがコンパイラ/標準ライブラリ間で異なる可能性
- C++参照のABI互換性が保証されない
- 異なるコンパイル単位間でのC++型の受け渡しは危険

**解決**: モジュール境界では**C互換の構造体**を使用し、ホスト側でUMIP互換の`AudioContext`に変換する。

### C ABI境界構造体

```cpp
// umi/module/abi.hh - モジュール境界で使用するC互換構造体

#pragma once

#include <cstdint>
#include <cstddef>

namespace umi::module {

/// イベント型（C互換）
struct AbiEvent {
    uint32_t port_id;
    uint32_t sample_pos;
    uint8_t type;           // EventType
    uint8_t midi_bytes[3];
    uint8_t midi_size;
    uint32_t param_id;
    float param_value;
};

/// オーディオ処理コンテキスト（C互換）
struct AbiProcessContext {
    // バッファ（ポインタ配列）
    const float* const* inputs;     ///< 入力バッファ配列
    float* const* outputs;          ///< 出力バッファ配列
    uint32_t num_inputs;            ///< 入力チャンネル数
    uint32_t num_outputs;           ///< 出力チャンネル数

    // イベント（ポインタ + サイズ）
    const AbiEvent* input_events;   ///< 入力イベント配列
    uint32_t num_input_events;      ///< 入力イベント数

    // タイミング
    uint32_t sample_rate;           ///< サンプルレート (Hz)
    uint32_t buffer_size;           ///< バッファサイズ
    uint64_t sample_position;       ///< 累積サンプル位置
};

} // namespace umi::module
```

### エクスポート関数

モジュールがエクスポートする関数（C ABI）:

```cpp
// umi/module/api.hh

#pragma once

#include "abi.hh"
#include <cstdint>

// ============================================================================
// ライフサイクル
// ============================================================================

/// モジュール初期化
/// @param sample_rate サンプルレート (Hz)
/// @param max_buffer_size 最大バッファサイズ
/// @return 0で成功、負値でエラー
extern "C" int32_t umi_init(uint32_t sample_rate, uint32_t max_buffer_size);

/// モジュール破棄
extern "C" void umi_teardown();

/// リセット（状態クリア、パラメータは保持）
extern "C" void umi_reset();

// ============================================================================
// オーディオ処理（C ABI）
// ============================================================================

/// オーディオ処理
/// @param ctx C互換のコンテキスト構造体へのポインタ
extern "C" void umi_process(const umi::module::AbiProcessContext* ctx);

// ============================================================================
// パラメータ
// ============================================================================

/// パラメータ数取得
extern "C" uint32_t umi_get_param_count();

/// パラメータ値取得
/// @param id パラメータID
extern "C" float umi_get_param(uint32_t id);

/// パラメータ値設定
/// @param id パラメータID
/// @param value 値
extern "C" void umi_set_param(uint32_t id, float value);

// ============================================================================
// ポート
// ============================================================================

/// ポート数取得
extern "C" uint32_t umi_get_port_count();

// ============================================================================
// 状態保存（オプション）
// ============================================================================

/// 状態保存に必要なバイト数
extern "C" size_t umi_get_state_size();

/// 状態を保存
/// @param buffer 保存先バッファ
/// @param size バッファサイズ
/// @return 実際に書き込んだバイト数
extern "C" size_t umi_save_state(uint8_t* buffer, size_t size);

/// 状態を復元
/// @param data 状態データ
/// @param size データサイズ
/// @return 成功で非ゼロ
extern "C" int umi_load_state(const uint8_t* data, size_t size);
```

### 関数命名規則

全エクスポート関数に`umi_`プレフィックスを付与：
- シンボル衝突を防止
- モジュールAPIであることが明確
- `nm`等でのデバッグが容易

## モジュールローダー

ホストがモジュールをロードし、UMIP互換の`AudioContext`からC ABI構造体への変換を行う。

```cpp
// umi/module/loader.hh

#pragma once

#include "header.hh"
#include "abi.hh"
#include <umi/core/audio_context.hh>
#include <umi/core/processor.hh>
#include <cstdint>
#include <cstddef>
#include <span>

namespace umi::module {

/// ロード結果
enum class LoadResult {
    OK,
    INVALID_ELF,
    INVALID_MAGIC,
    INVALID_VERSION,
    INVALID_SIZE,
    OUT_OF_MEMORY,
    SYMBOL_NOT_FOUND,
};

/// ロード済みモジュール（ProcessorLikeコンセプト準拠）
///
/// C ABI境界をカプセル化し、UMIP互換のインターフェースを提供。
/// process()でAudioContext→AbiProcessContext変換を行う。
class Module {
public:
    Module() = default;
    ~Module();

    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;
    Module(Module&& other) noexcept;
    Module& operator=(Module&& other) noexcept;

    /// モジュールをロード
    static LoadResult load(
        const uint8_t* data,
        size_t size,
        void* memory,
        size_t memory_size,
        Module& out
    );

    // --- UMIP ProcessorLike インターフェース ---

    /// オーディオ処理（ProcessorLikeコンセプト必須）
    ///
    /// AudioContext → AbiProcessContext 変換を行ってからモジュールを呼び出す
    void process(AudioContext& ctx);

    // --- パラメータ ---

    [[nodiscard]] uint32_t param_count() const {
        return get_param_count_fn ? get_param_count_fn() : 0;
    }

    [[nodiscard]] float get_param(param_id_t id) const {
        return get_param_fn ? get_param_fn(id) : 0.0f;
    }

    void set_param(param_id_t id, float value) {
        if (set_param_fn) set_param_fn(id, value);
    }

    // --- ポート ---

    [[nodiscard]] uint32_t port_count() const {
        return get_port_count_fn ? get_port_count_fn() : 0;
    }

    // --- ライフサイクル ---

    [[nodiscard]] bool init(uint32_t sample_rate, uint32_t max_buffer_size) {
        return init_fn && init_fn(sample_rate, max_buffer_size) == 0;
    }

    void teardown() {
        if (teardown_fn) teardown_fn();
    }

    void reset() {
        if (reset_fn) reset_fn();
    }

    // --- 状態 ---

    [[nodiscard]] size_t state_size() const {
        return get_state_size_fn ? get_state_size_fn() : 0;
    }

    size_t save_state(std::span<uint8_t> buffer) {
        return save_state_fn ? save_state_fn(buffer.data(), buffer.size()) : 0;
    }

    bool load_state(std::span<const uint8_t> data) {
        return load_state_fn && load_state_fn(data.data(), data.size());
    }

    // --- メタデータ ---

    [[nodiscard]] const ModuleHeader& header() const { return *module_header; }
    [[nodiscard]] bool is_loaded() const { return module_header != nullptr; }

private:
    void* base = nullptr;
    const ModuleHeader* module_header = nullptr;

    // 必須関数（C ABI）
    int32_t (*init_fn)(uint32_t, uint32_t) = nullptr;
    void (*teardown_fn)() = nullptr;
    void (*process_fn)(const AbiProcessContext*) = nullptr;  // C ABI

    // パラメータ（C ABI）
    uint32_t (*get_param_count_fn)() = nullptr;
    float (*get_param_fn)(uint32_t) = nullptr;
    void (*set_param_fn)(uint32_t, float) = nullptr;

    // ポート（C ABI）
    uint32_t (*get_port_count_fn)() = nullptr;

    // オプション（C ABI）
    void (*reset_fn)() = nullptr;
    size_t (*get_state_size_fn)() = nullptr;
    size_t (*save_state_fn)(uint8_t*, size_t) = nullptr;
    int (*load_state_fn)(const uint8_t*, size_t) = nullptr;

    // イベント変換用バッファ（スタック上に確保）
    static constexpr size_t max_events = 256;
};

// ProcessorLikeコンセプトを満たすことを確認
static_assert(ProcessorLike<Module>);

} // namespace umi::module
```

### AudioContext → AbiProcessContext 変換

```cpp
// umi/module/loader.cc (抜粋)

void Module::process(AudioContext& ctx) {
    if (!process_fn) return;

    // 入力バッファポインタ配列を構築
    const float* input_ptrs[MAX_CHANNELS];
    for (size_t i = 0; i < ctx.inputs.size() && i < MAX_CHANNELS; ++i) {
        input_ptrs[i] = ctx.inputs[i];
    }

    // 出力バッファポインタ配列を構築
    float* output_ptrs[MAX_CHANNELS];
    for (size_t i = 0; i < ctx.outputs.size() && i < MAX_CHANNELS; ++i) {
        output_ptrs[i] = ctx.outputs[i];
    }

    // イベント変換（スタック上）
    AbiEvent abi_events[max_events];
    uint32_t num_events = 0;
    for (const auto& ev : ctx.input_events) {
        if (num_events >= max_events) break;
        auto& ae = abi_events[num_events++];
        ae.port_id = ev.port_id;
        ae.sample_pos = ev.sample_pos;
        ae.type = static_cast<uint8_t>(ev.type);
        if (ev.type == EventType::Midi) {
            ae.midi_bytes[0] = ev.midi.bytes[0];
            ae.midi_bytes[1] = ev.midi.bytes[1];
            ae.midi_bytes[2] = ev.midi.bytes[2];
            ae.midi_size = ev.midi.size;
        } else if (ev.type == EventType::Param) {
            ae.param_id = ev.param.id;
            ae.param_value = ev.param.value;
        }
    }

    // C ABI構造体を構築
    AbiProcessContext abi_ctx{
        .inputs = input_ptrs,
        .outputs = output_ptrs,
        .num_inputs = static_cast<uint32_t>(ctx.inputs.size()),
        .num_outputs = static_cast<uint32_t>(ctx.outputs.size()),
        .input_events = abi_events,
        .num_input_events = num_events,
        .sample_rate = ctx.sample_rate,
        .buffer_size = ctx.buffer_size,
        .sample_position = ctx.sample_position,
    };

    // モジュール呼び出し（C ABI）
    process_fn(&abi_ctx);
}
```

## 使用例

### アプリケーション側

```cpp
// examples/module_host/src/main.cc

#include <umi/app.hh>
#include <umi/module/loader.hh>

int main() {
    using namespace umi;
    using namespace umi::module;

    // モジュール用メモリ
    static uint8_t module_memory[32 * 1024];

    // モジュールをロード
    Module synth;
    auto result = Module::load(
        module_data, module_size,
        module_memory, sizeof(module_memory),
        synth
    );

    if (result != LoadResult::OK) {
        return -1;
    }

    // 初期化
    if (!synth.init(48000, 256)) {
        return -1;
    }

    // ProcessorLikeなのでそのまま登録可能
    register_processor(synth);

    // イベントループ
    while (true) {
        auto ev = wait_event();
        if (ev.type == EventType::SHUTDOWN) break;

        // パラメータ変更はset_param()で
        if (ev.type == EventType::PARAM) {
            synth.set_param(ev.param.id, ev.param.value);
        }
    }

    synth.teardown();
    return 0;
}
```

### モジュール実装

```cpp
// my_synth/src/synth.cc

#include <umi/module/api.hh>
#include <umi/module/abi.hh>
#include <umi/module/header.hh>
#include <cmath>

using namespace umi::module;

// ============================================================================
// モジュールヘッダ
// ============================================================================

extern "C" const ModuleHeader UMI_MODULE_HEADER module_header = {
    .magic = module_magic,
    .header_size = sizeof(ModuleHeader),
    .api_version = module_api_version,
    .type = ModuleType::OSCILLATOR,
    .flags = 0,

    .vendor_id = 0x0001,
    .module_id = 0x0001,
    .version = 0x00010000,
    .name = "Simple Synth",

    .num_ports = 1,
    .num_params = 2,
};

// ============================================================================
// シンセ状態
// ============================================================================

namespace {

struct SynthState {
    float phase = 0.0f;
    float frequency = 440.0f;
    float volume = 0.8f;
    float cutoff = 1000.0f;
    float sample_rate = 48000.0f;

    uint8_t current_note = 0;
    bool note_active = false;

    float note_to_freq(uint8_t note) const {
        return 440.0f * std::pow(2.0f, (note - 69) / 12.0f);
    }

    float generate() {
        if (!note_active) return 0.0f;
        float sample = std::sin(phase * 6.283185f) * volume;
        phase += frequency / sample_rate;
        if (phase >= 1.0f) phase -= 1.0f;
        return sample;
    }
};

SynthState g_state;

// MIDIコマンド定数
constexpr uint8_t midi_note_on = 0x90;
constexpr uint8_t midi_note_off = 0x80;

} // namespace

// ============================================================================
// API実装（C ABI）
// ============================================================================

extern "C" {

int32_t umi_init(uint32_t sample_rate, uint32_t max_buffer_size) {
    (void)max_buffer_size;
    g_state = SynthState{};
    g_state.sample_rate = static_cast<float>(sample_rate);
    return 0;
}

void umi_teardown() {
    // クリーンアップ
}

void umi_reset() {
    g_state.phase = 0.0f;
    g_state.note_active = false;
}

void umi_process(const AbiProcessContext* ctx) {
    if (!ctx) return;

    // イベント処理（C ABI構造体から）
    for (uint32_t i = 0; i < ctx->num_input_events; ++i) {
        const auto& ev = ctx->input_events[i];
        if (ev.type == 0) {  // EventType::Midi
            uint8_t cmd = ev.midi_bytes[0] & 0xF0;
            uint8_t note = ev.midi_bytes[1];
            uint8_t velocity = ev.midi_bytes[2];

            if (cmd == midi_note_on && velocity > 0) {
                g_state.current_note = note;
                g_state.frequency = g_state.note_to_freq(note);
                g_state.note_active = true;
                g_state.phase = 0.0f;
            } else if (cmd == midi_note_off || (cmd == midi_note_on && velocity == 0)) {
                if (note == g_state.current_note) {
                    g_state.note_active = false;
                }
            }
        }
    }

    // オーディオ生成
    if (ctx->num_outputs == 0 || !ctx->outputs[0]) return;

    float* out_l = ctx->outputs[0];
    float* out_r = (ctx->num_outputs > 1) ? ctx->outputs[1] : nullptr;

    for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
        float sample = g_state.generate();
        out_l[i] = sample;
        if (out_r) out_r[i] = sample;
    }
}

uint32_t umi_get_param_count() {
    return 2;
}

float umi_get_param(uint32_t id) {
    switch (id) {
    case 0: return g_state.volume;
    case 1: return g_state.cutoff;
    default: return 0.0f;
    }
}

void umi_set_param(uint32_t id, float value) {
    switch (id) {
    case 0: g_state.volume = value; break;
    case 1: g_state.cutoff = value; break;
    }
}

uint32_t umi_get_port_count() {
    return 1;
}

} // extern "C"
```

## ビルド設定

### リンカスクリプト

```ld
/* ld/umim.ld */

PHDRS
{
    headers PT_PHDR PHDRS;
    text PT_LOAD FILEHDR PHDRS;
    module_header PT_LOAD;
    data PT_LOAD;
    dynamic PT_DYNAMIC;
}

SECTIONS
{
    . = SIZEOF_HEADERS;

    .hash : ALIGN(4) { KEEP(*(.hash)) } :text
    .dynsym : ALIGN(4) { KEEP(*(.dynsym)) } :text
    .dynstr : ALIGN(4) { KEEP(*(.dynstr)) } :text
    .rel.dyn : ALIGN(4) { KEEP(*(.rel.dyn)) } :text
    .rel.plt : ALIGN(4) { KEEP(*(.rel.plt)) } :text

    .text : ALIGN(4)
    {
        *(.text .text.*)
        KEEP(*(.init))
        KEEP(*(.fini))
        *(.rodata .rodata.*)
    } :text

    .umim_header : ALIGN(8)
    {
        KEEP(*(.umim_header))
    } :module_header

    .init_array : ALIGN(4)
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN(__init_array_end = .);
    } :data

    .dynamic : ALIGN(8) { *(.dynamic) } :data :dynamic

    .got : ALIGN(4)
    {
        *(.got.plt .igot.plt .got .igot)
    } :data

    .data : ALIGN(4)
    {
        __data_start__ = .;
        *(.data .data.*)
        __data_end__ = .;
    } :data

    .bss (NOLOAD) : ALIGN(4)
    {
        __bss_start__ = .;
        *(.bss .bss.* COMMON)
        __bss_end__ = .;
    } :data

    /DISCARD/ :
    {
        *(.fini_array* .note.GNU-stack .gnu_debuglink)
    }
}
```

### xmake.lua

```lua
target("my_synth_module")
    set_kind("shared")
    set_extension(".umim")

    add_files("src/*.cc")
    add_includedirs("$(projectdir)/lib/umi/include")

    add_cxflags("-fPIC")
    add_cxxflags("-fPIC", "-fno-rtti", "-fno-exceptions", "-std=c++17")

    add_ldflags(
        "-shared",
        "--entry=0",
        "-nostartfiles",
        "-Wl,-z,max-page-size=128",
        "-Wl,-T,$(projectdir)/ld/umim.ld",
        {force = true}
    )

    set_toolchains("arm-none-eabi")

    if is_mode("release") then
        set_optimize("smallest")
    end
```

## UMIP互換性

### アーキテクチャ

```
┌─────────────────────────────────────────────────────────────────┐
│                    ホスト（アプリケーション）                      │
│                                                                   │
│   ┌─────────────────────────────────────────────────────────┐   │
│   │              Module クラス (loader.hh)                   │   │
│   │                                                           │   │
│   │   process(AudioContext& ctx)                              │   │
│   │       │                                                   │   │
│   │       │  AudioContext → AbiProcessContext 変換            │   │
│   │       ▼                                                   │   │
│   │   process_fn(&abi_ctx)  ─────────────────────────────────│───│──┐
│   │                                                           │   │  │
│   │   ※ ProcessorLike コンセプト準拠                          │   │  │
│   └─────────────────────────────────────────────────────────┘   │  │
│                                                                   │  │
├───────────────────────────────────────────────────────────────────┤  │
│                        C ABI 境界                                 │  │
├───────────────────────────────────────────────────────────────────┤  │
│                                                                   │  │
│   ┌─────────────────────────────────────────────────────────┐   │  │
│   │              モジュール (.umim)                           │   │  │
│   │                                                           │◄──│──┘
│   │   umi_process(const AbiProcessContext* ctx)               │   │
│   │       - C互換構造体のみ受け取り                            │   │
│   │       - std::span, C++参照なし                            │   │
│   │       - PIC安全                                           │   │
│   └─────────────────────────────────────────────────────────┘   │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘
```

### ProcessorLikeコンセプト

`Module`クラスはホスト側でUMIP `ProcessorLike`コンセプトを満たす:

```cpp
template<typename T>
concept ProcessorLike = requires(T& p, AudioContext& ctx) {
    { p.process(ctx) } -> std::same_as<void>;
};

// Module は ProcessorLike を満たす
static_assert(ProcessorLike<Module>);
```

ホストは`Module`を他のUMIP Processorと同様に扱える。

### C ABI境界の分離

| 層 | 使用する型 | 目的 |
|----|-----------|------|
| **ホスト側** | `AudioContext`, `std::span`, C++参照 | UMIP互換 |
| **境界** | `AbiProcessContext`, Cポインタ | ABI安定性 |
| **モジュール側** | `AbiProcessContext*`, プリミティブ型 | PIC安全 |

### なぜC ABIが必要か

1. **std::spanのレイアウト**: 標準ライブラリ実装間で異なる可能性
2. **C++参照**: コンパイラ間でABI互換性なし
3. **vtable/RTTI**: 動的ロードで解決不可能
4. **PIC**: モジュールは任意アドレスにロードされる

## セキュリティ

### 検証項目

1. **マジックナンバー**: `0x4D494D55` ("UMIM")
2. **APIバージョン**: メジャーバージョン一致
3. **ELFヘッダ**: 有効なARM ELF共有オブジェクト
4. **サイズ**: メモリスロットに収まるか

### 拡張（将来）

```cpp
struct SignedModuleHeader {
    ModuleHeader base;
    uint8_t signature[64];  // Ed25519
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

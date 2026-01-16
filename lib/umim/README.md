# UMI-OS WASM Adapter

WASM オーディオアプリケーション用の共通アダプタ。

## ファイル

| ファイル | 説明 |
|----------|------|
| `wasm_adapter.hh` | 共通テンプレート `WasmAdapter<App>` + C API 生成マクロ |
| `synth_wasm.cc` | Synth 単体（legacy） |
| `test_wasm.cc` | テスト用（legacy） |

## 使用方法

### 1. アプリクラスを定義

```cpp
// my_app.hh
class MyApp {
public:
    void process(AudioContext& ctx);
    void note_on(uint8_t note, uint8_t velocity);
    void note_off(uint8_t note);
    void set_param(uint32_t index, float value);
    float get_param(uint32_t index) const;
    
    // 必須: メタデータアクセス
    static constexpr size_t param_count();
    static const ParamMeta& param_meta(uint32_t index);
    static constexpr uint32_t find_param_index_by_cc(uint8_t ch, uint8_t cc);
};
```

### 2. WASM エントリポイントを作成

```cpp
// my_app_wasm.cc
#include "adapter/wasm/wasm_adapter.hh"
#include "my_app.hh"

using MyAdapter = umi::wasm::WasmAdapter<MyApp>;
MyAdapter g_adapter;

// C API を生成（prefix = "myapp"）
UMI_WASM_DEFINE_API(myapp, MyAdapter, g_adapter)
```

### 3. xmake.lua に追加

```lua
target("wasm_myapp")
    -- ... 共通設定 ...
    add_files("examples/myapp/myapp_wasm.cc")
    add_ldflags("-sEXPORTED_FUNCTIONS=['_myapp_create','_myapp_process',...]")
target_end()
```

## 生成される C API

`UMI_WASM_DEFINE_API(prefix, ...)` で以下が生成されます：

| 関数 | 説明 |
|------|------|
| `prefix_create(sample_rate)` | 初期化 |
| `prefix_process(in, out, frames)` | オーディオ処理 |
| `prefix_process_synth(out, frames)` | シンセのみ |
| `prefix_note_on(note, velocity)` | ノートオン |
| `prefix_note_off(note)` | ノートオフ |
| `prefix_set_param(index, value)` | パラメータ設定 |
| `prefix_get_param(index)` | パラメータ取得 |
| `prefix_process_cc(ch, cc, value)` | MIDI CC 処理 |
| `prefix_midi_learn(cc, param_id)` | MIDI Learn |
| `prefix_midi_unlearn(cc)` | MIDI Unlearn |
| `prefix_get_buffer_ptr()` | バッファポインタ |
| `prefix_get_param_count()` | パラメータ数 |
| `prefix_get_param_name(index)` | パラメータ名 |
| `prefix_get_param_min/max/default(index)` | 範囲 |
| `prefix_get_param_curve(index)` | カーブ種別 |
| `prefix_get_param_unit(index)` | 単位 |

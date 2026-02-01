# 04 — パラメータシステム

## 概要

パラメータシステムは、MIDI CC やハードウェア入力をアプリケーションのパラメータに変換する仕組みである。
変換は EventRouter が機械的に行い、結果を共有メモリ（SharedParamState）に書き込む。
Processor は process() 内で `ctx.params` として読み取る。

## SharedParamState

完全な構造体定義は [10-shared-memory.md](10-shared-memory.md) を参照。以下は主要メンバーの抜粋。

```cpp
struct SharedParamState {
    float values[32];           // パラメータ値
    uint32_t changed_flags;     // 変更フラグ（ビットフィールド）
    uint32_t version;           // 更新カウンタ（毎ブロック先頭でインクリメント）
};
// sizeof = 164B
```

- `values[]` は EventRouter のみが書き込む
- Processor / Controller は読み取りのみ
- `changed_flags` はビット i が立っていれば `values[i]` が前ブロックから変化した

### Processor からの読み取り

```cpp
void process(umi::AudioContext& ctx) {
    float cutoff = ctx.params.values[PARAM_CUTOFF];
    float reso = ctx.params.values[PARAM_RESONANCE];

    // 変更検出（オプション）
    if (ctx.params.changed_flags & (1u << PARAM_CUTOFF)) {
        filter.set_cutoff(cutoff);
    }
}
```

## SharedChannelState

MIDI チャンネル状態の最新値。System が更新する。完全な定義は [10-shared-memory.md](10-shared-memory.md) を参照。

```cpp
struct SharedChannelState {
    struct Channel {
        uint8_t program;        // プログラム番号
        uint8_t pressure;       // チャンネルプレッシャー
        int16_t pitch_bend;     // ピッチベンド (-8192 ~ 8191)
    };
    Channel channels[16];
};
// sizeof = 64B
```

```cpp
void process(umi::AudioContext& ctx) {
    int16_t bend = ctx.channel.channels[0].pitch_bend;
    float bend_semitones = float(bend) / 8192.0f * 2.0f;  // ±2半音
}
```

## SharedInputState

ハードウェア入力の生値。System が ADC/GPIO からの値を正規化して格納する。完全な定義は [10-shared-memory.md](10-shared-memory.md) を参照。

```cpp
struct SharedInputState {
    uint16_t raw[16];   // 0x0000〜0xFFFF
};
// sizeof = 32B
```

process() 内で CV 入力の即値が必要な場合に使用:

```cpp
float pitch_cv = float(ctx.input.raw[CV_PITCH]) / 65535.0f;
```

ただし通常は InputParamMapping 経由で SharedParamState に変換して使う方が望ましい。

## ParamDescriptor — パラメータの正式定義

各パラメータの名前、値域、デフォルト値、変換カーブを定義する。Processor が `params()` メソッドで公開する。

```cpp
enum class ParamCurve : uint8_t {
    Linear = 0,
    Log,            // 周波数、時間等の対数スケール
    Exp,            // 指数スケール
    Auto = 255,     // 名前と範囲から自動推定
};

struct ParamDescriptor {
    param_id_t id = 0;
    std::string_view name;
    float default_value = 0.0f;
    float min_value = 0.0f;
    float max_value = 1.0f;
    ParamCurve curve = ParamCurve::Auto;

    // 正規化値 (0.0-1.0) → 実値（カーブ適用）
    constexpr float denormalize(float normalized) const noexcept;
    // 実値 → 正規化値
    constexpr float normalize(float value) const noexcept;
    // 値域にクランプ
    constexpr float clamp(float value) const noexcept;
};
```

`ParamCurve::Auto` は名前と範囲から自動推定する（"cutoff", "freq" → Log、"attack", "release" → Log 等）。

### Processor からの公開

```cpp
template<typename T>
concept HasParams = requires(const T& p) {
    { p.params() } -> std::convertible_to<std::span<const ParamDescriptor>>;
};
```

```cpp
struct Synth {
    static constexpr ParamDescriptor param_descriptors[] = {
        {0, "Cutoff",    500.0f, 20.0f, 20000.0f, ParamCurve::Log},
        {1, "Resonance", 0.0f,   0.0f,  1.0f,     ParamCurve::Linear},
        {2, "Attack",    0.01f,  0.001f, 2.0f,     ParamCurve::Log},
    };

    std::span<const ParamDescriptor> params() const { return param_descriptors; }
    void process(umi::AudioContext& ctx) { /* ... */ }
};
```

### OS への受け渡し

`register_processor()` 時に `HasParams` を満たす Processor からは自動的に ParamDescriptor のスパンが取得される。OS 側はポインタと長さのみ保持し、アプリ Flash 上の constexpr データを直接参照する（コピー不要）。

`HasParams` を満たさない Processor の場合、全パラメータがデフォルト定義（0.0〜1.0, Linear）として扱われる。

### 用途

| バックエンド | ParamDescriptor の用途 |
|-------------|----------------------|
| 組み込み | EventRouter のクランプ・カーブ変換、シェルでのパラメータ名表示 |
| Plugin | ホストへのパラメータリスト公開（名前、値域、デフォルト） |
| WASM | JS 側への metadata 公開（名前、単位等） |

## ParamMapping — CC → パラメータ接続

RouteTable で `ROUTE_PARAM` に振り分けられた CC を、SharedParamState のどのパラメータに接続するかのテーブル。

ParamMapping はルーティング（どの CC がどのパラメータか）と正規化範囲を定義する。実値への変換は ParamDescriptor が担う。

```cpp
struct ParamMapEntry {
    param_id_t param_id;    // 対象パラメータ ID (INVALID = 未マッピング)
    uint8_t _pad[3];        // 4B アライメント
    float range_low;        // 正規化範囲下限 (0.0 = パラメータ min)
    float range_high;       // 正規化範囲上限 (1.0 = パラメータ max)
};
// sizeof = 12B

struct ParamMapping {
    static constexpr param_id_t INVALID = 0xFF;
    ParamMapEntry control_change[128];  // CC 番号でインデクス
};
// 128 × 12B = 1536B
```

大半のケースでは `{param_id, {}, 0.0f, 1.0f}` で全域マッピング。部分範囲が必要な場合のみ変更する:

```cpp
// CC#74 で Cutoff の全域 (20-20000Hz) を制御
cfg.param_mapping.control_change[74] = {PARAM_CUTOFF, {}, 0.0f, 1.0f};

// CC#71 で Cutoff の中域 (200-8000Hz) だけを制御
cfg.param_mapping.control_change[71] = {PARAM_CUTOFF, {}, 0.15f, 0.85f};
```

### EventRouter の変換フロー

```
CC#74 値=100
    │
    ▼ 正規化: 100 / 127 = 0.787
    │
    ▼ ParamMapEntry: range_low=0.0, range_high=1.0
    │ → mapped = 0.0 + 0.787 * (1.0 - 0.0) = 0.787
    │
    ▼ ParamDescriptor[CUTOFF]: min=20, max=20000, curve=Log
    │ → denormalize(0.787) = 4536 Hz
    │
    ▼ SharedParamState.values[PARAM_CUTOFF] = 4536.0
```

```cpp
void handle_cc_param(const umidi::UMP32& ump) {
    const auto& entry = active_param_mapping().control_change[ump.cc_number()];
    if (entry.param_id == ParamMapping::INVALID) return;

    float normalized = float(ump.cc_value()) / 127.0f;
    float mapped = entry.range_low + normalized * (entry.range_high - entry.range_low);

    // ParamDescriptor があればカーブ変換とクランプ
    float value = param_descriptors
        ? param_descriptors[entry.param_id].denormalize(mapped)
        : mapped;  // HasParams なし → mapped がそのまま実値

    shared_param_state.values[entry.param_id] = value;
}
```

配列の直接インデクス — O(1)、分岐なし。

## InputParamMapping — ハードウェア入力 → パラメータ変換

MIDI CC を経由せず、ハードウェア入力 ID（ノブ/CV/エンコーダ）をキーとして直接パラメータに変換する。

```cpp
struct InputParamMapping {
    ParamMapEntry entries[16];  // input_id でインデクス
};
// 16 × 12B = 192B
```

ParamMapEntry は ParamMapping と同じ型を再利用する。変換フローも同一（正規化 → range_low/high → denormalize）。

```cpp
void handle_input_param(uint8_t input_id, uint16_t raw_value) {
    const auto& entry = active_input_mapping().entries[input_id];
    if (entry.param_id == ParamMapping::INVALID) return;

    float normalized = float(raw_value) / 65535.0f;
    float mapped = entry.range_low + normalized * (entry.range_high - entry.range_low);

    float value = param_descriptors
        ? param_descriptors[entry.param_id].denormalize(mapped)
        : mapped;

    shared_param_state.values[entry.param_id] = value;
}
```

CC マッピングと入力マッピングが同じパラメータを指す場合、last-write-wins。

## ParamSetRequest — Controller からのパラメータ変更

Controller が Processor のパラメータを変更する場合、直接 SharedParamState を書き換えず、System 経由でリクエストする。

```cpp
struct ParamSetRequest {
    param_id_t param_id;
    uint8_t _pad[3];
    float value;              // 実値（ParamDescriptor の値域内）
};
// sizeof = 8B

// Controller から（実値で指定）:
umi::send_param_request({PARAM_CUTOFF, {}, 4536.0f});
```

System は ParamSetRequestQueue から pop し、ParamDescriptor があればクランプした上で SharedParamState に書き込む。

## AppConfig — 設定の一元化

RouteTable + ParamMapping + InputParamMapping + InputConfig を統合する構造体。
アプリ開発者は AppConfig ひとつで全設定を定義できる。

```cpp
struct AppConfig {
    RouteTable route_table;             // 272B
    ParamMapping param_mapping;         // 1536B
    InputParamMapping input_mapping;    // 192B
    InputConfig inputs[16];             // 128B (8B × 16)
};
// 合計: ~2128B
```

### constexpr 定義

```cpp
#include <umi/app.hh>

constexpr AppConfig SYNTH_CONFIG = [] {
    AppConfig cfg = umi::default_app_config();

    // CC#74 → Cutoff
    cfg.route_table.control_change[74] = ROUTE_PARAM;
    cfg.param_mapping.control_change[74] = {PARAM_CUTOFF, {}, 0.0f, 1.0f};

    // CC#71 → Resonance
    cfg.route_table.control_change[71] = ROUTE_PARAM;
    cfg.param_mapping.control_change[71] = {PARAM_RESONANCE, {}, 0.0f, 1.0f};

    // ハードウェアノブ → パラメータ
    cfg.input_mapping.entries[KNOB_CUTOFF] = {PARAM_CUTOFF, {}, 0.0f, 1.0f};
    cfg.input_mapping.entries[KNOB_RESO] = {PARAM_RESONANCE, {}, 0.0f, 1.0f};

    // ノブのスムージング
    // deadzone=0, smoothing=1000ms, threshold=655 (≈1% of 65535)
    cfg.inputs[KNOB_CUTOFF] = {KNOB_CUTOFF, InputMode::PARAM_AND_EVENT, 0, 1000, 655};

    return cfg;
}();
```

### 適用

```cpp
int main() {
    static Synth synth;
    umi::register_processor(synth);
    umi::set_app_config(SYNTH_CONFIG);
    // ...
}
```

`umi::set_app_config()` は1回の syscall で全設定を OS に渡す。
OS 側は inactive バッファに RouteTable, ParamMapping, InputParamMapping を一括コピーし、各 InputConfig を設定する。
ブロック境界で一括反映。

コスト: ~2KB memcpy。Cortex-M4 で ~600 サイクル（~3.6μs @ 168MHz）。

### レイヤー切り替え

複数の constexpr AppConfig を定義し、ボタン等で切り替える:

```cpp
constexpr AppConfig PLAY_CONFIG = [] {
    AppConfig cfg = umi::default_app_config();
    cfg.input_mapping.entries[KNOB_1] = {PARAM_CUTOFF, {}, 0.0f, 1.0f};
    cfg.input_mapping.entries[KNOB_2] = {PARAM_RESONANCE, {}, 0.0f, 1.0f};
    return cfg;
}();

constexpr AppConfig EDIT_CONFIG = [] {
    AppConfig cfg = umi::default_app_config();
    cfg.input_mapping.entries[KNOB_1] = {PARAM_ATTACK, {}, 0.0f, 1.0f};
    cfg.input_mapping.entries[KNOB_2] = {PARAM_RELEASE, {}, 0.0f, 1.0f};
    return cfg;
}();

// Controller でレイヤー切り替え
void switch_layer(Layer layer) {
    switch (layer) {
    case Layer::PLAY: umi::set_app_config(PLAY_CONFIG); break;
    case Layer::EDIT: umi::set_app_config(EDIT_CONFIG); break;
    }
}
```

切り替えはブロック境界で一括反映されるため、レイヤー間で設定が混在しない。

### YAML からの生成（オプション）

`xmake umi-config app_config.yaml` で constexpr ヘッダを自動生成できる:

```yaml
defaults: synth

params:
  cutoff:    { id: 0, min: 20.0, max: 20000.0, default: 500.0, curve: log }
  resonance: { id: 1, min: 0.0, max: 1.0, default: 0.0 }
  attack:    { id: 3, min: 0.001, max: 2.0, default: 0.01, curve: log }

inputs:
  knob_cutoff: { id: 3, type: knob, smoothing: 1000 }
  knob_reso:   { id: 4, type: knob, smoothing: 1000 }

routes:
  cc:
    74: { param: cutoff }
    71: { param: resonance }
  input:
    knob_cutoff: { param: cutoff }
    knob_reso:   { param: resonance }

layers:
  play:
    input:
      knob_cutoff: { param: cutoff }
      knob_reso:   { param: resonance }
  edit:
    input:
      knob_cutoff: { param: attack }
      knob_reso:   { param: resonance, range: [0.1, 0.99] }  # 正規化範囲
```

YAML は便利だが必須ではない。constexpr ヘッダを直接書いても構わない。

## メモリ使用量

### OS 側（カーネル RAM）

| コンポーネント | サイズ | 備考 |
|---------------|--------|------|
| RouteTable × 2 | 544B | ダブルバッファ |
| ParamMapping × 2 | 3072B | ダブルバッファ (1536B × 2) |
| InputParamMapping × 2 | 384B | ダブルバッファ (192B × 2) |
| AudioEventQueue (64) | 512B | umidi::Event 8B × 64 |
| ControlEventQueue (32) | 256B | ControlEvent 8B × 32 |
| ParamSetRequestQueue (16) | 128B | 8B × 16 |
| ParamDescriptor ポインタ | 8B | pointer + count（Flash 参照） |
| **合計** | **~4.9KB** | |

### 共有メモリ

| コンポーネント | サイズ | 備考 |
|---------------|--------|------|
| SharedParamState | 164B | float×32 + flags + version |
| SharedChannelState | 64B | Channel 4B × 16 |
| SharedInputState | 32B | uint16_t × 16 |
| **合計** | **~260B** | |

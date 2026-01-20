# UI API (入出力抽象化)

`umi::ui` は**完全にハードウェア非依存**の統一インターフェースを提供します。
物理デバイスの種類（ノブ/スライダー/ボタン、LED/メーター/ディスプレイ）は区別しません。
全ては「値 + 変更イベント」に統一されます。

```cpp
#include <umi/ui.hh>
```

---

## 設計思想

**物理デバイスの違いはアプリから見えない**

| 物理デバイス | 抽象化 |
|--------------|--------|
| ポテンショメータ、スライダー、エンコーダ | `input[i]` → float (0.0-1.0) |
| ボタン、タクトスイッチ | `input[i]` → float (0.0 or 1.0) |
| XYパッド | `input[i]`, `input[i+1]` → float × 2 |
| LED、メーター、7セグ | `output[i]` ← float |
| RGB LED | `output[i]` ← float × 3 |
| ディスプレイ | `canvas` ← バッファ |

```
┌─────────────────────────────────────────────────────────────────────┐
│  Application                                                        │
│                                                                     │
│    float v = input[0];      // 値を読む                             │
│    bool c = input.changed(0); // 変化を検出                         │
│    output[0] = 0.8f;        // 値を書く                             │
│                                                                     │
└──────────────────────┬──────────────────────┬───────────────────────┘
                       │                      │
                       v                      v
┌─────────────────────────────────────────────────────────────────────┐
│  Shared Memory                                                      │
│  ┌────────────────────────┐  ┌────────────────────────┐             │
│  │ inputs[N]              │  │ outputs[M]             │             │
│  │  float value           │  │  float value[4]        │             │
│  │  bool  changed         │  │  bool  dirty           │             │
│  └────────────────────────┘  └────────────────────────┘             │
└──────────────────────┬──────────────────────┬───────────────────────┘
                       │                      │
                       v                      v
┌─────────────────────────────────────────────────────────────────────┐
│  Kernel / Driver (BSPで物理デバイスにマッピング)                     │
│  - ADC → 正規化 → inputs[0]                                         │
│  - outputs[0] → PWM → LED                                           │
│  - outputs[1] → I2C → 7seg                                          │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Input (入力)

全ての入力は統一された `Input` クラスでアクセス。

```cpp
class Input {
public:
    explicit Input(SharedRegion& shared);
    
    // 値の読み取り（常に 0.0-1.0 に正規化済み）
    float operator[](uint8_t id) const;
    float get(uint8_t id) const;
    
    // 変化検出
    bool changed(uint8_t id) const;      // 前回から変化した
    bool triggered(uint8_t id) const;    // 0→1 に変化した瞬間
    bool released(uint8_t id) const;     // 1→0 に変化した瞬間
    
    // 相対値（エンコーダ等、読むとリセット）
    int delta(uint8_t id);
    
    // 入力数
    uint8_t count() const;
    
    // 属性取得（オプショナル）
    const InputAttr* attr(uint8_t id) const;
};
```

### 使用例

```cpp
int main() {
    auto& shared = umi::get_shared();
    umi::ui::Input input(shared);
    umi::ui::Output output(shared);
    
    static Synth synth;
    umi::register_processor(synth);
    
    while (true) {
        auto ev = umi::wait_event();
        if (ev.type == umi::EventType::Shutdown) break;
        
        // 連続値（ノブでもスライダーでも同じ）
        synth.volume = input[0];
        synth.cutoff = input[1];
        synth.resonance = input[2];
        
        // 2値（ボタンでもフットスイッチでも同じ）
        if (input.triggered(3)) {
            synth.toggle_mode();
        }
        
        // 相対値（エンコーダでもホイールでも同じ）
        int d = input.delta(4);
        if (d != 0) {
            synth.adjust_preset(d);
        }
        
        // 出力
        output[0] = synth.volume;
        output[1] = synth.get_level();
    }
}
```

---

## Output (出力)

全ての出力は統一された `Output` クラスでアクセス。

```cpp
class Output {
public:
    explicit Output(SharedRegion& shared);
    
    // 単一値出力（0.0-1.0）
    OutputRef operator[](uint8_t id);
    void set(uint8_t id, float value);
    
    // 複数値出力（RGB等）
    void set(uint8_t id, float v0, float v1, float v2);
    void set(uint8_t id, std::span<const float> values);
    
    // 一括更新
    void flush();
    
    // 出力数
    uint8_t count() const;
    
    // 属性取得（オプショナル）
    const OutputAttr* attr(uint8_t id) const;
};

// 代入演算子対応のプロキシ
class OutputRef {
public:
    OutputRef& operator=(float value);
    OutputRef& operator=(bool value);
    OutputRef& operator=(std::initializer_list<float> rgb);
    operator float() const;
};
```

### 使用例

```cpp
// 単一値（LED、メーター、7セグ、全て同じ）
output[0] = 0.8f;          // 80%
output[1] = level;         // メーター
output[2] = is_active;     // bool → 0.0 or 1.0

// 複数値（RGB等）
output[3] = {1.0f, 0.0f, 0.0f};  // 赤
output.set(3, r, g, b);          // 同等

// 配列出力（LEDストリップ等）
float strip[16];
output.set_array(STRIP_ID, strip);

// 更新を反映
output.flush();
```

---

## 属性 (Attributes)

入力/出力にオプショナルな名前やラベルを付与できます。  
これは主に**ディスプレイを持つハイエンドデバイス**で表示に使用し、  
**低リソースのデバイスでは省略可能**です。

```cpp
// 属性構造体（BSP/アプリで定義）
struct InputAttr {
    const char* name;       // 完全名: "Master Volume", "Filter Cutoff"
    const char* label;      // 短縮名: "VOL", "CUT" (OLEDやLCD用)
    const char* unit;       // 単位: "%", "Hz", "dB" (表示用、オプション)
};

struct OutputAttr {
    const char* name;       // 完全名: "Level Meter", "Status LED"
    const char* label;      // 短縮名: "LV", "ST"
};
```

### 属性の使用例

```cpp
// 属性があればディスプレイに表示
void update_display(umi::ui::Input& input, umi::ui::Canvas& canvas) {
    for (uint8_t i = 0; i < input.count(); ++i) {
        if (input.changed(i)) {
            const auto* attr = input.attr(i);
            float value = input[i];
            
            if (attr && attr->label) {
                // ラベル付きで表示: "VOL: 80%"
                canvas.text(0, i * 12, attr->label);
                canvas.text(40, i * 12, format_value(value, attr->unit));
            } else {
                // ラベルなし: "IN0: 0.80"
                canvas.text(0, i * 12, "IN");
                canvas.number(20, i * 12, i);
                canvas.text(40, i * 12, format_float(value));
            }
        }
    }
    canvas.flush();
}
```

### BSPでの属性定義

```cpp
// lib/bsp/my_board/io_attrs.hh (オプショナル)

namespace bsp::io {

// 入力属性（ディスプレイ付きデバイス用）
constexpr InputAttr input_attrs[] = {
    { .name = "Master Volume",   .label = "VOL", .unit = "%" },
    { .name = "Filter Cutoff",   .label = "CUT", .unit = "Hz" },
    { .name = "Filter Resonance",.label = "RES", .unit = nullptr },
    { .name = "Trigger Button",  .label = "TRG", .unit = nullptr },
    { .name = "Menu Encoder",    .label = "ENC", .unit = nullptr },
};

// 出力属性
constexpr OutputAttr output_attrs[] = {
    { .name = "Volume Indicator",.label = "VOL" },
    { .name = "Level Meter",     .label = "LV" },
    { .name = "Status LED",      .label = "ST" },
    { .name = "RGB Indicator",   .label = "RGB" },
};

}
```

### 低リソースデバイスでの省略

属性はオプショナルなので、メモリ制約のあるデバイスでは省略できます：

```cpp
// lib/bsp/minimal_board/io_attrs.hh

namespace bsp::io {

// 属性なし（nullptr で初期化）
constexpr InputAttr* input_attrs = nullptr;
constexpr OutputAttr* output_attrs = nullptr;

}
```

アプリコードは属性の有無に関わらず動作します：

```cpp
const auto* attr = input.attr(i);
if (attr && attr->label) {
    // ラベルがあれば使う
} else {
    // なければインデックス番号で表示
}
```

---

## Canvas (2次元出力)

ディスプレイ用のフレームバッファ。これも本質的には「大きな出力配列」。

```cpp
class Canvas {
public:
    explicit Canvas(SharedRegion& shared);
    
    int width() const;
    int height() const;
    
    // ピクセル単位（最も基本的な操作）
    void set(int x, int y, float brightness);
    void set(int x, int y, float r, float g, float b);
    
    // ユーティリティ（描画ヘルパー）
    void clear();
    void fill(float brightness);
    void line(int x0, int y0, int x1, int y1, float brightness);
    void rect(int x, int y, int w, int h, float brightness);
    void text(int x, int y, const char* str);
    void number(int x, int y, int value);
    
    // バッファ直接アクセス
    std::span<uint8_t> buffer();
    
    // 更新通知
    void flush();
};
```

---

## イベントベース vs ポーリング

```cpp
// イベントベース（推奨）
while (true) {
    auto ev = umi::wait_event();
    if (ev.type == umi::EventType::InputChanged) {
        // ev.input.id で変化した入力を特定
        handle_input(ev.input.id, input[ev.input.id]);
    }
}

// ポーリング（低レイテンシ用）
while (true) {
    umi::wait_event(1000);  // 1ms タイムアウト
    
    for (int i = 0; i < input.count(); ++i) {
        if (input.changed(i)) {
            handle_input(i, input[i]);
        }
    }
}
```

---

## コルーチンでの使用

```cpp
umi::Task<void> ui_task(umi::ui::Input& input, umi::ui::Output& output, Synth& synth) {
    while (true) {
        co_await umi::sleep(16ms);  // 60fps
        
        // 全入力を読む
        synth.volume = input[0];
        synth.cutoff = input[1];
        
        // 全出力を更新
        output[0] = synth.get_level();
        output[1] = synth.is_playing();
        output.flush();
    }
}

int main() {
    auto& shared = umi::get_shared();
    umi::ui::Input input(shared);
    umi::ui::Output output(shared);
    
    static Synth synth;
    umi::register_processor(synth);
    
    umi::Scheduler<2> sched;
    sched.spawn(ui_task(input, output, synth));
    sched.run();
    
    return 0;
}
```

---

## 共有メモリモデル

### アーキテクチャ

```
┌─────────────────────────────────────────────────────────────────────┐
│                         Shared Memory                               │
├─────────────────────────────────────────────────────────────────────┤
│  inputs[N] ────────────────────────────────────────────────────────│
│  │ [0] : { value: 0.75, changed: true,  delta: 0 }                 │
│  │ [1] : { value: 0.50, changed: false, delta: 0 }                 │
│  │ [2] : { value: 1.00, changed: true,  delta: 0 }  ← ボタン       │
│  │ [3] : { value: 0.30, changed: true,  delta: 2 }  ← エンコーダ   │
│  └──────────────────────────────────────────────────────────────── │
│                                                                     │
│  outputs[M] ───────────────────────────────────────────────────────│
│  │ [0] : { value: [0.8, 0, 0, 0], dirty: true  }   ← 単一値        │
│  │ [1] : { value: [1.0, 0, 0, 0], dirty: true  }   ← LED           │
│  │ [2] : { value: [1.0, 0.5, 0, 0], dirty: true }  ← RGB           │
│  └──────────────────────────────────────────────────────────────── │
│                                                                     │
│  canvas ───────────────────────────────────────────────────────────│
│  │ buffer[width * height * bpp]                                    │
│  │ dirty: true                                                     │
│  └──────────────────────────────────────────────────────────────── │
│                                                                     │
│  audio, events, params (既存)                                       │
└─────────────────────────────────────────────────────────────────────┘
```

### SharedRegion 構造

```cpp
struct SharedRegion {
    // === 入力（カーネル→アプリ、読み取り専用） ===
    struct InputSlot {
        float value;              // 正規化値 (0.0-1.0)
        int16_t delta;            // 相対値（エンコーダ等）
        uint8_t flags;            // changed, triggered, released
    };
    InputSlot inputs[MAX_INPUTS];
    
    // === 出力（アプリ→カーネル） ===
    struct OutputSlot {
        float value[4];           // 最大4値（単一, RGB, RGBW等）
        uint8_t dirty;            // 更新フラグ
    };
    OutputSlot outputs[MAX_OUTPUTS];
    
    // === キャンバス ===
    struct CanvasSlot {
        uint8_t buffer[MAX_CANVAS_SIZE];
        uint16_t width, height;
        uint8_t bpp;
        uint8_t dirty;
    };
    CanvasSlot canvas;
    
    // === オーディオ ===
    struct Audio {
        float output[2][BUFFER_SIZE];
        float input[2][BUFFER_SIZE];
    };
    Audio audio;
    
    // === イベント ===
    SpscQueue<Event, 64> events;
    
    // === パラメータ（Processor用） ===
    std::atomic<float> params[MAX_PARAMS];
};
```

### 同期モデル

| データ | Writer | Reader | 方式 |
|--------|--------|--------|------|
| `inputs[]` | Kernel | App | 単一writer、flag通知 |
| `outputs[]` | App | Kernel | dirty flag + flush |
| `canvas` | App | Kernel | dirty + flush |
| `audio` | App (process) | Kernel (DMA) | ダブルバッファ |
| `events` | Kernel | App | SpscQueue |
| `params` | App (main) | App (process) | atomic relaxed |

---

## BSPによるマッピング

BSPが論理インデックス→物理デバイスのマッピングを定義します。
テンプレートベースの設計により、属性あり/なしをコンパイル時に切り替え可能です。

### 基本型

```cpp
#include <bsp/io_types.hh>

namespace bsp::io {

// ハードウェア種別
enum class HwType : uint8_t {
    None = 0,
    // 入力
    Adc, Gpio, Encoder, Touch,
    // 出力
    Pwm, PwmRgb, GpioOut, I2c7Seg, SpiOled,
};

// 値の表示タイプ（UIでの値フォーマット用）
// カスタム表示は unit 文字列とアプリ側フォーマッタで拡張可能
enum class ValueType : uint8_t {
    None,       // 生の整数値 (unit文字列で単位表示)
    Percent,    // パーセント (0-100%)
    Bipolar,    // バイポーラー (-100〜+100, PanはL-C-R表示)
    Db,         // デシベル (dB)
    Frequency,  // 周波数 (Hz/kHz自動スケール)
    Time,       // 時間 (ms/s自動スケール)
    Note,       // 音程 (C0-G10 または ±セミトーン)
    Enum,       // 列挙インデックス (文字列テーブル使用)
    Toggle,     // ブール (On/Off, ラベルで表示)
};

// カーブ種別 - 生値から出力値へのマッピング
enum class Curve : uint8_t {
    Linear,     // 線形 (デフォルト)
    Log,        // 対数 (オーディオテーパー: 音量, 周波数)
    Exp,        // 指数 (アタック/ディケイタイム向け)
    Toggle,     // 二値トグル (50%で閾値)
};

// 極性 - 中心点の挙動
enum class Polarity : uint8_t {
    Unipolar,   // 0 〜 max (中心 = min)
    Bipolar,    // -max 〜 +max (中心 = 0)
};

// 出力動作ヒント - 値変化への出力の反応
enum class Animation : uint8_t {
    None,       // 即時更新 (静的)
    Smooth,     // 補間遷移 (フェード/スルー)
    Blink,      // 値に比例したレートで点滅
    Meter,      // ピークホールド + ディケイ (レベルメーター)
};

}
```

### 入力属性

属性はディスプレイ表示やパラメータ割り当て用のメタデータです。
`int16_t` でスケール値を格納し、実使用時にアプリでスケーリングします。

```cpp
// フル属性（ディスプレイ付きデバイス向け）
struct InputAttrs {
    const char* name;       // "Filter Cutoff"
    const char* label;      // "CUT" (短縮形)
    const char* unit;       // "Hz", "%"
    int16_t min;            // 最小値（スケール済み）
    int16_t max;            // 最大値（スケール済み）
    int16_t center;         // バイポーラー中心値 (= min for unipolar)
    int16_t init;           // デフォルト値
    ValueType type;         // 表示タイプ
    Curve curve;            // レスポンスカーブ
    Polarity polarity;      // Unipolar / Bipolar
    uint8_t frac;           // 小数桁数（0=整数, 1=0.1刻み, 2=0.01刻み）
};

// 出力属性
struct OutputAttrs {
    const char* name;
    const char* label;
    int16_t min;            // 出力最小値（スケール済み）
    int16_t max;            // 出力最大値（スケール済み）
    Animation anim;         // アニメーション種別
};

// スケーリング例:
//   min=0, max=1000, frac=1  → 0.0〜100.0%
//   min=-600, max=0, frac=1  → -60.0〜0.0 dB
//   min=20, max=20000, frac=0 → 20〜20000 Hz（整数）
```

### ファクトリ関数

デバイス種別ごとにファクトリ関数を提供。属性の有無で自動的にオーバーロードが選択されます。

```cpp
// === 入力 (属性なし) ===
adc(hw_id)                              // ADC入力
adc(hw_id, Curve::Log, Polarity::Unipolar)  // カーブ・極性指定
button(hw_id)                           // ボタン/スイッチ
button(hw_id, threshold, inverted)      // 閾値・反転指定
encoder(hw_id)                          // エンコーダ
encoder(hw_id, scale, wrap, detent)     // スケール、ラップ、デテント指定
touch(hw_id)                            // タッチセンサー

// === 入力 (属性あり) ===
adc(name, label, hw_id, min, max, type, curve, unit)  // シンプル版
adc_bipolar(name, label, hw_id, min, max, center, type, curve, unit)  // バイポーラー

// 構造体版（フル指定）
adc(AdcParams{
    .name = "Filter Cutoff",
    .label = "CUT",
    .hw_id = 1,
    .min = 20,          // 20Hz
    .max = 20000,       // 20kHz
    .center = 20,       // = min for unipolar
    .init = 1000,       // デフォルト値 1kHz
    .type = ValueType::Frequency,
    .curve = Curve::Log,
    .polarity = Polarity::Unipolar,
    .unit = "Hz",
    .frac = 0,          // 整数表示
});

button(name, label, hw_id, threshold, inverted)
encoder(name, label, hw_id, min, max, scale, wrap, detent)
touch(name, label, hw_id, threshold)

// === 出力 (属性なし) ===
led(hw_id)                              // PWM LED
led(hw_id, Animation::Smooth)           // スムーズ遷移指定
led_onoff(hw_id)                        // GPIO LED（ON/OFF）
rgb(hw_id)                              // RGB LED

// === 出力 (属性あり) ===
led(name, label, hw_id, Animation::Smooth)
led_meter(name, label, hw_id, min_db, max_db)   // レベルメーター
led_blink(name, label, hw_id)                   // 点滅LED
rgb(name, label, hw_id, Animation::Smooth)
```

### BSP定義例（高機能デバイス）

```cpp
// lib/bsp/synth_pro/io_mapping.hh

#pragma once
#include <bsp/io_types.hh>

namespace bsp::synth_pro {
using namespace bsp::io;

// === 入力定義（属性あり） ===
constexpr auto inputs = make_inputs(
    // Volume: 0-100% (frac=0, 整数パーセント)
    adc("Master Volume", "VOL", 0, 0, 100, ValueType::Percent, Curve::Linear, "%"),
    
    // Filter: 20Hz〜20000Hz (対数レスポンス)
    adc("Filter Cutoff", "CUT", 1, 20, 20000, ValueType::Frequency, Curve::Log, "Hz"),
    
    // Pan: -100〜+100 (バイポーラー、L-C-R表示)
    adc_bipolar("Pan", "PAN", 2, -100, 100, 0, ValueType::Bipolar, Curve::Linear),
    
    // ボタン
    button("Trigger", "TRG", 0),
    
    // エンコーダ: 0-127
    encoder("Menu", "ENC", 0, 0, 127, 1.0f, false, 0)
);

// === 出力定義（属性あり） ===
constexpr auto outputs = make_outputs(
    led("Volume", "VOL", 0, Animation::Smooth),
    led_meter("Level L", "L", 1, -600, 0),  // -60.0〜0.0 dB (frac=1)
    led_meter("Level R", "R", 2, -600, 0),  // -60.0〜0.0 dB (frac=1)
    led_blink("Status", "ST", 0),
    rgb("Mode", "RGB", 0, Animation::Smooth)
);

// === Canvas ===
constexpr CanvasConfig canvas = {
    .hw_type = HwType::SpiOled,
    .width = 128,
    .height = 64,
    .bpp = 1,
};

// === id シンボル ===
namespace in {
    constexpr uint8_t volume    = 0;
    constexpr uint8_t cutoff    = 1;
    constexpr uint8_t pan       = 2;
    constexpr uint8_t trigger   = 3;
    constexpr uint8_t menu      = 4;
    constexpr uint8_t count     = inputs.size();
}

namespace out {
    constexpr uint8_t volume_led = 0;
    constexpr uint8_t level_l    = 1;
    constexpr uint8_t level_r    = 2;
    constexpr uint8_t status     = 3;
    constexpr uint8_t mode_rgb   = 4;
    constexpr uint8_t count      = outputs.size();
}

// === コンパイル時検証 ===
static_assert(validate_no_duplicate_hw(inputs), "duplicate input hw_id");
static_assert(validate_no_duplicate_hw(outputs), "duplicate output hw_id");

}
```

### BSP定義例（低リソースデバイス）

```cpp
// lib/bsp/synth_mini/io_mapping.hh

namespace bsp::synth_mini {
using namespace bsp::io;

// 属性なし版（メモリ節約）
constexpr auto inputs = make_inputs(
    adc(0),
    adc(1, Curve::Log),
    button(0),
    encoder(0)
);

constexpr auto outputs = make_outputs(
    led(0),
    led_onoff(1)
);

namespace in {
    constexpr uint8_t volume  = 0;
    constexpr uint8_t cutoff  = 1;
    constexpr uint8_t trigger = 2;
    constexpr uint8_t menu    = 3;
    constexpr uint8_t count   = inputs.size();
}

static_assert(validate_no_duplicate_hw(inputs));

}
```

### 統一アクセス

属性の有無に関わらず、同じAPIでアクセスできます。

```cpp
void update_display() {
    for (uint8_t i = 0; i < in::count; ++i) {
        const auto& def = inputs[i];
        
        // 属性なしなら nullptr が返る
        const char* lbl = def.label();
        if (lbl) {
            canvas.text(0, i * 12, lbl);
        } else {
            canvas.text(0, i * 12, "IN");
            canvas.number(20, i * 12, i);
        }
        
        canvas.number(40, i * 12, input[i] * 100);
    }
}
```

### メモリ効率

| 構成 | InputDef サイズ | 5入力の合計 |
|------|-----------------|-------------|
| 属性なし | ~12 bytes | ~60 bytes |
| 属性あり | ~40 bytes | ~200 bytes + 文字列 |

### コンパイル時検証

```cpp
// hw_id の重複検出
static_assert(validate_no_duplicate_hw(inputs), "duplicate hw_id");

// 要素数の整合性
static_assert(in::count == inputs.size(), "count mismatch");
```

---

## 入力の更新フロー

```
Hardware          Kernel Driver         Shared Memory        Application
   │                    │                     │                    │
   │ ADC/GPIO/TIM IRQ   │                     │                    │
   │───────────────────>│                     │                    │
   │                    │ BSPマッピング参照    │                    │
   │                    │ ノイズ除去、正規化   │                    │
   │                    │ カーブ適用          │                    │
   │                    │                     │                    │
   │                    │ inputs[i].value = v │                    │
   │                    │ inputs[i].flags |= CHANGED               │
   │                    │────────────────────>│                    │
   │                    │                     │                    │
   │                    │ events.push(InputChanged, i)             │
   │                    │────────────────────>│                    │
   │                    │                     │                    │
   │                    │                     │ wait_event()       │
   │                    │                     │───────────────────>│
   │                    │                     │ input[i]           │
   │                    │                     │<───────────────────│
```

---

## 出力の更新フロー

```
Application          Shared Memory        Kernel Driver         Hardware
   │                      │                    │                    │
   │ output[0] = 0.8f     │                    │                    │
   │─────────────────────>│                    │                    │
   │ outputs[0].value[0]=0.8                   │                    │
   │ outputs[0].dirty=true│                    │                    │
   │                      │                    │                    │
   │ output.flush()       │                    │                    │
   │─────────────────────>│                    │                    │
   │                      │ BSPマッピング参照  │                    │
   │                      │<───────────────────│                    │
   │                      │ outputs[0]→PWM CH0 │                    │
   │                      │ outputs[2]→RGB PWM │                    │
   │                      │                    │───────────────────>│
   │                      │ dirty = false      │                    │
```

---

## プラットフォーム統一

| 操作 | 組込み | Web | Desktop |
|------|--------|-----|---------|
| `input[i]` | 共有メモリ | JS→WASM | MIDI CC / GUI |
| `output[i] = v` | 共有メモリ | Canvas/CSS | Log / GUI |
| `canvas.set()` | 共有メモリ | Canvas | Window |
| `flush()` | syscall→DMA | requestAnimationFrame | Render |

アプリコードは**完全に同一**。BSP/ランタイムがプラットフォーム差を吸収。

---

## 備考: フォースフィードバック

モーター付きフェーダーなど、入力と出力が結合されたデバイスは特殊ケースです。  
これらは将来のバージョンで専用APIを検討予定：

```cpp
// 将来の拡張案
class ForceFeedback {
    float get(uint8_t id) const;      // 現在位置を読む
    void set(uint8_t id, float pos);  // 目標位置を設定
    void set_force(uint8_t id, float force);  // 抵抗力を設定
};
```

---

## 関連ドキュメント

- [API.md](API.md) - API インデックス
- [API_APPLICATION.md](API_APPLICATION.md) - アプリケーションAPI
- [API_DSP.md](API_DSP.md) - DSPモジュール
- [API_KERNEL.md](API_KERNEL.md) - Kernel API

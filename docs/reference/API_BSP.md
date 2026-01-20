# BSP I/O 型定義

BSP (Board Support Package) が論理インデックス→物理デバイスのマッピングを定義します。
テンプレートベースの設計により、属性あり/なしをコンパイル時に切り替え可能です。

```cpp
#include <bsp/io_types.hh>
```

---

## 基本型

### ハードウェア種別

```cpp
namespace bsp::io {

enum class HwType : uint8_t {
    None = 0,
    // 入力
    Adc,        // ADC入力 (ポテンショメータ、スライダー)
    Gpio,       // GPIO入力 (ボタン、スイッチ)
    Encoder,    // ロータリーエンコーダ
    Touch,      // タッチセンサー / 静電容量ボタン
    // 出力
    Pwm,        // PWM出力 (LED輝度)
    PwmRgb,     // RGB LED (3チャンネルPWM)
    GpioOut,    // GPIO出力 (LED ON/OFF)
    I2c7Seg,    // I2C 7セグメントディスプレイ
    SpiOled,    // SPI OLED ディスプレイ
};

}
```

### 値の表示タイプ

UIでの値フォーマット用。カスタム表示は `unit` 文字列とアプリ側フォーマッタで拡張可能。

```cpp
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
```

### カーブ種別

生値から出力値へのマッピング。

```cpp
enum class Curve : uint8_t {
    Linear,     // 線形 (デフォルト)
    Log,        // 対数 (オーディオテーパー: 音量, 周波数)
    Exp,        // 指数 (アタック/ディケイタイム向け)
    Toggle,     // 二値トグル (50%で閾値)
};
```

### 極性

中心点の挙動を決定。

```cpp
enum class Polarity : uint8_t {
    Unipolar,   // 0 〜 max (中心 = min)
    Bipolar,    // -max 〜 +max (中心 = 0)
};
```

### 出力動作ヒント

値変化への出力の反応を定義。

```cpp
enum class Animation : uint8_t {
    None,       // 即時更新 (静的)
    Smooth,     // 補間遷移 (フェード/スルー)
    Blink,      // 値に比例したレートで点滅
    Meter,      // ピークホールド + ディケイ (レベルメーター)
};
```

---

## 入力属性

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
```

### スケーリング規約

| min | max | frac | 実際の範囲 |
|-----|-----|------|-----------|
| 0 | 1000 | 1 | 0.0〜100.0% |
| -600 | 0 | 1 | -60.0〜0.0 dB |
| 20 | 20000 | 0 | 20〜20000 Hz (整数) |
| -24 | 24 | 0 | -24〜+24 セミトーン |

---

## ファクトリ関数

デバイス種別ごとにファクトリ関数を提供。属性の有無で自動的にオーバーロードが選択されます。

### 入力 (属性なし)

```cpp
adc(hw_id)                              // ADC入力
adc(hw_id, Curve::Log, Polarity::Unipolar)  // カーブ・極性指定
button(hw_id)                           // ボタン/スイッチ
button(hw_id, threshold, inverted)      // 閾値・反転指定
encoder(hw_id)                          // エンコーダ
encoder(hw_id, scale, wrap, detent)     // スケール、ラップ、デテント指定
touch(hw_id)                            // タッチセンサー
```

### 入力 (属性あり)

```cpp
// シンプル版
adc(name, label, hw_id, min, max, type, curve, unit)

// バイポーラー
adc_bipolar(name, label, hw_id, min, max, center, type, curve, unit)

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
```

### 出力 (属性なし)

```cpp
led(hw_id)                              // PWM LED
led(hw_id, Animation::Smooth)           // スムーズ遷移指定
led_onoff(hw_id)                        // GPIO LED（ON/OFF）
rgb(hw_id)                              // RGB LED
```

### 出力 (属性あり)

```cpp
led(name, label, hw_id, Animation::Smooth)
led_meter(name, label, hw_id, min_db, max_db)   // レベルメーター
led_blink(name, label, hw_id)                   // 点滅LED
rgb(name, label, hw_id, Animation::Smooth)
```

---

## BSP定義例

### 高機能デバイス（属性あり）

```cpp
// lib/bsp/synth_pro/io_mapping.hh

#pragma once
#include <bsp/io_types.hh>

namespace bsp::synth_pro {
using namespace bsp::io;

// === 入力定義 ===
constexpr auto inputs = make_inputs(
    // Volume: 0-100%
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

// === 出力定義 ===
constexpr auto outputs = make_outputs(
    led("Volume", "VOL", 0, Animation::Smooth),
    led_meter("Level L", "L", 1, -600, 0),  // -60.0〜0.0 dB
    led_meter("Level R", "R", 2, -600, 0),
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

### 低リソースデバイス（属性なし）

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

---

## 統一アクセス

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

---

## メモリ効率

| 構成 | InputDef サイズ | 5入力の合計 |
|------|-----------------|-------------|
| 属性なし | ~12 bytes | ~60 bytes |
| 属性あり | ~40 bytes | ~200 bytes + 文字列 |

---

## コンパイル時検証

```cpp
// hw_id の重複検出
static_assert(validate_no_duplicate_hw(inputs), "duplicate hw_id");

// 要素数の整合性
static_assert(in::count == inputs.size(), "count mismatch");
```

---

## 関連ドキュメント

- [API_UI.md](API_UI.md) - UI API（Input/Output/Canvas）
- [API_KERNEL.md](API_KERNEL.md) - Kernel API
- [../specs/ARCHITECTURE.md](../specs/ARCHITECTURE.md) - アーキテクチャ

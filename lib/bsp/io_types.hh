// SPDX-License-Identifier: MIT
// UMI-OS - Universal Musical Instruments Operating System
// BSP I/O Types - Type-safe input/output mapping definitions
//
// 入力/出力の属性は表示・割り当て用のメタデータ。
// 省メモリのため int16_t を使用。実使用時はアプリで拡大縮小する。

#pragma once

#include <cstdint>
#include <array>
#include <type_traits>

namespace bsp::io {

// ============================================================================
// Hardware Types
// ============================================================================

/// Hardware device type enumeration
enum class HwType : uint8_t {
    None = 0,
    // Input types
    Adc,        ///< Analog-to-digital converter (potentiometer, slider)
    Gpio,       ///< GPIO input (button, switch)
    Encoder,    ///< Rotary encoder
    Touch,      ///< Touch sensor / capacitive button
    // Output types
    Pwm,        ///< PWM output (LED brightness)
    PwmRgb,     ///< RGB LED (3-channel PWM)
    GpioOut,    ///< GPIO output (LED on/off)
    I2c7Seg,    ///< I2C 7-segment display
    SpiOled,    ///< SPI OLED display
};

// ============================================================================
// Value Types (for display formatting)
// ============================================================================

/// Value display type - core categories for musical instruments
/// カスタム表示は unit 文字列とアプリ側フォーマッタで拡張可能
enum class ValueType : uint8_t {
    None = 0,       ///< Raw integer value (use unit string for suffix)
    Percent,        ///< Percentage (0-100%)
    Bipolar,        ///< Bipolar display (-100 to +100, or L-C-R for pan)
    Db,             ///< Decibels (dB)
    Frequency,      ///< Frequency (auto-scale Hz/kHz)
    Time,           ///< Time (auto-scale ms/s)
    Note,           ///< Musical note (C0-G10, or semitone offset)
    Enum,           ///< Enumeration index (uses string table)
    Toggle,         ///< Boolean (On/Off, displayed via label)
};

// ============================================================================
// Curve Types (response shaping)
// ============================================================================

/// Input response curve - how raw ADC/encoder values map to output
enum class Curve : uint8_t {
    Linear,     ///< Linear response (default)
    Log,        ///< Logarithmic (audio taper: volume, frequency)
    Exp,        ///< Exponential (inverse log: attack/decay time)
    Toggle,     ///< Binary toggle (threshold at 50%)
};

/// Value polarity - determines center point behavior
enum class Polarity : uint8_t {
    Unipolar,   ///< 0 to max (center = min)
    Bipolar,    ///< -max to +max (center = 0)
};

// ============================================================================
// Output Types (for behavior hints)
// ============================================================================

/// Output behavior hint - how the output should respond to value changes
enum class Animation : uint8_t {
    None,       ///< Immediate update (static)
    Smooth,     ///< Interpolated transitions (fade/slew)
    Blink,      ///< Periodic on/off at rate proportional to value
    Meter,      ///< Peak-hold with decay (level meter)
};

// ============================================================================
// Mapping Structures
// ============================================================================

/// Input hardware mapping
struct InputMapping {
    HwType hw_type = HwType::None;
    uint8_t hw_id = 0;
    uint8_t id = 0;  // Logical ID (auto-assigned by make_inputs)
    
    // Device-specific parameters (union to save memory)
    union Params {
        struct { Curve curve; Polarity polarity; } adc;
        struct { float threshold; bool inverted; } gpio;
        struct { float scale; bool wrap; int16_t detent; } encoder;
        struct { float threshold; } touch;
        
        constexpr Params() : adc{Curve::Linear, Polarity::Unipolar} {}
    } params = {};
};

/// Output hardware mapping
struct OutputMapping {
    HwType hw_type = HwType::None;
    uint8_t hw_id = 0;
    uint8_t id = 0;
    uint8_t channels = 1;  // 1=mono, 3=RGB, 4=RGBW
    Animation anim = Animation::None;
};

/// Canvas (display) configuration
struct CanvasConfig {
    HwType hw_type = HwType::SpiOled;
    uint8_t hw_id = 0;
    uint16_t width = 128;
    uint16_t height = 64;
    uint8_t bpp = 1;  // Bits per pixel
};

// ============================================================================
// Attributes (Full version for high-end devices)
// ============================================================================

/// Full input attributes for devices with displays
/// 値は int16_t で格納し、実使用時にアプリでスケーリングする
struct InputAttrs {
    const char* name = nullptr;      ///< Full name: "Filter Cutoff"
    const char* label = nullptr;     ///< Short label: "CUT" (for LEDs/7seg)
    const char* unit = nullptr;      ///< Unit suffix: "Hz", "%"
    int16_t min = 0;                 ///< Minimum value (scaled)
    int16_t max = 1000;              ///< Maximum value (scaled, e.g. 1000 = 100.0%)
    int16_t center = 0;              ///< Center for bipolar (= min for unipolar)
    int16_t init = 0;                ///< Initial/default value
    ValueType type = ValueType::None; ///< Display format type
    Curve curve = Curve::Linear;     ///< Response curve
    Polarity polarity = Polarity::Unipolar; ///< Unipolar or bipolar
    uint8_t frac = 0;                ///< Fractional digits (0-3) for display
};

/// Full output attributes
struct OutputAttrs {
    const char* name = nullptr;
    const char* label = nullptr;
    int16_t min = 0;                 ///< Minimum output value (scaled)
    int16_t max = 1000;              ///< Maximum output value (scaled)
    Animation anim = Animation::None; ///< Animation type
};

/// Empty attributes for minimal builds (zero size with [[no_unique_address]])
struct NoAttrs {};

// ============================================================================
// Input Definition (Template)
// ============================================================================

template<bool WithAttrs>
struct InputDef;

/// InputDef without attributes (minimal memory footprint)
template<>
struct InputDef<false> {
    InputMapping mapping;
    
    [[nodiscard]] constexpr const char* name() const { return nullptr; }
    [[nodiscard]] constexpr const char* label() const { return nullptr; }
    [[nodiscard]] constexpr const char* unit() const { return nullptr; }
    [[nodiscard]] constexpr int16_t min() const { return 0; }
    [[nodiscard]] constexpr int16_t max() const { return 1000; }
    [[nodiscard]] constexpr int16_t center() const { return 0; }
    [[nodiscard]] constexpr int16_t init() const { return 0; }
    [[nodiscard]] constexpr ValueType type() const { return ValueType::None; }
    [[nodiscard]] constexpr Curve curve() const { return Curve::Linear; }
    [[nodiscard]] constexpr Polarity polarity() const { return Polarity::Unipolar; }
    [[nodiscard]] constexpr bool is_bipolar() const { return false; }
};

/// InputDef with full attributes
template<>
struct InputDef<true> {
    InputAttrs attrs;
    InputMapping mapping;
    
    [[nodiscard]] constexpr const char* name() const { return attrs.name; }
    [[nodiscard]] constexpr const char* label() const { return attrs.label; }
    [[nodiscard]] constexpr const char* unit() const { return attrs.unit; }
    [[nodiscard]] constexpr int16_t min() const { return attrs.min; }
    [[nodiscard]] constexpr int16_t max() const { return attrs.max; }
    [[nodiscard]] constexpr int16_t center() const { return attrs.center; }
    [[nodiscard]] constexpr int16_t init() const { return attrs.init; }
    [[nodiscard]] constexpr ValueType type() const { return attrs.type; }
    [[nodiscard]] constexpr Curve curve() const { return attrs.curve; }
    [[nodiscard]] constexpr Polarity polarity() const { return attrs.polarity; }
    [[nodiscard]] constexpr bool is_bipolar() const { return attrs.polarity == Polarity::Bipolar; }
};

// ============================================================================
// Output Definition (Template)
// ============================================================================

template<bool WithAttrs>
struct OutputDef;

/// OutputDef without attributes
template<>
struct OutputDef<false> {
    OutputMapping mapping;
    
    [[nodiscard]] constexpr const char* name() const { return nullptr; }
    [[nodiscard]] constexpr const char* label() const { return nullptr; }
    [[nodiscard]] constexpr int16_t min() const { return 0; }
    [[nodiscard]] constexpr int16_t max() const { return 1000; }
    [[nodiscard]] constexpr Animation animation() const { return Animation::None; }
};

/// OutputDef with full attributes
template<>
struct OutputDef<true> {
    OutputAttrs attrs;
    OutputMapping mapping;
    
    [[nodiscard]] constexpr const char* name() const { return attrs.name; }
    [[nodiscard]] constexpr const char* label() const { return attrs.label; }
    [[nodiscard]] constexpr int16_t min() const { return attrs.min; }
    [[nodiscard]] constexpr int16_t max() const { return attrs.max; }
    [[nodiscard]] constexpr Animation animation() const { return attrs.anim; }
};

// ============================================================================
// Factory Functions - Input (No Attributes)
// ============================================================================

/// Create ADC input definition (potentiometer, slider)
[[nodiscard]] constexpr InputDef<false> adc(uint8_t hw_id, Curve curve = Curve::Linear, 
                                            Polarity polarity = Polarity::Unipolar) {
    InputDef<false> def{};
    def.mapping.hw_type = HwType::Adc;
    def.mapping.hw_id = hw_id;
    def.mapping.params.adc = {curve, polarity};
    return def;
}

/// Create GPIO button input definition
[[nodiscard]] constexpr InputDef<false> button(uint8_t hw_id, float threshold = 0.5f, 
                                                bool inverted = false) {
    InputDef<false> def{};
    def.mapping.hw_type = HwType::Gpio;
    def.mapping.hw_id = hw_id;
    def.mapping.params.gpio = {threshold, inverted};
    return def;
}

/// Create encoder input definition
[[nodiscard]] constexpr InputDef<false> encoder(uint8_t hw_id, float scale = 0.01f, 
                                                 bool wrap = false, int16_t detent = 0) {
    InputDef<false> def{};
    def.mapping.hw_type = HwType::Encoder;
    def.mapping.hw_id = hw_id;
    def.mapping.params.encoder = {scale, wrap, detent};
    return def;
}

/// Create touch sensor input definition
[[nodiscard]] constexpr InputDef<false> touch(uint8_t hw_id, float threshold = 0.5f) {
    InputDef<false> def{};
    def.mapping.hw_type = HwType::Touch;
    def.mapping.hw_id = hw_id;
    def.mapping.params.touch = {threshold};
    return def;
}

// ============================================================================
// Factory Functions - Input (With Full Attributes)
// ============================================================================

/// ADC input with full parameter attributes
struct AdcParams {
    const char* name;
    const char* label;
    uint8_t hw_id;
    int16_t min = 0;
    int16_t max = 1000;       // e.g. 1000 = 100.0% with frac=1
    int16_t center = 0;       // = min for unipolar
    int16_t init = 0;         // default value
    ValueType type = ValueType::None;
    Curve curve = Curve::Linear;
    Polarity polarity = Polarity::Unipolar;
    const char* unit = nullptr;
    uint8_t frac = 0;         // fractional digits for display
};

[[nodiscard]] constexpr InputDef<true> adc(const AdcParams& p) {
    InputDef<true> def{};
    def.attrs = {p.name, p.label, p.unit, p.min, p.max, 
                 p.polarity == Polarity::Bipolar ? p.center : p.min,
                 p.init, p.type, p.curve, p.polarity, p.frac};
    def.mapping.hw_type = HwType::Adc;
    def.mapping.hw_id = p.hw_id;
    def.mapping.params.adc = {p.curve, p.polarity};
    return def;
}

/// Simplified ADC with common parameters
[[nodiscard]] constexpr InputDef<true> adc(
    const char* name, const char* label,
    uint8_t hw_id, 
    int16_t min_val, int16_t max_val,
    ValueType type = ValueType::None,
    Curve curve = Curve::Linear,
    const char* unit = nullptr,
    uint8_t frac = 0
) {
    InputDef<true> def{};
    def.attrs = {name, label, unit, min_val, max_val, min_val, min_val, type, curve, Polarity::Unipolar, frac};
    def.mapping.hw_type = HwType::Adc;
    def.mapping.hw_id = hw_id;
    def.mapping.params.adc = {curve, Polarity::Unipolar};
    return def;
}

/// Bipolar ADC (e.g., pan, pitch bend)
[[nodiscard]] constexpr InputDef<true> adc_bipolar(
    const char* name, const char* label,
    uint8_t hw_id,
    int16_t min_val, int16_t max_val, int16_t center_val,
    ValueType type = ValueType::None,
    Curve curve = Curve::Linear,
    const char* unit = nullptr,
    uint8_t frac = 0
) {
    InputDef<true> def{};
    def.attrs = {name, label, unit, min_val, max_val, center_val, center_val, type, curve, Polarity::Bipolar, frac};
    def.mapping.hw_type = HwType::Adc;
    def.mapping.hw_id = hw_id;
    def.mapping.params.adc = {curve, Polarity::Bipolar};
    return def;
}

/// Create GPIO button input definition with attributes
[[nodiscard]] constexpr InputDef<true> button(
    const char* name, const char* label,
    uint8_t hw_id, float threshold = 0.5f, bool inverted = false
) {
    InputDef<true> def{};
    def.attrs = {name, label, nullptr, 0, 1, 0, 0, ValueType::Toggle, Curve::Toggle, Polarity::Unipolar, 0};
    def.mapping.hw_type = HwType::Gpio;
    def.mapping.hw_id = hw_id;
    def.mapping.params.gpio = {threshold, inverted};
    return def;
}

/// Create encoder input definition with attributes
[[nodiscard]] constexpr InputDef<true> encoder(
    const char* name, const char* label,
    uint8_t hw_id, 
    int16_t min_val, int16_t max_val,
    float scale = 0.01f, bool wrap = false, int16_t detent = 0
) {
    InputDef<true> def{};
    def.attrs = {name, label, nullptr, min_val, max_val, min_val, min_val, ValueType::None, Curve::Linear, Polarity::Unipolar, 0};
    def.mapping.hw_type = HwType::Encoder;
    def.mapping.hw_id = hw_id;
    def.mapping.params.encoder = {scale, wrap, detent};
    return def;
}

/// Create touch sensor input definition with attributes
[[nodiscard]] constexpr InputDef<true> touch(
    const char* name, const char* label,
    uint8_t hw_id, float threshold = 0.5f
) {
    InputDef<true> def{};
    def.attrs = {name, label, nullptr, 0, 1, 0, 0, ValueType::Toggle, Curve::Toggle, Polarity::Unipolar, 0};
    def.mapping.hw_type = HwType::Touch;
    def.mapping.hw_id = hw_id;
    def.mapping.params.touch = {threshold};
    return def;
}

// ============================================================================
// Factory Functions - Output (No Attributes)
// ============================================================================

/// Create PWM LED output definition
[[nodiscard]] constexpr OutputDef<false> led(uint8_t hw_id, Animation anim = Animation::None) {
    return {{.hw_type = HwType::Pwm, .hw_id = hw_id, .channels = 1, .anim = anim}};
}

/// Create GPIO LED output definition (on/off only)
[[nodiscard]] constexpr OutputDef<false> led_onoff(uint8_t hw_id) {
    return {{.hw_type = HwType::GpioOut, .hw_id = hw_id, .channels = 1, .anim = Animation::None}};
}

/// Create RGB LED output definition
[[nodiscard]] constexpr OutputDef<false> rgb(uint8_t hw_id, Animation anim = Animation::None) {
    return {{.hw_type = HwType::PwmRgb, .hw_id = hw_id, .channels = 3, .anim = anim}};
}

/// Create 7-segment display output definition
[[nodiscard]] constexpr OutputDef<false> seg7(uint8_t hw_id) {
    return {{.hw_type = HwType::I2c7Seg, .hw_id = hw_id, .channels = 1, .anim = Animation::None}};
}

// ============================================================================
// Factory Functions - Output (With Attributes)
// ============================================================================

/// Create PWM LED output definition with attributes
[[nodiscard]] constexpr OutputDef<true> led(const char* name, const char* label, 
                                            uint8_t hw_id, Animation anim = Animation::Smooth) {
    return {{name, label, 0, 1000, anim},
            {.hw_type = HwType::Pwm, .hw_id = hw_id, .channels = 1, .anim = anim}};
}

/// Create PWM LED for level meter (min/max in scaled dB, e.g. -600 to 0 for -60.0 to 0.0 dB)
[[nodiscard]] constexpr OutputDef<true> led_meter(const char* name, const char* label,
                                                   uint8_t hw_id, int16_t min_db = -600, int16_t max_db = 0) {
    return {{name, label, min_db, max_db, Animation::Meter},
            {.hw_type = HwType::Pwm, .hw_id = hw_id, .channels = 1, .anim = Animation::Meter}};
}

/// Create GPIO LED output definition with attributes
[[nodiscard]] constexpr OutputDef<true> led_onoff(const char* name, const char* label, uint8_t hw_id) {
    return {{name, label, 0, 1, Animation::None},
            {.hw_type = HwType::GpioOut, .hw_id = hw_id, .channels = 1, .anim = Animation::None}};
}

/// Create blinking GPIO LED
[[nodiscard]] constexpr OutputDef<true> led_blink(const char* name, const char* label, uint8_t hw_id) {
    return {{name, label, 0, 1, Animation::Blink},
            {.hw_type = HwType::GpioOut, .hw_id = hw_id, .channels = 1, .anim = Animation::Blink}};
}

/// Create RGB LED output definition with attributes
[[nodiscard]] constexpr OutputDef<true> rgb(const char* name, const char* label, 
                                            uint8_t hw_id, Animation anim = Animation::Smooth) {
    return {{name, label, 0, 1000, anim},
            {.hw_type = HwType::PwmRgb, .hw_id = hw_id, .channels = 3, .anim = anim}};
}

/// Create 7-segment display output definition with attributes
[[nodiscard]] constexpr OutputDef<true> seg7(const char* name, const char* label, uint8_t hw_id) {
    return {{name, label, 0, 9999, Animation::None},
            {.hw_type = HwType::I2c7Seg, .hw_id = hw_id, .channels = 1, .anim = Animation::None}};
}

// ============================================================================
// Array Generation Helpers
// ============================================================================

/// Create input array with auto-assigned IDs
template<typename... Defs>
[[nodiscard]] constexpr auto make_inputs(Defs... defs) {
    using DefType = std::common_type_t<Defs...>;
    std::array<DefType, sizeof...(Defs)> result{defs...};
    for (uint8_t i = 0; i < result.size(); ++i) {
        result[i].mapping.id = i;
    }
    return result;
}

/// Create output array with auto-assigned IDs
template<typename... Defs>
[[nodiscard]] constexpr auto make_outputs(Defs... defs) {
    using DefType = std::common_type_t<Defs...>;
    std::array<DefType, sizeof...(Defs)> result{defs...};
    for (uint8_t i = 0; i < result.size(); ++i) {
        result[i].mapping.id = i;
    }
    return result;
}

// ============================================================================
// Compile-time Validation
// ============================================================================

/// Check for duplicate hardware IDs in an array
template<typename Array>
[[nodiscard]] constexpr bool validate_no_duplicate_hw(const Array& arr) {
    for (size_t i = 0; i < arr.size(); ++i) {
        for (size_t j = i + 1; j < arr.size(); ++j) {
            if (arr[i].mapping.hw_type == arr[j].mapping.hw_type &&
                arr[i].mapping.hw_id == arr[j].mapping.hw_id) {
                return false;
            }
        }
    }
    return true;
}

/// Validate array size matches expected count
template<typename Array, size_t ExpectedCount>
[[nodiscard]] constexpr bool validate_count(const Array& arr) {
    return arr.size() == ExpectedCount;
}

// ============================================================================
// Utility Functions
// ============================================================================

/// Get the InputMapping from any InputDef type
template<bool W>
[[nodiscard]] constexpr const InputMapping& get_mapping(const InputDef<W>& def) {
    return def.mapping;
}

/// Get the OutputMapping from any OutputDef type
template<bool W>
[[nodiscard]] constexpr const OutputMapping& get_mapping(const OutputDef<W>& def) {
    return def.mapping;
}

}  // namespace bsp::io

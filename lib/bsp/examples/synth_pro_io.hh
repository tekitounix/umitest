// SPDX-License-Identifier: MIT
// UMI-OS - Universal Musical Instruments Operating System
// Example BSP I/O Mapping - Synth Pro (with OLED display)
//
// 入力/出力属性は表示・割り当て用のメタデータ。
// int16_t でスケール値を格納し、実使用時にアプリでスケーリングする。
// 例: min=20, max=20000, frac=0 → 20Hz〜20000Hz

#pragma once

#include <bsp/io_types.hh>

namespace bsp::synth_pro {

using namespace bsp::io;

// ============================================================================
// Input Definitions (With Full Attributes for OLED Display)
// ============================================================================

constexpr auto inputs = make_inputs(
    // Volume: 0-100%
    adc("Master Volume", "VOL", 0, 0, 100, ValueType::Percent, Curve::Linear, "%"),
    
    // Filter: 20Hz〜20000Hz (対数レスポンス)
    adc("Filter Cutoff", "CUT", 1, 20, 20000, ValueType::Frequency, Curve::Log, "Hz"),
    
    // Resonance: 0-100%
    adc("Resonance", "RES", 2, 0, 100, ValueType::Percent, Curve::Linear),
    
    // Envelope times: 1ms〜2000ms, 10ms〜5000ms (対数)
    adc("Attack",  "ATK", 3, 1, 2000, ValueType::Time, Curve::Log, "ms"),
    adc("Release", "REL", 4, 10, 5000, ValueType::Time, Curve::Log, "ms"),
    
    // Pitch: -24〜+24 semitones (バイポーラー)
    adc_bipolar("Pitch Bend", "PIT", 5, -24, 24, 0, ValueType::Note, Curve::Linear),
    
    // Pan: -100〜+100 (バイポーラー、L-C-R表示)
    adc_bipolar("Pan", "PAN", 6, -100, 100, 0, ValueType::Bipolar, Curve::Linear),
    
    // Buttons
    button("Trigger", "TRG", 0),
    button("Mode",    "MOD", 1),
    
    // Encoder: 0-127
    encoder("Menu", "ENC", 0, 0, 127, 1.0f, false, 0)
);

namespace in {
    constexpr uint8_t volume    = 0;
    constexpr uint8_t cutoff    = 1;
    constexpr uint8_t resonance = 2;
    constexpr uint8_t attack    = 3;
    constexpr uint8_t release   = 4;
    constexpr uint8_t pitch     = 5;
    constexpr uint8_t pan       = 6;
    constexpr uint8_t trigger   = 7;
    constexpr uint8_t mode      = 8;
    constexpr uint8_t menu      = 9;
    constexpr uint8_t count     = inputs.size();
}

// ============================================================================
// Output Definitions (With Attributes and Animation)
// ============================================================================

constexpr auto outputs = make_outputs(
    led("Volume", "VOL", 0, Animation::Smooth),    // Smooth volume indicator
    led_meter("Level L", "L", 1, -600, 0),          // Left level meter (-60.0〜0.0 dB)
    led_meter("Level R", "R", 2, -600, 0),          // Right level meter
    led_blink("Status", "ST", 0),                    // Blinking status LED
    rgb("Mode", "RGB", 0, Animation::Smooth)         // Mode indicator with smooth
);

namespace out {
    constexpr uint8_t volume_led = 0;
    constexpr uint8_t level_l    = 1;
    constexpr uint8_t level_r    = 2;
    constexpr uint8_t status     = 3;
    constexpr uint8_t mode_rgb   = 4;
    constexpr uint8_t count      = outputs.size();
}

// ============================================================================
// Canvas (OLED Display)
// ============================================================================

constexpr CanvasConfig canvas = {
    .hw_type = HwType::SpiOled,
    .hw_id = 0,
    .width = 128,
    .height = 64,
    .bpp = 1,  // Monochrome
};

// ============================================================================
// Compile-time Validation
// ============================================================================

static_assert(validate_no_duplicate_hw(inputs), "duplicate input hw_id");
static_assert(validate_no_duplicate_hw(outputs), "duplicate output hw_id");
static_assert(in::count == inputs.size(), "input count mismatch");
static_assert(out::count == outputs.size(), "output count mismatch");

}  // namespace bsp::synth_pro

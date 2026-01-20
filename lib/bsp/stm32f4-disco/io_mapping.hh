// SPDX-License-Identifier: MIT
// UMI-OS - Universal Musical Instruments Operating System
// BSP I/O Mapping for STM32F4-Discovery Board

#pragma once

#include <bsp/io_types.hh>

namespace bsp::stm32f4_disco {

using namespace bsp::io;

// ============================================================================
// Input Definitions
// ============================================================================

// STM32F4-Discovery has:
// - User button (PA0)
// - Optional: ADC inputs on expansion headers

constexpr auto inputs = make_inputs(
    button(0, 0.5f)  // User button on PA0
);

namespace in {
    constexpr uint8_t user_button = 0;
    constexpr uint8_t count = inputs.size();
}

// ============================================================================
// Output Definitions
// ============================================================================

// STM32F4-Discovery has:
// - LD3 (Orange, PD13)
// - LD4 (Green, PD12)
// - LD5 (Red, PD14)
// - LD6 (Blue, PD15)

constexpr auto outputs = make_outputs(
    led_onoff(0),  // LD3 Orange
    led_onoff(1),  // LD4 Green
    led_onoff(2),  // LD5 Red
    led_onoff(3)   // LD6 Blue
);

namespace out {
    constexpr uint8_t led_orange = 0;
    constexpr uint8_t led_green  = 1;
    constexpr uint8_t led_red    = 2;
    constexpr uint8_t led_blue   = 3;
    constexpr uint8_t count = outputs.size();
}

// ============================================================================
// Compile-time Validation
// ============================================================================

static_assert(validate_no_duplicate_hw(inputs), "duplicate input hw_id");
static_assert(validate_no_duplicate_hw(outputs), "duplicate output hw_id");

}  // namespace bsp::stm32f4_disco

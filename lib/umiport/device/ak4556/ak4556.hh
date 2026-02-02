// SPDX-License-Identifier: MIT
// AK4556 Audio Codec Driver
// AK4556 is a register-less codec (no I2C/SPI control).
// Only requires a reset pin and stable clock.
#pragma once

#include <cstdint>

namespace umi::device {

/// AK4556 codec driver
/// Daisy Seed Rev4 uses this codec — no register interface.
/// Power-on sequence: hold reset low for >150ns, then release.
template <typename GpioDriver>
class AK4556 {
    GpioDriver& gpio;
    std::uint8_t reset_pin;

public:
    constexpr AK4556(GpioDriver& gpio_drv, std::uint8_t pin)
        : gpio(gpio_drv), reset_pin(pin) {}

    /// Assert reset (active low)
    void reset_assert() { gpio.reset_pin(reset_pin); }

    /// Release reset
    void reset_release() { gpio.set_pin(reset_pin); }

    /// Power-on sequence: assert reset, wait, release
    void init() {
        reset_assert();
        // Caller should provide delay of at least 150ns between assert/release
        // At 480MHz, a few NOPs suffice
        for (int i = 0; i < 100; ++i) { asm volatile("" ::: "memory"); }
        reset_release();
    }
};

} // namespace umi::device

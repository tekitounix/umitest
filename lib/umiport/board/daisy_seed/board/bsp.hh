// SPDX-License-Identifier: MIT
// Daisy Seed Board Support Package
#pragma once

#include <cstdint>
#include <mcu/rcc.hh>
#include <mcu/gpio.hh>
#include <mmio/transport/direct.hh>

namespace umi::daisy {

/// Board revision / codec variant
enum class BoardVersion : std::uint8_t {
    SEED_REV4,      // AK4556 (no I2C control)
    SEED_REV5,      // WM8731 (I2C: 0x1A)
    SEED_2_DFM,     // PCM3060 (I2C: 0x46)
};

/// Daisy Seed board constants
struct SeedBoard {
    // Clock
    static constexpr std::uint32_t hse_clock_hz = 16'000'000;    // 16 MHz HSE
    static constexpr std::uint32_t system_clock_hz = 480'000'000; // 480 MHz (boost)
    static constexpr std::uint32_t hclk_hz = 240'000'000;        // AHB = SYSCLK/2
    static constexpr std::uint32_t apb1_hz = 120'000'000;        // APB1 = HCLK/2
    static constexpr std::uint32_t apb2_hz = 120'000'000;        // APB2 = HCLK/2

    // Memory sizes
    static constexpr std::uint32_t internal_flash_size = 128 * 1024;
    static constexpr std::uint32_t dtcmram_size = 128 * 1024;
    static constexpr std::uint32_t sram_d1_size = 512 * 1024;
    static constexpr std::uint32_t sram_d2_size = 288 * 1024;
    static constexpr std::uint32_t sdram_size = 64 * 1024 * 1024;
    static constexpr std::uint32_t qspi_flash_size = 8 * 1024 * 1024;

    // LED pin: PC7
    static constexpr std::uint8_t led_port = 2;  // GPIOC (index)
    static constexpr std::uint8_t led_pin = 7;

    // Test point: PG14
    static constexpr std::uint8_t test_port = 6;  // GPIOG
    static constexpr std::uint8_t test_pin = 14;

    // I2C4 pins for codec control (PB6=SCL, PB7=SDA)
    static constexpr std::uint8_t i2c_scl_pin = 6;
    static constexpr std::uint8_t i2c_sda_pin = 7;
    static constexpr std::uint8_t i2c_af = 6;  // AF6 = I2C4

    // Codec reset pin: PB11
    static constexpr std::uint8_t codec_reset_pin = 11;
};

/// Detect board version by reading GPIO strapping pins.
/// PD3 low → Rev5 (WM8731), PD4 low → DFM (PCM3060), else → Rev4 (AK4556)
/// Must be called after GPIOD clock is enabled.
inline BoardVersion detect_board_version() {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> transport;

    // Enable GPIOD clock
    transport.modify(RCC::AHB4ENR::GPIODEN::Set{});
    [[maybe_unused]] auto dummy = transport.read(RCC::AHB4ENR{});

    // Configure PD3 and PD4 as input with pull-up
    gpio_configure_pin<GPIOD>(transport, 3,
                               gpio_mode::INPUT, gpio_otype::PUSH_PULL,
                               gpio_speed::LOW, gpio_pupd::PULL_UP);
    gpio_configure_pin<GPIOD>(transport, 4,
                               gpio_mode::INPUT, gpio_otype::PUSH_PULL,
                               gpio_speed::LOW, gpio_pupd::PULL_UP);

    // Brief delay for pull-ups to settle
    for (int i = 0; i < 100; ++i) { asm volatile("" ::: "memory"); }

    auto idr = transport.read(GPIOD::IDR{});

    if (!(idr & (1U << 3))) {
        return BoardVersion::SEED_REV5;
    }
    if (!(idr & (1U << 4))) {
        return BoardVersion::SEED_2_DFM;
    }
    return BoardVersion::SEED_REV4;
}

} // namespace umi::daisy

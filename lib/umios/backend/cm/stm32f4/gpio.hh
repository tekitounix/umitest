// SPDX-License-Identifier: MIT
// STM32F4 GPIO
#pragma once

#include <cstdint>

namespace umi::stm32 {

struct GPIO {
    // Register offsets
    static constexpr uint32_t MODER = 0x00;
    static constexpr uint32_t OTYPER = 0x04;
    static constexpr uint32_t OSPEEDR = 0x08;
    static constexpr uint32_t PUPDR = 0x0C;
    static constexpr uint32_t IDR = 0x10;
    static constexpr uint32_t ODR = 0x14;
    static constexpr uint32_t BSRR = 0x18;
    static constexpr uint32_t LCKR = 0x1C;
    static constexpr uint32_t AFRL = 0x20;
    static constexpr uint32_t AFRH = 0x24;

    // Mode values
    static constexpr uint32_t MODE_INPUT = 0;
    static constexpr uint32_t MODE_OUTPUT = 1;
    static constexpr uint32_t MODE_AF = 2;
    static constexpr uint32_t MODE_ANALOG = 3;

    // Speed values
    static constexpr uint32_t SPEED_LOW = 0;
    static constexpr uint32_t SPEED_MEDIUM = 1;
    static constexpr uint32_t SPEED_FAST = 2;
    static constexpr uint32_t SPEED_HIGH = 3;

    // Pull-up/down
    static constexpr uint32_t PUPD_NONE = 0;
    static constexpr uint32_t PUPD_UP = 1;
    static constexpr uint32_t PUPD_DOWN = 2;

    // Alternate functions
    static constexpr uint32_t AF0 = 0;   // System
    static constexpr uint32_t AF4 = 4;   // I2C1-3
    static constexpr uint32_t AF5 = 5;   // SPI1-2
    static constexpr uint32_t AF6 = 6;   // SPI3, I2S2, I2S3
    static constexpr uint32_t AF7 = 7;   // USART1-3
    static constexpr uint32_t AF10 = 10; // OTG_FS

    uint32_t base;

    explicit GPIO(char port) : base(0x40020000 + 0x400 * (port - 'A')) {}

    volatile uint32_t& reg(uint32_t offset) const {
        return *reinterpret_cast<volatile uint32_t*>(base + offset);
    }

    void set_mode(uint8_t pin, uint32_t mode) {
        reg(MODER) = (reg(MODER) & ~(3U << (pin * 2))) | (mode << (pin * 2));
    }

    void set_speed(uint8_t pin, uint32_t speed) {
        reg(OSPEEDR) = (reg(OSPEEDR) & ~(3U << (pin * 2))) | (speed << (pin * 2));
    }

    void set_pupd(uint8_t pin, uint32_t pupd) {
        reg(PUPDR) = (reg(PUPDR) & ~(3U << (pin * 2))) | (pupd << (pin * 2));
    }

    void set_af(uint8_t pin, uint32_t af) {
        if (pin < 8) {
            reg(AFRL) = (reg(AFRL) & ~(0xFU << (pin * 4))) | (af << (pin * 4));
        } else {
            reg(AFRH) = (reg(AFRH) & ~(0xFU << ((pin - 8) * 4))) | (af << ((pin - 8) * 4));
        }
    }

    void set_output_type(uint8_t pin, bool open_drain) {
        if (open_drain) {
            reg(OTYPER) |= 1U << pin;
        } else {
            reg(OTYPER) &= ~(1U << pin);
        }
    }

    void set(uint8_t pin) { reg(BSRR) = 1U << pin; }
    void reset(uint8_t pin) { reg(BSRR) = 1U << (pin + 16); }
    bool read(uint8_t pin) const { return (reg(IDR) & (1U << pin)) != 0; }

    /// Configure pin as alternate function
    void config_af(uint8_t pin, uint32_t af, uint32_t speed = SPEED_HIGH,
                   uint32_t pupd = PUPD_NONE, bool open_drain = false) {
        set_mode(pin, MODE_AF);
        set_af(pin, af);
        set_speed(pin, speed);
        set_pupd(pin, pupd);
        set_output_type(pin, open_drain);
    }

    /// Configure pin as output
    void config_output(uint8_t pin, uint32_t speed = SPEED_MEDIUM,
                       bool open_drain = false, uint32_t pupd = PUPD_NONE) {
        set_mode(pin, MODE_OUTPUT);
        set_speed(pin, speed);
        set_pupd(pin, pupd);
        set_output_type(pin, open_drain);
    }
};

}  // namespace umi::stm32

// SPDX-License-Identifier: MIT
// UMI-OS STM32F4 GPIO
#pragma once

#include <cstdint>

namespace umi::port::stm32 {

/// STM32F4 GPIO port base addresses
enum class GPIOPort : uint32_t {
    A = 0x40020000,
    B = 0x40020400,
    C = 0x40020800,
    D = 0x40020C00,
    E = 0x40021000,
    F = 0x40021400,
    G = 0x40021800,
    H = 0x40021C00,
    I = 0x40022000,
};

/// GPIO mode
enum class GPIOMode : uint8_t {
    Input    = 0b00,
    Output   = 0b01,
    AltFunc  = 0b10,
    Analog   = 0b11,
};

/// GPIO output type
enum class GPIOOutputType : uint8_t {
    PushPull  = 0,
    OpenDrain = 1,
};

/// GPIO speed
enum class GPIOSpeed : uint8_t {
    Low      = 0b00,
    Medium   = 0b01,
    High     = 0b10,
    VeryHigh = 0b11,
};

/// GPIO pull-up/pull-down
enum class GPIOPull : uint8_t {
    None     = 0b00,
    PullUp   = 0b01,
    PullDown = 0b10,
};

/// STM32F4 GPIO register access
struct GPIO {
    struct Regs {
        volatile uint32_t MODER;
        volatile uint32_t OTYPER;
        volatile uint32_t OSPEEDR;
        volatile uint32_t PUPDR;
        volatile uint32_t IDR;
        volatile uint32_t ODR;
        volatile uint32_t BSRR;
        volatile uint32_t LCKR;
        volatile uint32_t AFR[2];
    };

    static Regs* port(GPIOPort p) {
        return reinterpret_cast<Regs*>(static_cast<uint32_t>(p));
    }

    /// Configure a pin
    static void configure(GPIOPort p, uint8_t pin, GPIOMode mode, 
                         GPIOOutputType otype = GPIOOutputType::PushPull,
                         GPIOSpeed speed = GPIOSpeed::High,
                         GPIOPull pull = GPIOPull::None) {
        auto* regs = port(p);
        
        // Mode
        regs->MODER &= ~(0b11 << (pin * 2));
        regs->MODER |= (static_cast<uint8_t>(mode) << (pin * 2));
        
        // Output type
        if (static_cast<uint8_t>(otype)) {
            regs->OTYPER |= (1 << pin);
        } else {
            regs->OTYPER &= ~(1 << pin);
        }
        
        // Speed
        regs->OSPEEDR &= ~(0b11 << (pin * 2));
        regs->OSPEEDR |= (static_cast<uint8_t>(speed) << (pin * 2));
        
        // Pull-up/pull-down
        regs->PUPDR &= ~(0b11 << (pin * 2));
        regs->PUPDR |= (static_cast<uint8_t>(pull) << (pin * 2));
    }

    /// Set alternate function
    static void set_alt_func(GPIOPort p, uint8_t pin, uint8_t af) {
        auto* regs = port(p);
        uint8_t idx = pin >> 3;  // 0 for pins 0-7, 1 for pins 8-15
        uint8_t pos = (pin & 0x7) * 4;
        regs->AFR[idx] &= ~(0xF << pos);
        regs->AFR[idx] |= (af << pos);
    }

    /// Set pin high
    static void set(GPIOPort p, uint8_t pin) {
        port(p)->BSRR = (1 << pin);
    }

    /// Set pin low
    static void reset(GPIOPort p, uint8_t pin) {
        port(p)->BSRR = (1 << (pin + 16));
    }

    /// Toggle pin
    static void toggle(GPIOPort p, uint8_t pin) {
        port(p)->ODR ^= (1 << pin);
    }

    /// Read pin
    static bool read(GPIOPort p, uint8_t pin) {
        return (port(p)->IDR & (1 << pin)) != 0;
    }
};

} // namespace umi::port::stm32

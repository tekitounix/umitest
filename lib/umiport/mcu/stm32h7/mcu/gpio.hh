// SPDX-License-Identifier: MIT
// STM32H750 GPIO - mmio register definitions
#pragma once

#include <umimmio.hh>

namespace umi::stm32h7 {

// NOLINTBEGIN(readability-identifier-naming)

/// STM32H750 GPIO register block template (RM0433 Section 11)
/// Each GPIO port (A-K) has the same register layout
template <mm::Addr BaseAddr>
struct GPIOx : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = BaseAddr;

    struct MODER : mm::Register<GPIOx, 0x00, 32> {};    // Mode register
    struct OTYPER : mm::Register<GPIOx, 0x04, 32> {};   // Output type
    struct OSPEEDR : mm::Register<GPIOx, 0x08, 32> {};  // Output speed
    struct PUPDR : mm::Register<GPIOx, 0x0C, 32> {};    // Pull-up/pull-down
    struct IDR : mm::Register<GPIOx, 0x10, 32, mm::RO> {};  // Input data
    struct ODR : mm::Register<GPIOx, 0x14, 32> {};      // Output data
    struct BSRR : mm::Register<GPIOx, 0x18, 32, mm::WO> {}; // Bit set/reset
    struct LCKR : mm::Register<GPIOx, 0x1C, 32> {};     // Lock register
    struct AFRL : mm::Register<GPIOx, 0x20, 32> {};     // Alternate function low
    struct AFRH : mm::Register<GPIOx, 0x24, 32> {};     // Alternate function high
};

// GPIO port instances
using GPIOA = GPIOx<0x5802'0000>;
using GPIOB = GPIOx<0x5802'0400>;
using GPIOC = GPIOx<0x5802'0800>;
using GPIOD = GPIOx<0x5802'0C00>;
using GPIOE = GPIOx<0x5802'1000>;
using GPIOF = GPIOx<0x5802'1400>;
using GPIOG = GPIOx<0x5802'1800>;
using GPIOH = GPIOx<0x5802'1C00>;
using GPIOI = GPIOx<0x5802'2000>;
using GPIOJ = GPIOx<0x5802'2400>;
using GPIOK = GPIOx<0x5802'2800>;

// GPIO mode values (2 bits per pin)
namespace gpio_mode {
constexpr std::uint32_t INPUT = 0b00;
constexpr std::uint32_t OUTPUT = 0b01;
constexpr std::uint32_t ALTERNATE = 0b10;
constexpr std::uint32_t ANALOG = 0b11;
} // namespace gpio_mode

// GPIO output type values (1 bit per pin)
namespace gpio_otype {
constexpr std::uint32_t PUSH_PULL = 0;
constexpr std::uint32_t OPEN_DRAIN = 1;
} // namespace gpio_otype

// GPIO output speed values (2 bits per pin)
namespace gpio_speed {
constexpr std::uint32_t LOW = 0b00;
constexpr std::uint32_t MEDIUM = 0b01;
constexpr std::uint32_t HIGH = 0b10;
constexpr std::uint32_t VERY_HIGH = 0b11;
} // namespace gpio_speed

// GPIO pull-up/pull-down values (2 bits per pin)
namespace gpio_pupd {
constexpr std::uint32_t NONE = 0b00;
constexpr std::uint32_t PULL_UP = 0b01;
constexpr std::uint32_t PULL_DOWN = 0b10;
} // namespace gpio_pupd

// NOLINTEND(readability-identifier-naming)

/// Helper: configure a GPIO pin
/// @param transport DirectTransport instance
/// @param pin Pin number (0-15)
/// @param mode gpio_mode::*
template <typename GPIO, typename Transport>
void gpio_configure_pin(Transport& transport, std::uint8_t pin, std::uint32_t mode,
                        std::uint32_t otype = gpio_otype::PUSH_PULL,
                        std::uint32_t speed = gpio_speed::LOW,
                        std::uint32_t pupd = gpio_pupd::NONE) {
    // MODER: 2 bits per pin
    auto moder = transport.read(typename GPIO::MODER{});
    moder &= ~(0x3U << (pin * 2));
    moder |= (mode << (pin * 2));
    transport.write(GPIO::MODER::value(moder));

    // OTYPER: 1 bit per pin
    auto otyper = transport.read(typename GPIO::OTYPER{});
    otyper &= ~(0x1U << pin);
    otyper |= (otype << pin);
    transport.write(GPIO::OTYPER::value(otyper));

    // OSPEEDR: 2 bits per pin
    auto ospeedr = transport.read(typename GPIO::OSPEEDR{});
    ospeedr &= ~(0x3U << (pin * 2));
    ospeedr |= (speed << (pin * 2));
    transport.write(GPIO::OSPEEDR::value(ospeedr));

    // PUPDR: 2 bits per pin
    auto pupdr = transport.read(typename GPIO::PUPDR{});
    pupdr &= ~(0x3U << (pin * 2));
    pupdr |= (pupd << (pin * 2));
    transport.write(GPIO::PUPDR::value(pupdr));
}

/// Helper: set GPIO pin high
template <typename GPIO, typename Transport>
void gpio_set(Transport& transport, std::uint8_t pin) {
    transport.write(GPIO::BSRR::value(1U << pin));
}

/// Helper: set GPIO pin low
template <typename GPIO, typename Transport>
void gpio_reset(Transport& transport, std::uint8_t pin) {
    transport.write(GPIO::BSRR::value(1U << (pin + 16)));
}

/// Helper: set alternate function for a GPIO pin
/// Pins 0-7 use AFRL, pins 8-15 use AFRH (4 bits per pin)
template <typename GPIO, typename Transport>
void gpio_set_af(Transport& transport, std::uint8_t pin, std::uint8_t af) {
    if (pin < 8) {
        auto afrl = transport.read(typename GPIO::AFRL{});
        afrl &= ~(0xFU << (pin * 4));
        afrl |= (static_cast<std::uint32_t>(af) << (pin * 4));
        transport.write(GPIO::AFRL::value(afrl));
    } else {
        auto afrh = transport.read(typename GPIO::AFRH{});
        afrh &= ~(0xFU << ((pin - 8) * 4));
        afrh |= (static_cast<std::uint32_t>(af) << ((pin - 8) * 4));
        transport.write(GPIO::AFRH::value(afrh));
    }
}

/// Helper: toggle GPIO pin
template <typename GPIO, typename Transport>
void gpio_toggle(Transport& transport, std::uint8_t pin) {
    auto odr = transport.read(typename GPIO::ODR{});
    if (odr & (1U << pin)) {
        gpio_reset<GPIO>(transport, pin);
    } else {
        gpio_set<GPIO>(transport, pin);
    }
}

} // namespace umi::stm32h7

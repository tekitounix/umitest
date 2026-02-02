// SPDX-License-Identifier: MIT
// Daisy Seed USB Initialization (USB1 OTG HS in FS mode)
// PB14=DM, PB15=DP (AF12) — using internal FS PHY on HS peripheral
#pragma once

#include <cstdint>
#include <mcu/rcc.hh>
#include <mcu/gpio.hh>
#include <mmio/transport/direct.hh>

namespace umi::daisy {

/// Initialize USB1 OTG HS GPIO and clocks
/// Daisy Seed uses PB14=DM, PB15=DP (AF12) for USB OTG HS with internal FS PHY
inline void init_usb() {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> transport;

    // Enable GPIOB clock
    transport.modify(RCC::AHB4ENR::GPIOBEN::Set{});
    [[maybe_unused]] auto d1 = transport.read(RCC::AHB4ENR{});

    // Configure PB14 (DM) and PB15 (DP) as AF12 (USB OTG HS)
    constexpr std::uint8_t af12 = 12;
    for (std::uint8_t pin : {14, 15}) {
        gpio_configure_pin<GPIOB>(transport, pin,
                                   gpio_mode::ALTERNATE, gpio_otype::PUSH_PULL,
                                   gpio_speed::VERY_HIGH, gpio_pupd::NONE);
        gpio_set_af<GPIOB>(transport, pin, af12);
    }

    // Select HSI48 as USB clock source (USBSEL=0b11)
    transport.modify(RCC::D2CCIP2R::USBSEL::value(0b11));

    // Enable USB1 OTG HS clock
    transport.modify(RCC::AHB1ENR::USB1OTGHSEN::Set{});
    [[maybe_unused]] auto d2 = transport.read(RCC::AHB1ENR{});

    // Disable ULPI clock (using internal FS PHY)
    transport.modify(RCC::AHB1ENR::USB1OTGHSULPIEN::Reset{});
}

} // namespace umi::daisy

// SPDX-License-Identifier: MIT
// Daisy Seed MCU Initialization (clock, GPIO, power)
#pragma once

#include <mcu/rcc.hh>
#include <mcu/pwr.hh>
#include <mcu/gpio.hh>
#include <mcu/flash.hh>
#include <mmio/transport/direct.hh>
#include "board/bsp.hh"

namespace umi::daisy {

/// Initialize STM32H750 clocks for Daisy Seed (480MHz boost mode)
/// Based on libDaisy System::ConfigureClocks()
///
/// HSE 16MHz → PLL1 (M=4, N=240, P=2) → SYSCLK 480MHz
/// AHB = 240MHz, APB1/2/3/4 = 120MHz
/// Flash: 4 wait states, VOS0 (boost)
inline void init_clocks() {
    using namespace stm32h7;
    mm::DirectTransportT<> transport;

    // 1. Enable PWR clock and configure supply (LDO)
    transport.modify(RCC::APB4ENR::SYSCFGEN::Set{});

    // 2. Set voltage scaling to VOS1 first (required before VOS0)
    transport.modify(PWR::D3CR::VOS::value(pwr_vos::SCALE1));

    // Wait for VOS ready
    while (!transport.is(PWR::D3CR::VOSRDY::Set{})) {}

    // 3. Enable HSE oscillator
    transport.modify(RCC::CR::HSEON::Set{});
    while (!transport.is(RCC::CR::HSERDY::Set{})) {}

    // 4. Enable HSI48 for USB (CR register, bit 12)
    transport.modify(RCC::CR::HSI48ON::Set{});
    while (!transport.is(RCC::CR::HSI48RDY::Set{})) {}

    // 5. Configure PLL1: HSE(16MHz) / M(4) * N(240) / P(2) = 480MHz
    transport.modify(
        RCC::PLLCKSELR::PLLSRC::value(rcc_pllsrc::HSE),
        RCC::PLLCKSELR::DIVM1::value(4)
    );

    // PLL1 config: wide VCO, input range 4-8MHz, enable P/Q/R outputs
    transport.write(
        RCC::PLLCFGR::PLL1VCOSEL::Reset{},          // Wide VCO (192-960MHz)
        RCC::PLLCFGR::PLL1RGE::value(rcc_pllrge::RANGE_4_8MHZ),
        RCC::PLLCFGR::DIVP1EN::Set{},
        RCC::PLLCFGR::DIVQ1EN::Set{},
        RCC::PLLCFGR::DIVR1EN::Set{}
    );

    // PLL1 dividers: N=240-1=239, P=2-1=1, Q=5-1=4, R=2-1=1
    transport.write(
        RCC::PLL1DIVR::DIVN1::value(239),
        RCC::PLL1DIVR::DIVP1::value(1),
        RCC::PLL1DIVR::DIVQ1::value(4),
        RCC::PLL1DIVR::DIVR1::value(1)
    );

    // 6. Enable PLL1
    transport.modify(RCC::CR::PLL1ON::Set{});
    while (!transport.is(RCC::CR::PLL1RDY::Set{})) {}

    // 7. Set flash latency for 480MHz
    transport.modify(
        FLASH::ACR::LATENCY::value(flash_latency::WS4),
        FLASH::ACR::WRHIGHFREQ::value(0b10)
    );

    // 8. Configure bus dividers
    //    D1CPRE=1, HPRE=2, D1PPRE(APB3)=2
    transport.write(
        RCC::D1CFGR::D1CPRE::value(0),              // /1
        RCC::D1CFGR::HPRE::value(rcc_hpre::DIV2),   // AHB = SYSCLK/2 = 240MHz
        RCC::D1CFGR::D1PPRE::value(rcc_ppre::DIV2)   // APB3 = HCLK/2 = 120MHz
    );

    transport.write(
        RCC::D2CFGR::D2PPRE1::value(rcc_ppre::DIV2), // APB1 = 120MHz
        RCC::D2CFGR::D2PPRE2::value(rcc_ppre::DIV2)  // APB2 = 120MHz
    );

    transport.write(
        RCC::D3CFGR::D3PPRE::value(rcc_ppre::DIV2)   // APB4 = 120MHz
    );

    // 9. Switch system clock to PLL1
    transport.modify(RCC::CFGR::SW::value(rcc_sw::PLL1));
    while (transport.read(RCC::CFGR::SWS{}) != rcc_sw::PLL1) {}

    // 10. Enable VOS0 (boost) via SYSCFG for 480MHz
    // SYSCFG->PWRCR |= SYSCFG_PWRCR_ODEN
    auto* SYSCFG_PWRCR = reinterpret_cast<volatile std::uint32_t*>(0x5800'0404);
    *SYSCFG_PWRCR |= 1U;  // ODEN bit
    while (!transport.is(PWR::D3CR::VOSRDY::Set{})) {}
}

/// Initialize GPIO for LED (PC7)
inline void init_led() {
    using namespace stm32h7;
    mm::DirectTransportT<> transport;

    // Enable GPIOC clock
    transport.modify(RCC::AHB4ENR::GPIOCEN::Set{});

    // Small delay for clock to stabilize
    [[maybe_unused]] auto dummy = transport.read(RCC::AHB4ENR{});

    // Configure PC7 as output push-pull
    gpio_configure_pin<GPIOC>(transport, SeedBoard::led_pin,
                               gpio_mode::OUTPUT, gpio_otype::PUSH_PULL,
                               gpio_speed::LOW, gpio_pupd::NONE);
}

/// Toggle LED (PC7)
inline void toggle_led() {
    using namespace stm32h7;
    mm::DirectTransportT<> transport;
    gpio_toggle<GPIOC>(transport, SeedBoard::led_pin);
}

/// Set LED state
inline void set_led(bool on) {
    using namespace stm32h7;
    mm::DirectTransportT<> transport;
    if (on) {
        gpio_set<GPIOC>(transport, SeedBoard::led_pin);
    } else {
        gpio_reset<GPIOC>(transport, SeedBoard::led_pin);
    }
}

} // namespace umi::daisy

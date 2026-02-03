// SPDX-License-Identifier: MIT
// Daisy Seed MCU Initialization (clock, GPIO, power)
#pragma once

#include <mcu/flash.hh>
#include <mcu/gpio.hh>
#include <mcu/pwr.hh>
#include <mcu/rcc.hh>
#include <transport/direct.hh>

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

    // 1. Enable PWR clock and SYSCFG clock
    transport.modify(RCC::APB4ENR::SYSCFGEN::Set{});

    // 2. Configure power supply: LDO mode (required before VOS scaling)
    // Equivalent to HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY)
    // Use modify to preserve other bits (especially if already configured after reset)
    transport.modify(PWR::CR3::LDOEN::Set{}, PWR::CR3::SDEN::Reset{}, PWR::CR3::BYPASS::Reset{});

    // Wait for active voltage output ready (CSR1.ACTVOSRDY)
    while (!transport.is(PWR::CSR1::ACTVOSRDY::Set{})) {
    }

    // 3. Set voltage scaling to VOS1 first (required before VOS0)
    transport.modify(PWR::D3CR::VOS::value(pwr_vos::SCALE1));

    // Wait for VOS ready
    while (!transport.is(PWR::D3CR::VOSRDY::Set{})) {
    }

    // 3. Enable HSE oscillator
    transport.modify(RCC::CR::HSEON::Set{});
    while (!transport.is(RCC::CR::HSERDY::Set{})) {
    }

    // 4. Enable HSI48 for USB (CR register, bit 12)
    transport.modify(RCC::CR::HSI48ON::Set{});
    while (!transport.is(RCC::CR::HSI48RDY::Set{})) {
    }

    // 5. Configure PLL prescalers: HSE → PLL1(M=4), PLL2(M=1)
    transport.modify(RCC::PLLCKSELR::PLLSRC::value(rcc_pllsrc::HSE),
                     RCC::PLLCKSELR::DIVM1::value(4), // PLL1: 16MHz/4 = 4MHz
                     RCC::PLLCKSELR::DIVM2::value(1)  // PLL2: 16MHz/1 = 16MHz
    );

    // PLL config: VCO range, input range, output enables
    transport.write(
        // PLL1: wide VCO, 4-8MHz input, P/Q/R enabled
        RCC::PLLCFGR::PLL1VCOSEL::Reset{},
        RCC::PLLCFGR::PLL1RGE::value(rcc_pllrge::RANGE_4_8MHZ),
        RCC::PLLCFGR::DIVP1EN::Set{},
        RCC::PLLCFGR::DIVQ1EN::Set{},
        RCC::PLLCFGR::DIVR1EN::Set{},
        // PLL2: wide VCO, 4-8MHz input (16MHz), P/Q/R enabled, fractional
        RCC::PLLCFGR::PLL2FRACEN::Set{},
        RCC::PLLCFGR::PLL2VCOSEL::Reset{},
        RCC::PLLCFGR::PLL2RGE::value(rcc_pllrge::RANGE_4_8MHZ),
        RCC::PLLCFGR::DIVP2EN::Set{},
        RCC::PLLCFGR::DIVQ2EN::Set{},
        RCC::PLLCFGR::DIVR2EN::Set{});

    // PLL1 dividers: N=240, P=2, Q=5, R=2 (matching libDaisy boost)
    // VCO = 4MHz * 240 = 960MHz
    // P = 960/2 = 480MHz (SYSCLK), Q = 960/5 = 192MHz, R = 960/2 = 480MHz
    transport.write(RCC::PLL1DIVR::DIVN1::value(239), // N-1
                    RCC::PLL1DIVR::DIVP1::value(1),   // P-1
                    RCC::PLL1DIVR::DIVQ1::value(4),   // Q-1 (5-1=4)
                    RCC::PLL1DIVR::DIVR1::value(1)    // R-1 (2-1=1)
    );

    // PLL2 dividers: N=12, P=8, Q=2, R=1, FRACN=4096 (matching libDaisy)
    // VCO = 16MHz * (12 + 4096/8192) = 16 * 12.5 = 200MHz
    // P = 200/8 = 25MHz, Q = 200/2 = 100MHz, R = 200/1 = 200MHz (FMC)
    transport.write(RCC::PLL2DIVR::DIVN2::value(11), // N-1
                    RCC::PLL2DIVR::DIVP2::value(7),  // P-1 (8-1=7)
                    RCC::PLL2DIVR::DIVQ2::value(1),  // Q-1 (2-1=1)
                    RCC::PLL2DIVR::DIVR2::value(0)   // R-1 (1-1=0)
    );
    transport.write(RCC::PLL2FRACR::FRACN2::value(4096));

    // 6. Enable PLL1 and PLL2
    transport.modify(RCC::CR::PLL1ON::Set{}, RCC::CR::PLL2ON::Set{});
    while (!transport.is(RCC::CR::PLL1RDY::Set{})) {
    }
    while (!transport.is(RCC::CR::PLL2RDY::Set{})) {
    }

    // 7. Set flash latency for 480MHz
    transport.modify(FLASH::ACR::LATENCY::value(flash_latency::WS4), FLASH::ACR::WRHIGHFREQ::value(0b10));

    // 8. Configure bus dividers
    //    D1CPRE=1, HPRE=2, D1PPRE(APB3)=2
    transport.write(RCC::D1CFGR::D1CPRE::value(0),             // /1
                    RCC::D1CFGR::HPRE::value(rcc_hpre::DIV2),  // AHB = SYSCLK/2 = 240MHz
                    RCC::D1CFGR::D1PPRE::value(rcc_ppre::DIV2) // APB3 = HCLK/2 = 120MHz
    );

    transport.write(RCC::D2CFGR::D2PPRE1::value(rcc_ppre::DIV2), // APB1 = 120MHz
                    RCC::D2CFGR::D2PPRE2::value(rcc_ppre::DIV2)  // APB2 = 120MHz
    );

    transport.write(RCC::D3CFGR::D3PPRE::value(rcc_ppre::DIV2) // APB4 = 120MHz
    );

    // 9. Switch system clock to PLL1
    transport.modify(RCC::CFGR::SW::value(rcc_sw::PLL1));
    while (transport.read(RCC::CFGR::SWS{}) != rcc_sw::PLL1) {
    }

    // 10. Enable VOS0 (boost) via SYSCFG for 480MHz
    // SYSCFG->PWRCR |= SYSCFG_PWRCR_ODEN
    auto* SYSCFG_PWRCR = reinterpret_cast<volatile std::uint32_t*>(0x5800'0404);
    *SYSCFG_PWRCR |= 1U; // ODEN bit
    while (!transport.is(PWR::D3CR::VOSRDY::Set{})) {
    }

    // 11. Configure peripheral clock sources (matching libDaisy)
    // FMC → PLL2R (200MHz), SDMMC → PLL2R
    transport.modify(RCC::D1CCIPR::FMCSEL::value(rcc_fmcsel::PLL2R),
                     RCC::D1CCIPR::SDMMCSEL::value(rcc_sdmmcsel::PLL2R));

    // SPI1/2/3 → PLL2P (25MHz)
    transport.modify(RCC::D2CCIP1R::SPI123SEL::value(rcc_spi123sel::PLL2P));

    // ADC → PLL3R
    transport.modify(RCC::D3CCIPR::ADCSEL::value(rcc_adcsel::PLL3R));

    // USB → HSI48
    transport.modify(RCC::D2CCIP2R::USBSEL::value(0b11));

    // Note: USB33DEN is set in init_usb() after USB clock is enabled (per libDaisy)
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
    gpio_configure_pin<GPIOC>(
        transport, SeedBoard::led_pin, gpio_mode::OUTPUT, gpio_otype::PUSH_PULL, gpio_speed::LOW, gpio_pupd::NONE);
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

// SPDX-License-Identifier: MIT
// Daisy Seed SDRAM initialization (AS4C16M32MSA 64MB)
// FMC SDRAM Bank 1, 32-bit bus, mapped at 0xC000'0000
#pragma once

#include <cstdint>
#include <mcu/rcc.hh>
#include <mcu/gpio.hh>
#include <mcu/fmc.hh>
#include <mmio/transport/direct.hh>

namespace umi::daisy {

/// Initialize FMC GPIO pins for SDRAM (AF12 for all FMC pins)
inline void init_sdram_gpio() {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    // Enable GPIO clocks: C, D, E, F, G, H, I
    t.modify(RCC::AHB4ENR::GPIOCEN::Set{});
    t.modify(RCC::AHB4ENR::GPIODEN::Set{});
    t.modify(RCC::AHB4ENR::GPIOEEN::Set{});
    t.modify(RCC::AHB4ENR::GPIOFEN::Set{});
    t.modify(RCC::AHB4ENR::GPIOGEN::Set{});
    t.modify(RCC::AHB4ENR::GPIOHEN::Set{});
    t.modify(RCC::AHB4ENR::GPIOIEN::Set{});
    [[maybe_unused]] auto dummy = t.read(RCC::AHB4ENR{});

    constexpr std::uint8_t AF12 = 12;

    auto cfg = [&](auto gpio, std::uint8_t pin) {
        gpio_configure_pin<decltype(gpio)>(
            t, pin, gpio_mode::ALTERNATE, gpio_otype::PUSH_PULL,
            gpio_speed::VERY_HIGH, gpio_pupd::NONE);
        gpio_set_af<decltype(gpio)>(t, pin, AF12);
    };

    // PC0 = SDNWE
    cfg(GPIOC{}, 0);

    // PD0=D2, PD1=D3, PD8=D13, PD9=D14, PD10=D15, PD14=D0, PD15=D1
    for (auto pin : {0, 1, 8, 9, 10, 14, 15}) cfg(GPIOD{}, static_cast<std::uint8_t>(pin));

    // PE0=NBL0, PE1=NBL1, PE7-PE15=D4-D12
    for (auto pin : {0, 1, 7, 8, 9, 10, 11, 12, 13, 14, 15}) cfg(GPIOE{}, static_cast<std::uint8_t>(pin));

    // PF0-PF5=A0-A5, PF11=SDNRAS, PF12-PF15=A6-A9
    for (auto pin : {0, 1, 2, 3, 4, 5, 11, 12, 13, 14, 15}) cfg(GPIOF{}, static_cast<std::uint8_t>(pin));

    // PG0-PG2=A10-A12, PG4-PG5=BA0-BA1, PG8=SDCLK, PG15=SDNCAS
    for (auto pin : {0, 1, 2, 4, 5, 8, 15}) cfg(GPIOG{}, static_cast<std::uint8_t>(pin));

    // PH2=SDCKE0, PH3=SDNE0, PH5, PH8-PH15=D16-D23
    for (auto pin : {2, 3, 5, 8, 9, 10, 11, 12, 13, 14, 15}) cfg(GPIOH{}, static_cast<std::uint8_t>(pin));

    // PI0-PI7=D24-D31(excl. PI4=NBL2, PI5=NBL3), PI9=D30, PI10=D31
    for (auto pin : {0, 1, 2, 3, 4, 5, 6, 7, 9, 10}) cfg(GPIOI{}, static_cast<std::uint8_t>(pin));
}

/// Initialize SDRAM via FMC
/// AS4C16M32MSA: 512Mbit (64MB), 13-row, 9-col, 32-bit, 4 banks
inline void init_sdram() {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    // Enable FMC clock
    t.modify(RCC::AHB3ENR::FMCEN::Set{});
    [[maybe_unused]] auto dummy = t.read(RCC::AHB3ENR{});

    init_sdram_gpio();

    // SDCR1: 9-col, 13-row, 32-bit, 4 banks, CAS=3, SDCLK=HCLK/2, read burst, no pipe delay
    t.write(FMC_SDRAM::SDCR1::value(
        (0b01U << 0)  |  // NC: 9 column bits
        (0b01U << 2)  |  // NR: 13 row bits
        (0b10U << 4)  |  // MWID: 32-bit
        (1U << 6)     |  // NB: 4 internal banks
        (0b11U << 7)  |  // CAS: 3 cycles
        (0b10U << 10) |  // SDCLK: HCLK/2 (120MHz)
        (1U << 12)    |  // RBURST: read burst enable
        (0b00U << 13)    // RPIPE: no delay
    ));

    // SDTR1: timing for 120MHz (8.33ns period)
    t.write(FMC_SDRAM::SDTR1::value(
        (2U - 1) << 0  |   // TMRD: 2 cycles
        (7U - 1) << 4  |   // TXSR: 7 cycles
        (4U - 1) << 8  |   // TRAS: 4 cycles
        (8U - 1) << 12 |   // TRC: 8 cycles
        (3U - 1) << 16 |   // TWR: 3 cycles
        (16U - 1) << 20 |  // TRP: 16 cycles
        (10U - 1) << 24    // TRCD: 10 cycles
    ));

    // Step 1: Clock configuration enable
    t.write(FMC_SDRAM::SDCMR::value(
        fmc_sdcmd::CLK_ENABLE | (1U << 4) | (0U << 5)
    ));
    while (t.read(FMC_SDRAM::SDSR{}) & (1U << 5)) {}

    // Brief delay (~100ms at 480MHz)
    for (int i = 0; i < 480000; ++i) { asm volatile("" ::: "memory"); }

    // Step 2: PALL (precharge all)
    t.write(FMC_SDRAM::SDCMR::value(
        fmc_sdcmd::PALL | (1U << 4) | (0U << 5)
    ));
    while (t.read(FMC_SDRAM::SDSR{}) & (1U << 5)) {}

    // Step 3: Auto-refresh (4 cycles)
    t.write(FMC_SDRAM::SDCMR::value(
        fmc_sdcmd::AUTO_REFRESH | (1U << 4) | ((4U - 1) << 5)
    ));
    while (t.read(FMC_SDRAM::SDSR{}) & (1U << 5)) {}

    // Step 4: Load mode register
    constexpr std::uint32_t mode_reg =
        (1U << 1) |   // Burst length 4
        (0U << 3) |   // Sequential burst
        (3U << 4) |   // CAS latency 3
        (1U << 9);    // Single write burst

    t.write(FMC_SDRAM::SDCMR::value(
        fmc_sdcmd::LOAD_MODE | (1U << 4) | (mode_reg << 9)
    ));
    while (t.read(FMC_SDRAM::SDSR{}) & (1U << 5)) {}

    // Step 5: Refresh rate (conservative, from libDaisy)
    t.write(FMC_SDRAM::SDRTR::value(0x0806U << 1));
}

} // namespace umi::daisy

// SPDX-License-Identifier: MIT
// STM32H750 RCC (Reset and Clock Control) - mmio register definitions
#pragma once

#include <umimmio.hh>
#include <transport/direct.hh>

namespace umi::stm32h7 {

// NOLINTBEGIN(readability-identifier-naming)

/// STM32H750 RCC register block (RM0433 Section 8)
/// Base address: 0x5802'4400
struct RCC : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0x5802'4400;

    /// Clock control register
    struct CR : mm::Register<RCC, 0x00, 32> {
        struct HSION : mm::Field<CR, 0, 1> {};
        struct HSIRDY : mm::Field<CR, 2, 1> {};
        struct HSIDIV : mm::Field<CR, 3, 2> {};    // HSI divider
        struct HSIDIVF : mm::Field<CR, 5, 1> {};   // HSI divider flag
        struct CSION : mm::Field<CR, 7, 1> {};
        struct CSIRDY : mm::Field<CR, 8, 1> {};
        struct HSI48ON : mm::Field<CR, 12, 1> {};
        struct HSI48RDY : mm::Field<CR, 13, 1> {};
        struct HSEON : mm::Field<CR, 16, 1> {};
        struct HSERDY : mm::Field<CR, 17, 1> {};
        struct HSEBYP : mm::Field<CR, 18, 1> {};
        struct HSECSSON : mm::Field<CR, 19, 1> {};
        struct PLL1ON : mm::Field<CR, 24, 1> {};
        struct PLL1RDY : mm::Field<CR, 25, 1> {};
        struct PLL2ON : mm::Field<CR, 26, 1> {};
        struct PLL2RDY : mm::Field<CR, 27, 1> {};
        struct PLL3ON : mm::Field<CR, 28, 1> {};
        struct PLL3RDY : mm::Field<CR, 29, 1> {};
    };

    /// HSI48 clock recovery RC register (offset 0x08, read-only calibration)
    struct CRRCR : mm::Register<RCC, 0x08, 32> {
        struct HSI48CAL : mm::Field<CRRCR, 0, 10> {};  // Read-only calibration
    };

    /// Clock configuration register
    struct CFGR : mm::Register<RCC, 0x10, 32> {
        struct SW : mm::Field<CFGR, 0, 3> {};      // System clock switch
        struct SWS : mm::Field<CFGR, 3, 3> {};     // System clock switch status
        struct HPRE : mm::Field<CFGR, 8, 4> {};    // AHB prescaler
        struct D1PPRE : mm::Field<CFGR, 12, 3> {}; // D1 APB3 prescaler
        struct D1CPRE : mm::Field<CFGR, 8, 4> {};  // D1 domain core prescaler
    };

    /// Domain 1 clock configuration register
    struct D1CFGR : mm::Register<RCC, 0x18, 32> {
        struct HPRE : mm::Field<D1CFGR, 0, 4> {};   // AHB prescaler
        struct D1PPRE : mm::Field<D1CFGR, 4, 3> {};  // D1 APB3 prescaler
        struct D1CPRE : mm::Field<D1CFGR, 8, 4> {};  // D1 core prescaler
    };

    /// Domain 2 clock configuration register
    struct D2CFGR : mm::Register<RCC, 0x1C, 32> {
        struct D2PPRE1 : mm::Field<D2CFGR, 4, 3> {}; // D2 APB1 prescaler
        struct D2PPRE2 : mm::Field<D2CFGR, 8, 3> {}; // D2 APB2 prescaler
    };

    /// Domain 3 clock configuration register
    struct D3CFGR : mm::Register<RCC, 0x20, 32> {
        struct D3PPRE : mm::Field<D3CFGR, 4, 3> {};  // D3 APB4 prescaler
    };

    /// PLL clock source selection register
    struct PLLCKSELR : mm::Register<RCC, 0x28, 32> {
        struct PLLSRC : mm::Field<PLLCKSELR, 0, 2> {};   // PLL source: 0=HSI, 1=CSI, 2=HSE
        struct DIVM1 : mm::Field<PLLCKSELR, 4, 6> {};    // PLL1 prescaler
        struct DIVM2 : mm::Field<PLLCKSELR, 12, 6> {};   // PLL2 prescaler
        struct DIVM3 : mm::Field<PLLCKSELR, 20, 6> {};   // PLL3 prescaler
    };

    /// PLL configuration register
    struct PLLCFGR : mm::Register<RCC, 0x2C, 32> {
        struct PLL1FRACEN : mm::Field<PLLCFGR, 0, 1> {};
        struct PLL1VCOSEL : mm::Field<PLLCFGR, 1, 1> {};  // 0=wide VCO, 1=medium
        struct PLL1RGE : mm::Field<PLLCFGR, 2, 2> {};     // PLL1 input freq range
        struct PLL2FRACEN : mm::Field<PLLCFGR, 4, 1> {};
        struct PLL2VCOSEL : mm::Field<PLLCFGR, 5, 1> {};
        struct PLL2RGE : mm::Field<PLLCFGR, 6, 2> {};
        struct PLL3FRACEN : mm::Field<PLLCFGR, 8, 1> {};
        struct PLL3VCOSEL : mm::Field<PLLCFGR, 9, 1> {};
        struct PLL3RGE : mm::Field<PLLCFGR, 10, 2> {};
        struct DIVP1EN : mm::Field<PLLCFGR, 16, 1> {};
        struct DIVQ1EN : mm::Field<PLLCFGR, 17, 1> {};
        struct DIVR1EN : mm::Field<PLLCFGR, 18, 1> {};
        struct DIVP2EN : mm::Field<PLLCFGR, 19, 1> {};
        struct DIVQ2EN : mm::Field<PLLCFGR, 20, 1> {};
        struct DIVR2EN : mm::Field<PLLCFGR, 21, 1> {};
        struct DIVP3EN : mm::Field<PLLCFGR, 22, 1> {};
        struct DIVQ3EN : mm::Field<PLLCFGR, 23, 1> {};
        struct DIVR3EN : mm::Field<PLLCFGR, 24, 1> {};
    };

    /// PLL1 dividers register
    struct PLL1DIVR : mm::Register<RCC, 0x30, 32> {
        struct DIVN1 : mm::Field<PLL1DIVR, 0, 9> {};     // PLL1 N multiplier (actual = field + 1)
        struct DIVP1 : mm::Field<PLL1DIVR, 9, 7> {};     // PLL1 P divider (actual = field + 1)
        struct DIVQ1 : mm::Field<PLL1DIVR, 16, 7> {};    // PLL1 Q divider
        struct DIVR1 : mm::Field<PLL1DIVR, 24, 7> {};    // PLL1 R divider
    };

    /// PLL1 fractional divider register
    struct PLL1FRACR : mm::Register<RCC, 0x34, 32> {
        struct FRACN1 : mm::Field<PLL1FRACR, 3, 13> {};
    };

    /// PLL2 dividers register
    struct PLL2DIVR : mm::Register<RCC, 0x38, 32> {
        struct DIVN2 : mm::Field<PLL2DIVR, 0, 9> {};
        struct DIVP2 : mm::Field<PLL2DIVR, 9, 7> {};
        struct DIVQ2 : mm::Field<PLL2DIVR, 16, 7> {};
        struct DIVR2 : mm::Field<PLL2DIVR, 24, 7> {};
    };

    /// PLL2 fractional divider register
    struct PLL2FRACR : mm::Register<RCC, 0x3C, 32> {
        struct FRACN2 : mm::Field<PLL2FRACR, 3, 13> {};
    };

    /// PLL3 dividers register
    struct PLL3DIVR : mm::Register<RCC, 0x40, 32> {
        struct DIVN3 : mm::Field<PLL3DIVR, 0, 9> {};
        struct DIVP3 : mm::Field<PLL3DIVR, 9, 7> {};
        struct DIVQ3 : mm::Field<PLL3DIVR, 16, 7> {};
        struct DIVR3 : mm::Field<PLL3DIVR, 24, 7> {};
    };

    /// PLL3 fractional divider register
    struct PLL3FRACR : mm::Register<RCC, 0x44, 32> {
        struct FRACN3 : mm::Field<PLL3FRACR, 3, 13> {};
    };

    /// AHB3 peripheral clock enable register (FMC, QSPI)
    struct AHB3ENR : mm::Register<RCC, 0xD4, 32> {
        struct SDMMC1EN : mm::Field<AHB3ENR, 16, 1> {};
        struct FMCEN    : mm::Field<AHB3ENR, 12, 1> {};
        struct QSPIEN   : mm::Field<AHB3ENR, 14, 1> {};
    };

    /// AHB4 peripheral clock enable register (GPIO clocks)
    struct AHB4ENR : mm::Register<RCC, 0xE0, 32> {
        struct GPIOAEN : mm::Field<AHB4ENR, 0, 1> {};
        struct GPIOBEN : mm::Field<AHB4ENR, 1, 1> {};
        struct GPIOCEN : mm::Field<AHB4ENR, 2, 1> {};
        struct GPIODEN : mm::Field<AHB4ENR, 3, 1> {};
        struct GPIOEEN : mm::Field<AHB4ENR, 4, 1> {};
        struct GPIOFEN : mm::Field<AHB4ENR, 5, 1> {};
        struct GPIOGEN : mm::Field<AHB4ENR, 6, 1> {};
        struct GPIOHEN : mm::Field<AHB4ENR, 7, 1> {};
        struct GPIOIEN : mm::Field<AHB4ENR, 8, 1> {};
        struct GPIOJEN : mm::Field<AHB4ENR, 9, 1> {};
        struct GPIOKEN : mm::Field<AHB4ENR, 10, 1> {};
    };

    /// AHB1 peripheral clock enable register
    struct AHB1ENR : mm::Register<RCC, 0xD8, 32> {
        struct DMA1EN : mm::Field<AHB1ENR, 0, 1> {};
        struct DMA2EN : mm::Field<AHB1ENR, 1, 1> {};
        struct ADC12EN : mm::Field<AHB1ENR, 5, 1> {};
        struct USB1OTGHSEN : mm::Field<AHB1ENR, 25, 1> {};
        struct USB1OTGHSULPIEN : mm::Field<AHB1ENR, 26, 1> {};
    };

    /// AHB2 peripheral clock enable register (D2 domain)
    struct AHB2ENR : mm::Register<RCC, 0xDC, 32> {
        struct D2SRAM1EN : mm::Field<AHB2ENR, 29, 1> {};
        struct D2SRAM2EN : mm::Field<AHB2ENR, 30, 1> {};
        struct D2SRAM3EN : mm::Field<AHB2ENR, 31, 1> {};
    };

    /// APB1L peripheral clock enable register
    struct APB1LENR : mm::Register<RCC, 0xE8, 32> {
        struct I2C1EN : mm::Field<APB1LENR, 21, 1> {};
        struct I2C2EN : mm::Field<APB1LENR, 22, 1> {};
        struct I2C3EN : mm::Field<APB1LENR, 23, 1> {};
    };

    /// APB2 peripheral clock enable register
    struct APB2ENR : mm::Register<RCC, 0xF0, 32> {
        struct USART1EN : mm::Field<APB2ENR, 4, 1> {};
        struct SAI1EN   : mm::Field<APB2ENR, 22, 1> {};
        struct SAI2EN   : mm::Field<APB2ENR, 23, 1> {};
    };

    /// Domain 2 kernel clock configuration register (peripheral clock mux)
    struct D2CCIP1R : mm::Register<RCC, 0x50, 32> {
        struct SAI1SEL : mm::Field<D2CCIP1R, 0, 3> {};    // SAI1 clock source
        struct SAI23SEL : mm::Field<D2CCIP1R, 6, 3> {};   // SAI2/3 clock source
        struct SPI123SEL : mm::Field<D2CCIP1R, 12, 3> {}; // SPI1/2/3 clock source
    };

    /// Domain 2 kernel clock configuration register 2
    struct D2CCIP2R : mm::Register<RCC, 0x54, 32> {
        struct USART234578SEL : mm::Field<D2CCIP2R, 0, 3> {};
        struct USART16910SEL : mm::Field<D2CCIP2R, 3, 3> {};
        struct I2C123SEL : mm::Field<D2CCIP2R, 12, 2> {};
        struct USBSEL : mm::Field<D2CCIP2R, 20, 2> {};
    };

    /// Domain 1 kernel clock configuration register
    struct D1CCIPR : mm::Register<RCC, 0x4C, 32> {
        struct FMCSEL : mm::Field<D1CCIPR, 0, 2> {};    // FMC clock source
        struct SDMMCSEL : mm::Field<D1CCIPR, 16, 1> {};  // SDMMC clock source (bit 16)
    };

    /// Domain 3 kernel clock configuration register
    struct D3CCIPR : mm::Register<RCC, 0x58, 32> {
        struct ADCSEL : mm::Field<D3CCIPR, 16, 2> {};   // ADC clock source
    };

    /// APB4 peripheral clock enable register
    struct APB4ENR : mm::Register<RCC, 0xF4, 32> {
        struct SYSCFGEN : mm::Field<APB4ENR, 1, 1> {};
        struct I2C4EN : mm::Field<APB4ENR, 7, 1> {};
    };
};

// SW field values
namespace rcc_sw {
constexpr std::uint32_t HSI = 0;
constexpr std::uint32_t CSI = 1;
constexpr std::uint32_t HSE = 2;
constexpr std::uint32_t PLL1 = 3;
} // namespace rcc_sw

// HPRE field values (AHB prescaler)
namespace rcc_hpre {
constexpr std::uint32_t DIV1 = 0b0000;
constexpr std::uint32_t DIV2 = 0b1000;
constexpr std::uint32_t DIV4 = 0b1001;
constexpr std::uint32_t DIV8 = 0b1010;
} // namespace rcc_hpre

// D1PPRE/D2PPRE/D3PPRE field values (APB prescaler)
namespace rcc_ppre {
constexpr std::uint32_t DIV1 = 0b000;
constexpr std::uint32_t DIV2 = 0b100;
constexpr std::uint32_t DIV4 = 0b101;
} // namespace rcc_ppre

// PLLSRC field values
namespace rcc_pllsrc {
constexpr std::uint32_t HSI = 0;
constexpr std::uint32_t CSI = 1;
constexpr std::uint32_t HSE = 2;
} // namespace rcc_pllsrc

// PLL RGE (input frequency range)
namespace rcc_pllrge {
constexpr std::uint32_t RANGE_1_2MHZ = 0;   // 1-2 MHz
constexpr std::uint32_t RANGE_2_4MHZ = 1;   // 2-4 MHz
constexpr std::uint32_t RANGE_4_8MHZ = 2;   // 4-8 MHz
constexpr std::uint32_t RANGE_8_16MHZ = 3;  // 8-16 MHz
} // namespace rcc_pllrge

// SAI clock source selection
namespace rcc_saisel {
constexpr std::uint32_t PLL1Q = 0;
constexpr std::uint32_t PLL2P = 1;
constexpr std::uint32_t PLL3P = 2;
constexpr std::uint32_t I2S_CKIN = 3;
constexpr std::uint32_t PER_CK = 4;
} // namespace rcc_saisel

// FMC clock source selection (D1CCIPR.FMCSEL, 2-bit)
namespace rcc_fmcsel {
constexpr std::uint32_t HCLK = 0;   // D1 HCLK (default)
constexpr std::uint32_t PLL1Q = 1;  // PLL1 Q output
constexpr std::uint32_t PLL2R = 2;  // PLL2 R output
constexpr std::uint32_t CLKP = 3;   // per_ck
} // namespace rcc_fmcsel

// SDMMC clock source selection (D1CCIPR.SDMMCSEL, 1-bit)
namespace rcc_sdmmcsel {
constexpr std::uint32_t PLL1Q = 0;  // PLL1 Q output
constexpr std::uint32_t PLL2R = 1;  // PLL2 R output
} // namespace rcc_sdmmcsel

// ADC clock source selection (D3CCIPR.ADCSEL, 2-bit)
namespace rcc_adcsel {
constexpr std::uint32_t PLL2P = 0;  // PLL2 P output
constexpr std::uint32_t PLL3R = 1;  // PLL3 R output
constexpr std::uint32_t CLKP = 2;   // per_ck
} // namespace rcc_adcsel

// SPI1/2/3 clock source selection (D2CCIP1R.SPI123SEL, 3-bit)
namespace rcc_spi123sel {
constexpr std::uint32_t PLL1Q = 0;  // PLL1 Q output
constexpr std::uint32_t PLL2P = 1;  // PLL2 P output
constexpr std::uint32_t PLL3P = 2;  // PLL3 P output
constexpr std::uint32_t I2S_CKIN = 3;
constexpr std::uint32_t PER_CK = 4;
} // namespace rcc_spi123sel

// NOLINTEND(readability-identifier-naming)

// ============================================================================
// Clock calculation helpers (constexpr, testable on host)
// ============================================================================

/// PLL output frequency: VCO = (src_hz / M) * N, P output = VCO / P
struct PllConfig {
    std::uint32_t src_hz;  // Input clock (e.g. 16'000'000 for HSE)
    std::uint32_t m;       // Input divider (1-63)
    std::uint32_t n;       // VCO multiplier (4-512)
    std::uint32_t p;       // P output divider (1-128)
};

constexpr std::uint32_t pll_vco_hz(const PllConfig& cfg) {
    return (cfg.src_hz / cfg.m) * cfg.n;
}

constexpr std::uint32_t pll_p_hz(const PllConfig& cfg) {
    return pll_vco_hz(cfg) / cfg.p;
}

constexpr std::uint32_t pll_ref_hz(const PllConfig& cfg) {
    return cfg.src_hz / cfg.m;
}

/// Required flash wait states for given HCLK and VOS level
/// VOS0 (boost): WS4 for 210-225MHz AXI, actual 240MHz HCLK
/// VOS1: WS2 for up to 200MHz
constexpr std::uint32_t flash_wait_states(std::uint32_t hclk_hz, bool vos0_boost) {
    if (vos0_boost) {
        if (hclk_hz <= 70'000'000)  return 0;
        if (hclk_hz <= 140'000'000) return 1;
        if (hclk_hz <= 185'000'000) return 2;
        if (hclk_hz <= 210'000'000) return 3;
        return 4;  // up to 240MHz
    }
    // VOS1
    if (hclk_hz <= 70'000'000)  return 0;
    if (hclk_hz <= 140'000'000) return 1;
    return 2;  // up to 200MHz
}

/// GPIO pin bit mask for 2-bit fields (MODER, OSPEEDR, PUPDR)
constexpr std::uint32_t gpio_2bit_mask(std::uint8_t pin) {
    return 0x3U << (pin * 2);
}

/// GPIO pin bit mask for 1-bit fields (OTYPER, ODR, IDR)
constexpr std::uint32_t gpio_1bit_mask(std::uint8_t pin) {
    return 1U << pin;
}

/// GPIO AF register index: 0 for pins 0-7 (AFRL), 1 for pins 8-15 (AFRH)
constexpr std::uint8_t gpio_af_reg_index(std::uint8_t pin) {
    return pin >> 3;  // 0 or 1
}

/// GPIO AF field shift within AFR register
constexpr std::uint8_t gpio_af_shift(std::uint8_t pin) {
    return (pin & 7) * 4;
}

} // namespace umi::stm32h7

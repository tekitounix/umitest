// SPDX-License-Identifier: MIT
// STM32F4 RCC (Reset and Clock Control)
#pragma once

#include <cstdint>

namespace umi::stm32 {

struct RCC {
    static constexpr uint32_t BASE = 0x40023800;

    // Register offsets
    static constexpr uint32_t CR = 0x00;
    static constexpr uint32_t PLLCFGR = 0x04;
    static constexpr uint32_t CFGR = 0x08;
    static constexpr uint32_t CIR = 0x0C;
    static constexpr uint32_t AHB1RSTR = 0x10;
    static constexpr uint32_t AHB2RSTR = 0x14;
    static constexpr uint32_t APB1RSTR = 0x20;
    static constexpr uint32_t APB2RSTR = 0x24;
    static constexpr uint32_t AHB1ENR = 0x30;
    static constexpr uint32_t AHB2ENR = 0x34;
    static constexpr uint32_t APB1ENR = 0x40;
    static constexpr uint32_t APB2ENR = 0x44;

    static volatile uint32_t& reg(uint32_t offset) {
        return *reinterpret_cast<volatile uint32_t*>(BASE + offset);
    }

    // CR bits
    static constexpr uint32_t CR_HSION = 1U << 0;
    static constexpr uint32_t CR_HSIRDY = 1U << 1;
    static constexpr uint32_t CR_HSEON = 1U << 16;
    static constexpr uint32_t CR_HSERDY = 1U << 17;
    static constexpr uint32_t CR_PLLON = 1U << 24;
    static constexpr uint32_t CR_PLLRDY = 1U << 25;

    // CFGR bits
    static constexpr uint32_t CFGR_SW_HSI = 0;
    static constexpr uint32_t CFGR_SW_HSE = 1;
    static constexpr uint32_t CFGR_SW_PLL = 2;
    static constexpr uint32_t CFGR_SWS_HSI = 0 << 2;
    static constexpr uint32_t CFGR_SWS_HSE = 1 << 2;
    static constexpr uint32_t CFGR_SWS_PLL = 2 << 2;

    // AHB1ENR bits
    static constexpr uint32_t AHB1ENR_GPIOAEN = 1U << 0;
    static constexpr uint32_t AHB1ENR_GPIOBEN = 1U << 1;
    static constexpr uint32_t AHB1ENR_GPIOCEN = 1U << 2;
    static constexpr uint32_t AHB1ENR_GPIODEN = 1U << 3;
    static constexpr uint32_t AHB1ENR_DMA1EN = 1U << 21;
    static constexpr uint32_t AHB1ENR_DMA2EN = 1U << 22;

    // AHB2ENR bits
    static constexpr uint32_t AHB2ENR_OTGFSEN = 1U << 7;

    // APB1ENR bits
    static constexpr uint32_t APB1ENR_TIM2EN = 1U << 0;
    static constexpr uint32_t APB1ENR_TIM3EN = 1U << 1;
    static constexpr uint32_t APB1ENR_SPI2EN = 1U << 14;
    static constexpr uint32_t APB1ENR_SPI3EN = 1U << 15;
    static constexpr uint32_t APB1ENR_USART2EN = 1U << 17;
    static constexpr uint32_t APB1ENR_I2C1EN = 1U << 21;
    static constexpr uint32_t APB1ENR_PWREN = 1U << 28;

    // APB2ENR bits
    static constexpr uint32_t APB2ENR_SYSCFGEN = 1U << 14;

    // Clock enable helpers
    static void enable_gpio(char port) {
        reg(AHB1ENR) |= 1U << (port - 'A');
    }

    static void enable_i2c1() { reg(APB1ENR) |= APB1ENR_I2C1EN; }
    static void enable_spi2() { reg(APB1ENR) |= APB1ENR_SPI2EN; }
    static void enable_spi3() { reg(APB1ENR) |= APB1ENR_SPI3EN; }
    static void enable_dma1() { reg(AHB1ENR) |= AHB1ENR_DMA1EN; }
    static void enable_usb_otg_fs() { reg(AHB2ENR) |= AHB2ENR_OTGFSEN; }

    /// Configure system clock to 168MHz using 8MHz HSE
    /// PLL: M=8, N=336, P=2, Q=7 -> SYSCLK=168MHz, USB=48MHz
    static void init_168mhz() {
        // Enable HSE
        reg(CR) |= CR_HSEON;
        while (!(reg(CR) & CR_HSERDY)) {}

        // Enable power interface and set voltage scale 1
        reg(APB1ENR) |= APB1ENR_PWREN;
        *reinterpret_cast<volatile uint32_t*>(0x40007000) |= 3U << 14;  // PWR_CR VOS

        // Configure Flash latency (5 wait states for 168MHz)
        *reinterpret_cast<volatile uint32_t*>(0x40023C00) =
            (5U << 0) |   // LATENCY = 5
            (1U << 8) |   // PRFTEN (prefetch enable)
            (1U << 9) |   // ICEN (instruction cache enable)
            (1U << 10);   // DCEN (data cache enable)

        // Configure PLL: M=8, N=336, P=2, Q=7
        // PLLCFGR = PLLQ | PLLSRC_HSE | PLLP | PLLN | PLLM
        reg(PLLCFGR) =
            (7U << 24) |    // PLLQ = 7 (48MHz for USB)
            (1U << 22) |    // PLLSRC = HSE
            (0U << 16) |    // PLLP = 2 (00 = /2)
            (336U << 6) |   // PLLN = 336
            (8U << 0);      // PLLM = 8

        // Enable PLL
        reg(CR) |= CR_PLLON;
        while (!(reg(CR) & CR_PLLRDY)) {}

        // Configure prescalers: AHB=/1, APB1=/4, APB2=/2
        reg(CFGR) =
            (0U << 4) |     // HPRE = /1 (AHB = 168MHz)
            (5U << 10) |    // PPRE1 = /4 (APB1 = 42MHz)
            (4U << 13);     // PPRE2 = /2 (APB2 = 84MHz)

        // Switch to PLL
        reg(CFGR) = (reg(CFGR) & ~3U) | CFGR_SW_PLL;
        while ((reg(CFGR) & 0x0C) != CFGR_SWS_PLL) {}
    }
};

}  // namespace umi::stm32

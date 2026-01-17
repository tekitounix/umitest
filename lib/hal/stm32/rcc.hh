// SPDX-License-Identifier: MIT
// UMI-OS STM32F4 RCC (Reset and Clock Control)
#pragma once

#include <cstdint>

namespace umi::port::stm32 {

/// STM32F4 Reset and Clock Control
struct RCC {
    static inline volatile uint32_t* const CR       = reinterpret_cast<volatile uint32_t*>(0x40023800);
    static inline volatile uint32_t* const PLLCFGR  = reinterpret_cast<volatile uint32_t*>(0x40023804);
    static inline volatile uint32_t* const CFGR     = reinterpret_cast<volatile uint32_t*>(0x40023808);
    static inline volatile uint32_t* const CIR      = reinterpret_cast<volatile uint32_t*>(0x4002380C);
    static inline volatile uint32_t* const AHB1RSTR = reinterpret_cast<volatile uint32_t*>(0x40023810);
    static inline volatile uint32_t* const AHB2RSTR = reinterpret_cast<volatile uint32_t*>(0x40023814);
    static inline volatile uint32_t* const AHB3RSTR = reinterpret_cast<volatile uint32_t*>(0x40023818);
    static inline volatile uint32_t* const APB1RSTR = reinterpret_cast<volatile uint32_t*>(0x40023820);
    static inline volatile uint32_t* const APB2RSTR = reinterpret_cast<volatile uint32_t*>(0x40023824);
    static inline volatile uint32_t* const AHB1ENR  = reinterpret_cast<volatile uint32_t*>(0x40023830);
    static inline volatile uint32_t* const AHB2ENR  = reinterpret_cast<volatile uint32_t*>(0x40023834);
    static inline volatile uint32_t* const AHB3ENR  = reinterpret_cast<volatile uint32_t*>(0x40023838);
    static inline volatile uint32_t* const APB1ENR  = reinterpret_cast<volatile uint32_t*>(0x40023840);
    static inline volatile uint32_t* const APB2ENR  = reinterpret_cast<volatile uint32_t*>(0x40023844);

    // AHB1ENR bits
    static constexpr uint32_t GPIOAEN = (1 << 0);
    static constexpr uint32_t GPIOBEN = (1 << 1);
    static constexpr uint32_t GPIOCEN = (1 << 2);
    static constexpr uint32_t GPIODEN = (1 << 3);
    static constexpr uint32_t GPIOEEN = (1 << 4);
    static constexpr uint32_t DMA1EN  = (1 << 21);
    static constexpr uint32_t DMA2EN  = (1 << 22);

    // APB1ENR bits
    static constexpr uint32_t TIM2EN   = (1 << 0);
    static constexpr uint32_t TIM3EN   = (1 << 1);
    static constexpr uint32_t TIM4EN   = (1 << 2);
    static constexpr uint32_t USART2EN = (1 << 17);
    static constexpr uint32_t USART3EN = (1 << 18);
    static constexpr uint32_t I2C1EN   = (1 << 21);
    static constexpr uint32_t I2C2EN   = (1 << 22);
    static constexpr uint32_t SPI2EN   = (1 << 14);
    static constexpr uint32_t SPI3EN   = (1 << 15);

    // APB2ENR bits
    static constexpr uint32_t USART1EN = (1 << 4);
    static constexpr uint32_t USART6EN = (1 << 5);
    static constexpr uint32_t SPI1EN   = (1 << 12);
    static constexpr uint32_t TIM1EN   = (1 << 0);

    /// Enable peripheral clock on AHB1
    static void enable_ahb1(uint32_t mask) { *AHB1ENR |= mask; }
    
    /// Enable peripheral clock on APB1
    static void enable_apb1(uint32_t mask) { *APB1ENR |= mask; }
    
    /// Enable peripheral clock on APB2
    static void enable_apb2(uint32_t mask) { *APB2ENR |= mask; }
};

} // namespace umi::port::stm32

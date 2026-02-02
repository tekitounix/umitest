// SPDX-License-Identifier: MIT
// STM32F4 EXTI (External Interrupt) HAL
//
// Provides external interrupt configuration for GPIO pins.
// Reference: RM0090 Section 12.2 (EXTI)
#pragma once

#include <cstdint>

namespace umi::stm32 {

/// EXTI register base address
inline constexpr uint32_t EXTI_BASE = 0x40013C00;

/// SYSCFG register base address (for EXTI line selection)
inline constexpr uint32_t SYSCFG_BASE = 0x40013800;

/// EXTI registers
struct ExtiRegs {
    volatile uint32_t IMR;    // 0x00: Interrupt mask register
    volatile uint32_t EMR;    // 0x04: Event mask register
    volatile uint32_t RTSR;   // 0x08: Rising trigger selection
    volatile uint32_t FTSR;   // 0x0C: Falling trigger selection
    volatile uint32_t SWIER;  // 0x10: Software interrupt event register
    volatile uint32_t PR;     // 0x14: Pending register
};

/// SYSCFG registers (partial, for EXTICR)
struct SyscfgRegs {
    volatile uint32_t MEMRMP;     // 0x00: Memory remap
    volatile uint32_t PMC;        // 0x04: Peripheral mode configuration
    volatile uint32_t EXTICR[4];  // 0x08-0x14: External interrupt configuration
    uint32_t RESERVED[2];
    volatile uint32_t CMPCR;      // 0x20: Compensation cell control
};

/// Port index for EXTI configuration
enum class ExtiPort : uint8_t {
    PA = 0,
    PB = 1,
    PC = 2,
    PD = 3,
    PE = 4,
    PF = 5,
    PG = 6,
    PH = 7,
    PI = 8,
};

/// EXTI Controller
class EXTI {
public:
    /// Configure EXTI line to a specific GPIO port
    /// @param line EXTI line number (0-15, corresponds to pin number)
    /// @param port GPIO port (PA=0, PB=1, PC=2, PD=3, etc.)
    static void config_line(uint8_t line, ExtiPort port) {
        if (line > 15) return;

        auto* syscfg = reinterpret_cast<SyscfgRegs*>(SYSCFG_BASE);
        uint8_t reg_idx = line / 4;      // Which EXTICR register
        uint8_t bit_pos = (line % 4) * 4; // Bit position within register

        uint32_t val = syscfg->EXTICR[reg_idx];
        val &= ~(0xF << bit_pos);                      // Clear bits
        val |= (static_cast<uint8_t>(port) << bit_pos); // Set port
        syscfg->EXTICR[reg_idx] = val;
    }

    /// Enable rising edge trigger
    static void enable_rising(uint8_t line) {
        if (line > 22) return;
        auto* exti = regs();
        exti->RTSR |= (1U << line);
    }

    /// Disable rising edge trigger
    static void disable_rising(uint8_t line) {
        if (line > 22) return;
        auto* exti = regs();
        exti->RTSR &= ~(1U << line);
    }

    /// Enable falling edge trigger
    static void enable_falling(uint8_t line) {
        if (line > 22) return;
        auto* exti = regs();
        exti->FTSR |= (1U << line);
    }

    /// Disable falling edge trigger
    static void disable_falling(uint8_t line) {
        if (line > 22) return;
        auto* exti = regs();
        exti->FTSR &= ~(1U << line);
    }

    /// Enable interrupt for line
    static void enable_interrupt(uint8_t line) {
        if (line > 22) return;
        auto* exti = regs();
        exti->IMR |= (1U << line);
    }

    /// Disable interrupt for line
    static void disable_interrupt(uint8_t line) {
        if (line > 22) return;
        auto* exti = regs();
        exti->IMR &= ~(1U << line);
    }

    /// Check if interrupt is pending
    static bool is_pending(uint8_t line) {
        if (line > 22) return false;
        auto* exti = regs();
        return (exti->PR & (1U << line)) != 0;
    }

    /// Clear pending interrupt
    static void clear_pending(uint8_t line) {
        if (line > 22) return;
        auto* exti = regs();
        exti->PR = (1U << line);  // Write 1 to clear
    }

    /// Generate software interrupt
    static void trigger_software(uint8_t line) {
        if (line > 22) return;
        auto* exti = regs();
        exti->SWIER |= (1U << line);
    }

private:
    static ExtiRegs* regs() {
        return reinterpret_cast<ExtiRegs*>(EXTI_BASE);
    }
};

} // namespace umi::stm32

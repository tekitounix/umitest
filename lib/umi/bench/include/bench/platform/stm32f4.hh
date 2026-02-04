// SPDX-License-Identifier: MIT
#pragma once

#include "bench/core/runner.hh"
#include "bench/output/uart.hh"
#include "bench/timer/dwt.hh"

namespace umi::bench {

/// STM32F4 platform configuration
struct Stm32f4 {
    using Timer = DwtTimer;
    using Output = UartOutput;

    // CoreDebug->DHCSR register address (Debug Halting Control and Status Register)
    static constexpr std::uint32_t coredebug_dhcsr = 0xE000EDF0;
    // Bit 0: C_DEBUGEN - Debugger enabled flag
    static constexpr std::uint32_t dhcsr_c_debugen = 1u << 0;

    static void init() {
        Timer::enable();
        Output::init();
    }

    /// Check if debugger is attached
    static bool is_debugger_attached() {
        return (*reinterpret_cast<volatile std::uint32_t*>(coredebug_dhcsr) & dhcsr_c_debugen) != 0;
    }

    /// Halt execution safely
    /// If debugger is attached, trigger BKPT for debug halt.
    /// If no debugger, enter infinite WFI loop (or reset if watchdog is configured).
    [[noreturn]] static void halt() {
        if (is_debugger_attached()) {
            // BKPT triggers debug halt - Renode can detect this
            asm volatile("bkpt #0");
        }
        // Fallback: infinite loop with WFI to save power
        // Note: If no interrupts are configured, this will hang forever.
        // Consider enabling watchdog timer before calling halt() in production.
        while (true) {
            asm volatile("wfi");
        }
        __builtin_unreachable();
    }
};

} // namespace umi::bench

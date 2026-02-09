// SPDX-License-Identifier: MIT
#pragma once

/// @file
/// @brief CoreSight MMIO register mappings shared across Cortex-M targets.

#include <umimmio/mmio.hh>

namespace umi::port::cortex_m {

namespace mm = umi::mmio;

// NOLINTBEGIN(readability-identifier-naming)

/// @brief Data Watchpoint and Trace (DWT) register map.
struct DWT : mm::Device<mm::RW, mm::DirectTransportTag> {
    /// @brief Base address of DWT registers.
    static constexpr mm::Addr base_address = 0xE0001000;

    /// @brief DWT control register.
    struct CTRL : mm::Register<DWT, 0x00, 32> {
        /// @brief Enable cycle counter bit.
        struct CYCCNTENA : mm::Field<CTRL, 0, 1> {};
    };

    /// @brief Cycle counter register.
    struct CYCCNT : mm::Register<DWT, 0x04, 32> {};
};

/// @brief CoreDebug register map used to enable DWT tracing.
struct CoreDebug : mm::Device<mm::RW, mm::DirectTransportTag> {
    /// @brief Base address of CoreDebug registers.
    static constexpr mm::Addr base_address = 0xE000ED00;

    /// @brief Debug Halting Control and Status Register.
    struct DHCSR : mm::Register<CoreDebug, 0xF0, 32, mm::RO> {
        /// @brief Debug enable bit.
        struct C_DEBUGEN : mm::Field<DHCSR, 0, 1> {};
    };

    /// @brief Debug Exception and Monitor Control Register.
    struct DEMCR : mm::Register<CoreDebug, 0xFC, 32> {
        /// @brief Trace enable bit required for DWT cycle counter.
        struct TRCENA : mm::Field<DEMCR, 24, 1> {};
    };
};

// NOLINTEND(readability-identifier-naming)

} // namespace umi::port::cortex_m

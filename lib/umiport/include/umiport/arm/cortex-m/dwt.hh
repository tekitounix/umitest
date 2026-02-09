// SPDX-License-Identifier: MIT
#pragma once

/// @file
/// @brief Cortex-M DWT cycle counter timer backend.

#include <cstdint>

#include <umiport/arm/cortex-m/cortex_m_mmio.hh>

namespace umi::port::cortex_m {

/// @brief ARM DWT cycle counter timer.
struct DwtTimer {
    /// @brief Counter type read from DWT `CYCCNT`.
    using Counter = std::uint32_t;

    /// @brief Enable and reset the hardware cycle counter.
    static void enable() {
        umi::mmio::DirectTransport<> transport;
        transport.modify(CoreDebug::DEMCR::TRCENA::Set{});
        transport.write(DWT::CYCCNT::value(0u));
        transport.modify(DWT::CTRL::CYCCNTENA::Set{});
    }

    /// @brief Read the current cycle counter.
    /// @return Current `CYCCNT` value.
    static Counter now() {
        umi::mmio::DirectTransport<> transport;
        return transport.read(DWT::CYCCNT{});
    }
};

} // namespace umi::port::cortex_m

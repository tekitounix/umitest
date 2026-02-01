// SPDX-License-Identifier: MIT
// UMI-USB: ASRC Strategy
#pragma once

#include <cstdint>
#include <concepts>

namespace umiusb {

// ============================================================================
// AsrcStrategy Concept
// ============================================================================

template<typename T>
concept AsrcStrategy = requires(T& a, const T& ca) {
    { a.reset() } -> std::same_as<void>;
    { a.update(uint32_t{}, uint32_t{}) } -> std::convertible_to<uint32_t>;
};

// ============================================================================
// PI-LPF ASRC Strategy (Default)
// ============================================================================

/// Default ASRC rate smoother using PI-style low-pass filter.
/// Extracted from AudioInterface::update_asrc_rate.
/// Realtime-safe: pure fixed-point arithmetic.
struct PiLpfAsrc {
    void reset() {
        smoothed_rate_q32 = static_cast<int64_t>(0x10000) << 16;  // 1.0 in Q16.32
        cached_rate_q16 = 0x10000;
        update_counter = 0;
    }

    /// Update ASRC rate from target ratio.
    /// @param target_q16 Target rate in Q16.16 format (e.g., 0x10000 = 1.0x)
    /// @param update_interval How often to actually recalculate (reduces CPU load)
    /// @return Smoothed rate in Q16.16
    uint32_t update(uint32_t target_q16, uint32_t update_interval) {
        if (++update_counter < update_interval) {
            return cached_rate_q16;
        }
        update_counter = 0;

        int64_t target_q32 = static_cast<int64_t>(target_q16) << 16;
        int64_t error_q32 = target_q32 - smoothed_rate_q32;
        smoothed_rate_q32 += (error_q32 * lpf_alpha) >> 32;

        cached_rate_q16 = static_cast<uint32_t>(smoothed_rate_q32 >> 16);
        return cached_rate_q16;
    }

private:
    static constexpr uint32_t lpf_alpha = 2863;  // ~0.0007 in Q32
    int64_t smoothed_rate_q32 = static_cast<int64_t>(0x10000) << 16;
    uint32_t cached_rate_q16 = 0x10000;
    uint32_t update_counter = 0;
};

static_assert(AsrcStrategy<PiLpfAsrc>);

}  // namespace umiusb

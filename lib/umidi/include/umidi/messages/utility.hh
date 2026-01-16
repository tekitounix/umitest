// SPDX-License-Identifier: MIT
// umidi - Utility Messages (MT=0, MIDI 2.0)
// JR Timestamp, JR Clock, NOOP for timing jitter correction
#pragma once

#include "../core/ump.hh"
#include <cstdint>

namespace umidi::message {

// =============================================================================
// JR Timestamp: Jitter Reduction Timestamp (MT=0, 0x0020)
// =============================================================================
// Provides sample-accurate timing for subsequent messages
// Timestamp unit: 1/31250 second (32 microseconds)

struct JRTimestamp {
    UMP32 ump;

    static constexpr uint8_t MT = 0;
    static constexpr uint8_t STATUS = 0x20;

    [[nodiscard]] constexpr uint8_t group() const noexcept { return ump.group(); }

    /// Get timestamp value (0-16383, 14-bit)
    /// Unit: 1/31250 second = 32 microseconds
    [[nodiscard]] constexpr uint16_t timestamp() const noexcept {
        return uint16_t(ump.data1()) | (uint16_t(ump.data2()) << 7);
    }

    /// Convert timestamp to microseconds
    [[nodiscard]] constexpr uint32_t to_microseconds() const noexcept {
        return uint32_t(timestamp()) * 32u;
    }

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return ump.mt() == MT && ump.status() == STATUS;
    }

    /// Create JR Timestamp
    /// @param ts Timestamp value (0-16383)
    /// @param group UMP group (0-15)
    [[nodiscard]] static constexpr JRTimestamp create(uint16_t ts, uint8_t group = 0) noexcept {
        return {UMP32(MT, group, STATUS, ts & 0x7F, (ts >> 7) & 0x7F)};
    }

    /// Create from microseconds (truncates to fit 14-bit range)
    /// @param us Microseconds (0-524287 maps to 0-16383)
    [[nodiscard]] static constexpr JRTimestamp from_microseconds(uint32_t us, uint8_t group = 0) noexcept {
        uint16_t ts = uint16_t((us / 32u) & 0x3FFF);
        return create(ts, group);
    }

    [[nodiscard]] static constexpr JRTimestamp from_ump(UMP32 u) noexcept { return {u}; }
};

// =============================================================================
// JR Clock: Jitter Reduction Clock (MT=0, 0x0010)
// =============================================================================
// Sender's clock for synchronization

struct JRClock {
    UMP32 ump;

    static constexpr uint8_t MT = 0;
    static constexpr uint8_t STATUS = 0x10;

    [[nodiscard]] constexpr uint8_t group() const noexcept { return ump.group(); }

    /// Get clock value (0-16383, 14-bit)
    [[nodiscard]] constexpr uint16_t clock() const noexcept {
        return uint16_t(ump.data1()) | (uint16_t(ump.data2()) << 7);
    }

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return ump.mt() == MT && ump.status() == STATUS;
    }

    [[nodiscard]] static constexpr JRClock create(uint16_t clk, uint8_t group = 0) noexcept {
        return {UMP32(MT, group, STATUS, clk & 0x7F, (clk >> 7) & 0x7F)};
    }

    [[nodiscard]] static constexpr JRClock from_ump(UMP32 u) noexcept { return {u}; }
};

// =============================================================================
// NOOP: No Operation (MT=0, 0x0000)
// =============================================================================
// For maintaining connection or padding

struct NOOP {
    UMP32 ump;

    static constexpr uint8_t MT = 0;
    static constexpr uint8_t STATUS = 0x00;

    [[nodiscard]] constexpr uint8_t group() const noexcept { return ump.group(); }

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return ump.mt() == MT && ump.status() == STATUS;
    }

    [[nodiscard]] static constexpr NOOP create(uint8_t group = 0) noexcept {
        return {UMP32(MT, group, STATUS, 0, 0)};
    }

    [[nodiscard]] static constexpr NOOP from_ump(UMP32 u) noexcept { return {u}; }
};

// =============================================================================
// JR Timestamp Tracker: Track and apply jitter reduction
// =============================================================================
// Converts JR timestamps to sample positions for audio processing

class JRTimestampTracker {
public:
    /// Configure sample rate
    constexpr void set_sample_rate(uint32_t rate) noexcept {
        sample_rate_ = rate;
        // 1 JR tick = 32us = 32/1000000 seconds
        // samples per JR tick = sample_rate * 32 / 1000000
        // Use fixed-point: multiply by 32, divide by 1000000
        samples_per_tick_fp_ = (rate * 32u) / 1000u;  // Fixed-point * 1000
    }

    /// Process JR Timestamp message
    constexpr void process(const JRTimestamp& ts) noexcept {
        last_timestamp_ = ts.timestamp();
        has_timestamp_ = true;
    }

    /// Get sample offset for current timestamp
    /// @return Sample offset from buffer start (0 if no timestamp)
    [[nodiscard]] constexpr uint32_t get_sample_offset() const noexcept {
        if (!has_timestamp_) return 0;
        // Convert JR ticks to samples
        return (last_timestamp_ * samples_per_tick_fp_) / 1000u;
    }

    /// Clear current timestamp (call at start of each audio buffer)
    constexpr void clear() noexcept {
        has_timestamp_ = false;
    }

    /// Check if timestamp is available
    [[nodiscard]] constexpr bool has_timestamp() const noexcept {
        return has_timestamp_;
    }

private:
    uint32_t sample_rate_ = 48000;
    uint32_t samples_per_tick_fp_ = (48000 * 32u) / 1000u;
    uint16_t last_timestamp_ = 0;
    bool has_timestamp_ = false;
};

} // namespace umidi::message

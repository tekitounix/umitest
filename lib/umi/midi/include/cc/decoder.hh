// SPDX-License-Identifier: MIT
// umidi - CC Decoder with RPN/NRPN State Machine
// Optimized for ARM Cortex-M (no heap allocation)
#pragma once

#include "types.hh"
#include "standards.hh"
#include <cstdint>
#include <cstddef>

namespace umidi::cc {

// =============================================================================
// 14-bit CC State (per channel)
// =============================================================================

template <typename CCType>
struct CC14BitState {
    uint8_t msb = 0;
    uint16_t value = 0;

    [[nodiscard]] constexpr uint16_t parse_msb(uint8_t v) noexcept {
        msb = v & 0x7F;
        value = uint16_t(msb) << 7;
        return value;
    }

    [[nodiscard]] constexpr uint16_t parse_lsb(uint8_t v) noexcept {
        value = (uint16_t(msb) << 7) | (v & 0x7F);
        return value;
    }
};

// =============================================================================
// RPN/NRPN State Machine (per channel)
// =============================================================================
// Handles: CC99 (NRPN MSB) → CC98 (NRPN LSB) → CC6 (Data Entry MSB) → CC38 (Data Entry LSB)
//          CC101 (RPN MSB) → CC100 (RPN LSB) → CC6 (Data Entry MSB) → CC38 (Data Entry LSB)

class ParameterNumberState {
public:
    enum class Phase : uint8_t {
        IDLE,
        PARAMETER_SELECTED,  // RPN/NRPN MSB+LSB received
        DATA_MSB_RECEIVED    // Data Entry MSB received, waiting for LSB
    };

    /// Process CC message
    /// @return true if a complete RPN/NRPN value is ready
    [[nodiscard]] constexpr bool process(uint8_t cc_num, uint8_t value) noexcept {
        switch (cc_num) {
        case 101:  // RPN MSB
            rpn_msb_ = value;
            is_nrpn_ = false;
            phase_ = Phase::PARAMETER_SELECTED;
            return false;

        case 100:  // RPN LSB
            rpn_lsb_ = value;
            is_nrpn_ = false;
            phase_ = Phase::PARAMETER_SELECTED;
            return false;

        case 99:   // NRPN MSB
            rpn_msb_ = value;
            is_nrpn_ = true;
            phase_ = Phase::PARAMETER_SELECTED;
            return false;

        case 98:   // NRPN LSB
            rpn_lsb_ = value;
            is_nrpn_ = true;
            phase_ = Phase::PARAMETER_SELECTED;
            return false;

        case 6:    // Data Entry MSB
            if (phase_ != Phase::IDLE) {
                data_value_ = (uint16_t(value) << 7) | (data_value_ & 0x7F);
                phase_ = Phase::DATA_MSB_RECEIVED;
                return true;  // Coarse value ready
            }
            return false;

        case 38:   // Data Entry LSB
            if (phase_ == Phase::DATA_MSB_RECEIVED) {
                data_value_ = (data_value_ & 0x3F80) | value;
                return true;  // Fine value ready
            }
            return false;

        case 96:   // Data Increment
            if (phase_ != Phase::IDLE && data_value_ < 0x3FFF) {
                ++data_value_;
                return true;
            }
            return false;

        case 97:   // Data Decrement
            if (phase_ != Phase::IDLE && data_value_ > 0) {
                --data_value_;
                return true;
            }
            return false;

        default:
            return false;
        }
    }

    /// Get selected parameter number (14-bit)
    [[nodiscard]] constexpr uint16_t parameter_number() const noexcept {
        return (uint16_t(rpn_msb_) << 7) | rpn_lsb_;
    }

    /// Check if NRPN (vs RPN)
    [[nodiscard]] constexpr bool is_nrpn() const noexcept { return is_nrpn_; }

    /// Get current data value (14-bit)
    [[nodiscard]] constexpr uint16_t data_value() const noexcept { return data_value_; }

    /// Check if RPN Null (0x3FFF) is selected
    [[nodiscard]] constexpr bool is_null() const noexcept {
        return !is_nrpn_ && parameter_number() == 0x3FFF;
    }

    /// Check if a parameter is currently selected
    [[nodiscard]] constexpr bool is_active() const noexcept {
        return phase_ != Phase::IDLE && !is_null();
    }

    /// Reset state
    constexpr void reset() noexcept {
        phase_ = Phase::IDLE;
        rpn_msb_ = 0x7F;
        rpn_lsb_ = 0x7F;
        data_value_ = 0;
        is_nrpn_ = false;
    }

    /// Check if specific RPN is selected
    template <typename T>
    [[nodiscard]] constexpr bool is_selected() const noexcept {
        return phase_ != Phase::IDLE &&
               is_nrpn_ == T::is_nrpn() &&
               parameter_number() == T::number();
    }

private:
    Phase phase_ = Phase::IDLE;
    uint8_t rpn_msb_ = 0x7F;   // 0x7F = NULL
    uint8_t rpn_lsb_ = 0x7F;
    uint16_t data_value_ = 0;
    bool is_nrpn_ = false;
};

// =============================================================================
// Multi-channel RPN/NRPN Decoder
// =============================================================================

class ParameterNumberDecoder {
public:
    /// Result of decoding
    struct Result {
        uint16_t parameter_number;
        uint16_t value;
        bool is_nrpn;
        bool complete;  // True if value is ready
    };

    /// Process CC message
    /// @param channel MIDI channel (0-15)
    /// @param cc_num CC number (0-127)
    /// @param value CC value (0-127)
    /// @return Result with complete=true if RPN/NRPN value is ready
    [[nodiscard]] constexpr Result decode(uint8_t channel, uint8_t cc_num, uint8_t value) noexcept {
        if (channel >= 16) {
            return {0, 0, false, false};
        }

        auto& state = channels_[channel];
        bool complete = state.process(cc_num, value);

        return {
            state.parameter_number(),
            state.data_value(),
            state.is_nrpn(),
            complete && state.is_active()
        };
    }

    /// Check if specific RPN/NRPN is selected on channel
    template <typename T>
    [[nodiscard]] constexpr bool is_selected(uint8_t channel) const noexcept {
        if (channel >= 16) return false;
        return channels_[channel].template is_selected<T>();
    }

    /// Get raw state for channel
    [[nodiscard]] constexpr const ParameterNumberState& get_state(uint8_t channel) const noexcept {
        return channels_[channel < 16 ? channel : 0];
    }

    /// Reset channel
    constexpr void reset_channel(uint8_t channel) noexcept {
        if (channel < 16) {
            channels_[channel].reset();
        }
    }

    /// Reset all channels
    constexpr void reset_all() noexcept {
        for (auto& ch : channels_) {
            ch.reset();
        }
    }

private:
    ParameterNumberState channels_[16] = {};
};

// =============================================================================
// Standard RPN Pitch Bend Sensitivity Helper
// =============================================================================

namespace pitch_bend_sensitivity {

/// Parse RPN 0x0000 value to semitones and cents
struct Value {
    uint8_t semitones;
    uint8_t cents;

    [[nodiscard]] constexpr float to_float() const noexcept {
        return float(semitones) + float(cents) / 100.0f;
    }
};

[[nodiscard]] constexpr Value parse(uint16_t rpn_value) noexcept {
    return {uint8_t((rpn_value >> 7) & 0x7F), uint8_t(rpn_value & 0x7F)};
}

[[nodiscard]] constexpr uint16_t make(uint8_t semitones, uint8_t cents) noexcept {
    return (uint16_t(semitones & 0x7F) << 7) | (cents & 0x7F);
}

/// Default: +/- 2 semitones
inline constexpr uint16_t DEFAULT = make(2, 0);

} // namespace pitch_bend_sensitivity

} // namespace umidi::cc

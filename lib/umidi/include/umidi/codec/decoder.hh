// SPDX-License-Identifier: MIT
// umidi - Template Static Decoder
// Only instantiates code for message types actually used
#pragma once

#include "../core/ump.hh"
#include "../core/result.hh"
#include "../messages/channel_voice.hh"
#include "../messages/system.hh"
#include "../messages/sysex.hh"
#include "../messages/utility.hh"
#include <cstdint>
#include <type_traits>

namespace umidi::codec {

// =============================================================================
// Message Type Traits
// =============================================================================

template <typename T>
struct message_traits {
    static constexpr bool is_channel_voice = false;
    static constexpr bool is_system = false;
    static constexpr bool is_sysex = false;
    static constexpr bool is_utility = false;
};

// Channel Voice specializations
template <> struct message_traits<message::NoteOn> {
    static constexpr bool is_channel_voice = true;
    static constexpr uint8_t status_high = 0x90;
};
template <> struct message_traits<message::NoteOff> {
    static constexpr bool is_channel_voice = true;
    static constexpr uint8_t status_high = 0x80;
};
template <> struct message_traits<message::ControlChange> {
    static constexpr bool is_channel_voice = true;
    static constexpr uint8_t status_high = 0xB0;
};
template <> struct message_traits<message::ProgramChange> {
    static constexpr bool is_channel_voice = true;
    static constexpr uint8_t status_high = 0xC0;
};
template <> struct message_traits<message::PitchBend> {
    static constexpr bool is_channel_voice = true;
    static constexpr uint8_t status_high = 0xE0;
};
template <> struct message_traits<message::ChannelPressure> {
    static constexpr bool is_channel_voice = true;
    static constexpr uint8_t status_high = 0xD0;
};
template <> struct message_traits<message::PolyPressure> {
    static constexpr bool is_channel_voice = true;
    static constexpr uint8_t status_high = 0xA0;
};

// System specializations
template <> struct message_traits<message::TimingClock> {
    static constexpr bool is_system = true;
    static constexpr uint8_t status = 0xF8;
};
template <> struct message_traits<message::Start> {
    static constexpr bool is_system = true;
    static constexpr uint8_t status = 0xFA;
};
template <> struct message_traits<message::Continue> {
    static constexpr bool is_system = true;
    static constexpr uint8_t status = 0xFB;
};
template <> struct message_traits<message::Stop> {
    static constexpr bool is_system = true;
    static constexpr uint8_t status = 0xFC;
};

// Utility specializations
template <> struct message_traits<message::JRTimestamp> {
    static constexpr bool is_utility = true;
};
template <> struct message_traits<message::JRClock> {
    static constexpr bool is_utility = true;
};

// SysEx specialization
template <> struct message_traits<message::SysEx7> {
    static constexpr bool is_sysex = true;
};

// =============================================================================
// Channel Configuration
// =============================================================================

enum class MidiMode : uint8_t {
    OMNI_ON_POLY  = 1,  // Mode 1: Receive all channels
    OMNI_ON_MONO  = 2,  // Mode 2: Receive all channels, mono
    OMNI_OFF_POLY = 3,  // Mode 3: Receive single channel
    OMNI_OFF_MONO = 4   // Mode 4: Receive single channel, mono
};

struct ChannelConfig {
    uint8_t basic_channel = 0;
    uint8_t min_channel = 0;
    uint8_t max_channel = 15;
    MidiMode mode = MidiMode::OMNI_ON_POLY;

    [[nodiscard]] constexpr bool omni_mode() const noexcept {
        return mode == MidiMode::OMNI_ON_POLY || mode == MidiMode::OMNI_ON_MONO;
    }

    [[nodiscard]] constexpr bool should_accept(uint8_t channel) const noexcept {
        if (omni_mode()) {
            return channel >= min_channel && channel <= max_channel;
        }
        return channel == basic_channel;
    }
};

inline constexpr ChannelConfig default_config{};

constexpr ChannelConfig single_channel_config(uint8_t ch) {
    return {ch, ch, ch, MidiMode::OMNI_OFF_POLY};
}

// =============================================================================
// Template Static Decoder
// =============================================================================
// Only instantiates code paths for message types in MessageTypes...

template <ChannelConfig Config = default_config, typename... MessageTypes>
class Decoder {
public:
    /// Check at compile time if a message type is supported
    template <typename T>
    static constexpr bool is_supported() noexcept {
        return (std::is_same_v<T, MessageTypes> || ...);
    }

    /// Decode a single MIDI byte
    /// @param byte Input byte
    /// @param out Output UMP32 (valid only if returns Ok)
    /// @return Ok(true) if message complete, Ok(false) if waiting for more data, Err on error
    [[nodiscard]] constexpr Result<bool> decode_byte(uint8_t byte, UMP32& out) noexcept {
        // Real-time messages (always pass through, don't affect state)
        if (byte >= 0xF8) {
            return decode_realtime(byte, out);
        }

        // Status byte
        if (byte & 0x80) {
            return handle_status(byte, out);
        }

        // Data byte
        return handle_data(byte, out);
    }

    /// Reset decoder state
    constexpr void reset() noexcept {
        state_ = State::IDLE;
        running_status_ = 0;
        data1_ = 0;
    }

    /// Get current group
    [[nodiscard]] constexpr uint8_t group() const noexcept { return group_; }

    /// Set current group
    constexpr void set_group(uint8_t g) noexcept { group_ = g & 0x0F; }

private:
    enum class State : uint8_t { IDLE, WAITING_DATA1, WAITING_DATA2, SYSEX };

    State state_ = State::IDLE;
    uint8_t running_status_ = 0;
    uint8_t data1_ = 0;
    uint8_t group_ = 0;

    // === Real-time message handling ===
    [[nodiscard]] constexpr Result<bool> decode_realtime(uint8_t status, UMP32& out) noexcept {
        // Only process if supported
        if constexpr (is_supported<message::TimingClock>()) {
            if (status == 0xF8) {
                out = UMP32::timing_clock(group_);
                return Ok(true);
            }
        }
        if constexpr (is_supported<message::Start>()) {
            if (status == 0xFA) {
                out = UMP32::start(group_);
                return Ok(true);
            }
        }
        if constexpr (is_supported<message::Continue>()) {
            if (status == 0xFB) {
                out = UMP32::continue_msg(group_);
                return Ok(true);
            }
        }
        if constexpr (is_supported<message::Stop>()) {
            if (status == 0xFC) {
                out = UMP32::stop(group_);
                return Ok(true);
            }
        }

        // Unsupported real-time - pass through as raw
        out.word = 0x10000000u | (uint32_t(status) << 16);
        return Ok(true);
    }

    // === Status byte handling ===
    [[nodiscard]] constexpr Result<bool> handle_status(uint8_t status, UMP32& out) noexcept {
        uint8_t cmd = status & 0xF0;
        uint8_t channel = status & 0x0F;

        // Channel filtering
        if (cmd >= 0x80 && cmd <= 0xE0) {
            if (!Config.should_accept(channel)) {
                state_ = State::IDLE;
                return Err(ErrorCode::ChannelFiltered);
            }
        }

        // System common messages
        if (status >= 0xF0) {
            return handle_system_common(status, out);
        }

        // Channel messages
        running_status_ = status;
        state_ = State::WAITING_DATA1;

        // Check if supported
        bool supported = false;
        if constexpr (is_supported<message::NoteOff>()) {
            supported |= (cmd == 0x80);
        }
        if constexpr (is_supported<message::NoteOn>()) {
            supported |= (cmd == 0x90);
        }
        if constexpr (is_supported<message::PolyPressure>()) {
            supported |= (cmd == 0xA0);
        }
        if constexpr (is_supported<message::ControlChange>()) {
            supported |= (cmd == 0xB0);
        }
        if constexpr (is_supported<message::ProgramChange>()) {
            supported |= (cmd == 0xC0);
        }
        if constexpr (is_supported<message::ChannelPressure>()) {
            supported |= (cmd == 0xD0);
        }
        if constexpr (is_supported<message::PitchBend>()) {
            supported |= (cmd == 0xE0);
        }

        if (!supported) {
            state_ = State::IDLE;
            return Err(ErrorCode::NotSupported);
        }

        return Ok(false);  // Waiting for data
    }

    // === System common handling ===
    [[nodiscard]] constexpr Result<bool> handle_system_common(uint8_t status, UMP32& out) noexcept {
        running_status_ = 0;  // System common clears running status

        switch (status) {
        case 0xF0:  // SysEx Start
            if constexpr (is_supported<message::SysEx7>()) {
                state_ = State::SYSEX;
                return Ok(false);
            }
            state_ = State::IDLE;
            return Err(ErrorCode::NotSupported);

        case 0xF7:  // SysEx End
            state_ = State::IDLE;
            return Ok(false);

        case 0xF1:  // MTC Quarter Frame
        case 0xF3:  // Song Select
            state_ = State::WAITING_DATA1;
            running_status_ = status;
            return Ok(false);

        case 0xF2:  // Song Position
            state_ = State::WAITING_DATA1;
            running_status_ = status;
            return Ok(false);

        case 0xF6:  // Tune Request
            out.word = 0x10000000u | (uint32_t(status) << 16);
            return Ok(true);

        default:
            return Err(ErrorCode::InvalidData);
        }
    }

    // === Data byte handling ===
    [[nodiscard]] constexpr Result<bool> handle_data(uint8_t byte, UMP32& out) noexcept {
        if (state_ == State::IDLE) {
            return Err(ErrorCode::InvalidData);
        }

        if (state_ == State::WAITING_DATA1) {
            data1_ = byte;

            // Check for 2-byte messages
            uint8_t cmd = running_status_ & 0xF0;
            if (cmd == 0xC0 || cmd == 0xD0 || running_status_ == 0xF1 || running_status_ == 0xF3) {
                return complete_message(0, out);
            }

            state_ = State::WAITING_DATA2;
            return Ok(false);
        }

        if (state_ == State::WAITING_DATA2) {
            return complete_message(byte, out);
        }

        return Err(ErrorCode::InvalidData);
    }

    // === Complete message ===
    [[nodiscard]] constexpr Result<bool> complete_message(uint8_t data2, UMP32& out) noexcept {
        state_ = State::IDLE;

        // System messages (MT=1)
        if (running_status_ >= 0xF0) {
            out.word = 0x10000000u | (uint32_t(running_status_) << 16) |
                       (uint32_t(data1_) << 8) | data2;
            return Ok(true);
        }

        // Channel messages (MT=2)
        out.word = 0x20000000u | (uint32_t(group_) << 24) |
                   (uint32_t(running_status_) << 16) |
                   (uint32_t(data1_) << 8) | data2;

        return Ok(true);
    }
};

// =============================================================================
// Common Decoder Configurations
// =============================================================================

/// Full MIDI 1.0 decoder (all channel voice + realtime)
using FullDecoder = Decoder<
    default_config,
    message::NoteOn, message::NoteOff,
    message::ControlChange, message::ProgramChange,
    message::PitchBend, message::ChannelPressure, message::PolyPressure,
    message::TimingClock, message::Start, message::Continue, message::Stop
>;

/// Minimal synth decoder (Note On/Off only)
using SynthDecoder = Decoder<
    default_config,
    message::NoteOn, message::NoteOff
>;

/// Note + CC decoder (common for synths with mod wheel etc.)
using NoteCCDecoder = Decoder<
    default_config,
    message::NoteOn, message::NoteOff, message::ControlChange, message::PitchBend
>;

/// Single-channel decoder factory
template <uint8_t Channel, typename... MessageTypes>
using SingleChannelDecoder = Decoder<single_channel_config(Channel), MessageTypes...>;

} // namespace umidi::codec

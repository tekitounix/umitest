// SPDX-License-Identifier: MIT
#pragma once

#include "ump.hh"
#include "result.hh"
#include <cstdint>

namespace umidi {

/// MIDI 1.0 byte stream to UMP32 parser.
class Parser {
public:
    constexpr Parser() noexcept = default;

    /// Parse one byte, returns true when message complete.
    [[nodiscard]] constexpr bool parse(uint8_t byte, UMP32& out) noexcept {
        // Real-time messages: 1-byte, don't affect running status
        if (byte >= 0xF8) {
            out.word = 0x10000000u | (uint32_t(byte) << 16);
            return true;
        }

        // Status byte
        if (byte & 0x80) {
            if (byte >= 0xF0) {
                return handle_system_common(byte, out);
            }

            partial_ = 0x20000000u | (uint32_t(byte) << 16);
            count_ = 0;
            running_status_ = byte;
            is_2byte_ = ((byte & 0xE0) == 0xC0);
            return false;
        }

        // Data byte
        if (count_ == 0) {
            partial_ |= (uint32_t(byte) << 8);
            count_ = 1;

            if (is_2byte_) {
                out.word = partial_;
                return true;
            }
            return false;
        }

        out.word = partial_ | byte;
        count_ = 0;
        return true;
    }

    /// Parse with running status support.
    [[nodiscard]] constexpr bool parse_running(uint8_t byte, UMP32& out) noexcept {
        if (byte >= 0xF8) {
            out.word = 0x10000000u | (uint32_t(byte) << 16);
            return true;
        }

        if (byte & 0x80) {
            return parse(byte, out);
        }

        if (running_status_ == 0) {
            return false;
        }

        if (count_ == 0 && (partial_ & 0x00FF0000u) == 0) {
            partial_ = 0x20000000u | (uint32_t(running_status_) << 16);
            is_2byte_ = ((running_status_ & 0xE0) == 0xC0);
        }

        if (count_ == 0) {
            partial_ |= (uint32_t(byte) << 8);
            count_ = 1;

            if (is_2byte_) {
                out.word = partial_;
                partial_ = 0x20000000u | (uint32_t(running_status_) << 16);
                count_ = 0;
                return true;
            }
            return false;
        }

        out.word = partial_ | byte;
        partial_ = 0x20000000u | (uint32_t(running_status_) << 16);
        count_ = 0;
        return true;
    }

    constexpr void reset() noexcept {
        partial_ = 0;
        count_ = 0;
        is_2byte_ = false;
        running_status_ = 0;
    }

    [[nodiscard]] constexpr uint8_t running_status() const noexcept {
        return running_status_;
    }

private:
    uint32_t partial_ = 0;
    uint8_t running_status_ = 0;
    uint8_t count_ = 0;
    bool is_2byte_ = false;

    constexpr bool handle_system_common(uint8_t status, UMP32& out) noexcept {
        running_status_ = 0;

        switch (status) {
        case 0xF0:
        case 0xF7:
            partial_ = 0;
            count_ = 0;
            return false;

        case 0xF1:
        case 0xF3:
            partial_ = 0x10000000u | (uint32_t(status) << 16);
            count_ = 0;
            is_2byte_ = true;
            return false;

        case 0xF2:
            partial_ = 0x10000000u | (uint32_t(status) << 16);
            count_ = 0;
            is_2byte_ = false;
            return false;

        case 0xF4:
        case 0xF5:
            return false;

        case 0xF6:
            out.word = 0x10000000u | (uint32_t(status) << 16);
            return true;

        default:
            return false;
        }
    }
};

/// UMP32 to MIDI 1.0 byte serializer.
class Serializer {
public:
    /// Serialize UMP32 to bytes, returns byte count (0-3).
    [[nodiscard]] static constexpr size_t serialize(const UMP32& ump, uint8_t* out) noexcept {
        uint32_t w = ump.word;
        uint8_t mt = w >> 28;

        if (mt == 1) {
            out[0] = (w >> 16) & 0xFF;
            uint8_t status = out[0];

            if (status >= 0xF8) return 1;
            if (status == 0xF6) return 1;
            if (status == 0xF1 || status == 0xF3) {
                out[1] = (w >> 8) & 0x7F;
                return 2;
            }
            if (status == 0xF2) {
                out[1] = (w >> 8) & 0x7F;
                out[2] = w & 0x7F;
                return 3;
            }
            return 0;
        }

        if (mt != 2) return 0;

        out[0] = (w >> 16) & 0xFF;
        out[1] = (w >> 8) & 0x7F;

        uint8_t cmd = out[0] & 0xF0;
        if ((cmd & 0xE0) == 0xC0) return 2;

        out[2] = w & 0x7F;
        return 3;
    }

    /// Serialize with running status optimization.
    [[nodiscard]] static constexpr size_t serialize_running(
        const UMP32& ump, uint8_t* out, uint8_t& running_status) noexcept
    {
        uint32_t w = ump.word;
        uint8_t mt = w >> 28;

        if (mt == 1) {
            running_status = 0;
            return serialize(ump, out);
        }

        if (mt != 2) return 0;

        uint8_t status = (w >> 16) & 0xFF;
        uint8_t d1 = (w >> 8) & 0x7F;
        uint8_t d2 = w & 0x7F;

        if (status == running_status) {
            out[0] = d1;
            if ((status & 0xE0) == 0xC0) return 1;
            out[1] = d2;
            return 2;
        }

        running_status = status;
        out[0] = status;
        out[1] = d1;
        if ((status & 0xE0) == 0xC0) return 2;
        out[2] = d2;
        return 3;
    }
};

} // namespace umidi

// SPDX-License-Identifier: MIT
/// @file message.hh
/// @brief UMI protocol message builder and parser
#pragma once

#include "encoding.hh"
#include "commands.hh"
#include <cstdint>
#include <cstddef>

namespace umidi::protocol {

/// @brief Build a UMI SysEx message.
/// @tparam MaxPayload Maximum payload size
template <size_t MaxPayload = 256>
class MessageBuilder {
public:
    /// @brief Start building a new message.
    constexpr void begin(Command cmd, uint8_t seq) noexcept {
        pos_ = 0;
        buffer_[pos_++] = 0xF0;
        for (size_t i = 0; i < UMI_SYSEX_ID_LEN; ++i) {
            buffer_[pos_++] = UMI_SYSEX_ID[i];
        }
        cmd_start_ = pos_;
        buffer_[pos_++] = static_cast<uint8_t>(cmd);
        buffer_[pos_++] = seq & 0x7F;
    }

    /// @brief Add raw 7-bit data (already encoded).
    constexpr bool add_raw(const uint8_t* data, size_t len) noexcept {
        if (pos_ + len + 2 > sizeof(buffer_)) return false;
        for (size_t i = 0; i < len; ++i) {
            buffer_[pos_++] = data[i] & 0x7F;
        }
        return true;
    }

    /// @brief Add 8-bit data (will be 7-bit encoded).
    constexpr bool add_data(const uint8_t* data, size_t len) noexcept {
        size_t enc_len = encoded_size(len);
        if (pos_ + enc_len + 2 > sizeof(buffer_)) return false;
        pos_ += encode_7bit(data, len, &buffer_[pos_]);
        return true;
    }

    /// @brief Add single byte.
    constexpr bool add_byte(uint8_t byte) noexcept {
        if (pos_ + 3 > sizeof(buffer_)) return false;
        buffer_[pos_++] = byte & 0x7F;
        return true;
    }

    /// @brief Add 16-bit value (big endian, 7-bit encoded).
    constexpr bool add_u16(uint16_t value) noexcept {
        uint8_t data[2] = {
            static_cast<uint8_t>(value >> 8),
            static_cast<uint8_t>(value & 0xFF)
        };
        return add_data(data, 2);
    }

    /// @brief Add 32-bit value (big endian, 7-bit encoded).
    constexpr bool add_u32(uint32_t value) noexcept {
        uint8_t data[4] = {
            static_cast<uint8_t>(value >> 24),
            static_cast<uint8_t>((value >> 16) & 0xFF),
            static_cast<uint8_t>((value >> 8) & 0xFF),
            static_cast<uint8_t>(value & 0xFF)
        };
        return add_data(data, 4);
    }

    /// @brief Finalize message (add checksum and F7).
    /// @return Total message length
    constexpr size_t finalize() noexcept {
        uint8_t checksum = calculate_checksum(&buffer_[cmd_start_],
                                               pos_ - cmd_start_);
        buffer_[pos_++] = checksum;
        buffer_[pos_++] = 0xF7;
        return pos_;
    }

    /// @brief Get message buffer.
    [[nodiscard]] constexpr const uint8_t* data() const noexcept {
        return buffer_;
    }

    /// @brief Get current message length.
    [[nodiscard]] constexpr size_t size() const noexcept { return pos_; }

private:
    uint8_t buffer_[1 + UMI_SYSEX_ID_LEN + 2 + encoded_size(MaxPayload) + 2]{};
    size_t pos_ = 0;
    size_t cmd_start_ = 0;
};

/// @brief Parsed UMI SysEx message.
struct ParsedMessage {
    Command command;
    uint8_t sequence;
    const uint8_t* payload;  ///< 7-bit encoded payload
    size_t payload_len;
    bool valid;

    /// @brief Decode payload to 8-bit data.
    size_t decode_payload(uint8_t* out, size_t max_len) const noexcept {
        if (!valid || !payload || payload_len == 0) return 0;
        size_t dec_len = decoded_size(payload_len);
        if (dec_len > max_len) return 0;
        return decode_7bit(payload, payload_len, out);
    }
};

/// @brief Parse a UMI SysEx message.
/// @param data SysEx data (including F0 and F7)
/// @param len Data length
/// @return Parsed message
inline constexpr ParsedMessage parse_message(const uint8_t* data,
                                              size_t len) noexcept {
    ParsedMessage msg{};
    msg.valid = false;

    // Minimum: F0 + ID(3) + CMD + SEQ + CHECKSUM + F7 = 8
    if (len < 8) return msg;
    if (data[0] != 0xF0 || data[len - 1] != 0xF7) return msg;

    for (size_t i = 0; i < UMI_SYSEX_ID_LEN; ++i) {
        if (data[1 + i] != UMI_SYSEX_ID[i]) return msg;
    }

    size_t cmd_pos = 1 + UMI_SYSEX_ID_LEN;
    size_t checksum_pos = len - 2;
    uint8_t expected = data[checksum_pos];
    uint8_t actual = calculate_checksum(&data[cmd_pos], checksum_pos - cmd_pos);
    if (expected != actual) return msg;

    msg.command = static_cast<Command>(data[cmd_pos]);
    msg.sequence = data[cmd_pos + 1];
    msg.payload = &data[cmd_pos + 2];
    msg.payload_len = checksum_pos - cmd_pos - 2;
    msg.valid = true;
    return msg;
}

} // namespace umidi::protocol

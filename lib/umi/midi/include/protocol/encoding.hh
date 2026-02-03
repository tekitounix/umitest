// SPDX-License-Identifier: MIT
/// @file encoding.hh
/// @brief 7-bit SysEx encoding/decoding utilities
#pragma once

#include <cstdint>
#include <cstddef>

namespace umidi::protocol {

/// @brief Encode 8-bit data to 7-bit SysEx format.
/// @param in Input buffer (8-bit data)
/// @param in_len Input length
/// @param out Output buffer (must be at least encoded_size(in_len))
/// @return Output length
inline constexpr size_t encode_7bit(const uint8_t* in, size_t in_len,
                                    uint8_t* out) noexcept {
    size_t out_pos = 0;
    for (size_t i = 0; i < in_len; i += 7) {
        uint8_t msb_byte = 0;
        size_t chunk_len = (in_len - i < 7) ? (in_len - i) : 7;
        for (size_t j = 0; j < chunk_len; ++j) {
            if (in[i + j] & 0x80) {
                msb_byte |= (1 << j);
            }
        }
        out[out_pos++] = msb_byte;
        for (size_t j = 0; j < chunk_len; ++j) {
            out[out_pos++] = in[i + j] & 0x7F;
        }
    }
    return out_pos;
}

/// @brief Decode 7-bit SysEx format to 8-bit data.
/// @param in Input buffer (7-bit data)
/// @param in_len Input length
/// @param out Output buffer
/// @return Output length, or 0 if invalid
inline constexpr size_t decode_7bit(const uint8_t* in, size_t in_len,
                                    uint8_t* out) noexcept {
    size_t out_pos = 0;
    size_t in_pos = 0;
    while (in_pos < in_len) {
        uint8_t msb_byte = in[in_pos++];
        if (msb_byte > 0x7F) return 0;
        size_t remaining = in_len - in_pos;
        size_t chunk_len = (remaining < 7) ? remaining : 7;
        for (size_t j = 0; j < chunk_len; ++j) {
            if (in_pos >= in_len) break;
            uint8_t byte = in[in_pos++];
            if (byte > 0x7F) return 0;
            if (msb_byte & (1 << j)) {
                byte |= 0x80;
            }
            out[out_pos++] = byte;
        }
    }
    return out_pos;
}

/// @brief Calculate encoded size for given input length.
inline constexpr size_t encoded_size(size_t in_len) noexcept {
    return (in_len / 7) * 8 + (in_len % 7 ? (in_len % 7) + 1 : 0);
}

/// @brief Calculate decoded size for given encoded length.
inline constexpr size_t decoded_size(size_t in_len) noexcept {
    return (in_len / 8) * 7 + (in_len % 8 ? (in_len % 8) - 1 : 0);
}

/// @brief Calculate XOR checksum over data.
inline constexpr uint8_t calculate_checksum(const uint8_t* data,
                                            size_t len) noexcept {
    uint8_t checksum = 0;
    for (size_t i = 0; i < len; ++i) {
        checksum ^= data[i];
    }
    return checksum & 0x7F;
}

// Note: CRC-32 is provided in umi_firmware.hh (table-based, more efficient)

} // namespace umidi::protocol

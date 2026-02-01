// SPDX-License-Identifier: MIT
// UMI-USB: Sample Codec Strategy
#pragma once

#include <cstdint>
#include <concepts>

namespace umiusb {

// ============================================================================
// SampleCodec Concept
// ============================================================================

template<typename T, typename SampleT>
concept SampleCodec = requires(const T& c, const uint8_t* src, uint8_t* dst, SampleT sample) {
    { c.decode_i16(int16_t{}) } -> std::convertible_to<SampleT>;
    { c.decode_i24(src) } -> std::convertible_to<SampleT>;
    { c.encode_i16(sample) } -> std::convertible_to<int16_t>;
    { T::encode_i24(sample, dst) } -> std::same_as<void>;
};

// ============================================================================
// Default Sample Codec
// ============================================================================

/// Default codec extracted from AudioInterface::sample_from_i16/i24/to_i16/i24.
/// Realtime-safe: pure arithmetic, no allocations.
template<typename SampleT = int32_t>
struct DefaultSampleCodec {
    /// Decode 16-bit sample to internal format (left-shift by 8)
    [[nodiscard]] constexpr SampleT decode_i16(int16_t value) const {
        return static_cast<SampleT>(static_cast<int32_t>(value) << 8);
    }

    /// Decode 24-bit LE sample to internal format
    [[nodiscard]] SampleT decode_i24(const uint8_t* data) const {
        uint32_t raw = static_cast<uint32_t>(data[0])
                     | (static_cast<uint32_t>(data[1]) << 8)
                     | (static_cast<uint32_t>(data[2]) << 16);
        // Sign-extend from 24-bit
        if (raw & 0x800000) {
            raw |= 0xFF000000;
        }
        return static_cast<SampleT>(static_cast<int32_t>(raw));
    }

    /// Encode internal format to 16-bit (right-shift by 8 with clamp)
    [[nodiscard]] constexpr int16_t encode_i16(SampleT value) const {
        int32_t v = static_cast<int32_t>(value);
        // Clamp to 24-bit range first
        if (v > 0x7FFFFF) v = 0x7FFFFF;
        if (v < -0x800000) v = -0x800000;
        return static_cast<int16_t>(v >> 8);
    }

    /// Encode internal format to 24-bit LE
    static void encode_i24(SampleT value, uint8_t* data) {
        int32_t v = static_cast<int32_t>(value);
        if (v > 0x7FFFFF) v = 0x7FFFFF;
        if (v < -0x800000) v = -0x800000;
        data[0] = static_cast<uint8_t>(v & 0xFF);
        data[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
        data[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    }
};

static_assert(SampleCodec<DefaultSampleCodec<int32_t>, int32_t>);

}  // namespace umiusb

// SPDX-License-Identifier: MIT
// UMI-USB: Gain Processor Strategy
#pragma once

#include <cstdint>
#include <concepts>
#include <type_traits>

namespace umiusb {

// ============================================================================
// GainProcessor Concept
// ============================================================================

template<typename T, typename SampleT>
concept GainProcessor = requires(T& g, SampleT* buf, uint32_t frames, uint8_t channels) {
    { g.apply(buf, frames, channels) } -> std::same_as<void>;
    { g.set_mute(bool{}) } -> std::same_as<void>;
    { g.set_volume_db256(int32_t{}) } -> std::same_as<void>;
};

// ============================================================================
// Default Gain Processor
// ============================================================================

/// Default gain implementation extracted from AudioInterface::apply_volume_out.
/// Realtime-safe: no heap, no locks, no exceptions, no stdio.
template<typename SampleT = int32_t>
struct DefaultGain {
    void set_mute(bool mute) { mute_ = mute; }
    void set_volume_db256(int32_t db256) { volume_db256_ = db256; }

    void apply(SampleT* dest, uint32_t frames, uint8_t channels) {
        if (frames == 0) return;
        uint32_t samples = frames * channels;

        if (mute_) {
            __builtin_memset(dest, 0, static_cast<size_t>(samples) * sizeof(SampleT));
            return;
        }

        if (volume_db256_ >= 0) return;  // 0 dB or above = unity gain

        int32_t neg_vol = -volume_db256_;
        // Map USB volume (0..127 in negative) to dB*256
        int32_t db = (neg_vol * 48) / 127;

        if (db >= 12288) {  // -48 dB = silence
            __builtin_memset(dest, 0, static_cast<size_t>(samples) * sizeof(SampleT));
            return;
        }

        // Approximate dB attenuation using shift + linear interpolation
        int32_t shift = db / 1541;  // ~6 dB per shift
        int32_t frac = db % 1541;
        int32_t gain = 32768 >> shift;
        gain = gain - ((gain * frac) / 3082);
        if (gain < 1) gain = 1;

        for (uint32_t i = 0; i < samples; ++i) {
            int32_t s = static_cast<int32_t>(dest[i]);
            int32_t result = static_cast<int32_t>((static_cast<int64_t>(s) * gain) >> 15);
            if constexpr (std::is_same_v<SampleT, int16_t>) {
                if (result > 32767) result = 32767;
                if (result < -32768) result = -32768;
                dest[i] = static_cast<int16_t>(result);
            } else {
                // Clamp to 24-bit range
                if (result > 0x7FFFFF) result = 0x7FFFFF;
                if (result < -0x800000) result = -0x800000;
                dest[i] = static_cast<SampleT>(result);
            }
        }
    }

private:
    bool mute_ = false;
    int32_t volume_db256_ = 0;
};

static_assert(GainProcessor<DefaultGain<int32_t>, int32_t>);
static_assert(GainProcessor<DefaultGain<int16_t>, int16_t>);

}  // namespace umiusb

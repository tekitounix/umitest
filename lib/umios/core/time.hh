// SPDX-License-Identifier: MIT
// UMI-OS - Universal Musical Instruments Operating System
// Time utilities

#pragma once

#include <cstdint>

namespace umi::time {

/// Convert milliseconds to samples
[[nodiscard]] constexpr uint32_t ms_to_samples(float ms, uint32_t sample_rate) noexcept {
    return static_cast<uint32_t>(ms * static_cast<float>(sample_rate) / 1000.0f);
}

/// Convert samples to milliseconds
[[nodiscard]] constexpr float samples_to_ms(uint32_t samples, uint32_t sample_rate) noexcept {
    return static_cast<float>(samples) * 1000.0f / static_cast<float>(sample_rate);
}

/// Convert seconds to samples
[[nodiscard]] constexpr uint32_t sec_to_samples(float sec, uint32_t sample_rate) noexcept {
    return static_cast<uint32_t>(sec * static_cast<float>(sample_rate));
}

/// Convert samples to seconds
[[nodiscard]] constexpr float samples_to_sec(uint32_t samples, uint32_t sample_rate) noexcept {
    return static_cast<float>(samples) / static_cast<float>(sample_rate);
}

/// Convert BPM to samples per beat
[[nodiscard]] constexpr uint32_t bpm_to_samples_per_beat(float bpm, uint32_t sample_rate) noexcept {
    return static_cast<uint32_t>(static_cast<float>(sample_rate) * 60.0f / bpm);
}

/// Convert Hz to samples per period
[[nodiscard]] constexpr float hz_to_samples_per_period(float hz, uint32_t sample_rate) noexcept {
    return static_cast<float>(sample_rate) / hz;
}

/// Convert samples per period to Hz
[[nodiscard]] constexpr float samples_per_period_to_hz(float samples, uint32_t sample_rate) noexcept {
    return static_cast<float>(sample_rate) / samples;
}

} // namespace umi::time

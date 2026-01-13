// SPDX-License-Identifier: MIT
// UMI-OS - DSP Module
//
// This header includes all DSP building blocks.
// All components are dependency-free and can be used in any C++ project.
//
// Design principles:
// - No UMI-OS dependencies (pure C++ standard)
// - No assert/log calls (hot path optimization)
// - Inlinable tick() methods
// - Concrete classes (no virtual functions)
// - Template-based polymorphism when needed

#pragma once

#include "constants.hh"
#include "oscillator.hh"
#include "filter.hh"
#include "envelope.hh"

namespace umi::dsp {

// ============================================================================
// Utility Functions
// ============================================================================

/// Convert MIDI note to frequency (A4 = 440Hz)
[[nodiscard]] inline constexpr float midi_to_freq(int note) noexcept {
    // f = 440 * 2^((note - 69) / 12)
    return 440.0f * std::pow(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
}

/// Convert frequency to normalized frequency
[[nodiscard]] inline constexpr float normalize_freq(float freq, float sample_rate) noexcept {
    return freq / sample_rate;
}

/// Soft clip function (tanh-like)
[[nodiscard]] inline float soft_clip(float x) noexcept {
    if (x > 1.0f) return 1.0f;
    if (x < -1.0f) return -1.0f;
    return x * (1.5f - 0.5f * x * x);
}

/// Hard clip
[[nodiscard]] inline constexpr float hard_clip(float x, float limit = 1.0f) noexcept {
    if (x > limit) return limit;
    if (x < -limit) return -limit;
    return x;
}

/// Linear interpolation
[[nodiscard]] inline constexpr float lerp(float a, float b, float t) noexcept {
    return a + t * (b - a);
}

/// Convert decibels to linear gain
[[nodiscard]] inline float db_to_gain(float db) noexcept {
    return std::pow(10.0f, db / 20.0f);
}

/// Convert linear gain to decibels
[[nodiscard]] inline float gain_to_db(float gain) noexcept {
    return 20.0f * std::log10(gain);
}

} // namespace umi::dsp

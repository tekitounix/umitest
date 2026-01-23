// SPDX-License-Identifier: MIT
// UMI-DSP: Digital Signal Processing Library
//
// This header includes all DSP components.
// All components are dependency-free and can be used in any C++ project.
//
// Structure:
//   core/        - Primitive building blocks (domain-independent)
//   filter/      - Filter implementations (shared by audio/video)
//   audio/       - Audio-specific DSP
//     synth/     - Synthesis (oscillator, envelope)
//     fx/        - Effects (delay, reverb, etc.)
//     rate/      - Sample rate conversion (ASRC)
//   video/       - Video/image processing (future)
//
// Design principles:
// - No UMI-OS dependencies (pure C++ standard)
// - No assert/log calls (hot path optimization)
// - Inlinable tick() methods
// - Concrete classes (no virtual functions)
// - Template-based polymorphism when needed
#pragma once

// Core primitives (domain-independent)
#include "core/constants.hh"
#include "core/interpolate.hh"
#include "core/phase.hh"

// Filters (shared)
#include "filter/filter.hh"

// Audio: Synthesis
#include "audio/synth/envelope.hh"
#include "audio/synth/oscillator.hh"

// Audio: Sample rate conversion
#include "audio/rate/asrc.hh"
#include "audio/rate/pi_controller.hh"

namespace umidsp {

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
    if (x > 1.0f)
        return 1.0f;
    if (x < -1.0f)
        return -1.0f;
    return x * (1.5f - 0.5f * x * x);
}

/// Hard clip
[[nodiscard]] inline constexpr float hard_clip(float x, float limit = 1.0f) noexcept {
    if (x > limit)
        return limit;
    if (x < -limit)
        return -limit;
    return x;
}

// Legacy filter aliases
using SVF = Svf<SvfInlinePolicy>;
using K35 = ::K35<K35InlinePolicy>;

} // namespace umidsp

// Legacy namespace alias for backward compatibility
namespace umi::dsp {
using namespace umidsp;
}

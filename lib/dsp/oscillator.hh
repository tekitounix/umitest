// SPDX-License-Identifier: MIT
// UMI-OS - DSP Oscillator
//
// Dependency-free oscillator implementations for audio synthesis.
// Can be used in any C++ project without UMI-OS dependencies.

#pragma once

#include <cmath>
#include "constants.hh"

namespace umi::dsp {

// ============================================================================
// Phase Accumulator
// ============================================================================

/// Simple phase accumulator (0.0 to 1.0)
class Phase {
public:
    Phase() = default;
    explicit Phase(float initial) noexcept : phase_(wrap(initial)) {}
    
    /// Advance phase by normalized frequency (freq / sample_rate)
    float tick(float freq_norm) noexcept {
        float out = phase_;
        phase_ += freq_norm;
        phase_ = wrap(phase_);
        return out;
    }
    
    /// Current phase value
    [[nodiscard]] float value() const noexcept { return phase_; }
    
    /// Reset phase to 0
    void reset() noexcept { phase_ = 0.0f; }
    
    /// Set phase directly
    void set(float p) noexcept { phase_ = wrap(p); }
    
private:
    static float wrap(float p) noexcept {
        p -= static_cast<int>(p);
        return p < 0.0f ? p + 1.0f : p;
    }
    
    float phase_ = 0.0f;
};

// ============================================================================
// Sine Oscillator
// ============================================================================

/// Pure sine wave oscillator
class Sine {
public:
    Sine() = default;

    /// Generate next sample
    /// @param freq_norm Normalized frequency (Hz / sample_rate)
    [[nodiscard]] float tick(float freq_norm) noexcept {
        return std::sin(phase_.tick(freq_norm) * k2Pi);
    }

    /// Reset oscillator phase
    void reset() noexcept { phase_.reset(); }

    /// Set phase (0.0 to 1.0)
    void set_phase(float p) noexcept { phase_.set(p); }

private:
    Phase phase_;
};

// ============================================================================
// Saw Oscillator (Naive, non-bandlimited)
// ============================================================================

/// Naive sawtooth oscillator (will alias at high frequencies)
class SawNaive {
public:
    SawNaive() = default;
    
    /// Generate next sample (-1.0 to 1.0)
    [[nodiscard]] float tick(float freq_norm) noexcept {
        return phase_.tick(freq_norm) * 2.0f - 1.0f;
    }
    
    void reset() noexcept { phase_.reset(); }
    void set_phase(float p) noexcept { phase_.set(p); }
    
private:
    Phase phase_;
};

// ============================================================================
// Square Oscillator (Naive, non-bandlimited)
// ============================================================================

/// Naive square wave oscillator (will alias at high frequencies)
class SquareNaive {
public:
    SquareNaive() = default;
    
    /// Generate next sample (-1.0 or 1.0)
    /// @param pulse_width Pulse width (0.0 to 1.0, default 0.5)
    [[nodiscard]] float tick(float freq_norm, float pulse_width = 0.5f) noexcept {
        return phase_.tick(freq_norm) < pulse_width ? 1.0f : -1.0f;
    }
    
    void reset() noexcept { phase_.reset(); }
    void set_phase(float p) noexcept { phase_.set(p); }
    
private:
    Phase phase_;
};

// ============================================================================
// Triangle Oscillator
// ============================================================================

/// Triangle wave oscillator
class Triangle {
public:
    Triangle() = default;
    
    /// Generate next sample (-1.0 to 1.0)
    [[nodiscard]] float tick(float freq_norm) noexcept {
        float p = phase_.tick(freq_norm);
        // Triangle: goes 0→1→0 in one period
        return (p < 0.5f) ? (4.0f * p - 1.0f) : (3.0f - 4.0f * p);
    }
    
    void reset() noexcept { phase_.reset(); }
    void set_phase(float p) noexcept { phase_.set(p); }
    
private:
    Phase phase_;
};

// ============================================================================
// PolyBLEP Oscillators (Bandlimited)
// ============================================================================

namespace detail {

/// PolyBLEP correction for discontinuities
inline float polyblep(float t, float dt) noexcept {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    } else if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

} // namespace detail

/// Bandlimited sawtooth oscillator using PolyBLEP
class SawBL {
public:
    SawBL() = default;
    
    [[nodiscard]] float tick(float freq_norm) noexcept {
        float t = phase_.value();
        float naive = 2.0f * t - 1.0f;
        float blep = naive - detail::polyblep(t, freq_norm);
        phase_.tick(freq_norm);
        return blep;
    }
    
    void reset() noexcept { phase_.reset(); }
    
private:
    Phase phase_;
};

/// Bandlimited square oscillator using PolyBLEP
class SquareBL {
public:
    SquareBL() = default;
    
    [[nodiscard]] float tick(float freq_norm, float pw = 0.5f) noexcept {
        float t = phase_.value();
        float naive = (t < pw) ? 1.0f : -1.0f;
        float blep = naive;
        blep -= detail::polyblep(t, freq_norm);
        blep += detail::polyblep(fmod(t + 1.0f - pw, 1.0f), freq_norm);
        phase_.tick(freq_norm);
        return blep;
    }
    
    void reset() noexcept { phase_.reset(); }
    
private:
    Phase phase_;
    
    static float fmod(float a, float b) noexcept {
        return a - b * static_cast<int>(a / b);
    }
};

} // namespace umi::dsp

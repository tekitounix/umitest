// SPDX-License-Identifier: MIT
// UMI-DSP: Phase Accumulator
//
// Fixed-point and floating-point phase accumulators for DSP.
#pragma once

#include <cstdint>

namespace umidsp {

// ============================================================================
// Fixed-Point Phase Accumulator (Q16.16)
// ============================================================================

/// Fixed-point phase accumulator for sample rate conversion
/// Uses Q16.16 format: upper 16 bits = integer, lower 16 bits = fraction
class PhaseAccumulator {
public:
    static constexpr uint32_t FRAC_BITS = 16;
    static constexpr uint32_t FRAC_MASK = (1U << FRAC_BITS) - 1;
    static constexpr uint32_t ONE = 1U << FRAC_BITS;  // 1.0 in Q16.16

    void reset() {
        phase_ = 0;
        rate_ = ONE;
    }

    /// Set rate ratio (Q16.16). 1.0 = 0x10000
    void set_rate(uint32_t rate_q16) { rate_ = rate_q16; }

    /// Get current rate (Q16.16)
    [[nodiscard]] uint32_t rate() const { return rate_; }

    /// Advance phase and return integer samples consumed
    uint32_t advance() {
        phase_ += rate_;
        uint32_t consumed = phase_ >> FRAC_BITS;
        phase_ &= FRAC_MASK;
        return consumed;
    }

    /// Get fractional part as Q0.16
    [[nodiscard]] uint32_t fraction() const { return phase_; }

    /// Get fractional part as float [0, 1)
    [[nodiscard]] float fraction_f() const {
        return static_cast<float>(phase_) / static_cast<float>(ONE);
    }

private:
    uint32_t phase_ = 0;
    uint32_t rate_ = ONE;
};

// ============================================================================
// Floating-Point Phase Accumulator
// ============================================================================

/// Simple phase accumulator (0.0 to 1.0) for synthesis
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

}  // namespace umidsp

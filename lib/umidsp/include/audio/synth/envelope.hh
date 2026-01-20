// SPDX-License-Identifier: MIT
// UMI-OS - DSP Envelope
//
// Dependency-free envelope generator implementations.
// Can be used in any C++ project without UMI-OS dependencies.

#pragma once

#include <cstdint>
#include <cmath>

namespace umi::dsp {

// ============================================================================
// ADSR Envelope
// ============================================================================

/// ADSR envelope generator
/// Sample-rate independent: call tick(dt) with delta time in seconds
class ADSR {
public:
    enum class State : uint8_t {
        Idle,
        Attack,
        Decay,
        Sustain,
        Release
    };
    
    ADSR() = default;
    
    /// Set envelope times and sustain level (sample-rate independent)
    /// @param attack_ms Attack time in milliseconds
    /// @param decay_ms Decay time in milliseconds
    /// @param sustain Sustain level (0.0 to 1.0)
    /// @param release_ms Release time in milliseconds
    void set_params(float attack_ms, float decay_ms, float sustain,
                    float release_ms) noexcept {
        sustain_ = sustain;
        
        // Store times in seconds for dt-based calculation
        // Time constant for ~99% of target: tau = time / 5
        attack_tau_ = (attack_ms > 0) ? (attack_ms / 1000.0f / 5.0f) : 0.001f;
        decay_tau_ = (decay_ms > 0) ? (decay_ms / 1000.0f / 5.0f) : 0.001f;
        release_tau_ = (release_ms > 0) ? (release_ms / 1000.0f / 5.0f) : 0.001f;
    }
    
    /// Legacy: set params with sample_rate (for compatibility)
    void set_params(float attack_ms, float decay_ms, float sustain,
                    float release_ms, [[maybe_unused]] float sample_rate) noexcept {
        set_params(attack_ms, decay_ms, sustain, release_ms);
    }
    
    /// Trigger envelope (gate on)
    void trigger() noexcept {
        state_ = State::Attack;
    }
    
    /// Release envelope (gate off)
    void release() noexcept {
        if (state_ != State::Idle) {
            state_ = State::Release;
        }
    }
    
    /// Force envelope to idle
    void reset() noexcept {
        state_ = State::Idle;
        value_ = 0.0f;
    }
    
    /// Generate next sample with delta time
    /// @param dt Time step in seconds (1.0/sample_rate)
    [[nodiscard]] float tick(float dt) noexcept {
        switch (state_) {
            case State::Idle:
                value_ = 0.0f;
                break;
                
            case State::Attack: {
                float rate = dt / attack_tau_;
                value_ += rate * (1.0f - value_);
                if (value_ >= 0.999f) {
                    value_ = 1.0f;
                    state_ = State::Decay;
                }
                break;
            }
                
            case State::Decay: {
                float rate = dt / decay_tau_;
                value_ += rate * (sustain_ - value_);
                if (value_ <= sustain_ + 0.001f) {
                    value_ = sustain_;
                    state_ = State::Sustain;
                }
                break;
            }
                
            case State::Sustain:
                value_ = sustain_;
                break;
                
            case State::Release: {
                float rate = dt / release_tau_;
                value_ += rate * (0.0f - value_);
                if (value_ <= 0.001f) {
                    value_ = 0.0f;
                    state_ = State::Idle;
                }
                break;
            }
        }
        
        return value_;
    }
    
    /// Legacy: tick without dt (uses default 48kHz)
    [[nodiscard]] float tick() noexcept {
        return tick(1.0f / 48000.0f);
    }
    
    /// Current envelope state
    [[nodiscard]] State state() const noexcept { return state_; }
    
    /// Current envelope value
    [[nodiscard]] float value() const noexcept { return value_; }
    
    /// Is envelope active (not idle)?
    [[nodiscard]] bool active() const noexcept { return state_ != State::Idle; }
    
private:
    State state_ = State::Idle;
    float value_ = 0.0f;
    float sustain_ = 0.5f;
    float attack_tau_ = 0.002f;   // ~10ms default
    float decay_tau_ = 0.02f;     // ~100ms default
    float release_tau_ = 0.04f;   // ~200ms default
};

// ============================================================================
// AR Envelope (Attack-Release only)
// ============================================================================

/// Simple attack-release envelope
class AR {
public:
    AR() = default;
    
    void set_params(float attack_ms, float release_ms, float sample_rate) noexcept {
        float attack_samples = attack_ms * sample_rate / 1000.0f;
        float release_samples = release_ms * sample_rate / 1000.0f;
        
        attack_rate_ = attack_samples > 0 ?
            1.0f - std::exp(-5.0f / attack_samples) : 1.0f;
        release_rate_ = release_samples > 0 ?
            1.0f - std::exp(-5.0f / release_samples) : 1.0f;
    }
    
    void trigger() noexcept { attacking_ = true; }
    void release() noexcept { attacking_ = false; }
    void reset() noexcept { value_ = 0.0f; attacking_ = false; }
    
    [[nodiscard]] float tick() noexcept {
        if (attacking_) {
            value_ += attack_rate_ * (1.0f - value_);
        } else {
            value_ += release_rate_ * (0.0f - value_);
        }
        return value_;
    }
    
    [[nodiscard]] float value() const noexcept { return value_; }
    [[nodiscard]] bool active() const noexcept { return value_ > 0.001f; }
    
private:
    float value_ = 0.0f;
    float attack_rate_ = 0.01f;
    float release_rate_ = 0.001f;
    bool attacking_ = false;
};

// ============================================================================
// Linear Ramp
// ============================================================================

/// Linear ramp generator
class Ramp {
public:
    Ramp() = default;
    
    /// Set target value and time to reach it
    void set_target(float target, uint32_t samples) noexcept {
        if (samples == 0) {
            value_ = target;
            rate_ = 0.0f;
            samples_remaining_ = 0;
        } else {
            rate_ = (target - value_) / static_cast<float>(samples);
            samples_remaining_ = samples;
            target_ = target;
        }
    }
    
    /// Set immediate value (no ramp)
    void set_immediate(float value) noexcept {
        value_ = value;
        rate_ = 0.0f;
        samples_remaining_ = 0;
    }
    
    [[nodiscard]] float tick() noexcept {
        if (samples_remaining_ > 0) {
            value_ += rate_;
            samples_remaining_--;
            if (samples_remaining_ == 0) {
                value_ = target_;
            }
        }
        return value_;
    }
    
    [[nodiscard]] float value() const noexcept { return value_; }
    [[nodiscard]] bool active() const noexcept { return samples_remaining_ > 0; }
    
private:
    float value_ = 0.0f;
    float target_ = 0.0f;
    float rate_ = 0.0f;
    uint32_t samples_remaining_ = 0;
};

} // namespace umi::dsp

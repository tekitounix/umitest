// SPDX-License-Identifier: MIT
// UMI-DSP: PI Rate Controller
//
// PI controller for buffer level management in ASRC.
// Maintains buffer at target level to absorb timing jitter.
#pragma once

#include <cstdint>

namespace umidsp {

// ============================================================================
// PI Controller Configuration
// ============================================================================

struct PiConfig {
    int32_t target_level = 128;    // Target buffer level (frames)
    int32_t hysteresis = 8;        // Dead zone (±frames)
    int32_t max_ppm = 1000;        // Maximum PPM adjustment
    int32_t kp_num = 2;            // Kp numerator
    int32_t kp_den = 1;            // Kp denominator
    int32_t ki_num = 1;            // Ki numerator
    int32_t ki_den = 50;           // Ki denominator
    int32_t integral_max = 25000;  // Anti-windup limit
    
    // Preset: Fast response (quick settling, more jitter)
    static constexpr PiConfig fast(int32_t target = 128) {
        return {target, 4, 2000, 4, 1, 1, 25, 10000};
    }
    
    // Preset: Stable (slow settling, low jitter)
    static constexpr PiConfig stable(int32_t target = 128) {
        return {target, 16, 500, 1, 1, 1, 100, 50000};
    }
    
    // Preset: Default (balanced)
    static constexpr PiConfig default_config(int32_t target = 128) {
        return {target, 8, 1000, 2, 1, 1, 50, 25000};
    }
};

// ============================================================================
// Runtime-Configurable PI Controller
// ============================================================================

class PiRateController {
public:
    explicit PiRateController(const PiConfig& cfg = PiConfig::default_config())
        : cfg_(cfg) {}
    
    void set_config(const PiConfig& cfg) { cfg_ = cfg; }
    const PiConfig& config() const { return cfg_; }
    
    void reset() {
        current_ppm_ = 0;
        integral_ = 0;
        prev_error_ = 0;
        update_count_ = 0;
        deadband_hits_ = 0;
    }

    /// Update rate adjustment based on buffer level
    /// Returns PPM adjustment: positive = speed up, negative = slow down
    int32_t update(int32_t buffer_level) {
        ++update_count_;
        int32_t error = buffer_level - cfg_.target_level;

        // Deadband: ignore small errors for stability
        if (error > -cfg_.hysteresis && error < cfg_.hysteresis) {
            ++deadband_hits_;
            // Still accumulate I term slowly
            integral_ += error / 4;
        } else {
            // P term
            int32_t p_contribution = (error * cfg_.kp_num) / cfg_.kp_den;
            
            // I term with trapezoidal integration
            integral_ += (error + prev_error_) / 2;
            
            current_ppm_ = p_contribution + (integral_ * cfg_.ki_num) / cfg_.ki_den;
        }
        
        prev_error_ = error;

        // Anti-windup clamp
        if (integral_ > cfg_.integral_max) integral_ = cfg_.integral_max;
        if (integral_ < -cfg_.integral_max) integral_ = -cfg_.integral_max;

        // Clamp output
        if (current_ppm_ > cfg_.max_ppm) current_ppm_ = cfg_.max_ppm;
        if (current_ppm_ < -cfg_.max_ppm) current_ppm_ = -cfg_.max_ppm;

        return current_ppm_;
    }

    // Monitoring API
    [[nodiscard]] int32_t current_ppm() const { return current_ppm_; }
    [[nodiscard]] int32_t integral() const { return integral_; }
    [[nodiscard]] int32_t prev_error() const { return prev_error_; }
    [[nodiscard]] uint32_t update_count() const { return update_count_; }
    [[nodiscard]] uint32_t deadband_hits() const { return deadband_hits_; }

    /// Convert PPM to Q16.16 rate ratio
    [[nodiscard]] static constexpr uint32_t ppm_to_rate_q16(int32_t ppm) {
        return static_cast<uint32_t>(0x10000 + (ppm * 65536 + 500000) / 1000000);
    }
    
    /// Convert PPM to float rate ratio
    [[nodiscard]] static constexpr float ppm_to_rate_f(int32_t ppm) {
        return 1.0f + static_cast<float>(ppm) / 1000000.0f;
    }

private:
    PiConfig cfg_;
    int32_t current_ppm_ = 0;
    int32_t integral_ = 0;
    int32_t prev_error_ = 0;
    uint32_t update_count_ = 0;
    uint32_t deadband_hits_ = 0;
};

// ============================================================================
// Legacy Template-Based PI Controller (for backward compatibility)
// ============================================================================

template<
    int32_t TargetLevel = 128,
    int32_t Hysteresis = 8,
    int32_t MaxPpmAdjust = 1000,
    int32_t KpNum = 2,
    int32_t KpDen = 1,
    int32_t KiNum = 1,
    int32_t KiDen = 50,
    int32_t IntegralMax = 25000
>
class PiRateControllerT {
public:
    static constexpr int32_t TARGET_LEVEL = TargetLevel;
    static constexpr int32_t HYSTERESIS = Hysteresis;
    static constexpr int32_t MAX_PPM_ADJUST = MaxPpmAdjust;

    void reset() {
        current_ppm_ = 0;
        integral_ = 0;
        prev_error_ = 0;
    }

    int32_t update(int32_t buffer_level) {
        int32_t error = buffer_level - TARGET_LEVEL;

        int32_t p_contribution = 0;
        if (error < -HYSTERESIS || error > HYSTERESIS) {
            p_contribution = (error * KpNum) / KpDen;
        }

        integral_ += (error + prev_error_) / 2;
        prev_error_ = error;

        if (integral_ > IntegralMax) integral_ = IntegralMax;
        if (integral_ < -IntegralMax) integral_ = -IntegralMax;

        int32_t i_contribution = (integral_ * KiNum) / KiDen;
        current_ppm_ = p_contribution + i_contribution;

        if (current_ppm_ > MAX_PPM_ADJUST) current_ppm_ = MAX_PPM_ADJUST;
        if (current_ppm_ < -MAX_PPM_ADJUST) current_ppm_ = -MAX_PPM_ADJUST;

        return current_ppm_;
    }

    [[nodiscard]] int32_t current_ppm() const { return current_ppm_; }
    [[nodiscard]] int32_t integral() const { return integral_; }
    [[nodiscard]] int32_t prev_error() const { return prev_error_; }

    [[nodiscard]] static constexpr uint32_t ppm_to_rate_q16(int32_t ppm) {
        return static_cast<uint32_t>(0x10000 + (ppm * 65536 + 500000) / 1000000);
    }

private:
    int32_t current_ppm_ = 0;
    int32_t integral_ = 0;
    int32_t prev_error_ = 0;
};

// Default USB Audio configuration (balanced: moderate jitter, reasonable settling)
// Parameters: TargetLevel=128, Hysteresis=8, MaxPpm=500, Kp=1/1, Ki=1/75, IntegralMax=40000
using UsbAudioPiController = PiRateControllerT<128, 8, 500, 1, 1, 1, 75, 40000>;

}  // namespace umidsp

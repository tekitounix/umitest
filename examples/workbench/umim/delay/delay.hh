// =====================================================================
// UMI-OS Delay Effect Processor (Header-only)
// =====================================================================
// Simple delay effect with feedback and filter.
// Can be used with any adapter (WASM, VST3, embedded, etc.)
// =====================================================================

#pragma once

#include <umim_adapter.hh>

#include <array>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace umi::umim {

using namespace umi::dsp;

// ============================================================================
// Delay Line
// ============================================================================

template<size_t MaxSamples = 48000>
class DelayLine {
public:
    void set_delay(size_t samples) noexcept {
        delay_samples_ = samples < MaxSamples ? samples : MaxSamples - 1;
    }
    
    float process(float in) noexcept {
        float out = buffer_[read_pos_];
        buffer_[write_pos_] = in;
        write_pos_ = (write_pos_ + 1) % MaxSamples;
        read_pos_ = (write_pos_ + MaxSamples - delay_samples_) % MaxSamples;
        return out;
    }
    
private:
    std::array<float, MaxSamples> buffer_{};
    size_t write_pos_ = 0;
    size_t read_pos_ = 0;
    size_t delay_samples_ = 0;
};

// ============================================================================
// Delay Effect Processor
// ============================================================================

class Delay {
public:
    void process(AudioContext& ctx) {
        const float dt = ctx.dt;
        
        if (dt != last_dt_) {
            last_dt_ = dt;
            update_delay(dt);
            update_filter(dt);
        }
        
        const sample_t* in = ctx.input(0);
        sample_t* out = ctx.output(0);
        if (!in || !out) return;
        
        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            float dry = in[i];
            float delay_in = dry + delay_buffer_ * feedback_;
            float delayed = delay_.process(delay_in);
            delay_buffer_ = delayed;
            
            filter_.tick(delayed);
            float wet = filter_.lp();
            
            out[i] = dry * (1.0f - mix_) + wet * mix_;
        }
    }
    
    void set_param(uint32_t id, float value) {
        switch (id) {
            case 0: delay_time_ms_ = value; if (last_dt_ > 0.0f) update_delay(last_dt_); break;
            case 1: feedback_ = std::clamp(value, 0.0f, 0.95f); break;
            case 2: mix_ = std::clamp(value, 0.0f, 1.0f); break;
            case 3: filter_cutoff_hz_ = value; if (last_dt_ > 0.0f) update_filter(last_dt_); break;
        }
    }
    
    float get_param(uint32_t id) const {
        switch (id) {
            case 0: return delay_time_ms_;
            case 1: return feedback_;
            case 2: return mix_;
            case 3: return filter_cutoff_hz_;
        }
        return 0.0f;
    }
    
private:
    void update_delay(float dt) {
        float sample_rate = 1.0f / dt;
        size_t samples = static_cast<size_t>(delay_time_ms_ * sample_rate / 1000.0f);
        delay_.set_delay(samples);
    }
    
    void update_filter(float dt) {
        float cutoff_norm = std::clamp(filter_cutoff_hz_ * dt, 0.001f, 0.45f);
        filter_.set_params(cutoff_norm, 0.3f);
    }
    
    DelayLine<48000> delay_;
    SVF filter_;
    float delay_buffer_ = 0.0f;
    
    float last_dt_ = 0.0f;
    float delay_time_ms_ = 300.0f;
    float feedback_ = 0.4f;
    float mix_ = 0.5f;
    float filter_cutoff_hz_ = 8000.0f;
};

// ============================================================================
// Parameter Metadata
// ============================================================================

inline constexpr std::array<Param, 4> kDelayParams = {{
    {"Time",     10.0f,  1000.0f, 300.0f, 0, "ms"},
    {"Feedback", 0.0f,   0.95f,   0.4f,   0, ""},
    {"Mix",      0.0f,   1.0f,    0.5f,   0, ""},
    {"Filter",   100.0f, 20000.0f, 8000.0f, 1, "Hz"},
}};

} // namespace umi::umim

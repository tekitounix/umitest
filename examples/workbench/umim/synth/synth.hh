// =====================================================================
// UMI-OS Polyphonic Synth Processor (Header-only)
// =====================================================================
// 8-voice polyphonic synthesizer with filter.
// Can be used with any adapter (WASM, VST3, embedded, etc.)
// =====================================================================

#pragma once

#include <umim_adapter.hh>

#include <array>
#include <cmath>
#include <algorithm>

namespace umi::umim {

using namespace umi::dsp;

// ============================================================================
// Voice - Single monophonic voice
// ============================================================================

struct SynthVoice {
    SawBL osc;
    ADSR env;
    SVF filter;
    
    uint8_t note = 0;
    float velocity = 0.0f;
    bool active = false;
    bool stealing = false;
    uint8_t pending_note = 0;
    float pending_velocity = 0.0f;
    float freq_norm = 0.0f;
    float fade = 0.0f;
    
    SynthVoice() {
        env.set_params(10.0f, 100.0f, 0.6f, 200.0f);
    }
    
    void note_on(uint8_t n, float vel) {
        if (active && !stealing) {
            stealing = true;
            fade = 1.0f;
            pending_note = n;
            pending_velocity = vel;
        } else {
            start_note(n, vel);
        }
    }
    
    void start_note(uint8_t n, float vel) {
        note = n;
        velocity = vel;
        active = true;
        stealing = false;
        fade = 0.0f;
        freq_norm = 0.0f;
        env.trigger();
    }
    
    void note_off() {
        if (!stealing) env.release();
    }
    
    void set_filter(float cutoff_norm, float resonance) {
        filter.set_params(cutoff_norm, resonance);
    }
    
    float tick(float dt) {
        if (!active) return 0.0f;
        
        if (stealing) {
            fade -= dt / 0.005f;
            if (fade <= 0.0f) start_note(pending_note, pending_velocity);
            float e = env.tick(dt);
            float sample = osc.tick(freq_norm);
            filter.tick(sample);
            return filter.lp() * e * velocity * std::max(0.0f, fade);
        }
        
        if (freq_norm == 0.0f && note > 0) {
            freq_norm = midi_to_freq(note) * dt;
        }
        
        float e = env.tick(dt);
        if (env.state() == ADSR::State::Idle) {
            active = false;
            return 0.0f;
        }
        
        float sample = osc.tick(freq_norm);
        filter.tick(sample);
        return filter.lp() * e * velocity;
    }
};

// ============================================================================
// PolySynth - 8-voice polyphonic synthesizer
// ============================================================================

template<size_t NumVoices = 8>
class PolySynth {
public:
    void process(AudioContext& ctx) {
        const float dt = ctx.dt;
        
        if (dt != last_dt_) {
            last_dt_ = dt;
            update_filters(dt);
        }
        
        sample_t* out = ctx.output(0);
        if (!out) return;
        
        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            float sum = 0.0f;
            for (auto& v : voices_) {
                sum += v.tick(dt);
            }
            out[i] = soft_clip(sum * master_volume_);
        }
    }
    
    void note_on(uint8_t note, uint8_t velocity) {
        float vel = velocity / 127.0f;
        SynthVoice* target = nullptr;
        
        for (auto& v : voices_) {
            if (!v.active) { target = &v; break; }
        }
        if (!target) {
            for (auto& v : voices_) {
                if (v.env.state() == ADSR::State::Release) { target = &v; break; }
            }
        }
        if (!target) {
            target = &voices_[next_voice_];
            next_voice_ = (next_voice_ + 1) % NumVoices;
        }
        
        target->note_on(note, vel);
    }
    
    void note_off(uint8_t note) {
        for (auto& v : voices_) {
            if (v.active && v.note == note && !v.stealing) {
                v.note_off();
            }
        }
    }
    
    void set_param(uint32_t id, float value) {
        switch (id) {
            case 0: master_volume_ = value; break;
            case 1: filter_cutoff_hz_ = value; if (last_dt_ > 0.0f) update_filters(last_dt_); break;
            case 2: filter_resonance_ = value; if (last_dt_ > 0.0f) update_filters(last_dt_); break;
        }
    }
    
    float get_param(uint32_t id) const {
        switch (id) {
            case 0: return master_volume_;
            case 1: return filter_cutoff_hz_;
            case 2: return filter_resonance_;
        }
        return 0.0f;
    }
    
private:
    void update_filters(float dt) {
        float cutoff_norm = std::clamp(filter_cutoff_hz_ * dt, 0.001f, 0.45f);
        for (auto& v : voices_) {
            v.set_filter(cutoff_norm, filter_resonance_);
        }
    }
    
    std::array<SynthVoice, NumVoices> voices_;
    float last_dt_ = 0.0f;
    float master_volume_ = 0.7f;
    float filter_cutoff_hz_ = 8000.0f;
    float filter_resonance_ = 0.3f;
    size_t next_voice_ = 0;
};

// ============================================================================
// Parameter Metadata
// ============================================================================

inline constexpr std::array<Param, 3> kSynthParams = {{
    {"Volume",    0.0f,   1.0f,     0.7f,  0, ""},
    {"Cutoff",    20.0f,  20000.0f, 8000.0f, 1, "Hz"},
    {"Resonance", 0.0f,   1.0f,     0.3f,  0, ""},
}};

} // namespace umi::umim

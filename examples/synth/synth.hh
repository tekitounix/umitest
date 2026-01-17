// =====================================================================
// UMI-OS Simple Synthesizer
// =====================================================================
// Shared between embedded and WASM builds.
// Pure DSP logic - no platform-specific dependencies.
// Uses lib/dsp/ for DSP components.
// =====================================================================

#pragma once

#include <dsp/dsp.hh>
#include <cstdint>
#include <cstddef>

namespace umi::synth {

// =====================================================================
// Configuration
// =====================================================================

constexpr int NUM_VOICES = 4;

// =====================================================================
// Parameter IDs
// =====================================================================

enum class ParamId {
    Attack = 0,
    Decay,
    Sustain,
    Release,
    Cutoff,
    Resonance,
    Volume,
    Count
};

constexpr int PARAM_COUNT = static_cast<int>(ParamId::Count);

// =====================================================================
// Port definitions (for UMIM spec compliance)
// =====================================================================

constexpr uint32_t PORT_MIDI_IN = 0;
constexpr uint32_t PORT_AUDIO_OUT = 1;
constexpr uint32_t PORT_COUNT = 2;

// =====================================================================
// Synth Voice
// =====================================================================

class Voice {
public:
    Voice() = default;

    void init(float sample_rate) {
        sample_rate_ = sample_rate;
        dt_ = 1.0f / sample_rate;
        // Note: ADSR and filter parameters are NOT set here.
        // They must be set via set_adsr() and set_filter() by PolySynth.
        // This ensures single source of truth for default values.
    }

    void note_on(uint8_t note, uint8_t velocity) {
        note_ = note;
        velocity_ = static_cast<float>(velocity) / 127.0f;

        // Calculate normalized frequency
        float freq = dsp::midi_to_freq(note);
        freq_norm_ = freq / sample_rate_;

        // Reset oscillator and trigger envelope
        osc_.reset();
        filter_.reset();
        env_.trigger();
        active_ = true;
    }

    void note_off() {
        env_.release();
    }

    bool is_active() const { return active_; }
    uint8_t note() const { return note_; }

    // Parameter setters
    void set_adsr(float attack_ms, float decay_ms, float sustain, float release_ms) {
        env_.set_params(attack_ms, decay_ms, sustain, release_ms);
    }

    void set_filter(float cutoff_hz, float resonance) {
        filter_.set_params(cutoff_hz / sample_rate_, resonance);
    }

    float process() {
        if (!active_) return 0.0f;

        // Generate oscillator output
        float osc_out = osc_.tick(freq_norm_);

        // Apply filter
        filter_.tick(osc_out);
        float filtered = filter_.lp();

        // Apply envelope
        float env_val = env_.tick(dt_);
        float out = filtered * env_val * velocity_;

        // Deactivate when envelope finishes
        if (!env_.active()) {
            active_ = false;
        }

        return out;
    }

private:
    dsp::SawBL osc_;
    dsp::SVF filter_;
    dsp::ADSR env_;

    float sample_rate_ = 48000.0f;
    float dt_ = 1.0f / 48000.0f;
    float freq_norm_ = 0.0f;
    float velocity_ = 0.0f;
    uint8_t note_ = 0;
    bool active_ = false;
};

// =====================================================================
// Polyphonic Synthesizer
// =====================================================================

class PolySynth {
public:
    void init(float sample_rate) {
        sample_rate_ = sample_rate;
        for (int i = 0; i < NUM_VOICES; ++i) {
            voices_[i].init(sample_rate);
        }
        // Apply current parameter values to all voices
        update_adsr();
        update_filter();
    }

    // === MIDI event handling ===

    /// Handle MIDI bytes (3-byte message)
    void handle_midi(const uint8_t* data, uint8_t size) {
        if (size < 2) return;

        uint8_t status = data[0];
        uint8_t cmd = status & 0xF0;
        // uint8_t channel = status & 0x0F;  // not used currently

        if (cmd == 0x90 && size >= 3 && data[2] > 0) {
            // Note On
            note_on(data[1], data[2]);
        } else if (cmd == 0x80 || (cmd == 0x90 && data[2] == 0)) {
            // Note Off
            note_off(data[1]);
        }
        // CC and other events can be handled here
    }

    // === Direct note interface ===

    void note_on(uint8_t note, uint8_t velocity) {
        // First check if this note is already playing
        for (int i = 0; i < NUM_VOICES; ++i) {
            if (voices_[i].is_active() && voices_[i].note() == note) {
                voices_[i].note_on(note, velocity);
                return;
            }
        }

        // Find free voice
        for (int i = 0; i < NUM_VOICES; ++i) {
            if (!voices_[i].is_active()) {
                voices_[i].note_on(note, velocity);
                return;
            }
        }

        // Voice stealing: use first voice (simple strategy)
        voices_[0].note_on(note, velocity);
    }

    void note_off(uint8_t note) {
        for (int i = 0; i < NUM_VOICES; ++i) {
            if (voices_[i].is_active() && voices_[i].note() == note) {
                voices_[i].note_off();
            }
        }
    }

    // === Parameter control ===

    void set_param(ParamId id, float value) {
        switch (id) {
            case ParamId::Attack:
                attack_ms_ = value;
                update_adsr();
                break;
            case ParamId::Decay:
                decay_ms_ = value;
                update_adsr();
                break;
            case ParamId::Sustain:
                sustain_ = value;
                update_adsr();
                break;
            case ParamId::Release:
                release_ms_ = value;
                update_adsr();
                break;
            case ParamId::Cutoff:
                cutoff_hz_ = value;
                update_filter();
                break;
            case ParamId::Resonance:
                resonance_ = value;
                update_filter();
                break;
            case ParamId::Volume:
                volume_ = value;
                break;
            default:
                break;
        }
    }

    float get_param(ParamId id) const {
        switch (id) {
            case ParamId::Attack:   return attack_ms_;
            case ParamId::Decay:    return decay_ms_;
            case ParamId::Sustain:  return sustain_;
            case ParamId::Release:  return release_ms_;
            case ParamId::Cutoff:   return cutoff_hz_;
            case ParamId::Resonance: return resonance_;
            case ParamId::Volume:   return volume_;
            default: return 0.0f;
        }
    }

    // === Audio processing ===

    /// Process single sample
    float process_sample() {
        float sum = 0.0f;
        for (int i = 0; i < NUM_VOICES; ++i) {
            sum += voices_[i].process();
        }
        // Scale down and soft clip to prevent clipping
        return dsp::soft_clip(sum * volume_ * 0.25f);
    }

    /// Process buffer
    void process(float* output, uint32_t frames) {
        for (uint32_t i = 0; i < frames; ++i) {
            output[i] = process_sample();
        }
    }

    float sample_rate() const { return sample_rate_; }

    /// Get number of active voices
    uint32_t active_voice_count() const {
        uint32_t count = 0;
        for (int i = 0; i < NUM_VOICES; ++i) {
            if (voices_[i].is_active()) {
                ++count;
            }
        }
        return count;
    }

private:
    void update_adsr() {
        for (int i = 0; i < NUM_VOICES; ++i) {
            voices_[i].set_adsr(attack_ms_, decay_ms_, sustain_, release_ms_);
        }
    }

    void update_filter() {
        for (int i = 0; i < NUM_VOICES; ++i) {
            voices_[i].set_filter(cutoff_hz_, resonance_);
        }
    }

    Voice voices_[NUM_VOICES];
    float sample_rate_ = 48000.0f;

    // Parameters with defaults
    float attack_ms_ = 10.0f;
    float decay_ms_ = 100.0f;
    float sustain_ = 0.7f;
    float release_ms_ = 200.0f;
    float cutoff_hz_ = 2000.0f;
    float resonance_ = 0.3f;
    float volume_ = 1.0f;
};

} // namespace umi::synth

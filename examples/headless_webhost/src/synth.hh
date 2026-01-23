// =====================================================================
// UMI-OS Simple Synthesizer
// =====================================================================
// Shared between embedded and WASM builds.
// Pure DSP logic - no platform-specific dependencies.
// Uses lib/dsp/ for DSP components.
//
// Conforms to UMIP specification:
// - process(AudioContext&) method for unified application model
// - process_sample() for legacy single-sample processing
// =====================================================================

#pragma once

#include <cstddef>
#include <cstdint>
#include <umidsp.hh>

// Forward declaration for AudioContext (optional dependency)
namespace umi {
struct AudioContext;
}

namespace umi::synth {

// =====================================================================
// Configuration
// =====================================================================

constexpr int NUM_VOICES = 1;

// =====================================================================
// Parameter IDs
// =====================================================================

enum class ParamId { Attack = 0, Decay, Sustain, Release, Cutoff, Resonance, Volume, Count };

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
        this->sample_rate = sample_rate;
        dt = 1.0f / sample_rate;
        // Note: ADSR and filter parameters are NOT set here.
        // They must be set via set_adsr() and set_filter() by PolySynth.
        // This ensures single source of truth for default values.
    }

    void set_sample_rate(float new_rate) {
        sample_rate = new_rate;
        dt = 1.0f / new_rate;
        if (active) {
            float freq = dsp::midi_to_freq(note);
            freq_norm = freq * dt;
        }
    }

    void note_on(uint8_t note_num, uint8_t vel, bool reset_osc = false) {
        note = note_num;
        velocity = static_cast<float>(vel) / 127.0f;

        // Calculate normalized frequency
        float freq = dsp::midi_to_freq(note_num);
        freq_norm = freq * dt;

        // Reset oscillator and filter only when stealing a voice (not on fresh voice)
        // This avoids clicks on normal note-on while allowing clean restart on steal
        if (reset_osc) {
            osc.reset();
            filter.reset();
            fade_samples = FADE_DURATION; // Start fade-in to prevent click
        }

        env.trigger();
        active = true;
    }

    void note_off() { env.release(); }

    bool is_active() const { return active; }
    uint8_t get_note() const { return note; }

    // Parameter setters
    void set_adsr(float attack_ms, float decay_ms, float sustain, float release_ms) {
        env.set_params(attack_ms, decay_ms, sustain, release_ms);
    }

    void set_filter(float cutoff_hz, float resonance) { filter.set_params(cutoff_hz * dt, resonance); }

    float process() {
        if (!active)
            return 0.0f;

        // Generate oscillator output
        float osc_out = osc(freq_norm);

        // Apply filter
        float filtered = filter.process(osc_out);

        // Apply envelope
        float env_val = env(dt);
        float out = filtered * env_val * velocity;

        // Soft clip per-voice to prevent overload when multiple voices overlap
        // Simple soft clipper: x / (1 + |x|)
        if (out > 0.0f) {
            out = out / (1.0f + out);
        } else {
            out = out / (1.0f - out);
        }

        // Fade in after reset to prevent click on voice stealing
        if (fade_samples > 0) {
            float fade = static_cast<float>(FADE_DURATION - fade_samples) / static_cast<float>(FADE_DURATION);
            out *= fade;
            --fade_samples;
        }

        // Deactivate when envelope finishes
        if (!env.active()) {
            active = false;
        }

        return out;
    }

  private:
    static constexpr uint32_t FADE_DURATION = 32; // ~0.7ms at 48kHz

    dsp::SawBL osc;
    dsp::K35 filter;
    dsp::ADSR env;

    float sample_rate = 48000.0f;
    float dt = 1.0f / 48000.0f;
    float freq_norm = 0.0f;
    float velocity = 0.0f;
    uint8_t note = 0;
    bool active = false;
    uint32_t fade_samples = 0; // Fade-in counter after reset
};

// =====================================================================
// Polyphonic Synthesizer
// =====================================================================

class PolySynth {
  public:
    void init(float sr) {
        sample_rate = sr;
        for (int i = 0; i < NUM_VOICES; ++i) {
            voices[i].init(sr);
        }
        // Apply current parameter values to all voices
        update_adsr();
        update_filter();
    }

    void set_sample_rate(float sr) {
        if (sr == sample_rate)
            return;
        sample_rate = sr;
        for (int i = 0; i < NUM_VOICES; ++i) {
            voices[i].set_sample_rate(sr);
        }
        // Recompute coefficient-dependent params
        update_adsr();
        update_filter();
    }

    // === MIDI event handling ===

    /// Handle MIDI bytes (3-byte message)
    void handle_midi(const uint8_t* data, uint8_t size) {
        if (size < 2)
            return;

        uint8_t status = data[0];
        uint8_t cmd = status & 0xF0;
        // uint8_t channel = status & 0x0F;  // not used currently

        if (cmd == 0x90 && size >= 3 && data[2] > 0) {
            // Note On
            note_on(data[1], data[2]);
        } else if (cmd == 0x80 || (cmd == 0x90 && data[2] == 0)) {
            // Note Off
            note_off(data[1]);
        } else if (cmd == 0xB0 && size >= 3) {
            // Control Change
            handle_cc(data[1], data[2]);
        }
    }

    // === Direct note interface ===

    void note_on(uint8_t note, uint8_t vel) {
        // First check if this note is already playing (retrigger without reset)
        for (int i = 0; i < NUM_VOICES; ++i) {
            if (voices[i].is_active() && voices[i].get_note() == note) {
                voices[i].note_on(note, vel, false); // No reset on retrigger
                return;
            }
        }

        // Find free voice
        for (int i = 0; i < NUM_VOICES; ++i) {
            if (!voices[i].is_active()) {
                voices[i].note_on(note, vel, false); // No reset on fresh voice
                return;
            }
        }

        // Voice stealing: NO RESET - just change note and continue from current phase
        // This avoids clicks completely, though may cause brief dissonance during transition
        static uint8_t steal_index = 0;
        steal_index = (steal_index + 1) % NUM_VOICES;
        voices[steal_index].note_on(note, vel, false); // No reset even on steal
    }

    void note_off(uint8_t note) {
        for (int i = 0; i < NUM_VOICES; ++i) {
            if (voices[i].is_active() && voices[i].get_note() == note) {
                voices[i].note_off();
            }
        }
    }

    // === Parameter control ===

    void set_param(ParamId id, float value) {
        switch (id) {
        case ParamId::Attack:
            attack_ms = value;
            update_adsr();
            break;
        case ParamId::Decay:
            decay_ms = value;
            update_adsr();
            break;
        case ParamId::Sustain:
            sustain = value;
            update_adsr();
            break;
        case ParamId::Release:
            release_ms = value;
            update_adsr();
            break;
        case ParamId::Cutoff:
            cutoff_hz = value;
            update_filter();
            break;
        case ParamId::Resonance:
            resonance = value;
            update_filter();
            break;
        case ParamId::Volume:
            volume = value;
            break;
        default:
            break;
        }
    }

    float get_param(ParamId id) const {
        switch (id) {
        case ParamId::Attack:
            return attack_ms;
        case ParamId::Decay:
            return decay_ms;
        case ParamId::Sustain:
            return sustain;
        case ParamId::Release:
            return release_ms;
        case ParamId::Cutoff:
            return cutoff_hz;
        case ParamId::Resonance:
            return resonance;
        case ParamId::Volume:
            return volume;
        default:
            return 0.0f;
        }
    }

    // === Audio processing ===

    /// Process single sample (legacy interface)
    float process_sample() {
        float sum = 0.0f;
        for (int i = 0; i < NUM_VOICES; ++i) {
            sum += voices[i].process();
        }
        // Scale down and soft clip to prevent clipping
        // Scale down by number of voices to prevent clipping when all are active
        return dsp::soft_clip(sum * volume / static_cast<float>(NUM_VOICES));
    }

    /// Process buffer (legacy interface)
    void process(float* output, uint32_t frames) {
        for (uint32_t i = 0; i < frames; ++i) {
            output[i] = process_sample();
        }
    }

    /// Process with AudioContext (UMIP compliant)
    /// Satisfies ProcessorLike concept
    void process(umi::AudioContext& ctx);

    float get_sample_rate() const { return sample_rate; }

    /// Get number of active voices
    uint32_t active_voice_count() const {
        uint32_t count = 0;
        for (int i = 0; i < NUM_VOICES; ++i) {
            if (voices[i].is_active()) {
                ++count;
            }
        }
        return count;
    }

  private:
    static float lerp_cc(uint8_t cc_value, float min_value, float max_value) {
        const float t = static_cast<float>(cc_value) / 127.0f;
        return min_value + (max_value - min_value) * t;
    }

    void handle_cc(uint8_t cc, uint8_t value) {
        if (cc < 21 || cc > 27)
            return;

        const uint8_t index = static_cast<uint8_t>(cc - 21);
        switch (index) {
        case 0:
            set_param(ParamId::Attack, lerp_cc(value, 1.0f, 2000.0f));
            break;
        case 1:
            set_param(ParamId::Decay, lerp_cc(value, 1.0f, 2000.0f));
            break;
        case 2:
            set_param(ParamId::Sustain, lerp_cc(value, 0.0f, 1.0f));
            break;
        case 3:
            set_param(ParamId::Release, lerp_cc(value, 5.0f, 4000.0f));
            break;
        case 4:
            set_param(ParamId::Cutoff, lerp_cc(value, 50.0f, 8000.0f));
            break;
        case 5:
            set_param(ParamId::Resonance, lerp_cc(value, 0.0f, 0.99f));
            break;
        case 6:
            set_param(ParamId::Volume, lerp_cc(value, 0.0f, 1.0f));
            break;
        default:
            break;
        }
    }

    void update_adsr() {
        for (int i = 0; i < NUM_VOICES; ++i) {
            voices[i].set_adsr(attack_ms, decay_ms, sustain, release_ms);
        }
    }

    void update_filter() {
        for (int i = 0; i < NUM_VOICES; ++i) {
            voices[i].set_filter(cutoff_hz, resonance);
        }
    }

    Voice voices[NUM_VOICES];
    float sample_rate = 48000.0f;

    // Parameters with defaults
    float attack_ms = 10.0f;
    float decay_ms = 100.0f;
    float sustain = 0.7f;
    float release_ms = 200.0f;
    float cutoff_hz = 2000.0f;
    float resonance = 0.3f;
    float volume = 1.0f;
};

} // namespace umi::synth

// =====================================================================
// AudioContext-based processing (UMIP compliant)
// =====================================================================

// Include AudioContext if available (outside namespace to avoid std:: conflicts)
#if __has_include(<umios/core/audio_context.hh>)
    #include <umios/core/audio_context.hh>

namespace umi::synth {

inline void PolySynth::process(umi::AudioContext& ctx) {
    // Keep synth in sync with AudioContext timing
    set_sample_rate(static_cast<float>(ctx.sample_rate));

    // Process MIDI events
    for (const auto& ev : ctx.input_events) {
        if (ev.type == umi::EventType::Midi) {
            handle_midi(ev.midi.bytes, ev.midi.size);
        }
    }

    // Generate audio output
    auto* out = ctx.output(0);
    if (!out)
        return;

    for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
        out[i] = process_sample();
    }
}

} // namespace umi::synth

#else

namespace umi::synth {
// Stub when AudioContext is not available
inline void PolySynth::process(umi::AudioContext&) {}
} // namespace umi::synth

#endif

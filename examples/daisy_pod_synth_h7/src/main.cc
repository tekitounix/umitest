// SPDX-License-Identifier: MIT
// Daisy Pod Synth Application (.umia) for STM32H750 QSPI XIP
// Simple 8-voice polyphonic saw synth with ADSR envelope
// Demonstrates: register_processor, process(AudioContext&), syscall API

#include <cstdint>
#include <umi_app.hh>
#include <umios/app/syscall.hh>
#include <umios/core/audio_context.hh>
#include <umios/kernel/loader.hh>

using namespace umi;

// ============================================================================
// Polyphonic Saw Synth
// ============================================================================

namespace {

constexpr std::uint32_t midi_note_to_phase_inc(std::uint8_t note, float sample_rate) {
    // A4 = 440Hz, phase accumulator is 32-bit
    constexpr float a4_freq = 440.0f;
    constexpr float pow2_32 = 4294967296.0f;

    constexpr float semitone_ratios[12] = {
        1.0f,       1.05946f, 1.12246f, 1.18921f,
        1.25992f,   1.33484f, 1.41421f, 1.49831f,
        1.58740f,   1.68179f, 1.78180f, 1.88775f,
    };

    int semitone = note % 12;
    int octave = note / 12;
    float freq = a4_freq * semitone_ratios[semitone] / semitone_ratios[9];
    int shift = octave - 5;
    if (shift > 0) {
        for (int i = 0; i < shift; ++i) freq *= 2.0f;
    } else if (shift < 0) {
        for (int i = 0; i < -shift; ++i) freq *= 0.5f;
    }
    return static_cast<std::uint32_t>(freq * pow2_32 / sample_rate);
}

struct Voice {
    std::uint32_t phase = 0;
    std::uint32_t phase_inc = 0;
    float amplitude = 0.0f;
    float env_level = 0.0f;
    float env_target = 0.0f;
    float env_rate = 0.0f;
    std::uint8_t note = 0;
    bool active = false;
};

constexpr int NUM_VOICES = 8;

class SawSynth {
public:
    void process(AudioContext& ctx) {
        if (sample_rate_ == 0.0f) {
            sample_rate_ = static_cast<float>(ctx.sample_rate);
        }

        // Process MIDI events
        for (const auto& ev : ctx.input_events) {
            if (ev.type == EventType::MIDI) {
                handle_midi(ev.midi.bytes, ev.midi.size);
            }
        }

        auto* out_l = ctx.output(0);
        auto* out_r = ctx.output(1);
        if (!out_l) return;

        for (std::uint32_t i = 0; i < ctx.buffer_size; ++i) {
            float sample = 0.0f;

            for (auto& v : voices_) {
                if (!v.active) continue;

                // Envelope
                if (v.env_rate > 0.0f && v.env_level < v.env_target) {
                    v.env_level += v.env_rate;
                    if (v.env_level > v.env_target) v.env_level = v.env_target;
                } else if (v.env_rate < 0.0f) {
                    v.env_level += v.env_rate;
                    if (v.env_level <= 0.0f) {
                        v.env_level = 0.0f;
                        v.active = false;
                        continue;
                    }
                }

                // Saw oscillator
                float saw = static_cast<float>(static_cast<std::int32_t>(v.phase)) / 2147483648.0f;
                sample += saw * v.env_level * v.amplitude;
                v.phase += v.phase_inc;
            }

            sample *= volume_;
            out_l[i] = sample;
            if (out_r) out_r[i] = sample;
        }
    }

private:
    Voice voices_[NUM_VOICES] = {};
    float sample_rate_ = 0.0f;
    float volume_ = 0.5f;

    void handle_midi(const std::uint8_t* data, std::uint8_t len) {
        if (len < 2) return;
        std::uint8_t status = data[0] & 0xF0;

        if (status == 0x90 && len >= 3 && data[2] > 0) {
            note_on(data[1], data[2]);
        } else if (status == 0x80 || (status == 0x90 && len >= 3 && data[2] == 0)) {
            note_off(data[1]);
        } else if (status == 0xB0 && len >= 3) {
            if (data[1] == 7) {
                volume_ = static_cast<float>(data[2]) / 127.0f;
            }
        }
    }

    void note_on(std::uint8_t note, std::uint8_t velocity) {
        Voice* target = nullptr;
        for (auto& v : voices_) {
            if (!v.active) { target = &v; break; }
        }
        if (!target) target = &voices_[0];

        target->note = note;
        target->phase = 0;
        target->phase_inc = midi_note_to_phase_inc(note, sample_rate_);
        target->amplitude = static_cast<float>(velocity) / 127.0f;
        target->active = true;
        target->env_level = 0.0f;
        target->env_target = 1.0f;
        target->env_rate = 1.0f / (sample_rate_ * 0.01f);
    }

    void note_off(std::uint8_t note) {
        for (auto& v : voices_) {
            if (v.active && v.note == note) {
                v.env_target = 0.0f;
                v.env_rate = -v.env_level / (sample_rate_ * 0.1f);
                if (v.env_rate == 0.0f) v.env_rate = -0.0001f;
                break;
            }
        }
    }
};

} // namespace

// ============================================================================
// Main
// ============================================================================

int main() {
    static SawSynth synth;
    umi::register_processor(synth);

    // Controller loop: yield forever (LED/HID control remains in kernel)
    while (true) {
        umi::syscall::yield();
    }
}

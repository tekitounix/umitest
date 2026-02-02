// SPDX-License-Identifier: MIT
// Daisy Pod Kernel - Inline polyphonic synth (fallback processor)
#pragma once

#include <cstdint>
#include <atomic>

namespace daisy_kernel {

// ============================================================================
// Simple Event Queue (HID/MIDI → synth bridge)
// ============================================================================

enum class EventType : std::uint8_t {
    NONE = 0, NOTE_ON, NOTE_OFF, CC, KNOB,
    BUTTON_DOWN, BUTTON_UP, ENCODER_INCREMENT,
};

struct Event {
    EventType type = EventType::NONE;
    std::uint8_t channel = 0;
    std::uint8_t param = 0;
    std::uint8_t value = 0;
};

struct MidiEventQueue {
    static constexpr std::uint32_t CAPACITY = 64;
    Event events[CAPACITY] = {};
    std::atomic<std::uint32_t> head{0};
    std::atomic<std::uint32_t> tail{0};

    bool push(const Event& e) {
        auto h = head.load(std::memory_order_relaxed);
        auto next = (h + 1) % CAPACITY;
        if (next == tail.load(std::memory_order_acquire)) return false;
        events[h] = e;
        head.store(next, std::memory_order_release);
        return true;
    }

    bool pop(Event& e) {
        auto t = tail.load(std::memory_order_relaxed);
        if (t == head.load(std::memory_order_acquire)) return false;
        e = events[t];
        tail.store((t + 1) % CAPACITY, std::memory_order_release);
        return true;
    }
};

// ============================================================================
// Minimal Polyphonic Synth (8 voices, saw oscillator)
// ============================================================================

struct Voice {
    std::uint32_t phase = 0;
    std::uint32_t phase_inc = 0;
    std::int32_t amplitude = 0;
    std::uint8_t note = 0;
    bool active = false;
    std::int32_t env_level = 0;
    std::int32_t env_target = 0;
    std::int32_t env_rate = 0;
};

constexpr std::uint32_t midi_note_to_phase_inc(std::uint8_t note) {
    constexpr std::uint32_t a4_inc = 39370534U;
    if (note == 69) return a4_inc;
    constexpr std::uint32_t semitone_ratio[12] = {
        65536, 69433, 73562, 77936, 82570, 87480,
        92682, 98193, 104032, 110218, 116772, 123715,
    };
    int semitone = note % 12;
    int octave = note / 12;
    std::uint64_t inc = static_cast<std::uint64_t>(a4_inc) * semitone_ratio[semitone] / semitone_ratio[9];
    int shift = octave - 5;
    if (shift > 0) inc <<= shift;
    else if (shift < 0) inc >>= (-shift);
    return static_cast<std::uint32_t>(inc);
}

class DaisySynth {
    static constexpr int NUM_VOICES = 8;
    Voice voices[NUM_VOICES] = {};

public:
    volatile float volume = 0.5f;

    /// Push MIDI/HID event to synth queue
    MidiEventQueue& event_queue() { return queue; }

    /// Process pending MIDI events and render audio into interleaved int32 buffer
    void process(std::int32_t* out, std::uint32_t frames) {
        process_events();

        bool has_active = false;
        for (const auto& v : voices) {
            if (v.active) { has_active = true; break; }
        }

        if (has_active) {
            render(out, frames);
        } else {
            // Silence — caller handles passthrough
            return;
        }
    }

    [[nodiscard]] bool has_active_voice() const {
        for (const auto& v : voices) {
            if (v.active) return true;
        }
        return false;
    }

private:
    MidiEventQueue queue;

    void process_events() {
        Event ev;
        while (queue.pop(ev)) {
            if (ev.type == EventType::NOTE_ON) {
                Voice* target = nullptr;
                for (auto& v : voices) {
                    if (!v.active) { target = &v; break; }
                }
                if (target == nullptr) target = &voices[0];

                target->note = ev.param;
                target->phase = 0;
                target->phase_inc = midi_note_to_phase_inc(ev.param);
                target->amplitude = static_cast<std::int32_t>(ev.value) << 16;
                target->active = true;
                target->env_level = 0;
                target->env_target = target->amplitude;
                target->env_rate = target->amplitude / 48;
            } else if (ev.type == EventType::NOTE_OFF) {
                for (auto& v : voices) {
                    if (v.active && v.note == ev.param) {
                        v.env_target = 0;
                        v.env_rate = -(v.env_level / 480);
                        if (v.env_rate == 0) v.env_rate = -1;
                        break;
                    }
                }
            }
        }
    }

    void render(std::int32_t* out, std::uint32_t frames) {
        for (std::uint32_t i = 0; i < frames * 2; ++i) {
            out[i] = 0;
        }

        float vol = volume;

        for (auto& v : voices) {
            if (!v.active) continue;
            for (std::uint32_t i = 0; i < frames; ++i) {
                if (v.env_rate > 0 && v.env_level < v.env_target) {
                    v.env_level += v.env_rate;
                    if (v.env_level > v.env_target) v.env_level = v.env_target;
                } else if (v.env_rate < 0) {
                    v.env_level += v.env_rate;
                    if (v.env_level <= 0) {
                        v.env_level = 0;
                        v.active = false;
                        break;
                    }
                }
                std::int32_t saw = static_cast<std::int32_t>(v.phase >> 8) - (1 << 23);
                std::int32_t sample = static_cast<std::int32_t>(
                    (static_cast<std::int64_t>(saw) * v.env_level >> 24) * static_cast<std::int64_t>(vol * 256) >> 8
                );
                out[i * 2] += sample;
                out[i * 2 + 1] += sample;
                v.phase += v.phase_inc;
            }
        }
    }
};

} // namespace daisy_kernel

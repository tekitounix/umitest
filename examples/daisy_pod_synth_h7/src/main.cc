// SPDX-License-Identifier: MIT
// Daisy Pod H7 Synth Application (.umia)
// Minimal synth that registers a simple process function via syscall

#include <cstdint>
#include <cmath>

// Syscall interface (inline SVC calls)
namespace syscall {

constexpr uint32_t nr_register_proc = 2;
constexpr uint32_t nr_yield = 1;

inline int32_t svc(uint32_t nr, uint32_t arg0 = 0, uint32_t arg1 = 0) {
    int32_t result;
    __asm__ volatile(
        "mov r0, %[a0]\n"
        "mov r1, %[a1]\n"
        "mov r12, %[nr]\n"
        "svc 0\n"
        "mov %[res], r0\n"
        : [res] "=r"(result)
        : [nr] "r"(nr), [a0] "r"(arg0), [a1] "r"(arg1)
        : "r0", "r1", "r12", "memory");
    return result;
}

inline void yield() { svc(nr_yield); }

inline int register_processor(void* fn) {
    return svc(nr_register_proc, reinterpret_cast<uint32_t>(fn), 0);
}

} // namespace syscall

// ============================================================================
// Simple Synth Voice
// ============================================================================

struct Voice {
    float phase = 0.0f;
    float freq = 0.0f;
    float amp = 0.0f;
    bool active = false;
    uint8_t note = 0;

    float render(float dt) {
        if (!active) return 0.0f;
        phase += freq * dt;
        if (phase >= 1.0f) phase -= 1.0f;
        // Simple saw wave
        return (phase * 2.0f - 1.0f) * amp;
    }
};

constexpr uint32_t max_voices = 8;
Voice voices[max_voices];
float volume = 0.5f;

float note_to_freq(uint8_t note) {
    return 440.0f * powf(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
}

void note_on(uint8_t note, uint8_t vel) {
    for (auto& v : voices) {
        if (!v.active) {
            v.note = note;
            v.freq = note_to_freq(note);
            v.amp = static_cast<float>(vel) / 127.0f;
            v.phase = 0.0f;
            v.active = true;
            return;
        }
    }
}

void note_off(uint8_t note) {
    for (auto& v : voices) {
        if (v.active && v.note == note) {
            v.active = false;
            return;
        }
    }
}

// ============================================================================
// Process function (called by kernel via AppLoader.call_process)
// Signature: void(float* out, const float* in, uint32_t frames, float dt)
// ============================================================================

extern "C" void synth_process(float* out, const float* /*in*/, uint32_t frames, float dt) {
    for (uint32_t i = 0; i < frames; ++i) {
        float sample = 0.0f;
        for (auto& v : voices) {
            sample += v.render(dt);
        }
        sample *= volume;

        // Stereo interleaved output
        out[i * 2] = sample;
        out[i * 2 + 1] = sample;
    }
}

// ============================================================================
// Main (Controller task)
// ============================================================================

int main() {
    // Register simple process function with kernel
    syscall::register_processor(reinterpret_cast<void*>(&synth_process));

    // Controller loop — yield forever
    // MIDI events will be handled via shared memory event queue (future)
    while (true) {
        syscall::yield();
    }
}

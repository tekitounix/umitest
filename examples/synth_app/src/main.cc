// SPDX-License-Identifier: MIT
// Polyphonic Synth Application (.umiapp)
// Uses PolySynth from headless_webhost
// Receives MIDI via kernel syscall, outputs audio via synth_process callback

#include <synth.hh> // headless_webhost/src/synth.hh
#include <umi_app.hh>

// ============================================================================
// Synth Instance
// ============================================================================

static umi::synth::PolySynth g_synth;
static bool g_initialized = false;

// ============================================================================
// Audio Processor (AudioContext-based)
// ============================================================================

namespace {
struct SynthProcessor {
    void process(umi::AudioContext& ctx) {
        // Initialize or update sample rate from AudioContext
        if (!g_initialized) {
            g_synth.init(static_cast<float>(ctx.sample_rate));
            g_initialized = true;
        } else if (static_cast<uint32_t>(g_synth.get_sample_rate()) != ctx.sample_rate) {
            g_synth.set_sample_rate(static_cast<float>(ctx.sample_rate));
        }

        // Process input events from AudioContext
        for (const auto& ev : ctx.input_events) {
            if (ev.type == umi::EventType::Midi) {
                g_synth.handle_midi(ev.midi.bytes, ev.midi.size);
            }
        }

        auto* out_l = ctx.output(0);
        auto* out_r = ctx.output(1);
        if (!out_l)
            return;

        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            float sample = g_synth.process_sample();
            out_l[i] = sample;
            if (out_r) {
                out_r[i] = sample;
            }
        }
    }
};

static SynthProcessor g_processor;
} // namespace

// ============================================================================
// Main
// ============================================================================

int main() {
    // Register processor with kernel (AudioContext-based)
    umi::register_processor(g_processor);

    // Return to kernel - audio processing happens via callback
    // crt0 will call yield() syscall after main() returns
    return 0;
}

// SPDX-License-Identifier: MIT
// Polyphonic Synth Application (.umiapp)
// Uses PolySynth from headless_webhost
// Receives MIDI via kernel syscall, outputs audio via synth_process callback
// Demonstrates coroutine-based control task with LED/Button handling

#include <synth.hh> // headless_webhost/src/synth.hh
#include <umi_app.hh>
#include <umios/kernel/coro.hh>
#include <umios/app/syscall.hh>

using namespace umi::coro;
using namespace umi::coro::literals;

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
// Coroutine Tasks
// ============================================================================

/// LED heartbeat task - blinks green LED at 500ms interval
template <std::size_t N>
Task<void> led_heartbeat_task(SchedulerContext<N>& ctx) {
    while (true) {
        co_await ctx.sleep(500ms);
        umi::syscall::led_toggle(0);  // Green LED (PD12)
    }
}

/// Button handler task - cycles through LED modes on button press
template <std::size_t N>
Task<void> button_handler_task(SchedulerContext<N>& ctx) {
    uint8_t mode = 0;

    while (true) {
        // Wait for button event
        co_await ctx.wait_for(umi::syscall::event::Button);

        // Check if button was actually pressed
        if (umi::syscall::button_pressed()) {
            mode = (mode + 1) % 5;  // 5 modes: 0-4

            // Update LED pattern based on mode
            // Mode 0: All off (heartbeat only)
            // Mode 1-4: Binary pattern on LEDs 1-3
            for (uint8_t i = 1; i < 4; ++i) {
                umi::syscall::led_set(i, (mode >> (i - 1)) & 1);
            }
        }
    }
}

// ============================================================================
// Scheduler Wait/Time Functions
// ============================================================================

/// Wait function for scheduler (wraps syscall)
static uint32_t sched_wait(uint32_t mask, usec timeout_us) {
    return umi::syscall::wait_event(mask, static_cast<uint32_t>(timeout_us));
}

/// Time function for scheduler (wraps syscall)
static usec sched_get_time() {
    return static_cast<usec>(umi::syscall::get_time_usec());
}

// ============================================================================
// Main
// ============================================================================

int main() {
    // Register processor with kernel (AudioContext-based)
    umi::register_processor(g_processor);

    // Initialize LEDs (all off)
    for (uint8_t i = 0; i < 4; ++i) {
        umi::syscall::led_set(i, false);
    }

    // Create scheduler with wait/time functions
    Scheduler<4> sched(sched_wait, sched_get_time);
    SchedulerContext<4> ctx(sched);

    // Spawn coroutine tasks
    sched.spawn(led_heartbeat_task(ctx));
    sched.spawn(button_handler_task(ctx));

    // Run scheduler (never returns)
    sched.run();
}

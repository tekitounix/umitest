// SPDX-License-Identifier: MIT
// Polyphonic Synth Application (.umia)
// Uses PolySynth from headless_webhost
// Processor/Controller separation model:
//   - Processor: Audio processing + MIDI/Button events (called from ISR)
//   - Controller: LED state management (main loop with co_await 16ms)

#include <atomic>
#include <synth.hh> // headless_webhost/src/synth.hh
#include <umi_app.hh>
#include <umios/app/syscall.hh>
#include <umios/core/event_router.hh>
#include <umios/core/param_mapping.hh>
#include <umios/kernel/coro.hh>
#include <umios/kernel/loader.hh>

using namespace umi::coro::literals;
using namespace umi;

// ============================================================================
// Parameter Definitions
// ============================================================================

namespace param {
constexpr uint8_t CUTOFF = 0;
constexpr uint8_t RESONANCE = 1;
} // namespace param

// ============================================================================
// Routing Configuration
// ============================================================================

// Route table: notes → audio, CC#74/71 → param pipeline
static const RouteTable g_route_table = [] {
    auto rt = RouteTable::make_default();
    rt.control_change[74] = ROUTE_PARAM; // CC#74 (Brightness) → param
    rt.control_change[71] = ROUTE_PARAM; // CC#71 (Resonance) → param
    return rt;
}();

// Parameter mapping: CC → denormalized param values
static const ParamMapping g_param_mapping = [] {
    auto pm = ParamMapping::make_empty();
    pm.entries[74] = {param::CUTOFF, {}, 20.0f, 20000.0f};   // CC#74 → Cutoff Hz
    pm.entries[71] = {param::RESONANCE, {}, 0.0f, 1.0f};     // CC#71 → Resonance 0-1
    return pm;
}();

// ============================================================================
// LED Constants
// ============================================================================

namespace led {
constexpr uint8_t GREEN = 0;  // PD12 - Heartbeat
constexpr uint8_t ORANGE = 1; // PD13 - Mode indicator
constexpr uint8_t RED = 2;    // PD14 - Mode indicator
constexpr uint8_t BLUE = 3;   // PD15 - Mode indicator
} // namespace led

// ============================================================================
// Synth Processor (Audio + Events)
// ============================================================================

class SynthProcessor {
  public:
    void process(AudioContext& ctx) {
        // Initialize or update sample rate
        if (!initialized_) {
            synth_.init(static_cast<float>(ctx.sample_rate));
            initialized_ = true;
        } else if (static_cast<uint32_t>(synth_.get_sample_rate()) != ctx.sample_rate) {
            synth_.set_sample_rate(static_cast<float>(ctx.sample_rate));
        }

        // Read denormalized parameters from SharedParamState (written by EventRouter)
        if (ctx.params && ctx.params->changed_flags != 0) {
            if (ctx.params->changed_flags & (1u << param::CUTOFF)) {
                last_cutoff_ = ctx.params->values[param::CUTOFF];
            }
            if (ctx.params->changed_flags & (1u << param::RESONANCE)) {
                last_resonance_ = ctx.params->values[param::RESONANCE];
            }
            // TODO: Apply cutoff/resonance to synth filter when PolySynth exposes filter API
        }

        // Process input events (MIDI notes via ROUTE_AUDIO, buttons)
        for (const auto& ev : ctx.input_events) {
            switch (ev.type) {
            case EventType::Midi:
                synth_.handle_midi(ev.midi.bytes, ev.midi.size);
                break;
            case EventType::ButtonDown:
                led_mode_.store((led_mode_.load(std::memory_order_relaxed) + 1) % 5, std::memory_order_relaxed);
                break;
            default:
                break;
            }
        }

        // Generate audio output
        auto* out_l = ctx.output(0);
        auto* out_r = ctx.output(1);
        if (!out_l) {
            return;
        }

        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            float sample = synth_.process_sample();
            out_l[i] = sample;
            if (out_r) {
                out_r[i] = sample;
            }

            // Update LFO phase for LED heartbeat (wrap at 1.0)
            lfo_phase_ += ctx.dt * 2.0f; // 2Hz = 500ms period
            if (lfo_phase_ >= 1.0f) {
                lfo_phase_ -= 1.0f;
            }
        }

        // Export LFO phase for controller (atomic write)
        lfo_phase_out_.store(lfo_phase_, std::memory_order_relaxed);
    }

    // Atomic accessors for Controller
    [[nodiscard]] float lfo_phase() const noexcept { return lfo_phase_out_.load(std::memory_order_relaxed); }
    [[nodiscard]] uint8_t led_mode() const noexcept { return led_mode_.load(std::memory_order_relaxed); }

  private:
    umi::synth::PolySynth synth_;
    bool initialized_ = false;

    // Cached parameter values (read from SharedParamState in process())
    float last_cutoff_ = 1000.0f;
    float last_resonance_ = 0.5f;

    // LFO for heartbeat LED
    float lfo_phase_ = 0.0f;
    std::atomic<float> lfo_phase_out_{0.0f};

    // LED mode (changed by button events)
    std::atomic<uint8_t> led_mode_{0};
};

// ============================================================================
// Controller Task (LED management)
// ============================================================================

umi::coro::Task<void> controller_task(SynthProcessor& proc, umi::kernel::SharedMemory& shared) {
    while (true) {
        co_await 16ms;

        float phase = proc.lfo_phase();
        uint8_t mode = proc.led_mode();

        uint8_t led_state = 0;

        // Green LED: heartbeat (on when phase < 0.5)
        if (phase < 0.5f) {
            led_state |= (1 << led::GREEN);
        }

        // Mode LEDs: binary pattern on Orange/Red/Blue
        if (mode > 0) {
            if ((mode & 1) != 0) {
                led_state |= (1 << led::ORANGE);
            }
            if ((mode & 2) != 0) {
                led_state |= (1 << led::RED);
            }
            if ((mode & 4) != 0) {
                led_state |= (1 << led::BLUE);
            }
        }

        shared.led_state.store(led_state, std::memory_order_relaxed);
    }
}

// ============================================================================
// Main
// ============================================================================

int main() {
    SynthProcessor processor;

    // Register processor with kernel
    umi::register_processor(processor);

    // Configure routing: notes → audio, CC#74/71 → param pipeline
    umi::set_route_table(&g_route_table);
    umi::set_param_mapping(&g_param_mapping);

    // Get shared memory from kernel
    auto& shared = *static_cast<umi::kernel::SharedMemory*>(umi::get_shared());
    shared.led_state.store(0, std::memory_order_relaxed);

    // Create scheduler with syscall adapters
    umi::coro::Scheduler<2> sched(umi::syscall::coro_adapter::wait, umi::syscall::coro_adapter::get_time);

    // Spawn controller task
    sched.spawn(controller_task(processor, shared));

    // Run scheduler (never returns)
    sched.run();
}

// SPDX-License-Identifier: MIT
// Polyphonic Synth Application (.umia)
// Uses PolySynth from headless_webhost
// Processor/Controller separation model:
//   - Processor: Audio processing + MIDI events (called from ISR)
//   - Controller: Button/LED state management (main loop with co_await 16ms)

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

// AppConfig: route table + param mapping consolidated
static const AppConfig g_app_config = [] {
    auto cfg = AppConfig::make_default();
    // Route CC#74/71 → param pipeline
    cfg.route_table.control_change[74] = ROUTE_PARAM;
    cfg.route_table.control_change[71] = ROUTE_PARAM;
    // Parameter mapping: CC → denormalized param values
    cfg.param_mapping.entries[74] = {param::CUTOFF, {}, 20.0f, 20000.0f};
    cfg.param_mapping.entries[71] = {param::RESONANCE, {}, 0.0f, 1.0f};
    return cfg;
}();

// ============================================================================
// LED Constants
// ============================================================================

namespace led {
constexpr uint8_t GREEN = 0; // PD12 - Heartbeat
constexpr uint8_t RED = 2;   // PD14 - Mode indicator
} // namespace led

// ============================================================================
// Synth Processor (Audio only — no button handling)
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

        // Process input events (MIDI notes via ROUTE_AUDIO)
        for (const auto& ev : ctx.input_events) {
            if (ev.type == EventType::MIDI) {
                synth_.handle_midi(ev.midi.bytes, ev.midi.size);
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
    }

    [[nodiscard]] float lfo_phase() const noexcept { return lfo_phase_; }

  private:
    umi::synth::PolySynth synth_;
    bool initialized_ = false;

    // Cached parameter values (read from SharedParamState in process())
    float last_cutoff_ = 1000.0f;
    float last_resonance_ = 0.5f;

    // LFO for heartbeat LED (updated in process(), read in controller_task)
    // Note: single-writer (audio ISR), single-reader (controller task) — no atomic needed
    // since we only read an approximate phase value for LED blinking
    float lfo_phase_ = 0.0f;
};

// ============================================================================
// Controller Task (Button + LED management via ControlEventQueue)
// ============================================================================

umi::coro::Task<void> controller_task(SynthProcessor& proc, umi::kernel::SharedMemory& shared) {
    uint8_t led_mode = 0;

    while (true) {
        co_await 16ms;

        // Poll ControlEventQueue for button events (routed via ROUTE_CONTROL)
        // ControlEvents are delivered by EventRouter → g_control_event_queue
        // The kernel drains them; here we check SharedInputState for button changes
        // Button events come as input_events in AudioContext (ROUTE_AUDIO path),
        // but for controller we use the control path:
        // For now, check SharedMemory input_state for button (input 0)
        // The EventRouter receive_input() with ROUTE_CONTROL pushes ControlEvent::INPUT_CHANGE
        // which the kernel's control_event_queue receives.
        // Since synth_app's controller_task doesn't have direct access to the kernel's
        // control_event_queue, we use the audio event path: buttons routed to ROUTE_AUDIO
        // arrive as ButtonDown/ButtonUp events in process(). We need a way to communicate
        // button presses from audio ISR to controller.
        //
        // Simplest approach: use SharedInputState. The kernel updates input_state.raw[0]
        // with the button value via EventRouter. We detect edges here.

        // Read button state from SharedInputState (written by EventRouter ROUTE_PARAM path
        // or directly by kernel)
        static uint16_t prev_button = 0;
        uint16_t cur_button = shared.input.raw[0];
        if (cur_button != prev_button) {
            if (cur_button == 0xFFFF && prev_button == 0) {
                // Button pressed — cycle LED mode
                led_mode = (led_mode + 1) % 5;
            }
            prev_button = cur_button;
        }

        float phase = proc.lfo_phase();

        uint8_t led_state = 0;

        // Green LED: heartbeat (on when phase < 0.5)
        if (phase < 0.5f) {
            led_state |= (1 << led::GREEN);
        }

        // Mode LEDs: binary pattern on Red (Orange/Blue are kernel-controlled)
        if (led_mode > 0) {
            if ((led_mode & 1) != 0) {
                led_state |= (1 << led::RED);
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

    // Configure routing and parameter mapping
    umi::set_app_config(&g_app_config);

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

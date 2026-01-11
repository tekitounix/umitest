// =====================================================================
// UMI-OS Example Application
// =====================================================================
//
// Demonstrates:
//   - Concept-based Processor (no inheritance required)
//   - Coroutine-based UI/MIDI tasks
//   - Sample-accurate event processing
//
// =====================================================================

#include <cstdint>
#include <cmath>
#include <chrono>
#include <array>

#include <umi/processor.hpp>
#include <umi/audio_context.hpp>
#include <umi/event.hpp>
#include "../core/umi_coro.hh"

// =====================================================================
// Syscall Interface (provided by kernel)
// =====================================================================

namespace umi_syscall {

using usec = std::uint64_t;

enum class RegionId : std::uint8_t {
    Audio       = 0,
    Midi        = 1,
    Framebuffer = 2,
    HwState     = 3,
};

struct RegionDesc {
    void* base;
    std::size_t size;
    bool valid() const { return base != nullptr && size > 0; }
};

namespace event {
    constexpr std::uint32_t MidiReady  = 1 << 1;
    constexpr std::uint32_t VSync      = 1 << 2;
}

// Syscall stubs (implemented by kernel's SVC handler)
inline RegionDesc get_shared(RegionId id) {
    RegionDesc desc{};
    (void)id;
    return desc;
}

inline std::uint32_t wait(std::uint32_t mask, usec timeout_us) {
    std::uint32_t result = 0;
    (void)mask;
    (void)timeout_us;
    return result;
}

inline usec get_time_usec() {
    return 0;
}

} // namespace umi_syscall

// =====================================================================
// Shared Memory Layouts
// =====================================================================

struct HwState {
    volatile std::uint64_t uptime_usec;
    volatile std::uint32_t sample_rate;
    volatile std::uint16_t adc_values[8];
    volatile std::uint8_t encoder_positions[4];
    volatile std::uint8_t cpu_load_percent;
};

struct MidiShared {
    volatile std::size_t head;
    volatile std::size_t tail;
    std::uint8_t data[256];
};

// =====================================================================
// SimpleSynth - Concept-based Processor (no inheritance!)
// =====================================================================
//
// Satisfies:
//   - ProcessorLike (has process())
//   - Controllable (has control())
//   - HasParams (has params())
//
// =====================================================================

class SimpleSynth {
public:
    explicit SimpleSynth(std::uint32_t sample_rate)
        : sample_rate_(static_cast<float>(sample_rate))
    {
        update_phase_inc();
    }
    
    // === ProcessorLike: required ===
    void process(umi::AudioContext& ctx) {
        if (ctx.num_outputs() < 1) return;
        
        auto* out = ctx.output(0);
        
        // Process sample-accurate events
        for (std::size_t i = 0; i < ctx.buffer_size; ++i) {
            // Check for events at this sample
            umi::Event ev;
            while (ctx.events.pop_until(static_cast<umi::sample_position_t>(i), ev)) {
                handle_event(ev);
            }
            
            // Generate audio
            out[i] = amplitude_ * std::sin(phase_);
            phase_ += phase_inc_;
            if (phase_ >= two_pi_) phase_ -= two_pi_;
        }
    }
    
    // === Controllable: optional ===
    void control(umi::ControlContext& ctx) {
        // Process any remaining events (parameter changes from UI, etc.)
        umi::Event ev;
        while (ctx.events.pop(ev)) {
            handle_event(ev);
        }
    }
    
    // === HasParams: optional ===
    std::span<const umi::ParamDescriptor> params() const {
        return params_;
    }
    
    // === Public interface ===
    void set_frequency(float hz) {
        frequency_ = hz;
        update_phase_inc();
    }
    
    void set_amplitude(float amp) {
        amplitude_ = amp;
    }
    
private:
    void handle_event(const umi::Event& ev) {
        switch (ev.type) {
            case umi::EventType::Midi:
                if (ev.midi.is_note_on()) {
                    // Simple MIDI-to-frequency (A4 = 440Hz at note 69)
                    float note = static_cast<float>(ev.midi.note());
                    frequency_ = 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
                    amplitude_ = static_cast<float>(ev.midi.velocity()) / 127.0f * 0.3f;
                    update_phase_inc();
                } else if (ev.midi.is_note_off()) {
                    amplitude_ = 0.0f;
                }
                break;
                
            case umi::EventType::Param:
                if (ev.param.id == 0) {
                    set_frequency(params_[0].denormalize(ev.param.value));
                } else if (ev.param.id == 1) {
                    set_amplitude(ev.param.value);
                }
                break;
                
            default:
                break;
        }
    }
    
    void update_phase_inc() {
        phase_inc_ = frequency_ * two_pi_ / sample_rate_;
    }
    
    static constexpr float two_pi_ = 6.283185307f;
    
    static constexpr umi::ParamDescriptor params_[] = {
        {0, "Frequency", 440.0f, 20.0f, 20000.0f},
        {1, "Amplitude", 0.3f, 0.0f, 1.0f},
    };
    
    float sample_rate_;
    float phase_ = 0.0f;
    float phase_inc_ = 0.0f;
    float frequency_ = 440.0f;
    float amplitude_ = 0.3f;
};

// Verify concepts at compile time
static_assert(umi::ProcessorLike<SimpleSynth>);
static_assert(umi::Controllable<SimpleSynth>);
static_assert(umi::HasParams<SimpleSynth>);

// Global synth instance
SimpleSynth* g_synth = nullptr;

// =====================================================================
// Audio Callback (called from DMA ISR or audio thread)
// =====================================================================

extern "C" void umi_audio_process(float* input, float* output, 
                                   std::size_t frames, std::uint8_t channels,
                                   std::uint32_t sample_rate) {
    (void)input;
    
    if (!g_synth) return;
    
    // Create AudioContext
    // Note: In real implementation, events would come from kernel's event queue
    static umi::EventQueue<64> events;
    
    std::array<float, 1024> out_buf;
    float* out_ptr = out_buf.data();
    
    umi::AudioContext ctx{
        .inputs = {},
        .outputs = std::span<umi::sample_t* const>(&out_ptr, 1),
        .events = events,
        .sample_rate = sample_rate,
        .buffer_size = static_cast<std::uint32_t>(frames),
        .sample_position = 0
    };
    
    // Process using concept-based API (inlined, no vtable)
    umi::process_once(*g_synth, ctx);
    
    // Copy to interleaved output
    for (std::size_t i = 0; i < frames; ++i) {
        for (std::uint8_t ch = 0; ch < channels; ++ch) {
            output[i * channels + ch] = out_buf[i];
        }
    }
}

// =====================================================================
// Application Coroutines (User priority task)
// =====================================================================

namespace app {

using namespace umi::coro;
using namespace umi_syscall;

const HwState* hw_state = nullptr;
const MidiShared* midi_buffer = nullptr;

// UI Coroutine: handles display updates
Task<void> ui_task(SchedulerContext<8>& ctx) {
    while (true) {
        co_await ctx.wait_for(event::VSync);
        // Update display
    }
}

// Parameter Coroutine: updates audio params from hardware knobs
Task<void> param_task(SchedulerContext<8>& ctx) {
    while (true) {
        co_await ctx.sleep(std::chrono::milliseconds(16));
        
        if (hw_state && g_synth) {
            std::uint16_t knob = hw_state->adc_values[0];
            float freq = 100.0f + static_cast<float>(knob) * 0.5f;
            g_synth->set_frequency(freq);
        }
    }
}

// MIDI Coroutine: processes incoming MIDI
Task<void> midi_task(SchedulerContext<8>& ctx) {
    while (true) {
        co_await ctx.wait_for(event::MidiReady);
        
        if (midi_buffer) {
            while (midi_buffer->head != midi_buffer->tail) {
                // Parse and queue MIDI events for sample-accurate processing
            }
        }
    }
}

// LED Blink Coroutine
Task<void> led_task(SchedulerContext<8>& ctx) {
    while (true) {
        co_await ctx.sleep(std::chrono::milliseconds(500));
        // Toggle LED
    }
}

} // namespace app

// =====================================================================
// Application Entry Point
// =====================================================================

extern "C" void umi_main(void*) {
    using namespace umi_syscall;
    using namespace umi::coro;
    
    // Initialize synth
    static SimpleSynth synth(48000);
    g_synth = &synth;
    
    // Get shared memory
    auto hw_region = get_shared(RegionId::HwState);
    auto midi_region = get_shared(RegionId::Midi);
    
    app::hw_state = static_cast<const HwState*>(hw_region.base);
    app::midi_buffer = static_cast<const MidiShared*>(midi_region.base);
    
    // Create scheduler
    Scheduler<8> sched(wait, get_time_usec);
    SchedulerContext<8> ctx(sched);
    
    // Spawn coroutines
    sched.spawn(app::ui_task(ctx));
    sched.spawn(app::param_task(ctx));
    sched.spawn(app::midi_task(ctx));
    sched.spawn(app::led_task(ctx));
    
    // Run scheduler (never returns)
    sched.run();
}

// =====================================================================
// UMI-OS Example Application
// =====================================================================
//
// Task-based design:
//   - Audio Task (Priority::Realtime): DMA ISR notifies, task processes
//   - UI/MIDI: Coroutines in User priority task
//
// =====================================================================

#include <cstdint>
#include <cmath>
#include <chrono>
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

/// Wait with timeout - returns fired event mask
/// timeout_us: microseconds to wait, UINT64_MAX for infinite
inline std::uint32_t wait(std::uint32_t mask, usec timeout_us) {
    std::uint32_t result = 0;
    (void)mask;
    (void)timeout_us;
    return result;
}

/// Get current monotonic time in microseconds
inline usec get_time_usec() {
    return 0;  // Stub - kernel provides real implementation
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
// Audio Processing
// =====================================================================
//
// In the real implementation:
//   - DMA ISR calls: audio_engine.on_dma_complete(in, out);
//                    kernel.notify(audio_task_id, Event::AudioReady);
//   - Audio task calls: audio_engine.process(kernel, midi_queue);
//
// =====================================================================

namespace audio {

float phase = 0.0f;
float frequency = 440.0f;
constexpr float two_pi = 6.283185307f;

void process_callback(float* input, float* output, std::size_t frames, 
                       std::uint8_t channels, std::uint32_t sample_rate) {
    (void)input;
    
    float phase_inc = frequency * two_pi / static_cast<float>(sample_rate);
    
    for (std::size_t i = 0; i < frames; ++i) {
        float sample = 0.3f * std::sin(phase);
        phase += phase_inc;
        if (phase >= two_pi) phase -= two_pi;
        
        std::size_t idx = i * channels;
        output[idx + 0] = sample;
        output[idx + 1] = sample;
    }
}

} // namespace audio

extern "C" {
using AudioCallback = void (*)(float*, float*, std::size_t, std::uint8_t, std::uint32_t);
AudioCallback umi_audio_callback = audio::process_callback;
}

// =====================================================================
// Application Coroutines (User priority task)
// =====================================================================

namespace app {

using namespace umi::coro;
using namespace umi::coro::literals;  // for ms, s literals
using namespace umi_syscall;

// Global state accessible from coroutines
const HwState* hw_state = nullptr;
const MidiShared* midi_buffer = nullptr;

// =====================================================================
// UI Coroutine: handles display updates
// =====================================================================

Task<void> ui_task(SchedulerContext<8>& ctx) {
    while (true) {
        // Wait for vsync
        co_await ctx.wait_for(event::VSync);
        
        // Update display (non-blocking)
        // In real app: draw to framebuffer
    }
}

// =====================================================================
// Parameter Coroutine: updates audio params from hardware
// =====================================================================

Task<void> param_task(SchedulerContext<8>& ctx) {
    while (true) {
        // Update at ~60Hz using sleep
        co_await ctx.sleep(std::chrono::milliseconds(16));
        
        // Read knob and update frequency
        if (hw_state) {
            std::uint16_t knob = hw_state->adc_values[0];
            audio::frequency = 100.0f + static_cast<float>(knob) * 0.5f;
        }
    }
}

// =====================================================================
// MIDI Coroutine: processes incoming MIDI messages
// =====================================================================

Task<void> midi_task(SchedulerContext<8>& ctx) {
    while (true) {
        // Wait for MIDI data
        co_await ctx.wait_for(event::MidiReady);
        
        // Process MIDI ring buffer
        if (midi_buffer) {
            while (midi_buffer->head != midi_buffer->tail) {
                // Parse MIDI bytes...
                // For CC: update parameters
                // For notes: handled in audio callback for sample-accuracy
            }
        }
    }
}

// =====================================================================
// LED Blink Coroutine: demonstrates sleep usage
// =====================================================================

Task<void> led_task(SchedulerContext<8>& ctx) {
    while (true) {
        // LED on
        // gpio::set(LED_PIN);
        
        co_await ctx.sleep(std::chrono::milliseconds(500));
        
        // LED off
        // gpio::clear(LED_PIN);
        
        co_await ctx.sleep(std::chrono::milliseconds(500));
    }
}

} // namespace app

// =====================================================================
// Application Entry Point
// =====================================================================
//
// Audio Task is created separately by the kernel at Priority::Realtime.
// This function runs as a User priority task with coroutines.
//
// =====================================================================

extern "C" void umi_main(void*) {
    using namespace umi_syscall;
    using namespace umi::coro;
    
    // Get shared memory
    auto hw_region = get_shared(RegionId::HwState);
    auto midi_region = get_shared(RegionId::Midi);
    
    app::hw_state = static_cast<const HwState*>(hw_region.base);
    app::midi_buffer = static_cast<const MidiShared*>(midi_region.base);
    
    // Create scheduler with wait function and time function
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

// SPDX-License-Identifier: MIT
// UMI-OS - Embedded MCU Adapter
//
// This adapter bridges the platform-independent AudioProcessor 
// with the MCU-specific kernel and hardware abstraction layer.

#pragma once

#include <umios/processor.hh>
#include <umios/audio_context.hh>
#include <umios/event.hh>
#include <umios/types.hh>
#include <umios/umi_kernel.hh>

#include <array>
#include <span>

namespace umi::embedded {

// ============================================================================
// Configuration
// ============================================================================

/// Embedded adapter configuration
struct AdapterConfig {
    uint32_t sample_rate = 48000;
    uint32_t buffer_size = 64;
    uint8_t num_inputs = 0;
    uint8_t num_outputs = 2;
    uint8_t num_midi_ports = 1;
};

// ============================================================================
// Embedded Adapter Template
// ============================================================================

/// Adapter that runs an AudioProcessor on an embedded MCU
/// 
/// Template Parameters:
/// - Proc: Type satisfying ProcessorLike concept
/// - Hw: Hardware abstraction layer type (from port/)
/// - Config: Compile-time configuration
///
/// Usage:
/// @code
/// struct MyHw { /* HAL implementation */ };
/// using Kernel = umi::Kernel<MyHw>;
/// 
/// MySynth synth;
/// umi::embedded::Adapter<MySynth, MyHw> adapter{synth};
/// adapter.run(kernel);  // Never returns
/// @endcode
template<ProcessorLike Proc, typename Hw, AdapterConfig Config = AdapterConfig{}>
class Adapter {
public:
    explicit Adapter(Proc& processor) noexcept
        : processor_(processor)
    {}
    
    // Non-copyable, non-movable (owns references)
    Adapter(const Adapter&) = delete;
    Adapter& operator=(const Adapter&) = delete;
    
    /// Run the audio processing loop (does not return)
    template<typename KernelType>
    [[noreturn]] void run(KernelType& kernel) {
        // Create audio task
        TaskConfig audio_cfg{
            .entry = [](void* arg) {
                auto* self = static_cast<Adapter*>(arg);
                self->audio_task_entry();
            },
            .arg = this,
            .prio = Priority::REALTIME,
            .name = "audio",
        };
        auto audio_tid = kernel.create_task(audio_cfg);
        (void)audio_tid;
        
        // Create MIDI task if needed
        if constexpr (Config.num_midi_ports > 0) {
            TaskConfig midi_cfg{
                .entry = [](void* arg) {
                    auto* self = static_cast<Adapter*>(arg);
                    self->midi_task_entry();
                },
                .arg = this,
                .prio = Priority::SERVER,
                .name = "midi",
            };
            auto midi_tid = kernel.create_task(midi_cfg);
            (void)midi_tid;
        }
        
        // Start kernel (never returns)
        kernel.start();
        
        // Unreachable
        while (true) {
            asm volatile("wfi");
        }
    }
    
    /// Get current audio load (0.0 - 1.0)
    [[nodiscard]] float audio_load() const noexcept {
        return audio_load_;
    }
    
    /// Get event queue for external MIDI input
    [[nodiscard]] EventQueue<>& events() noexcept {
        return events_;
    }
    
private:
    void audio_task_entry() {
        // Initialize buffers
        clear_buffers();
        
        while (true) {
            // Wait for audio ready event from DMA/I2S
            // (In real implementation, kernel.wait(KernelEvent::AudioReady))
            
            // Swap event buffers
            swap_event_buffers();
            
            // Build context (using std::span for type safety)
            AudioContext ctx{
                .inputs = std::span<const sample_t* const>(
                    const_cast<const sample_t**>(input_ptrs_.data()),
                    Config.num_inputs
                ),
                .outputs = std::span<sample_t* const>(output_ptrs_.data(), Config.num_outputs),
                .input_events = std::span<const Event>{},  // process_events_から変換
                .output_events = process_events_,
                .sample_rate = Config.sample_rate,
                .buffer_size = Config.buffer_size,
                .dt = 1.0f / static_cast<float>(Config.sample_rate),
                .sample_position = sample_position_
            };
            
            // Measure processing time
            auto start = cycle_count();
            
            // Process audio
            processor_.process(ctx);
            
            // Update load monitor
            auto elapsed = cycle_count() - start;
            update_load(elapsed);
            
            // Advance sample position
            sample_position_ += Config.buffer_size;
            
            // Clear processed events
            process_events_.clear();
        }
    }
    
    void midi_task_entry() {
        while (true) {
            // Wait for MIDI ready event
            // (In real implementation, kernel.wait(KernelEvent::MidiReady))
            
            // Process incoming MIDI from hardware UART/USB
            // and push to event queue with sample-accurate timestamps
        }
    }
    
    void clear_buffers() {
        for (auto& buf : output_buffers_) {
            for (auto& s : buf) {
                s = 0.0f;
            }
        }
        for (auto& buf : input_buffers_) {
            for (auto& s : buf) {
                s = 0.0f;
            }
        }
        
        // Setup pointers
        for (size_t i = 0; i < Config.num_outputs; ++i) {
            output_ptrs_[i] = output_buffers_[i].data();
        }
        for (size_t i = 0; i < Config.num_inputs; ++i) {
            input_ptrs_[i] = input_buffers_[i].data();
        }
    }
    
    void swap_event_buffers() {
        // Move pending events to processing queue
        Event ev;
        while (events_.pop(ev)) {
            (void)process_events_.push(ev);
        }
    }
    
    uint32_t cycle_count() const {
        // Read DWT cycle counter (Cortex-M specific)
        // In real implementation: return Hw::cycle_count();
        return 0;
    }
    
    void update_load(uint32_t cycles) {
        // Calculate load as percentage of available cycles
        // cycles_per_buffer = sample_rate * cycles_per_sample / buffers_per_sec
        constexpr uint32_t cpu_freq = 168'000'000;  // 168 MHz typical for STM32F4
        constexpr uint32_t cycles_per_buffer = 
            cpu_freq / (Config.sample_rate / Config.buffer_size);
        
        float load = static_cast<float>(cycles) / static_cast<float>(cycles_per_buffer);
        
        // Exponential moving average
        audio_load_ = audio_load_ * 0.9f + load * 0.1f;
    }
    
private:
    Proc& processor_;
    
    // Audio buffers
    static constexpr size_t kMaxChannels = 8;
    std::array<std::array<sample_t, Config.buffer_size>, kMaxChannels> output_buffers_{};
    std::array<std::array<sample_t, Config.buffer_size>, kMaxChannels> input_buffers_{};
    std::array<sample_t*, kMaxChannels> output_ptrs_{};
    std::array<sample_t*, kMaxChannels> input_ptrs_{};
    
    // Event queues (double buffer)
    EventQueue<> events_{};         // For external MIDI input
    EventQueue<> process_events_{}; // For processing
    
    // State
    sample_position_t sample_position_ = 0;
    float audio_load_ = 0.0f;
};

// ============================================================================
// Convenience function for simple apps
// ============================================================================

/// Run an audio processor on the embedded platform
/// 
/// This is the simplest way to run an audio app on MCU.
/// Creates the adapter, then starts processing with the provided kernel.
///
/// @param processor  The audio processor to run (must satisfy ProcessorLike)
/// @param kernel     The kernel instance to use
///
/// Usage:
/// @code
/// MySynth synth;
/// MyKernel kernel;
/// umi::embedded::run(synth, kernel);  // Never returns
/// @endcode
template<ProcessorLike Proc, typename Hw, typename KernelType>
[[noreturn]] void run(Proc& processor, KernelType& kernel) {
    Adapter<Proc, Hw> adapter{processor};
    adapter.run(kernel);
}

} // namespace umi::embedded

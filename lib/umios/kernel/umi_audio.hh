#pragma once
#include "umi_kernel.hh"
#include "umi_midi.hh"
#include <cmath>

// UMI Audio Subsystem
// Hardware-independent audio engine with real-time guarantees.
// C++23, header-only.

namespace umi::audio {

// =====================================
// Audio Configuration
// =====================================

struct AudioConfig {
    std::uint32_t sample_rate {48000};      // Hz
    std::uint16_t buffer_size {128};        // frames per buffer (callback period)
    std::uint8_t input_channels {0};        // 0 = no input
    std::uint8_t output_channels {2};       // stereo default
    std::uint8_t max_drop_count {3};        // consecutive drops before panic
    float silence_threshold {1e-6f};        // for auto-standby detection
    std::uint32_t silence_frames {48000};   // frames of silence before standby (1 sec default)
};

// =====================================
// Audio Buffer View
// =====================================

// Non-owning view of audio buffer (interleaved or per-channel)
template <typename SampleT = float>
struct AudioBuffer {
    SampleT* data {nullptr};
    std::size_t frames {0};
    std::uint8_t channels {0};
    
    // Access sample at frame/channel (interleaved layout)
    SampleT& at(std::size_t frame, std::uint8_t channel) {
        return data[frame * channels + channel];
    }
    
    const SampleT& at(std::size_t frame, std::uint8_t channel) const {
        return data[frame * channels + channel];
    }
    
    // Get span for a single channel (requires de-interleaving or non-interleaved layout)
    std::span<SampleT> channel_span(std::uint8_t ch) {
        // For interleaved: user must handle striding
        // For non-interleaved: return contiguous block
        return std::span<SampleT>(data + ch * frames, frames);
    }
    
    // Total samples in buffer
    std::size_t total_samples() const { return frames * channels; }
    
    // Clear buffer to zero
    void clear() {
        for (std::size_t i = 0; i < total_samples(); ++i) {
            data[i] = SampleT{0};
        }
    }
    
    // Check if buffer is silent (below threshold)
    bool is_silent(float threshold) const {
        for (std::size_t i = 0; i < total_samples(); ++i) {
            if (std::abs(static_cast<float>(data[i])) > threshold) {
                return false;
            }
        }
        return true;
    }
};

// =====================================
// Audio IO Hooks (Hardware Dependent)
// =====================================

// Template parameter for hardware-specific audio I/O
// Platform must implement:
//   static void start_dma();
//   static void stop_dma();
//   static void mute_output();
//   static void unmute_output();
//   static bool is_dma_running();

// =====================================
// Audio Callback Signature
// =====================================

// User-provided audio processing callback
// Called from ISR context - must be wait-free!
// All inputs (including parameters) come through MIDI events.
template <typename SampleT = float>
using AudioCallback = void (*)(  
    const AudioBuffer<SampleT>& input,
    AudioBuffer<SampleT>& output,
    std::span<const midi::Event> events
);// =====================================
// Engine State
// =====================================

enum class EngineState : std::uint8_t {
    STOPPED,
    RUNNING,
    SUSPENDED,
    STANDBY,    // Auto-suspended due to silence
};

// =====================================
// Audio Engine
// =====================================

template <class HW, class AudioIO, typename SampleT = float>
class AudioEngine {
public:
    using Buffer = AudioBuffer<SampleT>;
    using Callback = AudioCallback<SampleT>;

    explicit AudioEngine(const AudioConfig& cfg = {})
        : config_(cfg)
        , load_monitor_()
    {
        update_budget_cycles();
    }

    // =====================================
    // Configuration
    // =====================================
    
    const AudioConfig& config() const { return config_; }
    
    // Change sample rate (must be stopped)
    bool set_sample_rate(std::uint32_t rate) {
        if (state_ != EngineState::STOPPED) return false;
        config_.sample_rate = rate;
        update_budget_cycles();
        return true;
    }
    
    // Change buffer size (must be stopped)
    bool set_buffer_size(std::uint16_t size) {
        if (state_ != EngineState::STOPPED) return false;
        config_.buffer_size = size;
        update_budget_cycles();
        return true;
    }

    // =====================================
    // Lifecycle Management
    // =====================================
    
    void start() {
        if (state_ == EngineState::RUNNING) return;
        consecutive_drops_ = 0;
        frame_count_ = 0;
        silence_counter_ = 0;
        processing_ = false;
        AudioIO::unmute_output();
        AudioIO::start_dma();
        state_ = EngineState::RUNNING;
    }
    
    void stop() {
        AudioIO::stop_dma();
        AudioIO::mute_output();
        state_ = EngineState::STOPPED;
    }
    
    void suspend() {
        if (state_ != EngineState::RUNNING) return;
        AudioIO::mute_output();
        state_ = EngineState::SUSPENDED;
    }
    
    void resume() {
        if (state_ != EngineState::SUSPENDED && state_ != EngineState::STANDBY) return;
        silence_counter_ = 0;
        AudioIO::unmute_output();
        state_ = EngineState::RUNNING;
    }
    
    EngineState state() const { return state_; }
    bool is_running() const { return state_ == EngineState::RUNNING; }

    // =====================================
    // Callback Registration
    // =====================================
    
    void set_callback(Callback cb) {
        callback_ = cb;
    }

    // =====================================
    // Buffer Management for Task-Based Processing
    // =====================================
    //
    // Design: ISR only notifies, processing happens in a Realtime task
    //
    //   DMA ISR:
    //     audio_engine.on_dma_complete(input, output);
    //     kernel.notify(audio_task_id, Event::AudioReady);
    //
    //   Audio Task (Priority::Realtime):
    //     while (true) {
    //         kernel.wait_block(Event::AudioReady);
    //         audio_engine.process(midi_queue);
    //     }
    //
    // =====================================
    
    /// Call from DMA ISR: saves buffer pointers, does NOT process
    void on_dma_complete(SampleT* input_buffer, SampleT* output_buffer) {
        pending_input_ = input_buffer;
        pending_output_ = output_buffer;
        buffer_pending_.store(true, std::memory_order_release);
    }
    
    /// Check if buffer is pending (for task to poll if needed)
    bool has_pending_buffer() const {
        return buffer_pending_.load(std::memory_order_acquire);
    }
    
    /// Call from Audio Task: processes pending buffer
    /// Returns true if buffer was processed, false if no pending buffer
    template <class Kernel, std::size_t QueueCapacity, std::size_t MaxEventsPerBuffer = 32>
    bool process(Kernel& kernel, midi::EventQueue<QueueCapacity>& midi_queue) {
        if (!buffer_pending_.load(std::memory_order_acquire)) {
            return false;
        }
        
        // Read events from lock-free queue into local buffer
        midi::EventReader<MaxEventsPerBuffer> reader;
        reader.read_from(midi_queue);
        
        process_buffer_impl(kernel, pending_input_, pending_output_, reader.all());
        
        buffer_pending_.store(false, std::memory_order_release);
        return true;
    }
    
    /// Simplified version without MIDI
    template <class Kernel>
    bool process(Kernel& kernel) {
        if (!buffer_pending_.load(std::memory_order_acquire)) {
            return false;
        }
        
        process_buffer_impl(kernel, pending_input_, pending_output_, std::span<const midi::Event>{});
        
        buffer_pending_.store(false, std::memory_order_release);
        return true;
    }

    // =====================================
    // Legacy: Direct ISR Processing (not recommended)
    // =====================================
    // Use only if you need absolute minimum latency and understand the tradeoffs.
    // This blocks other interrupts during audio processing.
    
    /// [DEPRECATED] Process directly in ISR - use on_dma_complete + process() instead
    template <class Kernel, std::size_t QueueCapacity, std::size_t MaxEventsPerBuffer = 32>
    [[deprecated("Use on_dma_complete() + process() pattern instead")]]
    void on_buffer_complete(
        Kernel& kernel,
        SampleT* input_buffer,
        SampleT* output_buffer,
        midi::EventQueue<QueueCapacity>& midi_queue
    ) {
        // Read events from lock-free queue into local buffer
        midi::EventReader<MaxEventsPerBuffer> reader;
        reader.read_from(midi_queue);
        
        process_buffer_impl(kernel, input_buffer, output_buffer, reader.all());
    }

    /// [DEPRECATED] Process directly in ISR with EventBuffer
    template <class Kernel, std::size_t MidiCapacity>
    [[deprecated("Use on_dma_complete() + process() pattern instead")]]
    void on_buffer_complete(
        Kernel& kernel,
        SampleT* input_buffer,
        SampleT* output_buffer,
        midi::EventBuffer<MidiCapacity>& midi_events
    ) {
        auto events = midi_events.events();
        process_buffer_impl(kernel, input_buffer, output_buffer, events);
        midi_events.clear();
    }

    // =====================================
    // Sample-Accurate Clock
    // =====================================
    
    // Total frames processed since start
    std::uint64_t frame_count() const { return frame_count_; }
    
    // Time in seconds since start
    double time_seconds() const {
        return static_cast<double>(frame_count_) / static_cast<double>(config_.sample_rate);
    }
    
    // Time in samples (alias for frame_count)
    std::uint64_t time_samples() const { return frame_count_; }

    // =====================================
    // DSP Load Monitor
    // =====================================
    
    // Instant load (0-10000 = 0.00%-100.00%)
    std::uint32_t load_instant() const { return load_monitor_.instant(); }
    
    // Moving average load
    std::uint32_t load_average() const { return load_monitor_.average(); }
    
    // Peak load since reset
    std::uint32_t load_peak() const { return load_monitor_.peak(); }
    
    // Reset peak load
    void reset_peak_load() { load_monitor_.reset_peak(); }
    
    // Convert to percentage float
    static float load_to_percent(std::uint32_t load) {
        return LoadMonitor<HW>::to_percent(load);
    }

    // =====================================
    // Statistics
    // =====================================
    
    std::uint32_t total_drops() const { return total_drops_; }
    std::uint32_t consecutive_drops() const { return consecutive_drops_; }

private:
    AudioConfig config_;
    EngineState state_ {EngineState::STOPPED};
    
    Callback callback_ {nullptr};
    
    // Task-based processing: buffer pointers set by ISR, processed by task
    SampleT* pending_input_ {nullptr};
    SampleT* pending_output_ {nullptr};
    std::atomic<bool> buffer_pending_ {false};
    
    // Processing state - atomic for multi-core safety (weak memory model)
    std::atomic<bool> processing_ {false};
    std::uint8_t consecutive_drops_ {0};
    std::uint32_t total_drops_ {0};
    
    // Sample-accurate clock
    std::uint64_t frame_count_ {0};
    
    // Auto-standby
    std::uint32_t silence_counter_ {0};
    
    // Load monitoring
    LoadMonitor<HW, 8> load_monitor_;
    std::uint32_t budget_cycles_ {0};
    
    void update_budget_cycles() {
        // Budget = (buffer_size / sample_rate) * cpu_cycles_per_second
        // = buffer_size * cycles_per_usec * 1000000 / sample_rate
        std::uint64_t usecs_per_buffer = 
            static_cast<std::uint64_t>(config_.buffer_size) * 1000000ULL / config_.sample_rate;
        budget_cycles_ = static_cast<std::uint32_t>(usecs_per_buffer * HW::cycles_per_usec());
    }
    
    void clear_output(SampleT* buffer) {
        std::size_t total = config_.buffer_size * config_.output_channels;
        for (std::size_t i = 0; i < total; ++i) {
            buffer[i] = SampleT{0};
        }
    }
    
    void enter_standby() {
        AudioIO::mute_output();
        state_ = EngineState::STANDBY;
    }
    
    // Common buffer processing logic (called by both on_buffer_complete overloads)
    template <class Kernel>
    void process_buffer_impl(
        Kernel& kernel,
        SampleT* input_buffer,
        SampleT* output_buffer,
        std::span<const midi::Event> events
    ) {
        // Check-Busy-at-Interrupt: skip if previous callback still running
        if (processing_.load(std::memory_order_acquire)) {
            ++consecutive_drops_;
            ++total_drops_;
            
            // Continuous Drop Watchdog
            if (consecutive_drops_ >= config_.max_drop_count) {
                CrashDump dump{};
                dump.reason = "Audio overload: too many consecutive drops";
                kernel.panic(dump);
            }
            
            clear_output(output_buffer);
            return;
        }
        
        processing_.store(true, std::memory_order_release);
        consecutive_drops_ = 0;
        
        // Cache coherency for DMA buffers (Cortex-M7, Cortex-A)
        // Invalidate input buffer so CPU sees DMA-written data
        if (input_buffer && config_.input_channels > 0) {
            std::size_t input_bytes = config_.buffer_size * config_.input_channels * sizeof(SampleT);
            HW::cache_invalidate(input_buffer, input_bytes);
        }
        
        load_monitor_.begin();
        
        Buffer input{input_buffer, config_.buffer_size, config_.input_channels};
        Buffer output{output_buffer, config_.buffer_size, config_.output_channels};
        output.clear();
        
        if (callback_) {
            callback_(input, output, events);
        }
        
        load_monitor_.end(budget_cycles_);
        frame_count_ += config_.buffer_size;
        
        // Clean output buffer so DMA sees CPU-written data
        if (output_buffer && config_.output_channels > 0) {
            std::size_t output_bytes = config_.buffer_size * config_.output_channels * sizeof(SampleT);
            HW::cache_clean(output_buffer, output_bytes);
        }
        
        // Auto-standby detection
        if (state_ == EngineState::RUNNING) {
            if (output.is_silent(config_.silence_threshold)) {
                silence_counter_ += config_.buffer_size;
                if (silence_counter_ >= config_.silence_frames) {
                    enter_standby();
                }
            } else {
                silence_counter_ = 0;
            }
        }
        
        processing_.store(false, std::memory_order_release);
    }
};

// =====================================
// Convenience Types
// =====================================

// Stereo buffer pair for common use case
template <std::size_t MaxFrames, typename SampleT = float>
struct StereoBufferStorage {
    std::array<SampleT, MaxFrames * 2> input {};
    std::array<SampleT, MaxFrames * 2> output {};
    
    SampleT* input_ptr() { return input.data(); }
    SampleT* output_ptr() { return output.data(); }
};

// =====================================
// Audio Utilities
// =====================================

// Convert samples to time
inline constexpr double samples_to_seconds(std::uint64_t samples, std::uint32_t sample_rate) {
    return static_cast<double>(samples) / static_cast<double>(sample_rate);
}

// Convert time to samples
inline constexpr std::uint64_t seconds_to_samples(double seconds, std::uint32_t sample_rate) {
    return static_cast<std::uint64_t>(seconds * static_cast<double>(sample_rate));
}

// Convert BPM to samples per beat
inline constexpr double bpm_to_samples_per_beat(double bpm, std::uint32_t sample_rate) {
    return (60.0 / bpm) * static_cast<double>(sample_rate);
}

// Linear to dB conversion
inline float linear_to_db(float linear) {
    return (linear > 0.0f) ? 20.0f * std::log10(linear) : -144.0f;
}

// dB to linear conversion
inline float db_to_linear(float db) {
    return std::pow(10.0f, db / 20.0f);
}

} // namespace umi::audio

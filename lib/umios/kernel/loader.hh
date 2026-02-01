// SPDX-License-Identifier: MIT
// UMI-OS Application Loader
// Validates and loads .umia binaries into protected memory regions

#pragma once

#include "app_header.hh"
#include "../core/audio_context.hh"
#include "../core/event.hh"
#include "../core/shared_state.hh"
#include <cstdint>
#include <cstddef>
#include <span>
#include <atomic>

namespace umi::kernel {

// ============================================================================
// Forward Declarations
// ============================================================================

struct SharedMemory;

// ============================================================================
// Application Runtime State
// ============================================================================

/// Application execution state
enum class AppState : uint8_t {
    NONE = 0,       ///< No application loaded
    LOADED,         ///< Application loaded, not yet started
    RUNNING,        ///< Application running
    SUSPENDED,      ///< Application suspended
    TERMINATED,     ///< Application terminated
};

/// Processor function signature (called from audio ISR)
using ProcessFn = void (*)(void* processor, AudioContext& ctx);

/// Application runtime information
struct AppRuntime {
    AppState state = AppState::NONE;
    
    // Memory layout
    void* base = nullptr;           ///< Application base address
    void* text_start = nullptr;     ///< .text section start
    void* data_start = nullptr;     ///< .data/.bss section start
    void* stack_base = nullptr;     ///< Stack base (grows down)
    void* stack_top = nullptr;      ///< Stack top (initial SP)
    
    // Entry points
    void (*entry)() = nullptr;      ///< _start() entry point
    
    // Processor registration (set by register_processor syscall)
    void* processor = nullptr;      ///< Processor instance pointer
    ProcessFn process_fn = nullptr; ///< process() function pointer
    
    // Exit code
    int exit_code = 0;
    
    /// Check if processor is registered
    [[nodiscard]] bool has_processor() const noexcept {
        return processor != nullptr && process_fn != nullptr;
    }
    
    /// Clear all state
    void clear() noexcept {
        state = AppState::NONE;
        base = nullptr;
        text_start = nullptr;
        data_start = nullptr;
        stack_base = nullptr;
        stack_top = nullptr;
        entry = nullptr;
        processor = nullptr;
        process_fn = nullptr;
        exit_code = 0;
    }
};

// ============================================================================
// Application Loader
// ============================================================================

/// Application loader - validates and loads .umia binaries
/// 
/// Usage:
/// @code
/// AppLoader loader;
/// loader.set_app_memory(app_ram_base, app_ram_size);
/// 
/// auto result = loader.load(app_image, app_size);
/// if (result != LoadResult::OK) {
///     // Handle error
/// }
/// 
/// // Start control task
/// loader.start();
/// 
/// // In audio ISR:
/// loader.call_process(audio_ctx);
/// @endcode
class AppLoader {
public:
    // --- Configuration ---
    
    /// Set memory region for application RAM
    void set_app_memory(void* base, size_t size) noexcept {
        app_ram_base_ = base;
        app_ram_size_ = size;
    }
    
    /// Set shared memory region (for audio buffers, events, etc.)
    void set_shared_memory(SharedMemory* shared) noexcept {
        shared_ = shared;
    }
    
    // --- Loading ---
    
    /// Load application from image
    /// @param image Pointer to .umia binary
    /// @param size Size of image in bytes
    /// @return LoadResult::OK on success, error code otherwise
    LoadResult load(const uint8_t* image, size_t size) noexcept;
    
    /// Unload current application
    void unload() noexcept;
    
    // --- Execution ---
    
    /// Start the control task (calls _start() in unprivileged mode)
    /// @return true if started successfully
    bool start() noexcept;
    
    /// Call the application entry point (_start -> main)
    /// This runs the app's initialization code synchronously
    /// @return true if entry was called, false if not ready
    bool call_entry() noexcept {
        if (runtime_.state != AppState::RUNNING || runtime_.entry == nullptr) {
            return false;
        }
        runtime_.entry();
        return true;
    }
    
    /// Terminate the application
    /// @param exit_code Exit code to record
    void terminate(int exit_code) noexcept;
    
    /// Suspend the application
    void suspend() noexcept;
    
    /// Resume the application
    void resume() noexcept;
    
    // --- Processor Interface ---
    
    /// Register processor (called from syscall handler)
    /// @param processor Pointer to processor instance
    /// @param process_fn Pointer to process() function
    /// @return 0 on success, -1 on error
    int register_processor(void* processor, ProcessFn process_fn) noexcept;
    
    /// Simple processor registration with function pointer only
    /// @param fn Simple process function: void (*)(float* out, const float* in, size_t frames)
    /// @return 0 on success, -1 on error
    int register_processor(void* fn) noexcept {
        // Cast to simple function pointer and wrap
        using SimpleFn = void (*)(float*, const float*, uint32_t, float);
        simple_process_fn_ = reinterpret_cast<SimpleFn>(fn);
        return 0;
    }
    
    /// Call processor's process() function (from audio ISR)
    /// @param ctx Audio context
    /// @note This is called from ISR context, must be fast and non-blocking
    void call_process(AudioContext& ctx) noexcept {
        if (runtime_.has_processor() && runtime_.state == AppState::RUNNING) {
            runtime_.process_fn(runtime_.processor, ctx);
        }
    }
    
    /// Simple call_process for minimal kernel
    /// @param output Output buffer (stereo interleaved)
    /// @param input Input buffer (stereo interleaved)
    /// @param sample_pos Current sample position
    /// @param frames Number of frames
    /// @param dt Time delta per sample
    void call_process(std::span<float> output, std::span<const float> input, 
                     uint64_t sample_pos, uint32_t frames, float dt) noexcept {
        (void)sample_pos;
        if (simple_process_fn_ != nullptr && runtime_.state == AppState::RUNNING) {
            simple_process_fn_(output.data(), input.data(), frames, dt);
        }
    }
    
    // --- State Queries ---
    
    /// Get current application state
    [[nodiscard]] AppState state() const noexcept { return runtime_.state; }
    
    /// Check if an application is loaded
    [[nodiscard]] bool is_loaded() const noexcept { 
        return runtime_.state != AppState::NONE; 
    }
    
    /// Check if processor is available
    [[nodiscard]] bool has_processor() const noexcept {
        return runtime_.has_processor();
    }
    
    /// Get application exit code
    [[nodiscard]] int exit_code() const noexcept { return runtime_.exit_code; }
    
    /// Get runtime info (for debugging)
    [[nodiscard]] const AppRuntime& runtime() const noexcept { return runtime_; }
    
    /// Set entry point and mark as running (for direct XIP execution)
    void set_entry(void (*entry)()) noexcept {
        runtime_.entry = entry;
        runtime_.state = AppState::RUNNING;
    }

private:
    // --- Internal Methods ---
    
    /// Validate application header
    LoadResult validate_header(const AppHeader* header, size_t image_size) noexcept;
    
    /// Verify CRC32 checksum
    bool verify_crc(const AppHeader* header, const uint8_t* image) noexcept;
    
    /// Verify Ed25519 signature (for Release apps)
    bool verify_signature(const AppHeader* header, const uint8_t* image) noexcept;
    
    /// Setup memory layout for application
    bool setup_memory(const AppHeader* header) noexcept;
    
    /// Copy sections from image to RAM
    void copy_sections(const AppHeader* header, const uint8_t* image) noexcept;
    
    /// Configure MPU for application isolation
    void configure_mpu() noexcept;
    
    // --- State ---
    
    AppRuntime runtime_;
    const AppHeader* loaded_header_ = nullptr;
    
    // Memory configuration
    void* app_ram_base_ = nullptr;
    size_t app_ram_size_ = 0;
    SharedMemory* shared_ = nullptr;
    
    // Simple process function (for minimal kernel)
    using SimpleFn = void (*)(float*, const float*, uint32_t, float);
    SimpleFn simple_process_fn_ = nullptr;
};

// ============================================================================
// Shared Memory Layout
// ============================================================================

/// Shared memory between kernel and application
/// Accessible by both privileged and unprivileged code
struct SharedMemory {
    // Audio buffers (written by kernel, read by app in process())
    // App writes output here
    static constexpr size_t AUDIO_BUFFER_FRAMES = 256;
    static constexpr size_t AUDIO_CHANNELS = 2;
    
    // USB Audio OUT -> app input (host playback)
    float audio_input[AUDIO_BUFFER_FRAMES * AUDIO_CHANNELS];
    // App synth output -> USB Audio IN Right channel
    float audio_output[AUDIO_BUFFER_FRAMES * AUDIO_CHANNELS];
    // PDM Mic -> USB Audio IN Left channel
    float mic_input[AUDIO_BUFFER_FRAMES];
    
    // Audio context info
    uint32_t sample_rate = 48000;
    uint32_t buffer_size = AUDIO_BUFFER_FRAMES;
    float dt = 1.0f / 48000.0f;  ///< Delta time per sample (1.0 / sample_rate), pre-calculated
    uint64_t sample_position = 0;
    
    /// Update sample rate and recalculate dt
    void set_sample_rate(uint32_t rate) noexcept {
        sample_rate = rate;
        dt = 1.0f / static_cast<float>(rate);
    }
    
    // Event queue (kernel -> app)
    static constexpr size_t EVENT_QUEUE_SIZE = 64;
    Event event_queue[EVENT_QUEUE_SIZE];
    std::atomic<uint32_t> event_write_idx{0};
    std::atomic<uint32_t> event_read_idx{0};
    
    // Parameter state (written by EventRouter, read by Processor via AudioContext)
    SharedParamState params;

    // MIDI channel state (written by EventRouter)
    SharedChannelState channel;

    // Hardware input state (written by kernel drivers)
    SharedInputState input;

    // Flags
    std::atomic<uint32_t> flags{0};

    // LED state (app → kernel)
    std::atomic<uint8_t> led_state{0};        ///< LED state bitmap (bit0-3 = LED0-3)
    uint8_t _pad_io[3]{};                     ///< Padding for alignment

    // Application heap memory region
    // Set by kernel before starting app
    void* heap_base = nullptr;    ///< Heap start address  // NOLINT(misc-non-private-member-variables-in-classes)
    size_t heap_size = 0;         ///< Heap size in bytes  // NOLINT(misc-non-private-member-variables-in-classes)

    /// Push event to queue (kernel side)
    bool push_event(const Event& ev) noexcept {
        uint32_t write = event_write_idx.load(std::memory_order_relaxed);
        uint32_t read = event_read_idx.load(std::memory_order_acquire);
        
        if (((write + 1) % EVENT_QUEUE_SIZE) == read) {
            return false;  // Full
        }
        
        event_queue[write] = ev;
        event_write_idx.store((write + 1) % EVENT_QUEUE_SIZE, std::memory_order_release);
        return true;
    }
    
    /// Pop event from queue (app side via syscall)
    bool pop_event(Event& ev) noexcept {
        uint32_t read = event_read_idx.load(std::memory_order_relaxed);
        uint32_t write = event_write_idx.load(std::memory_order_acquire);
        
        if (read == write) {
            return false;  // Empty
        }
        
        ev = event_queue[read];
        event_read_idx.store((read + 1) % EVENT_QUEUE_SIZE, std::memory_order_release);
        return true;
    }
};

// SharedMemory must fit within the SHARED region (16KB)
// See: docs/umios-architecture/07-memory.md
static_assert(sizeof(SharedMemory) <= 16 * 1024,
              "SharedMemory exceeds 16KB SHARED region");

// ============================================================================
// CRC32 Utility
// ============================================================================

/// Calculate CRC32 (IEEE 802.3 polynomial)
inline uint32_t crc32(const uint8_t* data, size_t len) noexcept {
    uint32_t crc = 0xFFFFFFFF;
    
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    
    return ~crc;
}

} // namespace umi::kernel

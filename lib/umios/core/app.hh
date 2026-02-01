// SPDX-License-Identifier: MIT
// UMI-OS - Universal Musical Instruments Operating System
// Unified Application API (UMIP/UMIC compliant)

#pragma once

#include "types.hh"
#include "event.hh"
#include "audio_context.hh"
#include "processor.hh"
#include <cstdint>

namespace umi {

// ============================================================================
// Application Event Types
// ============================================================================

/// Application event type (Control Task events)
enum class AppEventType : uint8_t {
    NONE = 0,
    SHUTDOWN,           ///< Application should terminate
    MIDI_NOTE_ON,
    MIDI_NOTE_OFF,
    MIDI_CC,
    MIDI_PITCH_BEND,
    PARAM_CHANGE,
    ENCODER_ROTATE,
    BUTTON_PRESS,
    BUTTON_RELEASE,
    DISPLAY_UPDATE,
    METER,
};

/// Application event (for Control Task)
struct AppEvent {
    AppEventType type = AppEventType::NONE;

    union {
        struct {
            uint8_t channel;
            uint8_t note;
            uint8_t velocity;
        } midi;

        struct {
            param_id_t id;
            float value;
        } param;

        struct {
            int id;
            int delta;
        } encoder;

        struct {
            int id;
        } button;

        struct {
            int id;
            float value;
        } meter;
    };

    AppEvent() noexcept : type(AppEventType::NONE) {}

    /// Check if event indicates shutdown
    [[nodiscard]] bool is_shutdown() const noexcept {
        return type == AppEventType::SHUTDOWN;
    }
};

// ============================================================================
// Processor Registration
// ============================================================================

namespace detail {
    // Type-erased processor storage
    struct ProcessorHolder {
        void* processor = nullptr;
        void (*process_fn)(void*, AudioContext&) = nullptr;

        void process(AudioContext& ctx) {
            if (process_fn && processor) {
                process_fn(processor, ctx);
            }
        }
    };

    // Global processor holder (set by register_processor)
    inline ProcessorHolder g_processor_holder;
}

/// Register a processor with the kernel
/// The processor must satisfy ProcessorLike concept (have process(AudioContext&) method)
template<ProcessorLike P>
void register_processor(P& processor) {
    detail::g_processor_holder.processor = &processor;
    detail::g_processor_holder.process_fn = [](void* p, AudioContext& ctx) {
        static_cast<P*>(p)->process(ctx);
    };
}

/// Get registered processor holder (for kernel use)
inline detail::ProcessorHolder& get_processor_holder() {
    return detail::g_processor_holder;
}

// ============================================================================
// Control Task API (Platform-specific implementations)
// ============================================================================

/// Wait for next application event (blocking)
/// Platform must implement this:
/// - Embedded: syscall → kernel block
/// - WASM: Asyncify yield
/// Returns event or shutdown event
AppEvent wait_event();

/// Send event to kernel/host
void send_event(const AppEvent& event);

/// Log message (platform-independent)
void log(const char* message);

/// Get current monotonic time in microseconds
uint64_t get_time_us();

// ============================================================================
// Shared Memory Access
// ============================================================================

/// Shared memory region between Processor Task and Control Task
struct SharedRegion {
    // Input values (normalized 0.0-1.0)
    static constexpr size_t MAX_INPUTS = 32;
    float inputs[MAX_INPUTS] = {};
    bool input_changed[MAX_INPUTS] = {};

    // Output values (normalized 0.0-1.0)
    static constexpr size_t MAX_OUTPUTS = 32;
    float outputs[MAX_OUTPUTS] = {};
    bool output_dirty[MAX_OUTPUTS] = {};

    // Input count / output count (set by BSP)
    uint8_t input_count = 0;
    uint8_t output_count = 0;
};

/// Get shared memory region
/// Platform must provide this
SharedRegion& get_shared();

}  // namespace umi

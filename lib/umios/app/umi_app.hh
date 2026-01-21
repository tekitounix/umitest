// SPDX-License-Identifier: MIT
// UMI-OS Application API
// High-level API for UMI applications

#pragma once

#include "syscall.hh"
#include "../core/event.hh"
#include "../core/audio_context.hh"
#include "../core/processor.hh"
#include <cstdint>
#include <atomic>

namespace umi {

// ============================================================================
// Processor Registration
// ============================================================================

namespace detail {

/// Type-erased process function
using ProcessFnPtr = void (*)(void*, AudioContext&);

/// Internal: Register processor with kernel
inline int32_t register_processor_impl(void* processor, ProcessFnPtr fn) noexcept {
    return syscall::call(
        syscall::nr::RegisterProc,
        reinterpret_cast<uint32_t>(processor),
        reinterpret_cast<uint32_t>(fn)
    );
}

} // namespace detail

/// Register a processor with the kernel
/// 
/// The processor must have a `process(AudioContext&)` method.
/// This registers the processor to be called from the audio ISR.
/// 
/// @tparam P Processor type (must satisfy ProcessorLike concept)
/// @param processor Reference to processor instance
/// @return 0 on success, negative on error
/// 
/// Example:
/// @code
/// class MySynth {
/// public:
///     void process(umi::AudioContext& ctx) {
///         // Generate audio
///     }
/// };
/// 
/// int main() {
///     static MySynth synth;
///     umi::register_processor(synth);
///     // ...
/// }
/// @endcode
template<typename P>
    requires requires(P& p, AudioContext& ctx) { p.process(ctx); }
int register_processor(P& processor) noexcept {
    auto fn = [](void* p, AudioContext& ctx) {
        static_cast<P*>(p)->process(ctx);
    };
    return detail::register_processor_impl(&processor, fn);
}

/// Register a simple process function with the kernel
/// 
/// The function signature is: void (*)(float* out, const float* in, uint32_t frames, float dt)
/// 
/// @param fn Process function pointer
/// @return 0 on success, negative on error
using SimpleProcessFn = void (*)(float*, const float*, uint32_t, float);

inline int register_processor(SimpleProcessFn fn) noexcept {
    return syscall::call(
        syscall::nr::RegisterProc,
        reinterpret_cast<uint32_t>(fn),
        0
    );
}

// ============================================================================
// Event Handling
// ============================================================================

/// Wait for an event (blocking)
/// 
/// This syscall blocks until an event is available from the kernel.
/// Events include MIDI messages, UI input, timer events, etc.
/// 
/// @return The received event
/// 
/// Example:
/// @code
/// while (true) {
///     auto ev = umi::wait_event();
///     if (ev.is_shutdown()) break;
///     handle_event(ev);
/// }
/// @endcode
inline Event wait_event() noexcept {
    Event ev;
    syscall::call(syscall::nr::WaitEvent, reinterpret_cast<uint32_t>(&ev));
    return ev;
}

/// Check for an event (non-blocking)
/// 
/// @param out_event Pointer to receive the event
/// @return true if an event was available, false otherwise
inline bool peek_event(Event* out_event) noexcept {
    return syscall::call(syscall::nr::PeekEvent,
                        reinterpret_cast<uint32_t>(out_event)) != 0;
}

/// Send an event to the kernel
/// 
/// @param ev Event to send
/// @return 0 on success, negative on error
inline int send_event(const Event& ev) noexcept {
    return syscall::call(syscall::nr::SendEvent,
                        reinterpret_cast<uint32_t>(&ev));
}

// ============================================================================
// Time Functions
// ============================================================================

/// Get current time in microseconds
/// 
/// @return Time in microseconds (wraps around at ~35 minutes)
inline uint32_t time_usec() noexcept {
    return syscall::get_time_usec();
}

/// Sleep for specified duration
/// 
/// @param usec Duration in microseconds
inline void sleep_usec(uint32_t usec) noexcept {
    syscall::sleep_usec(usec);
}

/// Sleep for specified duration in milliseconds
/// 
/// @param ms Duration in milliseconds
inline void sleep_ms(uint32_t ms) noexcept {
    syscall::sleep_usec(ms * 1000);
}

// ============================================================================
// Debug/Logging
// ============================================================================

/// Output debug log message
/// 
/// @param msg Null-terminated message string
inline void log(const char* msg) noexcept {
    syscall::log(msg);
}

/// Panic with error message
/// 
/// This terminates the application immediately.
/// 
/// @param msg Error message
[[noreturn]] inline void panic(const char* msg) noexcept {
    syscall::panic(msg);
}

// ============================================================================
// Parameter Access
// ============================================================================

/// Get a parameter value
/// 
/// Parameters are shared between the processor (audio thread)
/// and the control task (main thread).
/// 
/// @param index Parameter index (0-31)
/// @return Parameter value
inline float get_param(uint32_t index) noexcept {
    return syscall::get_param(index);
}

/// Set a parameter value
/// 
/// @param index Parameter index (0-31)
/// @param value Parameter value
inline void set_param(uint32_t index, float value) noexcept {
    syscall::set_param(index, value);
}

// ============================================================================
// Shared Memory Access
// ============================================================================

/// Get pointer to shared memory region
/// 
/// Shared memory contains:
/// - Audio buffers (input/output)
/// - Event queue
/// - Parameter block
/// 
/// @return Pointer to SharedMemory structure
inline void* get_shared() noexcept {
    return syscall::get_shared();
}

// ============================================================================
// Application Exit
// ============================================================================

/// Exit application with code
/// 
/// @param code Exit code (0 = success)
[[noreturn]] inline void exit(int code = 0) noexcept {
    syscall::exit(code);
}

} // namespace umi

// SPDX-License-Identifier: MIT
// UMI-OS Application API
// High-level API for UMI applications

#pragma once

#include <atomic>
#include <cstdint>

#include "../core/audio_context.hh"
#include "../core/event.hh"
#include "../core/processor.hh"
#include "syscall.hh"

namespace umi {

// ============================================================================
// Processor Registration
// ============================================================================

namespace detail {

/// Type-erased process function
using ProcessFnPtr = void (*)(void*, AudioContext&);

/// Internal: Register processor with kernel
/// @param event_capacity Desired event buffer capacity (0 = default)
inline int32_t register_processor_impl(void* processor, ProcessFnPtr fn, uint32_t event_capacity = 0) noexcept {
    return syscall::call(
        syscall::nr::register_proc, reinterpret_cast<uint32_t>(processor), reinterpret_cast<uint32_t>(fn),
        event_capacity);
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
template <typename P>
    requires requires(P& p, AudioContext& ctx) { p.process(ctx); }
int register_processor(P& processor) noexcept {
    auto fn = [](void* p, AudioContext& ctx) { static_cast<P*>(p)->process(ctx); };
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
    return syscall::call(syscall::nr::register_proc, reinterpret_cast<uint32_t>(fn), 0);
}

// ============================================================================
// Event Handling
// ============================================================================

/// Wait for events (blocking)
///
/// @param mask Event mask to wait for (OR of event:: bits)
/// @param timeout_usec Timeout in microseconds (0 = wait indefinitely)
/// @return Events that occurred (bitmask)
inline uint32_t wait_event(uint32_t mask, uint32_t timeout_usec = 0) noexcept {
    return syscall::wait_event(mask, timeout_usec);
}

// peek_event / send_event: reserved for future syscall implementation

// ============================================================================
// Time Functions
// ============================================================================

/// Get current time in microseconds
///
/// @return Time in microseconds (64-bit)
inline uint64_t time_usec() noexcept {
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

// get_param / set_param: use SharedParamState via get_shared() or request_param()

// ============================================================================
// Configuration API (RouteTable, ParamMapping)
// ============================================================================

/// Set the MIDI routing table
/// @param table Pointer to RouteTable (must remain valid)
/// @return 0 on success
inline int32_t set_route_table(const void* table) noexcept {
    return syscall::set_route_table(table);
}

/// Set the CC-to-parameter mapping
/// @param mapping Pointer to ParamMapping (must remain valid)
/// @return 0 on success
inline int32_t set_param_mapping(const void* mapping) noexcept {
    return syscall::set_param_mapping(mapping);
}

/// Set the hardware input-to-parameter mapping
/// @param mapping Pointer to InputParamMapping (must remain valid)
/// @return 0 on success
inline int32_t set_input_mapping(const void* mapping) noexcept {
    return syscall::set_input_mapping(mapping);
}

/// Set full application configuration
/// @param config Pointer to AppConfig (must remain valid)
/// @return 0 on success
inline int32_t set_app_config(const void* config) noexcept {
    return syscall::set_app_config(config);
}

/// Request a parameter value change from the control task
/// @param param_id Parameter index (0-31)
/// @param value Desired value
/// @return 0 on success
inline int32_t request_param(uint32_t param_id, float value) noexcept {
    return syscall::send_param_request(param_id, value);
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

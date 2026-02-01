// SPDX-License-Identifier: MIT
// UMI-OS Application Syscall Interface
// Low-level syscall wrappers for ARM Cortex-M

#pragma once

#include <cstdint>

namespace umi::syscall {

// ============================================================================
// Syscall Numbers
// ============================================================================
// Number layout:
//   0–15:   Core API (process control, scheduling, info)
//   16–31:  Reserved (core API expansion)
//   32–47:  Filesystem
//   48–63:  Reserved (storage expansion)
//   64–255: Reserved

namespace nr {
// --- Core API (0–15) ---
inline constexpr uint32_t exit = 0;            ///< Terminate application (unload trigger)
inline constexpr uint32_t yield = 1;           ///< Return control to kernel
inline constexpr uint32_t wait_event = 2;      ///< Wait for event with optional timeout
inline constexpr uint32_t get_time = 3;        ///< Get monotonic time in microseconds
inline constexpr uint32_t get_shared = 4;      ///< Get SharedMemory pointer
inline constexpr uint32_t register_proc = 5;   ///< Register audio processor
inline constexpr uint32_t unregister_proc = 6; ///< Unregister audio processor (future)
inline constexpr uint32_t peek_event = 7;      ///< Peek event (future)
inline constexpr uint32_t send_event = 8;      ///< Send event (future)
inline constexpr uint32_t sleep_usec = 9;      ///< Sleep (future)
inline constexpr uint32_t log = 10;            ///< Log output (future)
inline constexpr uint32_t get_param = 11;      ///< Get parameter (future)
inline constexpr uint32_t set_param = 12;      ///< Set parameter (future)
// 13–15: reserved

// --- Configuration API (20–25) ---
inline constexpr uint32_t set_route_table = 20;    ///< Set RouteTable (ptr)
inline constexpr uint32_t set_param_mapping = 21;  ///< Set ParamMapping (ptr)
inline constexpr uint32_t set_input_mapping = 22;  ///< Set InputParamMapping (ptr)
inline constexpr uint32_t configure_input = 23;    ///< Set InputConfig (ptr)
inline constexpr uint32_t set_app_config = 24;     ///< Set full AppConfig (ptr)
inline constexpr uint32_t send_param_request = 25; ///< Request param change (id, value_bits)
// 26–31: reserved

// --- Filesystem (32–47) ---
inline constexpr uint32_t file_open = 32;  ///< Open file (future)
inline constexpr uint32_t file_read = 33;  ///< Read from file (future)
inline constexpr uint32_t file_write = 34; ///< Write to file (future)
inline constexpr uint32_t file_close = 35; ///< Close file (future)
inline constexpr uint32_t file_seek = 36;  ///< Seek within file (future)
inline constexpr uint32_t file_stat = 37;  ///< Get file info (future)
inline constexpr uint32_t dir_open = 38;   ///< Open directory (future)
inline constexpr uint32_t dir_read = 39;   ///< Read directory entry (future)
inline constexpr uint32_t dir_close = 40;  ///< Close directory (future)
// 41–47: reserved
} // namespace nr

// ============================================================================
// Event Bit Definitions
// ============================================================================

namespace event {
inline constexpr uint32_t Audio = (1 << 0);     ///< Audio buffer ready
inline constexpr uint32_t Midi = (1 << 1);      ///< MIDI data available
inline constexpr uint32_t VSync = (1 << 2);     ///< Display refresh
inline constexpr uint32_t Timer = (1 << 3);     ///< Timer tick
inline constexpr uint32_t Button = (1 << 4);    ///< Button press
inline constexpr uint32_t Shutdown = (1 << 31); ///< Shutdown requested
} // namespace event

// ============================================================================
// Low-level Syscall Invocation
// ============================================================================

#if defined(__ARM_ARCH)

/// Invoke syscall with 0-4 arguments
/// Calling convention: r12 = syscall number, r0-r3 = arguments, return in r0
/// Cortex-M SVC exception frame auto-stacks {r0,r1,r2,r3,r12,lr,pc,xpsr}
/// so kernel reads syscall_nr from sp[4] and args from sp[0]-sp[3].
[[gnu::always_inline]] inline int32_t call(uint32_t nr, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0,
                                           uint32_t a3 = 0) noexcept {
    register uint32_t r0 __asm__("r0") = a0;
    register uint32_t r1 __asm__("r1") = a1;
    register uint32_t r2 __asm__("r2") = a2;
    register uint32_t r3 __asm__("r3") = a3;
    register uint32_t r12 __asm__("r12") = nr;

    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r3), "r"(r12) : "memory");

    return static_cast<int32_t>(r0);
}

#else

// Host/simulation stub - syscalls are no-ops or simulated
inline int32_t call(uint32_t nr, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0) noexcept {
    (void)nr;
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
    return 0;
}

#endif

// ============================================================================
// Typed Syscall Wrappers
// ============================================================================

/// Terminate application with exit code
[[noreturn]] inline void exit(int code) noexcept {
    call(nr::exit, static_cast<uint32_t>(code));
    while (true) {
        __asm__ volatile("");
    }
}

/// Panic with error message (currently maps to exit)
[[noreturn]] inline void panic(const char* msg) noexcept {
    (void)msg;
    exit(-1);
}

/// Yield control back to kernel
inline void yield() noexcept {
    call(nr::yield);
}

/// Wait for events with timeout
/// @param mask Event mask to wait for (OR of event:: bits)
/// @param timeout_usec Timeout in microseconds (0 = wait indefinitely)
/// @return Events that occurred (bitmask)
inline uint32_t wait_event(uint32_t mask, uint32_t timeout_usec = 0) noexcept {
    return static_cast<uint32_t>(call(nr::wait_event, mask, timeout_usec));
}

/// Sleep for specified duration (microseconds)
inline void sleep_usec(uint32_t usec) noexcept {
    (void)wait_event(0, usec);
}

/// Get current time in microseconds (64-bit)
/// Returns 64-bit value via r0 (low) and r1 (high)
inline uint64_t get_time_usec() noexcept {
#if defined(__ARM_ARCH)
    register uint32_t r0 __asm__("r0");
    register uint32_t r1 __asm__("r1");
    register uint32_t r12 __asm__("r12") = nr::get_time;
    __asm__ volatile("svc #0" : "=r"(r0), "=r"(r1) : "r"(r12) : "memory");
    return (static_cast<uint64_t>(r1) << 32) | r0;
#else
    return 0;
#endif
}

/// Get shared memory pointer
inline void* get_shared() noexcept {
    return reinterpret_cast<void*>(call(nr::get_shared));
}

/// Output debug log message (stub)
inline void log(const char* msg) noexcept {
    (void)msg;
}

/// Get a parameter value (stub)
inline float get_param(uint32_t /*index*/) noexcept {
    return 0.0f;
}

/// Set a parameter value (stub)
inline void set_param(uint32_t /*index*/, float /*value*/) noexcept {}

/// Set route table (copies from app memory to kernel)
inline int32_t set_route_table(const void* table) noexcept {
    return call(nr::set_route_table, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(table)));
}

/// Set parameter mapping (copies from app memory to kernel)
inline int32_t set_param_mapping(const void* mapping) noexcept {
    return call(nr::set_param_mapping, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mapping)));
}

/// Set input parameter mapping (copies from app memory to kernel)
inline int32_t set_input_mapping(const void* mapping) noexcept {
    return call(nr::set_input_mapping, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mapping)));
}

/// Configure a single hardware input
inline int32_t configure_input(const void* config) noexcept {
    return call(nr::configure_input, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(config)));
}

/// Set full application config (copies from app memory to kernel)
inline int32_t set_app_config(const void* config) noexcept {
    return call(nr::set_app_config, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(config)));
}

/// Request parameter value change (param_id, float value as bits)
inline int32_t send_param_request(uint32_t param_id, float value) noexcept {
    uint32_t value_bits;
    __builtin_memcpy(&value_bits, &value, sizeof(value_bits));
    return call(nr::send_param_request, param_id, value_bits);
}

// ============================================================================
// Coroutine Scheduler Adapters
// ============================================================================
// Adapters to connect syscalls with umi::coro::Scheduler

namespace coro_adapter {

/// Wait function for Scheduler (WaitFn signature)
inline uint32_t wait(uint32_t mask, uint64_t timeout_us) noexcept {
    return wait_event(mask, static_cast<uint32_t>(timeout_us));
}

/// Time function for Scheduler (TimeFn signature)
inline uint64_t get_time() noexcept {
    return static_cast<uint64_t>(get_time_usec());
}

} // namespace coro_adapter

} // namespace umi::syscall

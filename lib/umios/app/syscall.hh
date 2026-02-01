// SPDX-License-Identifier: MIT
// UMI-OS Application Syscall Interface
// Low-level syscall wrappers for ARM Cortex-M

#pragma once

#include <cstdint>

namespace umi::syscall {

// ============================================================================
// Syscall Numbers (06-syscall.md spec)
// ============================================================================
// Number layout (sparse, 10-unit groups):
//   0– 9:  プロセス制御 (exit, yield, register_proc)
//  10–19:  時間・スケジューリング (wait_event, get_time, sleep)
//  20–29:  構成・パラメータ (set_app_config, set_route_table, etc.)
//  30–39:  MIDI / イベント (将来)
//  40–49:  情報取得 (get_shared)
//  50–59:  I/O (log, panic)
//  60–69:  ファイルシステム (将来)

namespace nr {
// --- Group 0: Process Control (0–9) ---
inline constexpr uint32_t exit = 0;              ///< Terminate application
inline constexpr uint32_t yield = 1;             ///< Return control to kernel
inline constexpr uint32_t register_proc = 2;     ///< Register audio processor
inline constexpr uint32_t unregister_proc = 3;   ///< Unregister audio processor (将来)

// --- Group 1: Time / Scheduling (10–19) ---
inline constexpr uint32_t wait_event = 10;       ///< Wait for event with optional timeout
inline constexpr uint32_t get_time = 11;         ///< Get monotonic time in microseconds
inline constexpr uint32_t sleep = 12;            ///< Sleep for specified duration

// --- Group 2: Configuration (20–29) ---
inline constexpr uint32_t set_app_config = 20;      ///< Set full AppConfig (ptr)
inline constexpr uint32_t set_route_table = 21;     ///< Set RouteTable (ptr)
inline constexpr uint32_t set_param_mapping = 22;   ///< Set ParamMapping (ptr)
inline constexpr uint32_t set_input_mapping = 23;   ///< Set InputParamMapping (ptr)
inline constexpr uint32_t configure_input = 24;     ///< Set InputConfig (ptr)
inline constexpr uint32_t send_param_request = 25;  ///< Request param change (id, value_bits)

// --- Group 4: Info (40–49) ---
inline constexpr uint32_t get_shared = 40;       ///< Get SharedMemory pointer

// --- Group 5: I/O (50–59) ---
inline constexpr uint32_t log = 50;              ///< Log output
inline constexpr uint32_t panic = 51;            ///< Panic (halt)

// --- Group 6: Filesystem (60–69, 将来) ---
inline constexpr uint32_t file_open = 60;  ///< Open file (将来)
inline constexpr uint32_t file_read = 61;  ///< Read from file (将来)
inline constexpr uint32_t file_write = 62; ///< Write to file (将来)
inline constexpr uint32_t file_close = 63; ///< Close file (将来)
inline constexpr uint32_t file_seek = 64;  ///< Seek within file (将来)
inline constexpr uint32_t file_stat = 65;  ///< Get file info (将来)
inline constexpr uint32_t dir_open = 66;   ///< Open directory (将来)
inline constexpr uint32_t dir_read = 67;   ///< Read directory entry (将来)
inline constexpr uint32_t dir_close = 68;  ///< Close directory (将来)
} // namespace nr

// ============================================================================
// Syscall Error Codes
// ============================================================================

enum class SyscallError : int32_t {
    OK = 0,
    INVALID_SYSCALL = -1,
    INVALID_PARAM = -2,
    ACCESS_DENIED = -3,
    NOT_FOUND = -4,
    TIMEOUT = -5,
    BUSY = -6,
};

// ============================================================================
// Event Bit Definitions
// ============================================================================

namespace event {
inline constexpr uint32_t audio = (1 << 0);       ///< Audio buffer ready
inline constexpr uint32_t midi = (1 << 1);        ///< MIDI data available
inline constexpr uint32_t vsync = (1 << 2);       ///< Display refresh
inline constexpr uint32_t timer = (1 << 3);       ///< Timer tick
inline constexpr uint32_t control = (1 << 4);     ///< ControlEvent arrived
inline constexpr uint32_t shutdown = (1u << 31);  ///< Shutdown requested
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
    call(nr::sleep, usec);
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

/// Output debug log message
inline void log(const char* msg) noexcept {
    call(nr::log, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(msg)));
}

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

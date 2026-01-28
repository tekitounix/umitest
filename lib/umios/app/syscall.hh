// SPDX-License-Identifier: MIT
// UMI-OS Application Syscall Interface
// Low-level syscall wrappers for ARM Cortex-M

#pragma once

#include <cstdint>

namespace umi::syscall {

// ============================================================================
// Syscall Numbers
// ============================================================================
// Must match kernel's syscall_handler.hh

namespace nr {
    // Process control
    inline constexpr uint32_t Exit          = 0;
    inline constexpr uint32_t RegisterProc  = 1;

    // Event handling
    inline constexpr uint32_t WaitEvent     = 2;
    inline constexpr uint32_t SendEvent     = 3;
    inline constexpr uint32_t PeekEvent     = 4;
    inline constexpr uint32_t Yield         = 5;   // Return control to kernel

    // Time
    inline constexpr uint32_t GetTime       = 10;
    inline constexpr uint32_t Sleep         = 11;
    
    // Debug/Log
    inline constexpr uint32_t Log           = 20;
    inline constexpr uint32_t Panic         = 21;
    
    // Parameters
    inline constexpr uint32_t GetParam      = 30;
    inline constexpr uint32_t SetParam      = 31;
    
    // Shared memory
    inline constexpr uint32_t GetShared     = 40;

    // MIDI
    inline constexpr uint32_t MidiSend      = 50;
    inline constexpr uint32_t MidiRecv      = 51;

    // LED/Button
    inline constexpr uint32_t SetLed        = 60;
    inline constexpr uint32_t GetLed        = 61;
    inline constexpr uint32_t GetButton     = 62;
}

// ============================================================================
// Event Bit Definitions
// ============================================================================

namespace event {
    inline constexpr uint32_t Audio       = (1 << 0);  ///< Audio buffer ready
    inline constexpr uint32_t Midi        = (1 << 1);  ///< MIDI data available
    inline constexpr uint32_t VSync       = (1 << 2);  ///< Display refresh
    inline constexpr uint32_t Timer       = (1 << 3);  ///< Timer tick
    inline constexpr uint32_t Button      = (1 << 4);  ///< Button press
    inline constexpr uint32_t Shutdown    = (1 << 31); ///< Shutdown requested
}

// ============================================================================
// Low-level Syscall Invocation
// ============================================================================

#if defined(__ARM_ARCH)

/// Invoke syscall with 0-4 arguments
/// @param nr Syscall number (in r0)
/// @param a0-a3 Arguments (in r1-r4)
/// @return Result (from r0)
inline int32_t call(uint32_t nr, uint32_t a0 = 0, uint32_t a1 = 0,
                    uint32_t a2 = 0, uint32_t a3 = 0) noexcept {
    register uint32_t r0 __asm__("r0") = nr;
    register uint32_t r1 __asm__("r1") = a0;
    register uint32_t r2 __asm__("r2") = a1;
    register uint32_t r3 __asm__("r3") = a2;
    register uint32_t r4 __asm__("r4") = a3;
    
    __asm__ volatile(
        "svc #0"
        : "+r"(r0)
        : "r"(r1), "r"(r2), "r"(r3), "r"(r4)
        : "memory"
    );
    
    return static_cast<int32_t>(r0);
}

#else

// Host/simulation stub - syscalls are no-ops or simulated
inline int32_t call(uint32_t nr, uint32_t a0 = 0, uint32_t a1 = 0,
                    uint32_t a2 = 0, uint32_t a3 = 0) noexcept {
    (void)nr; (void)a0; (void)a1; (void)a2; (void)a3;
    return 0;
}

#endif

// ============================================================================
// Typed Syscall Wrappers
// ============================================================================

/// Terminate application with exit code
[[noreturn]] inline void exit(int code) noexcept {
    call(nr::Exit, static_cast<uint32_t>(code));
    // Should not return, but compiler doesn't know that
    while (true) {
        __asm__ volatile("");
    }
}

/// Get current time in microseconds (lower 31 bits)
inline uint32_t get_time_usec() noexcept {
    return static_cast<uint32_t>(call(nr::GetTime));
}

/// Sleep for specified microseconds
inline void sleep_usec(uint32_t usec) noexcept {
    call(nr::Sleep, usec);
}

/// Output debug log message
inline void log(const char* msg, uint32_t len) noexcept {
    call(nr::Log, reinterpret_cast<uint32_t>(msg), len);
}

/// Output debug log message (null-terminated)
inline void log(const char* msg) noexcept {
    uint32_t len = 0;
    while (msg[len]) ++len;
    log(msg, len);
}

/// Panic with message
[[noreturn]] inline void panic(const char* msg) noexcept {
    call(nr::Panic, reinterpret_cast<uint32_t>(msg));
    while (true) {
        __asm__ volatile("");
    }
}

/// Yield control back to kernel (used after initialization)
inline void yield() noexcept {
    call(nr::Yield);
}

/// Get shared memory pointer
inline void* get_shared() noexcept {
    // GetShared syscall returns the shared memory address directly in r0
    return reinterpret_cast<void*>(call(nr::GetShared));
}

/// Get parameter value
inline float get_param(uint32_t index) noexcept {
    float value = 0.0f;
    call(nr::GetParam, index, reinterpret_cast<uint32_t>(&value));
    return value;
}

/// Set parameter value
inline void set_param(uint32_t index, float value) noexcept {
    uint32_t bits;
    __builtin_memcpy(&bits, &value, sizeof(bits));
    call(nr::SetParam, index, bits);
}

// ============================================================================
// LED/Button API
// ============================================================================

/// Set LED state
/// @param index LED index (0-3)
/// @param on true to turn on, false to turn off
inline void led_set(uint8_t index, bool on) noexcept {
    call(nr::SetLed, index, on ? 1 : 0);
}

/// Toggle LED state
/// @param index LED index (0-3)
inline void led_toggle(uint8_t index) noexcept {
    call(nr::SetLed, index, 2);  // 2 = toggle
}

/// Get LED state bitmap
/// @return Bitmap of LED states (bit0-3 = LED0-3)
inline uint8_t led_get() noexcept {
    return static_cast<uint8_t>(call(nr::GetLed));
}

/// Check if button was pressed (clears the flag)
/// @return true if button was pressed since last call
inline bool button_pressed() noexcept {
    return call(nr::GetButton, 0) != 0;
}

/// Get current button state
/// @return true if button is currently pressed
inline bool button_state() noexcept {
    return call(nr::GetButton, 1) != 0;
}

/// Wait for events with timeout
/// @param mask Event mask to wait for
/// @param timeout_usec Timeout in microseconds (0 = no timeout)
/// @return Events that occurred
inline uint32_t wait_event(uint32_t mask, uint32_t timeout_usec = 0) noexcept {
    return static_cast<uint32_t>(call(nr::WaitEvent, mask, timeout_usec));
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

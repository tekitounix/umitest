// SPDX-License-Identifier: MIT
// UMI-OS Kernel Port: Cortex-M4 Context Switch Support
//
// Provides helper constants and fast path for context switch.
// PendSV and SVC handlers should be implemented in application code
// as they require project-specific customization (TCB pointer names, etc).
#pragma once

#include <cstdint>
#include "context.hh"

namespace umi::kernel::port::cm4 {

// ============================================================================
// BASEPRI Values for Critical Sections
// ============================================================================

/// Default BASEPRI value for critical sections in PendSV.
/// Set to 0x50 (priority 5) to allow higher priority interrupts.
/// Adjust based on your interrupt priority scheme.
constexpr uint32_t DEFAULT_CRITICAL_BASEPRI = 0x50;

// ============================================================================
// Exception Priority Recommendations
// ============================================================================

/// Recommended exception priorities for kernel operation:
/// - SysTick: 0xF0 (low, but can preempt tasks)
/// - PendSV:  0xFF (lowest, context switch)
/// - SVC:     any (used only for first task start)

constexpr uint8_t RECOMMENDED_SYSTICK_PRIO = 0xF0;
constexpr uint8_t RECOMMENDED_PENDSV_PRIO  = 0xFF;

// ============================================================================
// Fast Path Yield Support
// ============================================================================

/// Check if we're in Thread mode (can use fast yield)
/// Returns true if IPSR == 0 (Thread mode), false if in exception handler
[[gnu::always_inline]]
inline bool in_thread_mode() noexcept {
    register std::uint32_t ipsr asm("r0");
    asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    return ipsr == 0;
}

/// Check if we can use fast yield path
/// Fast path is only available in Thread mode (not from ISR)
[[gnu::always_inline]]
inline bool can_use_fast_yield() noexcept {
    return in_thread_mode();
}

/// Get current exception number
/// Returns 0 if Thread mode, otherwise exception number
[[gnu::always_inline]]
inline std::uint32_t get_exception_number() noexcept {
    register std::uint32_t ipsr asm("r0");
    asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    return ipsr & 0x1FF;
}

/// Enter BASEPRI critical section
/// Returns previous BASEPRI value for restoration
[[gnu::always_inline]]
inline std::uint32_t enter_basepri_critical(std::uint32_t basepri = DEFAULT_CRITICAL_BASEPRI) noexcept {
    register std::uint32_t prev asm("r0");
    register std::uint32_t bp asm("r1") = basepri;
    asm volatile(
        "mrs   %0, basepri     \n"
        "msr   basepri, %1     \n"
        "dsb                   \n"
        "isb                   \n"
        : "=r"(prev)
        : "r"(bp)
        : "memory"
    );
    return prev;
}

/// Exit BASEPRI critical section
[[gnu::always_inline]]
inline void exit_basepri_critical(std::uint32_t prev) noexcept {
    register std::uint32_t p asm("r0") = prev;
    asm volatile(
        "msr   basepri, %0     \n"
        :
        : "r"(p)
        : "memory"
    );
}

/// RAII guard for BASEPRI critical section
class BasepriGuard {
public:
    explicit BasepriGuard(std::uint32_t basepri = DEFAULT_CRITICAL_BASEPRI) noexcept
        : prev_(enter_basepri_critical(basepri)) {}

    ~BasepriGuard() noexcept {
        exit_basepri_critical(prev_);
    }

    BasepriGuard(const BasepriGuard&) = delete;
    BasepriGuard& operator=(const BasepriGuard&) = delete;

private:
    std::uint32_t prev_;
};

}  // namespace umi::kernel::port::cm4

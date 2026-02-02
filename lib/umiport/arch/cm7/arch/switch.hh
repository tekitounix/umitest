// SPDX-License-Identifier: MIT
// UMI-OS Kernel Port: Cortex-M7 Context Switch Support
#pragma once

#include <cstdint>
#include <arch/context.hh>

namespace umi::kernel::port::cm7 {

constexpr uint32_t DEFAULT_CRITICAL_BASEPRI = 0x50;

constexpr uint8_t RECOMMENDED_SYSTICK_PRIO = 0xF0;
constexpr uint8_t RECOMMENDED_PENDSV_PRIO  = 0xFF;

[[gnu::always_inline]]
inline bool in_thread_mode() noexcept {
    register std::uint32_t ipsr asm("r0");
    asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    return ipsr == 0;
}

[[gnu::always_inline]]
inline std::uint32_t get_exception_number() noexcept {
    register std::uint32_t ipsr asm("r0");
    asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    return ipsr & 0x1FF;
}

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

/// Trigger PendSV for deferred context switch
[[gnu::always_inline]]
inline void request_context_switch() noexcept {
    auto* icsr = reinterpret_cast<volatile std::uint32_t*>(0xE000ED04);
    *icsr = (1U << 28);  // PENDSVSET
}

}  // namespace umi::kernel::port::cm7

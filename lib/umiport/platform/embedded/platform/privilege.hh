// SPDX-License-Identifier: MIT
// UMI-OS Privilege Control for Cortex-M4
#pragma once

#include <cstdint>

namespace umi::privilege {

// ============================================================================
// CONTROL Register Management
// ============================================================================
// Cortex-M4 CONTROL register:
//   Bit 0 (nPRIV): 0=Privileged, 1=Unprivileged (Thread mode only)
//   Bit 1 (SPSEL): 0=MSP, 1=PSP (Thread mode only)
//   Bit 2 (FPCA):  0=No FP context, 1=FP context active

namespace reg {

inline uint32_t control() {
    uint32_t r;
    asm volatile("mrs %0, control" : "=r"(r));
    return r;
}

inline void set_control(uint32_t v) {
    asm volatile("msr control, %0\n isb" ::"r"(v) : "memory");
}

}  // namespace reg

// CONTROL bits
inline constexpr uint32_t CONTROL_NPRIV = (1U << 0);
inline constexpr uint32_t CONTROL_SPSEL = (1U << 1);
inline constexpr uint32_t CONTROL_FPCA = (1U << 2);

/// Check if currently in privileged mode
inline bool is_privileged() {
    return (reg::control() & CONTROL_NPRIV) == 0;
}

/// Check if using PSP
inline bool is_using_psp() {
    return (reg::control() & CONTROL_SPSEL) != 0;
}

/// Switch to unprivileged mode (cannot be undone without SVC)
/// Only works in Thread mode (not Handler mode)
inline void drop_privilege() {
    uint32_t ctrl = reg::control();
    ctrl |= CONTROL_NPRIV;
    reg::set_control(ctrl);
}

/// Switch to PSP for Thread mode
inline void use_psp() {
    uint32_t ctrl = reg::control();
    ctrl |= CONTROL_SPSEL;
    reg::set_control(ctrl);
}

/// Set PSP value
inline void set_psp(uint32_t psp) {
    asm volatile("msr psp, %0" ::"r"(psp) : "memory");
}

/// Get PSP value
inline uint32_t get_psp() {
    uint32_t r;
    asm volatile("mrs %0, psp" : "=r"(r));
    return r;
}

/// Get MSP value
inline uint32_t get_msp() {
    uint32_t r;
    asm volatile("mrs %0, msp" : "=r"(r));
    return r;
}

// ============================================================================
// User Mode Transition
// ============================================================================

/// Switch to user mode with separate stack
/// @param user_sp  User stack pointer (top of user stack)
/// @param entry    User entry function
/// @note This function does not return
[[noreturn]] inline void enter_user_mode(uint32_t user_sp, void (*entry)()) {
    // Set up PSP
    set_psp(user_sp);

    // Switch to PSP and unprivileged
    uint32_t ctrl = reg::control();
    ctrl |= CONTROL_NPRIV | CONTROL_SPSEL;
    reg::set_control(ctrl);

    // Jump to user entry
    entry();

    // Should never reach here
    while (true) {
        asm volatile("wfi");
    }
}

}  // namespace umi::privilege

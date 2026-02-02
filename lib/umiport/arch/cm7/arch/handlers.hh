// SPDX-License-Identifier: MIT
// UMI-OS Kernel Port: Cortex-M7 Exception Handlers
//
// PendSV and SVC handlers for context switching.
// Identical to CM4 — Cortex-M7 shares the same exception model.
#pragma once

#include <cstdint>
#include <arch/context.hh>

namespace umi::port::cm7 {
}  // namespace umi::port::cm7

// Application-defined symbols (must be at global scope with C linkage)
extern "C" {
    /// Current task's context pointer. Must be defined by application.
    extern umi::port::cm7::TaskContext* volatile umi_cm7_current_tcb;

    /// Context switch callback. Must be defined by application.
    void umi_cm7_switch_context();
}

namespace umi::port::cm7 {

// Handler declarations
extern "C" void PendSV_Handler();
extern "C" void SVC_Handler();

/// Start the first task using SVC.
/// Call this after setting umi_cm7_current_tcb to the first task.
[[noreturn]] inline void start_first_task() {
    asm volatile(
        "   .syntax unified                 \n"
        // Reset MSP from vector table
        "   ldr     r0, =0xE000ED08         \n"  // VTOR
        "   ldr     r0, [r0]                \n"
        "   ldr     r0, [r0]                \n"  // Initial MSP
        "   msr     msp, r0                 \n"
        // Clear FPU state
        "   mov     r0, #0                  \n"
        "   msr     control, r0             \n"
        "   isb                             \n"
        // Enable interrupts and trigger SVC
        "   cpsie   i                       \n"
        "   cpsie   f                       \n"
        "   dsb                             \n"
        "   isb                             \n"
        "   svc     #0                      \n"
        "   nop                             \n"
        ::: "r0", "memory"
    );
    __builtin_unreachable();
}

}  // namespace umi::port::cm7

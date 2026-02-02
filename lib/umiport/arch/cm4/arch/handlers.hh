// SPDX-License-Identifier: MIT
// UMI-OS Kernel Port: Cortex-M4 Exception Handlers
//
// PendSV and SVC handlers for context switching.
// Applications must define the required symbols (see below).
#pragma once

#include <cstdint>
#include <arch/context.hh>

namespace umi::port::cm4 {

// ============================================================================
// Application-Defined Symbols
// ============================================================================
//
// The following symbols must be defined by the application:
//
//   umi_cm4_current_tcb  - Pointer to current task's TaskContext
//   umi_cm4_switch_context() - Called by PendSV to select next task
//
// Example implementation:
//
//   TaskContext* volatile umi_cm4_current_tcb = nullptr;
//
//   extern "C" void umi_cm4_switch_context() {
//       auto next = kernel.get_next_task();
//       if (next) {
//           umi_cm4_current_tcb = &task_contexts[*next];
//           kernel.prepare_switch(*next);
//       }
//   }
//
// Note: The symbols below are declared with C linkage at global scope.
// Define them at global scope in your application (outside any namespace).

}  // namespace umi::port::cm4

// Application-defined symbols (must be at global scope with C linkage)
extern "C" {
    /// Current task's context pointer. Must be defined by application.
    /// PendSV/SVC handlers read and write this to perform context switch.
    extern umi::port::cm4::TaskContext* volatile umi_cm4_current_tcb;
    
    /// Context switch callback. Must be defined by application.
    /// Called by PendSV handler with interrupts masked.
    /// Should update umi_cm4_current_tcb to point to the next task.
    void umi_cm4_switch_context();
}

namespace umi::port::cm4 {

// ============================================================================
// Handler Declarations
// ============================================================================

/// PendSV exception handler for context switching.
/// Saves current task's context and restores next task's context.
/// Supports FPU lazy stacking on Cortex-M4F.
extern "C" void PendSV_Handler();

/// SVC exception handler for starting the first task.
/// Restores context from umi_cm4_current_tcb and switches to PSP.
extern "C" void SVC_Handler();

// ============================================================================
// First Task Startup
// ============================================================================

/// Start the first task using SVC.
/// Call this after setting umi_cm4_current_tcb to the first task.
/// Never returns.
///
/// Prerequisites:
///   - umi_cm4_current_tcb points to first task's TaskContext
///   - Task stack is initialized via init_task_stack()
///   - PendSV priority set to lowest (0xFF recommended)
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

}  // namespace umi::port::cm4

// SPDX-License-Identifier: MIT
// UMI-OS Kernel Port: Cortex-M4 Exception Handlers Implementation
//
// PendSV and SVC handlers for preemptive multitasking.
// Uses FreeRTOS-style context switch with FPU lazy stacking support.

#include <arch/handlers.hh>

namespace umi::port::cm4 {

// ============================================================================
// PendSV Handler - Context Switch
// ============================================================================
//
// Call flow:
//   1. Hardware pushes R0-R3, R12, LR, PC, xPSR (and S0-S15 if FPU used)
//   2. This handler saves R4-R11 (and S16-S31 if FPU) to current task's stack
//   3. Calls umi_cm4_switch_context() to update umi_cm4_current_tcb
//   4. Restores R4-R11 (and S16-S31) from new task's stack
//   5. Returns via BX LR, hardware restores R0-R3, R12, LR, PC, xPSR
//
// Stack layout after save:
//   [Low address / top of saved stack]
//   R4..R11, EXC_RETURN  <- Software saved
//   S16..S31             <- Software saved (if FPU used)
//   R0..R3, R12, LR, PC, xPSR  <- Hardware pushed
//   S0..S15, FPSCR, reserved   <- Hardware pushed (if FPU used)
//   [High address / original stack top]

extern "C" __attribute__((naked)) void PendSV_Handler() {
    asm volatile(
        "   .syntax unified                 \n"
        
        // Get PSP (current task's stack pointer)
        "   mrs     r0, psp                 \n"
        "   isb                             \n"
        
        // Get current TCB pointer
        "   ldr     r3, =umi_cm4_current_tcb \n"
        "   ldr     r2, [r3]                \n"
        
        // Save FPU high registers if used (bit 4 of LR = 0 means FPU)
        "   tst     lr, #0x10               \n"
        "   it      eq                      \n"
        "   vstmdbeq r0!, {s16-s31}         \n"
        
        // Save R4-R11 and EXC_RETURN (LR)
        "   stmdb   r0!, {r4-r11, lr}       \n"
        
        // Store stack pointer to TCB (first member)
        "   str     r0, [r2]                \n"
        
        // Critical section: mask interrupts
        "   mov     r0, #0x50               \n"  // BASEPRI threshold
        "   msr     basepri, r0             \n"
        "   dsb                             \n"
        "   isb                             \n"
        
        // Call application's switch context function
        "   bl      umi_cm4_switch_context  \n"
        
        // End critical section
        "   mov     r0, #0                  \n"
        "   msr     basepri, r0             \n"
        
        // Get new TCB (may have changed)
        "   ldr     r3, =umi_cm4_current_tcb \n"
        "   ldr     r1, [r3]                \n"
        "   ldr     r0, [r1]                \n"  // Stack pointer
        
        // Restore R4-R11 and EXC_RETURN
        "   ldmia   r0!, {r4-r11, lr}       \n"
        
        // Restore FPU high registers if needed
        "   tst     lr, #0x10               \n"
        "   it      eq                      \n"
        "   vldmiaeq r0!, {s16-s31}         \n"
        
        // Set PSP and return
        "   msr     psp, r0                 \n"
        "   isb                             \n"
        "   bx      lr                      \n"
        
        ".align 4                           \n"
        ::: "memory"
    );
}

// ============================================================================
// SVC Handler - First Task Startup
// ============================================================================
//
// Called via SVC #0 from start_first_task().
// Restores context from umi_cm4_current_tcb and switches to thread mode.

extern "C" __attribute__((naked)) void SVC_Handler() {
    asm volatile(
        "   .syntax unified                 \n"
        
        // Get current TCB
        "   ldr     r3, =umi_cm4_current_tcb \n"
        "   ldr     r1, [r3]                \n"
        "   ldr     r0, [r1]                \n"  // Stack pointer
        
        // Restore R4-R11 and EXC_RETURN
        "   ldmia   r0!, {r4-r11, lr}       \n"
        
        // Restore FPU high registers if needed
        "   tst     lr, #0x10               \n"
        "   it      eq                      \n"
        "   vldmiaeq r0!, {s16-s31}         \n"
        
        // Set PSP
        "   msr     psp, r0                 \n"
        "   isb                             \n"
        
        // Ensure interrupts enabled
        "   mov     r0, #0                  \n"
        "   msr     basepri, r0             \n"
        
        // Return (hardware restores remaining context)
        "   bx      lr                      \n"
        
        ".align 4                           \n"
        ::: "memory"
    );
}

}  // namespace umi::port::cm4

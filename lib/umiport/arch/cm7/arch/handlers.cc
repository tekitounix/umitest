// SPDX-License-Identifier: MIT
// UMI-OS Kernel Port: Cortex-M7 Exception Handlers Implementation
//
// PendSV and SVC handlers for preemptive multitasking.
// Identical assembly to CM4 — Cortex-M7 shares the same exception model.

#include <arch/handlers.hh>

namespace umi::port::cm7 {

extern "C" __attribute__((naked)) void PendSV_Handler() {
    asm volatile(
        "   .syntax unified                 \n"
        "   mrs     r0, psp                 \n"
        "   isb                             \n"
        "   ldr     r3, =umi_cm7_current_tcb \n"
        "   ldr     r2, [r3]                \n"
        // Save FPU high registers if used
        "   tst     lr, #0x10               \n"
        "   it      eq                      \n"
        "   vstmdbeq r0!, {s16-s31}         \n"
        // Save R4-R11 and EXC_RETURN
        "   stmdb   r0!, {r4-r11, lr}       \n"
        "   str     r0, [r2]                \n"
        // Critical section
        "   mov     r0, #0x50               \n"
        "   msr     basepri, r0             \n"
        "   dsb                             \n"
        "   isb                             \n"
        "   bl      umi_cm7_switch_context  \n"
        "   mov     r0, #0                  \n"
        "   msr     basepri, r0             \n"
        // Restore next task
        "   ldr     r3, =umi_cm7_current_tcb \n"
        "   ldr     r1, [r3]                \n"
        "   ldr     r0, [r1]                \n"
        "   ldmia   r0!, {r4-r11, lr}       \n"
        "   tst     lr, #0x10               \n"
        "   it      eq                      \n"
        "   vldmiaeq r0!, {s16-s31}         \n"
        "   msr     psp, r0                 \n"
        "   isb                             \n"
        "   bx      lr                      \n"
        ".align 4                           \n"
        ::: "memory"
    );
}

extern "C" __attribute__((naked)) void SVC_Handler() {
    asm volatile(
        "   .syntax unified                 \n"
        "   ldr     r3, =umi_cm7_current_tcb \n"
        "   ldr     r1, [r3]                \n"
        "   ldr     r0, [r1]                \n"
        "   ldmia   r0!, {r4-r11, lr}       \n"
        "   tst     lr, #0x10               \n"
        "   it      eq                      \n"
        "   vldmiaeq r0!, {s16-s31}         \n"
        "   msr     psp, r0                 \n"
        "   isb                             \n"
        "   mov     r0, #0                  \n"
        "   msr     basepri, r0             \n"
        "   bx      lr                      \n"
        ".align 4                           \n"
        ::: "memory"
    );
}

}  // namespace umi::port::cm7

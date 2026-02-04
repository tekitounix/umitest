// SPDX-License-Identifier: MIT
// UMI-OS Kernel Port: Cortex-M4 Context Management
//
// Task context save/restore and stack initialization for Cortex-M4(F).
// Supports both FPU and non-FPU tasks with lazy stacking.
#pragma once

#include <cstdint>
#include <umi/kernel/fpu_policy.hh>

namespace umi::port::cm4 {

// ============================================================================
// Exception Return Values
// ============================================================================

namespace exc_return {
// Exception return values (EXC_RETURN):
// Bit 4: Stack frame type (1 = basic, 0 = extended/FPU)
// Bit 3: Return mode (1 = Thread, 0 = Handler)
// Bit 2: Stack pointer (1 = PSP, 0 = MSP)

constexpr uint32_t THREAD_PSP_BASIC = 0xFFFFFFFD;    // Thread mode, PSP, no FPU
constexpr uint32_t THREAD_PSP_EXTENDED = 0xFFFFFFED; // Thread mode, PSP, with FPU
constexpr uint32_t THREAD_MSP_BASIC = 0xFFFFFFF9;    // Thread mode, MSP, no FPU
constexpr uint32_t HANDLER_MSP_BASIC = 0xFFFFFFF1;   // Handler mode, MSP, no FPU
} // namespace exc_return

// ============================================================================
// Task Control Block
// ============================================================================

/// Minimal TCB for Cortex-M4 context switching.
/// stack_ptr MUST be the first member for assembly compatibility.
struct TaskContext {
    uint32_t* stack_ptr;  // Current stack pointer (PSP) - MUST BE FIRST
    void (*entry)(void*); // Entry function
    void* arg;            // Entry argument
    bool uses_fpu;        // Task uses FPU
    bool initialized;     // Has been initialized
};

// ============================================================================
// Stack Frame Layout
// ============================================================================

// Cortex-M4 exception stack frame (hardware pushed on exception entry):
// - Basic frame (8 words): R0, R1, R2, R3, R12, LR, PC, xPSR
// - Extended frame (26 words): Basic + S0-S15, FPSCR, reserved
//
// Software-saved context (pushed by PendSV):
// - R4-R11, EXC_RETURN (9 words)
// - S16-S31 (16 words, if FPU used)

// ============================================================================
// Stack Initialization
// ============================================================================

/// Initialize task stack for first context switch.
/// Creates a stack frame that looks like it was interrupted mid-execution.
///
/// @param stack_top   Pointer to top of stack (highest address)
/// @param entry       Task entry function
/// @param arg         Argument passed to entry function (in R0)
/// @param uses_fpu    Whether task will use FPU
/// @return            New stack pointer (to be stored in TCB)
inline uint32_t* init_task_stack(uint32_t* stack_top, void (*entry)(void*), void* arg, bool uses_fpu) {
    // Align stack to 8 bytes (ABI requirement)
    stack_top = reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(stack_top) & ~0x7UL);

    if (uses_fpu) {
        // Extended frame: S0-S15, FPSCR, reserved (18 words)
        *(--stack_top) = 0x00000000; // Reserved (alignment)
        *(--stack_top) = 0x00000000; // FPSCR
        for (int i = 15; i >= 0; --i) {
            *(--stack_top) = 0x00000000; // S15..S0
        }
    }

    // Hardware exception frame (pushed automatically on exception)
    *(--stack_top) = 0x01000000;                        // xPSR (Thumb bit set)
    *(--stack_top) = reinterpret_cast<uint32_t>(entry); // PC (task entry)
    *(--stack_top) = 0xFFFFFFFE;                        // LR (invalid - task should not return)
    *(--stack_top) = 0x12121212;                        // R12
    *(--stack_top) = 0x03030303;                        // R3
    *(--stack_top) = 0x02020202;                        // R2
    *(--stack_top) = 0x01010101;                        // R1
    *(--stack_top) = reinterpret_cast<uint32_t>(arg);   // R0 (argument)

    if (uses_fpu) {
        // S16-S31 (software saved, 16 words)
        for (int i = 31; i >= 16; --i) {
            *(--stack_top) = 0x00000000; // S31..S16
        }
    }

    // Software saved context: R4-R11 and EXC_RETURN
    // Push order matches ldmia restore: r4,r5,r6,r7,r8,r9,r10,r11,lr
    *(--stack_top) = uses_fpu ? exc_return::THREAD_PSP_EXTENDED : exc_return::THREAD_PSP_BASIC; // LR (EXC_RETURN)
    *(--stack_top) = 0x11111111;                                                                // R11
    *(--stack_top) = 0x10101010;                                                                // R10
    *(--stack_top) = 0x09090909;                                                                // R9
    *(--stack_top) = 0x08080808;                                                                // R8
    *(--stack_top) = 0x07070707;                                                                // R7
    *(--stack_top) = 0x06060606;                                                                // R6
    *(--stack_top) = 0x05050505;                                                                // R5
    *(--stack_top) = 0x04040404;                                                                // R4

    return stack_top;
}

/// Initialize a TaskContext with a new stack.
///
/// @param ctx         TaskContext to initialize
/// @param stack_base  Base address of stack array
/// @param stack_size  Size of stack in uint32_t words
/// @param entry       Task entry function
/// @param arg         Argument passed to entry
/// @param uses_fpu    Whether task will use FPU
inline void init_task_context(
    TaskContext& ctx, uint32_t* stack_base, uint32_t stack_size, void (*entry)(void*), void* arg, bool uses_fpu) {
    uint32_t* stack_top = stack_base + stack_size; // Stack grows down

    ctx.stack_ptr = init_task_stack(stack_top, entry, arg, uses_fpu);
    ctx.entry = entry;
    ctx.arg = arg;
    ctx.uses_fpu = uses_fpu;
    ctx.initialized = true;
}

// ============================================================================
// Policy-based Stack Initialization (compile-time FPU policy)
// ============================================================================

/// Initialize task stack with compile-time FPU policy.
/// The policy is automatically determined by resolve_fpu_policy().
template <umi::FpuPolicy Policy>
inline uint32_t* init_task_stack(uint32_t* stack_top, void (*entry)(void*), void* arg) {
    return init_task_stack(stack_top, entry, arg, umi::needs_fpu_frame(Policy));
}

/// Initialize a TaskContext with compile-time FPU policy.
template <umi::FpuPolicy Policy>
inline void
init_task_context(TaskContext& ctx, uint32_t* stack_base, uint32_t stack_size, void (*entry)(void*), void* arg) {
    init_task_context(ctx, stack_base, stack_size, entry, arg, umi::needs_fpu_frame(Policy));
}

} // namespace umi::port::cm4

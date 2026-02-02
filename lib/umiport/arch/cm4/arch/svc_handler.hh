// SPDX-License-Identifier: MIT
// UMI-OS SVC Handler for Cortex-M4
#pragma once

#include <cstddef>
#include <cstdint>
#include <umios/kernel/syscall/syscall_numbers.hh>

namespace umi::kernel {

// Forward declaration - kernel provides this
struct KernelState;
extern KernelState* g_kernel;

// ============================================================================
// SVC Handler Implementation
// ============================================================================

/// Exception frame pushed by hardware on exception entry
struct ExceptionFrame {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t lr;
    uint32_t pc;
    uint32_t xpsr;
};

/// SVC dispatcher - called from assembly handler
/// @param frame  Pointer to exception frame on stack
/// @param svc_num  Syscall number (from r12)
extern "C" void svc_dispatch(ExceptionFrame* frame, uint8_t svc_num);

/// SVC Handler entry point (naked function)
/// Determines stack (MSP/PSP), extracts syscall number from r12,
/// and calls svc_dispatch.
[[gnu::naked]] inline void SVC_Handler() {
    asm volatile(
        // Determine which stack was used (EXC_RETURN bit 2)
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"      // Using MSP
        "mrsne r0, psp\n"      // Using PSP

        // r0 = exception frame pointer
        // r12 contains syscall number (set by caller)
        // Pass r12 as second argument (r1)
        "mov r1, r12\n"

        // Call C++ dispatcher
        "b svc_dispatch\n"
    );
}

// ============================================================================
// Syscall Implementation
// ============================================================================

namespace impl {

// System time counter
extern uint64_t system_time_us;

// Current task ID
extern uint32_t current_task_id;

/// Handle sys_get_shared
inline uintptr_t handle_get_shared(uint32_t /*region_id*/) {
    // TODO: Implement with SharedMemory pointer
    return 0;
}

/// Handle sys_get_time
inline void handle_get_time(ExceptionFrame* frame) {
    frame->r0 = static_cast<uint32_t>(system_time_us);
    frame->r1 = static_cast<uint32_t>(system_time_us >> 32);
}

/// Handle sys_yield
inline void handle_yield() {
    // Trigger PendSV for context switch
    volatile uint32_t& ICSR = *reinterpret_cast<volatile uint32_t*>(0xE000ED04);
    ICSR = (1U << 28);  // Set PENDSVSET
}

}  // namespace impl

/// Main syscall dispatcher
extern "C" inline void svc_dispatch(ExceptionFrame* frame, uint8_t svc_num) {
    using namespace syscall;

    switch (svc_num) {
        case sys_get_shared:
            frame->r0 = impl::handle_get_shared(frame->r0);
            break;

        case sys_get_time:
            impl::handle_get_time(frame);
            break;

        case sys_yield:
            impl::handle_yield();
            break;

        default:
            frame->r0 = static_cast<uint32_t>(SyscallError::INVALID_SYSCALL);
            break;
    }
}

}  // namespace umi::kernel

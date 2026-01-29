// SPDX-License-Identifier: MIT
// Cortex-M4 Architecture Abstraction Layer Implementation

#include "arch.hh"

#include <port/cm4/context.hh>
#include <umios/backend/cm/common/dwt.hh>
#include <umios/backend/cm/common/scb.hh>
#include <umios/backend/cm/common/systick.hh>

namespace umi::arch::cm4 {

// Kernel callbacks (internal linkage but accessible within this TU)
static TickCallback g_tick_callback = nullptr;
static SwitchContextCallback g_switch_context_callback = nullptr;
static SvcCallback g_svc_callback = nullptr;

// Current TCB (used by PendSV_Handler assembly)
TaskContext* volatile current_tcb = nullptr;

// Syscall number for yield (must match kernel's app_syscall::Yield)
constexpr uint32_t syscall_yield = 5;

void init_systick(uint32_t freq_hz, uint32_t period_us) {
    umi::port::arm::SysTick::init_us(freq_hz, period_us);
}

void init_cycle_counter() {
    umi::port::arm::DWT::enable();
}

uint32_t dwt_cycle() {
    return umi::port::arm::DWT::cycles();
}

void init_task(TaskContext& tcb,
               uint32_t* stack,
               uint32_t stack_size,
               void (*entry)(void*),
               void* arg,
               bool use_fpu) {
    umi::port::cm4::init_task_context(tcb, stack, stack_size, entry, arg, use_fpu);
}

void yield() {
    __asm__ volatile("mov r0, %0\n"
                     "svc 0\n" ::"i"(syscall_yield)
                     : "r0");
}

void wait_for_interrupt() {
    __asm__ volatile("wfi");
}

void request_context_switch() {
    umi::port::arm::SCB::trigger_pendsv();
}

void delay_cycles(uint32_t cycles) {
    for (uint32_t i = 0; i < cycles; ++i) {
        __asm__ volatile("nop");
    }
}

void set_tick_callback(TickCallback cb) {
    g_tick_callback = cb;
}

void set_switch_context_callback(SwitchContextCallback cb) {
    g_switch_context_callback = cb;
}

void set_svc_callback(SvcCallback cb) {
    g_svc_callback = cb;
}

// Forward declaration for umi_cm4_current_tcb (defined below, used by PendSV_Handler)
extern "C" umi::port::cm4::TaskContext* volatile umi_cm4_current_tcb;

[[noreturn]] void start_scheduler(umi::port::cm4::TaskContext* initial_tcb,
                                   void (*entry)(void*),
                                   void* arg,
                                   [[maybe_unused]] uint32_t* stack_top) {
    current_tcb = initial_tcb;
    umi_cm4_current_tcb = initial_tcb;

    // Use the pre-initialized stack pointer from TCB (set by init_task_context)
    // This contains the properly constructed context frame
    __asm__ volatile("msr psp, %0" ::"r"(initial_tcb->stack_ptr));
    __asm__ volatile("isb");

    // Switch to PSP usage in thread mode (CONTROL[1]=1, CONTROL[0]=0 privileged)
    __asm__ volatile("mov r0, #2\n"
                     "msr control, r0\n"
                     "isb\n" ::
                         : "r0");

    // Call entry directly - when it blocks, PendSV handles context switch
    entry(arg);

    // Should never reach here
    while (true) {
        __asm__ volatile("wfi");
    }
}

} // namespace umi::arch::cm4

// Symbol for PendSV_Handler assembly (extern "C" linkage)
extern "C" [[gnu::used]] umi::port::cm4::TaskContext* volatile umi_cm4_current_tcb = nullptr;

// C wrapper for switch context callback
extern "C" [[gnu::used]] void umi_cm4_switch_context() {
    if (umi::arch::cm4::g_switch_context_callback != nullptr) {
        umi::arch::cm4::g_switch_context_callback();
    }
    // Sync the assembly-visible pointer
    umi_cm4_current_tcb = umi::arch::cm4::current_tcb;
}

// C wrapper for SVC callback
extern "C" [[gnu::used]] void svc_handler_c(uint32_t* sp) {
    if (umi::arch::cm4::g_svc_callback != nullptr) {
        umi::arch::cm4::g_svc_callback(sp);
    }
}

// Exception handlers (Cortex-M4 architecture specific)
extern "C" [[gnu::naked]] void SVC_Handler() {
    __asm__ volatile("tst lr, #4\n"
                     "ite eq\n"
                     "mrseq r0, msp\n"
                     "mrsne r0, psp\n"
                     "push {lr}\n"
                     "bl svc_handler_c\n"
                     "pop {pc}\n" :::);
}

extern "C" [[gnu::naked]] void PendSV_Handler() {
    __asm__ volatile(".syntax unified\n"

                     "ldr     r3, =umi_cm4_current_tcb\n"
                     "ldr     r2, [r3]\n"
                     "cbz     r2, 1f\n"

                     "mrs     r0, psp\n"
                     "isb\n"

                     "tst     lr, #0x10\n"
                     "it      eq\n"
                     "vstmdbeq r0!, {s16-s31}\n"

                     "stmdb   r0!, {r4-r11, lr}\n"
                     "str     r0, [r2]\n"

                     "1:\n"
                     "mov     r0, #0x60\n"
                     "msr     basepri, r0\n"
                     "dsb\n"
                     "isb\n"

                     "bl      umi_cm4_switch_context\n"

                     "mov     r0, #0\n"
                     "msr     basepri, r0\n"

                     "ldr     r3, =umi_cm4_current_tcb\n"
                     "ldr     r1, [r3]\n"
                     "cbz     r1, 2f\n"

                     "ldr     r0, [r1]\n"
                     "ldmia   r0!, {r4-r11, lr}\n"

                     "tst     lr, #0x10\n"
                     "it      eq\n"
                     "vldmiaeq r0!, {s16-s31}\n"

                     "msr     psp, r0\n"
                     "isb\n"
                     "bx      lr\n"

                     "2:\n"
                     "bx      lr\n"

                     ".align 4\n" ::
                         : "memory");
}

extern "C" void SysTick_Handler() {
    if (umi::arch::cm4::g_tick_callback != nullptr) {
        umi::arch::cm4::g_tick_callback();
    }
}

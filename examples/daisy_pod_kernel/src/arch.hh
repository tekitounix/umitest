// SPDX-License-Identifier: MIT
// Cortex-M7 Architecture Abstraction Layer
// Architecture-specific initialization (SysTick, DWT, exception handlers)

#pragma once

#include <arch/context.hh>
#include <cstdint>
#include <umi/kernel/fpu_policy.hh>

namespace umi::arch::cm7 {

// Task context type alias (kernel uses this instead of port::cm7::TaskContext directly)
using TaskContext = umi::port::cm7::TaskContext;

// SysTick initialization
void init_systick(uint32_t freq_hz, uint32_t period_us);

// DWT Cycle Counter
void init_cycle_counter();
uint32_t dwt_cycle();

// Task primitives
void init_task(TaskContext& tcb, uint32_t* stack, uint32_t stack_size, void (*entry)(void*), void* arg, bool use_fpu);

template <umi::FpuPolicy Policy>
inline void init_task(TaskContext& tcb, uint32_t* stack, uint32_t stack_size, void (*entry)(void*), void* arg) {
    umi::port::cm7::init_task_context<Policy>(tcb, stack, stack_size, entry, arg);
}

void yield();
void wait_for_interrupt();
void request_context_switch();

// Kernel callbacks (set by kernel before start_rtos)
using TickCallback = void (*)();
using SwitchContextCallback = void (*)();
using SvcCallback = void (*)(uint32_t* sp);

void set_tick_callback(TickCallback cb);
void set_switch_context_callback(SwitchContextCallback cb);
void set_svc_callback(SvcCallback cb);

// Start scheduler (switches to PSP, privileged mode, calls entry)
[[noreturn]] void start_scheduler(TaskContext* initial_tcb, void (*entry)(void*), void* arg, uint32_t* stack_top);

// Current TCB pointer (used by PendSV_Handler)
extern TaskContext* volatile current_tcb;

} // namespace umi::arch::cm7

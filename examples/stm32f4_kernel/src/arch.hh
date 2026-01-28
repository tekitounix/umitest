// SPDX-License-Identifier: MIT
// Cortex-M4 Architecture Abstraction Layer
// Architecture-specific initialization (SysTick, DWT, exception handlers)

#pragma once

#include <cstdint>

#include <port/cm4/context.hh>

namespace umi::arch::cm4 {

// Task context type alias (kernel uses this instead of port::cm4::TaskContext directly)
using TaskContext = umi::port::cm4::TaskContext;

// SysTick initialization
void init_systick(uint32_t freq_hz, uint32_t period_us);

// DWT Cycle Counter
void init_cycle_counter();
uint32_t dwt_cycle();

// Task primitives
void init_task(TaskContext& tcb,
               uint32_t* stack,
               uint32_t stack_size,
               void (*entry)(void*),
               void* arg,
               bool use_fpu);
void yield();
void wait_for_interrupt();
void request_context_switch();
void delay_cycles(uint32_t cycles);

// Kernel callbacks (set by kernel before start_rtos)
using TickCallback = void (*)();
using SwitchContextCallback = void (*)();
using SvcCallback = void (*)(uint32_t* sp);

void set_tick_callback(TickCallback cb);
void set_switch_context_callback(SwitchContextCallback cb);
void set_svc_callback(SvcCallback cb);

// Start scheduler (switches to PSP, enters unprivileged mode, calls entry)
// stack_top: top of stack array (stack_base + stack_size), used for PSP
[[noreturn]] void start_scheduler(TaskContext* initial_tcb,
                                   void (*entry)(void*),
                                   void* arg,
                                   uint32_t* stack_top);

// Current TCB pointer (used by PendSV_Handler)
extern TaskContext* volatile current_tcb;

} // namespace umi::arch::cm4

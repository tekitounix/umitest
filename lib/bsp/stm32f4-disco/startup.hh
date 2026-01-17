// SPDX-License-Identifier: MIT
// UMI-OS STM32F4 Startup (ARM Cortex-M4 only)
// NOTE: clangd warnings on host are expected - this code is ARM-specific.
#pragma once

#pragma GCC diagnostic ignored "-Wshorten-64-to-32"

#include "../../arm/cortex-m/cortex_m4.hh"
#include "../../arm/cortex-m/common/vector_table.hh"
#include "hw_impl.hh"

// =============================================================================
// Linker Symbols
// =============================================================================
extern "C" {
    extern std::uint32_t _estack;
    extern std::uint32_t _sdata, _edata, _sidata;
    extern std::uint32_t _sbss, _ebss;
    extern void (*__init_array_start[])();
    extern void (*__init_array_end[])();
}

namespace umi::board::stm32f4 {

using namespace umi::port::arm;

// Global vector table (RAM-based for dynamic handler registration)
inline VecTableF4 g_vectors;

/// Early hardware init before main
inline void early_init() {
    // Enable FPU
    SCB::enable_fpu();
    
    // Enable cycle counter
    DWT::enable();
}

/// Copy .data from flash to RAM
inline void init_data() {
    auto* src = &_sidata;
    for (auto* dst = &_sdata; dst < &_edata; ) {
        *dst++ = *src++;
    }
}

/// Zero .bss section
inline void init_bss() {
    for (auto* p = &_sbss; p < &_ebss; ) {
        *p++ = 0;
    }
}

/// Call global constructors
inline void call_ctors() {
    for (auto fn = __init_array_start; fn < __init_array_end; ++fn) {
        (*fn)();
    }
}

/// Initialize RAM vector table with system handlers
inline void init_vectors(void(*systick)(), void(*pendsv)() = nullptr) {
    g_vectors.init(static_cast<std::uint32_t>(reinterpret_cast<uintptr_t>(&_estack)), nullptr);
    if (systick) g_vectors.set(VecTableF4::Exc::SysTick, systick);
    if (pendsv)  g_vectors.set(VecTableF4::Exc::PendSV, pendsv);
}

} // namespace umi::board::stm32f4

// =============================================================================
// Minimal Boot Vector (place in .isr_vector)
// =============================================================================
// Usage: Link this file and define Reset_Handler in your main.cc
// =============================================================================

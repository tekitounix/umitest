// SPDX-License-Identifier: MIT
// Unified ARM Cortex-M4 header (umbrella include)
#pragma once

// IWYU pragma: begin_exports
#include <common/systick.hh>
#include <common/nvic.hh>
#include <common/scb.hh>
#include <common/dwt.hh>
#include <common/vector_table.hh>
// IWYU pragma: end_exports

namespace umi::port::arm {

/// Cortex-M4 CPU primitives
struct CM4 {
    static void cpsid_i() { asm volatile("cpsid i" ::: "memory"); }
    static void cpsie_i() { asm volatile("cpsie i" ::: "memory"); }
    static void dsb() { asm volatile("dsb" ::: "memory"); }
    static void isb() { asm volatile("isb" ::: "memory"); }
    static void dmb() { asm volatile("dmb" ::: "memory"); }
    static void wfi() { asm volatile("wfi"); }
    static void wfe() { asm volatile("wfe"); }
    static void sev() { asm volatile("sev"); }
    static void nop() { asm volatile("nop"); }
    
    static std::uint32_t primask() {
        std::uint32_t r; asm volatile("mrs %0, primask" : "=r"(r)); return r;
    }
    static void set_primask(std::uint32_t v) {
        asm volatile("msr primask, %0" :: "r"(v) : "memory");
    }
    static std::uint32_t ipsr() {
        std::uint32_t r; asm volatile("mrs %0, ipsr" : "=r"(r)); return r & 0xFF;
    }
    static bool in_isr() { return ipsr() != 0; }
    
    static std::uint32_t psp() {
        std::uint32_t r; asm volatile("mrs %0, psp" : "=r"(r)); return r;
    }
    static void set_psp(std::uint32_t v) {
        asm volatile("msr psp, %0" :: "r"(v) : "memory");
    }
    static std::uint32_t msp() {
        std::uint32_t r; asm volatile("mrs %0, msp" : "=r"(r)); return r;
    }
    static void set_msp(std::uint32_t v) {
        asm volatile("msr msp, %0" :: "r"(v) : "memory");
    }
};

} // namespace umi::port::arm

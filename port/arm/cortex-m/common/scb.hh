// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>

namespace umi::port::arm {

struct SCB {
    static constexpr std::uint32_t BASE = 0xE000ED00;
    static auto& icsr()  { return *reinterpret_cast<volatile std::uint32_t*>(BASE + 0x04); }
    static auto& vtor()  { return *reinterpret_cast<volatile std::uint32_t*>(BASE + 0x08); }
    static auto& aircr() { return *reinterpret_cast<volatile std::uint32_t*>(BASE + 0x0C); }
    static auto& cpacr() { return *reinterpret_cast<volatile std::uint32_t*>(BASE + 0x88); }
    static auto* shpr()  { return reinterpret_cast<volatile std::uint8_t*>(BASE + 0x18); }
    
    static void trigger_pendsv() { icsr() = 1u << 28; }
    static void clear_pendsv()   { icsr() = 1u << 27; }
    static void set_vtor(std::uint32_t addr) { vtor() = addr; }
    static void enable_fpu() { cpacr() |= 0xF << 20; }
    
    [[noreturn]] static void reset() {
        aircr() = 0x05FA0004;
        while(1) asm volatile("");
    }
    
    /// Set system exception priority (4-15: MemManage..SysTick)
    static void set_exc_prio(std::uint32_t exc, std::uint8_t prio) {
        if (exc >= 4 && exc <= 15) shpr()[exc - 4] = prio;
    }
};

} // namespace umi::port::arm

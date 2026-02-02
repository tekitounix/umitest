// SPDX-License-Identifier: MIT
// UMI-OS Backend: ARM Cortex-M Fault Information
//
// Provides fault register definitions and capture utilities.
// Fault handlers should be implemented in application code.
#pragma once

#include <cstdint>

namespace umi::backend::cm {

// ============================================================================
// Fault Status Register Addresses
// ============================================================================

namespace fault_regs {
    constexpr uint32_t CFSR  = 0xE000ED28;  // Configurable Fault Status Register
    constexpr uint32_t HFSR  = 0xE000ED2C;  // HardFault Status Register
    constexpr uint32_t DFSR  = 0xE000ED30;  // Debug Fault Status Register
    constexpr uint32_t MMFAR = 0xE000ED34;  // MemManage Fault Address Register
    constexpr uint32_t BFAR  = 0xE000ED38;  // BusFault Address Register
    constexpr uint32_t AFSR  = 0xE000ED3C;  // Auxiliary Fault Status Register
}

// ============================================================================
// CFSR Bit Definitions
// ============================================================================

namespace cfsr {
    // MemManage Fault Status (bits 0-7)
    constexpr uint32_t IACCVIOL  = 1 << 0;   // Instruction access violation
    constexpr uint32_t DACCVIOL  = 1 << 1;   // Data access violation
    constexpr uint32_t MUNSTKERR = 1 << 3;   // MemManage on unstacking
    constexpr uint32_t MSTKERR   = 1 << 4;   // MemManage on stacking
    constexpr uint32_t MLSPERR   = 1 << 5;   // MemManage during FP lazy state
    constexpr uint32_t MMARVALID = 1 << 7;   // MMFAR valid
    
    // BusFault Status (bits 8-15)
    constexpr uint32_t IBUSERR   = 1 << 8;   // Instruction bus error
    constexpr uint32_t PRECISERR = 1 << 9;   // Precise data bus error
    constexpr uint32_t IMPRECISERR = 1 << 10; // Imprecise data bus error
    constexpr uint32_t UNSTKERR  = 1 << 11;  // BusFault on unstacking
    constexpr uint32_t STKERR    = 1 << 12;  // BusFault on stacking
    constexpr uint32_t LSPERR    = 1 << 13;  // BusFault during FP lazy state
    constexpr uint32_t BFARVALID = 1 << 15;  // BFAR valid
    
    // UsageFault Status (bits 16-31)
    constexpr uint32_t UNDEFINSTR = 1 << 16; // Undefined instruction
    constexpr uint32_t INVSTATE   = 1 << 17; // Invalid EPSR.T or EPSR.IT
    constexpr uint32_t INVPC      = 1 << 18; // Invalid EXC_RETURN value
    constexpr uint32_t NOCP       = 1 << 19; // No coprocessor
    constexpr uint32_t UNALIGNED  = 1 << 24; // Unaligned access
    constexpr uint32_t DIVBYZERO  = 1 << 25; // Divide by zero
}

// ============================================================================
// HFSR Bit Definitions
// ============================================================================

namespace hfsr {
    constexpr uint32_t VECTTBL  = 1 << 1;    // Vector table read fault
    constexpr uint32_t FORCED   = 1 << 30;   // Forced HardFault (escalated)
    constexpr uint32_t DEBUGEVT = 1U << 31;  // Debug event
}

// ============================================================================
// Fault Information Structure
// ============================================================================

/// Captured fault information for debugging
struct FaultInfo {
    uint32_t cfsr;    // Configurable Fault Status Register
    uint32_t hfsr;    // HardFault Status Register
    uint32_t mmfar;   // MemManage Fault Address (if valid)
    uint32_t bfar;    // BusFault Address (if valid)
    uint32_t lr;      // Link register at fault
    uint32_t pc;      // Program counter at fault (from stack)
    
    /// Check if this was an escalated fault
    bool is_escalated() const { return hfsr & hfsr::FORCED; }
    
    /// Check if MMFAR is valid
    bool mmfar_valid() const { return cfsr & cfsr::MMARVALID; }
    
    /// Check if BFAR is valid
    bool bfar_valid() const { return cfsr & cfsr::BFARVALID; }
    
    /// Capture fault registers
    void capture() {
        cfsr  = *reinterpret_cast<volatile uint32_t*>(fault_regs::CFSR);
        hfsr  = *reinterpret_cast<volatile uint32_t*>(fault_regs::HFSR);
        mmfar = *reinterpret_cast<volatile uint32_t*>(fault_regs::MMFAR);
        bfar  = *reinterpret_cast<volatile uint32_t*>(fault_regs::BFAR);
    }
};

}  // namespace umi::backend::cm

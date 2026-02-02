// SPDX-License-Identifier: MIT
// STM32H750 FMC (Flexible Memory Controller) - SDRAM registers
// Reference: RM0433 Rev 8, Section 22
#pragma once

#include <mmio/mmio.hh>

namespace umi::stm32h7 {

// NOLINTBEGIN(readability-identifier-naming)

/// FMC SDRAM controller registers
/// Base: 0x5200'0000 (FMC), SDRAM control starts at offset 0x140
struct FMC_SDRAM : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0x5200'0000;

    // SDRAM Control Register Bank 1
    struct SDCR1 : mm::Register<FMC_SDRAM, 0x140, 32> {
        struct NC : mm::Field<SDCR1, 0, 2> {};       // Column address bits
        struct NR : mm::Field<SDCR1, 2, 2> {};       // Row address bits
        struct MWID : mm::Field<SDCR1, 4, 2> {};     // Memory data bus width
        struct NB : mm::Field<SDCR1, 6, 1> {};       // Internal banks (0=2, 1=4)
        struct CAS : mm::Field<SDCR1, 7, 2> {};      // CAS latency
        struct WP : mm::Field<SDCR1, 9, 1> {};       // Write protection
        struct SDCLK : mm::Field<SDCR1, 10, 2> {};   // SDRAM clock period
        struct RBURST : mm::Field<SDCR1, 12, 1> {};  // Burst read
        struct RPIPE : mm::Field<SDCR1, 13, 2> {};   // Read pipe delay
    };

    // SDRAM Control Register Bank 2
    struct SDCR2 : mm::Register<FMC_SDRAM, 0x144, 32> {
        struct NC : mm::Field<SDCR2, 0, 2> {};
        struct NR : mm::Field<SDCR2, 2, 2> {};
        struct MWID : mm::Field<SDCR2, 4, 2> {};
        struct NB : mm::Field<SDCR2, 6, 1> {};
        struct CAS : mm::Field<SDCR2, 7, 2> {};
        struct WP : mm::Field<SDCR2, 9, 1> {};
        struct SDCLK : mm::Field<SDCR2, 10, 2> {};
        struct RBURST : mm::Field<SDCR2, 12, 1> {};
        struct RPIPE : mm::Field<SDCR2, 13, 2> {};
    };

    // SDRAM Timing Register Bank 1
    struct SDTR1 : mm::Register<FMC_SDRAM, 0x148, 32> {
        struct TMRD : mm::Field<SDTR1, 0, 4> {};   // Load mode register to active
        struct TXSR : mm::Field<SDTR1, 4, 4> {};   // Exit self-refresh delay
        struct TRAS : mm::Field<SDTR1, 8, 4> {};   // Self-refresh time
        struct TRC : mm::Field<SDTR1, 12, 4> {};   // Row cycle delay
        struct TWR : mm::Field<SDTR1, 16, 4> {};   // Recovery delay
        struct TRP : mm::Field<SDTR1, 20, 4> {};   // Row precharge delay
        struct TRCD : mm::Field<SDTR1, 24, 4> {};  // Row to column delay
    };

    // SDRAM Timing Register Bank 2
    struct SDTR2 : mm::Register<FMC_SDRAM, 0x14C, 32> {
        struct TMRD : mm::Field<SDTR2, 0, 4> {};
        struct TXSR : mm::Field<SDTR2, 4, 4> {};
        struct TRAS : mm::Field<SDTR2, 8, 4> {};
        struct TRC : mm::Field<SDTR2, 12, 4> {};
        struct TWR : mm::Field<SDTR2, 16, 4> {};
        struct TRP : mm::Field<SDTR2, 20, 4> {};
        struct TRCD : mm::Field<SDTR2, 24, 4> {};
    };

    // SDRAM Command Mode Register
    struct SDCMR : mm::Register<FMC_SDRAM, 0x150, 32> {
        struct MODE : mm::Field<SDCMR, 0, 3> {};    // Command mode
        struct CTB2 : mm::Field<SDCMR, 3, 1> {};    // Bank 2 target
        struct CTB1 : mm::Field<SDCMR, 4, 1> {};    // Bank 1 target
        struct NRFS : mm::Field<SDCMR, 5, 4> {};    // Number of auto-refresh
        struct MRD : mm::Field<SDCMR, 9, 13> {};    // Mode register definition
    };

    // SDRAM Refresh Timer Register
    struct SDRTR : mm::Register<FMC_SDRAM, 0x154, 32> {
        struct CRE : mm::Field<SDRTR, 0, 1> {};     // Clear refresh error
        struct COUNT : mm::Field<SDRTR, 1, 13> {};   // Refresh count
        struct REIE : mm::Field<SDRTR, 14, 1> {};    // Refresh error interrupt enable
    };

    // SDRAM Status Register
    struct SDSR : mm::Register<FMC_SDRAM, 0x158, 32, mm::RO> {
        struct RE : mm::Field<SDSR, 0, 1> {};        // Refresh error
        struct MODES1 : mm::Field<SDSR, 1, 2> {};    // Bank 1 status
        struct MODES2 : mm::Field<SDSR, 3, 2> {};    // Bank 2 status
        struct BUSY : mm::Field<SDSR, 5, 1> {};      // Busy
    };
};

// SDRAM command modes
namespace fmc_sdcmd {
constexpr std::uint32_t NORMAL          = 0b000;
constexpr std::uint32_t CLK_ENABLE      = 0b001;
constexpr std::uint32_t PALL            = 0b010;
constexpr std::uint32_t AUTO_REFRESH    = 0b011;
constexpr std::uint32_t LOAD_MODE       = 0b100;
constexpr std::uint32_t SELF_REFRESH    = 0b101;
constexpr std::uint32_t POWER_DOWN      = 0b110;
}

// NOLINTEND(readability-identifier-naming)

} // namespace umi::stm32h7

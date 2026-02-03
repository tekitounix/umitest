// SPDX-License-Identifier: MIT
// STM32H750 QUADSPI Register Definitions
// Reference: RM0433 Rev 8, Section 24
#pragma once

#include <umimmio.hh>

namespace umi::stm32h7 {

// NOLINTBEGIN(readability-identifier-naming)

/// QUADSPI register block
/// Base: 0x5200'5000
struct QUADSPI : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0x5200'5000;

    struct CR : mm::Register<QUADSPI, 0x00, 32> {
        struct EN : mm::Field<CR, 0, 1> {};         // Enable
        struct ABORT : mm::Field<CR, 1, 1> {};      // Abort request
        struct DMAEN : mm::Field<CR, 2, 1> {};      // DMA enable
        struct TCEN : mm::Field<CR, 3, 1> {};       // Timeout counter enable
        struct SSHIFT : mm::Field<CR, 4, 1> {};     // Sample shift
        struct FTHRES : mm::Field<CR, 8, 5> {};     // FIFO threshold level
        struct TEIE : mm::Field<CR, 16, 1> {};      // Transfer error interrupt enable
        struct TCIE : mm::Field<CR, 17, 1> {};      // Transfer complete interrupt enable
        struct FTIE : mm::Field<CR, 18, 1> {};      // FIFO threshold interrupt enable
        struct SMIE : mm::Field<CR, 19, 1> {};      // Status match interrupt enable
        struct TOIE : mm::Field<CR, 20, 1> {};      // Timeout interrupt enable
        struct APMS : mm::Field<CR, 22, 1> {};      // Automatic poll mode stop
        struct PMM : mm::Field<CR, 23, 1> {};       // Polling match mode
        struct PRESCALER : mm::Field<CR, 24, 8> {}; // Clock prescaler
    };

    struct DCR : mm::Register<QUADSPI, 0x04, 32> {
        struct CKMODE : mm::Field<DCR, 0, 1> {}; // Clock mode
        struct CSHT : mm::Field<DCR, 8, 3> {};   // Chip select high time
        struct FSIZE : mm::Field<DCR, 16, 5> {}; // Flash size (2^(FSIZE+1) bytes)
    };

    struct SR : mm::Register<QUADSPI, 0x08, 32, mm::RO> {
        struct TEF : mm::Field<SR, 0, 1> {};    // Transfer error flag
        struct TCF : mm::Field<SR, 1, 1> {};    // Transfer complete flag
        struct FTF : mm::Field<SR, 2, 1> {};    // FIFO threshold flag
        struct SMF : mm::Field<SR, 3, 1> {};    // Status match flag
        struct TOF : mm::Field<SR, 4, 1> {};    // Timeout flag
        struct BUSY : mm::Field<SR, 5, 1> {};   // Busy
        struct FLEVEL : mm::Field<SR, 8, 7> {}; // FIFO level
    };

    struct FCR : mm::Register<QUADSPI, 0x0C, 32> {
        struct CTEF : mm::Field<FCR, 0, 1> {}; // Clear transfer error flag
        struct CTCF : mm::Field<FCR, 1, 1> {}; // Clear transfer complete flag
        struct CSMF : mm::Field<FCR, 3, 1> {}; // Clear status match flag
        struct CTOF : mm::Field<FCR, 4, 1> {}; // Clear timeout flag
    };

    struct DLR : mm::Register<QUADSPI, 0x10, 32> {}; // Data length
    struct CCR : mm::Register<QUADSPI, 0x14, 32> {
        struct INSTRUCTION : mm::Field<CCR, 0, 8> {}; // Instruction
        struct IMODE : mm::Field<CCR, 8, 2> {};       // Instruction mode
        struct ADMODE : mm::Field<CCR, 10, 2> {};     // Address mode
        struct ADSIZE : mm::Field<CCR, 12, 2> {};     // Address size
        struct ABMODE : mm::Field<CCR, 14, 2> {};     // Alternate bytes mode
        struct ABSIZE : mm::Field<CCR, 16, 2> {};     // Alternate bytes size
        struct DCYC : mm::Field<CCR, 18, 5> {};       // Dummy cycles
        struct DMODE : mm::Field<CCR, 24, 2> {};      // Data mode
        struct FMODE : mm::Field<CCR, 26, 2> {};      // Functional mode
        struct SIOO : mm::Field<CCR, 28, 1> {};       // Send instruction only once
        struct DDRM : mm::Field<CCR, 31, 1> {};       // Double data rate mode
    };

    struct AR : mm::Register<QUADSPI, 0x18, 32> {};    // Address
    struct ABR : mm::Register<QUADSPI, 0x1C, 32> {};   // Alternate bytes
    struct DR : mm::Register<QUADSPI, 0x20, 32> {};    // Data
    struct PSMKR : mm::Register<QUADSPI, 0x24, 32> {}; // Polling status mask
    struct PSMAR : mm::Register<QUADSPI, 0x28, 32> {}; // Polling status match
    struct PIR : mm::Register<QUADSPI, 0x2C, 32> {};   // Polling interval
    struct LPTR : mm::Register<QUADSPI, 0x30, 32> {
        struct TIMEOUT : mm::Field<LPTR, 0, 16> {}; // Low-power timeout period
    };
};

// Functional modes
namespace qspi_fmode {
constexpr std::uint32_t INDIRECT_WRITE = 0b00;
constexpr std::uint32_t INDIRECT_READ = 0b01;
constexpr std::uint32_t AUTO_POLLING = 0b10;
constexpr std::uint32_t MEMORY_MAPPED = 0b11;
} // namespace qspi_fmode

// Line modes
namespace qspi_mode {
constexpr std::uint32_t NONE = 0b00;
constexpr std::uint32_t SINGLE = 0b01;
constexpr std::uint32_t DUAL = 0b10;
constexpr std::uint32_t QUAD = 0b11;
} // namespace qspi_mode

// Address size
namespace qspi_adsize {
constexpr std::uint32_t ADDR_8BIT = 0b00;
constexpr std::uint32_t ADDR_16BIT = 0b01;
constexpr std::uint32_t ADDR_24BIT = 0b10;
constexpr std::uint32_t ADDR_32BIT = 0b11;
} // namespace qspi_adsize

// NOLINTEND(readability-identifier-naming)

} // namespace umi::stm32h7

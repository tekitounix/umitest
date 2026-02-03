// SPDX-License-Identifier: MIT
// STM32H750 SDMMC1 registers (SD/MMC card interface)
// Reference: RM0433 Rev 8, Section 55
#pragma once

#include <umimmio.hh>

namespace umi::stm32h7 {

// NOLINTBEGIN(readability-identifier-naming)

/// SDMMC1 (AHB3, 0x5200'4400)
struct SDMMC1 : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0x5200'4400;

    struct POWER : mm::Register<SDMMC1, 0x00, 32> {
        struct PWRCTRL : mm::Field<POWER, 0, 2> {};  // 00=off, 11=on
    };

    struct CLKCR : mm::Register<SDMMC1, 0x04, 32> {
        struct CLKDIV  : mm::Field<CLKCR, 0, 10> {};   // Clock divider
        struct PWRSAV  : mm::Field<CLKCR, 12, 1> {};    // Power save
        struct WIDBUS  : mm::Field<CLKCR, 14, 2> {};    // Bus width (00=1bit, 01=4bit)
        struct NEGEDGE : mm::Field<CLKCR, 16, 1> {};    // Neg edge selection
        struct HWFC_EN : mm::Field<CLKCR, 17, 1> {};    // HW flow control
        struct DDR     : mm::Field<CLKCR, 18, 1> {};    // DDR mode
        struct BUSSPEED : mm::Field<CLKCR, 19, 1> {};   // Bus speed mode
        struct SELCLKRX : mm::Field<CLKCR, 20, 2> {};   // Receive clock selection
    };

    struct ARG : mm::Register<SDMMC1, 0x08, 32> {};   // Command argument
    struct CMD : mm::Register<SDMMC1, 0x0C, 32> {
        struct CMDINDEX  : mm::Field<CMD, 0, 6> {};     // Command index
        struct CMDTRANS  : mm::Field<CMD, 6, 1> {};     // Data transfer command
        struct CMDSTOP   : mm::Field<CMD, 7, 1> {};     // Stop transfer command
        struct WAITRESP  : mm::Field<CMD, 8, 2> {};     // Wait for response (01=short, 11=long)
        struct WAITINT   : mm::Field<CMD, 10, 1> {};    // CPSM wait for interrupt
        struct CPSMEN    : mm::Field<CMD, 12, 1> {};    // CPSM enable
    };

    struct RESPCMD : mm::Register<SDMMC1, 0x10, 32, mm::RO> {};
    struct RESP1   : mm::Register<SDMMC1, 0x14, 32, mm::RO> {};
    struct RESP2   : mm::Register<SDMMC1, 0x18, 32, mm::RO> {};
    struct RESP3   : mm::Register<SDMMC1, 0x1C, 32, mm::RO> {};
    struct RESP4   : mm::Register<SDMMC1, 0x20, 32, mm::RO> {};

    struct DTIMER  : mm::Register<SDMMC1, 0x24, 32> {};  // Data timeout
    struct DLEN    : mm::Register<SDMMC1, 0x28, 32> {};  // Data length
    struct DCTRL   : mm::Register<SDMMC1, 0x2C, 32> {
        struct DTEN    : mm::Field<DCTRL, 0, 1> {};     // Data transfer enable
        struct DTDIR   : mm::Field<DCTRL, 1, 1> {};     // 0=to card, 1=from card
        struct DTMODE  : mm::Field<DCTRL, 2, 2> {};     // 00=block, 01=SDIO
        struct DBLOCKSIZE : mm::Field<DCTRL, 4, 4> {};  // Block size (log2: 9=512)
    };

    struct STA : mm::Register<SDMMC1, 0x34, 32, mm::RO> {
        struct CCRCFAIL : mm::Field<STA, 0, 1> {};
        struct DCRCFAIL : mm::Field<STA, 1, 1> {};
        struct CTIMEOUT : mm::Field<STA, 2, 1> {};
        struct DTIMEOUT : mm::Field<STA, 3, 1> {};
        struct TXUNDERR : mm::Field<STA, 4, 1> {};
        struct RXOVERR  : mm::Field<STA, 5, 1> {};
        struct CMDREND  : mm::Field<STA, 6, 1> {};
        struct CMDSENT  : mm::Field<STA, 7, 1> {};
        struct DATAEND  : mm::Field<STA, 8, 1> {};
        struct DABORT   : mm::Field<STA, 11, 1> {};
        struct DPSMACT  : mm::Field<STA, 12, 1> {};
        struct CPSMACT  : mm::Field<STA, 13, 1> {};
        struct TXFIFOE  : mm::Field<STA, 14, 1> {};
        struct RXFIFONE : mm::Field<STA, 15, 1> {};
        struct TXFIFOF  : mm::Field<STA, 16, 1> {};
        struct RXFIFOF  : mm::Field<STA, 17, 1> {};
        struct BUSYD0   : mm::Field<STA, 20, 1> {};
        struct BUSYD0END : mm::Field<STA, 21, 1> {};
    };

    struct ICR : mm::Register<SDMMC1, 0x38, 32> {};  // Interrupt clear (write 1 to clear)

    struct FIFO : mm::Register<SDMMC1, 0x80, 32> {}; // Data FIFO (read/write)

    struct IDMACTRL : mm::Register<SDMMC1, 0x50, 32> {
        struct IDMAEN : mm::Field<IDMACTRL, 0, 1> {};
    };
    struct IDMABASE0 : mm::Register<SDMMC1, 0x58, 32> {}; // IDMA buffer 0 base address
};

// SDMMC command response types
namespace sdmmc_resp {
constexpr std::uint32_t NONE  = 0b00;
constexpr std::uint32_t SHORT = 0b01;
constexpr std::uint32_t LONG  = 0b11;
}

// Common SD commands
namespace sd_cmd {
constexpr std::uint32_t GO_IDLE_STATE    = 0;
constexpr std::uint32_t SEND_IF_COND     = 8;
constexpr std::uint32_t SEND_CSD         = 9;
constexpr std::uint32_t SEND_CID         = 10;
constexpr std::uint32_t SEND_STATUS      = 13;
constexpr std::uint32_t SET_BLOCKLEN     = 16;
constexpr std::uint32_t READ_SINGLE      = 17;
constexpr std::uint32_t READ_MULTI       = 18;
constexpr std::uint32_t WRITE_SINGLE     = 24;
constexpr std::uint32_t WRITE_MULTI      = 25;
constexpr std::uint32_t APP_CMD          = 55;
constexpr std::uint32_t SD_SEND_OP_COND  = 41;  // ACMD41
}

// NOLINTEND(readability-identifier-naming)

} // namespace umi::stm32h7

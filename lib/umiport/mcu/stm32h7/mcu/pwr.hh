// SPDX-License-Identifier: MIT
// STM32H750 PWR (Power Control) - mmio register definitions
#pragma once

#include <mmio/mmio.hh>

namespace umi::stm32h7 {

// NOLINTBEGIN(readability-identifier-naming)

/// STM32H750 PWR register block (RM0433 Section 6)
/// Base address: 0x5802'4800
struct PWR : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0x5802'4800;

    /// Power control register 1
    struct CR1 : mm::Register<PWR, 0x00, 32> {
        struct LPDS : mm::Field<CR1, 0, 1> {};
        struct PVDE : mm::Field<CR1, 4, 1> {};
        struct PLS : mm::Field<CR1, 5, 3> {};
        struct DBP : mm::Field<CR1, 8, 1> {};     // Backup domain access
        struct FLPS : mm::Field<CR1, 9, 1> {};
        struct SVOS : mm::Field<CR1, 14, 2> {};   // System Stop mode voltage scaling
        struct AVDEN : mm::Field<CR1, 16, 1> {};
        struct ALS : mm::Field<CR1, 17, 2> {};
    };

    /// Power control/status register 1
    struct CSR1 : mm::Register<PWR, 0x04, 32, mm::RO> {
        struct PVDO : mm::Field<CSR1, 4, 1> {};
        struct ACTVOSRDY : mm::Field<CSR1, 13, 1> {};  // VOS ready
        struct ACTVOS : mm::Field<CSR1, 14, 2> {};     // Current VOS
    };

    /// Power control register 3
    struct CR3 : mm::Register<PWR, 0x0C, 32> {
        struct BYPASS : mm::Field<CR3, 0, 1> {};
        struct LDOEN : mm::Field<CR3, 1, 1> {};
        struct SDEN : mm::Field<CR3, 2, 1> {};      // SMPS enable
        struct SDEXTHP : mm::Field<CR3, 3, 1> {};
        struct SDLEVEL : mm::Field<CR3, 4, 2> {};
        struct VBE : mm::Field<CR3, 8, 1> {};        // VBAT charging enable
        struct VBRS : mm::Field<CR3, 9, 1> {};
        struct USB33DEN : mm::Field<CR3, 24, 1> {};  // USB 3.3V detector enable
        struct USBREGEN : mm::Field<CR3, 25, 1> {};  // USB regulator enable
        struct USB33RDY : mm::Field<CR3, 26, 1> {};
    };

    /// D3 domain control register
    struct D3CR : mm::Register<PWR, 0x18, 32> {
        struct VOSRDY : mm::Field<D3CR, 13, 1> {};   // VOS ready
        struct VOS : mm::Field<D3CR, 14, 2> {};       // Voltage scaling
    };
};

// VOS field values
namespace pwr_vos {
// Note: VOS0 requires SYSCFG.PWRCR.ODEN=1 on top of VOS=0b11
constexpr std::uint32_t SCALE3 = 0b01;  // Lowest voltage, lowest freq
constexpr std::uint32_t SCALE2 = 0b10;
constexpr std::uint32_t SCALE1 = 0b11;  // Highest voltage (up to 400MHz)
// VOS0 = SCALE1 + SYSCFG ODEN (up to 480MHz)
} // namespace pwr_vos

// NOLINTEND(readability-identifier-naming)

} // namespace umi::stm32h7

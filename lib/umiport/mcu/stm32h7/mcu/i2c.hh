// SPDX-License-Identifier: MIT
// STM32H750 I2C Register Definitions
// Reference: RM0433 Rev 8, Section 47
#pragma once

#include <mmio/mmio.hh>

namespace umi::stm32h7 {

// NOLINTBEGIN(readability-identifier-naming)

template <mm::Addr BaseAddr>
struct I2Cx : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = BaseAddr;

    struct CR1 : mm::Register<I2Cx, 0x00, 32> {
        struct PE : mm::Field<CR1, 0, 1> {};
        struct TXIE : mm::Field<CR1, 1, 1> {};
        struct RXIE : mm::Field<CR1, 2, 1> {};
        struct ADDRIE : mm::Field<CR1, 3, 1> {};
        struct NACKIE : mm::Field<CR1, 4, 1> {};
        struct STOPIE : mm::Field<CR1, 5, 1> {};
        struct TCIE : mm::Field<CR1, 6, 1> {};
        struct ERRIE : mm::Field<CR1, 7, 1> {};
        struct DNF : mm::Field<CR1, 8, 4> {};
        struct ANFOFF : mm::Field<CR1, 12, 1> {};
        struct TXDMAEN : mm::Field<CR1, 14, 1> {};
        struct RXDMAEN : mm::Field<CR1, 15, 1> {};
        struct SBC : mm::Field<CR1, 16, 1> {};
        struct NOSTRETCH : mm::Field<CR1, 17, 1> {};
    };

    struct CR2 : mm::Register<I2Cx, 0x04, 32> {
        struct SADD : mm::Field<CR2, 0, 10> {};
        struct RD_WRN : mm::Field<CR2, 10, 1> {};
        struct ADD10 : mm::Field<CR2, 11, 1> {};
        struct HEAD10R : mm::Field<CR2, 12, 1> {};
        struct START : mm::Field<CR2, 13, 1> {};
        struct STOP : mm::Field<CR2, 14, 1> {};
        struct NACK : mm::Field<CR2, 15, 1> {};
        struct NBYTES : mm::Field<CR2, 16, 8> {};
        struct RELOAD : mm::Field<CR2, 24, 1> {};
        struct AUTOEND : mm::Field<CR2, 25, 1> {};
    };

    struct TIMINGR : mm::Register<I2Cx, 0x10, 32> {
        struct SCLL : mm::Field<TIMINGR, 0, 8> {};
        struct SCLH : mm::Field<TIMINGR, 8, 8> {};
        struct SDADEL : mm::Field<TIMINGR, 16, 4> {};
        struct SCLDEL : mm::Field<TIMINGR, 20, 4> {};
        struct PRESC : mm::Field<TIMINGR, 28, 4> {};
    };

    struct ISR : mm::Register<I2Cx, 0x18, 32, mm::RO> {
        struct TXE : mm::Field<ISR, 0, 1> {};
        struct TXIS : mm::Field<ISR, 1, 1> {};
        struct RXNE : mm::Field<ISR, 2, 1> {};
        struct ADDR : mm::Field<ISR, 3, 1> {};
        struct NACKF : mm::Field<ISR, 4, 1> {};
        struct STOPF : mm::Field<ISR, 5, 1> {};
        struct TC : mm::Field<ISR, 6, 1> {};
        struct TCR : mm::Field<ISR, 7, 1> {};
        struct BERR : mm::Field<ISR, 8, 1> {};
        struct ARLO : mm::Field<ISR, 9, 1> {};
        struct OVR : mm::Field<ISR, 10, 1> {};
        struct BUSY : mm::Field<ISR, 15, 1> {};
    };

    struct ICR : mm::Register<I2Cx, 0x1C, 32, mm::WO> {
        struct ADDRCF : mm::Field<ICR, 3, 1> {};
        struct NACKCF : mm::Field<ICR, 4, 1> {};
        struct STOPCF : mm::Field<ICR, 5, 1> {};
        struct BERRCF : mm::Field<ICR, 8, 1> {};
        struct ARLOCF : mm::Field<ICR, 9, 1> {};
        struct OVRCF : mm::Field<ICR, 10, 1> {};
    };

    struct TXDR : mm::Register<I2Cx, 0x28, 32> {};
    struct RXDR : mm::Register<I2Cx, 0x24, 32, mm::RO> {};
};

// I2C instances
using I2C1 = I2Cx<0x4000'5400>;
using I2C2 = I2Cx<0x4000'5800>;
using I2C3 = I2Cx<0x4000'5C00>;
using I2C4 = I2Cx<0x5800'1C00>;

// NOLINTEND(readability-identifier-naming)

} // namespace umi::stm32h7

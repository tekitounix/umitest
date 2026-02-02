// SPDX-License-Identifier: MIT
// STM32H750 SAI (Serial Audio Interface) Register Definitions
// Reference: RM0433 Rev 8, Section 51
#pragma once

#include <mmio/mmio.hh>

namespace umi::stm32h7 {

// NOLINTBEGIN(readability-identifier-naming)

// ============================================================================
// SAI Sub-block registers (Block A or Block B)
// ============================================================================

template <mm::Addr BaseAddr>
struct SAIBlock : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = BaseAddr;

    struct CR1 : mm::Register<SAIBlock, 0x00, 32> {
        struct MODE : mm::Field<CR1, 0, 2> {};
        struct PRTCFG : mm::Field<CR1, 2, 2> {};
        struct DS : mm::Field<CR1, 5, 3> {};
        struct LSBFIRST : mm::Field<CR1, 8, 1> {};
        struct CKSTR : mm::Field<CR1, 9, 1> {};
        struct SYNCEN : mm::Field<CR1, 10, 2> {};
        struct MONO : mm::Field<CR1, 12, 1> {};
        struct OUTDRIV : mm::Field<CR1, 13, 1> {};
        struct SAIEN : mm::Field<CR1, 16, 1> {};
        struct DMAEN : mm::Field<CR1, 17, 1> {};
        struct NODIV : mm::Field<CR1, 19, 1> {};
        struct MCKDIV : mm::Field<CR1, 20, 6> {};
        struct OSR : mm::Field<CR1, 26, 1> {};
        struct MCKEN : mm::Field<CR1, 27, 1> {};
    };

    struct CR2 : mm::Register<SAIBlock, 0x04, 32> {
        struct FTH : mm::Field<CR2, 0, 3> {};
        struct FFLUSH : mm::Field<CR2, 3, 1> {};
        struct TRIS : mm::Field<CR2, 4, 1> {};
        struct MUTE : mm::Field<CR2, 5, 1> {};
        struct MUTEVAL : mm::Field<CR2, 6, 1> {};
        struct MUTECNT : mm::Field<CR2, 7, 6> {};
        struct CPL : mm::Field<CR2, 13, 1> {};
        struct COMP : mm::Field<CR2, 14, 2> {};
    };

    struct FRCR : mm::Register<SAIBlock, 0x08, 32> {
        struct FRL : mm::Field<FRCR, 0, 8> {};
        struct FSALL : mm::Field<FRCR, 8, 7> {};
        struct FSDEF : mm::Field<FRCR, 16, 1> {};
        struct FSPOL : mm::Field<FRCR, 17, 1> {};
        struct FSOFF : mm::Field<FRCR, 18, 1> {};
    };

    struct SLOTR : mm::Register<SAIBlock, 0x0C, 32> {
        struct FBOFF : mm::Field<SLOTR, 0, 5> {};
        struct SLOTSZ : mm::Field<SLOTR, 6, 2> {};
        struct NBSLOT : mm::Field<SLOTR, 8, 4> {};
        struct SLOTEN : mm::Field<SLOTR, 16, 16> {};
    };

    struct IMR : mm::Register<SAIBlock, 0x10, 32> {
        struct OVRUDRIE : mm::Field<IMR, 0, 1> {};
        struct MUTEDETIE : mm::Field<IMR, 1, 1> {};
        struct WCKCFGIE : mm::Field<IMR, 2, 1> {};
        struct FREQIE : mm::Field<IMR, 3, 1> {};
        struct CNRDYIE : mm::Field<IMR, 4, 1> {};
        struct AFSDETIE : mm::Field<IMR, 5, 1> {};
        struct LFSDETIE : mm::Field<IMR, 6, 1> {};
    };

    struct SR : mm::Register<SAIBlock, 0x14, 32, mm::RO> {
        struct OVRUDR : mm::Field<SR, 0, 1> {};
        struct MUTEDET : mm::Field<SR, 1, 1> {};
        struct WCKCFG : mm::Field<SR, 2, 1> {};
        struct FREQ : mm::Field<SR, 3, 1> {};
        struct CNRDY : mm::Field<SR, 4, 1> {};
        struct AFSDET : mm::Field<SR, 5, 1> {};
        struct LFSDET : mm::Field<SR, 6, 1> {};
        struct FLVL : mm::Field<SR, 16, 3> {};
    };

    struct CLRFR : mm::Register<SAIBlock, 0x18, 32> {
        struct COVRUDR : mm::Field<CLRFR, 0, 1> {};
        struct CMUTEDET : mm::Field<CLRFR, 1, 1> {};
        struct CWCKCFG : mm::Field<CLRFR, 2, 1> {};
        struct CCNRDY : mm::Field<CLRFR, 4, 1> {};
        struct CAFSDET : mm::Field<CLRFR, 5, 1> {};
        struct CLFSDET : mm::Field<CLRFR, 6, 1> {};
    };

    struct DR : mm::Register<SAIBlock, 0x1C, 32> {};
};

// ============================================================================
// SAI Global Configuration Register
// ============================================================================

template <mm::Addr BaseAddr>
struct SAIGlobal : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = BaseAddr;

    struct GCR : mm::Register<SAIGlobal, 0x00, 32> {
        struct SYNCIN : mm::Field<GCR, 0, 2> {};
        struct SYNCOUT : mm::Field<GCR, 4, 2> {};
    };
};

// ============================================================================
// SAI instances
// ============================================================================

using SAI1   = SAIGlobal<0x4001'5800>;
using SAI1_A = SAIBlock<0x4001'5804>;
using SAI1_B = SAIBlock<0x4001'5824>;

// Mode constants
namespace sai_mode {
constexpr std::uint32_t MASTER_TX = 0b00;
constexpr std::uint32_t MASTER_RX = 0b01;
constexpr std::uint32_t SLAVE_TX  = 0b10;
constexpr std::uint32_t SLAVE_RX  = 0b11;
}

namespace sai_ds {
constexpr std::uint32_t DS_8BIT  = 0b010;
constexpr std::uint32_t DS_10BIT = 0b011;
constexpr std::uint32_t DS_16BIT = 0b100;
constexpr std::uint32_t DS_20BIT = 0b101;
constexpr std::uint32_t DS_24BIT = 0b110;
constexpr std::uint32_t DS_32BIT = 0b111;
}

namespace sai_sync {
constexpr std::uint32_t ASYNC    = 0b00;
constexpr std::uint32_t INTERNAL = 0b01;
}

namespace sai_slotsz {
constexpr std::uint32_t DATASIZE = 0b00;
constexpr std::uint32_t SZ_16BIT = 0b01;
constexpr std::uint32_t SZ_32BIT = 0b10;
}

namespace sai_fth {
constexpr std::uint32_t EMPTY     = 0b000;
constexpr std::uint32_t QUARTER   = 0b001;
constexpr std::uint32_t HALF      = 0b010;
constexpr std::uint32_t THREE_QTR = 0b011;
constexpr std::uint32_t FULL      = 0b100;
}

// NOLINTEND(readability-identifier-naming)

} // namespace umi::stm32h7

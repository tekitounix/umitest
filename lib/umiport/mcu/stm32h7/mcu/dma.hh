// SPDX-License-Identifier: MIT
// STM32H750 DMA and DMAMUX Register Definitions
// Reference: RM0433 Rev 8, Section 16 (DMA) and Section 17 (DMAMUX)
#pragma once

#include <mmio/mmio.hh>

namespace umi::stm32h7 {

// NOLINTBEGIN(readability-identifier-naming)

// ============================================================================
// DMA Stream registers
// ============================================================================

template <mm::Addr BaseAddr>
struct DMAStream : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = BaseAddr;

    struct CR : mm::Register<DMAStream, 0x00, 32> {
        struct EN : mm::Field<CR, 0, 1> {};
        struct DMEIE : mm::Field<CR, 1, 1> {};
        struct TEIE : mm::Field<CR, 2, 1> {};
        struct HTIE : mm::Field<CR, 3, 1> {};
        struct TCIE : mm::Field<CR, 4, 1> {};
        struct PFCTRL : mm::Field<CR, 5, 1> {};
        struct DIR : mm::Field<CR, 6, 2> {};
        struct CIRC : mm::Field<CR, 8, 1> {};
        struct PINC : mm::Field<CR, 9, 1> {};
        struct MINC : mm::Field<CR, 10, 1> {};
        struct PSIZE : mm::Field<CR, 11, 2> {};
        struct MSIZE : mm::Field<CR, 13, 2> {};
        struct PL : mm::Field<CR, 16, 2> {};
        struct DBM : mm::Field<CR, 18, 1> {};
        struct CT : mm::Field<CR, 19, 1> {};
    };

    struct NDTR : mm::Register<DMAStream, 0x04, 32> {};
    struct PAR : mm::Register<DMAStream, 0x08, 32> {};
    struct M0AR : mm::Register<DMAStream, 0x0C, 32> {};
    struct M1AR : mm::Register<DMAStream, 0x10, 32> {};

    struct FCR : mm::Register<DMAStream, 0x14, 32> {
        struct FTH : mm::Field<FCR, 0, 2> {};
        struct DMDIS : mm::Field<FCR, 2, 1> {};
        struct FS : mm::Field<FCR, 3, 3> {};
        struct FEIE : mm::Field<FCR, 7, 1> {};
    };
};

// ============================================================================
// DMA Controller (interrupt status/clear)
// ============================================================================

template <mm::Addr BaseAddr>
struct DMAController : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = BaseAddr;

    struct LISR : mm::Register<DMAController, 0x00, 32, mm::RO> {};
    struct HISR : mm::Register<DMAController, 0x04, 32, mm::RO> {};
    struct LIFCR : mm::Register<DMAController, 0x08, 32> {};
    struct HIFCR : mm::Register<DMAController, 0x0C, 32> {};
};

// ============================================================================
// DMAMUX Channel Configuration
// ============================================================================

template <mm::Addr BaseAddr>
struct DMAmuxChannel : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = BaseAddr;

    struct CCR : mm::Register<DMAmuxChannel, 0x00, 32> {
        struct DMAREQ_ID : mm::Field<CCR, 0, 8> {};
        struct SOIE : mm::Field<CCR, 8, 1> {};
        struct EGE : mm::Field<CCR, 9, 1> {};
        struct SE : mm::Field<CCR, 16, 1> {};
        struct SPOL : mm::Field<CCR, 17, 2> {};
        struct NBREQ : mm::Field<CCR, 19, 5> {};
        struct SYNC_ID : mm::Field<CCR, 24, 5> {};
    };
};

// ============================================================================
// Instances
// ============================================================================

using DMA1 = DMAController<0x4002'0000>;

using DMA1_Stream0 = DMAStream<0x4002'0010>;
using DMA1_Stream1 = DMAStream<0x4002'0028>;
using DMA1_Stream2 = DMAStream<0x4002'0040>;
using DMA1_Stream3 = DMAStream<0x4002'0058>;
using DMA1_Stream4 = DMAStream<0x4002'0070>;
using DMA1_Stream5 = DMAStream<0x4002'0088>;
using DMA1_Stream6 = DMAStream<0x4002'00A0>;
using DMA1_Stream7 = DMAStream<0x4002'00B8>;

using DMAMUX1_Ch0 = DMAmuxChannel<0x4002'0800>;
using DMAMUX1_Ch1 = DMAmuxChannel<0x4002'0804>;

// ============================================================================
// Constants
// ============================================================================

namespace dma_dir {
constexpr std::uint32_t PERIPH_TO_MEM = 0b00;
constexpr std::uint32_t MEM_TO_PERIPH = 0b01;
constexpr std::uint32_t MEM_TO_MEM    = 0b10;
}

namespace dma_size {
constexpr std::uint32_t BYTE     = 0b00;
constexpr std::uint32_t HALFWORD = 0b01;
constexpr std::uint32_t WORD     = 0b10;
}

namespace dma_pl {
constexpr std::uint32_t LOW       = 0b00;
constexpr std::uint32_t MEDIUM    = 0b01;
constexpr std::uint32_t HIGH      = 0b10;
constexpr std::uint32_t VERY_HIGH = 0b11;
}

namespace dmamux_req {
constexpr std::uint32_t SAI1_A = 87;
constexpr std::uint32_t SAI1_B = 88;
}

namespace dma_flags {
constexpr std::uint32_t S0_FEIF  = 1U << 0;
constexpr std::uint32_t S0_DMEIF = 1U << 2;
constexpr std::uint32_t S0_TEIF  = 1U << 3;
constexpr std::uint32_t S0_HTIF  = 1U << 4;
constexpr std::uint32_t S0_TCIF  = 1U << 5;
constexpr std::uint32_t S0_ALL   = S0_FEIF | S0_DMEIF | S0_TEIF | S0_HTIF | S0_TCIF;

constexpr std::uint32_t S1_FEIF  = 1U << 6;
constexpr std::uint32_t S1_DMEIF = 1U << 8;
constexpr std::uint32_t S1_TEIF  = 1U << 9;
constexpr std::uint32_t S1_HTIF  = 1U << 10;
constexpr std::uint32_t S1_TCIF  = 1U << 11;
constexpr std::uint32_t S1_ALL   = S1_FEIF | S1_DMEIF | S1_TEIF | S1_HTIF | S1_TCIF;
}

// NOLINTEND(readability-identifier-naming)

} // namespace umi::stm32h7

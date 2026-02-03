// SPDX-License-Identifier: MIT
// STM32H750 ADC Register Definitions
// Reference: RM0433 Rev 8, Section 25 (ADC)
#pragma once

#include <umimmio.hh>

namespace umi::stm32h7 {

// NOLINTBEGIN(readability-identifier-naming)

// ============================================================================
// ADC instance registers
// ============================================================================

template <mm::Addr BaseAddr>
struct ADCx : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = BaseAddr;

    struct ISR : mm::Register<ADCx, 0x00, 32> {
        struct ADRDY : mm::Field<ISR, 0, 1> {};
        struct EOSMP : mm::Field<ISR, 1, 1> {};
        struct EOC : mm::Field<ISR, 2, 1> {};
        struct EOS : mm::Field<ISR, 3, 1> {};
        struct OVR : mm::Field<ISR, 4, 1> {};
    };

    struct IER : mm::Register<ADCx, 0x04, 32> {
        struct ADRDYIE : mm::Field<IER, 0, 1> {};
        struct EOCIE : mm::Field<IER, 2, 1> {};
        struct EOSIE : mm::Field<IER, 3, 1> {};
        struct OVRIE : mm::Field<IER, 4, 1> {};
    };

    struct CR : mm::Register<ADCx, 0x08, 32> {
        struct ADEN : mm::Field<CR, 0, 1> {};
        struct ADDIS : mm::Field<CR, 1, 1> {};
        struct ADSTART : mm::Field<CR, 2, 1> {};
        struct ADSTP : mm::Field<CR, 4, 1> {};
        struct BOOST : mm::Field<CR, 8, 2> {};       // Boost mode (00=6.25MHz, 11=50MHz)
        struct ADCALLIN : mm::Field<CR, 16, 1> {};   // Linearity calibration
        struct ADCALDIF : mm::Field<CR, 30, 1> {};   // Differential calibration
        struct ADCAL : mm::Field<CR, 31, 1> {};      // Start calibration
        struct ADVREGEN : mm::Field<CR, 28, 1> {};   // Voltage regulator enable
        struct DEEPPWD : mm::Field<CR, 29, 1> {};    // Deep power down
    };

    struct CFGR : mm::Register<ADCx, 0x0C, 32> {
        struct DMNGT : mm::Field<CFGR, 0, 2> {};     // Data management config
        struct RES : mm::Field<CFGR, 2, 3> {};       // Resolution (H7: 3 bits)
        struct EXTSEL : mm::Field<CFGR, 5, 5> {};    // External trigger selection
        struct EXTEN : mm::Field<CFGR, 10, 2> {};    // External trigger enable
        struct OVRMOD : mm::Field<CFGR, 12, 1> {};   // Overrun mode
        struct CONT : mm::Field<CFGR, 13, 1> {};     // Continuous conversion
        struct AUTDLY : mm::Field<CFGR, 14, 1> {};   // Auto-delayed conversion
        struct DISCEN : mm::Field<CFGR, 16, 1> {};   // Discontinuous mode
        struct DISCNUM : mm::Field<CFGR, 17, 3> {};
    };

    struct CFGR2 : mm::Register<ADCx, 0x10, 32> {
        struct ROVSE : mm::Field<CFGR2, 0, 1> {};    // Regular oversampling enable
        struct ROVSM : mm::Field<CFGR2, 10, 1> {};   // Oversampling mode (continued/resumed)
        struct RSHIFT1 : mm::Field<CFGR2, 11, 1> {}; // Right-shift data
        struct OSVR : mm::Field<CFGR2, 16, 10> {};   // Oversampling ratio (0-1023)
        struct LSHIFT : mm::Field<CFGR2, 28, 4> {};  // Left bit shift
        struct OVSS : mm::Field<CFGR2, 5, 4> {};     // Oversampling shift (right-shift bits)
        struct TROVS : mm::Field<CFGR2, 9, 1> {};    // Triggered oversampling
    };

    struct SMPR1 : mm::Register<ADCx, 0x14, 32> {};  // Sample time ch0-9 (3 bits each)
    struct SMPR2 : mm::Register<ADCx, 0x18, 32> {};  // Sample time ch10-19 (3 bits each)

    struct PCSEL : mm::Register<ADCx, 0x1C, 32> {};  // Pre-channel selection (1 bit per ch)

    struct LTR1 : mm::Register<ADCx, 0x20, 32> {};   // Watchdog lower threshold 1
    struct HTR1 : mm::Register<ADCx, 0x24, 32> {};   // Watchdog upper threshold 1

    struct SQR1 : mm::Register<ADCx, 0x30, 32> {
        struct L : mm::Field<SQR1, 0, 4> {};         // Regular sequence length (0-based)
        struct SQ1 : mm::Field<SQR1, 6, 5> {};
        struct SQ2 : mm::Field<SQR1, 12, 5> {};
        struct SQ3 : mm::Field<SQR1, 18, 5> {};
        struct SQ4 : mm::Field<SQR1, 24, 5> {};
    };

    struct SQR2 : mm::Register<ADCx, 0x34, 32> {};
    struct SQR3 : mm::Register<ADCx, 0x38, 32> {};
    struct SQR4 : mm::Register<ADCx, 0x3C, 32> {};

    struct DR : mm::Register<ADCx, 0x40, 32, mm::RO> {};  // Regular data register

    struct DIFSEL : mm::Register<ADCx, 0xC0, 32> {};  // Differential mode selection
    struct CALFACT : mm::Register<ADCx, 0xC4, 32> {}; // Calibration factors
    struct CALFACT2 : mm::Register<ADCx, 0xC8, 32> {}; // Linearity calibration factors
};

// ============================================================================
// ADC Common registers (shared between ADC1/ADC2)
// ============================================================================

struct ADC12_Common : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0x4002'2300;

    struct CSR : mm::Register<ADC12_Common, 0x00, 32, mm::RO> {};  // Common status
    struct CCR : mm::Register<ADC12_Common, 0x08, 32> {
        struct CKMODE : mm::Field<CCR, 16, 2> {};    // ADC clock mode
        struct PRESC : mm::Field<CCR, 18, 4> {};     // Clock prescaler
        struct VREFEN : mm::Field<CCR, 22, 1> {};    // Vrefint enable
        struct TSEN : mm::Field<CCR, 23, 1> {};      // Temperature sensor enable
        struct VBATEN : mm::Field<CCR, 24, 1> {};    // VBAT enable
    };
    struct CDR : mm::Register<ADC12_Common, 0x0C, 32, mm::RO> {};  // Common regular data
};

// ============================================================================
// Instances
// ============================================================================

using ADC1 = ADCx<0x4002'2000>;
using ADC2 = ADCx<0x4002'2100>;

// ============================================================================
// Constants
// ============================================================================

// DMNGT: Data management configuration
namespace adc_dmngt {
constexpr std::uint32_t DR_STORE = 0b00;          // Data stored in DR only
constexpr std::uint32_t DMA_ONESHOT = 0b01;       // DMA one-shot mode
constexpr std::uint32_t DFSDM = 0b10;             // DFSDM mode
constexpr std::uint32_t DMA_CIRCULAR = 0b11;      // DMA circular mode
} // namespace adc_dmngt

// RES: Resolution
namespace adc_res {
constexpr std::uint32_t BITS_16 = 0b000;
constexpr std::uint32_t BITS_14 = 0b001;
constexpr std::uint32_t BITS_12 = 0b010;
constexpr std::uint32_t BITS_10 = 0b011;
constexpr std::uint32_t BITS_8 = 0b111;
} // namespace adc_res

// Sampling time values (3 bits, used in SMPR1/SMPR2)
namespace adc_smp {
constexpr std::uint32_t CYCLES_1_5 = 0b000;
constexpr std::uint32_t CYCLES_2_5 = 0b001;
constexpr std::uint32_t CYCLES_8_5 = 0b010;
constexpr std::uint32_t CYCLES_16_5 = 0b011;
constexpr std::uint32_t CYCLES_32_5 = 0b100;
constexpr std::uint32_t CYCLES_64_5 = 0b101;
constexpr std::uint32_t CYCLES_387_5 = 0b110;
constexpr std::uint32_t CYCLES_810_5 = 0b111;
} // namespace adc_smp

// CCR CKMODE: Clock mode
namespace adc_ckmode {
constexpr std::uint32_t ASYNC = 0b00;             // Asynchronous clock (from PLL)
constexpr std::uint32_t SYNC_DIV1 = 0b01;         // Synchronous AHB/1
constexpr std::uint32_t SYNC_DIV2 = 0b10;         // Synchronous AHB/2
constexpr std::uint32_t SYNC_DIV4 = 0b11;         // Synchronous AHB/4
} // namespace adc_ckmode

// CCR PRESC: Async clock prescaler
namespace adc_presc {
constexpr std::uint32_t DIV1 = 0b0000;
constexpr std::uint32_t DIV2 = 0b0001;
constexpr std::uint32_t DIV4 = 0b0010;
constexpr std::uint32_t DIV6 = 0b0011;
constexpr std::uint32_t DIV8 = 0b0100;
constexpr std::uint32_t DIV10 = 0b0101;
constexpr std::uint32_t DIV12 = 0b0110;
constexpr std::uint32_t DIV16 = 0b0111;
constexpr std::uint32_t DIV32 = 0b1000;
constexpr std::uint32_t DIV64 = 0b1001;
constexpr std::uint32_t DIV128 = 0b1010;
constexpr std::uint32_t DIV256 = 0b1011;
} // namespace adc_presc

// DMAMUX request IDs for ADC (add to dmamux_req namespace from dma.hh)
namespace dmamux_req {
constexpr std::uint32_t ADC1_REQ = 9;
constexpr std::uint32_t ADC2_REQ = 10;
} // namespace dmamux_req

// NOLINTEND(readability-identifier-naming)

} // namespace umi::stm32h7

// SPDX-License-Identifier: MIT
// STM32H750 USART registers (USART1 for MIDI)
// Reference: RM0433 Rev 8, Section 39
#pragma once

#include <umimmio.hh>

namespace umi::stm32h7 {

// NOLINTBEGIN(readability-identifier-naming)

/// USART1 (APB2, 0x4001'1000) — used for TRS MIDI on Daisy Seed
struct USART1 : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0x4001'1000;

    struct CR1 : mm::Register<USART1, 0x00, 32> {
        struct UE     : mm::Field<CR1, 0, 1> {};   // USART enable
        struct RE     : mm::Field<CR1, 2, 1> {};   // Receiver enable
        struct TE     : mm::Field<CR1, 3, 1> {};   // Transmitter enable
        struct RXNEIE : mm::Field<CR1, 5, 1> {};   // RXNE interrupt enable
        struct TCIE   : mm::Field<CR1, 6, 1> {};   // TC interrupt enable
        struct TXEIE  : mm::Field<CR1, 7, 1> {};   // TXE interrupt enable
        struct OVER8  : mm::Field<CR1, 15, 1> {};  // Oversampling mode
        struct FIFOEN : mm::Field<CR1, 29, 1> {};  // FIFO mode enable
    };

    struct CR2 : mm::Register<USART1, 0x04, 32> {
        struct STOP : mm::Field<CR2, 12, 2> {};    // Stop bits
    };

    struct CR3 : mm::Register<USART1, 0x08, 32> {
        struct EIE   : mm::Field<CR3, 0, 1> {};    // Error interrupt enable
        struct DMAR  : mm::Field<CR3, 6, 1> {};    // DMA enable receiver
        struct DMAT  : mm::Field<CR3, 7, 1> {};    // DMA enable transmitter
    };

    struct BRR : mm::Register<USART1, 0x0C, 32> {};

    struct ISR : mm::Register<USART1, 0x1C, 32, mm::RO> {
        struct RXNE : mm::Field<ISR, 5, 1> {};     // Read data register not empty
        struct TC   : mm::Field<ISR, 6, 1> {};     // Transmission complete
        struct TXE  : mm::Field<ISR, 7, 1> {};     // Transmit data register empty
        struct ORE  : mm::Field<ISR, 3, 1> {};     // Overrun error
        struct FE   : mm::Field<ISR, 1, 1> {};     // Framing error
        struct TEACK : mm::Field<ISR, 21, 1> {};   // Transmit enable acknowledge
        struct REACK : mm::Field<ISR, 22, 1> {};   // Receive enable acknowledge
    };

    struct ICR : mm::Register<USART1, 0x20, 32> {
        struct ORECF : mm::Field<ICR, 3, 1> {};    // Overrun error clear
        struct FECF  : mm::Field<ICR, 1, 1> {};    // Framing error clear
        struct TCCF  : mm::Field<ICR, 6, 1> {};    // TC clear
    };

    struct RDR : mm::Register<USART1, 0x24, 32, mm::RO> {};
    struct TDR : mm::Register<USART1, 0x28, 32> {};
};

// NOLINTEND(readability-identifier-naming)

} // namespace umi::stm32h7

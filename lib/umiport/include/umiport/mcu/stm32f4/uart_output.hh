// SPDX-License-Identifier: MIT
// Copyright (c) 2026, tekitounix
/// @file
/// @brief STM32F4 USART1 output backend for Renode simulation.
/// @author Shota Moriguchi @tekitounix
#pragma once

#include <cstdint>
#include <umimmio/mmio.hh>

namespace umi::port::stm32f4 {

namespace mm = umi::mmio;

/// @brief Minimal USART1 output backend for STM32F4/Renode.
///
/// Provides init() and putc() for routing stdout to USART1.
/// Used by all UMI library test platforms on STM32F4.
struct RenodeUartOutput {
    /// @brief Enable USART1 clock and transmitter.
    static void init() {
        transport().modify(RCC::APB2ENR::USART1EN::Set{});
        transport().modify(USART1::CR1::UE::Set{}, USART1::CR1::TE::Set{});
    }

    /// @brief Transmit one character via USART1.
    /// @param c Character to transmit.
    static void putc(char c) {
        while (!transport().is(USART1::SR::TXE::Set{})) {
        }
        transport().write(USART1::DR::value(static_cast<std::uint32_t>(c)));
    }

  private:
    struct RCC : mm::Device<mm::RW, mm::DirectTransportTag> {
        static constexpr mm::Addr base_address = 0x40023800;

        struct APB2ENR : mm::Register<RCC, 0x44, 32> {
            struct USART1EN : mm::Field<APB2ENR, 4, 1> {};
        };
    };

    struct USART1 : mm::Device<mm::RW, mm::DirectTransportTag> {
        static constexpr mm::Addr base_address = 0x40011000;

        struct SR : mm::Register<USART1, 0x00, 32, mm::RO> {
            struct TXE : mm::Field<SR, 7, 1> {};
        };

        struct DR : mm::Register<USART1, 0x04, 32> {};

        struct CR1 : mm::Register<USART1, 0x0C, 32> {
            struct UE : mm::Field<CR1, 13, 1> {};
            struct TE : mm::Field<CR1, 3, 1> {};
        };
    };

    static mm::DirectTransport<>& transport() {
        static mm::DirectTransport<> transport;
        return transport;
    }
};

} // namespace umi::port::stm32f4

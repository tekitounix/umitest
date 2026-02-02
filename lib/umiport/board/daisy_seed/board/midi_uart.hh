// SPDX-License-Identifier: MIT
// Daisy Seed MIDI UART driver (USART1, 31250 baud)
// PB6 = TX (AF7), PB7 = RX (AF7)
#pragma once

#include <cstdint>
#include <mcu/rcc.hh>
#include <mcu/gpio.hh>
#include <mcu/usart.hh>
#include <mmio/transport/direct.hh>

namespace umi::daisy {

/// Initialize USART1 GPIO pins for MIDI (PB6=TX, PB7=RX, AF7)
inline void init_midi_gpio() {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    // Enable GPIOB clock (likely already enabled)
    t.modify(RCC::AHB4ENR::GPIOBEN::Set{});
    [[maybe_unused]] auto dummy = t.read(RCC::AHB4ENR{});

    constexpr std::uint8_t AF7 = 7;

    // PB6 = USART1_TX
    gpio_configure_pin<GPIOB>(t, 6, gpio_mode::ALTERNATE, gpio_otype::PUSH_PULL,
                               gpio_speed::HIGH, gpio_pupd::NONE);
    gpio_set_af<GPIOB>(t, 6, AF7);

    // PB7 = USART1_RX
    gpio_configure_pin<GPIOB>(t, 7, gpio_mode::ALTERNATE, gpio_otype::PUSH_PULL,
                               gpio_speed::HIGH, gpio_pupd::PULL_UP);
    gpio_set_af<GPIOB>(t, 7, AF7);
}

/// Initialize USART1 for MIDI (31250 baud, 8N1)
/// Assumes USART1 kernel clock is APB2 = 120 MHz
inline void init_midi_uart() {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    // Enable USART1 clock (APB2)
    t.modify(RCC::APB2ENR::USART1EN::Set{});
    [[maybe_unused]] auto dummy = t.read(RCC::APB2ENR{});

    init_midi_gpio();

    // Disable USART before configuration
    t.write(USART1::CR1::value(0));
    t.write(USART1::CR2::value(0));  // 1 stop bit
    t.write(USART1::CR3::value(0));

    // BRR = fck / baud = 120000000 / 31250 = 3840
    t.write(USART1::BRR::value(3840));

    // Enable USART: UE + TE + RE + RXNEIE (receive interrupt)
    t.write(USART1::CR1::value(
        (1U << 0) |   // UE
        (1U << 2) |   // RE
        (1U << 3) |   // TE
        (1U << 5)     // RXNEIE
    ));
}

/// Send one MIDI byte (blocking, waits for TXE)
inline void midi_uart_send_byte(std::uint8_t byte) {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;
    while (!(t.read(USART1::ISR{}) & (1U << 7))) {}  // Wait TXE
    t.write(USART1::TDR::value(byte));
}

/// Send a 3-byte MIDI message
inline void midi_uart_send(std::uint8_t status, std::uint8_t data1, std::uint8_t data2) {
    midi_uart_send_byte(status);
    midi_uart_send_byte(data1);
    midi_uart_send_byte(data2);
}

/// Read one byte from USART1 RDR (call only when RXNE is set)
inline std::uint8_t midi_uart_read_byte() {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;
    return static_cast<std::uint8_t>(t.read(USART1::RDR{}) & 0xFF);
}

/// Check if USART1 has received data (RXNE flag)
inline bool midi_uart_rx_ready() {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;
    return (t.read(USART1::ISR{}) & (1U << 5)) != 0;
}

/// Clear overrun error (must clear to continue receiving)
inline void midi_uart_clear_errors() {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;
    // Clear ORE and FE
    t.write(USART1::ICR::value((1U << 3) | (1U << 1)));
}

/// Simple MIDI UART parser state machine
struct MidiUartParser {
    std::uint8_t running_status = 0;
    std::uint8_t data[2] = {};
    std::uint8_t data_count = 0;
    std::uint8_t expected = 0;

    // Returns true when a complete message is ready
    // On return true, running_status + data[0..expected-1] contain the message
    bool feed(std::uint8_t byte) {
        if (byte & 0x80) {
            // Status byte
            if (byte >= 0xF8) return false;  // Realtime — ignore for now
            if (byte >= 0xF0) {
                running_status = 0;  // System common cancels running status
                return false;
            }
            running_status = byte;
            data_count = 0;
            std::uint8_t cmd = byte & 0xF0;
            expected = (cmd == 0xC0 || cmd == 0xD0) ? 1 : 2;
            return false;
        }

        // Data byte
        if (running_status == 0) return false;
        data[data_count++] = byte;
        if (data_count >= expected) {
            data_count = 0;
            return true;
        }
        return false;
    }
};

} // namespace umi::daisy

// SPDX-License-Identifier: MIT
// UMI-OS UART Driver for STM32F4
#pragma once

#include <cstdint>
#include "../../kernel/driver.hh"

namespace umi::driver::uart {

// ============================================================================
// STM32F4 USART Registers
// ============================================================================

namespace reg {
// USART2 base address (STM32F4)
inline constexpr uint32_t USART2_BASE = 0x40004400;

// Register offsets
inline constexpr uint32_t SR = 0x00;   // Status
inline constexpr uint32_t DR = 0x04;   // Data
inline constexpr uint32_t BRR = 0x08;  // Baud rate
inline constexpr uint32_t CR1 = 0x0C;  // Control 1
inline constexpr uint32_t CR2 = 0x10;  // Control 2
inline constexpr uint32_t CR3 = 0x14;  // Control 3

// SR bits
inline constexpr uint32_t SR_RXNE = (1U << 5);  // RX not empty
inline constexpr uint32_t SR_TXE = (1U << 7);   // TX empty
inline constexpr uint32_t SR_TC = (1U << 6);    // Transmission complete

// CR1 bits
inline constexpr uint32_t CR1_RE = (1U << 2);     // RX enable
inline constexpr uint32_t CR1_TE = (1U << 3);     // TX enable
inline constexpr uint32_t CR1_RXNEIE = (1U << 5); // RX interrupt enable
inline constexpr uint32_t CR1_UE = (1U << 13);    // USART enable

// RCC registers for enabling USART clock
inline constexpr uint32_t RCC_BASE = 0x40023800;
inline constexpr uint32_t RCC_APB1ENR = RCC_BASE + 0x40;
inline constexpr uint32_t RCC_APB1ENR_USART2EN = (1U << 17);

inline volatile uint32_t& usart_reg(uint32_t offset) {
    return *reinterpret_cast<volatile uint32_t*>(USART2_BASE + offset);
}
}  // namespace reg

// ============================================================================
// UART Driver State
// ============================================================================

struct State {
    uint32_t baud_rate = 115200;
    uint32_t apb1_hz = 42000000;  // APB1 clock (PCLK1) for USART2

    // RX callback
    void (*on_rx)(uint8_t data, void* ctx) = nullptr;
    void* rx_ctx = nullptr;
};

inline State g_state;

// ============================================================================
// Driver Implementation
// ============================================================================

inline int init(const void* config) {
    if (config) {
        auto* cfg = static_cast<const UartConfig*>(config);
        g_state.baud_rate = cfg->baud_rate;
    }

    // Enable USART2 clock
    volatile uint32_t& apb1enr = *reinterpret_cast<volatile uint32_t*>(reg::RCC_APB1ENR);
    apb1enr |= reg::RCC_APB1ENR_USART2EN;

    // Calculate baud rate divisor
    // BRR = fck / baud (for oversampling by 16)
    uint32_t brr = (g_state.apb1_hz + g_state.baud_rate / 2) / g_state.baud_rate;
    reg::usart_reg(reg::BRR) = brr;

    // Configure: 8N1, TX/RX enable
    reg::usart_reg(reg::CR1) = reg::CR1_TE | reg::CR1_RE | reg::CR1_UE;
    reg::usart_reg(reg::CR2) = 0;  // 1 stop bit
    reg::usart_reg(reg::CR3) = 0;

    return 0;
}

inline void deinit() {
    reg::usart_reg(reg::CR1) = 0;
}

inline void irq(uint32_t) {
    uint32_t sr = reg::usart_reg(reg::SR);

    if (sr & reg::SR_RXNE) {
        uint8_t data = static_cast<uint8_t>(reg::usart_reg(reg::DR));
        if (g_state.on_rx) {
            g_state.on_rx(data, g_state.rx_ctx);
        }
    }
}

/// Send a single character (blocking)
inline void putc(char c) {
    // Wait for TX empty
    while (!(reg::usart_reg(reg::SR) & reg::SR_TXE)) {}
    reg::usart_reg(reg::DR) = static_cast<uint32_t>(c);
}

/// Send a string (blocking)
inline void puts(const char* s) {
    while (*s) {
        if (*s == '\n') {
            putc('\r');
        }
        putc(*s++);
    }
}

/// Check if RX data available
inline bool rx_available() {
    return (reg::usart_reg(reg::SR) & reg::SR_RXNE) != 0;
}

/// Get received character (non-blocking, returns -1 if none)
inline int getc() {
    if (rx_available()) {
        return static_cast<int>(reg::usart_reg(reg::DR) & 0xFF);
    }
    return -1;
}

/// Set RX callback
inline void set_rx_callback(void (*cb)(uint8_t, void*), void* ctx) {
    g_state.on_rx = cb;
    g_state.rx_ctx = ctx;

    // Enable RX interrupt
    if (cb) {
        reg::usart_reg(reg::CR1) |= reg::CR1_RXNEIE;
    } else {
        reg::usart_reg(reg::CR1) &= ~reg::CR1_RXNEIE;
    }
}

// Driver operations table
inline const Ops kOps = {
    .name = "uart",
    .category = Category::UART,
    .init = init,
    .deinit = deinit,
    .irq = irq,
};

}  // namespace umi::driver::uart

// Global function for svc_handler.hh
extern "C" inline void uart_puts(const char* s) {
    umi::driver::uart::puts(s);
}

// SPDX-License-Identifier: MIT
// UMI-OS STM32F4 UART/USART
#pragma once

#include <cstdint>

namespace umi::port::stm32 {

/// STM32F4 USART base addresses
enum class USARTPort : uint32_t {
    USART1 = 0x40011000,
    USART2 = 0x40004400,
    USART3 = 0x40004800,
    UART4  = 0x40004C00,
    UART5  = 0x40005000,
    USART6 = 0x40011400,
};

/// STM32F4 USART register access
struct UART {
    struct Regs {
        volatile uint32_t SR;
        volatile uint32_t DR;
        volatile uint32_t BRR;
        volatile uint32_t CR1;
        volatile uint32_t CR2;
        volatile uint32_t CR3;
        volatile uint32_t GTPR;
    };

    // Status register bits
    static constexpr uint32_t SR_PE   = (1 << 0);   // Parity error
    static constexpr uint32_t SR_FE   = (1 << 1);   // Framing error
    static constexpr uint32_t SR_NE   = (1 << 2);   // Noise error
    static constexpr uint32_t SR_ORE  = (1 << 3);   // Overrun error
    static constexpr uint32_t SR_IDLE = (1 << 4);   // IDLE line detected
    static constexpr uint32_t SR_RXNE = (1 << 5);   // Read data register not empty
    static constexpr uint32_t SR_TC   = (1 << 6);   // Transmission complete
    static constexpr uint32_t SR_TXE  = (1 << 7);   // Transmit data register empty

    // Control register 1 bits
    static constexpr uint32_t CR1_SBK    = (1 << 0);
    static constexpr uint32_t CR1_RWU    = (1 << 1);
    static constexpr uint32_t CR1_RE     = (1 << 2);   // Receiver enable
    static constexpr uint32_t CR1_TE     = (1 << 3);   // Transmitter enable
    static constexpr uint32_t CR1_IDLEIE = (1 << 4);
    static constexpr uint32_t CR1_RXNEIE = (1 << 5);   // RXNE interrupt enable
    static constexpr uint32_t CR1_TCIE   = (1 << 6);
    static constexpr uint32_t CR1_TXEIE  = (1 << 7);
    static constexpr uint32_t CR1_UE     = (1 << 13);  // USART enable

    static Regs* port(USARTPort p) {
        return reinterpret_cast<Regs*>(static_cast<uint32_t>(p));
    }

    /// Initialize UART
    /// @param p USART port
    /// @param pclk Peripheral clock frequency in Hz
    /// @param baudrate Desired baud rate
    static void init(USARTPort p, uint32_t pclk, uint32_t baudrate) {
        auto* regs = port(p);
        
        // Calculate baud rate register value
        // BRR = pclk / baudrate
        regs->BRR = pclk / baudrate;
        
        // Enable USART, transmitter and receiver
        regs->CR1 = CR1_UE | CR1_TE | CR1_RE;
    }

    /// Send a character (blocking)
    static void putc(USARTPort p, char c) {
        auto* regs = port(p);
        while (!(regs->SR & SR_TXE)) {}
        regs->DR = c;
    }

    /// Send a string (blocking)
    static void puts(USARTPort p, const char* s) {
        while (*s) {
            putc(p, *s++);
        }
    }

    /// Receive a character (blocking)
    static char getc(USARTPort p) {
        auto* regs = port(p);
        while (!(regs->SR & SR_RXNE)) {}
        return static_cast<char>(regs->DR);
    }

    /// Check if data available
    static bool available(USARTPort p) {
        return (port(p)->SR & SR_RXNE) != 0;
    }

    /// Check if transmit buffer empty
    static bool tx_ready(USARTPort p) {
        return (port(p)->SR & SR_TXE) != 0;
    }
};

} // namespace umi::port::stm32

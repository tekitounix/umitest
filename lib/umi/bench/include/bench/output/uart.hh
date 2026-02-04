// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace umi::bench {

/// UART output for STM32 (USART2)
struct UartOutput {
    static constexpr std::uint32_t usart2_base = 0x40004400;
    static constexpr std::uint32_t rcc_apb1enr = 0x40023840;
    // Maximum cycles to wait for TXE (prevents infinite hang when UART is not connected)
    static constexpr std::uint32_t max_txe_wait_cycles = 10000;

    static void init() {
        // Enable USART2 clock
        *reinterpret_cast<volatile std::uint32_t*>(rcc_apb1enr) |= (1u << 17);
        // Enable USART, TX
        *reinterpret_cast<volatile std::uint32_t*>(usart2_base + 0x0C) = (1u << 13) | (1u << 3);
    }

    /// Check if UART is ready for transmission (TXE bit set)
    static bool is_ready() { return (*reinterpret_cast<volatile std::uint32_t*>(usart2_base) & 0x80) != 0; }

    /// Write a character with timeout protection
    /// @return true if successful, false if timeout (UART not ready)
    static bool putc(char c) {
        // Wait for TXE with timeout to prevent infinite hang
        std::uint32_t timeout = max_txe_wait_cycles;
        while (!is_ready() && timeout > 0) {
            --timeout;
        }
        if (timeout == 0) {
            return false; // Timeout - UART not ready
        }
        *reinterpret_cast<volatile std::uint32_t*>(usart2_base + 0x04) = c;
        return true;
    }

    /// Non-blocking character write
    /// @return true if character was written, false if UART not ready
    static bool putc_nonblocking(char c) {
        if (!is_ready()) {
            return false;
        }
        *reinterpret_cast<volatile std::uint32_t*>(usart2_base + 0x04) = c;
        return true;
    }

    static void puts(const char* s) {
        while (*s) {
            if (!putc(*s++)) {
                break; // Stop on timeout
            }
        }
    }

    /// Check if UART output is functional (not in timeout state)
    static bool is_functional() { return is_ready(); }

    static bool print_uint(std::uint32_t value) {
        if (value == 0) {
            return putc('0');
        }
        char buf[12];
        int i = 0;
        while (value > 0) {
            buf[i++] = static_cast<char>('0' + (value % 10));
            value /= 10;
        }
        bool ok = true;
        while (i-- && ok) {
            ok = putc(buf[i]);
        }
        return ok;
    }
};

} // namespace umi::bench

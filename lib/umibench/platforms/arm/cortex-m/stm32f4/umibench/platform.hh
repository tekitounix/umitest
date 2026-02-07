// SPDX-License-Identifier: MIT
#pragma once

/// @file
/// @brief STM32F4 platform binding with DWT timer and USART1 output.

#include <cstdint>
#include <umiport/stm32f4/uart_output.hh>

#include "../../dwt.hh"

namespace umi::bench::target {

/// @brief Extended USART1 output backend for benchmarks.
///
/// Delegates to umiport's RenodeUartOutput for init/putc, and adds
/// formatted print methods needed by the benchmark framework.
struct BenchUartOutput {
    /// @brief Enable USART1 clock and transmitter.
    static void init() { umi::port::stm32f4::RenodeUartOutput::init(); }

    /// @brief Transmit one character via USART1.
    /// @param c Character to transmit.
    static void putc(char c) { umi::port::stm32f4::RenodeUartOutput::putc(c); }

    /// @brief Transmit a null-terminated string.
    /// @param s String to transmit.
    static void puts(const char* s) {
        while (*s != '\0') {
            putc(*s++);
        }
    }

    /// @brief Print an unsigned integer in decimal.
    /// @param value Value to print.
    static void print_uint(std::uint64_t value) {
        if (value == 0) {
            putc('0');
            return;
        }
        char buf[21];
        int i = 0;
        while (value > 0) {
            buf[i++] = static_cast<char>('0' + (value % 10));
            value /= 10;
        }
        while (i-- > 0) {
            putc(buf[i]);
        }
    }

    /// @brief Print a floating-point value with two decimals.
    /// @param value Value to print.
    static void print_double(double value) {
        if (value < 0.0) {
            putc('-');
            value = -value;
        }
        auto integer_part = static_cast<std::uint64_t>(value);
        print_uint(integer_part);
        putc('.');
        double frac = value - static_cast<double>(integer_part);
        // 2 decimal places
        auto frac_int = static_cast<std::uint64_t>((frac * 100.0) + 0.5);
        if (frac_int < 10) {
            putc('0');
        }
        print_uint(frac_int);
    }
};

/// @brief STM32F4 benchmark platform definition.
struct Platform {
    /// @brief Timer backend.
    using Timer = DwtTimer;
    /// @brief Output backend.
    using Output = BenchUartOutput;

    /// @brief Platform name shown in reports.
    /// @return `"stm32f4"`.
    static constexpr const char* target_name() { return "stm32f4"; }
    /// @brief Timer unit shown in reports.
    /// @return `"cy"`.
    static constexpr const char* timer_unit() { return "cy"; }

    /// @brief Initialize timer and output backend.
    static void init() {
        Timer::enable();
        Output::init();
    }

    /// @brief Halt the CPU in low-power wait-for-interrupt loop.
    [[noreturn]] static void halt() {
        while (true) {
            asm volatile("wfi");
        }
    }
};

} // namespace umi::bench::target

namespace umi::bench {
/// @brief Convenience alias to the selected target platform type.
using Platform = target::Platform;
} // namespace umi::bench

namespace umi::port {
/// @brief Platform alias for umiport startup/syscalls compatibility.
using Platform = umi::bench::target::Platform;
} // namespace umi::port

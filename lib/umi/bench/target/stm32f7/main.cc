// STM32F7 Target Main - Uses generic bench core
#include <cstdint>
#include "../../test/bench_core.cc"

// Target-specific DWT implementation (same address as M4)
struct DWT_Timer {
    static constexpr uint32_t BASE = 0xE0001000;
    static uint32_t now() {
        return *reinterpret_cast<volatile uint32_t*>(BASE + 4);
    }
    static void reset() {
        *reinterpret_cast<volatile uint32_t*>(BASE + 4) = 0;
    }
    static void enable() {
        *reinterpret_cast<volatile uint32_t*>(0xE000EDFC) |= 1u << 24;
        reset();
        *reinterpret_cast<volatile uint32_t*>(BASE) |= 1;
    }
};

// Target-specific UART implementation
struct UART_Console {
    static constexpr uint32_t USART2_BASE = 0x40004400;
    static constexpr uint32_t RCC_APB1ENR = 0x40023840;

    static void init() {
        *reinterpret_cast<volatile uint32_t*>(RCC_APB1ENR) |= (1 << 17);
        *reinterpret_cast<volatile uint32_t*>(USART2_BASE + 0x0C) = (1 << 13) | (1 << 3);
    }

    static void putc(char c) {
        while (!(*reinterpret_cast<volatile uint32_t*>(USART2_BASE) & 0x80));
        *reinterpret_cast<volatile uint32_t*>(USART2_BASE + 0x04) = c;
    }

    static void puts(const char* s) {
        while (*s) putc(*s++);
    }

    static void print_uint(uint32_t n) {
        if (n == 0) { putc('0'); return; }
        char buf[12]; int i = 0;
        while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
        while (i--) putc(buf[i]);
    }

    static void print_int(int32_t n) {
        if (n < 0) { putc('-'); n = -n; }
        print_uint(static_cast<uint32_t>(n));
    }
};

int main() {
    DWT_Timer::enable();
    UART_Console::init();

    UART_Console::puts("\n\n=== STM32F7 Benchmark ===\n");
    UART_Console::puts("CPU: Cortex-M7 @ 216MHz\n");

    run_benchmarks<DWT_Timer, UART_Console>();

    while (1) asm volatile("wfi");
    return 0;
}

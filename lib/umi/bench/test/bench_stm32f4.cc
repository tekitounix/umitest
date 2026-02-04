// STM32F4-Discovery benchmark using bench framework
#include <cstdint>

#include <umi/bench/bench.hh>

struct UART {
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
};

int main() {
    umi::bench::TimerImpl::enable();
    UART::init();

    UART::puts("\n\n=== STM32F4-Discovery Benchmark Starting ===\n");
    UART::puts("Running lambda-based inline benchmarks...\n\n");

    umi::bench::PlatformInlineRunner runner;
    runner.calibrate<512>();

    auto stats = runner.benchmark_corrected<128>([] {
        volatile int x = 0;
        x += 1; x += 2; x += 3; x += 4; x += 5;
        x += 6; x += 7; x += 8; x += 9; x += 10;
        (void)x;
    });

    UART::puts("10x add (min): ");
    UART::print_uint(static_cast<uint32_t>(stats.min));
    UART::puts(" cycles\n");

    UART::puts("\n=== Done ===\n\n");
    while (1) asm volatile("wfi");
    return 0;
}

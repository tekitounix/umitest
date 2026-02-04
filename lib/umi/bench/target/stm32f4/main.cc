// STM32F4 Target Main - Minimal template for user benchmarks
#include <cstdint>

struct DWT {
    static void enable() {
        *reinterpret_cast<volatile uint32_t*>(0xE000EDFC) |= 1u << 24;
        *reinterpret_cast<volatile uint32_t*>(0xE0001004) = 0;
        *reinterpret_cast<volatile uint32_t*>(0xE0001000) |= 1;
    }
    static uint32_t read() { return *reinterpret_cast<volatile uint32_t*>(0xE0001004); }
};

struct UART {
    static void init() {
        *reinterpret_cast<volatile uint32_t*>(0x40023840) |= (1 << 17);
        *reinterpret_cast<volatile uint32_t*>(0x4000440C) = (1 << 13) | (1 << 3);
    }
    static void putc(char c) {
        while (!(*reinterpret_cast<volatile uint32_t*>(0x40004400) & 0x80));
        *reinterpret_cast<volatile uint32_t*>(0x40004404) = c;
    }
    static void puts(const char* s) { while (*s) putc(*s++); }
    static void print(uint32_t n) {
        if (n == 0) { putc('0'); return; }
        char b[12]; int i = 0;
        while (n > 0) { b[i++] = '0' + (n % 10); n /= 10; }
        while (i--) putc(b[i]);
    }
};

template<int N>
uint32_t measure(void (*f)()) {
    uint32_t min = 0xFFFFFFFF;
    for (int i = 0; i < 7; i++) {
        uint32_t s = DWT::read();
        f();
        uint32_t e = DWT::read();
        if (e - s < min) min = e - s;
    }
    return min;
}

__attribute__((noinline)) void example_bench() {
    volatile int x = 0;
    for (int i = 0; i < 100; i++) x += i;
    (void)x;
}

int main() {
    DWT::enable(); UART::init();
    UART::puts("\n\n=== STM32F4 Benchmark Template ===\n");
    UART::puts("Example: "); UART::print(measure<100>(example_bench)); UART::puts(" cycles\n");
    while (1) asm volatile("wfi");
    return 0;
}

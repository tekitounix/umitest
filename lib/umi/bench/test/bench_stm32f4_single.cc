// STM32F4-Discovery Benchmark - Single file with startup
#include <cstdint>
#include <array>
#include <cstdio>
#include <string_view>

// ============================================================================
// Startup Code
// ============================================================================
extern "C" {
extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;
extern uint32_t _estack;

void Reset_Handler();
void Default_Handler();
int main();
}

__attribute__((section(".isr_vector"), used)) const void* const g_vector_table[] = {
    reinterpret_cast<const void*>(&_estack),
    reinterpret_cast<const void*>(Reset_Handler),
    reinterpret_cast<const void*>(Default_Handler),
    reinterpret_cast<const void*>(Default_Handler),
    reinterpret_cast<const void*>(Default_Handler),
    reinterpret_cast<const void*>(Default_Handler),
    reinterpret_cast<const void*>(Default_Handler),
    nullptr, nullptr, nullptr, nullptr,
    reinterpret_cast<const void*>(Default_Handler),
    nullptr, nullptr,
    reinterpret_cast<const void*>(Default_Handler),
    reinterpret_cast<const void*>(Default_Handler),
};

extern "C" __attribute__((noreturn)) void Reset_Handler() {
    uint32_t* src = &_sidata;
    uint32_t* dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;
    dst = &_sbss;
    while (dst < &_ebss) *dst++ = 0;

    volatile uint32_t& CPACR = *reinterpret_cast<volatile uint32_t*>(0xE000ED88);
    CPACR |= (0xFU << 20);
    asm volatile("dsb\n isb" ::: "memory");

    main();
    while (true) asm volatile("wfi");
}

extern "C" void Default_Handler() { while (true) asm volatile("bkpt #0"); }
extern "C" __attribute__((alias("Reset_Handler"))) void _start();

// ============================================================================
// DWT Timer
// ============================================================================
struct DWT {
    static constexpr uint32_t BASE = 0xE0001000;
    static auto& ctrl() { return *reinterpret_cast<volatile uint32_t*>(BASE); }
    static auto& cyccnt() { return *reinterpret_cast<volatile uint32_t*>(BASE + 4); }
    static auto& demcr() { return *reinterpret_cast<volatile uint32_t*>(0xE000EDFC); }
    static void enable() { demcr() |= 1u << 24; cyccnt() = 0; ctrl() |= 1; }
    static uint32_t cycles() { return cyccnt(); }
    static void reset() { cyccnt() = 0; }
};

// ============================================================================
// UART
// ============================================================================
struct UART {
    static constexpr uint32_t USART2_BASE = 0x40004400;
    static constexpr uint32_t RCC_APB1ENR = 0x40023840;

    static auto& dr() { return *reinterpret_cast<volatile uint32_t*>(USART2_BASE + 0x04); }
    static auto& sr() { return *reinterpret_cast<volatile uint32_t*>(USART2_BASE); }

    static void init() {
        *reinterpret_cast<volatile uint32_t*>(RCC_APB1ENR) |= (1 << 17);
        *reinterpret_cast<volatile uint32_t*>(USART2_BASE + 0x0C) = (1 << 13) | (1 << 3);
    }

    static void putc(char c) { while (!(sr() & 0x80)); dr() = c; }
    static void puts(const char* s) { while (*s) putc(*s++); }
    static void print_uint(uint32_t n) {
        if (n == 0) { putc('0'); return; }
        char buf[12]; int i = 0;
        while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
        while (i--) putc(buf[i]);
    }
};

// ============================================================================
// Main
// ============================================================================
template<typename Func>
uint32_t measure(Func&& f) {
    DWT::reset();
    auto start = DWT::cycles();
    f();
    auto end = DWT::cycles();
    return end - start;
}

int main() {
    DWT::enable();
    UART::init();

    UART::puts("\n\n=== STM32F4 Benchmark ===\n");
    UART::puts("CPU: Cortex-M4 @ 168MHz\n\n");

    // Lambda benchmark - 10x integer add
    auto t1 = measure([]{
        volatile int x = 0;
        x += 1; x += 2; x += 3; x += 4; x += 5;
        x += 6; x += 7; x += 8; x += 9; x += 10;
        (void)x;
    });
    UART::puts("10x add: "); UART::print_uint(t1); UART::puts(" cycles\n");

    // Lambda benchmark - 10x float mul
    auto t2 = measure([]{
        volatile float x = 1.0f;
        x *= 1.1f; x *= 1.2f; x *= 1.3f; x *= 1.4f; x *= 1.5f;
        x *= 1.6f; x *= 1.7f; x *= 1.8f; x *= 1.9f; x *= 2.0f;
        (void)x;
    });
    UART::puts("10x float mul: "); UART::print_uint(t2); UART::puts(" cycles\n");

    UART::puts("\n=== Done ===\n\n");

    while (1) asm volatile("wfi");
    return 0;
}

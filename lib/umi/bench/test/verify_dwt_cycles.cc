/* Cortex-M4 DWT_CYCCNT Verification */
#include <cstdint>

struct DWT {
    static constexpr uint32_t BASE = 0xE0001000;
    static auto& cyccnt() { return *reinterpret_cast<volatile uint32_t*>(BASE + 4); }
    static auto& demcr() { return *reinterpret_cast<volatile uint32_t*>(0xE000EDFC); }
    static auto& ctrl() { return *reinterpret_cast<volatile uint32_t*>(BASE); }
    static void enable() { demcr() |= 1u << 24; cyccnt() = 0; ctrl() |= 1; }
    static uint32_t cycles() { return cyccnt(); }
    static void reset() { cyccnt() = 0; }
};

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

extern "C" {
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
void Reset_Handler();
void Default_Handler();
int main();
}

__attribute__((section(".isr_vector"), used)) const void* const g_vector_table[] = {
    reinterpret_cast<const void*>(&_estack), reinterpret_cast<const void*>(Reset_Handler),
    reinterpret_cast<const void*>(Default_Handler), reinterpret_cast<const void*>(Default_Handler),
    reinterpret_cast<const void*>(Default_Handler), reinterpret_cast<const void*>(Default_Handler),
    reinterpret_cast<const void*>(Default_Handler), nullptr, nullptr, nullptr, nullptr,
    reinterpret_cast<const void*>(Default_Handler), nullptr, nullptr,
    reinterpret_cast<const void*>(Default_Handler), reinterpret_cast<const void*>(Default_Handler),
};

extern "C" __attribute__((noreturn)) void Reset_Handler() {
    uint32_t* src = &_sidata, *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;
    dst = &_sbss; while (dst < &_ebss) *dst++ = 0;
    volatile uint32_t& CPACR = *reinterpret_cast<volatile uint32_t*>(0xE000ED88);
    CPACR |= (0xFU << 20); asm volatile("dsb\n isb" ::: "memory");
    main(); while (true) asm volatile("wfi");
}
extern "C" void Default_Handler() { while (true) asm volatile("bkpt #0"); }
extern "C" __attribute__((alias("Reset_Handler"))) void _start();

template<typename Func> uint32_t measure(Func&& f) {
    DWT::reset(); auto start = DWT::cycles(); f(); return DWT::cycles() - start;
}

__attribute__((noinline)) void test_add() { volatile uint32_t x = 1; x += 1; x += 2; x += 3; (void)x; }
__attribute__((noinline)) void test_mul() { volatile uint32_t x = 100; x *= 2; x *= 3; (void)x; }
__attribute__((noinline)) void test_load() { volatile uint32_t d[2] = {1,2}; volatile uint32_t s = d[0] + d[1]; (void)s; }
__attribute__((noinline)) void test_store() { volatile uint32_t d[2]; d[0] = 1; d[1] = 2; (void)d; }

int main() {
    DWT::enable(); UART::init();
    UART::puts("\n\n=== DWT Verification ===\n");
    auto t1 = measure(test_add); UART::puts("ADD: "); UART::print_uint(t1); UART::puts(" cy\n");
    auto t2 = measure(test_mul); UART::puts("MUL: "); UART::print_uint(t2); UART::puts(" cy\n");
    auto t3 = measure(test_load); UART::puts("LDR: "); UART::print_uint(t3); UART::puts(" cy\n");
    auto t4 = measure(test_store); UART::puts("STR: "); UART::print_uint(t4); UART::puts(" cy\n");
    UART::puts("\nExpected: ADD=1, MUL=1, LDR=2, STR=2 per op\n");
    UART::puts("Done.\n\n");
    while (1) asm volatile("wfi");
}

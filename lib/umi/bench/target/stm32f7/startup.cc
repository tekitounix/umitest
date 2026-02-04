// STM32F7 (Cortex-M7) Startup
#include <cstdint>

extern "C" {
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
void Reset_Handler();
void Default_Handler();
int main();
}

__attribute__((section(".isr_vector"), used))
const void* const g_vector_table[] = {
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
    uint32_t* src = &_sidata, *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;
    dst = &_sbss;
    while (dst < &_ebss) *dst++ = 0;

    volatile uint32_t& CPACR = *reinterpret_cast<volatile uint32_t*>(0xE000ED88);
    CPACR |= (0xFU << 20);
    asm volatile("dsb\n isb" ::: "memory");

    main();
    while (true) asm volatile("wfi");
}

extern "C" void Default_Handler() {
    while (true) asm volatile("bkpt #0");
}

extern "C" __attribute__((alias("Reset_Handler"))) void _start();

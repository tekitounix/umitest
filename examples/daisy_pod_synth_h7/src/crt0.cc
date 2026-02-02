// SPDX-License-Identifier: MIT
// Minimal CRT0 for H7 application (.umia)
// Initializes .data and .bss, then calls main()

#include <cstdint>
#include <cstring>

extern "C" {
extern uint32_t _sdata, _edata, _sidata;
extern uint32_t _sbss, _ebss;
int main();
}

extern "C" [[gnu::section(".text._start"), gnu::used, noreturn]]
void _start() {
    // Copy .data from flash to RAM
    auto* dst = &_sdata;
    const auto* src = &_sidata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    // Zero .bss
    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0;
    }

    main();

    // Should not return
    while (true) {
        __asm__ volatile("wfi");
    }
}

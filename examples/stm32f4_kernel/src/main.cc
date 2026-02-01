// SPDX-License-Identifier: MIT
// STM32F4-Discovery Kernel Entry Point
// Loads and runs .umia applications

#include <cstdint>
#include <mpu_config.hh>
#include <umios/backend/cm/common/irq.hh>
#include <umios/backend/cm/common/nvic.hh>
#include <umios/backend/cm/common/scb.hh>
#include <umios/backend/cm/stm32f4/irq_num.hh>

#include "arch.hh"
#include "bsp.hh"
#include "kernel.hh"
#include "mcu.hh"

using umi::port::arm::SCB;
namespace bsp = umi::bsp::board;

// Linker-provided symbols
extern "C" {
extern const uint8_t _app_image_start[];
extern uint8_t _app_ram_start[];
extern const uint32_t _app_ram_size;
extern uint32_t _estack;
extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;
}

// Exception handlers (defined in arch.cc)
extern "C" void SVC_Handler();
extern "C" void PendSV_Handler();
extern "C" void SysTick_Handler();

// IRQ handlers (defined in mcu.cc)
extern "C" void DMA1_Stream3_IRQHandler();
extern "C" void DMA1_Stream5_IRQHandler();
extern "C" void OTG_FS_IRQHandler();

// Fault handlers
extern "C" {
void NMI_Handler() {
    while (true) {
    }
}
void HardFault_Handler() {
    umi::mcu::gpio(bsp::led::gpio_port).set(bsp::led::red);
    while (true) {
    }
}
void MemManage_Handler() {
    umi::mcu::gpio(bsp::led::gpio_port).set(bsp::led::red);
    while (true) {
    }
}
void BusFault_Handler() {
    umi::mcu::gpio(bsp::led::gpio_port).set(bsp::led::red);
    while (true) {
    }
}
void UsageFault_Handler() {
    umi::mcu::gpio(bsp::led::gpio_port).set(bsp::led::red);
    while (true) {
    }
}
void DebugMon_Handler() {
    while (true) {
    }
}
}

int main() {
    using namespace umi::kernel;

    umi::mcu::init_clocks();
    umi::mcu::init_gpio();

    umi::mcu::gpio(bsp::led::gpio_port).set(bsp::led::blue); // Blue LED - startup

    // Initialize kernel
    init_shared_memory();
    init_loader(_app_ram_start, reinterpret_cast<uintptr_t>(&_app_ram_size));

    // Load app
    void* app_entry = load_app(_app_image_start);

    if (app_entry != nullptr) {
        // Configure MPU for app execution
        mpu::configure_region(mpu::Region::KERNEL,
                              {
                                  .base = reinterpret_cast<void*>(bsp::memory::sram_base),
                                  .size = bsp::memory::kernel_ram_size,
                                  .readable = true,
                                  .writable = true,
                                  .executable = false,
                                  .privileged_only = false,
                                  .device_memory = false,
                              });

        mpu::configure_region(mpu::Region::APP_TEXT,
                              {
                                  .base = const_cast<uint8_t*>(_app_image_start),
                                  .size = bsp::memory::app_text_size,
                                  .readable = true,
                                  .writable = false,
                                  .executable = true,
                                  .privileged_only = false,
                                  .device_memory = false,
                              });

        mpu::configure_region(mpu::Region::APP_DATA,
                              {
                                  .base = _app_ram_start,
                                  .size = bsp::memory::app_data_size,
                                  .readable = true,
                                  .writable = true,
                                  .executable = false,
                                  .privileged_only = false,
                                  .device_memory = false,
                              });

        mpu::configure_region(mpu::Region::APP_STACK,
                              {
                                  .base = reinterpret_cast<uint8_t*>(_app_ram_start) + bsp::memory::app_data_size,
                                  .size = bsp::memory::app_stack_size,
                                  .readable = true,
                                  .writable = true,
                                  .executable = false,
                                  .privileged_only = false,
                                  .device_memory = false,
                              });

        mpu::configure_region(mpu::Region::SHARED,
                              {
                                  .base = reinterpret_cast<void*>(bsp::memory::shared_base),
                                  .size = bsp::memory::shared_size,
                                  .readable = true,
                                  .writable = true,
                                  .executable = false,
                                  .privileged_only = false,
                                  .device_memory = false,
                              });

        mpu::configure_region(mpu::Region::PERIPHERALS,
                              {
                                  .base = reinterpret_cast<void*>(bsp::memory::peripheral_base),
                                  .size = bsp::memory::peripheral_size,
                                  .readable = true,
                                  .writable = true,
                                  .executable = false,
                                  .privileged_only = false,
                                  .device_memory = true,
                              });

        mpu::configure_region(static_cast<mpu::Region>(6),
                              {
                                  .base = reinterpret_cast<void*>(bsp::memory::ccm_base),
                                  .size = bsp::memory::ccm_size,
                                  .readable = true,
                                  .writable = true,
                                  .executable = false,
                                  .privileged_only = false,
                                  .device_memory = false,
                              });

        mpu::configure_region(static_cast<mpu::Region>(7),
                              {
                                  .base = reinterpret_cast<void*>(bsp::memory::flash_base),
                                  .size = bsp::memory::kernel_flash_size,
                                  .readable = true,
                                  .writable = false,
                                  .executable = true,
                                  .privileged_only = false,
                                  .device_memory = false,
                              });

        mpu::enable(true);
    }

    // Initialize hardware
    // USB callbacks must be set BEFORE init_usb() enables NVIC
    setup_usb_callbacks();
    umi::mcu::init_usb();
    umi::arch::cm4::init_systick(bsp::cpu_freq_hz, 1000); // 1ms tick
    umi::arch::cm4::init_cycle_counter();
    umi::mcu::init_audio();
    umi::mcu::init_pdm_mic();

    umi::mcu::gpio(bsp::led::gpio_port).reset(bsp::led::blue);
    umi::mcu::gpio(bsp::led::gpio_port).reset(bsp::led::green);

    // Start RTOS
    start_rtos(app_entry);

    return 0;
}

// Vector Table
extern "C" [[noreturn]] void Reset_Handler();

__attribute__((section(".isr_vector"), used)) const void* const g_boot_vectors[2] = {
    reinterpret_cast<const void*>(&_estack),
    reinterpret_cast<const void*>(Reset_Handler),
};

// C++ global constructors
extern "C" {
extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);
}

extern "C" [[noreturn]] void Reset_Handler() {
    SCB::enable_fpu();
    __asm__ volatile("dsb\nisb" ::: "memory");

    // Copy .data from flash to RAM
    uint32_t* src = &_sidata;
    uint32_t* dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    // Zero .bss
    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0;
    }

    // Initialize dynamic IRQ system
    umi::irq::init();
    namespace exc = umi::backend::cm::exc;
    umi::irq::set_handler(exc::HardFault, HardFault_Handler);
    umi::irq::set_handler(exc::MemManage, MemManage_Handler);
    umi::irq::set_handler(exc::BusFault, BusFault_Handler);
    umi::irq::set_handler(exc::UsageFault, UsageFault_Handler);
    umi::irq::set_handler(exc::SVCall, SVC_Handler);
    umi::irq::set_handler(exc::PendSV, PendSV_Handler);
    umi::irq::set_handler(exc::SysTick, SysTick_Handler);

    // Set exception priorities per architecture spec:
    // SysTick = 0xF0 (priority 15) - below DMA, above PendSV
    // PendSV  = 0xFF (priority 15, lowest) - context switch must be lowest
    // This ensures DMA ISRs can preempt SysTick/PendSV.
    umi::port::arm::NVIC::set_prio(-1, 0xF0); // SysTick
    umi::port::arm::NVIC::set_prio(-2, 0xFF); // PendSV
    namespace irqn = umi::stm32f4::irq;
    umi::irq::set_handler(irqn::DMA1_Stream3, DMA1_Stream3_IRQHandler);
    umi::irq::set_handler(irqn::DMA1_Stream5, DMA1_Stream5_IRQHandler);
    umi::irq::set_handler(irqn::OTG_FS, OTG_FS_IRQHandler);
    // DMA I2S: priority 0x00 (highest) — above BASEPRI threshold (0x10).
    // These ISRs use signal()+PendSV, never MaskedCritical.
    // OTG_FS: priority 0x40 — below BASEPRI, uses notify() safely.
    umi::port::arm::NVIC::set_prio(irqn::DMA1_Stream3, 0x00);
    umi::port::arm::NVIC::set_prio(irqn::DMA1_Stream5, 0x00);
    umi::port::arm::NVIC::set_prio(irqn::OTG_FS, 0x40);

    // Call C++ global constructors
    for (void (**p)(void) = __init_array_start; p < __init_array_end; ++p) {
        (*p)();
    }

    main();

    while (true) {
    }
}

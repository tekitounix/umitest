// SPDX-License-Identifier: MIT
// Daisy Pod Kernel - Entry point, vector table, IRQ handlers
// All kernel logic lives in kernel.cc; this file is the hardware boundary.
#include "kernel.hh"

#include <cstdint>
#include <common/irq.hh>

#include <arch/cache.hh>
#include <board/mcu_init.hh>
#include <board/audio.hh>
#include <board/usb.hh>
#include <board/sdram.hh>
#include <board/qspi.hh>
#include <board/midi_uart.hh>
#include <board/sdcard.hh>

using namespace umi::daisy;

// Linker-provided symbols
extern "C" {
extern std::uint32_t _estack;
extern std::uint32_t _sidata;
extern std::uint32_t _sdata;
extern std::uint32_t _edata;
extern std::uint32_t _sbss;
extern std::uint32_t _ebss;
extern std::uint32_t _sdtcm_bss;
extern std::uint32_t _edtcm_bss;
}

// ============================================================================
// DMA audio buffers (D2 SRAM — non-cacheable via MPU)
// ============================================================================

__attribute__((section(".sram_d2_bss")))
std::int32_t audio_tx_buf[AUDIO_BUFFER_SIZE];

__attribute__((section(".sram_d2_bss")))
std::int32_t audio_rx_buf[AUDIO_BUFFER_SIZE];

// ============================================================================
// Debug: SDRAM/QSPI/SD test results
// ============================================================================

struct DbgMemTest {
    volatile std::uint32_t sdram_result = 0;
    volatile std::uint32_t sdram_read = 0;
    volatile std::uint32_t qspi_byte0 = 0;
    volatile std::uint32_t sd_result = 0;
    volatile std::uint32_t sd_byte0 = 0;
    volatile std::uint32_t dma_isr_count = 0;
    volatile std::uint32_t dma_lisr_last = 0;
} dbg_mem;

// ============================================================================
// DMA IRQ handlers
// ============================================================================

extern "C" {

void DMA1_Stream0_IRQHandler() {
    using namespace umi::stm32h7;
    mm::DirectTransportT<> transport;
    auto lisr = transport.read(DMA1::LISR{});

    std::int32_t* tx = nullptr;
    std::int32_t* rx = nullptr;

    if (lisr & dma_flags::S0_HTIF) {
        transport.write(DMA1::LIFCR::value(dma_flags::S0_HTIF));
        tx = audio_tx_buf;
        rx = audio_rx_buf;
    }
    if (lisr & dma_flags::S0_TCIF) {
        transport.write(DMA1::LIFCR::value(dma_flags::S0_TCIF));
        tx = audio_tx_buf + AUDIO_BUFFER_SIZE / 2;
        rx = audio_rx_buf + AUDIO_BUFFER_SIZE / 2;
    }
    transport.write(DMA1::LIFCR::value(lisr & dma_flags::S0_ALL));

    if (tx != nullptr) {
        // D-Cache invalidate RX buffer so CPU reads fresh DMA data
        {
            auto addr = reinterpret_cast<std::uintptr_t>(rx);
            auto end = addr + (AUDIO_BUFFER_SIZE / 2) * sizeof(std::int32_t);
            for (; addr < end; addr += 32) {
                *umi::cm7::scb::DCIMVAC = static_cast<std::uint32_t>(addr);
            }
            __asm__ volatile("dsb sy" ::: "memory");
        }
        daisy_kernel::on_audio_buffer_ready(tx, rx);
    }
}

void DMA1_Stream1_IRQHandler() {
    using namespace umi::stm32h7;
    mm::DirectTransportT<> transport;
    transport.write(DMA1::LIFCR::value(dma_flags::S1_ALL));
}

void USART1_IRQHandler() {
    daisy_kernel::handle_usart1_irq();
}

} // extern "C"

// ============================================================================
// Fault handlers
// ============================================================================

#if UMI_DEBUG
extern "C" {
extern volatile std::uint32_t d2_dbg[16];
}
#define DBG(idx, expr) (d2_dbg[(idx)] = (expr))
#else
#define DBG(idx, expr) ((void)0)
#endif

extern "C" {
void HardFault_Handler() {
    uint32_t* sp;
    __asm__ volatile("tst lr, #4\n"
                     "ite eq\n"
                     "mrseq %0, msp\n"
                     "mrsne %0, psp\n"
                     : "=r"(sp));
    DBG(0, 0xDEAD0001);
    DBG(1, sp[5]);
    DBG(2, sp[6]);
    DBG(3, sp[7]);
    DBG(4, *reinterpret_cast<volatile uint32_t*>(0xE000ED28));
    DBG(5, *reinterpret_cast<volatile uint32_t*>(0xE000ED38));
    DBG(6, reinterpret_cast<uint32_t>(sp));
    DBG(7, sp[0]);
    umi::daisy::set_led(true);
    while (true) {}
}
void MemManage_Handler() {
    umi::daisy::set_led(true);
    while (true) {}
}
void BusFault_Handler() {
    umi::daisy::set_led(true);
    while (true) {}
}
void UsageFault_Handler() {
    umi::daisy::set_led(true);
    while (true) {}
}
}

// ============================================================================
// main()
// ============================================================================

int main() {
    daisy_kernel::init();

    // Initialize clocks
    umi::daisy::init_clocks();
    umi::daisy::init_pll3();
    umi::daisy::init_led();

    // External memory
    umi::daisy::init_sdram();
    umi::daisy::init_qspi();

    // SDRAM verification
    {
        auto* sdram = reinterpret_cast<volatile std::uint32_t*>(0xC000'0000);
        sdram[0] = 0xDEAD'BEEF;
        asm volatile("dsb sy" ::: "memory");
        dbg_mem.sdram_read = sdram[0];
        dbg_mem.sdram_result = (dbg_mem.sdram_read == 0xDEAD'BEEF) ? 1 : 2;
    }

    // QSPI XIP verification
    {
        auto* qspi = reinterpret_cast<volatile std::uint8_t*>(0x9000'0000);
        dbg_mem.qspi_byte0 = qspi[0];
    }

    // Detect board version and initialize codec
    auto board_ver = umi::daisy::detect_board_version();
    umi::daisy::init_codec(board_ver);

    // Initialize audio subsystem
    umi::daisy::init_sai_gpio();
    umi::daisy::init_sai();
    umi::daisy::init_audio_dma(audio_tx_buf, audio_rx_buf, AUDIO_BUFFER_SIZE);

    // USB Audio + MIDI
    daisy_kernel::init_usb();

    // Pod HID
    constexpr float update_rate = static_cast<float>(AUDIO_SAMPLE_RATE) / AUDIO_BLOCK_SIZE;
    daisy_kernel::init_hid(update_rate);

    // MIDI UART
    umi::daisy::init_midi_uart();

    // Start audio DMA
    umi::daisy::start_audio();

    // Set PendSV and SysTick to lowest priority
    umi::port::arm::SCB::set_exc_prio(14, 0xFF);
    umi::port::arm::SCB::set_exc_prio(15, 0xFF);

    // Start RTOS (does not return)
    daisy_kernel::start_rtos();

    while (true) {}
}

// ============================================================================
// Boot Vector Table
// ============================================================================

extern "C" [[noreturn]] void Reset_Handler();

__attribute__((section(".isr_vector"), used))
const void* const g_boot_vectors[2] = {
    reinterpret_cast<const void*>(&_estack),
    reinterpret_cast<const void*>(Reset_Handler),
};

extern "C" {
extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);
}

extern "C" void PendSV_Handler();
extern "C" void SVC_Handler();
extern "C" void SysTick_Handler();

extern "C" [[noreturn]] void Reset_Handler() {
    umi::cm7::enable_fpu();
    asm volatile("dsb\nisb" ::: "memory");

    // AXI SRAM workaround (STM32H7 errata for Rev Y silicon)
    if ((*reinterpret_cast<volatile std::uint32_t*>(0x5C001000) & 0xFFFF0000U) < 0x20000000U) {
        *reinterpret_cast<volatile std::uint32_t*>(0x51008108) = 0x00000001U;
    }

    umi::cm7::configure_mpu();
    umi::cm7::enable_icache();

    std::uint32_t* src = &_sidata;
    std::uint32_t* dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0;
    }

    dst = &_sdtcm_bss;
    while (dst < &_edtcm_bss) {
        *dst++ = 0;
    }

    umi::irq::init();

    namespace exc = umi::backend::cm::exc;
    umi::irq::set_handler(exc::HardFault, HardFault_Handler);
    umi::irq::set_handler(exc::MemManage, MemManage_Handler);
    umi::irq::set_handler(exc::BusFault, BusFault_Handler);
    umi::irq::set_handler(exc::UsageFault, UsageFault_Handler);

    umi::irq::set_handler(exc::PendSV, PendSV_Handler);
    umi::irq::set_handler(exc::SVCall, SVC_Handler);
    umi::irq::set_handler(exc::SysTick, SysTick_Handler);

    // DMA1 Stream 0/1
    umi::irq::set_handler(11, DMA1_Stream0_IRQHandler);
    umi::irq::set_handler(12, DMA1_Stream1_IRQHandler);
    umi::irq::set_priority(11, 0x00);
    umi::irq::set_priority(12, 0x00);
    umi::irq::enable(11);
    umi::irq::enable(12);

    // USART1 (MIDI UART RX)
    umi::irq::set_handler(37, USART1_IRQHandler);
    umi::irq::set_priority(37, 0x40);
    umi::irq::enable(37);

    for (void (**p)(void) = __init_array_start; p < __init_array_end; ++p) {
        (*p)();
    }

    main();
    while (true) {}
}

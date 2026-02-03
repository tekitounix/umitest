// SPDX-License-Identifier: MIT
// Daisy Seed QSPI initialization (IS25LP080D / IS25LP064A)
// Memory-mapped mode at 0x9000'0000
#pragma once

#include <cstdint>
#include <mcu/gpio.hh>
#include <mcu/qspi.hh>
#include <mcu/rcc.hh>
#include <transport/direct.hh>

namespace umi::daisy {

// IS25LP064A constants
namespace is25lp {
constexpr std::uint8_t READ_STATUS = 0x05;
constexpr std::uint8_t WRITE_ENABLE = 0x06;
constexpr std::uint8_t WRITE_STATUS = 0x01;     // Write Status Register
constexpr std::uint8_t READ_READ_PARAM = 0x61;  // Read Read Parameters
constexpr std::uint8_t WRITE_READ_PARAM = 0x63; // Set Read Parameters
constexpr std::uint8_t ENTER_QPI = 0x35;
constexpr std::uint8_t QUAD_READ = 0xEB;
constexpr std::uint8_t PAGE_PROGRAM = 0x02;
constexpr std::uint8_t SECTOR_ERASE = 0x20;
constexpr std::uint8_t CHIP_ERASE = 0xC7;
constexpr std::uint8_t RESET_ENABLE = 0x66;
constexpr std::uint8_t RESET_DEVICE = 0x99;
constexpr std::uint8_t READ_ID = 0x9F;

// Status Register bits
constexpr std::uint8_t SR_WIP = 0x01; // Write In Progress
constexpr std::uint8_t SR_WEL = 0x02; // Write Enable Latch
constexpr std::uint8_t SR_QE = 0x40;  // Quad Enable bit (bit 6)

// Read Parameters for IS25LP064A: 8 dummy cycles, 50% drive strength
// Bits [7:5] = 111 (50% drive strength)
// Bits [4:3] = 11 (dummy cycles config = 8 cycles for quad read)
// Bits [2:1] = 00 (wrap enable disabled)
// Bits [0] = 0 (burst length 8)
constexpr std::uint8_t READ_PARAM_CFG = 0xF0;
} // namespace is25lp

/// Pre-initialize IO2/IO3 pins to HIGH state (libDaisy compatible)
/// This disables WP#/HOLD# functions before QSPI initialization
inline void init_qspi_gpio_preinit() {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    // Enable GPIOF clock
    t.modify(RCC::AHB4ENR::GPIOFEN::Set{});
    [[maybe_unused]] auto dummy = t.read(RCC::AHB4ENR{});

    // Set PF6 (IO3) and PF7 (IO2) HIGH as GPIO output first
    // This ensures WP# and HOLD# are inactive during initialization
    // Configure as output push-pull
    gpio_configure_pin<GPIOF>(t, 6, gpio_mode::OUTPUT, gpio_otype::PUSH_PULL, gpio_speed::LOW, gpio_pupd::NONE);
    gpio_configure_pin<GPIOF>(t, 7, gpio_mode::OUTPUT, gpio_otype::PUSH_PULL, gpio_speed::LOW, gpio_pupd::NONE);

    // Set HIGH
    auto odr = t.read(GPIOF::ODR{});
    t.write(GPIOF::ODR::value(odr | (1U << 6) | (1U << 7)));

    // Brief delay (simple busy loop)
    for (int i = 0; i < 10000; i++) { __asm__ volatile("nop"); }

    // Set LOW then HIGH (like libDaisy)
    t.write(GPIOF::ODR::value(odr & ~((1U << 6) | (1U << 7))));
    for (int i = 0; i < 10000; i++) { __asm__ volatile("nop"); }
    t.write(GPIOF::ODR::value(odr | (1U << 6) | (1U << 7)));
    for (int i = 0; i < 10000; i++) { __asm__ volatile("nop"); }
}

/// Initialize QSPI GPIO pins
inline void init_qspi_gpio() {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    t.modify(RCC::AHB4ENR::GPIOFEN::Set{});
    t.modify(RCC::AHB4ENR::GPIOGEN::Set{});
    [[maybe_unused]] auto dummy = t.read(RCC::AHB4ENR{});

    auto cfg_f = [&](std::uint8_t pin, std::uint8_t af) {
        gpio_configure_pin<GPIOF>(
            t, pin, gpio_mode::ALTERNATE, gpio_otype::PUSH_PULL, gpio_speed::VERY_HIGH, gpio_pupd::NONE);
        gpio_set_af<GPIOF>(t, pin, af);
    };

    cfg_f(6, 9);  // IO3 (AF9)
    cfg_f(7, 9);  // IO2 (AF9)
    cfg_f(8, 10); // IO0 (AF10)
    cfg_f(9, 10); // IO1 (AF10)
    cfg_f(10, 9); // CLK (AF9)

    // PG6=NCS (AF10)
    gpio_configure_pin<GPIOG>(
        t, 6, gpio_mode::ALTERNATE, gpio_otype::PUSH_PULL, gpio_speed::VERY_HIGH, gpio_pupd::PULL_UP);
    gpio_set_af<GPIOG>(t, 6, 10);
}

/// Wait for QSPI not busy
inline void qspi_wait_busy() {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;
    while (t.read(QUADSPI::SR{}) & (1U << 5)) {
    }
}

/// Send single-line command (no data)
inline void qspi_command(std::uint8_t cmd) {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    qspi_wait_busy();
    t.write(QUADSPI::CCR::value(static_cast<std::uint32_t>(cmd) | (qspi_mode::SINGLE << 8) | (qspi_mode::NONE << 10) |
                                (qspi_mode::NONE << 24) | (qspi_fmode::INDIRECT_WRITE << 26)));
}

/// Read single byte via indirect read
inline std::uint8_t qspi_read_byte(std::uint8_t cmd) {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    qspi_wait_busy();
    t.write(QUADSPI::DLR::value(0)); // 1 byte
    t.write(QUADSPI::CCR::value(static_cast<std::uint32_t>(cmd) | (qspi_mode::SINGLE << 8) | (qspi_mode::NONE << 10) |
                                (qspi_mode::SINGLE << 24) | (qspi_fmode::INDIRECT_READ << 26)));

    // Wait for transfer complete
    while (!(t.read(QUADSPI::SR{}) & (1U << 1))) {
    }
    std::uint8_t data = static_cast<std::uint8_t>(t.read(QUADSPI::DR{}));
    t.write(QUADSPI::FCR::value(1U << 1)); // Clear TCF
    qspi_wait_busy();
    return data;
}

/// Write single byte via indirect write
inline void qspi_write_byte(std::uint8_t cmd, std::uint8_t data) {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    qspi_wait_busy();
    t.write(QUADSPI::DLR::value(0)); // 1 byte
    t.write(QUADSPI::CCR::value(static_cast<std::uint32_t>(cmd) | (qspi_mode::SINGLE << 8) | (qspi_mode::NONE << 10) |
                                (qspi_mode::SINGLE << 24) | (qspi_fmode::INDIRECT_WRITE << 26)));

    // Write data
    t.write(QUADSPI::DR::value(data));

    // Wait for transfer complete
    while (!(t.read(QUADSPI::SR{}) & (1U << 1))) {
    }
    t.write(QUADSPI::FCR::value(1U << 1)); // Clear TCF
    qspi_wait_busy();
}

/// Auto-polling: wait for status register bit to match (libDaisy compatible)
/// @param cmd Status register read command
/// @param mask Bits to check
/// @param match Expected value (matched when (status & mask) == match)
/// @param interval Polling interval in QSPI clock cycles (default 0x10)
/// @return true on success, false on timeout
inline bool qspi_auto_poll(std::uint8_t cmd, std::uint32_t mask, std::uint32_t match, 
                           std::uint32_t interval = 0x10, std::uint32_t timeout_us = 5000000) {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    qspi_wait_busy();

    // Enable APMS (auto poll mode stop) so BUSY clears after match
    auto cr = t.read(QUADSPI::CR{});
    t.write(QUADSPI::CR::value(cr | (1U << 22))); // APMS

    t.write(QUADSPI::DLR::value(0));             // 1 byte status
    t.write(QUADSPI::PSMKR::value(mask));        // Mask
    t.write(QUADSPI::PSMAR::value(match));       // Match value
    t.write(QUADSPI::PIR::value(interval));      // Polling interval

    // Configure CCR: Single-line instruction, no address, single-line data, auto-polling mode
    t.write(QUADSPI::CCR::value(static_cast<std::uint32_t>(cmd) | 
                                (qspi_mode::SINGLE << 8) |   // IMODE: instruction on 1 line
                                (qspi_mode::NONE << 10) |    // ADMODE: no address
                                (qspi_mode::SINGLE << 24) |  // DMODE: data on 1 line
                                (qspi_fmode::AUTO_POLLING << 26)));

    // Wait for status match with timeout
    // At 120MHz QSPI clock, each cycle is ~8.3ns
    // Simple loop count as timeout approximation
    std::uint32_t timeout_count = timeout_us * 10; // Rough approximation
    while (!(t.read(QUADSPI::SR{}) & (1U << 3))) { // SMF: Status Match Flag
        timeout_count--;
        if (timeout_count == 0) {
            // Abort on timeout
            auto cr_val = t.read(QUADSPI::CR{});
            t.write(QUADSPI::CR::value(cr_val | (1U << 1))); // ABORT
            while (t.read(QUADSPI::CR{}) & (1U << 1)) {}
            t.write(QUADSPI::FCR::value(0x1F)); // Clear all flags
            return false;
        }
    }
    t.write(QUADSPI::FCR::value(1U << 3)); // Clear SMF
    qspi_wait_busy();
    return true;
}

/// Enable write operations (WREN command) with WEL confirmation via auto-poll
/// libDaisy compatible: uses HAL_QSPI_AutoPolling for WEL check
inline bool qspi_write_enable() {
    using namespace ::umi::stm32h7;
    qspi_command(is25lp::WRITE_ENABLE);
    
    // Wait for command to complete
    qspi_wait_busy();
    
    // Auto-poll until WEL bit is set (libDaisy style)
    // Match: WEL=1, Mask: WEL bit only
    return qspi_auto_poll(is25lp::READ_STATUS, is25lp::SR_WEL, is25lp::SR_WEL, 0x10);
}

/// Wait for memory ready (WIP=0) using auto-polling (libDaisy compatible)
/// This is the equivalent of libDaisy's AutopollingMemReady()
/// @param timeout_us Timeout in microseconds (default 5 seconds for erase operations)
/// @return true if ready, false if timeout
inline bool qspi_wait_ready(std::uint32_t timeout_us = 5000000) {
    // Auto-poll until WIP bit clears
    // Match: WIP=0 (0x00), Mask: WIP bit
    return qspi_auto_poll(is25lp::READ_STATUS, is25lp::SR_WIP, 0x00, 0x10, timeout_us);
}

/// Enable Quad mode (set QE bit in status register) - libDaisy compatible
inline bool qspi_quad_enable() {
    using namespace ::umi::stm32h7;

    // Enable write
    if (!qspi_write_enable()) return false;

    // Write status register with QE bit set
    qspi_write_byte(is25lp::WRITE_STATUS, is25lp::SR_QE);

    // Wait until QE is set and WIP/WEL are cleared (libDaisy style)
    // Match: QE=1, WEL=0, WIP=0
    // Mask: QE|WEL|WIP
    // Use longer interval (0x8000) like libDaisy for status register writes
    if (!qspi_auto_poll(is25lp::READ_STATUS,
                        is25lp::SR_QE | is25lp::SR_WEL | is25lp::SR_WIP,
                        is25lp::SR_QE, 0x8000)) {
        return false;
    }

    // Final wait for memory ready
    return qspi_wait_ready();
}

/// Configure dummy cycles for quad read (IS25LP064A) - libDaisy compatible
inline bool qspi_set_read_parameters() {
    using namespace ::umi::stm32h7;

    // Enable write
    if (!qspi_write_enable()) return false;

    // Write Read Parameters register
    qspi_write_byte(is25lp::WRITE_READ_PARAM, is25lp::READ_PARAM_CFG);

    // Wait for memory ready
    return qspi_wait_ready();
}

/// Invalidate I-Cache for QSPI region
inline void qspi_invalidate_icache() {
    // Cortex-M7 I-Cache invalidate by address
    // ICIMVAU: Instruction Cache line Invalidate by address to PoU
    constexpr std::uint32_t ICIMVAU_ADDR = 0xE000EF58;
    constexpr std::uint32_t QSPI_BASE = 0x90000000;
    constexpr std::uint32_t QSPI_SIZE = 8 * 1024 * 1024; // 8MB
    constexpr std::uint32_t CACHE_LINE_SIZE = 32;

    auto* const icimvau = reinterpret_cast<volatile std::uint32_t*>(ICIMVAU_ADDR);

    // Invalidate entire QSPI region
    for (std::uint32_t addr = QSPI_BASE; addr < QSPI_BASE + QSPI_SIZE; addr += CACHE_LINE_SIZE) {
        *icimvau = addr;
    }

    // DSB + ISB to ensure cache invalidation completes
    __asm__ volatile("dsb sy\nisb" ::: "memory");
}

/// Invalidate D-Cache for QSPI region (important for reading back written data)
inline void qspi_invalidate_dcache() {
    // Use SCB_InvalidateDCache() equivalent - invalidate entire D-Cache
    // This is faster than invalidating line by line for large regions
    // SCB->DCISW register: D-Cache invalidate by Set/Way
    
    // Method 1: Invalidate entire D-Cache via SCB->DCCSW (clean and invalidate)
    // CCSIDR layout: LineSize[2:0], Associativity[12:3], NumSets[27:13]
    constexpr std::uint32_t SCB_CCSIDR_ADDR = 0xE000ED80;
    constexpr std::uint32_t SCB_DCISW_ADDR = 0xE000EF60;  // D-cache invalidate by set/way
    
    auto* const ccsidr = reinterpret_cast<volatile std::uint32_t*>(SCB_CCSIDR_ADDR);
    auto* const dcisw = reinterpret_cast<volatile std::uint32_t*>(SCB_DCISW_ADDR);
    
    std::uint32_t ccsidr_val = *ccsidr;
    std::uint32_t sets = (ccsidr_val >> 13) & 0x7FFF;
    std::uint32_t ways = (ccsidr_val >> 3) & 0x3FF;
    
    // Calculate bit positions
    std::uint32_t way_shift = __builtin_clz(ways);
    std::uint32_t set_shift = ((ccsidr_val & 0x7) + 4); // LineSize + 4
    
    for (std::uint32_t set = 0; set <= sets; set++) {
        for (std::uint32_t way = 0; way <= ways; way++) {
            *dcisw = (way << way_shift) | (set << set_shift);
        }
    }
    
    // DSB to ensure cache invalidation completes
    __asm__ volatile("dsb sy" ::: "memory");
}

/// Initialize QSPI in memory-mapped mode
inline void init_qspi() {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    // Enable QSPI clock
    t.modify(RCC::AHB3ENR::QSPIEN::Set{});
    [[maybe_unused]] auto dummy = t.read(RCC::AHB3ENR{});

    // Pre-init IO2/IO3 HIGH (libDaisy compatible - disables WP#/HOLD#)
    init_qspi_gpio_preinit();

    init_qspi_gpio();

    // Abort any ongoing operation
    auto cr = t.read(QUADSPI::CR{});
    t.write(QUADSPI::CR::value(cr | (1U << 1))); // ABORT
    while (t.read(QUADSPI::CR{}) & (1U << 1)) {
    }

    // Configure: prescaler=1 (HCLK/2=120MHz), FIFO threshold=1 (libDaisy compatible)
    t.write(QUADSPI::CR::value((1U << 24) | // PRESCALER: 1
                               (1U << 8) |  // FTHRES: 1 (libDaisy uses 1)
                               (0U << 4)    // SSHIFT: none
                               ));

    // Device config: 8MB flash (FSIZE=22), CS high = 2 cycles
    t.write(QUADSPI::DCR::value((22U << 16) | // FSIZE
                                (1U << 8) |   // CSHT: 2 cycles
                                (0U << 0)     // CKMODE: 0
                                ));

    // Enable QSPI
    cr = t.read(QUADSPI::CR{});
    t.write(QUADSPI::CR::value(cr | (1U << 0)));

    // Reset flash device
    qspi_command(is25lp::RESET_ENABLE);
    qspi_command(is25lp::RESET_DEVICE);

    // Wait for device ready (WIP=0)
    qspi_wait_ready();

    // Set Read Parameters (dummy cycles configuration)
    qspi_set_read_parameters();

    // Enable Quad mode (set QE bit)
    qspi_quad_enable();

    // Enter memory-mapped mode with quad I/O read (0xEB)
    // Based on libDaisy's EnableMemoryMappedMode():
    // - Instruction: 0xEB (Fast Read Quad I/O)
    // - Address: 4 lines, 24-bit
    // - Alternate Bytes: 4 lines, 8-bit, value=0xA0 (continuous read mode)
    // - Dummy cycles: 6 (for IS25LP064A at up to 133MHz)
    // - Data: 4 lines
    // - SIOO: Send instruction only first time
    qspi_wait_busy();

    // Set Alternate Bytes value (0xA0 for continuous read mode)
    t.write(QUADSPI::ABR::value(0x000000A0));

    // Configure CCR for memory-mapped quad I/O read
    // CCR bits:
    // [7:0]   INSTRUCTION = 0xEB
    // [9:8]   IMODE = 01 (single line)
    // [11:10] ADMODE = 11 (quad)
    // [13:12] ADSIZE = 10 (24-bit)
    // [15:14] ABMODE = 11 (quad) - Alternate bytes mode
    // [17:16] ABSIZE = 00 (8-bit) - Alternate bytes size
    // [22:18] DCYC = 6 (dummy cycles)
    // [25:24] DMODE = 11 (quad)
    // [27:26] FMODE = 11 (memory mapped)
    // [28]    SIOO = 1 (send instruction only once)
    t.write(QUADSPI::CCR::value(static_cast<std::uint32_t>(is25lp::QUAD_READ) | // 0xEB
                                (qspi_mode::SINGLE << 8) |                      // IMODE: single line for instruction
                                (qspi_mode::QUAD << 10) |                       // ADMODE: quad for address
                                (qspi_adsize::ADDR_24BIT << 12) |               // ADSIZE: 24-bit
                                (qspi_mode::QUAD << 14) |                       // ABMODE: quad for alternate bytes
                                (0U << 16) |                                    // ABSIZE: 8-bit (0b00)
                                (6U << 18) |                                    // DCYC: 6 dummy cycles
                                (qspi_mode::QUAD << 24) |                       // DMODE: quad for data
                                (qspi_fmode::MEMORY_MAPPED << 26) | (1U << 28)  // SIOO: send instruction only once
                                ));

    // Invalidate I-Cache for QSPI region to ensure fresh data is read
    qspi_invalidate_icache();
}

} // namespace umi::daisy

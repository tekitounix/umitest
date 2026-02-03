// SPDX-License-Identifier: MIT
// Daisy Seed QSPI initialization
// Memory-mapped mode at 0x9000'0000
#pragma once

#include <cstdint>
#include <mcu/gpio.hh>
#include <mcu/qspi.hh>
#include <mcu/rcc.hh>
#include <transport/direct.hh>

#include "flash_is25lp064a.hh"

namespace umi::daisy {

// Flash device alias for this board
using Flash = flash::IS25LP064A;

/// QSPI operating modes (libDaisy compatible)
enum class QspiMode { INDIRECT_POLLING, MEMORY_MAPPED };

/// Current QSPI mode state (libDaisy SetMode pattern)
inline QspiMode g_qspi_current_mode = QspiMode::MEMORY_MAPPED;

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
    for (int i = 0; i < 10000; i++) {
        __asm__ volatile("nop");
    }

    // Set LOW then HIGH (like libDaisy)
    t.write(GPIOF::ODR::value(odr & ~((1U << 6) | (1U << 7))));
    for (int i = 0; i < 10000; i++) {
        __asm__ volatile("nop");
    }
    t.write(GPIOF::ODR::value(odr | (1U << 6) | (1U << 7)));
    for (int i = 0; i < 10000; i++) {
        __asm__ volatile("nop");
    }
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

struct QspiFailInfo {
    std::uint32_t code;
    std::uint32_t sr;
    std::uint32_t cr;
    std::uint32_t ccr;
    std::uint32_t dlr;
    std::uint32_t ar;
    std::uint8_t flash_sr;
};

inline volatile QspiFailInfo g_qspi_fail = {0, 0, 0, 0, 0, 0, 0};

inline void qspi_record_fail(std::uint32_t code);

/// Send single-line command (no data)
inline void qspi_command(std::uint8_t cmd) {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    qspi_wait_busy();
    t.write(QUADSPI::FCR::value(0x1F));
    t.write(QUADSPI::DLR::value(0));
    t.write(QUADSPI::CCR::value(static_cast<std::uint32_t>(cmd) | (qspi_mode::SINGLE << 8) | (qspi_mode::NONE << 10) |
                                (qspi_mode::NONE << 24) | (qspi_fmode::INDIRECT_WRITE << 26)));
    while (!(t.read(QUADSPI::SR{}) & (1U << 1))) {
    }
    t.write(QUADSPI::FCR::value(1U << 1));
    qspi_wait_busy();
}

/// Read single byte via indirect read
inline std::uint8_t qspi_read_byte(std::uint8_t cmd) {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    qspi_wait_busy();
    t.write(QUADSPI::FCR::value(0x1F));
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

inline void qspi_record_fail(std::uint32_t code) {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;
    g_qspi_fail.code = code;
    g_qspi_fail.sr = t.read(QUADSPI::SR{});
    g_qspi_fail.cr = t.read(QUADSPI::CR{});
    g_qspi_fail.ccr = t.read(QUADSPI::CCR{});
    g_qspi_fail.dlr = t.read(QUADSPI::DLR{});
    g_qspi_fail.ar = t.read(QUADSPI::AR{});
    if ((g_qspi_fail.sr & (1U << 5)) == 0) {
        g_qspi_fail.flash_sr = qspi_read_byte(Flash::READ_STATUS);
    } else {
        g_qspi_fail.flash_sr = 0xFF;
    }
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
    auto* const dr_byte = reinterpret_cast<volatile std::uint8_t*>(0x52005020);
    *dr_byte = data;

    // Wait for transfer complete
    while (!(t.read(QUADSPI::SR{}) & (1U << 1))) {
    }
    t.write(QUADSPI::FCR::value(1U << 1)); // Clear TCF
    qspi_wait_busy();
}

/// Write single byte to QSPI Data Register (HAL-like abstraction)
/// Replaces direct 0x52005020 access
inline void qspi_write_dr_byte(std::uint8_t data) {
    auto* const dr = reinterpret_cast<volatile std::uint8_t*>(0x52005020);
    *dr = data;
}

/// Read single byte from QSPI Data Register (HAL-like abstraction)
/// Replaces direct 0x52005020 access
inline std::uint8_t qspi_read_dr_byte() {
    auto* const dr = reinterpret_cast<volatile std::uint8_t*>(0x52005020);
    return *dr;
}

/// Auto-polling: wait for status register bit to match (libDaisy compatible)
/// @param cmd Status register read command
/// @param mask Bits to check
/// @param match Expected value (matched when (status & mask) == match)
/// @param interval Polling interval in QSPI clock cycles (default 0x10)
/// @return true on success, false on timeout
inline bool qspi_auto_poll(std::uint8_t cmd,
                           std::uint32_t mask,
                           std::uint32_t match,
                           std::uint32_t interval = 0x10,
                           std::uint32_t timeout_us = 5000000) {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    qspi_wait_busy();

    // Enable APMS (auto poll mode stop) so BUSY clears after match
    auto cr = t.read(QUADSPI::CR{});
    t.write(QUADSPI::CR::value(cr | (1U << 22))); // APMS

    t.write(QUADSPI::DLR::value(0));        // 1 byte status
    t.write(QUADSPI::PSMKR::value(mask));   // Mask
    t.write(QUADSPI::PSMAR::value(match));  // Match value
    t.write(QUADSPI::PIR::value(interval)); // Polling interval

    // Configure CCR: Single-line instruction, no address, single-line data, auto-polling mode
    t.write(QUADSPI::CCR::value(static_cast<std::uint32_t>(cmd) |
                                (qspi_mode::SINGLE << 8) |  // IMODE: instruction on 1 line
                                (qspi_mode::NONE << 10) |   // ADMODE: no address
                                (qspi_mode::SINGLE << 24) | // DMODE: data on 1 line
                                (qspi_fmode::AUTO_POLLING << 26)));

    std::uint32_t timeout_count = timeout_us * 10; // Rough approximation
    while (timeout_count > 0) {
        std::uint32_t sr = t.read(QUADSPI::SR{});
        if (sr & (1U << 3)) {
            break;
        }
        if (sr & (1U << 0)) {
            return false;
        }
        if (sr & (1U << 4)) {
            return false;
        }
        --timeout_count;
    }
    if (timeout_count == 0) {
        auto cr_val = t.read(QUADSPI::CR{});
        t.write(QUADSPI::CR::value(cr_val | (1U << 1)));
        while (t.read(QUADSPI::CR{}) & (1U << 1)) {
        }
        t.write(QUADSPI::FCR::value(0x1F));
        return false;
    }
    t.write(QUADSPI::FCR::value(1U << 3));
    qspi_wait_busy();
    return true;
}

/// Enable write operations (WREN command) with WEL confirmation via auto-poll
/// libDaisy compatible: uses HAL_QSPI_AutoPolling for WEL check
inline bool qspi_write_enable() {
    using namespace ::umi::stm32h7;
    qspi_command(Flash::WRITE_ENABLE);

    // Wait for command to complete
    qspi_wait_busy();

    // Auto-poll until WEL bit is set (libDaisy style)
    // Match: WEL=1, Mask: WEL bit only
    std::uint32_t timeout = 1000000;
    while (timeout-- > 0) {
        std::uint8_t sr = qspi_read_byte(Flash::READ_STATUS);
        if (sr & Flash::SR_WEL) {
            return true;
        }
    }

    qspi_command(Flash::EXIT_DEEP_POWER_DOWN);
    qspi_command(Flash::RESET_ENABLE);
    qspi_command(Flash::RESET_DEVICE);

    qspi_command(Flash::WRITE_ENABLE);

    timeout = 1000000;
    while (timeout-- > 0) {
        std::uint8_t sr = qspi_read_byte(Flash::READ_STATUS);
        if (sr & Flash::SR_WEL) {
            return true;
        }
    }

    qspi_record_fail(16);
    return false;
}

/// Wait for memory ready (WIP=0) using auto-polling (libDaisy compatible)
/// This is the equivalent of libDaisy's AutopollingMemReady()
/// @param timeout_us Timeout in microseconds (default 5 seconds for erase operations)
/// @return true if ready, false if timeout
inline bool qspi_wait_ready(std::uint32_t timeout_us = 5000000) {
    // Auto-poll until WIP bit clears
    // Match: WIP=0 (0x00), Mask: WIP bit
    std::uint32_t timeout = timeout_us * 10;
    while (timeout-- > 0) {
        std::uint8_t sr = qspi_read_byte(Flash::READ_STATUS);
        if ((sr & Flash::SR_WIP) == 0) {
            return true;
        }
    }
    qspi_record_fail(17);
    return false;
}

/// Enable Quad mode (set QE bit in status register) - libDaisy compatible
inline bool qspi_quad_enable() {
    using namespace ::umi::stm32h7;

    // Enable write
    if (!qspi_write_enable())
        return false;

    // Write status register with QE bit set
    qspi_write_byte(Flash::WRITE_STATUS, Flash::SR_QE);

    // Wait until QE is set and WIP/WEL are cleared (libDaisy style)
    // Match: QE=1, WEL=0, WIP=0
    // Mask: QE|WEL|WIP
    // Use longer interval (0x8000) like libDaisy for status register writes
    if (!qspi_auto_poll(Flash::READ_STATUS, Flash::SR_QE | Flash::SR_WEL | Flash::SR_WIP, Flash::SR_QE, 0x8000)) {
        return false;
    }

    // Final wait for memory ready
    return qspi_wait_ready();
}

/// Configure dummy cycles for quad read (IS25LP064A) - libDaisy compatible
inline bool qspi_set_read_parameters() {
    using namespace ::umi::stm32h7;

    // Enable write
    if (!qspi_write_enable())
        return false;

    // Write Read Parameters register
    qspi_write_byte(Flash::WRITE_READ_PARAM, Flash::READ_PARAM_DEFAULT);

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
    constexpr std::uint32_t SCB_CCSIDR_ADDR = 0xE000ED80;
    constexpr std::uint32_t SCB_CSSELR_ADDR = 0xE000ED84;
    constexpr std::uint32_t SCB_DCISW_ADDR = 0xE000EF60; // D-cache invalidate by set/way

    auto* const ccsidr = reinterpret_cast<volatile std::uint32_t*>(SCB_CCSIDR_ADDR);
    auto* const csselr = reinterpret_cast<volatile std::uint32_t*>(SCB_CSSELR_ADDR);
    auto* const dcisw = reinterpret_cast<volatile std::uint32_t*>(SCB_DCISW_ADDR);

    *csselr = 0;
    __asm__ volatile("dsb sy\nisb" ::: "memory");

    std::uint32_t ccsidr_val = *ccsidr;
    std::uint32_t sets = (ccsidr_val >> 13) & 0x7FFF;
    std::uint32_t ways = (ccsidr_val >> 3) & 0x3FF;

    std::uint32_t set_shift = ((ccsidr_val & 0x7) + 4); // LineSize + 4
    std::uint32_t way_shift = 32 - __builtin_clz(ways);

    for (std::uint32_t set = 0; set <= sets; set++) {
        for (std::uint32_t way = 0; way <= ways; way++) {
            *dcisw = (way << way_shift) | (set << set_shift);
        }
    }

    // DSB to ensure cache invalidation completes
    __asm__ volatile("dsb sy" ::: "memory");
}

/// Reset QSPI peripheral via RCC
inline void qspi_reset_peripheral() {
    // AHB3RSTR offset 0x7C, QSPIRST = bit 14
    constexpr std::uint32_t AHB3RSTR_ADDR = 0x5802'4400 + 0x7C;
    auto* const ahb3rstr = reinterpret_cast<volatile std::uint32_t*>(AHB3RSTR_ADDR);
    *ahb3rstr |= (1U << 14); // FORCE_RESET
    for (int i = 0; i < 100; ++i) {
        __asm__ volatile("nop");
    }
    *ahb3rstr &= ~(1U << 14); // RELEASE_RESET
    for (int i = 0; i < 100; ++i) {
        __asm__ volatile("nop");
    }
}

/// Configure QSPI controller (shared between PreInit and Init)
inline void qspi_configure_controller() {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    // Abort any ongoing operation (also flushes FIFO)
    auto cr = t.read(QUADSPI::CR{});
    t.write(QUADSPI::CR::value(cr | (1U << 1))); // ABORT
    while (t.read(QUADSPI::CR{}) & (1U << 1)) {
    }

    // Clear all flags (FIFO flush equivalent)
    t.write(QUADSPI::FCR::value(0x1F)); // Clear TEF, TCF, SMF, TOF, CTEF

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
}

/// libDaisy PreInit() equivalent:
/// Configure only IO0/IO1/CLK/NCS for single-line mode
/// Reset flash and set DefaultStatusRegister
inline void qspi_preinit() {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    // Enable QSPI clock
    t.modify(RCC::AHB3ENR::QSPIEN::Set{});
    [[maybe_unused]] auto dummy = t.read(RCC::AHB3ENR{});

    // Reset QSPI peripheral
    qspi_reset_peripheral();

    // Pre-init IO2/IO3 HIGH as GPIO (disables WP#/HOLD#)
    init_qspi_gpio_preinit();

    // Configure only single-line pins: IO0, IO1, CLK, NCS
    // IO2/IO3 stay as GPIO HIGH
    t.modify(RCC::AHB4ENR::GPIOFEN::Set{});
    t.modify(RCC::AHB4ENR::GPIOGEN::Set{});
    dummy = t.read(RCC::AHB4ENR{});

    // PF8=IO0 (AF10), PF9=IO1 (AF10), PF10=CLK (AF9)
    gpio_configure_pin<GPIOF>(
        t, 8, gpio_mode::ALTERNATE, gpio_otype::PUSH_PULL, gpio_speed::VERY_HIGH, gpio_pupd::NONE);
    gpio_set_af<GPIOF>(t, 8, 10);
    gpio_configure_pin<GPIOF>(
        t, 9, gpio_mode::ALTERNATE, gpio_otype::PUSH_PULL, gpio_speed::VERY_HIGH, gpio_pupd::NONE);
    gpio_set_af<GPIOF>(t, 9, 10);
    gpio_configure_pin<GPIOF>(
        t, 10, gpio_mode::ALTERNATE, gpio_otype::PUSH_PULL, gpio_speed::VERY_HIGH, gpio_pupd::NONE);
    gpio_set_af<GPIOF>(t, 10, 9);
    // PG6=NCS (AF10)
    gpio_configure_pin<GPIOG>(
        t, 6, gpio_mode::ALTERNATE, gpio_otype::PUSH_PULL, gpio_speed::VERY_HIGH, gpio_pupd::PULL_UP);
    gpio_set_af<GPIOG>(t, 6, 10);

    // Configure QSPI controller
    qspi_configure_controller();

    // Reset flash device (single-line mode)
    qspi_command(Flash::RESET_ENABLE);
    qspi_command(Flash::RESET_DEVICE);
    qspi_wait_ready();

    // DefaultStatusRegister: Set status register to 0x40 (QE=1 only, all BP bits=0)
    // This clears any block protection and enables Quad mode
    // libDaisy: reg = 0x40; WriteStatusReg(reg); AutoPolling(Mask=0xFF, Match=0x40)
    qspi_write_enable();
    qspi_write_byte(Flash::WRITE_STATUS, 0x40); // QE=1, BP0-BP3=0, WEL=0, WIP=0

    // Auto-poll until status register equals 0x40 exactly (libDaisy compatible)
    // Mask=0xFF (check all bits), Match=0x40 (QE=1, others=0), Interval=0x8000
    qspi_auto_poll(Flash::READ_STATUS, 0xFF, 0x40, 0x8000);
}

/// libDaisy DeInit() equivalent: Reset QSPI peripheral and release IO2/IO3 pins
inline void qspi_deinit() {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    // Disable QSPI
    auto cr = t.read(QUADSPI::CR{});
    t.write(QUADSPI::CR::value(cr & ~(1U << 0)));

    // Reset QSPI peripheral
    qspi_reset_peripheral();

    // Release IO2/IO3 pins back to GPIO mode (HIGH) - libDaisy HAL_QSPI_DeInit compatible
    // PF6=IO3, PF7=IO2 - set back to GPIO output HIGH
    gpio_configure_pin<GPIOF>(t, 6, gpio_mode::OUTPUT, gpio_otype::PUSH_PULL, gpio_speed::LOW, gpio_pupd::NONE);
    gpio_configure_pin<GPIOF>(t, 7, gpio_mode::OUTPUT, gpio_otype::PUSH_PULL, gpio_speed::LOW, gpio_pupd::NONE);
    auto odr = t.read(GPIOF::ODR{});
    t.write(GPIOF::ODR::value(odr | (1U << 6) | (1U << 7)));
}

/// Set QSPI mode (libDaisy SetMode pattern)
/// Switches between INDIRECT_POLLING and MEMORY_MAPPED modes
/// without full peripheral reset
inline bool qspi_set_mode(QspiMode mode) {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    if (g_qspi_current_mode == mode)
        return true;

    qspi_wait_busy();

    // Abort any ongoing operation
    auto cr = t.read(QUADSPI::CR{});
    t.write(QUADSPI::CR::value(cr | (1U << 1))); // ABORT
    while (t.read(QUADSPI::CR{}) & (1U << 1)) {
    }
    t.write(QUADSPI::FCR::value(0x1F));

    if (mode == QspiMode::INDIRECT_POLLING) {
        // Clear CCR to exit memory-mapped mode
        t.write(QUADSPI::CCR::value(0));
        qspi_wait_busy();
    } else {
        // MEMORY_MAPPED: Reconfigure CCR for quad I/O read (0xEB)
        t.write(QUADSPI::ABR::value(0x000000A0));
        t.write(QUADSPI::CCR::value(static_cast<std::uint32_t>(Flash::QUAD_IO_FAST_READ) |
                                    (qspi_mode::SINGLE << 8) |          // IMODE: single line for instruction
                                    (qspi_mode::QUAD << 10) |           // ADMODE: quad for address
                                    (qspi_adsize::ADDR_24BIT << 12) |   // ADSIZE: 24-bit
                                    (qspi_mode::QUAD << 14) |           // ABMODE: quad for alternate bytes
                                    (0U << 16) |                        // ABSIZE: 0 (no alternate bytes)
                                    (8U << 18) |                        // DCYC: 8 dummy cycles
                                    (qspi_mode::QUAD << 24) |           // DMODE: quad for data
                                    (qspi_fmode::MEMORY_MAPPED << 26) | // FMODE: memory-mapped
                                    (1U << 28)                          // SIOO: send instruction only once
                                    ));
        qspi_invalidate_icache();
        qspi_invalidate_dcache();
    }

    g_qspi_current_mode = mode;
    return true;
}

/// Initialize QSPI in memory-mapped mode (libDaisy 2-stage compatible)
inline void init_qspi() {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    // ========== Stage 1: PreInit (single-line mode) ==========
    // Reset flash and set DefaultStatusRegister to clear block protection
    qspi_preinit();

    // DeInit: Reset QSPI peripheral before full init
    qspi_deinit();

    // ========== Stage 2: Full Init (quad mode) ==========
    // Enable QSPI clock
    t.modify(RCC::AHB3ENR::QSPIEN::Set{});
    [[maybe_unused]] auto dummy = t.read(RCC::AHB3ENR{});

    // Reset QSPI peripheral
    qspi_reset_peripheral();

    // Configure all QSPI GPIO pins (including IO2/IO3 for quad mode)
    init_qspi_gpio();

    // Configure QSPI controller
    qspi_configure_controller();

    // Flash is already reset and has QE=1 from PreInit
    // Just verify it's ready
    qspi_wait_ready();

    // Set Read Parameters (dummy cycles configuration)
    // libDaisy: DummyCyclesConfig() with 0xF0
    qspi_set_read_parameters();
    qspi_wait_ready();

    // Verify/re-enable Quad mode with proper autopoll confirmation
    qspi_quad_enable();
    qspi_wait_ready();

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
    t.write(QUADSPI::CCR::value(static_cast<std::uint32_t>(Flash::QUAD_IO_FAST_READ) | // 0xEB
                                (qspi_mode::SINGLE << 8) |                     // IMODE: single line for instruction
                                (qspi_mode::QUAD << 10) |                      // ADMODE: quad for address
                                (qspi_adsize::ADDR_24BIT << 12) |              // ADSIZE: 24-bit
                                (qspi_mode::QUAD << 14) |                      // ABMODE: quad for alternate bytes
                                (0U << 16) |                                   // ABSIZE: 8-bit (0b00)
                                (8U << 18) |                                   // DCYC: 8 dummy cycles
                                (qspi_mode::QUAD << 24) |                      // DMODE: quad for data
                                (qspi_fmode::MEMORY_MAPPED << 26) | (1U << 28) // SIOO: send instruction only once
                                ));

    // Invalidate I-Cache for QSPI region to ensure fresh data is read
    qspi_invalidate_icache();
}

} // namespace umi::daisy

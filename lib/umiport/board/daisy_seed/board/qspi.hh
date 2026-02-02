// SPDX-License-Identifier: MIT
// Daisy Seed QSPI initialization (IS25LP080D / IS25LP064A)
// Memory-mapped mode at 0x9000'0000
#pragma once

#include <cstdint>
#include <mcu/rcc.hh>
#include <mcu/gpio.hh>
#include <mcu/qspi.hh>
#include <mmio/transport/direct.hh>

namespace umi::daisy {

/// Initialize QSPI GPIO pins
inline void init_qspi_gpio() {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    t.modify(RCC::AHB4ENR::GPIOFEN::Set{});
    t.modify(RCC::AHB4ENR::GPIOGEN::Set{});
    [[maybe_unused]] auto dummy = t.read(RCC::AHB4ENR{});

    auto cfg_f = [&](std::uint8_t pin, std::uint8_t af) {
        gpio_configure_pin<GPIOF>(t, pin, gpio_mode::ALTERNATE, gpio_otype::PUSH_PULL,
                                   gpio_speed::VERY_HIGH, gpio_pupd::NONE);
        gpio_set_af<GPIOF>(t, pin, af);
    };

    cfg_f(6, 9);   // IO3 (AF9)
    cfg_f(7, 9);   // IO2 (AF9)
    cfg_f(8, 10);  // IO0 (AF10)
    cfg_f(9, 10);  // IO1 (AF10)
    cfg_f(10, 9);  // CLK (AF9)

    // PG6=NCS (AF10)
    gpio_configure_pin<GPIOG>(t, 6, gpio_mode::ALTERNATE, gpio_otype::PUSH_PULL,
                               gpio_speed::VERY_HIGH, gpio_pupd::PULL_UP);
    gpio_set_af<GPIOG>(t, 6, 10);
}

/// Wait for QSPI not busy
inline void qspi_wait_busy() {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;
    while (t.read(QUADSPI::SR{}) & (1U << 5)) {}
}

/// Send single-line command (no data)
inline void qspi_command(std::uint8_t cmd) {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    qspi_wait_busy();
    t.write(QUADSPI::CCR::value(
        static_cast<std::uint32_t>(cmd) |
        (qspi_mode::SINGLE << 8) |
        (qspi_mode::NONE << 10) |
        (qspi_mode::NONE << 24) |
        (qspi_fmode::INDIRECT_WRITE << 26)
    ));
}

/// Auto-polling: wait for status register bit to match
inline void qspi_auto_poll(std::uint8_t cmd, std::uint32_t mask, std::uint32_t match) {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    qspi_wait_busy();

    // Enable APMS (auto poll mode stop) so BUSY clears after match
    auto cr = t.read(QUADSPI::CR{});
    t.write(QUADSPI::CR::value(cr | (1U << 22)));  // APMS

    t.write(QUADSPI::DLR::value(0));
    t.write(QUADSPI::PSMKR::value(mask));
    t.write(QUADSPI::PSMAR::value(match));
    t.write(QUADSPI::PIR::value(0x10));

    t.write(QUADSPI::CCR::value(
        static_cast<std::uint32_t>(cmd) |
        (qspi_mode::SINGLE << 8) |
        (qspi_mode::NONE << 10) |
        (qspi_mode::SINGLE << 24) |
        (qspi_fmode::AUTO_POLLING << 26)
    ));

    // Wait for status match
    while (!(t.read(QUADSPI::SR{}) & (1U << 3))) {}
    t.write(QUADSPI::FCR::value(1U << 3));  // Clear SMF
    qspi_wait_busy();
}

/// Initialize QSPI in memory-mapped mode
inline void init_qspi() {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> t;

    // Enable QSPI clock
    t.modify(RCC::AHB3ENR::QSPIEN::Set{});
    [[maybe_unused]] auto dummy = t.read(RCC::AHB3ENR{});

    init_qspi_gpio();

    // Abort any ongoing operation
    auto cr = t.read(QUADSPI::CR{});
    t.write(QUADSPI::CR::value(cr | (1U << 1)));  // ABORT
    while (t.read(QUADSPI::CR{}) & (1U << 1)) {}

    // Configure: prescaler=1 (HCLK/2=120MHz), FIFO threshold=1
    t.write(QUADSPI::CR::value(
        (1U << 24) |    // PRESCALER: 1
        (0U << 8) |     // FTHRES: 0
        (0U << 4)       // SSHIFT: none
    ));

    // Device config: 8MB flash (FSIZE=22), CS high = 2 cycles
    t.write(QUADSPI::DCR::value(
        (22U << 16) |   // FSIZE
        (1U << 8) |     // CSHT: 2 cycles
        (0U << 0)       // CKMODE: 0
    ));

    // Enable QSPI
    cr = t.read(QUADSPI::CR{});
    t.write(QUADSPI::CR::value(cr | (1U << 0)));

    // Reset flash device
    qspi_command(is25lp::RESET_ENABLE);
    qspi_command(is25lp::RESET_DEVICE);

    // Wait for device ready (WIP=0)
    qspi_auto_poll(is25lp::READ_STATUS, 0x01, 0x00);

    // Enter memory-mapped mode with quad read (0xEB)
    qspi_wait_busy();
    t.write(QUADSPI::CCR::value(
        static_cast<std::uint32_t>(is25lp::QUAD_READ) |
        (qspi_mode::SINGLE << 8) |
        (qspi_mode::QUAD << 10) |
        (qspi_adsize::ADDR_24BIT << 12) |
        (qspi_mode::QUAD << 24) |
        (6U << 18) |
        (qspi_fmode::MEMORY_MAPPED << 26) |
        (1U << 28)  // SIOO
    ));
}

} // namespace umi::daisy

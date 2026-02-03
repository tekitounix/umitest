// SPDX-License-Identifier: MIT
// Daisy Seed Audio Initialization (PLL3, SAI1, DMA, Codec)
#pragma once

#include <cstdint>
#include <mcu/rcc.hh>
#include <mcu/gpio.hh>
#include <mcu/sai.hh>
#include <mcu/dma.hh>
#include <mcu/i2c.hh>
#include <transport/direct.hh>
#include "board/bsp.hh"
#include <umiport/device/pcm3060/pcm3060.hh>
#include <umiport/device/wm8731/wm8731.hh>
#include <umiport/device/ak4556/ak4556.hh>

namespace umi::daisy {

// Audio buffer configuration
constexpr std::uint32_t AUDIO_SAMPLE_RATE = 48000;
constexpr std::uint32_t AUDIO_BLOCK_SIZE = 48;     // Samples per channel per callback
constexpr std::uint32_t AUDIO_CHANNELS = 2;         // Stereo
constexpr std::uint32_t AUDIO_BUFFER_SIZE = AUDIO_BLOCK_SIZE * AUDIO_CHANNELS * 2;  // Double buffer

/// Initialize PLL3 for SAI1 audio clock
/// HSE(16MHz) / M(6) * N(295) / P(16) = ~49.17MHz → SAI MCLK divider → 12.288MHz
inline void init_pll3() {
    using namespace stm32h7;
    mm::DirectTransportT<> transport;

    // Configure PLL3 prescaler: DIVM3 = 6 (16MHz / 6 = 2.667MHz VCO input)
    transport.modify(RCC::PLLCKSELR::DIVM3::value(6));

    // PLL3 config: wide VCO, input range 2-4MHz, enable P/Q/R outputs
    transport.modify(
        RCC::PLLCFGR::PLL3VCOSEL::Reset{},              // Wide VCO
        RCC::PLLCFGR::PLL3RGE::value(rcc_pllrge::RANGE_2_4MHZ),
        RCC::PLLCFGR::DIVP3EN::Set{},
        RCC::PLLCFGR::DIVQ3EN::Set{},
        RCC::PLLCFGR::DIVR3EN::Set{}
    );

    // PLL3 dividers: N=295-1=294, P=16-1=15
    transport.write(
        RCC::PLL3DIVR::DIVN3::value(294),
        RCC::PLL3DIVR::DIVP3::value(15),
        RCC::PLL3DIVR::DIVQ3::value(3),    // Q not critical
        RCC::PLL3DIVR::DIVR3::value(31)    // R not critical
    );

    // Enable PLL3
    transport.modify(RCC::CR::PLL3ON::Set{});
    while (!transport.is(RCC::CR::PLL3RDY::Set{})) {}

    // Select PLL3P as SAI1 clock source
    transport.modify(RCC::D2CCIP1R::SAI1SEL::value(rcc_saisel::PLL3P));
}

/// Initialize SAI1 GPIO pins (all on GPIOE, AF6)
/// PE2=MCLK, PE3=SB(RX), PE4=FS, PE5=SCK, PE6=SA(TX)
inline void init_sai_gpio() {
    using namespace stm32h7;
    mm::DirectTransportT<> transport;

    // Enable GPIOE clock
    transport.modify(RCC::AHB4ENR::GPIOEEN::Set{});
    [[maybe_unused]] auto dummy = transport.read(RCC::AHB4ENR{});

    // Configure PE2-PE6 as AF6 (SAI1)
    constexpr std::uint8_t af6 = 6;
    constexpr std::uint8_t pins[] = {2, 3, 4, 5, 6};

    for (auto pin : pins) {
        gpio_configure_pin<GPIOE>(transport, pin,
                                   gpio_mode::ALTERNATE, gpio_otype::PUSH_PULL,
                                   gpio_speed::MEDIUM, gpio_pupd::PULL_UP);
        // Set alternate function to AF6
        gpio_set_af<GPIOE>(transport, pin, af6);
    }
}

/// Configure SAI1 Block A as Master TX, Block B as Slave RX
/// 24-bit MSB-justified, stereo, 48kHz
inline void init_sai() {
    using namespace stm32h7;
    mm::DirectTransportT<> transport;

    // Enable SAI1 clock
    transport.modify(RCC::APB2ENR::SAI1EN::Set{});
    [[maybe_unused]] auto dummy = transport.read(RCC::APB2ENR{});

    // --- Block A: Master TX ---
    // Disable first
    transport.modify(SAI1_A::CR1::SAIEN::Reset{});

    // Wait until SAI is disabled
    // (in practice: poll until SAIEN reads back 0)

    // CR1: Master TX, 24-bit, MSB first, rising edge clock strobe (TX),
    //       master clock generation enabled, NODIV=0 (divider enabled)
    // SAI_MASTERDIVIDER_ENABLE = 0x00000000 → NODIV bit = 0 (divider enabled)
    // MCKDIV: NODIV=0, OSR=0 → MCKDIV = SAI_CK / (FS × (OSR+1) × 256)
    //         = 49.17MHz / (48kHz × 1 × 256) ≈ 4
    transport.write(
        SAI1_A::CR1::MODE::value(sai_mode::MASTER_TX),
        SAI1_A::CR1::DS::value(sai_ds::DS_24BIT),
        SAI1_A::CR1::CKSTR::Reset{},     // Clock strobing: rising edge (TX, matches libDaisy)
        SAI1_A::CR1::SYNCEN::value(sai_sync::ASYNC),  // Async (master)
        SAI1_A::CR1::OUTDRIV::Reset{},   // Output drive disabled
        SAI1_A::CR1::MCKEN::Set{},        // Master clock generation
        SAI1_A::CR1::MCKDIV::value(4),   // FS = PLL3P / (MCKDIV × 256) = 49.17MHz / 1024 ≈ 48kHz
        SAI1_A::CR1::DMAEN::Set{},       // DMA enable
        SAI1_A::CR1::NODIV::Reset{}      // Divider enabled (matches libDaisy)
    );

    // CR2: FIFO threshold empty (matches libDaisy)
    transport.write(
        SAI1_A::CR2::FTH::value(sai_fth::EMPTY)
    );

    // Frame: 64-bit frame (2 × 32-bit slots for 24-bit MSB-justified)
    // FRL = 64-1 = 63, FSALL = 32-1 = 31
    transport.write(
        SAI1_A::FRCR::FRL::value(63),
        SAI1_A::FRCR::FSALL::value(31),
        SAI1_A::FRCR::FSDEF::Set{},      // FS is channel identification
        SAI1_A::FRCR::FSPOL::Set{},      // FS active high (MSB-justified)
        SAI1_A::FRCR::FSOFF::Reset{}     // FS on first bit (MSB-justified)
    );

    // Slots: 2 slots, 32-bit slot size, first bit offset = 0, both slots enabled
    transport.write(
        SAI1_A::SLOTR::FBOFF::value(0),
        SAI1_A::SLOTR::SLOTSZ::value(sai_slotsz::SZ_32BIT),
        SAI1_A::SLOTR::NBSLOT::value(1),   // 2 slots (field = N-1)
        SAI1_A::SLOTR::SLOTEN::value(0x0003) // Slots 0 and 1 enabled
    );

    // --- Block B: Slave RX (synchronized to Block A) ---
    transport.modify(SAI1_B::CR1::SAIEN::Reset{});

    transport.write(
        SAI1_B::CR1::MODE::value(sai_mode::SLAVE_RX),
        SAI1_B::CR1::DS::value(sai_ds::DS_24BIT),
        SAI1_B::CR1::CKSTR::Set{},       // Clock strobing: falling edge (RX, matches libDaisy)
        SAI1_B::CR1::SYNCEN::value(sai_sync::INTERNAL),  // Sync to Block A
        SAI1_B::CR1::DMAEN::Set{},
        SAI1_B::CR1::NODIV::Set{}         // No divider for slave
    );

    transport.write(
        SAI1_B::CR2::FTH::value(sai_fth::EMPTY)
    );

    // Same frame config as Block A (64-bit frame for 24-bit MSB-justified)
    transport.write(
        SAI1_B::FRCR::FRL::value(63),
        SAI1_B::FRCR::FSALL::value(31),
        SAI1_B::FRCR::FSDEF::Set{},
        SAI1_B::FRCR::FSPOL::Set{},      // FS active high (MSB-justified)
        SAI1_B::FRCR::FSOFF::Reset{}     // FS on first bit (MSB-justified)
    );

    transport.write(
        SAI1_B::SLOTR::FBOFF::value(0),
        SAI1_B::SLOTR::SLOTSZ::value(sai_slotsz::SZ_32BIT),
        SAI1_B::SLOTR::NBSLOT::value(1),
        SAI1_B::SLOTR::SLOTEN::value(0x0003)
    );
}

/// Configure DMA1 for SAI1 audio
/// Stream 0: SAI1_A TX (memory→peripheral), Stream 1: SAI1_B RX (peripheral→memory)
/// Both in circular mode with half/full transfer interrupts
inline void init_audio_dma(std::int32_t* tx_buf, std::int32_t* rx_buf, std::uint32_t buf_size) {
    using namespace stm32h7;
    mm::DirectTransportT<> transport;

    // Enable DMA1 clock and D2 SRAM clocks (DMA buffers in D2 SRAM)
    transport.modify(RCC::AHB1ENR::DMA1EN::Set{});
    transport.modify(RCC::AHB2ENR::D2SRAM1EN::Set{});
    transport.modify(RCC::AHB2ENR::D2SRAM2EN::Set{});
    [[maybe_unused]] auto dummy = transport.read(RCC::AHB2ENR{});

    // --- DMAMUX: route SAI1_A to DMA1_Stream0, SAI1_B to DMA1_Stream1 ---
    transport.write(DMAMUX1_Ch0::CCR::value(dmamux_req::SAI1_A));
    transport.write(DMAMUX1_Ch1::CCR::value(dmamux_req::SAI1_B));

    // --- DMA1_Stream0: SAI1_A TX (memory → peripheral) ---
    // Disable stream first
    transport.modify(DMA1_Stream0::CR::EN::Reset{});

    // Clear all interrupt flags for stream 0
    transport.write(DMA1::LIFCR::value(dma_flags::S0_ALL));

    // Configure stream
    transport.write(
        DMA1_Stream0::CR::DIR::value(dma_dir::MEM_TO_PERIPH),
        DMA1_Stream0::CR::CIRC::Set{},        // Circular mode
        DMA1_Stream0::CR::PINC::Reset{},       // Peripheral fixed
        DMA1_Stream0::CR::MINC::Set{},         // Memory increment
        DMA1_Stream0::CR::PSIZE::value(dma_size::WORD),
        DMA1_Stream0::CR::MSIZE::value(dma_size::WORD),
        DMA1_Stream0::CR::PL::value(dma_pl::HIGH),
        DMA1_Stream0::CR::HTIE::Set{},         // Half transfer interrupt
        DMA1_Stream0::CR::TCIE::Set{}          // Transfer complete interrupt
    );

    // Direct mode (no FIFO) — DMDIS=0, FTH=don't care
    transport.write(DMA1_Stream0::FCR::value(0));

    // Set addresses and count
    transport.write(DMA1_Stream0::PAR::value(SAI1_A::base_address + 0x1C));  // SAI1_A DR
    transport.write(DMA1_Stream0::M0AR::value(reinterpret_cast<std::uint32_t>(tx_buf)));
    transport.write(DMA1_Stream0::NDTR::value(buf_size));

    // --- DMA1_Stream1: SAI1_B RX (peripheral → memory) ---
    transport.modify(DMA1_Stream1::CR::EN::Reset{});
    transport.write(DMA1::LIFCR::value(dma_flags::S1_ALL));

    transport.write(
        DMA1_Stream1::CR::DIR::value(dma_dir::PERIPH_TO_MEM),
        DMA1_Stream1::CR::CIRC::Set{},
        DMA1_Stream1::CR::PINC::Reset{},
        DMA1_Stream1::CR::MINC::Set{},
        DMA1_Stream1::CR::PSIZE::value(dma_size::WORD),
        DMA1_Stream1::CR::MSIZE::value(dma_size::WORD),
        DMA1_Stream1::CR::PL::value(dma_pl::HIGH),
        DMA1_Stream1::CR::HTIE::Set{},
        DMA1_Stream1::CR::TCIE::Set{}
    );

    // Direct mode (no FIFO)
    transport.write(DMA1_Stream1::FCR::value(0));

    transport.write(DMA1_Stream1::PAR::value(SAI1_B::base_address + 0x1C));  // SAI1_B DR
    transport.write(DMA1_Stream1::M0AR::value(reinterpret_cast<std::uint32_t>(rx_buf)));
    transport.write(DMA1_Stream1::NDTR::value(buf_size));
}

/// Start audio streaming (enable DMA streams, then SAI blocks)
inline void start_audio() {
    using namespace stm32h7;
    mm::DirectTransportT<> transport;

    // Enable DMA streams
    transport.modify(DMA1_Stream0::CR::EN::Set{});
    transport.modify(DMA1_Stream1::CR::EN::Set{});

    // Enable SAI blocks (slave first, then master)
    transport.modify(SAI1_B::CR1::SAIEN::Set{});
    transport.modify(SAI1_A::CR1::SAIEN::Set{});
}

/// Stop audio streaming
inline void stop_audio() {
    using namespace stm32h7;
    mm::DirectTransportT<> transport;

    // Disable SAI (master first)
    transport.modify(SAI1_A::CR1::SAIEN::Reset{});
    transport.modify(SAI1_B::CR1::SAIEN::Reset{});

    // Disable DMA
    transport.modify(DMA1_Stream0::CR::EN::Reset{});
    transport.modify(DMA1_Stream1::CR::EN::Reset{});
}

// ============================================================================
// I2C4 for codec control (PB6=SCL, PB7=SDA, AF6)
// ============================================================================

/// Initialize I2C4 peripheral for codec control (100kHz standard mode)
inline void init_i2c4() {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> transport;

    // Enable GPIOB and I2C4 clocks
    transport.modify(RCC::AHB4ENR::GPIOBEN::Set{});
    transport.modify(RCC::APB4ENR::I2C4EN::Set{});
    [[maybe_unused]] auto dummy = transport.read(RCC::APB4ENR{});

    // Configure PB6(SCL) and PB7(SDA) as AF6, open-drain, pull-up
    for (std::uint8_t pin : {SeedBoard::i2c_scl_pin, SeedBoard::i2c_sda_pin}) {
        gpio_configure_pin<GPIOB>(transport, pin,
                                   gpio_mode::ALTERNATE, gpio_otype::OPEN_DRAIN,
                                   gpio_speed::MEDIUM, gpio_pupd::PULL_UP);
        gpio_set_af<GPIOB>(transport, pin, SeedBoard::i2c_af);
    }

    // Disable I2C4 before configuration
    transport.modify(I2C4::CR1::PE::Reset{});

    // Timing for 100kHz @ 120MHz APB4 clock (from STM32CubeMX):
    // PRESC=0x9, SCLDEL=0x4, SDADEL=0x2, SCLH=0xC3, SCLL=0xC7
    transport.write(I2C4::TIMINGR::value(0x90420000 | (0xC3 << 8) | 0xC7));

    // Enable I2C4
    transport.modify(I2C4::CR1::PE::Set{});
}

/// Blocking I2C4 register write (addr=7-bit, reg+data)
inline void i2c4_write_reg(std::uint8_t dev_addr, std::uint8_t reg, std::uint8_t data) {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> transport;

    // Wait until bus is free
    while (transport.is(I2C4::ISR::BUSY::Set{})) {}

    // Configure transfer: 7-bit addr, 2 bytes (reg + data), autoend, write
    transport.write(I2C4::CR2::value(
        (static_cast<std::uint32_t>(dev_addr) << 1) |  // SADD (7-bit left-shifted)
        (2U << 16) |                                     // NBYTES=2
        (1U << 25) |                                     // AUTOEND
        (1U << 13)                                       // START
    ));

    // Send register address
    while (!transport.is(I2C4::ISR::TXIS::Set{})) {}
    transport.write(I2C4::TXDR::value(reg));

    // Send data
    while (!transport.is(I2C4::ISR::TXIS::Set{})) {}
    transport.write(I2C4::TXDR::value(data));

    // Wait for STOP
    while (!transport.is(I2C4::ISR::STOPF::Set{})) {}
    transport.write(I2C4::ICR::STOPCF::Set{});
}

/// Blocking I2C4 register read
inline std::uint8_t i2c4_read_reg(std::uint8_t dev_addr, std::uint8_t reg) {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> transport;

    while (transport.is(I2C4::ISR::BUSY::Set{})) {}

    // Write register address (1 byte, no autoend)
    transport.write(I2C4::CR2::value(
        (static_cast<std::uint32_t>(dev_addr) << 1) |
        (1U << 16) |   // NBYTES=1
        (1U << 13)     // START
    ));

    while (!transport.is(I2C4::ISR::TXIS::Set{})) {}
    transport.write(I2C4::TXDR::value(reg));

    // Wait for transfer complete
    while (!transport.is(I2C4::ISR::TC::Set{})) {}

    // Read 1 byte with autoend
    transport.write(I2C4::CR2::value(
        (static_cast<std::uint32_t>(dev_addr) << 1) |
        (1U << 16) |   // NBYTES=1
        (1U << 25) |   // AUTOEND
        (1U << 10) |   // RD_WRN=1 (read)
        (1U << 13)     // START
    ));

    while (!transport.is(I2C4::ISR::RXNE::Set{})) {}
    auto data = transport.read(I2C4::RXDR{});

    while (!transport.is(I2C4::ISR::STOPF::Set{})) {}
    transport.write(I2C4::ICR::STOPCF::Set{});

    return static_cast<std::uint8_t>(data);
}

/// WM8731 I2C write (16-bit: 7-bit addr + 9-bit data)
inline void i2c4_wm8731_write(std::uint8_t reg, std::uint16_t data) {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> transport;

    while (transport.is(I2C4::ISR::BUSY::Set{})) {}

    // 2 bytes: [(reg<<1) | data_bit8], [data_7:0]
    std::uint8_t byte0 = static_cast<std::uint8_t>((reg << 1) | ((data >> 8) & 0x01));
    std::uint8_t byte1 = static_cast<std::uint8_t>(data & 0xFF);

    transport.write(I2C4::CR2::value(
        (static_cast<std::uint32_t>(device::WM8731::i2c_address) << 1) |
        (2U << 16) |   // NBYTES=2
        (1U << 25) |   // AUTOEND
        (1U << 13)     // START
    ));

    while (!transport.is(I2C4::ISR::TXIS::Set{})) {}
    transport.write(I2C4::TXDR::value(byte0));

    while (!transport.is(I2C4::ISR::TXIS::Set{})) {}
    transport.write(I2C4::TXDR::value(byte1));

    while (!transport.is(I2C4::ISR::STOPF::Set{})) {}
    transport.write(I2C4::ICR::STOPCF::Set{});
}

/// Initialize codec based on detected board version.
/// For AK4556: just toggle reset pin (PB11).
/// For PCM3060/WM8731: init I2C4, then configure codec registers.
inline void init_codec(BoardVersion version) {
    using namespace ::umi::stm32h7;
    mm::DirectTransportT<> transport;

    switch (version) {
    case BoardVersion::SEED_REV4: {
        // AK4556: configure PB11 as output, toggle reset
        transport.modify(RCC::AHB4ENR::GPIOBEN::Set{});
        [[maybe_unused]] auto d = transport.read(RCC::AHB4ENR{});
        gpio_configure_pin<GPIOB>(transport, SeedBoard::codec_reset_pin,
                                   gpio_mode::OUTPUT, gpio_otype::PUSH_PULL,
                                   gpio_speed::LOW, gpio_pupd::NONE);
        gpio_reset<GPIOB>(transport, SeedBoard::codec_reset_pin);
        for (int i = 0; i < 100; ++i) { asm volatile("" ::: "memory"); }
        gpio_set<GPIOB>(transport, SeedBoard::codec_reset_pin);
        break;
    }
    case BoardVersion::SEED_2_DFM: {
        // PCM3060: I2C controlled
        init_i2c4();
        auto pcm_write = [](std::uint8_t reg, std::uint8_t data) {
            i2c4_write_reg(device::PCM3060::i2c_address, reg, data);
        };
        auto pcm_read = [](std::uint8_t reg) -> std::uint8_t {
            return i2c4_read_reg(device::PCM3060::i2c_address, reg);
        };
        device::PCM3060Driver driver(pcm_write, pcm_read);
        driver.init();
        break;
    }
    case BoardVersion::SEED_REV5: {
        // WM8731: I2C controlled (16-bit write protocol)
        init_i2c4();
        auto wm_write = [](std::uint8_t reg, std::uint16_t data) {
            i2c4_wm8731_write(reg, data);
        };
        device::WM8731Driver driver(wm_write);
        driver.init();
        break;
    }
    }
}

} // namespace umi::daisy

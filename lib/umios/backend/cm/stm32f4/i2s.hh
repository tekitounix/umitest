// SPDX-License-Identifier: MIT
// STM32F4 I2S Driver with DMA (for CS43L22 audio output)
#pragma once

#include <cstdint>
#include "gpio.hh"

namespace umi::stm32 {

struct I2S {
    // SPI3/I2S3 used on Discovery board
    static constexpr uint32_t SPI3_BASE = 0x40003C00;

    // Register offsets
    static constexpr uint32_t CR1 = 0x00;
    static constexpr uint32_t CR2 = 0x04;
    static constexpr uint32_t SR = 0x08;
    static constexpr uint32_t DR = 0x0C;
    static constexpr uint32_t I2SCFGR = 0x1C;
    static constexpr uint32_t I2SPR = 0x20;

    // I2SCFGR bits
    static constexpr uint32_t I2SCFGR_I2SMOD = 1U << 11;
    static constexpr uint32_t I2SCFGR_I2SE = 1U << 10;
    static constexpr uint32_t I2SCFGR_I2SCFG_TX = 2U << 8;  // Master transmit
    static constexpr uint32_t I2SCFGR_I2SSTD_PHILIPS = 0U << 4;
    static constexpr uint32_t I2SCFGR_DATLEN_16 = 0U << 1;
    static constexpr uint32_t I2SCFGR_CHLEN_16 = 0U << 0;

    // I2SPR bits
    static constexpr uint32_t I2SPR_MCKOE = 1U << 9;
    static constexpr uint32_t I2SPR_ODD = 1U << 8;

    // CR2 bits
    static constexpr uint32_t CR2_TXDMAEN = 1U << 1;

    // SR bits
    static constexpr uint32_t SR_TXE = 1U << 1;

    uint32_t base;

    explicit I2S(uint32_t b = SPI3_BASE) : base(b) {}

    volatile uint32_t& reg(uint32_t offset) const {
        return *reinterpret_cast<volatile uint32_t*>(base + offset);
    }

    /// Configure I2S3 for 48kHz stereo output
    /// I2S clock source: PLLI2S (configured separately)
    void init_48khz() {
        // Disable I2S
        reg(I2SCFGR) = 0;

        // Configure I2S: Master TX, Philips standard, 16-bit
        reg(I2SCFGR) =
            I2SCFGR_I2SMOD |
            I2SCFGR_I2SCFG_TX |
            I2SCFGR_I2SSTD_PHILIPS |
            I2SCFGR_DATLEN_16 |
            I2SCFGR_CHLEN_16;

        // I2S prescaler for ~48kHz @ PLLI2S = 86MHz
        // With MCKOE=1: Fs = I2SxCLK / [256 × (2×I2SDIV + ODD)]
        // 86MHz / [256 × (2×3 + 1)] = 86MHz / 1792 = 47,991 Hz
        // I2SDIV = 3, ODD = 1
        reg(I2SPR) = I2SPR_MCKOE | I2SPR_ODD | 3;
    }

    void enable() {
        reg(I2SCFGR) |= I2SCFGR_I2SE;
    }

    void disable() {
        reg(I2SCFGR) &= ~I2SCFGR_I2SE;
    }

    void enable_dma() {
        reg(CR2) |= CR2_TXDMAEN;
    }

    void write(int16_t sample) {
        while (!(reg(SR) & SR_TXE)) {}
        reg(DR) = static_cast<uint16_t>(sample);
    }

    uint32_t dr_addr() const { return base + DR; }
};

// DMA1 Stream 5 for SPI3_TX
struct DMA_I2S {
    static constexpr uint32_t DMA1_BASE = 0x40026000;
    static constexpr uint32_t STREAM5_OFFSET = 0x10 + 0x18 * 5;  // Stream 5

    // DMA register offsets (from stream base)
    static constexpr uint32_t CR = 0x00;
    static constexpr uint32_t NDTR = 0x04;
    static constexpr uint32_t PAR = 0x08;
    static constexpr uint32_t M0AR = 0x0C;
    static constexpr uint32_t M1AR = 0x10;
    static constexpr uint32_t FCR = 0x14;

    // High interrupt status/clear registers
    static constexpr uint32_t HISR = 0x04;
    static constexpr uint32_t HIFCR = 0x0C;

    // CR bits
    static constexpr uint32_t CR_EN = 1U << 0;
    static constexpr uint32_t CR_TCIE = 1U << 4;
    static constexpr uint32_t CR_HTIE = 1U << 3;
    static constexpr uint32_t CR_TEIE = 1U << 2;
    static constexpr uint32_t CR_DIR_M2P = 1U << 6;  // Memory to peripheral
    static constexpr uint32_t CR_CIRC = 1U << 8;     // Circular mode
    static constexpr uint32_t CR_MINC = 1U << 10;    // Memory increment
    static constexpr uint32_t CR_PSIZE_16 = 1U << 11;
    static constexpr uint32_t CR_MSIZE_16 = 1U << 13;
    static constexpr uint32_t CR_PL_HIGH = 2U << 16;
    static constexpr uint32_t CR_DBM = 1U << 18;     // Double buffer mode
    static constexpr uint32_t CR_CHSEL_0 = 0U << 25; // Channel 0 for SPI3_TX

    // HISR/HIFCR bits for Stream 5
    static constexpr uint32_t TCIF5 = 1U << 11;
    static constexpr uint32_t HTIF5 = 1U << 10;
    static constexpr uint32_t TEIF5 = 1U << 9;

    uint32_t stream_base;

    DMA_I2S() : stream_base(DMA1_BASE + STREAM5_OFFSET) {}

    volatile uint32_t& reg(uint32_t offset) const {
        return *reinterpret_cast<volatile uint32_t*>(stream_base + offset);
    }

    volatile uint32_t& dma_reg(uint32_t offset) const {
        return *reinterpret_cast<volatile uint32_t*>(DMA1_BASE + offset);
    }

    /// Initialize DMA for double-buffered I2S output
    /// @param buf0 First buffer (samples)
    /// @param buf1 Second buffer (samples)
    /// @param samples Number of samples per buffer (total, both channels)
    void init(int16_t* buf0, int16_t* buf1, uint32_t samples, uint32_t peripheral_addr) {
        // Disable stream
        reg(CR) = 0;
        while (reg(CR) & CR_EN) {}

        // Clear interrupt flags
        dma_reg(HIFCR) = TCIF5 | HTIF5 | TEIF5;

        // Configure stream
        reg(PAR) = peripheral_addr;
        reg(M0AR) = reinterpret_cast<uint32_t>(buf0);
        reg(M1AR) = reinterpret_cast<uint32_t>(buf1);
        reg(NDTR) = samples;

        // CR: M2P, circular, memory increment, 16-bit, high priority, double buffer, channel 0
        reg(CR) =
            CR_CHSEL_0 |
            CR_PL_HIGH |
            CR_MSIZE_16 |
            CR_PSIZE_16 |
            CR_MINC |
            CR_CIRC |
            CR_DIR_M2P |
            CR_DBM |
            CR_TCIE;  // Transfer complete interrupt

        // Disable FIFO (direct mode)
        reg(FCR) = 0;
    }

    void enable() {
        reg(CR) |= CR_EN;
    }

    void disable() {
        reg(CR) &= ~CR_EN;
    }

    bool transfer_complete() const {
        return (dma_reg(HISR) & TCIF5) != 0;
    }

    void clear_tc() {
        dma_reg(HIFCR) = TCIF5;
    }

    /// Returns which buffer is currently being used (0 or 1)
    uint8_t current_buffer() const {
        return (reg(CR) & (1U << 19)) ? 1 : 0;
    }
};

}  // namespace umi::stm32

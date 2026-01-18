// SPDX-License-Identifier: MIT
// STM32F4 I2C Driver (for CS43L22 codec control)
// Based on STM32CubeF4 HAL implementation patterns
#pragma once

#include <cstdint>

namespace umi::stm32 {

struct I2C {
    static constexpr uint32_t I2C1_BASE = 0x40005400;
    static constexpr uint32_t TIMEOUT = 500000;

    // Register offsets
    static constexpr uint32_t CR1 = 0x00;
    static constexpr uint32_t CR2 = 0x04;
    static constexpr uint32_t OAR1 = 0x08;
    static constexpr uint32_t DR = 0x10;
    static constexpr uint32_t SR1 = 0x14;
    static constexpr uint32_t SR2 = 0x18;
    static constexpr uint32_t CCR = 0x1C;
    static constexpr uint32_t TRISE = 0x20;

    // CR1 bits
    static constexpr uint32_t CR1_PE = 1U << 0;
    static constexpr uint32_t CR1_START = 1U << 8;
    static constexpr uint32_t CR1_STOP = 1U << 9;
    static constexpr uint32_t CR1_ACK = 1U << 10;
    static constexpr uint32_t CR1_POS = 1U << 11;
    static constexpr uint32_t CR1_SWRST = 1U << 15;

    // SR1 bits
    static constexpr uint32_t SR1_SB = 1U << 0;
    static constexpr uint32_t SR1_ADDR = 1U << 1;
    static constexpr uint32_t SR1_BTF = 1U << 2;
    static constexpr uint32_t SR1_RXNE = 1U << 6;
    static constexpr uint32_t SR1_TXE = 1U << 7;
    static constexpr uint32_t SR1_BERR = 1U << 8;
    static constexpr uint32_t SR1_ARLO = 1U << 9;
    static constexpr uint32_t SR1_AF = 1U << 10;
    static constexpr uint32_t SR1_OVR = 1U << 11;

    // SR2 bits
    static constexpr uint32_t SR2_BUSY = 1U << 1;

    uint32_t base;
    volatile uint32_t last_error = 0;

    explicit I2C(uint32_t b = I2C1_BASE) : base(b) {}

    volatile uint32_t& reg(uint32_t offset) const {
        return *reinterpret_cast<volatile uint32_t*>(base + offset);
    }

    static void delay() {
        for (int i = 0; i < 10; ++i) {
            asm volatile("" ::: "memory");
        }
    }

    /// Clear ADDR flag by reading SR1 then SR2 (per STM32 reference manual)
    void clear_addr_flag() const {
        volatile uint32_t tmpreg = reg(SR1);
        tmpreg = reg(SR2);
        (void)tmpreg;
    }

    /// Wait for bus to be free
    bool wait_not_busy() {
        for (uint32_t i = 0; i < TIMEOUT; ++i) {
            if ((reg(SR2) & SR2_BUSY) == 0) {
                return true;
            }
            delay();
        }
        last_error = 3;
        return false;
    }

    /// Clear any pending errors
    void clear_errors() const {
        uint32_t sr1 = reg(SR1);
        if ((sr1 & (SR1_BERR | SR1_ARLO | SR1_AF | SR1_OVR)) != 0) {
            reg(SR1) = ~(SR1_BERR | SR1_ARLO | SR1_AF | SR1_OVR);
        }
    }

    /// Software reset I2C peripheral
    void reset() const {
        reg(CR1) |= CR1_SWRST;
        delay();
        reg(CR1) &= ~CR1_SWRST;
        delay();
    }

    bool wait_flag(uint32_t flag) {
        for (uint32_t i = 0; i < TIMEOUT; ++i) {
            uint32_t sr1 = reg(SR1);
            if ((sr1 & flag) != 0) {
                return true;
            }
            if ((sr1 & SR1_AF) != 0) {
                reg(SR1) = ~SR1_AF;
                reg(CR1) |= CR1_STOP;
                last_error = 1;
                return false;
            }
            if ((sr1 & (SR1_BERR | SR1_ARLO | SR1_OVR)) != 0) {
                clear_errors();
                reg(CR1) |= CR1_STOP;
                last_error = 4;
                return false;
            }
        }
        last_error = 2;
        return false;
    }

    /// Initialize I2C at ~100kHz (APB1 = 42MHz)
    void init() {
        // Disable I2C first
        reg(CR1) = 0;
        delay();

        // Reset I2C
        reg(CR1) = CR1_SWRST;
        for (int i = 0; i < 1000; ++i) {
            asm volatile("" ::: "memory");
        }
        reg(CR1) = 0;
        for (int i = 0; i < 1000; ++i) {
            asm volatile("" ::: "memory");
        }

        // APB1 frequency in CR2 (42MHz)
        reg(CR2) = 42;

        // CCR for 100kHz: CCR = 42MHz / (2 * 100kHz) = 210
        reg(CCR) = 210;

        // TRISE = (1us / Tpclk) + 1 = (1us * 42MHz) + 1 = 43
        reg(TRISE) = 43;

        // Enable I2C
        reg(CR1) = CR1_PE;
        delay();
    }

    bool write(uint8_t addr, uint8_t reg_addr, uint8_t data) {
        last_error = 0;
        clear_errors();

        if (!wait_not_busy()) {
            reset();
            init();
            return false;
        }

        // Disable POS
        reg(CR1) &= ~CR1_POS;

        // Enable ACK
        reg(CR1) |= CR1_ACK;

        // Generate START
        reg(CR1) |= CR1_START;
        if (!wait_flag(SR1_SB)) {
            return false;
        }

        // Send address (write)
        reg(DR) = static_cast<uint32_t>(addr) << 1;
        if (!wait_flag(SR1_ADDR)) {
            return false;
        }
        clear_addr_flag();

        // Wait for TXE and send register address
        if (!wait_flag(SR1_TXE)) {
            return false;
        }
        reg(DR) = reg_addr;

        // Wait for TXE and send data
        if (!wait_flag(SR1_TXE)) {
            return false;
        }
        reg(DR) = data;

        // Wait for BTF (transfer complete)
        if (!wait_flag(SR1_BTF)) {
            return false;
        }

        // Generate STOP
        reg(CR1) |= CR1_STOP;

        return true;
    }

    uint8_t read(uint8_t addr, uint8_t reg_addr) {
        last_error = 0;
        clear_errors();

        if (!wait_not_busy()) {
            reset();
            init();
            return 0xFF;
        }

        // Disable POS
        reg(CR1) &= ~CR1_POS;

        // Enable ACK
        reg(CR1) |= CR1_ACK;

        // === Phase 1: Send memory address ===

        // Generate START
        reg(CR1) |= CR1_START;
        if (!wait_flag(SR1_SB)) {
            return 0xFF;
        }

        // Send device address (write mode)
        reg(DR) = static_cast<uint32_t>(addr) << 1;
        if (!wait_flag(SR1_ADDR)) {
            return 0xFF;
        }
        clear_addr_flag();

        // Wait for TXE
        if (!wait_flag(SR1_TXE)) {
            return 0xFF;
        }

        // Send register address
        reg(DR) = reg_addr;

        // Wait for TXE (byte shifted to shift register)
        if (!wait_flag(SR1_TXE)) {
            return 0xFF;
        }

        // === Phase 2: Repeated START for read ===

        // Generate repeated START
        reg(CR1) |= CR1_START;
        if (!wait_flag(SR1_SB)) {
            return 0xFF;
        }

        // Send device address (read mode)
        reg(DR) = (static_cast<uint32_t>(addr) << 1) | 1U;
        if (!wait_flag(SR1_ADDR)) {
            return 0xFF;
        }

        // For 1-byte reception: disable ACK before clearing ADDR
        reg(CR1) &= ~CR1_ACK;

        // Clear ADDR flag
        clear_addr_flag();

        // Generate STOP (must be after ADDR clear for single byte)
        reg(CR1) |= CR1_STOP;

        // Wait for RXNE
        if (!wait_flag(SR1_RXNE)) {
            return 0xFF;
        }

        return static_cast<uint8_t>(reg(DR));
    }
};

}  // namespace umi::stm32

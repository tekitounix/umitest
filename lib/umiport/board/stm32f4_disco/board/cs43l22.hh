// SPDX-License-Identifier: MIT
// CS43L22 Audio DAC Driver (STM32F4-Discovery)
#pragma once

#include <cstdint>
#include <mcu/i2c.hh>

namespace umi::stm32 {

/// CS43L22 Audio DAC on STM32F4-Discovery
/// Connected via I2C1 (PB6=SCL, PB9=SDA)
/// Reset pin: PD4
class CS43L22 {
public:
    static constexpr uint8_t I2C_ADDR = 0x4A;  // 7-bit address

    // Register addresses
    static constexpr uint8_t REG_ID = 0x01;
    static constexpr uint8_t REG_POWER_CTL1 = 0x02;
    static constexpr uint8_t REG_POWER_CTL2 = 0x04;
    static constexpr uint8_t REG_CLOCKING_CTL = 0x05;
    static constexpr uint8_t REG_INTERFACE_CTL1 = 0x06;
    static constexpr uint8_t REG_INTERFACE_CTL2 = 0x07;
    static constexpr uint8_t REG_PASSTHROUGH_A = 0x08;
    static constexpr uint8_t REG_PASSTHROUGH_B = 0x09;
    static constexpr uint8_t REG_ANALOG_SET = 0x0A;
    static constexpr uint8_t REG_PASSTHROUGH_GANG = 0x0C;
    static constexpr uint8_t REG_PLAYBACK_CTL1 = 0x0D;
    static constexpr uint8_t REG_MISC_CTL = 0x0E;
    static constexpr uint8_t REG_PLAYBACK_CTL2 = 0x0F;
    static constexpr uint8_t REG_PASSTHROUGH_VOL_A = 0x14;
    static constexpr uint8_t REG_PASSTHROUGH_VOL_B = 0x15;
    static constexpr uint8_t REG_PCMA_VOL = 0x1A;
    static constexpr uint8_t REG_PCMB_VOL = 0x1B;
    static constexpr uint8_t REG_BEEP_FREQ = 0x1C;
    static constexpr uint8_t REG_BEEP_VOL = 0x1D;
    static constexpr uint8_t REG_BEEP_CONF = 0x1E;
    static constexpr uint8_t REG_TONE_CTL = 0x1F;
    static constexpr uint8_t REG_MASTER_VOL_A = 0x20;
    static constexpr uint8_t REG_MASTER_VOL_B = 0x21;
    static constexpr uint8_t REG_HP_VOL_A = 0x22;
    static constexpr uint8_t REG_HP_VOL_B = 0x23;
    static constexpr uint8_t REG_SPEAKER_VOL_A = 0x24;
    static constexpr uint8_t REG_SPEAKER_VOL_B = 0x25;
    static constexpr uint8_t REG_CH_MIXER = 0x26;
    static constexpr uint8_t REG_LIMIT_CTL1 = 0x27;
    static constexpr uint8_t REG_LIMIT_CTL2 = 0x28;
    static constexpr uint8_t REG_LIMIT_ATTACK = 0x29;
    static constexpr uint8_t REG_STATUS = 0x2E;
    static constexpr uint8_t REG_BATTERY_COMP = 0x2F;
    static constexpr uint8_t REG_VP_BATTERY = 0x30;
    static constexpr uint8_t REG_SPEAKER_STATUS = 0x31;
    static constexpr uint8_t REG_CHARGE_PUMP = 0x34;

    explicit CS43L22(I2C& i2c) : i2c_(i2c) {}

    void write_reg(uint8_t reg, uint8_t val) {
        i2c_.write(I2C_ADDR, reg, val);
    }

    uint8_t read_reg(uint8_t reg) {
        return i2c_.read(I2C_ADDR, reg);
    }

    /// Initialize codec for I2S input, headphone output
    /// Must be called after hardware reset (PD4 high)
    bool init(bool use_24bit = false) {
        // Check chip ID (should be 0xE0-0xE7)
        uint8_t id = read_reg(REG_ID);
        if ((id & 0xF8) != 0xE0) {
            return false;
        }

        // Power down
        write_reg(REG_POWER_CTL1, 0x01);

        // Set headphone output (disable speaker)
        write_reg(REG_POWER_CTL2, 0xAF);

        // Auto-detect clock, internal MCLK/LRCK ratio
        write_reg(REG_CLOCKING_CTL, 0x81);

        // I2S format, slave mode, 16/24-bit
        // 0x04 = I2S 16-bit, 0x06 = I2S 24-bit (STM32F4-Discovery default)
        write_reg(REG_INTERFACE_CTL1, use_24bit ? 0x06 : 0x04);

        // Disable passthrough
        write_reg(REG_PASSTHROUGH_A, 0x00);
        write_reg(REG_PASSTHROUGH_B, 0x00);

        // Analog soft ramp/zero cross
        write_reg(REG_ANALOG_SET, 0x00);

        // No limit attack
        write_reg(REG_LIMIT_CTL1, 0x00);

        // PCM volume: 0dB
        write_reg(REG_PCMA_VOL, 0x00);
        write_reg(REG_PCMB_VOL, 0x00);

        // Disable tone control
        write_reg(REG_TONE_CTL, 0x0F);

        // Playback control
        write_reg(REG_PLAYBACK_CTL1, 0x00);
        write_reg(REG_PLAYBACK_CTL2, 0x00);

        // Master volume: 0dB
        write_reg(REG_MASTER_VOL_A, 0x00);
        write_reg(REG_MASTER_VOL_B, 0x00);

        // Headphone volume: 0dB
        write_reg(REG_HP_VOL_A, 0x00);
        write_reg(REG_HP_VOL_B, 0x00);

        return true;
    }

    /// Power on and enable outputs
    void power_on() {
        write_reg(REG_POWER_CTL1, 0x9E);
    }

    /// Power down
    void power_off() {
        write_reg(REG_POWER_CTL1, 0x01);
    }

    /// Set master volume (-102dB to +12dB in 0.5dB steps)
    /// @param vol_db Volume in dB (-102 to +12)
    void set_volume(int vol_db) {
        // Clamp range
        if (vol_db < -102) vol_db = -102;
        if (vol_db > 12) vol_db = 12;

        // Convert to register value
        // 0x00 = 0dB, 0x01 = +0.5dB, ..., 0x18 = +12dB
        // 0xFF = -0.5dB, 0xFE = -1dB, ..., 0x34 = -102dB
        uint8_t reg_val;
        if (vol_db >= 0) {
            reg_val = static_cast<uint8_t>(vol_db * 2);
        } else {
            reg_val = static_cast<uint8_t>(256 + vol_db * 2);
        }

        write_reg(REG_MASTER_VOL_A, reg_val);
        write_reg(REG_MASTER_VOL_B, reg_val);
    }

    /// Mute/unmute
    void mute(bool m) {
        if (m) {
            write_reg(REG_POWER_CTL2, 0xFF);  // Mute all
        } else {
            write_reg(REG_POWER_CTL2, 0xAF);  // Headphone on
        }
    }

private:
    I2C& i2c_;
};

}  // namespace umi::stm32

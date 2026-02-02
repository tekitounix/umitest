// SPDX-License-Identifier: MIT
// PCM3060 Audio Codec - Transport-independent driver
#pragma once

#include "pcm3060_regs.hh"
#include <cstdint>

namespace umi::device {

/// PCM3060 stereo ADC/DAC codec driver
/// Daisy Seed 2 DFM uses this codec (I2C controlled).
/// @tparam I2cWrite  callable: void(uint8_t reg, uint8_t data)
/// @tparam I2cRead   callable: uint8_t(uint8_t reg)
template <typename I2cWrite, typename I2cRead>
class PCM3060Driver {
    I2cWrite write_reg;
    I2cRead read_reg;

public:
    using Regs = PCM3060;

    constexpr PCM3060Driver(I2cWrite w, I2cRead r) : write_reg(w), read_reg(r) {}

    /// Initialize codec: release reset, configure as slave, left-justified (MSB) format
    void init() {
        // System reset release, both ADC and DAC powered on
        write_reg(0x40, 0x00);

        // DAC: slave mode, left-justified, no de-emphasis, single-ended
        write_reg(0x41, (pcm3060_fmt::LEFT_JUST << 1) | 0x01);  // SE=1, FMT=left-just

        // ADC: slave mode, left-justified, HPF enabled
        write_reg(0x44, (pcm3060_fmt::LEFT_JUST << 1));

        // DAC attenuation: 0dB (0xFF = 0dB, 0x00 = mute)
        write_reg(0x42, 0xFF);
        write_reg(0x43, 0xFF);

        // ADC attenuation: 0dB
        write_reg(0x45, 0xD7);  // 0dB default
        write_reg(0x46, 0xD7);
    }

    /// Power down codec
    void power_down() {
        write_reg(0x40, 0x70);  // MRST + ADPSV + DAPSV
    }

    /// Set DAC attenuation (both channels)
    /// @param att  0x00=mute, 0xFF=0dB
    void set_dac_volume(std::uint8_t att) {
        write_reg(0x42, att);
        write_reg(0x43, att);
    }

    /// Mute DAC (soft mute via DMC bit)
    void mute_dac(bool m) {
        auto val = read_reg(0x41);
        if (m) {
            val |= (1 << 5);   // DMC=1
        } else {
            val &= ~(1 << 5);  // DMC=0
        }
        write_reg(0x41, val);
    }
};

} // namespace umi::device

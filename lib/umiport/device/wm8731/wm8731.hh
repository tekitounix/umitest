// SPDX-License-Identifier: MIT
// WM8731 Audio Codec - Transport-independent driver
// WM8731 uses 7-bit register address + 9-bit data in a 16-bit I2C word.
#pragma once

#include "wm8731_regs.hh"
#include <cstdint>

namespace umi::device {

/// WM8731 codec driver
/// Daisy Seed Rev5 uses this codec (I2C controlled).
/// WM8731 I2C protocol: 2 bytes per write: [addr(7) | data_msb(1)] [data_lsb(8)]
/// @tparam I2cWrite16  callable: void(uint8_t reg_addr_7bit, uint16_t data_9bit)
template <typename I2cWrite16>
class WM8731Driver {
    I2cWrite16 write_reg;

public:
    using Regs = WM8731;

    constexpr explicit WM8731Driver(I2cWrite16 w) : write_reg(w) {}

    /// Reset codec to default state
    void reset() {
        write_reg(0x0F, 0x000);
    }

    /// Initialize codec: slave mode, left-justified 24-bit, 48kHz, line input to DAC
    void init() {
        reset();

        // Power down: mic off, oscillator off, clkout off
        // Keep line-in, ADC, DAC, output on
        write_reg(0x06, 0x062);  // MICPD=1, OSCPD=1, CLKOUTPD=1

        // Digital audio interface: left-justified, 24-bit, slave
        write_reg(0x07, (wm8731_fmt::LEFT_JUST << 0) | (wm8731_iwl::IWL_24BIT << 2));

        // Sampling: normal mode, 256fs, 48kHz (SR=0000, BOSR=0)
        write_reg(0x08, 0x000);

        // Analog audio path: select DAC, line input
        write_reg(0x04, 0x012);  // DACSEL=1, MUTEMIC=1

        // Digital audio path: no de-emphasis, DAC unmute
        write_reg(0x05, 0x000);

        // Line input volume: 0dB, unmuted
        write_reg(0x00, 0x017);
        write_reg(0x01, 0x017);

        // Headphone volume: 0dB, zero-cross
        write_reg(0x02, 0x179);  // LZCEN=1, vol=0x79 (0dB)
        write_reg(0x03, 0x179);

        // Activate
        write_reg(0x09, 0x001);
    }

    /// Power down codec
    void power_down() {
        write_reg(0x09, 0x000);  // Deactivate
        write_reg(0x06, 0x0FF);  // All power down
    }

    /// Set headphone volume (both channels)
    /// @param vol  0x30(-73dB) to 0x7F(+6dB), 0x79=0dB
    void set_hp_volume(std::uint8_t vol) {
        write_reg(0x02, 0x100 | vol);  // LRHPBOTH=1
    }

    /// Mute DAC
    void mute_dac(bool m) {
        write_reg(0x05, m ? 0x008 : 0x000);
    }
};

} // namespace umi::device

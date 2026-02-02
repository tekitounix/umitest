// SPDX-License-Identifier: MIT
// CS43L22 Audio DAC - Transport-independent driver
#pragma once

#include "cs43l22_regs.hh"
#include <cstdint>

namespace device {

/// CS43L22 Audio DAC driver
/// @tparam Transport  I2C transport providing write/read/modify/is operations
template <typename Transport>
class CS43L22Driver {
    Transport& transport;

public:
    using Regs = CS43L22;

    explicit CS43L22Driver(Transport& t) noexcept : transport(t) {}

    /// Check chip ID (upper 5 bits should be 0b11100 = 0x1C)
    [[nodiscard]] bool verify_id() const noexcept {
        auto chip_id = transport.read(Regs::ID::CHIPID{});
        return chip_id == 0x1C;
    }

    /// Initialize codec for I2S input, headphone output
    bool init(bool use_24bit = false) noexcept {
        if (!verify_id()) {
            return false;
        }

        // Power down
        transport.write(Regs::POWER_CTL1::PowerDown{});

        // Headphone output (disable speaker)
        transport.write(Regs::POWER_CTL2::HeadphoneOn{});

        // Auto-detect clock
        transport.write(Regs::CLOCKING_CTL::value(0x81));

        // I2S format
        if (use_24bit) {
            transport.write(Regs::INTERFACE_CTL1::I2s24Bit{});
        } else {
            transport.write(Regs::INTERFACE_CTL1::I2s16Bit{});
        }

        // Disable passthrough
        transport.write(Regs::PASSTHROUGH_A::value(0x00));
        transport.write(Regs::PASSTHROUGH_B::value(0x00));

        // Analog soft ramp/zero cross
        transport.write(Regs::ANALOG_SET::value(0x00));

        // No limit
        transport.write(Regs::LIMIT_CTL1::value(0x00));

        // PCM volume: 0dB
        transport.write(Regs::PCMA_VOL::value(0x00));
        transport.write(Regs::PCMB_VOL::value(0x00));

        // Disable tone control
        transport.write(Regs::TONE_CTL::value(0x0F));

        // Playback control
        transport.write(Regs::PLAYBACK_CTL1::value(0x00));
        transport.write(Regs::PLAYBACK_CTL2::value(0x00));

        // Master volume: 0dB
        transport.write(Regs::MASTER_VOL_A::value(0x00));
        transport.write(Regs::MASTER_VOL_B::value(0x00));

        // Headphone volume: 0dB
        transport.write(Regs::HP_VOL_A::value(0x00));
        transport.write(Regs::HP_VOL_B::value(0x00));

        return true;
    }

    /// Power on and enable outputs
    void power_on() noexcept {
        transport.write(Regs::POWER_CTL1::PowerUp{});
    }

    /// Power down
    void power_off() noexcept {
        transport.write(Regs::POWER_CTL1::PowerDown{});
    }

    /// Set master volume (-102dB to +12dB)
    void set_volume(int vol_db) noexcept {
        if (vol_db < -102) vol_db = -102;
        if (vol_db > 12) vol_db = 12;

        std::uint8_t reg_val;
        if (vol_db >= 0) {
            reg_val = static_cast<std::uint8_t>(vol_db * 2);
        } else {
            reg_val = static_cast<std::uint8_t>(256 + vol_db * 2);
        }

        transport.write(Regs::MASTER_VOL_A::value(reg_val));
        transport.write(Regs::MASTER_VOL_B::value(reg_val));
    }

    /// Mute/unmute
    void mute(bool m) noexcept {
        if (m) {
            transport.write(Regs::POWER_CTL2::MuteAll{});
        } else {
            transport.write(Regs::POWER_CTL2::HeadphoneOn{});
        }
    }
};

} // namespace device

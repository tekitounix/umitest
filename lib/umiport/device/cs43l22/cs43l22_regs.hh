// SPDX-License-Identifier: MIT
// CS43L22 Audio DAC - Register definitions via umimmio
#pragma once

#include <umimmio.hh>

namespace device {

// NOLINTBEGIN(readability-identifier-naming)

/// CS43L22 Audio DAC register map (I2C, 8-bit registers)
struct CS43L22 : mm::Device<mm::RW, mm::I2CTransportTag> {
    static constexpr std::uint8_t i2c_address = 0x4A;

    struct ID : mm::Register<CS43L22, 0x01, 8, mm::RO> {
        struct REVID : mm::Field<ID, 0, 3> {};
        struct CHIPID : mm::Field<ID, 3, 5> {};
    };

    struct POWER_CTL1 : mm::Register<CS43L22, 0x02, 8> {
        using PowerDown = mm::Value<POWER_CTL1, 0x01>;
        using PowerUp = mm::Value<POWER_CTL1, 0x9E>;
    };

    struct POWER_CTL2 : mm::Register<CS43L22, 0x04, 8> {
        struct SPK_A : mm::Field<POWER_CTL2, 0, 2> {};
        struct SPK_B : mm::Field<POWER_CTL2, 2, 2> {};
        struct HP_A : mm::Field<POWER_CTL2, 4, 2> {};
        struct HP_B : mm::Field<POWER_CTL2, 6, 2> {};
        using HeadphoneOn = mm::Value<POWER_CTL2, 0xAF>;
        using MuteAll = mm::Value<POWER_CTL2, 0xFF>;
    };

    struct CLOCKING_CTL : mm::Register<CS43L22, 0x05, 8> {
        struct AUTO_DETECT : mm::Field<CLOCKING_CTL, 7, 1> {};
        struct SPEED : mm::Field<CLOCKING_CTL, 5, 2> {};
        struct RATIO : mm::Field<CLOCKING_CTL, 1, 2> {};
    };

    struct INTERFACE_CTL1 : mm::Register<CS43L22, 0x06, 8> {
        struct SLAVE : mm::Field<INTERFACE_CTL1, 6, 1> {};
        struct SCLK_INV : mm::Field<INTERFACE_CTL1, 5, 1> {};
        struct DSP_MODE : mm::Field<INTERFACE_CTL1, 4, 1> {};
        struct DAC_IF : mm::Field<INTERFACE_CTL1, 2, 2> {};
        struct AWL : mm::Field<INTERFACE_CTL1, 0, 2> {};
        using I2s16Bit = mm::Value<INTERFACE_CTL1, 0x04>;
        using I2s24Bit = mm::Value<INTERFACE_CTL1, 0x06>;
    };

    struct INTERFACE_CTL2 : mm::Register<CS43L22, 0x07, 8> {};
    struct PASSTHROUGH_A : mm::Register<CS43L22, 0x08, 8> {};
    struct PASSTHROUGH_B : mm::Register<CS43L22, 0x09, 8> {};
    struct ANALOG_SET : mm::Register<CS43L22, 0x0A, 8> {};
    struct PASSTHROUGH_GANG : mm::Register<CS43L22, 0x0C, 8> {};
    struct PLAYBACK_CTL1 : mm::Register<CS43L22, 0x0D, 8> {};
    struct MISC_CTL : mm::Register<CS43L22, 0x0E, 8> {};
    struct PLAYBACK_CTL2 : mm::Register<CS43L22, 0x0F, 8> {};
    struct PASSTHROUGH_VOL_A : mm::Register<CS43L22, 0x14, 8> {};
    struct PASSTHROUGH_VOL_B : mm::Register<CS43L22, 0x15, 8> {};
    struct PCMA_VOL : mm::Register<CS43L22, 0x1A, 8> {};
    struct PCMB_VOL : mm::Register<CS43L22, 0x1B, 8> {};
    struct BEEP_FREQ : mm::Register<CS43L22, 0x1C, 8> {};
    struct BEEP_VOL : mm::Register<CS43L22, 0x1D, 8> {};
    struct BEEP_CONF : mm::Register<CS43L22, 0x1E, 8> {};
    struct TONE_CTL : mm::Register<CS43L22, 0x1F, 8> {};
    struct MASTER_VOL_A : mm::Register<CS43L22, 0x20, 8> {};
    struct MASTER_VOL_B : mm::Register<CS43L22, 0x21, 8> {};
    struct HP_VOL_A : mm::Register<CS43L22, 0x22, 8> {};
    struct HP_VOL_B : mm::Register<CS43L22, 0x23, 8> {};
    struct SPEAKER_VOL_A : mm::Register<CS43L22, 0x24, 8> {};
    struct SPEAKER_VOL_B : mm::Register<CS43L22, 0x25, 8> {};
    struct CH_MIXER : mm::Register<CS43L22, 0x26, 8> {};
    struct LIMIT_CTL1 : mm::Register<CS43L22, 0x27, 8> {};
    struct LIMIT_CTL2 : mm::Register<CS43L22, 0x28, 8> {};
    struct LIMIT_ATTACK : mm::Register<CS43L22, 0x29, 8> {};
    struct STATUS : mm::Register<CS43L22, 0x2E, 8, mm::RO> {};
    struct BATTERY_COMP : mm::Register<CS43L22, 0x2F, 8> {};
    struct VP_BATTERY : mm::Register<CS43L22, 0x30, 8, mm::RO> {};
    struct SPEAKER_STATUS : mm::Register<CS43L22, 0x31, 8, mm::RO> {};
    struct CHARGE_PUMP : mm::Register<CS43L22, 0x34, 8> {};
};

// NOLINTEND(readability-identifier-naming)

} // namespace device

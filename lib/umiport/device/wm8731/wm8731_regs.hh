// SPDX-License-Identifier: MIT
// WM8731 Audio Codec - Register definitions via umimmio
// Reference: WM8731 datasheet, Table 32 (Register Map)
#pragma once

#include <mmio/mmio.hh>

namespace umi::device {

// NOLINTBEGIN(readability-identifier-naming)

/// WM8731 stereo ADC/DAC register map (I2C, 9-bit data with 7-bit address)
/// Default I2C address: 0x1A (CSB=0 on Daisy Seed Rev5)
struct WM8731 : mm::Device<mm::RW, mm::I2CTransportTag> {
    static constexpr std::uint8_t i2c_address = 0x1A;

    // WM8731 uses 7-bit register address + 9-bit data packed into 16-bit I2C write.
    // Register "offset" here represents the 7-bit address (0x00-0x0F).

    struct LINVOL : mm::Register<WM8731, 0x00, 9> {
        struct LINVOL_F : mm::Field<LINVOL, 0, 5> {};    // Left line input volume
        struct LINMUTE : mm::Field<LINVOL, 7, 1> {};     // Mute
        struct LRINBOTH : mm::Field<LINVOL, 8, 1> {};    // Both channels
    };

    struct RINVOL : mm::Register<WM8731, 0x01, 9> {
        struct RINVOL_F : mm::Field<RINVOL, 0, 5> {};
        struct RINMUTE : mm::Field<RINVOL, 7, 1> {};
        struct RLINBOTH : mm::Field<RINVOL, 8, 1> {};
    };

    struct LHPOUT : mm::Register<WM8731, 0x02, 9> {
        struct LHPVOL : mm::Field<LHPOUT, 0, 7> {};
        struct LZCEN : mm::Field<LHPOUT, 7, 1> {};
        struct LRHPBOTH : mm::Field<LHPOUT, 8, 1> {};
    };

    struct RHPOUT : mm::Register<WM8731, 0x03, 9> {
        struct RHPVOL : mm::Field<RHPOUT, 0, 7> {};
        struct RZCEN : mm::Field<RHPOUT, 7, 1> {};
        struct RLHPBOTH : mm::Field<RHPOUT, 8, 1> {};
    };

    struct AAPCTRL : mm::Register<WM8731, 0x04, 9> {
        struct MICBOOST : mm::Field<AAPCTRL, 0, 1> {};
        struct MUTEMIC : mm::Field<AAPCTRL, 1, 1> {};
        struct INSEL : mm::Field<AAPCTRL, 2, 1> {};
        struct BYPASS : mm::Field<AAPCTRL, 3, 1> {};
        struct DACSEL : mm::Field<AAPCTRL, 4, 1> {};
        struct SIDETONE : mm::Field<AAPCTRL, 5, 1> {};
        struct SIDEATT : mm::Field<AAPCTRL, 6, 2> {};
    };

    struct DAPCTRL : mm::Register<WM8731, 0x05, 9> {
        struct ADCHPD : mm::Field<DAPCTRL, 0, 1> {};
        struct DEEMP : mm::Field<DAPCTRL, 1, 2> {};
        struct DACMU : mm::Field<DAPCTRL, 3, 1> {};
        struct HPOR : mm::Field<DAPCTRL, 4, 1> {};
    };

    struct PWRDOWN : mm::Register<WM8731, 0x06, 9> {
        struct LINEINPD : mm::Field<PWRDOWN, 0, 1> {};
        struct MICPD : mm::Field<PWRDOWN, 1, 1> {};
        struct ADCPD : mm::Field<PWRDOWN, 2, 1> {};
        struct DACPD : mm::Field<PWRDOWN, 3, 1> {};
        struct OUTPD : mm::Field<PWRDOWN, 4, 1> {};
        struct OSCPD : mm::Field<PWRDOWN, 5, 1> {};
        struct CLKOUTPD : mm::Field<PWRDOWN, 6, 1> {};
        struct POWEROFF : mm::Field<PWRDOWN, 7, 1> {};
    };

    struct DAIF : mm::Register<WM8731, 0x07, 9> {
        struct FORMAT : mm::Field<DAIF, 0, 2> {};
        struct IWL : mm::Field<DAIF, 2, 2> {};
        struct LRP : mm::Field<DAIF, 4, 1> {};
        struct LRSWAP : mm::Field<DAIF, 5, 1> {};
        struct MS : mm::Field<DAIF, 6, 1> {};
        struct BCLKINV : mm::Field<DAIF, 7, 1> {};
    };

    struct SAMPLING : mm::Register<WM8731, 0x08, 9> {
        struct NORMAL_USB : mm::Field<SAMPLING, 0, 1> {};
        struct BOSR : mm::Field<SAMPLING, 1, 1> {};
        struct SR : mm::Field<SAMPLING, 2, 4> {};
        struct CLKIDIV2 : mm::Field<SAMPLING, 6, 1> {};
        struct CLKODIV2 : mm::Field<SAMPLING, 7, 1> {};
    };

    struct ACTIVE : mm::Register<WM8731, 0x09, 9> {
        struct ACTIVE_F : mm::Field<ACTIVE, 0, 1> {};
    };

    struct RESET : mm::Register<WM8731, 0x0F, 9> {};
};

// Digital audio interface format values
namespace wm8731_fmt {
constexpr std::uint32_t RIGHT_JUST = 0b00;
constexpr std::uint32_t LEFT_JUST  = 0b01;  // MSB-justified
constexpr std::uint32_t I2S        = 0b10;
constexpr std::uint32_t DSP        = 0b11;
} // namespace wm8731_fmt

// Input word length
namespace wm8731_iwl {
constexpr std::uint32_t IWL_16BIT = 0b00;
constexpr std::uint32_t IWL_20BIT = 0b01;
constexpr std::uint32_t IWL_24BIT = 0b10;
constexpr std::uint32_t IWL_32BIT = 0b11;
} // namespace wm8731_iwl

// NOLINTEND(readability-identifier-naming)

} // namespace umi::device

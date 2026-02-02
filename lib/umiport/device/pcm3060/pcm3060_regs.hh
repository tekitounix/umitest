// SPDX-License-Identifier: MIT
// PCM3060 Audio Codec - Register definitions via umimmio
// Reference: PCM3060 datasheet, Table 1 (Register Map)
#pragma once

#include <mmio/mmio.hh>

namespace umi::device {

// NOLINTBEGIN(readability-identifier-naming)

/// PCM3060 stereo ADC/DAC register map (I2C, 8-bit registers)
/// Default I2C address: 0x46 (AD0=L, AD1=L on Daisy Seed DFM)
struct PCM3060 : mm::Device<mm::RW, mm::I2CTransportTag> {
    static constexpr std::uint8_t i2c_address = 0x46;

    /// System control register (Register 64, 0x40)
    struct SYSTEM : mm::Register<PCM3060, 0x40, 8> {
        struct MRST : mm::Field<SYSTEM, 7, 1> {};   // Mode reset (1=reset)
        struct SRST : mm::Field<SYSTEM, 6, 1> {};   // System reset (1=reset)
        struct ADPSV : mm::Field<SYSTEM, 5, 1> {};  // ADC power-save (1=power-save)
        struct DAPSV : mm::Field<SYSTEM, 4, 1> {};  // DAC power-save (1=power-save)
    };

    /// DAC control register (Register 65, 0x41)
    struct DAC_CTRL : mm::Register<PCM3060, 0x41, 8> {
        struct SE : mm::Field<DAC_CTRL, 0, 1> {};     // Single-ended (0=diff, 1=SE)
        struct FMT : mm::Field<DAC_CTRL, 1, 2> {};    // Audio format
        struct DMF : mm::Field<DAC_CTRL, 3, 2> {};    // De-emphasis filter
        struct DMC : mm::Field<DAC_CTRL, 5, 1> {};    // Soft mute control
        struct MS_DA : mm::Field<DAC_CTRL, 7, 1> {};  // Master/slave (0=slave, 1=master)
    };

    /// DAC channel 1 attenuation (Register 66, 0x42)
    struct DAC_ATT1 : mm::Register<PCM3060, 0x42, 8> {};
    /// DAC channel 2 attenuation (Register 67, 0x43)
    struct DAC_ATT2 : mm::Register<PCM3060, 0x43, 8> {};

    /// ADC control register (Register 68, 0x44)
    struct ADC_CTRL : mm::Register<PCM3060, 0x44, 8> {
        struct ADIN : mm::Field<ADC_CTRL, 0, 1> {};   // ADC input config
        struct FMT : mm::Field<ADC_CTRL, 1, 2> {};    // Audio format
        struct BYP : mm::Field<ADC_CTRL, 3, 1> {};    // HPF bypass
        struct DMC : mm::Field<ADC_CTRL, 5, 1> {};    // Soft mute control
        struct MS_AD : mm::Field<ADC_CTRL, 7, 1> {};  // Master/slave (0=slave, 1=master)
    };

    /// ADC channel 1 attenuation (Register 69, 0x45)
    struct ADC_ATT1 : mm::Register<PCM3060, 0x45, 8> {};
    /// ADC channel 2 attenuation (Register 70, 0x46)
    struct ADC_ATT2 : mm::Register<PCM3060, 0x46, 8> {};
};

// Audio format values for FMT field
namespace pcm3060_fmt {
constexpr std::uint32_t I2S         = 0b00;
constexpr std::uint32_t LEFT_JUST   = 0b01;  // MSB-justified / Left-justified
constexpr std::uint32_t RIGHT_JUST  = 0b10;
} // namespace pcm3060_fmt

// NOLINTEND(readability-identifier-naming)

} // namespace umi::device

// SPDX-License-Identifier: MIT
// STM32F4 MCU Abstraction Layer
// MCU-specific peripheral initialization and control

#pragma once

#include <cstdint>
#include <span>
#include <umios/backend/cm/stm32f4/gpio.hh>
#include <umios/backend/cm/stm32f4/pdm_mic.hh>

#include "bsp.hh"

// USB types
#include <audio/audio_interface.hh>
#include <hal/stm32_otg.hh>

namespace umi::mcu {

// Re-export audio configuration from BSP for convenience
namespace audio {
inline constexpr uint32_t default_sample_rate = 48000;
inline constexpr uint32_t buffer_size = 64;
inline constexpr uint32_t i2s_words_per_frame = 4;
inline constexpr uint32_t i2s_dma_words = buffer_size * i2s_words_per_frame;
inline constexpr uint32_t pdm_buf_size = 256;
inline constexpr uint32_t pcm_buf_size = 64;
} // namespace audio

// Hardware accessors
umi::stm32::GPIO& gpio(char port);
umi::stm32::CicDecimator& cic_decimator();

// DMA buffer accessors
uint16_t* audio_buf0();
uint16_t* audio_buf1();
uint16_t* pdm_buf0();
uint16_t* pdm_buf1();
int16_t* pcm_buf();
int16_t* stereo_buf();

// Initialization
void init_clocks();
void init_gpio();
void init_audio();
void init_pdm_mic();
void init_usb();

// Runtime control
uint32_t configure_plli2s(uint32_t rate);
void dma_i2s_disable();
void dma_i2s_enable();
void dma_i2s_init();
void i2s_disable();
void i2s_enable();
void i2s_enable_dma();
uint32_t i2s_dr_addr();

// DMA buffer state
uint8_t dma_i2s_current_buffer();
uint8_t dma_pdm_current_buffer();

// IRQ control
void disable_i2s_irq();
void enable_i2s_irq();

// System control
[[noreturn]] void system_reset();

// USB accessors
umiusb::Stm32FsHal& usb_hal();

#if USB_AUDIO_UAC2
// Integrated Audio+MIDI (STM32F4 OTG FS has only 4 EPs, no room for separate MIDI class)
using UsbAudioDevice =
    umiusb::AudioClass<umiusb::UacVersion::UAC2,
                        umiusb::MaxSpeed::FULL,
                        umiusb::AudioPort<2, 24, 48000, 1, 48000, umiusb::AudioRates<48000>>,
                        umiusb::AudioPort<2, 24, 48000, 3, 48000, umiusb::AudioRates<48000>>,
                        umiusb::MidiPort<1, 2>,
                        umiusb::MidiPort<1, 1>,
                        2,
                        umiusb::AudioSyncMode::ASYNC,
                        false>;
#elif USB_AUDIO_ADAPTIVE
using UsbAudioDevice = umiusb::AudioFullDuplexMidi96kMaxAdaptive;
#else
using UsbAudioDevice = umiusb::AudioFullDuplexMidi96kMaxAsyncFixedEps;
#endif

UsbAudioDevice& usb_audio();
bool usb_is_configured();

} // namespace umi::mcu

// SPDX-License-Identifier: MIT
// STM32F4-Discovery Board Support Package
// Board-specific configuration values only (no implementation code)

#pragma once

#define USB_AUDIO_ADAPTIVE 0
#define USB_AUDIO_UAC2     0

#include <cstdint>

namespace umi::bsp::stm32f4_discovery {

// CPU configuration
inline constexpr uint32_t cpu_freq_hz = 168000000;

// LEDs: PD12=Green, PD13=Orange, PD14=Red, PD15=Blue
namespace led {
inline constexpr char gpio_port = 'D';
inline constexpr uint8_t green = 12;
inline constexpr uint8_t orange = 13;
inline constexpr uint8_t red = 14;
inline constexpr uint8_t blue = 15;
} // namespace led

// User button: PA0
namespace button {
inline constexpr char gpio_port = 'A';
inline constexpr uint8_t user = 0;
} // namespace button

// CS43L22 Audio Codec
namespace codec {
inline constexpr char reset_gpio_port = 'D';
inline constexpr uint8_t reset_pin = 4;
} // namespace codec

// I2C1 pins: PB6=SCL, PB9=SDA
namespace i2c {
inline constexpr char gpio_port = 'B';
inline constexpr uint8_t scl = 6;
inline constexpr uint8_t sda = 9;
} // namespace i2c

// I2S3 Audio OUT: PC7=MCK, PC10=SCK, PC12=SD, PA4=WS
namespace i2s_out {
inline constexpr char mck_port = 'C';
inline constexpr uint8_t mck_pin = 7;
inline constexpr char sck_port = 'C';
inline constexpr uint8_t sck_pin = 10;
inline constexpr char sd_port = 'C';
inline constexpr uint8_t sd_pin = 12;
inline constexpr char ws_port = 'A';
inline constexpr uint8_t ws_pin = 4;
} // namespace i2s_out

// I2S2 PDM Microphone: PB10=CLK, PC3=SD
namespace pdm_mic {
inline constexpr char clk_port = 'B';
inline constexpr uint8_t clk_pin = 10;
inline constexpr char sd_port = 'C';
inline constexpr uint8_t sd_pin = 3;
} // namespace pdm_mic

// USB OTG FS: PA11=DM, PA12=DP
namespace usb {
inline constexpr char gpio_port = 'A';
inline constexpr uint8_t dm = 11;
inline constexpr uint8_t dp = 12;
inline constexpr uint16_t vendor_id = 0x1209;
inline constexpr uint16_t product_id = 0x000A;
inline constexpr uint16_t device_version = 0x0305;
inline constexpr const char* manufacturer = "UMI-OS";
inline constexpr const char* product = "UMI Kernel Synth";
} // namespace usb

// Audio configuration
namespace audio {
inline constexpr uint32_t default_sample_rate = 48000;
inline constexpr uint32_t buffer_size = 64;
inline constexpr uint32_t i2s_words_per_frame = 4;
inline constexpr uint32_t i2s_dma_words = buffer_size * i2s_words_per_frame;
inline constexpr uint32_t pdm_buf_size = 256;
inline constexpr uint32_t pcm_buf_size = 64;
} // namespace audio

// Memory map (for MPU configuration)
namespace memory {
inline constexpr uintptr_t sram_base = 0x20000000;
inline constexpr uint32_t sram_size = 128 * 1024;
inline constexpr uintptr_t ccm_base = 0x10000000;
inline constexpr uint32_t ccm_size = 64 * 1024;
inline constexpr uintptr_t flash_base = 0x08000000;
inline constexpr uint32_t kernel_flash_size = 384 * 1024;
inline constexpr uintptr_t peripheral_base = 0x40000000;
inline constexpr uint32_t peripheral_size = 256 * 1024 * 1024;
inline constexpr uintptr_t shared_base = 0x20018000;
inline constexpr uint32_t shared_size = 16 * 1024;

// App memory regions
inline constexpr uintptr_t app_text_base = 0x08060000; // After kernel
inline constexpr uint32_t app_text_size = 128 * 1024;
inline constexpr uint32_t app_data_size = 32 * 1024;
inline constexpr uint32_t app_stack_size = 16 * 1024;

// Kernel memory regions
inline constexpr uint32_t kernel_ram_size = 48 * 1024;
} // namespace memory

// DMA IRQ numbers (STM32F4)
namespace irq {
inline constexpr uint32_t dma1_stream3 = 14; // PDM DMA
inline constexpr uint32_t dma1_stream5 = 16; // I2S DMA
inline constexpr uint32_t otg_fs = 67;       // USB OTG FS
} // namespace irq

} // namespace umi::bsp::stm32f4_discovery

// Convenience alias
namespace umi::bsp {
namespace board = stm32f4_discovery;
} // namespace umi::bsp

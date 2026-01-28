// SPDX-License-Identifier: MIT
// STM32F4 MCU Abstraction Layer Implementation

#include "mcu.hh"
#include "bsp.hh"

#include <cstring>

// Platform drivers
#include <umios/backend/cm/common/nvic.hh>
#include <umios/backend/cm/stm32f4/cs43l22.hh>
#include <umios/backend/cm/stm32f4/gpio.hh>
#include <umios/backend/cm/stm32f4/i2c.hh>
#include <umios/backend/cm/stm32f4/i2s.hh>
#include <umios/backend/cm/stm32f4/pdm_mic.hh>
#include <umios/backend/cm/stm32f4/rcc.hh>

// USB stack
#include <audio_interface.hh>
#include <hal/stm32_otg.hh>
#include <umiusb.hh>

using umi::stm32::GPIO;
using umi::stm32::I2C;
using umi::stm32::I2S;
using umi::stm32::DMA_I2S;
using umi::stm32::CS43L22;
using umi::stm32::PdmMic;
using umi::stm32::DmaPdm;
using umi::stm32::CicDecimator;
using umi::stm32::RCC;
using umi::port::arm::NVIC;

namespace bsp = umi::bsp::board;

namespace {

// Hardware instances
GPIO gpio_a_inst('A');
GPIO gpio_b_inst('B');
GPIO gpio_c_inst('C');
GPIO gpio_d_inst('D');
I2C i2c1;
I2S i2s3;
DMA_I2S dma_i2s;
CS43L22 codec(i2c1);

// PDM microphone
PdmMic pdm_mic;
DmaPdm dma_pdm;
CicDecimator cic_decimator_inst;

// USB stack
umiusb::Stm32FsHal usb_hal_inst;
using UsbAudioDevice = umiusb::AudioFullDuplexMidi96kMaxAsyncFixedEps;
UsbAudioDevice usb_audio_inst;
umiusb::Device<umiusb::Stm32FsHal, UsbAudioDevice>
    usb_device(usb_hal_inst,
               usb_audio_inst,
               {
                   .vendor_id = bsp::usb::vendor_id,
                   .product_id = bsp::usb::product_id,
                   .device_version = bsp::usb::device_version,
                   .manufacturer_idx = 1,
                   .product_idx = 2,
                   .serial_idx = 0,
               });

} // namespace

// DMA buffers (must be in SRAM, not CCM)
__attribute__((section(".dma_buffer"))) uint16_t audio_buf0_data[bsp::audio::i2s_dma_words];
__attribute__((section(".dma_buffer"))) uint16_t audio_buf1_data[bsp::audio::i2s_dma_words];
__attribute__((section(".dma_buffer"))) uint16_t pdm_buf0_data[bsp::audio::pdm_buf_size];
__attribute__((section(".dma_buffer"))) uint16_t pdm_buf1_data[bsp::audio::pdm_buf_size];
int16_t pcm_buf_data[bsp::audio::pcm_buf_size];
int16_t stereo_buf_data[bsp::audio::buffer_size * 2];

// USB Descriptors
namespace usb_config {
using namespace umiusb::desc;

constexpr auto str_manufacturer = String("UMI-OS");
constexpr auto str_product = String("UMI Kernel Synth");

constexpr std::array<std::span<const uint8_t>, 2> string_table = {{
    {str_manufacturer.data.data(), str_manufacturer.size},
    {str_product.data.data(), str_product.size},
}};
} // namespace usb_config

namespace umi::mcu {

namespace bsp = ::umi::bsp::board;

// Hardware accessors
GPIO& gpio(char port) {
    switch (port) {
    case 'A':
        return gpio_a_inst;
    case 'B':
        return gpio_b_inst;
    case 'C':
        return gpio_c_inst;
    case 'D':
    default:
        return gpio_d_inst;
    }
}

CicDecimator& cic_decimator() {
    return cic_decimator_inst;
}

umiusb::Stm32FsHal& usb_hal() {
    return usb_hal_inst;
}

umiusb::AudioFullDuplexMidi96kMaxAsyncFixedEps& usb_audio() {
    return usb_audio_inst;
}

// Buffer accessors
uint16_t* audio_buf0() {
    return audio_buf0_data;
}
uint16_t* audio_buf1() {
    return audio_buf1_data;
}
uint16_t* pdm_buf0() {
    return pdm_buf0_data;
}
uint16_t* pdm_buf1() {
    return pdm_buf1_data;
}
int16_t* pcm_buf() {
    return pcm_buf_data;
}
int16_t* stereo_buf() {
    return stereo_buf_data;
}

void init_clocks() {
    RCC::init_168mhz();
}

void init_gpio() {
    RCC::enable_gpio('A');
    RCC::enable_gpio('B');
    RCC::enable_gpio('C');
    RCC::enable_gpio('D');
    RCC::enable_i2c1();
    RCC::enable_spi3();
    RCC::enable_spi2();
    RCC::enable_dma1();
    RCC::enable_usb_otg_fs();

    // LEDs
    gpio_d_inst.config_output(bsp::led::green);
    gpio_d_inst.config_output(bsp::led::orange);
    gpio_d_inst.config_output(bsp::led::red);
    gpio_d_inst.config_output(bsp::led::blue);

    // USER button
    gpio(bsp::button::gpio_port).set_mode(bsp::button::user, GPIO::MODE_INPUT);
    gpio(bsp::button::gpio_port).set_pupd(bsp::button::user, GPIO::PUPD_DOWN);

    // CS43L22 Reset
    gpio(bsp::codec::reset_gpio_port).config_output(bsp::codec::reset_pin);
    gpio(bsp::codec::reset_gpio_port).reset(bsp::codec::reset_pin);

    // I2C1: SCL, SDA
    gpio(bsp::i2c::gpio_port).config_af(bsp::i2c::scl, GPIO::AF4, GPIO::SPEED_FAST, GPIO::PUPD_UP, true);
    gpio(bsp::i2c::gpio_port).config_af(bsp::i2c::sda, GPIO::AF4, GPIO::SPEED_FAST, GPIO::PUPD_UP, true);

    // I2S3 (Audio OUT): MCK, SCK, SD, WS
    gpio(bsp::i2s_out::mck_port).config_af(bsp::i2s_out::mck_pin, GPIO::AF6, GPIO::SPEED_HIGH);
    gpio(bsp::i2s_out::sck_port).config_af(bsp::i2s_out::sck_pin, GPIO::AF6, GPIO::SPEED_HIGH);
    gpio(bsp::i2s_out::sd_port).config_af(bsp::i2s_out::sd_pin, GPIO::AF6, GPIO::SPEED_HIGH);
    gpio(bsp::i2s_out::ws_port).config_af(bsp::i2s_out::ws_pin, GPIO::AF6, GPIO::SPEED_HIGH);

    // I2S2 (PDM Microphone): CLK, SD
    gpio(bsp::pdm_mic::clk_port).config_af(bsp::pdm_mic::clk_pin, GPIO::AF5, GPIO::SPEED_HIGH);
    gpio(bsp::pdm_mic::sd_port).config_af(bsp::pdm_mic::sd_pin, GPIO::AF5, GPIO::SPEED_HIGH);

    // USB OTG FS: DM, DP
    gpio(bsp::usb::gpio_port).config_af(bsp::usb::dm, GPIO::AF10, GPIO::SPEED_HIGH);
    gpio(bsp::usb::gpio_port).config_af(bsp::usb::dp, GPIO::AF10, GPIO::SPEED_HIGH);
}

static void init_plli2s() {
    configure_plli2s(bsp::audio::default_sample_rate);
}

void init_audio() {
    i2c1.init();

    // Release CS43L22 from reset
    gpio(bsp::codec::reset_gpio_port).set(bsp::codec::reset_pin);
    for (int i = 0; i < 100000; ++i) {
        asm volatile("");
    }

    if (!codec.init(true)) {
        gpio(bsp::led::gpio_port).set(bsp::led::red);
        while (1) {
        }
    }

    init_plli2s();
    i2s3.init_48khz(true);

    __builtin_memset(audio_buf0_data, 0, sizeof(audio_buf0_data));
    __builtin_memset(audio_buf1_data, 0, sizeof(audio_buf1_data));

    dma_i2s.init(audio_buf0_data, audio_buf1_data, audio::i2s_dma_words, i2s3.dr_addr());

    NVIC::set_prio(bsp::irq::dma1_stream5, 5);
    NVIC::enable(bsp::irq::dma1_stream5);

    i2s3.enable_dma();
    i2s3.enable();
    dma_i2s.enable();
    codec.power_on();
    codec.set_volume(0);
}

void init_pdm_mic() {
    pdm_mic.init();
    cic_decimator_inst.reset();

    dma_pdm.init(pdm_buf0_data, pdm_buf1_data, audio::pdm_buf_size, pdm_mic.dr_addr());

    NVIC::set_prio(bsp::irq::dma1_stream3, 5);
    NVIC::enable(bsp::irq::dma1_stream3);

    pdm_mic.enable_dma();
    pdm_mic.enable();
    dma_pdm.enable();
}

void init_usb() {
    for (int i = 0; i < 10000; ++i) {
        asm volatile("");
    }

    usb_hal_inst.disconnect();
    for (int i = 0; i < 500000; ++i) {
        asm volatile("");
    }

    // NOTE: Callbacks must be set by kernel BEFORE calling init_usb()
    // Do NOT set empty callbacks here - they would overwrite kernel's callbacks

    usb_device.set_strings(usb_config::string_table);
    usb_device.init();
    usb_hal_inst.connect();

    for (int i = 0; i < 500000; ++i) {
        asm volatile("");
    }

    NVIC::set_prio(bsp::irq::otg_fs, 6);
    NVIC::enable(bsp::irq::otg_fs);
}

uint32_t configure_plli2s(uint32_t rate) {
    constexpr uint32_t rcc_plli2scfgr = 0x40023884;
    constexpr uint32_t rcc_cr = 0x40023800;

    *reinterpret_cast<volatile uint32_t*>(rcc_cr) &= ~(1U << 26);

    for (int i = 0; i < 10000; ++i) {
        if (!(*reinterpret_cast<volatile uint32_t*>(rcc_cr) & (1U << 27)))
            break;
    }

    uint32_t plli2sn, plli2sr;
    uint32_t actual_rate;
    uint8_t i2sdiv, odd;

    switch (rate) {
    case 44100:
        plli2sn = 271;
        plli2sr = 6;
        i2sdiv = 2;
        odd = 0;
        actual_rate = 44108;
        break;

    case 96000:
        plli2sn = 295;
        plli2sr = 3;
        i2sdiv = 2;
        odd = 0;
        actual_rate = 96028;
        break;

    case 48000:
    default:
        plli2sn = 258;
        plli2sr = 3;
        i2sdiv = 3;
        odd = 1;
        actual_rate = 47991;
        break;
    }

    *reinterpret_cast<volatile uint32_t*>(rcc_plli2scfgr) = (plli2sr << 28) | (plli2sn << 6);
    *reinterpret_cast<volatile uint32_t*>(rcc_cr) |= (1U << 26);

    for (int i = 0; i < 100000; ++i) {
        if (*reinterpret_cast<volatile uint32_t*>(rcc_cr) & (1U << 27))
            break;
    }

    i2s3.init_with_divider(i2sdiv, odd, true);

    return actual_rate;
}

void dma_i2s_disable() {
    dma_i2s.disable();
}

void dma_i2s_enable() {
    dma_i2s.enable();
}

void dma_i2s_init() {
    dma_i2s.init(audio_buf0_data, audio_buf1_data, audio::i2s_dma_words, i2s3.dr_addr());
}

void i2s_disable() {
    i2s3.disable();
}

void i2s_enable() {
    i2s3.enable();
}

void i2s_enable_dma() {
    i2s3.enable_dma();
}

uint32_t i2s_dr_addr() {
    return i2s3.dr_addr();
}

uint8_t dma_i2s_current_buffer() {
    return dma_i2s.current_buffer();
}

uint8_t dma_pdm_current_buffer() {
    return dma_pdm.current_buffer();
}

void disable_i2s_irq() {
    constexpr uint32_t nvic_icer0 = 0xE000E180;
    constexpr uint32_t dma1_stream5_irqn = 16;
    *reinterpret_cast<volatile uint32_t*>(nvic_icer0) = (1U << dma1_stream5_irqn);
}

void enable_i2s_irq() {
    constexpr uint32_t nvic_iser0 = 0xE000E100;
    constexpr uint32_t dma1_stream5_irqn = 16;
    *reinterpret_cast<volatile uint32_t*>(nvic_iser0) = (1U << dma1_stream5_irqn);
}

[[noreturn]] void system_reset() {
    constexpr uint32_t aircr = 0xE000ED0C;
    constexpr uint32_t vectkey = 0x05FA0000;
    constexpr uint32_t sysresetreq = 0x00000004;
    *reinterpret_cast<volatile uint32_t*>(aircr) = vectkey | sysresetreq;
    while (true) {
    }
}

} // namespace umi::mcu

// Forward declaration for kernel callbacks
namespace umi::kernel {
void on_audio_buffer_ready(uint16_t* buf);
void on_pdm_buffer_ready(uint16_t* buf);
} // namespace umi::kernel

// IRQ Handlers
extern "C" void DMA1_Stream3_IRQHandler() {
    if (dma_pdm.transfer_complete()) {
        dma_pdm.clear_tc();
        uint16_t* completed_buf =
            (umi::mcu::dma_pdm_current_buffer() == 0) ? pdm_buf1_data : pdm_buf0_data;
        umi::kernel::on_pdm_buffer_ready(completed_buf);
    }
}

extern "C" void DMA1_Stream5_IRQHandler() {
    if (dma_i2s.transfer_complete()) {
        dma_i2s.clear_tc();
        uint16_t* completed_buf =
            (umi::mcu::dma_i2s_current_buffer() == 1) ? audio_buf0_data : audio_buf1_data;
        umi::kernel::on_audio_buffer_ready(completed_buf);
    }
}

extern "C" void OTG_FS_IRQHandler() {
    usb_device.poll();
}

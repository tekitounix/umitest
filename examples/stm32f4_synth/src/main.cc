// SPDX-License-Identifier: MIT
// STM32F4-Discovery USB MIDI Synthesizer
// Uses synth.hh from headless_webhost (unchanged)
// Uses umiusb for portable USB Device Stack

#include <cstdint>

// Platform includes
#include <platform/syscall.hh>
#include <platform/protection.hh>
#include <platform/privilege.hh>

// STM32F4 drivers
#include <umios/backend/cm/stm32f4/rcc.hh>
#include <umios/backend/cm/stm32f4/gpio.hh>
#include <umios/backend/cm/stm32f4/i2c.hh>
#include <umios/backend/cm/stm32f4/i2s.hh>
#include <umios/backend/cm/stm32f4/cs43l22.hh>
#include <umios/backend/cm/stm32f4/pdm_mic.hh>
#include <umios/backend/cm/common/systick.hh>
#include <umios/backend/cm/common/nvic.hh>
#include <umios/backend/cm/common/scb.hh>
#include <umios/backend/cm/common/dwt.hh>
#include <umios/backend/cm/cortex_m4.hh>

// USB stack (umiusb)
#include <umiusb.hh>
#include <audio_interface.hh>
#include <hal/stm32_otg.hh>

// Synth engine (shared with WASM build)
#include <synth.hh>

using namespace umi::stm32;
using namespace umi::port::arm;

// ============================================================================
// Configuration
// ============================================================================

constexpr uint32_t SAMPLE_RATE = 48000;
constexpr uint32_t BUFFER_SIZE = 64;  // Samples per channel per buffer

// ============================================================================
// Audio Buffers (DMA double-buffering)
// ============================================================================

// DMA buffers must be in SRAM, not CCM
__attribute__((section(".dma_buffer")))
int16_t audio_buf0[BUFFER_SIZE * 2];  // Stereo interleaved

__attribute__((section(".dma_buffer")))
int16_t audio_buf1[BUFFER_SIZE * 2];

// PDM microphone DMA buffers
// 64 samples at 48kHz = 1.33ms, PDM at 2.048MHz = ~2730 PDM bits = ~171 x 16-bit words
// Round up to 256 for double buffering headroom
constexpr uint32_t PDM_BUF_SIZE = 256;

__attribute__((section(".dma_buffer")))
uint16_t pdm_buf0[PDM_BUF_SIZE];

__attribute__((section(".dma_buffer")))
uint16_t pdm_buf1[PDM_BUF_SIZE];

// PCM buffer for decimated audio (from PDM)
// Each 64 PDM words -> ~1 PCM sample (64x decimation)
// So 256 PDM words -> ~4 PCM samples, but we accumulate over multiple DMA transfers
constexpr uint32_t PCM_BUF_SIZE = 128;
int16_t pcm_buf[PCM_BUF_SIZE];

// Resampled buffer (32kHz -> 48kHz)
// 3:2 ratio means 128 * 3/2 = 192 max
constexpr uint32_t RESAMPLED_BUF_SIZE = 192;
int16_t resampled_buf[RESAMPLED_BUF_SIZE];

// ============================================================================
// USB Descriptors
// ============================================================================

namespace usb_config {
using namespace umiusb::desc;

constexpr auto str_manufacturer = String("UMI-OS");
constexpr auto str_product = String("UMI Synth");

// String table: index 1 = manufacturer, index 2 = product
// (index 0 = Language ID is handled internally by device.hh)
constexpr std::array<std::span<const uint8_t>, 2> string_table = {{
    {str_manufacturer.data.data(), str_manufacturer.size},
    {str_product.data.data(), str_product.size},
}};
}  // namespace usb_config

// ============================================================================
// Global State
// ============================================================================

namespace {

// Hardware instances
GPIO gpio_a('A');
GPIO gpio_b('B');
GPIO gpio_c('C');
GPIO gpio_d('D');
I2C i2c1;
I2S i2s3;
DMA_I2S dma_i2s;
CS43L22 codec(i2c1);

// USB stack instances (umiusb) - using AudioInterface class
umiusb::Stm32FsHal usb_hal;
// UAC1, 48kHz stereo Full Duplex (Audio IN + OUT)
umiusb::AudioFullDuplex48k usb_audio;
umiusb::Device<umiusb::Stm32FsHal, decltype(usb_audio)> usb_device(
    usb_hal, usb_audio,
    {
        .vendor_id = 0x1209,
        .product_id = 0x0001,
        .device_version = 0x0100,
        .manufacturer_idx = 1,
        .product_idx = 2,
        .serial_idx = 0,
    }
);

// PDM microphone
PdmMic pdm_mic;
DmaPdm dma_pdm;
CicDecimator cic_decimator;
Resampler32to48 resampler;

// Synth engine
umi::synth::PolySynth synth;

// LED state (for activity indication)
volatile uint32_t led_counter = 0;

// PLLI2S configuration for I2S clock
void init_plli2s() {
    // PLLI2S: N=192, R=2 -> PLLI2SCLK = 8MHz * 192 / 2 = 768MHz... that's wrong
    // Actually: PLLI2SCLK = (HSE / PLLM) * PLLI2SN / PLLI2SR
    // = (8MHz / 8) * 271 / 6 = 45.17MHz... still calculating

    // For 48kHz with MCLK: Fs = I2SCLK / (256 * 2)
    // Target I2SCLK = 48000 * 256 * 2 = 24.576MHz (not exact with HSE=8MHz)
    // Close enough: PLLI2SN=258, PLLI2SR=3 -> 8 * 258 / 8 / 3 = 86MHz
    // Then I2S prescaler will divide down

    constexpr uint32_t RCC_PLLI2SCFGR = 0x40023884;
    constexpr uint32_t RCC_CR = 0x40023800;

    // Disable PLLI2S
    *reinterpret_cast<volatile uint32_t*>(RCC_CR) &= ~(1U << 26);

    // Configure: PLLI2SN=258, PLLI2SR=3
    *reinterpret_cast<volatile uint32_t*>(RCC_PLLI2SCFGR) =
        (3U << 28) |   // PLLI2SR = 3
        (258U << 6);   // PLLI2SN = 258

    // Enable PLLI2S
    *reinterpret_cast<volatile uint32_t*>(RCC_CR) |= (1U << 26);

    // Wait for lock
    while (!(*reinterpret_cast<volatile uint32_t*>(RCC_CR) & (1U << 27))) {}
}

void init_gpio() {
    // Enable GPIO clocks
    RCC::enable_gpio('A');
    RCC::enable_gpio('B');
    RCC::enable_gpio('C');
    RCC::enable_gpio('D');

    // Enable peripheral clocks before GPIO AF configuration
    RCC::enable_i2c1();
    RCC::enable_spi2();  // For PDM microphone (I2S2)
    RCC::enable_spi3();
    RCC::enable_dma1();
    RCC::enable_usb_otg_fs();

    // LEDs: PD12 (Green), PD13 (Orange), PD14 (Red), PD15 (Blue)
    gpio_d.config_output(12);
    gpio_d.config_output(13);
    gpio_d.config_output(14);
    gpio_d.config_output(15);

    // USER button: PA0 (directly connected, active high)
    gpio_a.set_mode(0, GPIO::MODE_INPUT);
    gpio_a.set_pupd(0, GPIO::PUPD_DOWN);

    // CS43L22 Reset: PD4
    gpio_d.config_output(4);
    gpio_d.reset(4);  // Hold in reset initially

    // I2C1: PB6 (SCL), PB9 (SDA) - open-drain with pull-up
    gpio_b.config_af(6, GPIO::AF4, GPIO::SPEED_FAST, GPIO::PUPD_UP, true);
    gpio_b.config_af(9, GPIO::AF4, GPIO::SPEED_FAST, GPIO::PUPD_UP, true);

    // I2S3 (Audio OUT): PC7 (MCK), PC10 (SCK), PC12 (SD), PA4 (WS)
    gpio_c.config_af(7, GPIO::AF6, GPIO::SPEED_HIGH);   // MCK
    gpio_c.config_af(10, GPIO::AF6, GPIO::SPEED_HIGH);  // SCK
    gpio_c.config_af(12, GPIO::AF6, GPIO::SPEED_HIGH);  // SD
    gpio_a.config_af(4, GPIO::AF6, GPIO::SPEED_HIGH);   // WS

    // I2S2 (PDM Microphone): PB10 (CLK), PC3 (DOUT)
    // STM32F4-Discovery: MP45DT02 MEMS microphone
    gpio_b.config_af(10, GPIO::AF5, GPIO::SPEED_HIGH);  // I2S2_CK - Clock to mic
    gpio_c.config_af(3, GPIO::AF5, GPIO::SPEED_HIGH);   // I2S2_SD - PDM data from mic

    // USB OTG FS: PA11 (DM), PA12 (DP)
    gpio_a.config_af(11, GPIO::AF10, GPIO::SPEED_HIGH);
    gpio_a.config_af(12, GPIO::AF10, GPIO::SPEED_HIGH);
}

void init_audio() {
    // Clocks already enabled in init_gpio()

    // Initialize I2C
    i2c1.init();

    // Release codec from reset
    gpio_d.set(4);
    for (int i = 0; i < 100000; ++i) { asm volatile(""); }  // Delay

    // Initialize codec
    if (!codec.init()) {
        // Error: Red LED
        gpio_d.set(14);
        while (1) {}
    }

    // Initialize PLLI2S for I2S clock
    init_plli2s();

    // Initialize I2S
    i2s3.init_48khz();

    // Clear audio buffers before DMA starts (prevent noise on startup)
    __builtin_memset(audio_buf0, 0, sizeof(audio_buf0));
    __builtin_memset(audio_buf1, 0, sizeof(audio_buf1));

    // Initialize DMA
    dma_i2s.init(audio_buf0, audio_buf1, BUFFER_SIZE * 2, i2s3.dr_addr());

    // Enable DMA interrupt (priority 5, like HAL audio examples)
    NVIC::set_prio(16, 5);  // DMA1_Stream5 = IRQ 16
    NVIC::enable(16);

    // Start audio
    i2s3.enable_dma();
    i2s3.enable();
    dma_i2s.enable();
    codec.power_on();
    codec.set_volume(0);  // 0dB
}

void init_pdm_mic() {
    // Initialize PDM microphone (I2S2)
    pdm_mic.init();

    // Initialize CIC decimator and resampler
    cic_decimator.reset();
    resampler.reset();

    // Initialize DMA for PDM input (double-buffered)
    dma_pdm.init(pdm_buf0, pdm_buf1, PDM_BUF_SIZE, pdm_mic.dr_addr());

    // Enable DMA interrupt (priority 5, same as audio)
    // DMA1_Stream3 = IRQ 14
    NVIC::set_prio(14, 5);
    NVIC::enable(14);

    // Start PDM capture
    pdm_mic.enable_dma();
    pdm_mic.enable();
    dma_pdm.enable();
}

void init_usb() {
    // USB clock already enabled in init_gpio()

    // Small delay for USB PHY
    for (int i = 0; i < 10000; ++i) { asm volatile(""); }

    // Set streaming status callback - Blue LED indicates Audio OUT streaming active
    usb_audio.on_streaming_change = [](bool streaming) {
        if (streaming) {
            gpio_d.set(15);   // Blue LED ON when streaming starts
        } else {
            gpio_d.reset(15); // Blue LED OFF when streaming stops
        }
    };

    // Set Audio IN streaming callback - Orange LED indicates Audio IN streaming active
    usb_audio.on_audio_in_change = [](bool streaming) {
        if (streaming) {
            gpio_d.set(13);   // Orange LED ON when mic streaming starts
        } else {
            gpio_d.reset(13); // Orange LED OFF when mic streaming stops
        }
    };

    // Debug: Toggle green LED on each audio packet received
    usb_audio.on_audio_rx = []() {
        static uint8_t cnt = 0;
        if (++cnt >= 48) {  // Toggle every 48 packets (~48ms)
            cnt = 0;
            gpio_d.toggle(12);
        }
    };

    // Set string descriptors
    usb_device.set_strings(usb_config::string_table);

    // Initialize USB device (umiusb)
    usb_device.init();
    usb_hal.connect();

    // Enable USB interrupt (priority 6, lower than audio DMA at 5)
    // Audio DMA must have higher priority for consistent timing
    NVIC::set_prio(67, 6);  // OTG_FS = IRQ 67
    NVIC::enable(67);
}

/// Fill audio buffer from AudioInterface ring buffer
/// Uses PI+ASRC: Software resampling with cubic hermite interpolation
void fill_audio_buffer(int16_t* buf, uint32_t frame_count) {
    usb_audio.read_audio_asrc(buf, frame_count);
    // Note: Red LED is now used for DMA activity indicator in IRQ handler
}

#if 0  // Synth + USB mix version (for reference)
void fill_audio_buffer_mix(int16_t* buf, uint32_t samples) {
    for (uint32_t i = 0; i < samples; ++i) {
        float synth_sample = synth.process_sample();
        int32_t synth_s16 = static_cast<int32_t>(synth_sample * 32767.0f);
        if (synth_s16 > 32767) { synth_s16 = 32767; }
        if (synth_s16 < -32768) { synth_s16 = -32768; }

        int16_t usb_l = 0, usb_r = 0;
        uint32_t write_pos = usb_audio_write_pos;
        uint32_t read_pos = usb_audio_read_pos;
        if (read_pos != write_pos) {
            usb_l = usb_audio_buf[read_pos * 2];
            usb_r = usb_audio_buf[read_pos * 2 + 1];
            usb_audio_read_pos = (read_pos + 1) % USB_AUDIO_BUF_SIZE;
        }

        int32_t mix_l = synth_s16 + usb_l;
        int32_t mix_r = synth_s16 + usb_r;
        if (mix_l > 32767) { mix_l = 32767; }
        if (mix_l < -32768) { mix_l = -32768; }
        if (mix_r > 32767) { mix_r = 32767; }
        if (mix_r < -32768) { mix_r = -32768; }

        buf[i * 2] = static_cast<int16_t>(mix_l);
        buf[i * 2 + 1] = static_cast<int16_t>(mix_r);
    }
}
#endif

#if 0  // Synth version (for reference)
/// Fill audio buffer with synth output
void fill_audio_buffer_synth(int16_t* buf, uint32_t samples) {
    for (uint32_t i = 0; i < samples; ++i) {
        float sample = synth.process_sample();

        // Convert to 16-bit signed
        int32_t s16 = static_cast<int32_t>(sample * 32767.0f);
        if (s16 > 32767) s16 = 32767;
        if (s16 < -32768) s16 = -32768;

        // Stereo (duplicate mono)
        buf[i * 2] = static_cast<int16_t>(s16);
        buf[i * 2 + 1] = static_cast<int16_t>(s16);
    }
}
#endif

}  // namespace

// ============================================================================
// Interrupt Handlers
// ============================================================================

extern "C" void DMA1_Stream3_IRQHandler() {
    // PDM microphone DMA transfer complete
    if (dma_pdm.transfer_complete()) {
        dma_pdm.clear_tc();

        // Get the buffer that just completed (not the one currently being filled)
        uint16_t* pdm_data = (dma_pdm.current_buffer() == 0) ? pdm_buf1 : pdm_buf0;

        // Decimate PDM to PCM (2.048MHz -> 32kHz)
        uint32_t pcm_count = cic_decimator.process_buffer(pdm_data, PDM_BUF_SIZE, pcm_buf, PCM_BUF_SIZE);

        // Resample 32kHz -> 48kHz
        if (pcm_count > 0) {
            uint32_t resampled_count = resampler.process(pcm_buf, pcm_count, resampled_buf);

            // Convert mono to stereo and send to USB Audio IN buffer
            if (resampled_count > 0 && usb_audio.is_audio_in_streaming()) {
                int16_t stereo_buf[RESAMPLED_BUF_SIZE * 2];
                for (uint32_t i = 0; i < resampled_count; ++i) {
                    stereo_buf[i * 2] = resampled_buf[i];
                    stereo_buf[i * 2 + 1] = resampled_buf[i];
                }
                usb_audio.write_audio_in(stereo_buf, resampled_count);
            }
        }
    }
}

extern "C" void DMA1_Stream5_IRQHandler() {
    if (dma_i2s.transfer_complete()) {
        dma_i2s.clear_tc();

        // Fill the buffer that just finished
        // current_buffer() returns which buffer DMA is currently using
        // We fill the OTHER one
        int16_t* buf = (dma_i2s.current_buffer() == 0) ? audio_buf1 : audio_buf0;
        fill_audio_buffer(buf, BUFFER_SIZE);

        // Activity indication: blink red LED to show DMA is still running
        led_counter = led_counter + 1;
        if (led_counter >= 750) {  // ~1Hz blink (750 * 64 samples / 48000 = ~1s)
            led_counter = 0;
            gpio_d.toggle(14);  // Red LED toggle
        }
    }
}

extern "C" void OTG_FS_IRQHandler() {
    usb_device.poll();
}

extern "C" void SysTick_Handler() {
    // SOF is now handled by USB OTG interrupt, not SysTick
}

// ============================================================================
// Startup Code
// ============================================================================

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern void (*__init_array_start[])();
extern void (*__init_array_end[])();

extern "C" [[noreturn]] void Reset_Handler() {
    // Copy .data from Flash to RAM
    uint32_t* src = &_sidata;
    for (uint32_t* dst = &_sdata; dst < &_edata;) {
        *dst++ = *src++;
    }

    // Zero .bss
    for (uint32_t* p = &_sbss; p < &_ebss;) {
        *p++ = 0;
    }

    // Enable FPU
    SCB::enable_fpu();
    asm volatile("dsb\n isb" ::: "memory");

    // Call global constructors
    for (auto fn = __init_array_start; fn < __init_array_end; ++fn) {
        (*fn)();
    }

    // Initialize clocks (168MHz)
    RCC::init_168mhz();

    // Initialize hardware
    init_gpio();

    // Green LED on during init
    gpio_d.set(12);

    // Initialize synth
    synth.init(static_cast<float>(SAMPLE_RATE));

    // Initialize audio subsystem (Audio OUT to CS43L22 DAC)
    init_audio();

    // Initialize PDM microphone (Audio IN from MP45DT02)
    init_pdm_mic();

    // Initialize USB
    init_usb();

    // Initialize SysTick (1ms)
    SysTick::init(168000 - 1);  // 168MHz / 1000

    // Enable DWT cycle counter (for debugging/profiling)
    DWT::enable();

    // Show status LED (Green = streaming active)
    gpio_d.set(12);

    // Main loop
    while (true) {
        // Check USER button (PA0) for debug info display
        if (gpio_a.read(0)) {
            // Button pressed - show debug counters via LED blink
            // Turn all LEDs off first
            gpio_d.reset(12); gpio_d.reset(13); gpio_d.reset(14); gpio_d.reset(15);

            // Blink count: STALL count (red LED)
            for (uint32_t i = 0; i < usb_hal.dbg_ep0_stall_count_ && i < 20; ++i) {
                gpio_d.set(14);
                for (int d = 0; d < 500000; ++d) asm volatile("");
                gpio_d.reset(14);
                for (int d = 0; d < 500000; ++d) asm volatile("");
            }

            // Pause
            for (int d = 0; d < 2000000; ++d) asm volatile("");

            // Blink count: SETUP count mod 10 (green LED)
            for (uint32_t i = 0; i < (usb_hal.dbg_setup_count_ % 10); ++i) {
                gpio_d.set(12);
                for (int d = 0; d < 500000; ++d) asm volatile("");
                gpio_d.reset(12);
                for (int d = 0; d < 500000; ++d) asm volatile("");
            }

            // Wait for button release
            while (gpio_a.read(0)) {}
        }

        // Wait for interrupts (saves power)
        CM4::wfi();
    }
}

// ============================================================================
// Fault Handlers
// ============================================================================

// Fault status registers for debugging
volatile uint32_t g_fault_cfsr = 0;   // Configurable Fault Status Register
volatile uint32_t g_fault_hfsr = 0;   // Hard Fault Status Register
volatile uint32_t g_fault_bfar = 0;   // Bus Fault Address Register
volatile uint32_t g_fault_mmfar = 0;  // MemManage Fault Address Register
volatile uint32_t g_fault_pc = 0;     // Faulting PC
volatile uint32_t g_fault_lr = 0;     // Link Register at fault

extern "C" [[noreturn]] void HardFault_Handler() {
    // Read fault status registers
    g_fault_cfsr = *reinterpret_cast<volatile uint32_t*>(0xE000ED28);
    g_fault_hfsr = *reinterpret_cast<volatile uint32_t*>(0xE000ED2C);
    g_fault_bfar = *reinterpret_cast<volatile uint32_t*>(0xE000ED38);
    g_fault_mmfar = *reinterpret_cast<volatile uint32_t*>(0xE000ED34);

    // Red LED on to indicate fault
    gpio_d.set(14);

    while (true) {
        asm volatile("" ::: "memory");
    }
}

extern "C" [[noreturn]] void MemManage_Handler() {
    g_fault_cfsr = *reinterpret_cast<volatile uint32_t*>(0xE000ED28);
    g_fault_mmfar = *reinterpret_cast<volatile uint32_t*>(0xE000ED34);
    gpio_d.set(14);
    while (true) { asm volatile(""); }
}

extern "C" [[noreturn]] void BusFault_Handler() {
    g_fault_cfsr = *reinterpret_cast<volatile uint32_t*>(0xE000ED28);
    g_fault_bfar = *reinterpret_cast<volatile uint32_t*>(0xE000ED38);
    gpio_d.set(14);
    while (true) { asm volatile(""); }
}

extern "C" [[noreturn]] void UsageFault_Handler() {
    g_fault_cfsr = *reinterpret_cast<volatile uint32_t*>(0xE000ED28);
    gpio_d.set(14);
    while (true) { asm volatile(""); }
}

// Default handler for unused interrupts
extern "C" void Default_Handler() {
    gpio_d.set(14);  // Red LED
    while (true) { asm volatile(""); }
}

// Vector table - 84 entries total (16 system + 68 external for USB at 67)
// DMA1_Stream3 = IRQ 14 (position 30 in table = 16 system + 14) - PDM microphone
// DMA1_Stream5 = IRQ 16 (position 32 in table = 16 system + 16) - Audio OUT
// OTG_FS = IRQ 67 (position 83 in table = 16 system + 67)
__attribute__((section(".isr_vector"), used))
const void* const g_vector_table[16 + 68] = {
    // System exceptions (0-15)
    reinterpret_cast<const void*>(&_estack),         // 0: Initial SP
    reinterpret_cast<const void*>(Reset_Handler),    // 1: Reset
    reinterpret_cast<const void*>(Default_Handler),  // 2: NMI
    reinterpret_cast<const void*>(HardFault_Handler),  // 3: HardFault
    reinterpret_cast<const void*>(MemManage_Handler),  // 4: MemManage
    reinterpret_cast<const void*>(BusFault_Handler),   // 5: BusFault
    reinterpret_cast<const void*>(UsageFault_Handler), // 6: UsageFault
    nullptr, nullptr, nullptr, nullptr,              // 7-10: Reserved
    reinterpret_cast<const void*>(Default_Handler),  // 11: SVCall
    nullptr, nullptr,                                // 12-13: Reserved
    reinterpret_cast<const void*>(Default_Handler),  // 14: PendSV
    reinterpret_cast<const void*>(SysTick_Handler),  // 15: SysTick
    // External interrupts starting at index 16
    // IRQ 0-13: (table index 16-29)
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    // IRQ 14: DMA1_Stream3 (table index 30)
    reinterpret_cast<const void*>(DMA1_Stream3_IRQHandler),  // IRQ 14: DMA1_Stream3
    // IRQ 15: (table index 31)
    nullptr,
    // IRQ 16-31: (table index 32-47) DMA1_Stream5 = IRQ 16 (table index 32)
    reinterpret_cast<const void*>(DMA1_Stream5_IRQHandler),  // IRQ 16: DMA1_Stream5
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    // IRQ 32-47: (table index 48-63)
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    // IRQ 48-63: (table index 64-79)
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    // IRQ 64-67: (table index 80-83) OTG_FS = IRQ 67 (table index 83)
    nullptr, nullptr, nullptr,
    reinterpret_cast<const void*>(OTG_FS_IRQHandler),  // IRQ 67: OTG_FS
};

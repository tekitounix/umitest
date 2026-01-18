// SPDX-License-Identifier: MIT
// STM32F4-Discovery USB MIDI Synthesizer
// Uses synth.hh from headless_webhost (unchanged)

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
#include <umios/backend/cm/stm32f4/usb_midi.hh>
#include <umios/backend/cm/common/systick.hh>
#include <umios/backend/cm/common/nvic.hh>
#include <umios/backend/cm/common/scb.hh>
#include <umios/backend/cm/cortex_m4.hh>

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
USBDevice usb_dev;
USB_MIDI usb_midi(usb_dev);

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
    RCC::enable_spi3();
    RCC::enable_dma1();
    RCC::enable_usb_otg_fs();

    // LEDs: PD12 (Green), PD13 (Orange), PD14 (Red), PD15 (Blue)
    gpio_d.config_output(12);
    gpio_d.config_output(13);
    gpio_d.config_output(14);
    gpio_d.config_output(15);

    // CS43L22 Reset: PD4
    gpio_d.config_output(4);
    gpio_d.reset(4);  // Hold in reset initially

    // I2C1: PB6 (SCL), PB9 (SDA) - open-drain with pull-up
    gpio_b.config_af(6, GPIO::AF4, GPIO::SPEED_FAST, GPIO::PUPD_UP, true);
    gpio_b.config_af(9, GPIO::AF4, GPIO::SPEED_FAST, GPIO::PUPD_UP, true);

    // I2S3: PC7 (MCK), PC10 (SCK), PC12 (SD), PA4 (WS)
    gpio_c.config_af(7, GPIO::AF6, GPIO::SPEED_HIGH);   // MCK
    gpio_c.config_af(10, GPIO::AF6, GPIO::SPEED_HIGH);  // SCK
    gpio_c.config_af(12, GPIO::AF6, GPIO::SPEED_HIGH);  // SD
    gpio_a.config_af(4, GPIO::AF6, GPIO::SPEED_HIGH);   // WS

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

void init_usb() {
    // USB clock already enabled in init_gpio()

    // Small delay for USB PHY
    for (int i = 0; i < 10000; ++i) { asm volatile(""); }

    // Initialize USB MIDI
    usb_midi.init();

    // Set MIDI receive callback
    usb_midi.on_midi = [](uint8_t, const uint8_t* data, uint8_t len) {
        if (len >= 2) {
            synth.handle_midi(data, len);
        }
    };

    // Set SysEx callback (for standard IO)
    usb_midi.on_sysex = [](const uint8_t* data, uint16_t len) {
        // Handle SysEx-based standard IO
        // Format: F0 7D <command> <data...> F7
        // 7D = non-commercial/educational use
        if (len >= 3 && data[0] == 0xF0 && data[1] == 0x7D) {
            uint8_t cmd = data[2];
            switch (cmd) {
                case 0x01:  // Get version
                    {
                        uint8_t resp[] = {0xF0, 0x7D, 0x01, 0x02, 0x00, 0xF7};
                        usb_midi.send_sysex(resp, sizeof(resp));
                    }
                    break;
                case 0x10:  // Set parameter
                    if (len >= 6) {
                        uint8_t param = data[3];
                        // 14-bit value (2 x 7-bit)
                        float value = static_cast<float>((data[4] | (data[5] << 7))) / 16383.0f;
                        synth.set_param(static_cast<umi::synth::ParamId>(param), value * 10000.0f);
                    }
                    break;
                default:
                    break;
            }
        }
    };

    // Enable USB interrupt (priority 3, higher than audio)
    NVIC::set_prio(67, 3);  // OTG_FS = IRQ 67
    NVIC::enable(67);
}

/// Fill audio buffer with synth output
void fill_audio_buffer(int16_t* buf, uint32_t samples) {
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

}  // namespace

// ============================================================================
// Interrupt Handlers
// ============================================================================

extern "C" void DMA1_Stream5_IRQHandler() {
    if (dma_i2s.transfer_complete()) {
        dma_i2s.clear_tc();

        // Fill the buffer that just finished
        // current_buffer() returns which buffer DMA is currently using
        // We fill the OTHER one
        int16_t* buf = (dma_i2s.current_buffer() == 0) ? audio_buf1 : audio_buf0;
        fill_audio_buffer(buf, BUFFER_SIZE);

        // Activity LED
        led_counter = led_counter + 1;
        if (led_counter >= 100) {
            led_counter = 0;
            gpio_d.set(12);  // Green LED
        } else if (led_counter == 50) {
            gpio_d.reset(12);
        }
    }
}

extern "C" void OTG_FS_IRQHandler() {
    usb_midi.poll();
}

extern "C" void SysTick_Handler() {
    // 1ms tick (not used for audio, just for timeouts if needed)
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

    // Orange LED on during init
    gpio_d.set(13);

    // Initialize synth
    synth.init(static_cast<float>(SAMPLE_RATE));

    // Initialize audio subsystem
    init_audio();

    // Initialize USB
    init_usb();

    // Initialize SysTick (1ms)
    SysTick::init(168000 - 1);  // 168MHz / 1000

    // Orange LED off, blue LED on (ready)
    gpio_d.reset(13);
    gpio_d.set(15);

    // Main loop
    while (true) {
        // USB is handled in interrupt
        // Audio is handled in DMA interrupt
        // Just wait for interrupts
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
// DMA1_Stream5 = IRQ 16 (position 32 in table = 16 system + 16)
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
    // IRQ 0-15: (table index 16-31)
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
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

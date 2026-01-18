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
// UAC1, AudioEnabled, 48kHz, stereo, 16-bit, EP1=Audio, EP2=Feedback, default Async mode, no MIDI
umiusb::AudioInterface<umiusb::UacVersion::Uac1, true, 48000, 2, 16, 1, 2, umiusb::AudioSyncMode::Async, false> usb_audio;
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

// Sync mode state (for button switching)
// LED display: Green=Async, Orange=Adaptive, Blue=Sync
volatile bool user_button_prev = false;

// Synth engine
umi::synth::PolySynth synth;

// LED state (for activity indication)
volatile uint32_t led_counter = 0;

// ============================================================================
// Benchmark Statistics (DWT cycle counter)
// ============================================================================
struct BenchmarkStats {
    uint32_t min_cycles = UINT32_MAX;
    uint32_t max_cycles = 0;
    uint64_t total_cycles = 0;
    uint32_t count = 0;

    void record(uint32_t cycles) {
        if (cycles < min_cycles) min_cycles = cycles;
        if (cycles > max_cycles) max_cycles = cycles;
        total_cycles += cycles;
        count++;
    }

    uint32_t avg() const { return count > 0 ? static_cast<uint32_t>(total_cycles / count) : 0; }
    void reset() { min_cycles = UINT32_MAX; max_cycles = 0; total_cycles = 0; count = 0; }
};

volatile BenchmarkStats bench_read;    // ring_buffer_.read()
volatile BenchmarkStats bench_sof;     // on_sof()
// volatile BenchmarkStats bench_write;   // on_rx() -> ring_buffer_.write() (TODO: add later)
volatile bool bench_enabled = false;

// Sync mode evaluation statistics
struct SyncModeStats {
    uint32_t streaming_time_ms = 0;    // Total streaming time
    uint32_t underrun_count = 0;       // Buffer underruns
    uint32_t overrun_count = 0;        // Buffer overruns
    int32_t min_buffer_level = 256;    // Minimum buffer level seen
    int32_t max_buffer_level = 0;      // Maximum buffer level seen
    uint32_t feedback_updates = 0;     // Feedback EP transmissions (Async only)

    void reset() {
        streaming_time_ms = 0;
        underrun_count = 0;
        overrun_count = 0;
        min_buffer_level = 256;
        max_buffer_level = 0;
        feedback_updates = 0;
    }

    void update_buffer_level(int32_t level) {
        if (level < min_buffer_level) min_buffer_level = level;
        if (level > max_buffer_level) max_buffer_level = level;
    }
};

volatile SyncModeStats mode_stats;

// Benchmark results can be read via debugger:
// - bench_read.min_cycles, bench_read.max_cycles, bench_read.avg()
// - bench_sof.min_cycles, bench_sof.max_cycles, bench_sof.avg()
// At 168MHz: cycles / 168 = microseconds

/// Update LED to show current sync mode
/// Green=Async(0x05), Orange=Adaptive(0x09), Blue=Sync(0x0D)
void update_sync_mode_led() {
    auto mode = usb_audio.current_sync_mode();

    // Turn off all mode LEDs first
    gpio_d.reset(12);  // Green
    gpio_d.reset(13);  // Orange
    gpio_d.reset(15);  // Blue

    switch (mode) {
        case umiusb::AudioSyncMode::Async:
            gpio_d.set(12);  // Green
            break;
        case umiusb::AudioSyncMode::Adaptive:
            gpio_d.set(13);  // Orange
            break;
        case umiusb::AudioSyncMode::Sync:
            gpio_d.set(15);  // Blue
            break;
    }
}

/// Cycle to next sync mode
umiusb::AudioSyncMode next_sync_mode(umiusb::AudioSyncMode current) {
    switch (current) {
        case umiusb::AudioSyncMode::Async:
            return umiusb::AudioSyncMode::Adaptive;
        case umiusb::AudioSyncMode::Adaptive:
            return umiusb::AudioSyncMode::Sync;
        case umiusb::AudioSyncMode::Sync:
        default:
            return umiusb::AudioSyncMode::Async;
    }
}

/// Reinitialize USB with new sync mode
void reinit_usb_with_mode(umiusb::AudioSyncMode new_mode) {
    // Disable USB interrupt during reconfiguration
    NVIC::disable(67);

    // Disconnect USB
    usb_hal.disconnect();

    // Wait for host to notice disconnection
    for (int i = 0; i < 500000; ++i) { asm volatile("" ::: "memory"); }

    // Reset mode statistics for new mode test
    const_cast<SyncModeStats&>(mode_stats).reset();
    const_cast<BenchmarkStats&>(bench_read).reset();
    const_cast<BenchmarkStats&>(bench_sof).reset();

    // Set new sync mode and reset AudioInterface state
    usb_audio.set_sync_mode(new_mode);
    usb_audio.reset();

    // Reinitialize USB
    usb_device.init();
    usb_hal.connect();

    // Re-enable USB interrupt
    NVIC::enable(67);

    // Update LED
    update_sync_mode_led();
}

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

    // USER button: PA0 (directly connected, active high)
    gpio_a.set_mode(0, GPIO::MODE_INPUT);
    gpio_a.set_pupd(0, GPIO::PUPD_DOWN);

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

void init_usb() {
    // USB clock already enabled in init_gpio()

    // Small delay for USB PHY
    for (int i = 0; i < 10000; ++i) { asm volatile(""); }

    // Set streaming status callback
    usb_audio.on_streaming_change = [](bool streaming) {
        if (streaming) {
            // Clear red LED when streaming starts
            gpio_d.reset(14);
        }
    };

    // Set PLL adjustment callback for Adaptive mode
    // This would be called to adjust I2S PLL for clock synchronization
    usb_audio.on_pll_adjust = [](int32_t ppm) {
        // TODO: Implement actual PLL adjustment via PLLI2S tuning
        // For now, just log the adjustment request
        (void)ppm;
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
void fill_audio_buffer(int16_t* buf, uint32_t frame_count) {
    uint32_t start = 0;
    if (bench_enabled) {
        start = DWT::cycles();
    }

    // Read audio from AudioInterface's internal ring buffer
    uint32_t frames_read = usb_audio.read_audio(buf, frame_count);

    if (bench_enabled) {
        uint32_t elapsed = DWT::cycles() - start;
        const_cast<BenchmarkStats&>(bench_read).record(elapsed);
    }

    // Notify feedback calculator about consumed samples (for Async mode)
    if (frames_read > 0) {
        usb_audio.on_samples_consumed(frames_read);
    }

    // Check for underrun
    if (frames_read < frame_count) {
        // Underrun occurred - show red LED
        gpio_d.set(14);
    } else {
        // Normal operation - clear red LED
        gpio_d.reset(14);
    }
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

extern "C" void DMA1_Stream5_IRQHandler() {
    if (dma_i2s.transfer_complete()) {
        dma_i2s.clear_tc();

        // Fill the buffer that just finished
        // current_buffer() returns which buffer DMA is currently using
        // We fill the OTHER one
        int16_t* buf = (dma_i2s.current_buffer() == 0) ? audio_buf1 : audio_buf0;
        fill_audio_buffer(buf, BUFFER_SIZE);

        // Activity indication: blink the mode LED briefly off
        // (Mode LED is already set by update_sync_mode_led)
        led_counter = led_counter + 1;
        if (led_counter >= 100) {
            led_counter = 0;
            // Brief blink off to show activity
        }
    }
}

extern "C" void OTG_FS_IRQHandler() {
    usb_device.poll();
}

extern "C" void SysTick_Handler() {
    // 1ms tick - simulate SOF for feedback calculation in Async mode
    // Real SOF comes from USB but timing is similar
    if (usb_audio.is_streaming()) {
        uint32_t start = 0;
        if (bench_enabled) {
            start = DWT::cycles();
        }

        usb_audio.on_sof(usb_hal);

        if (bench_enabled) {
            uint32_t elapsed = DWT::cycles() - start;
            const_cast<BenchmarkStats&>(bench_sof).record(elapsed);
        }

        // Update mode statistics
        const_cast<SyncModeStats&>(mode_stats).streaming_time_ms++;
        const_cast<SyncModeStats&>(mode_stats).underrun_count = usb_audio.underrun_count();
        const_cast<SyncModeStats&>(mode_stats).overrun_count = usb_audio.overrun_count();
        const_cast<SyncModeStats&>(mode_stats).update_buffer_level(
            static_cast<int32_t>(usb_audio.buffered_frames()));
    }
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

    // Enable DWT cycle counter for benchmarking
    DWT::enable();
    bench_enabled = true;

    // Show initial sync mode on LED (default: Async = Green)
    update_sync_mode_led();

    // Main loop
    while (true) {
        // Poll USER button (PA0, active high)
        bool button_now = gpio_a.read(0);

        // Detect rising edge (button press)
        if (button_now && !user_button_prev) {
            // Debounce delay
            for (int i = 0; i < 50000; ++i) { asm volatile("" ::: "memory"); }

            // Check button still pressed
            if (gpio_a.read(0)) {
                // Cycle to next sync mode
                auto current = usb_audio.current_sync_mode();
                auto next = next_sync_mode(current);

                // Red LED on during transition
                gpio_d.set(14);

                // Reinitialize USB with new mode
                reinit_usb_with_mode(next);

                // Red LED off
                gpio_d.reset(14);
            }
        }
        user_button_prev = button_now;

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

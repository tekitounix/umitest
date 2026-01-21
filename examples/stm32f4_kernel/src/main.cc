// SPDX-License-Identifier: MIT
// STM32F4-Discovery Kernel with Audio/USB Support
// Loads and runs .umiapp applications, handles audio I/O and USB MIDI
// Full feature parity with original stm32f4_synth:
// - PDM Microphone input (CIC decimation @ 47,991Hz - same as I2S)
// - USB Audio IN: L=mic, R=synth
// - USB Audio OUT -> I2S output
// - USB MIDI -> App via syscall
// PDM and I2S are now perfectly synchronized - no resampling needed!

#include <cstdint>
#include <cstring>
#include <span>

// Kernel components
#include <app_header.hh>
#include <loader.hh>
#include <mpu_config.hh>

// Platform drivers
#include <umios/backend/cm/stm32f4/rcc.hh>
#include <umios/backend/cm/stm32f4/gpio.hh>
#include <umios/backend/cm/stm32f4/i2c.hh>
#include <umios/backend/cm/stm32f4/i2s.hh>
#include <umios/backend/cm/stm32f4/cs43l22.hh>
#include <umios/backend/cm/stm32f4/pdm_mic.hh>  // PDM microphone
#include <umios/backend/cm/common/nvic.hh>
#include <umios/backend/cm/common/scb.hh>

// USB stack (umiusb)
#include <umiusb.hh>
#include <audio_interface.hh>
#include <hal/stm32_otg.hh>

using namespace umi::stm32;
using namespace umi::port::arm;

// ============================================================================
// Linker-Provided Symbols
// ============================================================================

extern "C" {
    extern const uint8_t _app_image_start[];
    extern const uint8_t _app_image_end[];
    extern uint8_t _app_ram_start[];
    extern const uint8_t _app_ram_end[];
    extern const uint32_t _app_ram_size;
    extern uint8_t _shared_start[];
    extern const uint32_t _shared_size;
    extern uint32_t _estack;
    extern uint32_t _sidata;
    extern uint32_t _sdata;
    extern uint32_t _edata;
    extern uint32_t _sbss;
    extern uint32_t _ebss;
}

// ============================================================================
// Syscall Definitions
// ============================================================================

namespace umi::kernel::app_syscall {
    inline constexpr uint32_t Exit          = 0;
    inline constexpr uint32_t RegisterProc  = 1;
    inline constexpr uint32_t WaitEvent     = 2;
    inline constexpr uint32_t SendEvent     = 3;
    inline constexpr uint32_t PeekEvent     = 4;
    inline constexpr uint32_t GetTime       = 10;
    inline constexpr uint32_t Sleep         = 11;
    inline constexpr uint32_t Log           = 20;
    inline constexpr uint32_t Panic         = 21;
    inline constexpr uint32_t GetParam      = 30;
    inline constexpr uint32_t SetParam      = 31;
    inline constexpr uint32_t GetShared     = 40;
    inline constexpr uint32_t MidiSend      = 50;
    inline constexpr uint32_t MidiRecv      = 51;
}

// ============================================================================
// Configuration
// ============================================================================

constexpr uint32_t SAMPLE_RATE = 48000;
constexpr uint32_t BUFFER_SIZE = 64;  // Samples per channel per buffer

// ============================================================================
// Hardware Instances
// ============================================================================

namespace {

GPIO gpio_a('A');
GPIO gpio_b('B');
GPIO gpio_c('C');
GPIO gpio_d('D');
I2C i2c1;
I2S i2s3;
DMA_I2S dma_i2s;
CS43L22 codec(i2c1);

// PDM microphone (now running at 47,991Hz - same as I2S DAC)
PdmMic pdm_mic;
DmaPdm dma_pdm;
CicDecimator cic_decimator;
// No resampler needed - PDM and I2S are synchronized at same sample rate!

// USB stack instances (umiusb)
umiusb::Stm32FsHal usb_hal;
umiusb::AudioFullDuplexMidi48k usb_audio;
umiusb::Device<umiusb::Stm32FsHal, decltype(usb_audio)> usb_device(
    usb_hal, usb_audio,
    {
        .vendor_id = 0x1209,
        .product_id = 0x0006,
        .device_version = 0x0300,  // Version 3.0 for kernel/app separation
        .manufacturer_idx = 1,
        .product_idx = 2,
        .serial_idx = 0,
    }
);

}  // namespace

// ============================================================================
// USB Descriptors
// ============================================================================

namespace usb_config {
using namespace umiusb::desc;

constexpr auto str_manufacturer = String("UMI-OS");
constexpr auto str_product = String("UMI Kernel Synth");

constexpr std::array<std::span<const uint8_t>, 2> string_table = {{
    {str_manufacturer.data.data(), str_manufacturer.size},
    {str_product.data.data(), str_product.size},
}};
}  // namespace usb_config

// ============================================================================
// Audio Buffers (DMA double-buffering)
// ============================================================================

// DMA buffers must be in SRAM, not CCM
__attribute__((section(".dma_buffer")))
int16_t audio_buf0[BUFFER_SIZE * 2];  // Stereo interleaved

__attribute__((section(".dma_buffer")))
int16_t audio_buf1[BUFFER_SIZE * 2];

// PDM microphone DMA buffers
// PDM @ 3.0714MHz, DMA 256 words = 256*16 bits = 4096 PDM samples
// CIC 64x decimation = 4096/64 = 64 PCM samples @ 47,991Hz per DMA interrupt
// This matches I2S DMA (64 samples), so both ISRs fire at same rate (~1.33ms)
constexpr uint32_t PDM_BUF_SIZE = 256;

__attribute__((section(".dma_buffer")))
uint16_t pdm_buf0[PDM_BUF_SIZE];

__attribute__((section(".dma_buffer")))
uint16_t pdm_buf1[PDM_BUF_SIZE];

// PCM buffer for decimated audio (from PDM @ 47,991Hz)
constexpr uint32_t PCM_BUF_SIZE = 64;  // 4096 bits / 64 = 64 samples per DMA
int16_t pcm_buf[PCM_BUF_SIZE];

// Stereo buffer for USB Audio IN (mic L + synth R) @ 47,991Hz
constexpr uint32_t STEREO_BUF_SIZE = 128;  // 64 samples * 2 channels
int16_t stereo_buf[STEREO_BUF_SIZE];

// ============================================================================
// Global State
// ============================================================================

umi::kernel::AppLoader g_loader;

__attribute__((section(".shared")))
umi::kernel::SharedMemory g_shared;

volatile uint32_t g_current_buffer = 0;
volatile bool g_audio_ready = false;
volatile int16_t* g_active_buf = nullptr;
volatile bool g_pdm_ready = false;
volatile uint16_t* g_active_pdm_buf = nullptr;

// Debug counters (not volatile for GDB inspection only)
uint32_t dbg_i2s_isr_count = 0;
uint32_t dbg_fill_audio_count = 0;
uint32_t dbg_out_buffered = 0;
uint32_t dbg_in_buffered = 0;
uint32_t dbg_read_frames = 0;
uint32_t dbg_underrun = 0;
uint32_t dbg_streaming = 0;  // USB Audio OUT streaming flag
int32_t dbg_pll_ppm = 0;     // PLL adjustment in ppm
uint32_t dbg_overrun = 0;    // USB Audio OUT overrun count
uint32_t dbg_missed = 0;     // Missed audio frames (processing too slow)
uint32_t dbg_process_time = 0;  // fill_audio_buffer processing time (cycles)
uint32_t dbg_usb_rx_count = 0;  // USB Audio OUT packets received
int16_t dbg_sample_l = 0;       // Debug: last sample L
int16_t dbg_sample_r = 0;       // Debug: last sample R
uint32_t dbg_set_iface_count = 0;  // USB set_interface calls
uint32_t dbg_sof_count = 0;        // USB SOF count
uint32_t dbg_feedback = 0;         // Feedback value being sent to host
uint32_t dbg_fb_count = 0;         // Feedback EP write count
uint32_t dbg_fb_actual = 0;        // Actual feedback value sent (from HAL)
uint32_t dbg_fb_sent = 0;          // Feedback sent from AudioInterface
uint32_t dbg_set_iface_val = 0;    // (iface << 8) | alt
uint32_t dbg_out_iface_num = 0;    // Audio OUT interface number
uint32_t dbg_playback_started = 0; // playback_started_ flag
uint32_t dbg_muted = 0;            // fu_out_mute_ flag
int16_t dbg_volume = 0;            // fu_out_volume_ value
int16_t dbg_ring_sample0 = 0;      // Raw ring buffer sample[0]
int16_t dbg_ring_sample1 = 0;      // Raw ring buffer sample[1]
int16_t dbg_hal_rx_sample0 = 0;    // HAL received sample[0]
int16_t dbg_hal_rx_sample1 = 0;    // HAL received sample[1]
uint32_t dbg_fifo_word = 0;        // First word read from RX FIFO
uint32_t dbg_ep1_fifo_word = 0;   // First word from EP1 specifically

// MIDI queue (ISR -> App)
struct MidiMsg {
    uint8_t data[4];
    uint8_t len;
};
constexpr uint32_t MIDI_QUEUE_SIZE = 64;
MidiMsg g_midi_queue[MIDI_QUEUE_SIZE];
volatile uint32_t g_midi_write = 0;
volatile uint32_t g_midi_read = 0;

// Tick counter for GetTime syscall
volatile uint32_t g_tick_us = 0;

// ============================================================================
// PLLI2S Initialization (for 48kHz audio)
// ============================================================================

static void init_plli2s() {
    constexpr uint32_t RCC_PLLI2SCFGR = 0x40023884;
    constexpr uint32_t RCC_CR = 0x40023800;

    // Disable PLLI2S
    *reinterpret_cast<volatile uint32_t*>(RCC_CR) &= ~(1U << 26);

    // Configure: PLLI2SN=258, PLLI2SR=3
    // PLLI2SCLK = 1MHz * 258 / 3 = 86 MHz
    // Fs = 86MHz / [256 * 7] = 47,991 Hz (~48kHz)
    *reinterpret_cast<volatile uint32_t*>(RCC_PLLI2SCFGR) =
        (3U << 28) |   // PLLI2SR = 3
        (258U << 6);   // PLLI2SN = 258

    // Enable PLLI2S
    *reinterpret_cast<volatile uint32_t*>(RCC_CR) |= (1U << 26);

    // Wait for lock
    while (!(*reinterpret_cast<volatile uint32_t*>(RCC_CR) & (1U << 27))) {}
}

// ============================================================================
// GPIO Initialization
// ============================================================================

static void init_gpio() {
    RCC::enable_gpio('A');
    RCC::enable_gpio('B');
    RCC::enable_gpio('C');
    RCC::enable_gpio('D');
    RCC::enable_i2c1();
    RCC::enable_spi3();
    RCC::enable_spi2();  // For PDM mic
    RCC::enable_dma1();
    RCC::enable_usb_otg_fs();

    // LEDs: PD12 (Green), PD13 (Orange), PD14 (Red), PD15 (Blue)
    gpio_d.config_output(12);
    gpio_d.config_output(13);
    gpio_d.config_output(14);
    gpio_d.config_output(15);

    // USER button: PA0
    gpio_a.set_mode(0, GPIO::MODE_INPUT);
    gpio_a.set_pupd(0, GPIO::PUPD_DOWN);

    // CS43L22 Reset: PD4
    gpio_d.config_output(4);
    gpio_d.reset(4);

    // I2C1: PB6 (SCL), PB9 (SDA)
    gpio_b.config_af(6, GPIO::AF4, GPIO::SPEED_FAST, GPIO::PUPD_UP, true);
    gpio_b.config_af(9, GPIO::AF4, GPIO::SPEED_FAST, GPIO::PUPD_UP, true);

    // I2S3 (Audio OUT): PC7 (MCK), PC10 (SCK), PC12 (SD), PA4 (WS)
    gpio_c.config_af(7, GPIO::AF6, GPIO::SPEED_HIGH);
    gpio_c.config_af(10, GPIO::AF6, GPIO::SPEED_HIGH);
    gpio_c.config_af(12, GPIO::AF6, GPIO::SPEED_HIGH);
    gpio_a.config_af(4, GPIO::AF6, GPIO::SPEED_HIGH);

    // I2S2 (PDM Microphone): PB10 (CLK), PC3 (SD)
    gpio_b.config_af(10, GPIO::AF5, GPIO::SPEED_HIGH);
    gpio_c.config_af(3, GPIO::AF5, GPIO::SPEED_HIGH);

    // USB OTG FS: PA11 (DM), PA12 (DP)
    gpio_a.config_af(11, GPIO::AF10, GPIO::SPEED_HIGH);
    gpio_a.config_af(12, GPIO::AF10, GPIO::SPEED_HIGH);
}

// ============================================================================
// Audio Initialization
// ============================================================================

static void init_audio() {
    i2c1.init();

    // Release CS43L22 from reset
    gpio_d.set(4);
    for (int i = 0; i < 100000; ++i) { asm volatile(""); }

    if (!codec.init()) {
        gpio_d.set(14);  // Red LED = error
        while (1) {}
    }

    init_plli2s();
    i2s3.init_48khz();

    __builtin_memset(audio_buf0, 0, sizeof(audio_buf0));
    __builtin_memset(audio_buf1, 0, sizeof(audio_buf1));

    dma_i2s.init(audio_buf0, audio_buf1, BUFFER_SIZE * 2, i2s3.dr_addr());

    // DMA1_Stream5 = IRQ 16, priority 5 (audio - high priority)
    NVIC::set_prio(16, 5);
    NVIC::enable(16);

    i2s3.enable_dma();
    i2s3.enable();
    dma_i2s.enable();
    codec.power_on();
    codec.set_volume(0);
}

// ============================================================================
// PDM Microphone Initialization
// ============================================================================

static void init_pdm_mic() {
    pdm_mic.init();
    cic_decimator.reset();
    // No resampler needed - PDM is now at 47,991Hz, same as I2S DAC

    dma_pdm.init(pdm_buf0, pdm_buf1, PDM_BUF_SIZE, pdm_mic.dr_addr());

    // DMA1_Stream3 = IRQ 14, priority 5
    NVIC::set_prio(14, 5);
    NVIC::enable(14);

    pdm_mic.enable_dma();
    pdm_mic.enable();
    dma_pdm.enable();
}

// ============================================================================
// USB Initialization
// ============================================================================

static void init_usb() {
    for (int i = 0; i < 10000; ++i) { asm volatile(""); }

    usb_audio.on_streaming_change = [](bool streaming) {
        if (streaming) {
            gpio_d.set(15);   // Blue LED ON
        } else {
            gpio_d.reset(15);
        }
    };

    usb_audio.on_audio_in_change = [](bool streaming) {
        if (streaming) {
            gpio_d.set(13);   // Orange LED ON
        } else {
            gpio_d.reset(13);
        }
    };

    usb_audio.on_audio_rx = []() {
        // Audio OUT received - count packets and toggle green LED
        ++dbg_usb_rx_count;
        static uint8_t cnt = 0;
        if (++cnt >= 48) {
            cnt = 0;
            gpio_d.toggle(12);  // Green LED toggle at ~1Hz when receiving
        }
    };
    
    // Note: USB Audio IN is written directly from I2S DMA ISR (process_audio_frame)
    // No need for on_sof_app callback
    
    // USB MIDI -> MIDI queue (ISR context)
    usb_audio.set_midi_callback([](uint8_t /*cable*/, const uint8_t* data, uint8_t len) {
        uint32_t next = (g_midi_write + 1) % MIDI_QUEUE_SIZE;
        if (next != g_midi_read) {  // Not full
            g_midi_queue[g_midi_write].len = (len > 4) ? 4 : len;
            for (uint8_t i = 0; i < g_midi_queue[g_midi_write].len; ++i) {
                g_midi_queue[g_midi_write].data[i] = data[i];
            }
            g_midi_write = next;
        }
    });

    usb_device.set_strings(usb_config::string_table);
    usb_device.init();
    usb_hal.connect();

    // OTG_FS = IRQ 67, priority 6 (lower than audio DMA)
    NVIC::set_prio(67, 6);
    NVIC::enable(67);
}

// ============================================================================
// SysTick Initialization (1ms tick)
// ============================================================================

static void init_systick() {
    constexpr uint32_t SYST_CSR = 0xE000E010;
    constexpr uint32_t SYST_RVR = 0xE000E014;
    constexpr uint32_t SYST_CVR = 0xE000E018;
    
    // 168MHz / 168000 = 1kHz (1ms tick)
    *reinterpret_cast<volatile uint32_t*>(SYST_RVR) = 168000 - 1;
    *reinterpret_cast<volatile uint32_t*>(SYST_CVR) = 0;
    *reinterpret_cast<volatile uint32_t*>(SYST_CSR) = 0x07;  // Enable, IRQ, use core clock
}

// ============================================================================
// Interrupt Handlers
// ============================================================================

// PDM DMA: Signal buffer ready (processing in main loop)
extern "C" void DMA1_Stream3_IRQHandler() {
    if (dma_pdm.transfer_complete()) {
        dma_pdm.clear_tc();
        
        // Get the buffer that just completed - notify only, no processing
        g_active_pdm_buf = (dma_pdm.current_buffer() == 0) ? pdm_buf1 : pdm_buf0;
        g_pdm_ready = true;
    }
}

// I2S DMA: Signal buffer ready (processing in main loop)
extern "C" void DMA1_Stream5_IRQHandler() {
    if (dma_i2s.transfer_complete()) {
        dma_i2s.clear_tc();
        
        // Get the buffer that just completed - notify only, no processing
        g_active_buf = (dma_i2s.current_buffer() == 0) ? audio_buf1 : audio_buf0;
        g_audio_ready = true;
        
        ++dbg_i2s_isr_count;
    }
}

extern "C" void OTG_FS_IRQHandler() {
    usb_device.poll();
}

extern "C" void SysTick_Handler() {
    g_tick_us += 1000;  // 1ms = 1000us
}

// Actual syscall handler implementation
void svc_handler_impl(uint32_t* sp) {
    using namespace umi::kernel::app_syscall;
    
    // Syscall number is in r0 (sp[0]), arguments in r1-r4 (sp[1-4])
    uint32_t syscall_nr = sp[0];
    uint32_t arg0 = sp[1];
    uint32_t arg1 = sp[2];
    int32_t result = 0;
    
    switch (syscall_nr) {
        case Exit:
            // App requested exit - mark as terminated and return
            // The app's _start will loop, but kernel continues
            g_loader.terminate(static_cast<int>(arg0));
            result = 0;
            break;
            
        case RegisterProc:
            g_loader.register_processor(reinterpret_cast<void*>(arg0));
            result = 0;
            break;
            
        case WaitEvent:
            // For now, just return immediately (non-blocking)
            result = 0;
            break;
            
        case GetTime:
            sp[0] = g_tick_us;
            return;
            
        case GetShared:
            sp[0] = reinterpret_cast<uint32_t>(&g_shared);
            return;
            
        case MidiRecv:
            // Return MIDI message if available
            if (g_midi_read != g_midi_write) {
                MidiMsg* out = reinterpret_cast<MidiMsg*>(arg0);
                *out = g_midi_queue[g_midi_read];
                g_midi_read = (g_midi_read + 1) % MIDI_QUEUE_SIZE;
                result = out->len;
            } else {
                result = 0;  // No data
            }
            break;
            
        case MidiSend:
            // Send MIDI via USB
            // arg0 = pointer to data, arg1 = length (1-3 bytes)
            if (arg1 >= 1 && arg1 <= 3) {
                const uint8_t* data = reinterpret_cast<const uint8_t*>(arg0);
                uint8_t status = data[0];
                uint8_t d1 = (arg1 >= 2) ? data[1] : 0;
                uint8_t d2 = (arg1 >= 3) ? data[2] : 0;
                usb_audio.send_midi(usb_hal, 0, status, d1, d2);
                result = 0;
            } else {
                result = -1;
            }
            break;
            
        case Log:
        case Sleep:
        case SendEvent:
        case PeekEvent:
        case GetParam:
        case SetParam:
        case Panic:
            result = 0;  // Not implemented yet
            break;
            
        default:
            result = -1;  // Unknown syscall
            break;
    }
    
    sp[0] = static_cast<uint32_t>(result);
}

// Naked SVC handler - must get SP before any stack operations
extern "C" [[gnu::naked]] void SVC_Handler() {
    __asm__ volatile(
        "tst lr, #4\n"           // Check bit 4 of LR (EXC_RETURN)
        "ite eq\n"
        "mrseq r0, msp\n"        // If 0: use MSP
        "mrsne r0, psp\n"        // If 1: use PSP
        "b %0\n"                 // Jump to C handler with sp in r0
        :
        : "i"(svc_handler_impl)
        :
    );
}

// ============================================================================
// Audio Processing
// ============================================================================

// USB Audio IN buffer (separate from I2S DMA buffer)
int16_t usb_in_buf[BUFFER_SIZE * 2];

// Float buffer for synth processing
float synth_out_buf[BUFFER_SIZE * 2];
float synth_in_buf[BUFFER_SIZE * 2];

// Last synth output (for USB Audio IN)
int16_t last_synth_out[BUFFER_SIZE * 2];

// Debug: track what fill_audio_buffer returns
uint32_t dbg_fill_ret = 0;        // Return value from read_audio_asrc
uint32_t dbg_fill_buf_addr = 0;   // Buffer address passed to fill
int16_t dbg_buf_sample0 = 0;      // First sample in buffer after fill
int16_t dbg_buf_sample1 = 0;      // Second sample
uint32_t dbg_synth_called = 0;    // Synth process called count

// Process audio (called from main loop)
static void process_audio_frame(int16_t* buf) {
    constexpr float dt = 1.0f / 48000.0f;
    
    // 1. Read USB Audio OUT into buffer
    usb_audio.read_audio_asrc(buf, BUFFER_SIZE);
    
    // 2. Call app synth if registered
    if (g_loader.state() == umi::kernel::AppState::Running) {
        // Convert USB audio to float for synth input
        for (uint32_t i = 0; i < BUFFER_SIZE * 2; ++i) {
            synth_in_buf[i] = buf[i] / 32768.0f;
        }
        
        // Clear output buffer
        for (uint32_t i = 0; i < BUFFER_SIZE * 2; ++i) {
            synth_out_buf[i] = 0.0f;
        }
        
        // Call synth process
        g_loader.call_process(
            std::span<float>(synth_out_buf, BUFFER_SIZE * 2),
            std::span<const float>(synth_in_buf, BUFFER_SIZE * 2),
            g_shared.sample_position,
            BUFFER_SIZE,
            dt
        );
        g_shared.sample_position += BUFFER_SIZE;
        ++dbg_synth_called;
        
        // Mix synth output with USB audio for I2S output
        // Also save synth output for USB Audio IN
        for (uint32_t i = 0; i < BUFFER_SIZE * 2; ++i) {
            float mixed = synth_in_buf[i] + synth_out_buf[i];
            if (mixed > 1.0f) mixed = 1.0f;
            if (mixed < -1.0f) mixed = -1.0f;
            buf[i] = static_cast<int16_t>(mixed * 32767.0f);
            
            // Save synth output (convert back to int16)
            float synth_val = synth_out_buf[i];
            if (synth_val > 1.0f) synth_val = 1.0f;
            if (synth_val < -1.0f) synth_val = -1.0f;
            last_synth_out[i] = static_cast<int16_t>(synth_val * 32767.0f);
        }
    }
    
    // 3. Write to USB Audio IN (L=mic, R=synth)
    if (usb_audio.is_audio_in_streaming()) {
        for (uint32_t i = 0; i < BUFFER_SIZE; ++i) {
            stereo_buf[i * 2] = pcm_buf[i];           // L = mic
            stereo_buf[i * 2 + 1] = last_synth_out[i * 2];  // R = synth (mono from L)
        }
        usb_audio.write_audio_in(stereo_buf, BUFFER_SIZE);
    }
}

static void audio_loop() {
    // Main loop - process audio when buffer ready
    while (true) {
        // Process PDM decimation when ready (moved from ISR)
        if (g_pdm_ready) {
            g_pdm_ready = false;
            uint16_t* pdm_data = const_cast<uint16_t*>(g_active_pdm_buf);
            cic_decimator.process_buffer(pdm_data, PDM_BUF_SIZE, pcm_buf, PCM_BUF_SIZE);
        }
        
        // Process audio when I2S buffer ready
        if (g_audio_ready) {
            g_audio_ready = false;
            process_audio_frame(const_cast<int16_t*>(g_active_buf));
        }
        
        // Update debug counters
        dbg_underrun = usb_audio.underrun_count();
        dbg_overrun = usb_audio.overrun_count();
        dbg_streaming = usb_audio.is_streaming() ? 1 : 0;
    }
}

// ============================================================================
// Main
// ============================================================================

int main() {
    RCC::init_168mhz();
    init_gpio();
    
    gpio_d.set(15);  // Blue LED - startup
    
    // Initialize audio hardware
    init_audio();
    init_pdm_mic();
    
    // Initialize shared memory
    g_shared.sample_rate = SAMPLE_RATE;
    g_shared.buffer_size = BUFFER_SIZE;
    g_shared.sample_position = 0;
    
    // Configure app loader
    g_loader.set_app_memory(_app_ram_start, reinterpret_cast<uintptr_t>(&_app_ram_size));
    g_loader.set_shared_memory(&g_shared);
    
    // Direct XIP execution: skip header, treat flash as raw binary
    // The app is linked to run directly from 0x08060000
    // Entry point is at the start of the binary with Thumb bit set
    using EntryFn = void (*)();
    auto app_entry = reinterpret_cast<EntryFn>(
        reinterpret_cast<uintptr_t>(_app_image_start) | 1  // Thumb bit
    );
    
    // Mark app as running (needed for syscalls)
    g_loader.set_entry(app_entry);  // Store for debugging
    
    // Call app entry point directly (runs _start -> main -> register_processor)
    app_entry();
    
    // Initialize USB
    init_usb();
    
    // Initialize SysTick
    init_systick();
    
    gpio_d.reset(15);  // Turn off blue
    gpio_d.reset(12);  // Green LED off - will turn on when synth outputs
    
    // Enter audio processing loop
    audio_loop();
    
    return 0;
}

// ============================================================================
// Vector Table and Startup
// ============================================================================

extern "C" {
    [[noreturn]] void Reset_Handler();
    void NMI_Handler()        { while (true) {} }
    void HardFault_Handler()  { gpio_d.set(14); while (true) {} }
    void MemManage_Handler()  { gpio_d.set(14); while (true) {} }
    void BusFault_Handler()   { gpio_d.set(14); while (true) {} }
    void UsageFault_Handler() { gpio_d.set(14); while (true) {} }
    void DebugMon_Handler()   { while (true) {} }
    void PendSV_Handler()     { while (true) {} }
}

// Build full vector table (98 entries for STM32F407)
__attribute__((section(".isr_vector"), used))
const void* const g_pfnVectors[98] = {
    &_estack,                                  // 0: Initial SP
    reinterpret_cast<void*>(Reset_Handler),   // 1: Reset
    reinterpret_cast<void*>(NMI_Handler),     // 2: NMI
    reinterpret_cast<void*>(HardFault_Handler), // 3: HardFault
    reinterpret_cast<void*>(MemManage_Handler), // 4: MemManage
    reinterpret_cast<void*>(BusFault_Handler),  // 5: BusFault
    reinterpret_cast<void*>(UsageFault_Handler), // 6: UsageFault
    nullptr, nullptr, nullptr, nullptr,        // 7-10: Reserved
    reinterpret_cast<void*>(SVC_Handler),      // 11: SVCall
    reinterpret_cast<void*>(DebugMon_Handler), // 12: DebugMon
    nullptr,                                   // 13: Reserved
    reinterpret_cast<void*>(PendSV_Handler),   // 14: PendSV
    reinterpret_cast<void*>(SysTick_Handler),  // 15: SysTick
    // IRQ 0-15
    nullptr, nullptr, nullptr, nullptr,        // 16-19 (IRQ 0-3)
    nullptr, nullptr, nullptr, nullptr,        // 20-23 (IRQ 4-7)
    nullptr, nullptr, nullptr, nullptr,        // 24-27 (IRQ 8-11)
    nullptr, nullptr,                          // 28-29 (IRQ 12-13)
    reinterpret_cast<void*>(DMA1_Stream3_IRQHandler), // 30: DMA1_Stream3 (IRQ 14) - PDM mic
    nullptr,                                   // 31 (IRQ 15)
    // IRQ 16-31
    reinterpret_cast<void*>(DMA1_Stream5_IRQHandler), // 32: DMA1_Stream5 (IRQ 16) - I2S audio
    nullptr, nullptr, nullptr,                 // 33-35 (IRQ 17-19)
    nullptr, nullptr, nullptr, nullptr,        // 36-39 (IRQ 20-23)
    nullptr, nullptr, nullptr, nullptr,        // 40-43 (IRQ 24-27)
    nullptr, nullptr, nullptr, nullptr,        // 44-47 (IRQ 28-31)
    // IRQ 32-47
    nullptr, nullptr, nullptr, nullptr,        // 48-51
    nullptr, nullptr, nullptr, nullptr,        // 52-55
    nullptr, nullptr, nullptr, nullptr,        // 56-59
    nullptr, nullptr, nullptr, nullptr,        // 60-63
    // IRQ 48-63
    nullptr, nullptr, nullptr, nullptr,        // 64-67
    nullptr, nullptr, nullptr, nullptr,        // 68-71
    nullptr, nullptr, nullptr, nullptr,        // 72-75
    nullptr, nullptr, nullptr, nullptr,        // 76-79
    // IRQ 64-66
    nullptr, nullptr, nullptr,                 // 80-82
    reinterpret_cast<void*>(OTG_FS_IRQHandler), // 83: OTG_FS (IRQ 67)
    // Remaining IRQs
    nullptr, nullptr, nullptr, nullptr,        // 84-87
    nullptr, nullptr, nullptr, nullptr,        // 88-91
    nullptr, nullptr, nullptr, nullptr,        // 92-95
    nullptr, nullptr,                          // 96-97
};

// C++ global constructors
extern "C" {
    extern void (*__init_array_start[])(void);
    extern void (*__init_array_end[])(void);
}

extern "C" [[noreturn]] void Reset_Handler() {
    // Enable FPU
    SCB::enable_fpu();
    __asm__ volatile("dsb\nisb" ::: "memory");
    
    // Copy .data from flash to RAM
    uint32_t* src = &_sidata;
    uint32_t* dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }
    
    // Zero .bss
    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0;
    }
    
    // Call C++ global constructors (init_array)
    for (void (**p)(void) = __init_array_start; p < __init_array_end; ++p) {
        (*p)();
    }
    
    // Call main
    main();
    
    // Should not return
    while (true) {}
}

// SPDX-License-Identifier: MIT
// STM32F4-Discovery USB MIDI Synthesizer (umios kernel version)
// Migrated from bare-metal to umios kernel-based architecture
// Uses synth.hh from headless_webhost (unchanged)
// Uses umiusb for portable USB Device Stack

#include <cstdint>

// umios Kernel
#include <umi_kernel.hh>
#include <umi_startup.hh>

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
#include <umios/backend/cm/stm32f4/hw.hh>
#include <umios/backend/cm/common/systick.hh>
#include <umios/backend/cm/common/nvic.hh>
#include <umios/backend/cm/common/scb.hh>
#include <umios/backend/cm/common/dwt.hh>
#include <umios/backend/cm/common/fault.hh>
#include <umios/backend/cm/common/irq.hh>
#include <umios/backend/cm/stm32f4/irq_num.hh>
#include <umios/backend/cm/cortex_m4.hh>
#include <umios/kernel/port/cm4/cm4.hh>

// USB stack (umiusb)
#include <umiusb.hh>
#include <audio/audio_interface.hh>
#include <hal/stm32_otg.hh>

// Synth engine (shared with WASM build)
#include <synth.hh>

using namespace umi::stm32;
using namespace umi::port::arm;
namespace cm4 = umi::port::cm4;

// ============================================================================
// umios Kernel Configuration
// ============================================================================

// Use library HW trait, extended with app-specific functions
struct AppHw : public umi::backend::stm32f4::Hw<168'000'000> {
    // Context switch implementation (calls kernel to get next task)
    static void request_context_switch();  // Defined after g_task_contexts
    
    // Audio DMA mute
    static void mute_audio_dma() {
        // TODO: Stop DMA and mute codec
    }
    
    // Start first task
    [[noreturn]] static void start_first_task();  // Defined after kernel instance
};

using HW = umi::Hw<AppHw>;
using Kernel = umi::Kernel<8, 16, HW>;

// Global kernel instance
Kernel g_kernel;

// Task IDs
umi::TaskId g_audio_task_id;
umi::TaskId g_usb_task_id;
umi::TaskId g_main_task_id;

// Event definitions (for kernel notification)
namespace Event {
    constexpr uint32_t PdmReady  = 1 << 0;  // PDM DMA complete
    constexpr uint32_t I2sReady  = 1 << 1;  // I2S DMA complete
    constexpr uint32_t UsbIrq    = 1 << 2;  // USB interrupt (IRQ occurred)
    constexpr uint32_t MidiReady = 1 << 4;  // MIDI data available
}

// MIDI queue (ISR -> Audio task)
struct MidiMsg {
    uint8_t data[4];
    uint8_t len;
};
umi::SpscQueue<MidiMsg, 64> g_midi_queue;

// ============================================================================
// Context Switch Implementation (using kernel port library)
// ============================================================================

// Task stacks (statically allocated)
constexpr uint32_t AUDIO_STACK_SIZE = 1024;  // words (4KB)
constexpr uint32_t USB_STACK_SIZE = 512;     // words (2KB)
constexpr uint32_t MAIN_STACK_SIZE = 1024;   // words (4KB)
constexpr uint32_t IDLE_STACK_SIZE = 256;    // words (1KB)
constexpr uint32_t MAX_TASKS = 8;

__attribute__((aligned(8)))
uint32_t g_audio_stack[AUDIO_STACK_SIZE];

__attribute__((aligned(8)))
uint32_t g_usb_stack[USB_STACK_SIZE];

__attribute__((aligned(8)))
uint32_t g_main_stack[MAIN_STACK_SIZE];

__attribute__((aligned(8)))
uint32_t g_idle_stack[IDLE_STACK_SIZE];

// Use TaskContext from kernel port library
using TaskContext = cm4::TaskContext;
TaskContext g_task_contexts[MAX_TASKS] = {};

// Current task TCB - required by kernel port handlers (C linkage)
TaskContext* volatile umi_cm4_current_tcb = nullptr;

// Context switch callback - required by kernel port handlers
extern "C" void umi_cm4_switch_context() {
    auto next = g_kernel.get_next_task();
    if (!next.has_value()) return;
    
    uint16_t next_idx = *next;
    if (next_idx >= MAX_TASKS || !g_task_contexts[next_idx].initialized) return;
    
    umi_cm4_current_tcb = &g_task_contexts[next_idx];
    g_kernel.prepare_switch(next_idx);
}

// Task wrapper to handle task exit
extern "C" void task_exit_error() {
    // Task returned - this should not happen
    // Red LED and halt
    GPIO gpio_d('D');
    gpio_d.set(14);
    while (true) { asm volatile(""); }
}

// Idle task entry
void idle_task_entry(void*) {
    while (true) {
        CM4::wfi();
    }
}

// request_context_switch - trigger PendSV for deferred context switch
void AppHw::request_context_switch() {
    SCB::trigger_pendsv();
}

// ============================================================================
// Configuration
// ============================================================================

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
constexpr uint32_t PDM_BUF_SIZE = 128;

__attribute__((section(".dma_buffer")))
uint16_t pdm_buf0[PDM_BUF_SIZE];

__attribute__((section(".dma_buffer")))
uint16_t pdm_buf1[PDM_BUF_SIZE];

// PCM buffer for decimated audio (from PDM)
constexpr uint32_t PCM_BUF_SIZE = 64;
int16_t pcm_buf[PCM_BUF_SIZE];

// Resampled mic buffer (32kHz -> 48kHz)
constexpr uint32_t RESAMPLED_BUF_SIZE = 64;
int16_t resampled_mic_buf[RESAMPLED_BUF_SIZE];

// Stereo buffer for USB Audio IN (mic L + synth R)
constexpr uint32_t STEREO_BUF_SIZE = 128;
int16_t stereo_buf[STEREO_BUF_SIZE];

// Mic ring buffer for async timing between PDM DMA and I2S DMA
// PDM runs at different rate than I2S, so buffering is needed
// Size: 256 samples provides margin for timing jitter
constexpr uint32_t MIC_RING_SIZE = 256;
int16_t mic_ring_buf[MIC_RING_SIZE];
volatile uint32_t mic_ring_write = 0;
volatile uint32_t mic_ring_read = 0;
int16_t mic_last_sample = 0;

// Synth is generated synchronously with I2S DMA, no ring buffer needed.
// Audio data flows: I2S DMA → audio_task → write_audio_in() → USB IN buffer (with ASRC)
// The USB IN buffer's ASRC handles I2S (47,991Hz) vs USB (48kHz) drift smoothly.

// ============================================================================
// Latency Measurement
// ============================================================================
// Buffer levels for debug monitoring
volatile uint32_t dbg_mic_ring_level = 0;     // mic_ring_buf level
volatile uint32_t dbg_usb_in_level = 0;       // USB library in_ring_buffer_ level
volatile uint32_t dbg_total_latency_us = 0;   // USB IN buffer latency in microseconds

// Latency calculation:
// - Each sample at 48kHz = 20.83us
// - Total latency = usb_in_level * 20.83us (synth ring eliminated)

// ============================================================================
// USB Descriptors
// ============================================================================

namespace usb_config {
using namespace umiusb::desc;

constexpr auto str_manufacturer = String("UMI-OS");
constexpr auto str_product = String("UMI Synth (umios)");

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

// USB stack instances (umiusb)
umiusb::Stm32FsHal usb_hal;
umiusb::AudioFullDuplexMidi48k usb_audio;
umiusb::Device<umiusb::Stm32FsHal, decltype(usb_audio)> usb_device(
    usb_hal, usb_audio,
    {
        .vendor_id = 0x1209,
        .product_id = 0x0006,
        .device_version = 0x0200,  // Version 2.0 for umios
        .manufacturer_idx = 1,
        .product_idx = 2,
        .serial_idx = 0,
    }
);

// PDM microphone
PdmMic pdm_mic;
DmaPdm dma_pdm;
CicDecimator cic_decimator;
Resampler32to48 resampler_mic;    // Mic only, synth runs at native 48kHz

// Synth engine (initialized at 48kHz - native I2S rate)
umi::synth::PolySynth synth;

// LED state
volatile uint32_t led_counter = 0;

// Debug counters
volatile uint32_t dbg_pdm_dma_count = 0;
volatile uint32_t dbg_pdm_pcm_count = 0;

// Audio task context (for deferred processing)
struct AudioContext {
    uint16_t* pdm_data = nullptr;
    bool pdm_pending = false;
    bool i2s_pending = false;
    int16_t* i2s_buf = nullptr;
} g_audio_ctx;

// ============================================================================
// Initialization Functions
// ============================================================================

void init_plli2s() {
    constexpr uint32_t RCC_PLLI2SCFGR = 0x40023884;
    constexpr uint32_t RCC_CR = 0x40023800;

    // Disable PLLI2S
    *reinterpret_cast<volatile uint32_t*>(RCC_CR) &= ~(1U << 26);

    // Configure: PLLI2SN=258, PLLI2SR=3
    // PLLI2SCLK = 1MHz × 258 / 3 = 86 MHz
    //
    // With MCKOE=1 (master clock output enabled for CS43L22):
    // Fs = I2SxCLK / [256 × (2×I2SDIV + ODD)]
    // 48000 ≈ 86MHz / [256 × 7] = 86MHz / 1792 = 47,991 Hz (-0.019%)
    //
    // Note: Exact 48kHz requires I2SxCLK = 48000 × 256 × N
    // N=7: 86.016 MHz (closest achievable with HSE=8MHz)
    *reinterpret_cast<volatile uint32_t*>(RCC_PLLI2SCFGR) =
        (3U << 28) |   // PLLI2SR = 3
        (258U << 6);   // PLLI2SN = 258

    // Enable PLLI2S
    *reinterpret_cast<volatile uint32_t*>(RCC_CR) |= (1U << 26);

    // Wait for lock
    while (!(*reinterpret_cast<volatile uint32_t*>(RCC_CR) & (1U << 27))) {}
}

void init_gpio() {
    RCC::enable_gpio('A');
    RCC::enable_gpio('B');
    RCC::enable_gpio('C');
    RCC::enable_gpio('D');
    RCC::enable_i2c1();
    RCC::enable_spi2();
    RCC::enable_spi3();
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

void init_audio() {
    i2c1.init();

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

    // DMA1_Stream5 = IRQ 16, priority 5 (audio priority)
    NVIC::set_prio(16, 5);
    NVIC::enable(16);

    i2s3.enable_dma();
    i2s3.enable();
    dma_i2s.enable();
    codec.power_on();
    codec.set_volume(0);
}

void init_pdm_mic() {
    pdm_mic.init();
    cic_decimator.reset();
    resampler_mic.reset();

    // Pre-fill mic ring buffer for initial buffering
    for (uint32_t i = 0; i < MIC_RING_SIZE / 2; ++i) {
        mic_ring_buf[i] = 0;
    }
    mic_ring_write = MIC_RING_SIZE / 2;
    mic_ring_read = 0;

    dma_pdm.init(pdm_buf0, pdm_buf1, PDM_BUF_SIZE, pdm_mic.dr_addr());

    // DMA1_Stream3 = IRQ 14, priority 5
    NVIC::set_prio(14, 5);
    NVIC::enable(14);

    pdm_mic.enable_dma();
    pdm_mic.enable();
    dma_pdm.enable();
}

}  // namespace

// Forward declaration for USB SOF callback (outside anonymous namespace)
void send_audio_in_from_rings();

namespace {

void init_usb() {
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
        static uint8_t cnt = 0;
        if (++cnt >= 48) {
            cnt = 0;
            gpio_d.toggle(12);
        }
    };
    
    // USB SOF callback (1ms) - supply Audio IN data synchronized with USB timing
    // This is the correct place for Audio IN, not SysTick
    usb_audio.on_sof_app = []() {
        send_audio_in_from_rings();
    };

    // USB MIDI -> MIDI queue (ISR context)
    usb_audio.set_midi_callback([](uint8_t /*cable*/, const uint8_t* data, uint8_t len) {
        MidiMsg msg;
        msg.len = (len > 4) ? 4 : len;
        for (uint8_t i = 0; i < msg.len; ++i) {
            msg.data[i] = data[i];
        }
        g_midi_queue.try_push(msg);
        // Notify audio task that MIDI is available
        g_kernel.notify(g_audio_task_id, Event::MidiReady);
    });

    usb_device.set_strings(usb_config::string_table);
    usb_device.init();
    usb_hal.connect();

    // OTG_FS = IRQ 67, priority 6 (lower than audio)
    NVIC::set_prio(67, 6);
    NVIC::enable(67);
}

// ============================================================================
// Audio Processing Functions (moved from ISR to task context)
// ============================================================================

void fill_audio_buffer(int16_t* buf, uint32_t frame_count) {
    // Fill I2S output from USB Audio OUT
    usb_audio.read_audio_asrc(buf, frame_count);
    
    // Process MIDI messages
    while (auto msg = g_midi_queue.try_pop()) {
        synth.handle_midi(msg->data, msg->len);
    }
    
    // Generate synth and mix with mic, write directly to USB IN buffer
    // USB IN buffer has ASRC which handles I2S (47,991Hz) vs USB (48kHz) drift
    for (uint32_t i = 0; i < frame_count; ++i) {
        // Generate synth sample
        float synth_sample = synth.process_sample();
        int32_t synth_s16 = static_cast<int32_t>(synth_sample * 32767.0f);
        if (synth_s16 > 32767) synth_s16 = 32767;
        if (synth_s16 < -32768) synth_s16 = -32768;
        
        // Read mic from ring buffer (PDM runs async to I2S)
        if (mic_ring_read != mic_ring_write) {
            mic_last_sample = mic_ring_buf[mic_ring_read];
            mic_ring_read = (mic_ring_read + 1) % MIC_RING_SIZE;
        }
        // If empty, mic_last_sample holds previous value (implicit zero-order hold)
        
        // Compose stereo frame: L=mic, R=synth
        stereo_buf[i * 2] = mic_last_sample;
        stereo_buf[i * 2 + 1] = static_cast<int16_t>(synth_s16);
    }
    
    // Write directly to USB IN buffer (ASRC handles clock drift)
    usb_audio.write_audio_in(stereo_buf, frame_count);
}

void process_pdm_buffer(uint16_t* pdm_data) {
    dbg_pdm_dma_count = dbg_pdm_dma_count + 1;

    // Decimate PDM to PCM (2.048MHz -> 32kHz)
    uint32_t pcm_count = cic_decimator.process_buffer(pdm_data, PDM_BUF_SIZE, pcm_buf, PCM_BUF_SIZE);
    dbg_pdm_pcm_count = pcm_count;

    // Resample mic 32kHz -> 48kHz
    uint32_t resampled_count = resampler_mic.process(pcm_buf, pcm_count, resampled_mic_buf);

    // Write to mic ring buffer
    for (uint32_t i = 0; i < resampled_count; ++i) {
        uint32_t next = (mic_ring_write + 1) % MIC_RING_SIZE;
        if (next != mic_ring_read) {  // Not full
            mic_ring_buf[mic_ring_write] = resampled_mic_buf[i];
            mic_ring_write = next;
        }
    }
}

}  // namespace

// Called from USB SOF (1ms) - for debug monitoring only
// Audio data is now written directly from fill_audio_buffer() at I2S timing
void send_audio_in_from_rings() {
    if (!usb_audio.is_audio_in_streaming()) {
        return;
    }
    
    // Update debug latency measurement (USB IN buffer level only now)
    dbg_usb_in_level = usb_audio.in_buffered_frames();
    dbg_total_latency_us = dbg_usb_in_level * 21;  // samples * 20.83us
    
    // Mic ring level for monitoring
    uint32_t mic_level = (mic_ring_write >= mic_ring_read)
        ? (mic_ring_write - mic_ring_read)
        : (MIC_RING_SIZE - mic_ring_read + mic_ring_write);
    dbg_mic_ring_level = mic_level;
}

// ============================================================================
// Task Entry Points
// ============================================================================

/// Audio task: Realtime priority, processes audio in deferred context
void audio_task_entry(void*) {
    // Debug: Blue LED indicates audio_task started
    gpio_d.set(15);
    
    while (true) {
        // Wait for audio events (PDM or I2S DMA complete)
        uint32_t events = g_kernel.wait_block(
            g_audio_task_id,
            Event::PdmReady | Event::I2sReady
        );

        // Process PDM (microphone only - synth moved to I2S timing)
        if (events & Event::PdmReady) {
            if (g_audio_ctx.pdm_pending && g_audio_ctx.pdm_data) {
                process_pdm_buffer(g_audio_ctx.pdm_data);
                g_audio_ctx.pdm_pending = false;
            }
        }

        // Process I2S (audio output + synth @ 48kHz + USB IN)
        if (events & Event::I2sReady) {
            if (g_audio_ctx.i2s_pending && g_audio_ctx.i2s_buf) {
                fill_audio_buffer(g_audio_ctx.i2s_buf, BUFFER_SIZE);
                g_audio_ctx.i2s_pending = false;
            }
        }

        // Activity LED
        led_counter = led_counter + 1;
        if (led_counter >= 750) {
            led_counter = 0;
            gpio_d.toggle(14);
        }
    }
}

/// USB task: Server priority, handles USB notifications
/// Most USB work happens in IRQ (poll) and SysTick (audio IN)
void usb_task_entry(void*) {
    while (true) {
        // Wait for USB events
        g_kernel.wait_block(g_usb_task_id, Event::UsbIrq);
        // Currently nothing to do here - poll() is in ISR
        // This task exists for future USB work that needs task context
    }
}

/// Main task: User priority, initialization and UI
void main_task_entry(void*) {
    // Initialize synth at 48kHz (native I2S rate, no resampling needed)
    constexpr float SYNTH_RATE = 48000.0f;
    synth.init(SYNTH_RATE);

    // Initialize audio subsystem
    init_audio();

    // Initialize PDM microphone
    init_pdm_mic();

    // Initialize USB
    init_usb();

    // Initialize SysTick (1ms)
    SysTick::init(168000 - 1);

    // Enable DWT cycle counter
    DWT::enable();

    // Green LED indicates ready
    gpio_d.set(12);

    // Main loop: button handling - low priority task, yields to audio/usb
    // Since main_task has User priority (lowest active), higher priority
    // tasks (audio=Realtime, usb=Server) will preempt as needed.
    while (true) {
        if (gpio_a.read(0)) {
            // Button pressed: debug indicator
            gpio_d.reset(12); gpio_d.reset(13); gpio_d.reset(14); gpio_d.reset(15);

            for (uint32_t i = 0; i < usb_hal.dbg_ep0_stall_count_ && i < 20; ++i) {
                gpio_d.set(14);
                for (int d = 0; d < 500000; ++d) asm volatile("");
                gpio_d.reset(14);
                for (int d = 0; d < 500000; ++d) asm volatile("");
            }

            for (int d = 0; d < 2000000; ++d) asm volatile("");

            for (uint32_t i = 0; i < (usb_hal.dbg_setup_count_ % 10); ++i) {
                gpio_d.set(12);
                for (int d = 0; d < 500000; ++d) asm volatile("");
                gpio_d.reset(12);
                for (int d = 0; d < 500000; ++d) asm volatile("");
            }

            while (gpio_a.read(0)) {}
        }

        // WFI - wait for interrupt. When interrupt fires, ISR runs and
        // may notify higher priority tasks. After ISR returns, PendSV
        // context switch runs and scheduler picks highest priority ready task.
        AppHw::enter_sleep();
    }
}

// ============================================================================
// Interrupt Handlers (simplified: notify only)
// ============================================================================

// IRQ handlers are now registered as lambdas in Reset_Handler

// ============================================================================
// Fault Info (for debugging)
// ============================================================================

using umi::backend::cm::FaultInfo;
volatile FaultInfo g_fault_info = {};

// ============================================================================
// Startup Code
// ============================================================================

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern void (*__init_array_start[])();
extern void (*__init_array_end[])();

// Linker symbols for shared memory
extern uint8_t _shared_audio, _shared_midi, _shared_display, _shared_hwstate;
extern uint8_t _shared_end;

// Initialize task stack and context
void init_task_context(uint16_t task_idx, uint32_t* stack_base, uint32_t stack_size,
                       void (*entry)(void*), void* arg, bool uses_fpu) {
    uint32_t* stack_top = stack_base + stack_size;  // Stack grows down
    uint32_t* sp = cm4::init_task_stack(stack_top, entry, arg, uses_fpu);
    
    g_task_contexts[task_idx].stack_ptr = sp;
    g_task_contexts[task_idx].entry = entry;
    g_task_contexts[task_idx].arg = arg;
    g_task_contexts[task_idx].uses_fpu = uses_fpu;
    g_task_contexts[task_idx].initialized = true;
}

// Start first task - uses library implementation
[[noreturn]] void AppHw::start_first_task() {
    // Get first task to run
    auto next = g_kernel.get_next_task();
    if (!next.has_value()) {
        while (true) { CM4::wfi(); }
    }
    
    uint16_t task_idx = *next;
    g_kernel.prepare_switch(task_idx);
    
    // Set current TCB
    umi_cm4_current_tcb = &g_task_contexts[task_idx];
    
    // Set exception priorities
    SCB::set_exc_prio(14, 0xFF);  // PendSV - lowest
    SCB::set_exc_prio(15, 0xF0);  // SysTick - low
    
    // Use library's start_first_task (triggers SVC)
    cm4::start_first_task();
}

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

    // Initialize dynamic IRQ system (SRAM vector table)
    umi::irq::init();
    
    // Register system exception handlers
    namespace exc = umi::backend::cm::exc;
    umi::irq::set_handler(exc::HardFault, +[]() {
        const_cast<FaultInfo&>(g_fault_info).capture();
        gpio_d.set(14);
        while (true) { asm volatile("" ::: "memory"); }
    });
    umi::irq::set_handler(exc::MemManage, +[]() {
        const_cast<FaultInfo&>(g_fault_info).capture();
        gpio_d.set(14);
        while (true) { asm volatile(""); }
    });
    umi::irq::set_handler(exc::BusFault, +[]() {
        const_cast<FaultInfo&>(g_fault_info).capture();
        gpio_d.set(14);
        while (true) { asm volatile(""); }
    });
    umi::irq::set_handler(exc::UsageFault, +[]() {
        const_cast<FaultInfo&>(g_fault_info).capture();
        gpio_d.set(14);
        while (true) { asm volatile(""); }
    });
    umi::irq::set_handler(exc::SVCall, cm4::SVC_Handler);
    umi::irq::set_handler(exc::PendSV, cm4::PendSV_Handler);
    umi::irq::set_handler(exc::SysTick, +[]() {
        g_kernel.tick(1000);  // 1ms = 1000us
    });
    
    // Register peripheral IRQ handlers
    namespace irqn = umi::stm32f4::irq;
    umi::irq::set_handler(irqn::DMA1_Stream3, +[]() {  // PDM DMA
        if (dma_pdm.transfer_complete()) {
            dma_pdm.clear_tc();
            g_audio_ctx.pdm_data = (dma_pdm.current_buffer() == 0) ? pdm_buf1 : pdm_buf0;
            g_audio_ctx.pdm_pending = true;
            g_kernel.notify(g_audio_task_id, Event::PdmReady);
        }
    });
    umi::irq::set_handler(irqn::DMA1_Stream5, +[]() {  // I2S DMA
        if (dma_i2s.transfer_complete()) {
            dma_i2s.clear_tc();
            g_audio_ctx.i2s_buf = (dma_i2s.current_buffer() == 0) ? audio_buf1 : audio_buf0;
            g_audio_ctx.i2s_pending = true;
            g_kernel.notify(g_audio_task_id, Event::I2sReady);
        }
    });
    umi::irq::set_handler(irqn::OTG_FS, +[]() {  // USB
        usb_device.poll();
        g_kernel.notify(g_usb_task_id, Event::UsbIrq);
    });

    // Call global constructors
    for (auto fn = __init_array_start; fn < __init_array_end; ++fn) {
        (*fn)();
    }

    // Initialize clocks (168MHz)
    RCC::init_168mhz();

    // Initialize GPIO
    init_gpio();

    // Green LED on during init
    gpio_d.set(12);

    // Create kernel tasks and initialize their stacks
    // Task priorities:
    // - audio: Realtime (highest) - real-time audio processing
    // - usb: Server - USB handling
    // - main: User - button handling, UI
    // - idle: Idle (lowest) - sleep when nothing to do
    //
    // Key: audio_task and usb_task start blocked (wait_block),
    // so main_task will be selected first for initialization.
    // After init, main_task loops in WFI. When DMA/USB interrupts fire,
    // they notify audio/usb tasks which become Ready and get scheduled.

    g_audio_task_id = g_kernel.create_task({
        .entry = audio_task_entry,
        .arg = nullptr,
        .prio = umi::Priority::Realtime,  // Highest: real-time audio
        .core_affinity = 0,
        .uses_fpu = true,
        .name = "audio"
    });
    init_task_context(g_audio_task_id.value, g_audio_stack, AUDIO_STACK_SIZE,
                      audio_task_entry, nullptr, true);  // uses_fpu = true

    g_usb_task_id = g_kernel.create_task({
        .entry = usb_task_entry,
        .arg = nullptr,
        .prio = umi::Priority::Server,  // Medium: USB handling
        .core_affinity = 0,
        .uses_fpu = false,
        .name = "usb"
    });
    init_task_context(g_usb_task_id.value, g_usb_stack, USB_STACK_SIZE,
                      usb_task_entry, nullptr, false);  // uses_fpu = false

    // Main task is User priority - will run first since others start blocked
    g_main_task_id = g_kernel.create_task({
        .entry = main_task_entry,
        .arg = nullptr,
        .prio = umi::Priority::User,  // Low: button/UI
        .core_affinity = 0,
        .uses_fpu = true,
        .name = "main"
    });
    init_task_context(g_main_task_id.value, g_main_stack, MAIN_STACK_SIZE,
                      main_task_entry, nullptr, true);  // uses_fpu = true

    // Create idle task
    auto idle_task_id = g_kernel.create_task({
        .entry = idle_task_entry,
        .arg = nullptr,
        .prio = umi::Priority::Idle,
        .core_affinity = 0,
        .uses_fpu = false,
        .name = "idle"
    });
    init_task_context(idle_task_id.value, g_idle_stack, IDLE_STACK_SIZE,
                      idle_task_entry, nullptr, false);  // uses_fpu = false

    // Set interrupt priorities for kernel
    // SysTick = highest (0) for kernel tick
    // PendSV = lowest (15) for context switch
    NVIC::set_prio(-1, 0);   // SysTick
    NVIC::set_prio(-2, 15);  // PendSV

    // Start the scheduler
    AppHw::start_first_task();
}

// ============================================================================
// Boot Vector Table (minimal - only SP and Reset needed in Flash)
// ============================================================================
// The full vector table is in SRAM and managed by umi::irq system.
// VTOR is updated to point to SRAM table early in Reset_Handler.

__attribute__((section(".isr_vector"), used))
const void* const g_boot_vectors[2] = {
    reinterpret_cast<const void*>(&_estack),       // 0: Initial SP
    reinterpret_cast<const void*>(Reset_Handler),  // 1: Reset
};

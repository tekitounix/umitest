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
// umios Kernel Configuration
// ============================================================================

/// Hardware abstraction layer for STM32F4 @ 168MHz
struct Stm32f4Hw {
    static constexpr uint32_t CPU_FREQ = 168'000'000;
    static constexpr uint32_t CYCLES_PER_USEC = CPU_FREQ / 1'000'000;
    
    // Timer (using SysTick for now)
    static void set_timer_absolute(umi::usec target) {
        (void)target;  // SysTick is periodic, not one-shot
    }
    
    static umi::usec monotonic_time_usecs() {
        // Use DWT cycle counter for monotonic time
        static uint64_t base_time = 0;
        static uint32_t last_cycles = 0;
        uint32_t now = DWT::cycles();
        uint32_t delta = now - last_cycles;
        last_cycles = now;
        base_time += delta / CYCLES_PER_USEC;
        return base_time;
    }
    
    // Critical section (disable interrupts)
    static void enter_critical() { CM4::cpsid_i(); }
    static void exit_critical() { CM4::cpsie_i(); }
    
    // Multi-core (single core on STM32F4)
    static void trigger_ipi(uint8_t) { }
    static uint8_t current_core() { return 0; }
    
    // Context switch (trigger PendSV)
    static void request_context_switch();  // Defined after g_task_contexts
    
    // FPU context save/restore
    static void save_fpu() {
        // Lazy stacking handled by hardware on Cortex-M4F
    }
    static void restore_fpu() {
        // Lazy stacking handled by hardware on Cortex-M4F
    }
    
    // Audio DMA mute
    static void mute_audio_dma() {
        // TODO: Stop DMA and mute codec
    }
    
    // Persistent storage (backup RAM)
    static void write_backup_ram(const void*, size_t) { }
    static void read_backup_ram(void*, size_t) { }
    
    // MPU configuration
    static void configure_mpu_region(size_t, const void*, size_t, bool, bool) { }
    
    // Cache (no cache on Cortex-M4)
    static void cache_clean(const void*, size_t) { }
    static void cache_invalidate(void*, size_t) { }
    static void cache_clean_invalidate(void*, size_t) { }
    
    // System
    static void system_reset() { SCB::reset(); }
    static void enter_sleep() { CM4::wfi(); }
    [[noreturn]] static void start_first_task();  // Defined after kernel instance
    
    // Watchdog (not implemented)
    static void watchdog_init(uint32_t) { }
    static void watchdog_feed() { }
    
    // Performance counters
    static uint32_t cycle_count() { return DWT::cycles(); }
    static uint32_t cycles_per_usec() { return CYCLES_PER_USEC; }
};

using HW = umi::Hw<Stm32f4Hw>;
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
// Context Switch Implementation (FreeRTOS-style)
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

// Task control block (FreeRTOS-style: stack_ptr is FIRST member)
// This allows simple pointer arithmetic in assembly
struct TaskContext {
    uint32_t* stack_ptr;     // Current stack pointer (PSP) - MUST BE FIRST
    void (*entry)(void*);    // Entry function
    void* arg;               // Entry argument
    bool uses_fpu;           // Task uses FPU
    bool initialized;        // Has been initialized
};

TaskContext g_task_contexts[MAX_TASKS] = {};

// Pointer to current task's TCB (for PendSV handler)
// FreeRTOS calls this pxCurrentTCB
TaskContext* volatile g_pxCurrentTCB = nullptr;

// Cortex-M4 exception stack frame (hardware pushed)
// Order: R0, R1, R2, R3, R12, LR, PC, xPSR
// With FPU: + S0-S15, FPSCR (lazy stacking)

// Initial stack frame for a new task
// Stack grows downward, so we build from top
// Supports both FPU and non-FPU tasks
uint32_t* init_task_stack(uint32_t* stack_top, void (*entry)(void*), void* arg, bool uses_fpu) {
    // Align stack to 8 bytes
    stack_top = reinterpret_cast<uint32_t*>(
        reinterpret_cast<uintptr_t>(stack_top) & ~0x7UL
    );
    
    // Exception return values:
    // 0xFFFFFFFD = Thread mode, PSP, basic frame (no FPU context)
    // 0xFFFFFFED = Thread mode, PSP, extended frame (with FPU context)
    // Bit 4: 1 = basic frame, 0 = extended frame
    constexpr uint32_t EXC_RETURN_PSP_BASIC = 0xFFFFFFFD;    // No FPU
    constexpr uint32_t EXC_RETURN_PSP_EXTENDED = 0xFFFFFFED; // With FPU
    
    if (uses_fpu) {
        // FPU extended frame: S0-S15, FPSCR, reserved (18 words)
        // Hardware pushes these on exception entry when FPU was used
        *(--stack_top) = 0x00000000;  // Reserved (alignment)
        *(--stack_top) = 0x00000000;  // FPSCR
        for (int i = 15; i >= 0; --i) {
            *(--stack_top) = 0x00000000;  // S15..S0
        }
    }
    
    // Build hardware exception frame (pushed automatically on exception)
    *(--stack_top) = 0x01000000;                          // xPSR (Thumb bit set)
    *(--stack_top) = reinterpret_cast<uint32_t>(entry);   // PC (task entry)
    *(--stack_top) = 0xFFFFFFFE;                          // LR (invalid, task should not return)
    *(--stack_top) = 0x12121212;                          // R12
    *(--stack_top) = 0x03030303;                          // R3
    *(--stack_top) = 0x02020202;                          // R2
    *(--stack_top) = 0x01010101;                          // R1
    *(--stack_top) = reinterpret_cast<uint32_t>(arg);     // R0 (argument)
    
    if (uses_fpu) {
        // S16-S31 (software saved, 16 words)
        for (int i = 31; i >= 16; --i) {
            *(--stack_top) = 0x00000000;  // S31..S16
        }
    }
    
    // Software saved context: ldmia pops in register order (r4,r5...r11,lr)
    // So we must push in REVERSE order: LR first (highest addr), then r11..r4
    // Stack grows down, ldmia reads from low to high
    // Push order (stack_top decrements): lr, r11, r10, ..., r4
    // Memory layout after: [r4][r5][r6][r7][r8][r9][r10][r11][lr]  <- stack_top points here
    
    // Exception return value (will be popped into LR)
    *(--stack_top) = uses_fpu ? EXC_RETURN_PSP_EXTENDED : EXC_RETURN_PSP_BASIC;
    
    // R11-R4 (pushed in descending order so ldmia loads them correctly)
    *(--stack_top) = 0x11111111;  // R11
    *(--stack_top) = 0x10101010;  // R10
    *(--stack_top) = 0x09090909;  // R9
    *(--stack_top) = 0x08080808;  // R8
    *(--stack_top) = 0x07070707;  // R7
    *(--stack_top) = 0x06060606;  // R6
    *(--stack_top) = 0x05050505;  // R5
    *(--stack_top) = 0x04040404;  // R4
    
    return stack_top;
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

// request_context_switch implementation (FreeRTOS-style)
// Called when scheduler determines a context switch is needed
void Stm32f4Hw::request_context_switch() {
    // Get the next task from kernel
    auto next = g_kernel.get_next_task();
    if (!next.has_value()) {
        return;  // No task to switch to
    }
    
    uint16_t next_idx = *next;
    
    // Check if task context is initialized
    if (next_idx >= MAX_TASKS || !g_task_contexts[next_idx].initialized) {
        return;
    }
    
    // Don't switch to same task
    if (g_pxCurrentTCB == &g_task_contexts[next_idx]) {
        return;
    }
    
    // Prepare kernel state for the switch
    g_kernel.prepare_switch(next_idx);
    
    // Trigger PendSV - actual switch happens in PendSV_Handler
    // PendSV will save current context to g_pxCurrentTCB->stack_ptr
    // and restore next context from g_task_contexts[next_idx].stack_ptr
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

// Ring buffers for async timing between I2S DMA (~1.33ms/64samples) and USB SOF (1ms/48samples)
// Optimized for low latency while maintaining stability:
// - I2S DMA: 64 samples × 2 (double buffer) = 128 samples worst case
// - USB SOF: 48 samples per frame
// - Safety margin: ~32 samples
// Size: 192 samples = 4ms buffer at 48kHz (was 512 = 10.6ms)
constexpr uint32_t AUDIO_RING_SIZE = 192;
constexpr uint32_t AUDIO_RING_TARGET = 96;   // Target level (2ms) - was 256 (5.3ms)
constexpr uint32_t AUDIO_RING_HIGH = 144;    // Above this: skip samples (3ms)
constexpr uint32_t AUDIO_RING_LOW = 48;      // Below this: duplicate samples (1ms)

// Mic ring buffer
int16_t mic_ring_buf[AUDIO_RING_SIZE];
volatile uint32_t mic_ring_write = 0;
volatile uint32_t mic_ring_read = 0;
int16_t mic_last_sample = 0;
volatile uint32_t mic_underrun_count = 0;
volatile uint32_t mic_skip_count = 0;    // Drift correction: samples skipped
volatile uint32_t mic_dup_count = 0;     // Drift correction: samples duplicated

// Synth ring buffer
int16_t synth_ring_buf[AUDIO_RING_SIZE];
volatile uint32_t synth_ring_write = 0;
volatile uint32_t synth_ring_read = 0;
int16_t synth_last_sample = 0;
volatile uint32_t synth_underrun_count = 0;
volatile uint32_t synth_skip_count = 0;  // Drift correction: samples skipped
volatile uint32_t synth_dup_count = 0;   // Drift correction: samples duplicated

// ============================================================================
// Latency Measurement
// ============================================================================
// Buffer levels at each stage (in samples @ 48kHz)
volatile uint32_t dbg_mic_ring_level = 0;     // mic_ring_buf level
volatile uint32_t dbg_synth_ring_level = 0;   // synth_ring_buf level
volatile uint32_t dbg_usb_in_level = 0;       // USB library in_ring_buffer_ level
volatile uint32_t dbg_total_latency_us = 0;   // Calculated total latency in microseconds

// Latency calculation:
// - Each sample at 48kHz = 20.83us
// - Total latency = (mic_ring_level + usb_in_level) * 20.83us
// - For synth: synth_ring_level + usb_in_level

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

    // Pre-fill ring buffers to target level for drift correction headroom
    // Target = 96 samples (2ms at 48kHz) - optimized for low latency
    for (uint32_t i = 0; i < AUDIO_RING_TARGET; ++i) {
        mic_ring_buf[i] = 0;
        synth_ring_buf[i] = 0;
    }
    mic_ring_write = AUDIO_RING_TARGET;
    mic_ring_read = 0;
    synth_ring_write = AUDIO_RING_TARGET;
    synth_ring_read = 0;

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
    
    // Process synth at 48kHz and write to ring buffer
    for (uint32_t i = 0; i < frame_count; ++i) {
        float synth_sample = synth.process_sample();
        int32_t synth_s16 = static_cast<int32_t>(synth_sample * 32767.0f);
        if (synth_s16 > 32767) synth_s16 = 32767;
        if (synth_s16 < -32768) synth_s16 = -32768;
        
        // Write to synth ring buffer
        uint32_t next = (synth_ring_write + 1) % AUDIO_RING_SIZE;
        if (next != synth_ring_read) {  // Not full
            synth_ring_buf[synth_ring_write] = static_cast<int16_t>(synth_s16);
            synth_ring_write = next;
        }
    }
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
        uint32_t next = (mic_ring_write + 1) % AUDIO_RING_SIZE;
        if (next != mic_ring_read) {  // Not full
            mic_ring_buf[mic_ring_write] = resampled_mic_buf[i];
            mic_ring_write = next;
        }
    }
}

}  // namespace

// Called from USB SOF (1ms) - synchronized with USB host timing
void send_audio_in_from_rings() {
    if (!usb_audio.is_audio_in_streaming()) {
        return;
    }
    
    // Calculate ring buffer levels
    uint32_t mic_level = (mic_ring_write >= mic_ring_read)
        ? (mic_ring_write - mic_ring_read)
        : (AUDIO_RING_SIZE - mic_ring_read + mic_ring_write);
    uint32_t synth_level = (synth_ring_write >= synth_ring_read)
        ? (synth_ring_write - synth_ring_read)
        : (AUDIO_RING_SIZE - synth_ring_read + synth_ring_write);
    
    // Update debug latency measurements
    dbg_mic_ring_level = mic_level;
    dbg_synth_ring_level = synth_level;
    dbg_usb_in_level = usb_audio.in_buffered_frames();
    // Total latency in microseconds: (app_ring + usb_ring) * 1000000 / 48000
    // Simplified: samples * 20.83us ≈ samples * 21
    dbg_total_latency_us = (synth_level + dbg_usb_in_level) * 21;
    
    // Adaptive drift correction:
    // If buffer is filling up (producer faster), skip extra samples
    // If buffer is draining (consumer faster), duplicate samples
    
    // Mic drift correction
    if (mic_level > AUDIO_RING_HIGH && mic_ring_read != mic_ring_write) {
        // Skip one sample to reduce level
        mic_ring_read = (mic_ring_read + 1) % AUDIO_RING_SIZE;
        mic_skip_count = mic_skip_count + 1;
    }
    
    // Synth drift correction
    if (synth_level > AUDIO_RING_HIGH && synth_ring_read != synth_ring_write) {
        // Skip one sample to reduce level
        synth_ring_read = (synth_ring_read + 1) % AUDIO_RING_SIZE;
        synth_skip_count = synth_skip_count + 1;
    }
    
    // USB Audio IN expects ~48 samples per 1ms SOF
    constexpr uint32_t USB_FRAME_SIZE = 48;
    
    for (uint32_t i = 0; i < USB_FRAME_SIZE; ++i) {
        // Read mic from ring buffer
        if (mic_ring_read != mic_ring_write) {
            mic_last_sample = mic_ring_buf[mic_ring_read];
            mic_ring_read = (mic_ring_read + 1) % AUDIO_RING_SIZE;
        } else {
            // Underrun: keep last sample (already duplicating)
            mic_underrun_count = mic_underrun_count + 1;
        }
        
        // Read synth from ring buffer
        if (synth_ring_read != synth_ring_write) {
            synth_last_sample = synth_ring_buf[synth_ring_read];
            synth_ring_read = (synth_ring_read + 1) % AUDIO_RING_SIZE;
        } else {
            // Underrun: keep last sample (already duplicating)
            synth_underrun_count = synth_underrun_count + 1;
        }
        
        stereo_buf[i * 2] = mic_last_sample;       // L = mic
        stereo_buf[i * 2 + 1] = synth_last_sample; // R = synth
    }
    
    // Low buffer level: duplicate last sample (done implicitly by keeping last_sample)
    // This happens naturally when underrun occurs
    if (mic_level < AUDIO_RING_LOW) {
        mic_dup_count = mic_dup_count + 1;
    }
    if (synth_level < AUDIO_RING_LOW) {
        synth_dup_count = synth_dup_count + 1;
    }
    
    usb_audio.write_audio_in(stereo_buf, USB_FRAME_SIZE);
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
        Stm32f4Hw::enter_sleep();
    }
}

// ============================================================================
// Interrupt Handlers (simplified: notify only)
// ============================================================================

extern "C" void DMA1_Stream3_IRQHandler() {
    // PDM microphone DMA transfer complete
    if (dma_pdm.transfer_complete()) {
        dma_pdm.clear_tc();
        
        // Get the buffer that just completed
        g_audio_ctx.pdm_data = (dma_pdm.current_buffer() == 0) ? pdm_buf1 : pdm_buf0;
        g_audio_ctx.pdm_pending = true;
        
        // Notify audio task
        g_kernel.notify(g_audio_task_id, Event::PdmReady);
    }
}

extern "C" void DMA1_Stream5_IRQHandler() {
    // I2S DMA transfer complete
    if (dma_i2s.transfer_complete()) {
        dma_i2s.clear_tc();
        
        // Get the buffer to fill
        g_audio_ctx.i2s_buf = (dma_i2s.current_buffer() == 0) ? audio_buf1 : audio_buf0;
        g_audio_ctx.i2s_pending = true;
        
        // Notify audio task
        g_kernel.notify(g_audio_task_id, Event::I2sReady);
    }
}

extern "C" void OTG_FS_IRQHandler() {
    // USB interrupt: poll MUST be called here to clear interrupt flags
    // Otherwise the interrupt will fire again immediately
    usb_device.poll();
    
    // Notify USB task for additional processing if needed
    g_kernel.notify(g_usb_task_id, Event::UsbIrq);
}

extern "C" void SysTick_Handler() {
    // Kernel tick (1ms) - OS scheduling only
    // Audio is handled by USB SOF callback (on_sof_app)
    g_kernel.tick(1000);  // 1ms = 1000us
}

// ============================================================================
// PendSV Handler - Context Switch (FreeRTOS-style)
// ============================================================================
// This is the core of the context switch mechanism.
// - Save current task's context to its stack
// - Get next task from kernel
// - Restore next task's context from its stack
//
// Stack layout after save (FreeRTOS style):
//   [Low address / top of saved stack]
//   EXC_RETURN (LR)
//   R4..R11     <- Software saved
//   S16..S31    <- Software saved (if FPU used)
//   R0..R3, R12, LR, PC, xPSR  <- Hardware pushed
//   S0..S15, FPSCR, reserved   <- Hardware pushed (if FPU used)
//   [High address / original stack top]

extern "C" __attribute__((naked)) void PendSV_Handler() {
    asm volatile(
        "   .syntax unified                 \n"
        
        // Get PSP (current task's stack pointer)
        "   mrs     r0, psp                 \n"
        "   isb                             \n"
        
        // Get address of g_pxCurrentTCB
        "   ldr     r3, =g_pxCurrentTCB     \n"
        "   ldr     r2, [r3]                \n"  // r2 = g_pxCurrentTCB (pointer to current TCB)
        
        // Is the task using the FPU? If so, save S16-S31
        "   tst     lr, #0x10               \n"  // Bit 4 = 0 means FPU was used
        "   it      eq                      \n"
        "   vstmdbeq r0!, {s16-s31}         \n"  // Push S16-S31 (FPU high registers)
        
        // Save R4-R11 and EXC_RETURN (LR)
        "   stmdb   r0!, {r4-r11, lr}       \n"
        
        // Save new stack top to TCB (TCB->stack_ptr = r0)
        "   str     r0, [r2]                \n"  // TCB's first member is stack_ptr
        
        // --- Context switch logic ---
        // Disable interrupts during switch
        "   mov     r0, %[max_prio]         \n"
        "   msr     basepri, r0             \n"
        "   dsb                             \n"
        "   isb                             \n"
        
        // Call vTaskSwitchContext equivalent - select next task
        "   bl      vPortSwitchContext      \n"
        
        // Re-enable interrupts
        "   mov     r0, #0                  \n"
        "   msr     basepri, r0             \n"
        
        // --- Restore next task ---
        // Get the new current TCB (may have changed)
        "   ldr     r3, =g_pxCurrentTCB     \n"
        "   ldr     r1, [r3]                \n"  // r1 = new g_pxCurrentTCB
        "   ldr     r0, [r1]                \n"  // r0 = TCB->stack_ptr
        
        // Restore R4-R11 and EXC_RETURN
        "   ldmia   r0!, {r4-r11, lr}       \n"
        
        // Is the task using FPU? If so, restore S16-S31
        "   tst     lr, #0x10               \n"
        "   it      eq                      \n"
        "   vldmiaeq r0!, {s16-s31}         \n"
        
        // Set PSP to restored value
        "   msr     psp, r0                 \n"
        "   isb                             \n"
        
        // Return from exception (hardware restores R0-R3, R12, LR, PC, xPSR)
        "   bx      lr                      \n"
        
        ".align 4                           \n"
        :
        : [max_prio] "i" (0x50)  // Priority for critical section (adjustable)
        : "memory"
    );
}

// Context switch helper - called from PendSV
extern "C" void vPortSwitchContext() {
    // Get next task from kernel
    auto next = g_kernel.get_next_task();
    if (!next.has_value()) {
        return;  // Stay on current task
    }
    
    uint16_t next_idx = *next;
    
    // Check if task context is initialized
    if (next_idx >= MAX_TASKS || !g_task_contexts[next_idx].initialized) {
        return;
    }
    
    // Update current TCB pointer
    g_pxCurrentTCB = &g_task_contexts[next_idx];
    
    // Prepare kernel state for the switch
    g_kernel.prepare_switch(next_idx);
}

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
    uint32_t* sp = init_task_stack(stack_top, entry, arg, uses_fpu);
    
    g_task_contexts[task_idx].stack_ptr = sp;
    g_task_contexts[task_idx].entry = entry;
    g_task_contexts[task_idx].arg = arg;
    g_task_contexts[task_idx].uses_fpu = uses_fpu;
    g_task_contexts[task_idx].initialized = true;
}

// Start first task implementation (FreeRTOS-style using SVC)
// This properly starts the first task using exception return
[[noreturn]] void Stm32f4Hw::start_first_task() {
    // Get first task to run
    auto next = g_kernel.get_next_task();
    if (!next.has_value()) {
        // No task to run - halt
        while (true) { CM4::wfi(); }
    }
    
    uint16_t task_idx = *next;
    g_kernel.prepare_switch(task_idx);
    
    // Set current TCB to the first task
    g_pxCurrentTCB = &g_task_contexts[task_idx];
    
    // Set PendSV and SysTick priorities
    // PendSV = lowest (0xFF), SysTick = lower than DMA but can preempt tasks
    SCB::set_exc_prio(14, 0xFF);  // PendSV - lowest priority
    SCB::set_exc_prio(15, 0xF0);  // SysTick - low priority
    
    // FreeRTOS-style: Reset MSP, clear FPU state, call SVC
    asm volatile(
        "   .syntax unified                 \n"
        
        // Use NVIC offset register to locate the stack (like FreeRTOS)
        "   ldr     r0, =0xE000ED08         \n"  // VTOR address
        "   ldr     r0, [r0]                \n"  // Read VTOR (vector table address)
        "   ldr     r0, [r0]                \n"  // First entry is initial MSP
        "   msr     msp, r0                 \n"  // Reset MSP to clean state
        
        // Clear FPCA bit in CONTROL to ensure clean FPU state
        // This prevents stale FPU state from causing issues
        "   mov     r0, #0                  \n"
        "   msr     control, r0             \n"  // CONTROL = 0 (privileged, MSP, no FPU)
        "   isb                             \n"
        
        // Enable interrupts
        "   cpsie   i                       \n"
        "   cpsie   f                       \n"
        "   dsb                             \n"
        "   isb                             \n"
        
        // Call SVC to start the first task
        // SVC handler will restore context from g_pxCurrentTCB
        "   svc     #0                      \n"
        "   nop                             \n"
        ::: "r0", "memory"
    );
    
    // Should never reach here
    while (true) { CM4::wfi(); }
}

// SVC Handler - Starts the first task (FreeRTOS-style)
// Called via SVC #0 from start_first_task
extern "C" __attribute__((naked)) void SVC_Handler() {
    asm volatile(
        "   .syntax unified                 \n"
        
        // Get the current TCB
        "   ldr     r3, =g_pxCurrentTCB     \n"
        "   ldr     r1, [r3]                \n"  // r1 = g_pxCurrentTCB (pointer to TCB)
        "   ldr     r0, [r1]                \n"  // r0 = TCB->stack_ptr
        
        // Pop R4-R11 and EXC_RETURN (LR)
        "   ldmia   r0!, {r4-r11, lr}       \n"
        
        // Check if FPU context needs to be restored
        "   tst     lr, #0x10               \n"  // Bit 4 = 0 means FPU used
        "   it      eq                      \n"
        "   vldmiaeq r0!, {s16-s31}         \n"  // Pop S16-S31
        
        // Set PSP
        "   msr     psp, r0                 \n"
        "   isb                             \n"
        
        // Ensure interrupts are enabled
        "   mov     r0, #0                  \n"
        "   msr     basepri, r0             \n"
        
        // Return from exception - uses PSP, restores R0-R3, R12, LR, PC, xPSR
        "   bx      lr                      \n"
        
        ".align 4                           \n"
    );
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
    Stm32f4Hw::start_first_task();
}

// ============================================================================
// Fault Handlers
// ============================================================================

volatile uint32_t g_fault_cfsr = 0;
volatile uint32_t g_fault_hfsr = 0;
volatile uint32_t g_fault_bfar = 0;
volatile uint32_t g_fault_mmfar = 0;

extern "C" [[noreturn]] void HardFault_Handler() {
    g_fault_cfsr = *reinterpret_cast<volatile uint32_t*>(0xE000ED28);
    g_fault_hfsr = *reinterpret_cast<volatile uint32_t*>(0xE000ED2C);
    g_fault_bfar = *reinterpret_cast<volatile uint32_t*>(0xE000ED38);
    g_fault_mmfar = *reinterpret_cast<volatile uint32_t*>(0xE000ED34);
    gpio_d.set(14);
    while (true) { asm volatile("" ::: "memory"); }
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

extern "C" void Default_Handler() {
    gpio_d.set(14);
    while (true) { asm volatile(""); }
}

// ============================================================================
// Vector Table
// ============================================================================

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
    reinterpret_cast<const void*>(SVC_Handler),      // 11: SVCall
    nullptr, nullptr,                                // 12-13: Reserved
    reinterpret_cast<const void*>(PendSV_Handler),   // 14: PendSV
    reinterpret_cast<const void*>(SysTick_Handler),  // 15: SysTick
    // External interrupts (16+)
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    // IRQ 14: DMA1_Stream3 (PDM)
    reinterpret_cast<const void*>(DMA1_Stream3_IRQHandler),
    nullptr,
    // IRQ 16: DMA1_Stream5 (I2S)
    reinterpret_cast<const void*>(DMA1_Stream5_IRQHandler),
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr,
    // IRQ 67: OTG_FS (USB)
    reinterpret_cast<const void*>(OTG_FS_IRQHandler),
};

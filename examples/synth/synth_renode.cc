// =====================================================================
// UMI-OS Synthesizer for Renode Emulation
// =====================================================================
//
// This firmware runs on Renode with custom peripherals:
//   - I2S Audio Output (0x50000000) -> TCP:9001 -> Browser
//   - MIDI Input/Output (0x50000400) -> TCP:9002 -> Browser
//   - Control/Status (0x50000800) -> TCP:9003 -> Browser
//
// Build:
//   xmake build synth_renode
//
// Run:
//   1. python3 renode/scripts/web_bridge.py
//   2. renode --console --disable-xwt -e "i $CWD/renode/synth_audio.resc"
//   3. Open http://localhost:8088/workbench/synth_sim.html?backend=renode
//
// =====================================================================

#include "synth.hh"
#include <cstdint>

// =====================================================================
// Peripheral Registers
// =====================================================================

// I2S Audio (Python peripheral at 0x50000000)
// Uses DMA-like bulk transfer: write samples to buffer, then trigger send
#define I2S_BASE        0x50000000
#define I2S_SR          (*(volatile uint32_t*)(I2S_BASE + 0x08))
#define I2S_DR          (*(volatile uint32_t*)(I2S_BASE + 0x0C))
#define I2S_I2SCFGR     (*(volatile uint32_t*)(I2S_BASE + 0x1C))
#define I2S_I2SPR       (*(volatile uint32_t*)(I2S_BASE + 0x20))
// DMA-like registers for bulk transfer
#define I2S_DMA_ADDR    (*(volatile uint32_t*)(I2S_BASE + 0x24))  // Buffer address
#define I2S_DMA_COUNT   (*(volatile uint32_t*)(I2S_BASE + 0x28))  // Sample count
#define I2S_DMA_TRIGGER (*(volatile uint32_t*)(I2S_BASE + 0x2C))  // Write to send

#define I2S_SR_TXE      0x02    // TX buffer empty

// MIDI Peripheral (Python peripheral at 0x50000400)
#define MIDI_BASE       0x50000400
#define MIDI_SR         (*(volatile uint32_t*)(MIDI_BASE + 0x00))
#define MIDI_DR         (*(volatile uint32_t*)(MIDI_BASE + 0x04))
#define MIDI_CR         (*(volatile uint32_t*)(MIDI_BASE + 0x08))

#define MIDI_SR_RXNE    0x01    // RX not empty
#define MIDI_SR_TXE     0x02    // TX empty

// Control Peripheral (Python peripheral at 0x50000800)
#define CTRL_BASE       0x50000800
#define CTRL_CMD        (*(volatile uint32_t*)(CTRL_BASE + 0x00))
#define CTRL_STATUS     (*(volatile uint32_t*)(CTRL_BASE + 0x04))
#define CTRL_DATA0      (*(volatile uint32_t*)(CTRL_BASE + 0x08))
#define CTRL_DATA1      (*(volatile uint32_t*)(CTRL_BASE + 0x0C))
#define CTRL_DATA2      (*(volatile uint32_t*)(CTRL_BASE + 0x10))
#define CTRL_DATA3      (*(volatile uint32_t*)(CTRL_BASE + 0x14))

#define CTRL_CMD_REPORT_STATE   0x01

// =====================================================================
// Configuration
// =====================================================================

constexpr int SAMPLE_RATE = 48000;
constexpr int BUFFER_SIZE = 4096;  // Larger buffer for DMA-like transfer (~85ms at 48kHz)

// =====================================================================
// UART output (for debug messages)
// =====================================================================

extern "C" int _write(int fd, const char* buf, int len);

namespace {
void print(const char* s) {
    int len = 0;
    const char* p = s;
    while (*p++) len++;
    _write(1, s, len);
}

} // namespace

// =====================================================================
// Global State
// =====================================================================

umi::synth::PolySynth g_synth;
static float g_audio_buffer[BUFFER_SIZE];
static int16_t g_dma_buffer[BUFFER_SIZE * 2];  // Stereo interleaved
static uint32_t g_sample_count = 0;
static uint32_t g_buffer_count = 0;
static uint32_t g_uptime = 0;
static uint32_t g_midi_rx_count = 0;

// =====================================================================
// I2S Audio Output
// =====================================================================

void i2s_init() {
    // Configure I2S: Master TX, 16-bit, 48kHz
    // I2SCFGR: I2SE=1 (enable), I2SCFG=10 (master TX), DATLEN=00 (16-bit)
    I2S_I2SCFGR = 0x0600 | 0x0400;  // Master TX + Enable

    // Configure prescaler for 48kHz (approximate)
    // Assuming 84MHz I2S clock: 84MHz / (16*2 * (2*27+1)) = ~48kHz
    I2S_I2SPR = 27 | (1 << 8);  // DIV=27, ODD=1

    print("[I2S] Initialized at 48kHz\n");
}

void i2s_write_buffer_dma(const float* samples, int count) {
    // Convert float to int16 stereo interleaved in DMA buffer
    for (int i = 0; i < count; i++) {
        float clamped = samples[i];
        if (clamped > 1.0f) clamped = 1.0f;
        if (clamped < -1.0f) clamped = -1.0f;
        int16_t sample = (int16_t)(clamped * 32767.0f);
        // Stereo: L and R same
        g_dma_buffer[i * 2] = sample;
        g_dma_buffer[i * 2 + 1] = sample;
    }

    // DMA-like bulk transfer: set address, count, trigger
    // This is ONE peripheral access instead of count*2 accesses
    I2S_DMA_ADDR = (uint32_t)g_dma_buffer;
    I2S_DMA_COUNT = count * 2;  // stereo samples
    I2S_DMA_TRIGGER = 1;  // Trigger send

    g_sample_count += count;
    g_buffer_count++;
}

// =====================================================================
// MIDI Input
// =====================================================================

void midi_init() {
    MIDI_CR = 0x01;  // Enable
    print("[MIDI] Initialized\n");
}

bool midi_available() {
    return (MIDI_SR & MIDI_SR_RXNE) != 0;
}

uint8_t midi_read_byte() {
    return (uint8_t)(MIDI_DR & 0xFF);
}

void process_midi() {
    static uint8_t midi_state = 0;
    static uint8_t midi_cmd = 0;
    static uint8_t midi_data1 = 0;

    while (midi_available()) {
        uint8_t byte = midi_read_byte();
        g_midi_rx_count++;

        if (byte & 0x80) {
            // Status byte
            midi_cmd = byte;
            midi_state = 1;
        } else if (midi_state == 1) {
            // First data byte
            midi_data1 = byte;
            midi_state = 2;
        } else if (midi_state == 2) {
            // Second data byte - process message
            uint8_t cmd = midi_cmd & 0xF0;
            // uint8_t channel = midi_cmd & 0x0F;

            if (cmd == 0x90 && byte > 0) {
                // Note On
                g_synth.note_on(midi_data1, byte);
            } else if (cmd == 0x80 || (cmd == 0x90 && byte == 0)) {
                // Note Off
                g_synth.note_off(midi_data1);
            } else if (cmd == 0xB0) {
                // Control Change
                // Could handle CC messages here
            }

            midi_state = 1;  // Ready for next data pair
        }
    }
}

// =====================================================================
// Control/Status Reporting
// =====================================================================

void ctrl_init() {
    print("[CTRL] Initialized\n");
}

void ctrl_report_state() {
    // Pack state into data registers
    CTRL_DATA0 = g_uptime;
    CTRL_DATA1 = 50 * 100;  // 50% CPU load (dummy)
    CTRL_DATA2 = (4 << 16) | (2 << 8) | 2;  // 4 tasks, 2 ready, 2 blocked
    CTRL_DATA3 = 8192;  // Heap used

    // Trigger state report
    CTRL_CMD = CTRL_CMD_REPORT_STATE;
}

// =====================================================================
// Simple delay (cycle-based)
// =====================================================================

volatile uint32_t* const DWT_CYCCNT = (volatile uint32_t*)0xE0001004;
volatile uint32_t* const DWT_CTRL = (volatile uint32_t*)0xE0001000;
volatile uint32_t* const SCB_DEMCR = (volatile uint32_t*)0xE000EDFC;

void dwt_init() {
    *SCB_DEMCR |= (1 << 24);  // Enable DWT
    *DWT_CYCCNT = 0;
    *DWT_CTRL |= 1;  // Enable cycle counter
}

void delay_cycles(uint32_t cycles) {
    uint32_t start = *DWT_CYCCNT;
    while ((*DWT_CYCCNT - start) < cycles) {}
}

void delay_us(uint32_t us) {
    // Assuming 168 MHz clock
    delay_cycles(us * 168);
}

void delay_ms(uint32_t ms) {
    // Assuming 168 MHz clock
    delay_cycles(ms * 168000);
}

// =====================================================================
// Main Loop
// =====================================================================

extern "C" int main() {
    print("\n");
    print("========================================\n");
    print("UMI-OS Synth for Renode\n");
    print("========================================\n");

    // Initialize DWT for timing
    dwt_init();
    print("[DWT] Cycle counter enabled\n");

    // Initialize synth engine
    g_synth.init(static_cast<float>(SAMPLE_RATE));
    print("[SYNTH] Initialized at 48kHz, 4 voices\n");

    // Initialize peripherals
    i2s_init();
    midi_init();
    ctrl_init();

    print("\n");
    print("Entering main loop...\n");
    print("Waiting for MIDI input from browser.\n");
    print("========================================\n");
    print("\n");

    // Main audio loop
    uint32_t report_counter = 0;
    float phase = 0.0f;
    const float freq = 440.0f;
    const float phase_inc = freq / static_cast<float>(SAMPLE_RATE);

    while (true) {
        // Process MIDI input
        process_midi();

#if 0
        // Full synth processing (disabled for speed test)
        g_synth.process(g_audio_buffer, BUFFER_SIZE);
#else
        // Minimal test: simple sine wave (no DSP overhead)
        for (int i = 0; i < BUFFER_SIZE; i++) {
            // Simple sine approximation (parabolic, very fast)
            float p = phase * 2.0f - 1.0f;  // -1 to 1
            g_audio_buffer[i] = 4.0f * p * (1.0f - (p < 0 ? -p : p)) * 0.3f;
            phase += phase_inc;
            if (phase >= 1.0f) phase -= 1.0f;
        }
#endif

        // Output to I2S via DMA-like bulk transfer
        // Real-time pacing is handled by the C# I2SAudioBridge peripheral
        i2s_write_buffer_dma(g_audio_buffer, BUFFER_SIZE);

        // Status report disabled for audio testing
        // (ctrl_report_state causes periodic delays)
        (void)report_counter;
    }

    return 0;
}

// =====================================================================
// ARM Cortex-M Vector Table and Handlers
// =====================================================================

#if defined(__arm__) || defined(__thumb__)

extern "C" {
    extern uint32_t _estack;
    void Reset_Handler();
    void NMI_Handler();
    void HardFault_Handler();
    void MemManage_Handler();
    void BusFault_Handler();
    void UsageFault_Handler();

    __attribute__((section(".isr_vector"), used))
    const void* vector_table[] = {
        &_estack,
        reinterpret_cast<void*>(Reset_Handler),
        reinterpret_cast<void*>(NMI_Handler),
        reinterpret_cast<void*>(HardFault_Handler),
        reinterpret_cast<void*>(MemManage_Handler),
        reinterpret_cast<void*>(BusFault_Handler),
        reinterpret_cast<void*>(UsageFault_Handler),
    };

    void Reset_Handler() {
        // Enable FPU
        volatile uint32_t* CPACR = (volatile uint32_t*)0xE000ED88;
        *CPACR |= (0xF << 20);
        __asm volatile("dsb");
        __asm volatile("isb");

        // Initialize .data and .bss
        extern uint32_t _sdata, _edata, _sidata;
        extern uint32_t _sbss, _ebss;

        uint32_t* src = &_sidata;
        uint32_t* dst = &_sdata;
        while (dst < &_edata) *dst++ = *src++;

        dst = &_sbss;
        while (dst < &_ebss) *dst++ = 0;

        main();
        while (true) {}
    }

    void NMI_Handler() { print("!!! NMI !!!\n"); while (true) {} }
    void HardFault_Handler() { print("!!! HardFault !!!\n"); while (true) {} }
    void MemManage_Handler() { print("!!! MemManage !!!\n"); while (true) {} }
    void BusFault_Handler() { print("!!! BusFault !!!\n"); while (true) {} }
    void UsageFault_Handler() { print("!!! UsageFault !!!\n"); while (true) {} }
}

#endif

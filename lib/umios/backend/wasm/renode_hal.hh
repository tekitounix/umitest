// SPDX-License-Identifier: MIT
// =====================================================================
// UMI-OS Renode Hardware Abstraction Layer
// =====================================================================
//
// Implements umi::Hw<Impl> interface for Renode emulation environment.
// Uses custom Python peripherals that bridge to web_bridge.py via TCP.
//
// Peripherals (using free address space at 0x50000xxx):
//   - I2S (0x50000000): Audio output -> TCP:9001 -> Browser
//   - MIDI (0x50000400): MIDI I/O -> TCP:9002 -> Browser
//   - Control (0x50000800): Status -> TCP:9003 -> Browser
//
// Usage:
//   1. python3 renode/scripts/web_bridge.py
//   2. renode --console --disable-xwt -e "i $CWD/renode/synth_audio.resc"
//   3. Open http://localhost:8088/workbench/synth_sim.html?backend=renode
//
// =====================================================================

#pragma once

#include <cstdint>
#include <cstddef>

namespace umi::renode {

// =====================================================================
// Peripheral Register Definitions
// =====================================================================

// I2S Audio (at SPI2 base address)
struct I2SRegs {
    volatile uint32_t CR1;      // 0x00: Control register 1
    volatile uint32_t CR2;      // 0x04: Control register 2
    volatile uint32_t SR;       // 0x08: Status register
    volatile uint32_t DR;       // 0x0C: Data register
    volatile uint32_t CRCPR;    // 0x10: CRC polynomial
    volatile uint32_t RXCRCR;   // 0x14: RX CRC
    volatile uint32_t TXCRCR;   // 0x18: TX CRC
    volatile uint32_t I2SCFGR;  // 0x1C: I2S configuration
    volatile uint32_t I2SPR;    // 0x20: I2S prescaler
};

static inline I2SRegs* const I2S = reinterpret_cast<I2SRegs*>(0x50000000);

constexpr uint32_t I2S_SR_TXE = 0x02;      // TX buffer empty
constexpr uint32_t I2S_I2SCFGR_I2SE = 0x0400;  // I2S enable

// MIDI Peripheral
struct MIDIRegs {
    volatile uint32_t SR;       // 0x00: Status register
    volatile uint32_t DR;       // 0x04: Data register
    volatile uint32_t CR;       // 0x08: Control register
};

static inline MIDIRegs* const MIDI = reinterpret_cast<MIDIRegs*>(0x50000400);

constexpr uint32_t MIDI_SR_RXNE = 0x01;    // RX not empty
constexpr uint32_t MIDI_SR_TXE = 0x02;     // TX empty
constexpr uint32_t MIDI_CR_EN = 0x01;      // Enable

// Control/Status Peripheral
struct CtrlRegs {
    volatile uint32_t CMD;      // 0x00: Command register
    volatile uint32_t STATUS;   // 0x04: Status register
    volatile uint32_t DATA0;    // 0x08: Data register 0
    volatile uint32_t DATA1;    // 0x0C: Data register 1
    volatile uint32_t DATA2;    // 0x10: Data register 2
    volatile uint32_t DATA3;    // 0x14: Data register 3
};

static inline CtrlRegs* const CTRL = reinterpret_cast<CtrlRegs*>(0x50000800);

constexpr uint32_t CTRL_CMD_REPORT_STATE = 0x01;
constexpr uint32_t CTRL_STATUS_CONNECTED = 0x01;
constexpr uint32_t CTRL_STATUS_CMD_PENDING = 0x02;

// DWT (Data Watchpoint and Trace) for cycle counting
static inline volatile uint32_t* const DWT_CTRL = reinterpret_cast<volatile uint32_t*>(0xE0001000);
static inline volatile uint32_t* const DWT_CYCCNT = reinterpret_cast<volatile uint32_t*>(0xE0001004);
static inline volatile uint32_t* const SCB_DEMCR = reinterpret_cast<volatile uint32_t*>(0xE000EDFC);

// =====================================================================
// Audio I/O Interface
// =====================================================================

class AudioIO {
public:
    static void init(uint32_t sample_rate = 48000) {
        sample_rate_ = sample_rate;

        // Configure I2S prescaler for sample rate
        // Assuming 84MHz I2S clock: DIV = I2SxCLK / (Fs * 16 * 2 * 2)
        uint32_t div = 84000000 / (sample_rate * 64);
        I2S->I2SPR = div | (1 << 8);  // DIV + ODD bit

        // Enable I2S: Master TX, 16-bit
        I2S->I2SCFGR = 0x0600 | I2S_I2SCFGR_I2SE;  // Master TX + Enable
    }

    static void write_sample(int16_t left, int16_t right) {
        // Wait for TX ready and write left
        while (!(I2S->SR & I2S_SR_TXE)) {}
        I2S->DR = static_cast<uint16_t>(left);

        // Wait for TX ready and write right
        while (!(I2S->SR & I2S_SR_TXE)) {}
        I2S->DR = static_cast<uint16_t>(right);

        sample_count_++;
    }

    static void write_buffer(const float* samples, uint32_t count) {
        for (uint32_t i = 0; i < count; i++) {
            float sample = samples[i];
            if (sample > 1.0f) sample = 1.0f;
            if (sample < -1.0f) sample = -1.0f;
            int16_t s16 = static_cast<int16_t>(sample * 32767.0f);
            write_sample(s16, s16);  // Mono to stereo
        }
        buffer_count_++;
    }

    static uint32_t sample_rate() { return sample_rate_; }
    static uint32_t sample_count() { return sample_count_; }
    static uint32_t buffer_count() { return buffer_count_; }

private:
    static inline uint32_t sample_rate_ = 48000;
    static inline uint32_t sample_count_ = 0;
    static inline uint32_t buffer_count_ = 0;
};

// =====================================================================
// MIDI I/O Interface
// =====================================================================

class MidiIO {
public:
    static void init() {
        MIDI->CR = MIDI_CR_EN;
    }

    static bool available() {
        return (MIDI->SR & MIDI_SR_RXNE) != 0;
    }

    static uint8_t read_byte() {
        return static_cast<uint8_t>(MIDI->DR & 0xFF);
    }

    static void write_byte(uint8_t byte) {
        while (!(MIDI->SR & MIDI_SR_TXE)) {}
        MIDI->DR = byte;
    }

    static void write_message(uint8_t status, uint8_t data1, uint8_t data2) {
        write_byte(status);
        write_byte(data1);
        write_byte(data2);
    }

    static uint32_t rx_count() { return rx_count_; }
    static uint32_t tx_count() { return tx_count_; }

private:
    static inline uint32_t rx_count_ = 0;
    static inline uint32_t tx_count_ = 0;
};

// =====================================================================
// Renode Hardware Implementation
// =====================================================================

struct RenodeHwImpl {
    // =========================================================================
    // Timer Operations
    // =========================================================================

    static void set_timer_absolute(std::uint64_t target_us) {
        next_timer_target_ = target_us;
    }

    static std::uint64_t monotonic_time_usecs() {
        // Use DWT cycle counter if enabled
        if (*DWT_CTRL & 1) {
            return static_cast<uint64_t>(*DWT_CYCCNT) / cpu_freq_mhz_;
        }
        return time_us_;
    }

    static bool timer_expired() {
        return monotonic_time_usecs() >= next_timer_target_;
    }

    static std::uint64_t next_timer_target() {
        return next_timer_target_;
    }

    // =========================================================================
    // Critical Sections (real interrupts disabled)
    // =========================================================================

    static void enter_critical() {
        asm volatile("cpsid i" ::: "memory");
        in_critical_ = true;
    }

    static void exit_critical() {
        in_critical_ = false;
        asm volatile("cpsie i" ::: "memory");
    }

    static bool in_critical() {
        return in_critical_;
    }

    // =========================================================================
    // Multi-Core (STM32F4 is single-core)
    // =========================================================================

    static void trigger_ipi(std::uint8_t) {}
    static std::uint8_t current_core() { return 0; }

    // =========================================================================
    // Context Switch
    // =========================================================================

    static void request_context_switch() {
        context_switch_pending_ = true;
        // Trigger PendSV if not in ISR
        volatile uint32_t* ICSR = reinterpret_cast<volatile uint32_t*>(0xE000ED04);
        *ICSR = (1 << 28);  // Set PENDSVSET
    }

    static bool context_switch_pending() {
        return context_switch_pending_;
    }

    static void clear_context_switch() {
        context_switch_pending_ = false;
    }

    // =========================================================================
    // FPU Context
    // =========================================================================

    static void save_fpu() {
        // Cortex-M4 lazy stacking handles this automatically
    }

    static void restore_fpu() {
        // Cortex-M4 lazy stacking handles this automatically
    }

    // =========================================================================
    // Audio DMA
    // =========================================================================

    static void mute_audio_dma() { audio_muted_ = true; }
    static void unmute_audio_dma() { audio_muted_ = false; }
    static bool is_audio_muted() { return audio_muted_; }

    // =========================================================================
    // Persistent Storage (not implemented for Renode)
    // =========================================================================

    using StorageWriteFn = void (*)(const void*, std::size_t);
    using StorageReadFn = void (*)(void*, std::size_t);

    static void set_storage_callbacks(StorageWriteFn, StorageReadFn) {}
    static void write_backup_ram(const void*, std::size_t) {}
    static void read_backup_ram(void*, std::size_t) {}

    // =========================================================================
    // MPU (not used in Renode)
    // =========================================================================

    static void configure_mpu_region(std::size_t, const void*,
                                     std::size_t, bool, bool) {}

    // =========================================================================
    // Cache (STM32F4 has no cache)
    // =========================================================================

    static void cache_clean(const void*, std::size_t) {}
    static void cache_invalidate(void*, std::size_t) {}
    static void cache_clean_invalidate(void*, std::size_t) {}

    // =========================================================================
    // System Operations
    // =========================================================================

    [[noreturn]] static void system_reset() {
        // AIRCR software reset
        volatile uint32_t* AIRCR = reinterpret_cast<volatile uint32_t*>(0xE000ED0C);
        *AIRCR = 0x05FA0004;
        while (true) {}
    }

    static void enter_sleep() {
        asm volatile("wfi");
    }

    [[noreturn]] static void start_first_task() {
        // Would normally set up PSP and switch to unprivileged
        while (true) {
            asm volatile("wfi");
        }
    }

    // =========================================================================
    // Watchdog (software watchdog for Renode)
    // =========================================================================

    static void watchdog_init(std::uint32_t timeout_ms) {
        watchdog_timeout_ms_ = timeout_ms;
        watchdog_enabled_ = (timeout_ms > 0);
        watchdog_last_feed_us_ = monotonic_time_usecs();
    }

    static void watchdog_feed() {
        watchdog_last_feed_us_ = monotonic_time_usecs();
    }

    static bool watchdog_expired() {
        if (!watchdog_enabled_) return false;
        uint64_t elapsed = monotonic_time_usecs() - watchdog_last_feed_us_;
        return elapsed > (watchdog_timeout_ms_ * 1000ULL);
    }

    static bool watchdog_enabled() { return watchdog_enabled_; }
    static uint32_t watchdog_timeout_ms() { return watchdog_timeout_ms_; }

    // =========================================================================
    // Performance Counters
    // =========================================================================

    static std::uint32_t cycle_count() {
        return *DWT_CYCCNT;
    }

    static std::uint32_t cycles_per_usec() {
        return cpu_freq_mhz_;
    }

    // =========================================================================
    // Initialization
    // =========================================================================

    static void init() {
        // Enable DWT cycle counter
        *SCB_DEMCR |= (1 << 24);
        *DWT_CYCCNT = 0;
        *DWT_CTRL |= 1;

        // Initialize peripherals
        AudioIO::init();
        MidiIO::init();
    }

    // =========================================================================
    // Renode-Specific: Status Reporting
    // =========================================================================

    static void report_state(uint32_t uptime, uint32_t cpu_load,
                             uint32_t task_count, uint32_t task_ready,
                             uint32_t task_blocked, uint32_t heap_used) {
        CTRL->DATA0 = uptime;
        CTRL->DATA1 = cpu_load;
        CTRL->DATA2 = (task_count << 16) | (task_ready << 8) | task_blocked;
        CTRL->DATA3 = heap_used;
        CTRL->CMD = CTRL_CMD_REPORT_STATE;
    }

private:
    static inline std::uint64_t time_us_ = 0;
    static inline std::uint64_t next_timer_target_ = UINT64_MAX;
    static inline bool in_critical_ = false;
    static inline bool context_switch_pending_ = false;
    static inline bool audio_muted_ = false;
    static inline uint32_t watchdog_timeout_ms_ = 0;
    static inline uint64_t watchdog_last_feed_us_ = 0;
    static inline bool watchdog_enabled_ = false;
    static inline uint32_t cpu_freq_mhz_ = 168;
};

} // namespace umi::renode

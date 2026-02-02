// SPDX-License-Identifier: MIT
// Daisy Pod Kernel - Phase 6: Audio + RTOS + USB Audio/MIDI + Synth + HID Events
// 4-task architecture: Audio (REALTIME), System (SERVER), Control (USER), Idle
#include <cstdint>
#include <cstring>
#include <atomic>
#include <common/irq.hh>

#include <arch/cache.hh>
#include <arch/context.hh>
#include <arch/handlers.hh>
#include <arch/switch.hh>
#include <board/mcu_init.hh>
#include <board/audio.hh>
#include <board/usb.hh>
#include <board/sdram.hh>
#include <board/qspi.hh>

// Pod HID
#include <board/hid.hh>

// umiusb: USB Audio + MIDI
#include <hal/stm32_otg.hh>
#include <audio/audio_interface.hh>
#include <core/device.hh>
#include <core/descriptor.hh>

// Linker-provided symbols
extern "C" {
extern std::uint32_t _estack;
extern std::uint32_t _sidata;
extern std::uint32_t _sdata;
extern std::uint32_t _edata;
extern std::uint32_t _sbss;
extern std::uint32_t _ebss;
extern std::uint32_t _sdtcm_bss;
extern std::uint32_t _edtcm_bss;
}

// ============================================================================
// Task definitions
// ============================================================================

namespace {

using namespace umi::daisy;
using TaskContext = umi::port::cm7::TaskContext;

// Task IDs
enum TaskId : std::uint8_t {
    TASK_AUDIO   = 0,  // Priority: REALTIME
    TASK_SYSTEM  = 1,  // Priority: SERVER
    TASK_CONTROL = 2,  // Priority: USER
    TASK_IDLE    = 3,
    NUM_TASKS    = 4,
};

// Task stacks (in words)
constexpr std::uint32_t AUDIO_STACK_SIZE   = 512;
constexpr std::uint32_t SYSTEM_STACK_SIZE  = 256;
constexpr std::uint32_t CONTROL_STACK_SIZE = 256;
constexpr std::uint32_t IDLE_STACK_SIZE    = 64;

std::uint32_t audio_stack[AUDIO_STACK_SIZE];
std::uint32_t system_stack[SYSTEM_STACK_SIZE];
std::uint32_t control_stack[CONTROL_STACK_SIZE];
std::uint32_t idle_stack[IDLE_STACK_SIZE];

TaskContext task_contexts[NUM_TASKS];

// Minimal scheduler state
volatile TaskId current_task = TASK_IDLE;

// Task notification bits (set by ISR, cleared by task)
std::atomic<std::uint32_t> task_notifications[NUM_TASKS] = {};

// Notification bits
constexpr std::uint32_t NOTIFY_AUDIO_READY = (1U << 0);
[[maybe_unused]] constexpr std::uint32_t NOTIFY_USB_IRQ = (1U << 1);

// ============================================================================
// USB MIDI
// ============================================================================

// USB device uses HS peripheral with internal FS PHY (Stm32HsHal)
umiusb::Stm32HsHal usb_hal;
using UsbAudioMidi = umiusb::AudioFullDuplexMidi48k;
UsbAudioMidi usb_audio;

constexpr umiusb::DeviceInfo usb_device_info = {
    .vendor_id = 0x1209,       // pid.codes test VID
    .product_id = 0x000B,      // UMI Audio+MIDI
    .device_version = 0x0100,
    .manufacturer_idx = 1,
    .product_idx = 2,
    .serial_idx = 0,
};

inline constexpr auto str_mfr = umiusb::StringDesc("UMI");
inline constexpr auto str_prod = umiusb::StringDesc("Daisy Pod Audio");

[[maybe_unused]] inline const std::array<std::span<const uint8_t>, 2> usb_strings = {
    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&str_mfr), str_mfr.size()),
    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&str_prod), str_prod.size()),
};

umiusb::Device<umiusb::Stm32HsHal, UsbAudioMidi> usb_device(usb_hal, usb_audio, usb_device_info);

// ============================================================================
// Pod HID
// ============================================================================

umi::daisy::pod::PodHid pod_hid;

// Debug: SDRAM/QSPI test results (read via GDB)
struct DbgMemTest {
    volatile std::uint32_t sdram_result = 0;  // 0=untested, 1=pass, 2=fail
    volatile std::uint32_t sdram_read = 0;
    volatile std::uint32_t qspi_byte0 = 0;
} dbg_mem;

// ============================================================================
// DMA audio buffers
// ============================================================================

__attribute__((section(".dma_buffer")))
std::int32_t audio_tx_buf[AUDIO_BUFFER_SIZE];

__attribute__((section(".dma_buffer")))
std::int32_t audio_rx_buf[AUDIO_BUFFER_SIZE];

// Pending audio buffer pointers (set by ISR, consumed by audio task)
volatile std::int32_t* pending_tx = nullptr;
volatile std::int32_t* pending_rx = nullptr;

// ============================================================================
// Simple Event Queue (HID → audio/control event bridge)
// ============================================================================

enum class EventType : std::uint8_t {
    NONE = 0,
    NOTE_ON,
    NOTE_OFF,
    CC,
    KNOB,
    BUTTON_DOWN,
    BUTTON_UP,
    ENCODER_INCREMENT,
};

struct Event {
    EventType type = EventType::NONE;
    std::uint8_t channel = 0;
    std::uint8_t param = 0;    // note number or CC number or knob index
    std::uint8_t value = 0;    // velocity or CC value
};

// Lock-free SPSC ring buffer for events (ISR/callback → audio task)
struct EventQueue {
    static constexpr std::uint32_t CAPACITY = 64;
    Event events[CAPACITY] = {};
    std::atomic<std::uint32_t> head{0};
    std::atomic<std::uint32_t> tail{0};

    bool push(const Event& e) {
        auto h = head.load(std::memory_order_relaxed);
        auto next = (h + 1) % CAPACITY;
        if (next == tail.load(std::memory_order_acquire)) return false;  // full
        events[h] = e;
        head.store(next, std::memory_order_release);
        return true;
    }

    bool pop(Event& e) {
        auto t = tail.load(std::memory_order_relaxed);
        if (t == head.load(std::memory_order_acquire)) return false;  // empty
        e = events[t];
        tail.store((t + 1) % CAPACITY, std::memory_order_release);
        return true;
    }
};

EventQueue midi_event_queue;

// ============================================================================
// Minimal Polyphonic Synth (8 voices, saw oscillator)
// ============================================================================

struct Voice {
    std::uint32_t phase = 0;
    std::uint32_t phase_inc = 0;
    std::int32_t amplitude = 0;  // 0 = off, 24-bit scale
    std::uint8_t note = 0;
    bool active = false;

    // Simple AR envelope state
    std::int32_t env_level = 0;     // current level (24-bit)
    std::int32_t env_target = 0;    // target level
    std::int32_t env_rate = 0;      // increment per sample (positive=attack, negative=release)
};

constexpr int NUM_VOICES = 8;
Voice voices[NUM_VOICES];

// MIDI note → phase increment (for 48kHz, 32-bit phase accumulator)
// phase_inc = (freq * 2^32) / 48000
constexpr std::uint32_t midi_note_to_phase_inc(std::uint8_t note) {
    // A4 (69) = 440 Hz → phase_inc = 440 * 4294967296 / 48000 = 39370534
    // Each semitone: multiply by 2^(1/12) ≈ 1.05946
    // Pre-compute using integer approximation
    constexpr std::uint32_t a4_inc = 39370534U;
    if (note == 69) return a4_inc;
    // Use lookup or compute: inc = a4_inc * 2^((note-69)/12)
    // Simple shift-based approximation for real-time:
    // We'll use a table for the 12 semitones within an octave
    constexpr std::uint32_t semitone_ratio[12] = {
        // Ratios * 65536 for notes 0..11 relative to C
        // C=65536, C#=69433, D=73562, D#=77936, E=82570, F=87480,
        // F#=92682, G=98193, G#=104032, A=110218, A#=116772, B=123715
        65536, 69433, 73562, 77936, 82570, 87480,
        92682, 98193, 104032, 110218, 116772, 123715,
    };
    // C0 = note 12, A4 = note 69
    // C in octave: note % 12, octave = note / 12
    int semitone = note % 12;
    int octave = note / 12;
    // Base: C0 phase_inc = a4_inc / 2^(69/12 - 0) ... too complex
    // Simpler: A in octave 'oct' = a4_inc * 2^(oct - 5) (A4 is octave 5 in MIDI note/12 scheme, 69/12=5)
    // note 69 = octave 5, semitone 9 (A)
    // For any note: inc = (a4_inc * semitone_ratio[semitone]) / semitone_ratio[9]
    // Then shift by (octave - 5)
    std::uint64_t inc = static_cast<std::uint64_t>(a4_inc) * semitone_ratio[semitone] / semitone_ratio[9];
    int shift = octave - 5;
    if (shift > 0) inc <<= shift;
    else if (shift < 0) inc >>= (-shift);
    return static_cast<std::uint32_t>(inc);
}

// Synth parameters (controlled by knobs)
volatile float synth_volume = 0.5f;     // knob1: master volume

// MIDI callback — called from USB poll context
void on_midi_message(std::uint8_t /*cable*/, const std::uint8_t* data, std::uint8_t len) {
    if (len < 2) return;
    std::uint8_t status = data[0] & 0xF0;
    std::uint8_t ch = data[0] & 0x0F;

    Event ev;
    ev.channel = ch;

    if (status == 0x90 && len >= 3 && data[2] > 0) {
        ev.type = EventType::NOTE_ON;
        ev.param = data[1];
        ev.value = data[2];
        midi_event_queue.push(ev);
    } else if (status == 0x80 || (status == 0x90 && len >= 3 && data[2] == 0)) {
        ev.type = EventType::NOTE_OFF;
        ev.param = data[1];
        ev.value = 0;
        midi_event_queue.push(ev);
    } else if (status == 0xB0 && len >= 3) {
        ev.type = EventType::CC;
        ev.param = data[1];
        ev.value = data[2];
        midi_event_queue.push(ev);
    }
}

// Process MIDI events in audio task (voice allocation)
void process_midi_events() {
    Event ev;
    while (midi_event_queue.pop(ev)) {
        if (ev.type == EventType::NOTE_ON) {
            // Find free voice or steal oldest
            Voice* target = nullptr;
            for (auto& v : voices) {
                if (!v.active) { target = &v; break; }
            }
            if (target == nullptr) target = &voices[0];  // simple steal

            target->note = ev.param;
            target->phase = 0;
            target->phase_inc = midi_note_to_phase_inc(ev.param);
            target->amplitude = static_cast<std::int32_t>(ev.value) << 16;  // velocity → 24-bit
            target->active = true;
            target->env_level = 0;
            target->env_target = target->amplitude;
            target->env_rate = target->amplitude / 48;  // ~1ms attack at 48kHz
        } else if (ev.type == EventType::NOTE_OFF) {
            for (auto& v : voices) {
                if (v.active && v.note == ev.param) {
                    v.env_target = 0;
                    v.env_rate = -(v.env_level / 480);  // ~10ms release
                    if (v.env_rate == 0) v.env_rate = -1;
                    break;
                }
            }
        }
    }
}

// Render synth voices into buffer (interleaved stereo, 24-bit in 32-bit)
void render_synth(std::int32_t* out, std::uint32_t frames) {
    // Clear buffer
    for (std::uint32_t i = 0; i < frames * 2; ++i) {
        out[i] = 0;
    }

    float vol = synth_volume;

    for (auto& v : voices) {
        if (!v.active) continue;

        for (std::uint32_t i = 0; i < frames; ++i) {
            // Update envelope
            if (v.env_rate > 0 && v.env_level < v.env_target) {
                v.env_level += v.env_rate;
                if (v.env_level > v.env_target) v.env_level = v.env_target;
            } else if (v.env_rate < 0) {
                v.env_level += v.env_rate;
                if (v.env_level <= 0) {
                    v.env_level = 0;
                    v.active = false;
                    break;
                }
            }

            // Saw wave: phase is 0..2^32, map to -2^23..+2^23
            std::int32_t saw = static_cast<std::int32_t>(v.phase >> 8) - (1 << 23);

            // Apply envelope and volume
            std::int32_t sample = static_cast<std::int32_t>(
                (static_cast<std::int64_t>(saw) * v.env_level >> 24) * static_cast<std::int64_t>(vol * 256) >> 8
            );

            out[i * 2] += sample;
            out[i * 2 + 1] += sample;
            v.phase += v.phase_inc;
        }
    }
}

// ============================================================================
// Task entry functions
// ============================================================================

/// Audio task: waits for DMA notification, bridges USB Audio ↔ SAI DMA + synth
void audio_task_entry(void* /*arg*/) {
    while (true) {
        while (!(task_notifications[TASK_AUDIO].load(std::memory_order_acquire) & NOTIFY_AUDIO_READY)) {
            asm volatile("wfe");
        }
        task_notifications[TASK_AUDIO].fetch_and(~NOTIFY_AUDIO_READY, std::memory_order_release);

        // Process pending MIDI events into voice state
        process_midi_events();

        auto* out = const_cast<std::int32_t*>(pending_tx);
        auto* in = const_cast<std::int32_t*>(pending_rx);

        if (out != nullptr) {
            // USB Audio OUT → SAI TX (DAC): read from USB host ringbuffer
            auto frames = usb_audio.read_audio(out, AUDIO_BLOCK_SIZE);
            if (frames == 0) {
                // No USB audio playback: use synth output or passthrough
                bool has_active_voice = false;
                for (const auto& v : voices) {
                    if (v.active) { has_active_voice = true; break; }
                }

                if (has_active_voice) {
                    // Render MIDI synth
                    render_synth(out, AUDIO_BLOCK_SIZE);
                } else if (in != nullptr) {
                    // Passthrough: copy ADC input → DAC output
                    for (std::uint32_t i = 0; i < AUDIO_BLOCK_SIZE * 2; ++i) {
                        out[i] = in[i];
                    }
                } else {
                    // Silence
                    for (std::uint32_t i = 0; i < AUDIO_BLOCK_SIZE * 2; ++i) {
                        out[i] = 0;
                    }
                }
            }
        }

        if (in != nullptr) {
            // SAI RX (ADC) → USB Audio IN: write to USB host ringbuffer
            usb_audio.write_audio_in(in, AUDIO_BLOCK_SIZE);
        }
    }
}

/// System task: USB polling
void system_task_entry(void*) {
    while (true) {
        usb_device.poll();
        asm volatile("wfe");
    }
}

/// Control task: HID polling + USB polling + knob→synth parameter mapping
void control_task_entry(void* /*arg*/) {
    mm::DirectTransportT<> transport;
    constexpr float hid_rate = 1000.0f;  // ~1 kHz update rate
    std::uint32_t loop_counter = 0;

    while (true) {
        usb_device.poll();

        // Update digital controls + LED PWM at ~1 kHz
        if (++loop_counter >= 100) {
            loop_counter = 0;
            pod_hid.update_controls(transport, hid_rate);

            // Knob1 → synth master volume
            synth_volume = pod_hid.knobs.value(0);
            // Knob2 → LED2 blue (visual feedback)
            pod_hid.led1.set(synth_volume, 0.0f, 0.0f);
            pod_hid.led2.set(0.0f, 0.0f, pod_hid.knobs.value(1));

            // Encoder click → toggle Seed board LED
            if (pod_hid.encoder.click_just_pressed()) {
                umi::daisy::toggle_led();
            }

            // Push HID events to event queue
            if (pod_hid.encoder.click_just_pressed()) {
                midi_event_queue.push({EventType::BUTTON_DOWN, 0, 0, 127});
            }
            if (pod_hid.encoder.increment() != 0) {
                midi_event_queue.push({EventType::ENCODER_INCREMENT, 0, 0,
                    static_cast<std::uint8_t>(pod_hid.encoder.increment() > 0 ? 1 : 0xFF)});
            }
        }

        // Update knob filter at full loop rate
        pod_hid.process_knobs();
    }
}

/// Idle task: low power wait
void idle_task_entry(void*) {
    while (true) {
        asm volatile("wfi");
    }
}

// ============================================================================
// Scheduler
// ============================================================================

/// Simple priority scheduler: pick highest priority ready task
TaskId schedule() {
    // Audio task has highest priority
    if (task_notifications[TASK_AUDIO].load(std::memory_order_relaxed) & NOTIFY_AUDIO_READY) {
        return TASK_AUDIO;
    }
    // Control task is always ready (running LED loop)
    return TASK_CONTROL;
}

} // namespace

// ============================================================================
// RTOS linkage symbols (required by handlers.cc)
// ============================================================================

extern "C" __attribute__((used))
umi::port::cm7::TaskContext* volatile umi_cm7_current_tcb = nullptr;

extern "C" __attribute__((used)) void umi_cm7_switch_context() {
    auto next = schedule();
    current_task = next;
    umi_cm7_current_tcb = &task_contexts[next];
}

// ============================================================================
// DMA IRQ handlers
// ============================================================================

extern "C" {

void DMA1_Stream0_IRQHandler() {
    using namespace umi::stm32h7;
    mm::DirectTransportT<> transport;
    auto lisr = transport.read(DMA1::LISR{});

    if (lisr & dma_flags::S0_HTIF) {
        transport.write(DMA1::LIFCR::value(dma_flags::S0_HTIF));
        pending_tx = audio_tx_buf;
        pending_rx = audio_rx_buf;
        task_notifications[TASK_AUDIO].fetch_or(NOTIFY_AUDIO_READY, std::memory_order_release);
        umi::kernel::port::cm7::request_context_switch();
    }
    if (lisr & dma_flags::S0_TCIF) {
        transport.write(DMA1::LIFCR::value(dma_flags::S0_TCIF));
        pending_tx = audio_tx_buf + AUDIO_BUFFER_SIZE / 2;
        pending_rx = audio_rx_buf + AUDIO_BUFFER_SIZE / 2;
        task_notifications[TASK_AUDIO].fetch_or(NOTIFY_AUDIO_READY, std::memory_order_release);
        umi::kernel::port::cm7::request_context_switch();
    }
    transport.write(DMA1::LIFCR::value(lisr & dma_flags::S0_ALL));
}

void DMA1_Stream1_IRQHandler() {
    using namespace umi::stm32h7;
    mm::DirectTransportT<> transport;
    transport.write(DMA1::LIFCR::value(dma_flags::S1_ALL));
}

} // extern "C"

// Fault handlers
extern "C" {
void HardFault_Handler() {
    umi::daisy::set_led(true);
    while (true) {}
}
void MemManage_Handler() {
    umi::daisy::set_led(true);
    while (true) {}
}
void BusFault_Handler() {
    umi::daisy::set_led(true);
    while (true) {}
}
void UsageFault_Handler() {
    umi::daisy::set_led(true);
    while (true) {}
}
}

// ============================================================================
// main() — called from Reset_Handler, sets up tasks and starts scheduler
// ============================================================================

int main() {
    // Initialize clocks
    umi::daisy::init_clocks();
    umi::daisy::init_pll3();
    umi::daisy::init_led();

    // External memory
    umi::daisy::init_sdram();
    umi::daisy::init_qspi();

    // SDRAM verification
    {
        auto* sdram = reinterpret_cast<volatile std::uint32_t*>(0xC000'0000);
        sdram[0] = 0xDEAD'BEEF;
        asm volatile("dsb sy" ::: "memory");
        dbg_mem.sdram_read = sdram[0];
        dbg_mem.sdram_result = (dbg_mem.sdram_read == 0xDEAD'BEEF) ? 1 : 2;
    }

    // QSPI XIP verification
    {
        auto* qspi = reinterpret_cast<volatile std::uint8_t*>(0x9000'0000);
        dbg_mem.qspi_byte0 = qspi[0];
    }

    // Detect board version and initialize codec
    auto board_ver = umi::daisy::detect_board_version();
    umi::daisy::init_codec(board_ver);

    // Initialize audio subsystem
    umi::daisy::init_sai_gpio();
    umi::daisy::init_sai();
    umi::daisy::init_audio_dma(audio_tx_buf, audio_rx_buf, AUDIO_BUFFER_SIZE);

    // USB MIDI
    umi::daisy::init_usb();
    umiusb::configure_hs_internal_phy();
    usb_audio.set_midi_callback(on_midi_message);
    usb_device.set_strings(usb_strings);
    usb_device.init();
    usb_hal.connect();

    // Pod HID: enable ADC12 clock, then initialize all controls
    {
        mm::DirectTransportT<> transport;
        // ADC clock source: per_ck (HSI 64 MHz by default)
        transport.modify(umi::stm32h7::RCC::D3CCIPR::ADCSEL::value(2));  // 10 = per_ck
        // ADC12 clock enable (AHB1)
        transport.modify(umi::stm32h7::RCC::AHB1ENR::ADC12EN::Set{});
        [[maybe_unused]] auto dummy = transport.read(umi::stm32h7::RCC::AHB1ENR{});
        // GPIO clocks for Pod controls (A,B,C,D,G) — most already enabled by init_clocks
        transport.modify(umi::stm32h7::RCC::AHB4ENR::GPIOAEN::Set{});
        transport.modify(umi::stm32h7::RCC::AHB4ENR::GPIOBEN::Set{});
        transport.modify(umi::stm32h7::RCC::AHB4ENR::GPIOCEN::Set{});
        transport.modify(umi::stm32h7::RCC::AHB4ENR::GPIODEN::Set{});
        transport.modify(umi::stm32h7::RCC::AHB4ENR::GPIOGEN::Set{});
        dummy = transport.read(umi::stm32h7::RCC::AHB4ENR{});

        // Audio callback rate: 48000 / 48 = 1000 Hz
        constexpr float update_rate = static_cast<float>(AUDIO_SAMPLE_RATE) / AUDIO_BLOCK_SIZE;
        pod_hid.init(transport, update_rate);
    }

    // Initialize task stacks
    umi::port::cm7::init_task_context(task_contexts[TASK_AUDIO],
        audio_stack, AUDIO_STACK_SIZE, audio_task_entry, nullptr, true);
    umi::port::cm7::init_task_context(task_contexts[TASK_SYSTEM],
        system_stack, SYSTEM_STACK_SIZE, system_task_entry, nullptr, false);
    umi::port::cm7::init_task_context(task_contexts[TASK_CONTROL],
        control_stack, CONTROL_STACK_SIZE, control_task_entry, nullptr, true);
    umi::port::cm7::init_task_context(task_contexts[TASK_IDLE],
        idle_stack, IDLE_STACK_SIZE, idle_task_entry, nullptr, false);

    // Set PendSV to lowest priority, SysTick low priority
    // SHPR3: PendSV at [23:16], SysTick at [31:24]
    auto* shpr3 = reinterpret_cast<volatile std::uint32_t*>(0xE000ED20);
    *shpr3 = (*shpr3 & 0x0000FFFF) | (0xFF << 16) | (0xF0 << 24);

    // Start audio DMA
    umi::daisy::start_audio();

    // Start scheduler: boot into control task
    current_task = TASK_CONTROL;
    umi_cm7_current_tcb = &task_contexts[TASK_CONTROL];
    umi::port::cm7::start_first_task();  // Never returns
}

// ============================================================================
// Boot Vector Table (Flash) - only 2 entries
// ============================================================================

extern "C" [[noreturn]] void Reset_Handler();

__attribute__((section(".isr_vector"), used))
const void* const g_boot_vectors[2] = {
    reinterpret_cast<const void*>(&_estack),
    reinterpret_cast<const void*>(Reset_Handler),
};

extern "C" {
extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);
}

extern "C" [[noreturn]] void Reset_Handler() {
    umi::cm7::enable_fpu();
    asm volatile("dsb\nisb" ::: "memory");

    umi::cm7::configure_mpu();
    umi::cm7::enable_icache();
    umi::cm7::enable_dcache();

    std::uint32_t* src = &_sidata;
    std::uint32_t* dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0;
    }

    // Zero-init DTCM BSS (vector table lives here, must be zeroed before irq::init)
    dst = &_sdtcm_bss;
    while (dst < &_edtcm_bss) {
        *dst++ = 0;
    }

    umi::irq::init();

    namespace exc = umi::backend::cm::exc;
    umi::irq::set_handler(exc::HardFault, HardFault_Handler);
    umi::irq::set_handler(exc::MemManage, MemManage_Handler);
    umi::irq::set_handler(exc::BusFault, BusFault_Handler);
    umi::irq::set_handler(exc::UsageFault, UsageFault_Handler);

    // PendSV and SVC via SRAM vector table
    umi::irq::set_handler(exc::PendSV, umi::port::cm7::PendSV_Handler);
    umi::irq::set_handler(exc::SVCall, umi::port::cm7::SVC_Handler);

    // DMA1 Stream 0/1
    umi::irq::set_handler(11, DMA1_Stream0_IRQHandler);
    umi::irq::set_handler(12, DMA1_Stream1_IRQHandler);
    umi::irq::set_priority(11, 0x00);
    umi::irq::set_priority(12, 0x00);
    umi::irq::enable(11);
    umi::irq::enable(12);

    for (void (**p)(void) = __init_array_start; p < __init_array_end; ++p) {
        (*p)();
    }

    main();
    while (true) {}
}

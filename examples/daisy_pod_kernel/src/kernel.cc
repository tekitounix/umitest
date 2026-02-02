// SPDX-License-Identifier: MIT
// Daisy Pod Kernel - Task logic, MIDI, synth, audio processing
#include "kernel.hh"

#include <cstdint>
#include <cstring>
#include <atomic>

#include <arch/cache.hh>
#include <arch/context.hh>
#include <common/scb.hh>

// Board
#include <board/hid.hh>
#include <board/mcu_init.hh>
#include <board/audio.hh>
#include <board/usb.hh>
#include <board/midi_uart.hh>

// umiusb
#include <hal/stm32_otg.hh>
#include <audio/audio_interface.hh>
#include <core/device.hh>
#include <core/descriptor.hh>

// umios
#include <umios/kernel/umi_kernel.hh>
#include <umios/kernel/fpu_policy.hh>
#include <umios/kernel/loader.hh>
#include <syscall_nr.hh>
#include <audio_context.hh>
#include <event.hh>
#include <event_router.hh>
#include <shared_state.hh>

// Local
#include "arch.hh"
#include "synth.hh"

// Debug area in DTCM — outside namespace for C linkage
#if UMI_DEBUG
extern "C" {
__attribute__((section(".dtcmram_bss"), used))
volatile std::uint32_t d2_dbg[16];
}
#define DBG(idx, expr) (d2_dbg[(idx)] = (expr))
#define DBG_INC(idx)   (d2_dbg[(idx)] = d2_dbg[(idx)] + 1)
#else
#define DBG(idx, expr) ((void)0)
#define DBG_INC(idx)   ((void)0)
#endif

namespace daisy_kernel {

namespace arch = umi::arch::cm7;
using namespace umi::daisy;

// ============================================================================
// Hardware Abstraction
// ============================================================================

constexpr std::uint32_t SYSTICK_PERIOD_US = 1000;

volatile std::uint64_t g_tick_us = 0;

struct Stm32H7Hw {
    static void set_timer_absolute(umi::usec) {}
    static umi::usec monotonic_time_usecs() { return g_tick_us; }
    static void enter_critical() {
        __asm__ volatile("msr basepri, %0" ::"r"(0x20u) : "memory");
    }
    static void exit_critical() {
        __asm__ volatile("msr basepri, %0" ::"r"(0u) : "memory");
    }
    static void trigger_ipi(std::uint8_t) {}
    static std::uint8_t current_core() { return 0; }
    static void request_context_switch() { arch::request_context_switch(); }
    static void enter_sleep() {
        __asm__ volatile("msr basepri, %0\n"
                         "wfi\n"
                         "msr basepri, %0\n" ::"r"(0u)
                         : "memory");
    }
    static std::uint32_t cycle_count() { return arch::dwt_cycle(); }
    static std::uint32_t cycles_per_usec() { return 480; }
};

using HW = umi::Hw<Stm32H7Hw>;

// ============================================================================
// Kernel Instance
// ============================================================================

umi::Kernel<8, 4, HW, 1> g_kernel;

constexpr umi::TaskFpuDecl fpu_decl {
    .audio   = true,
    .system  = false,
    .control = true,
    .idle    = false,
};
constexpr int fpu_task_count = umi::count_fpu_tasks(fpu_decl);
constexpr auto audio_fpu_policy   = umi::resolve_fpu_policy(fpu_decl.audio,   fpu_task_count);
constexpr auto control_fpu_policy = umi::resolve_fpu_policy(fpu_decl.control, fpu_task_count);
constexpr auto idle_fpu_policy    = umi::resolve_fpu_policy(fpu_decl.idle,    fpu_task_count);

umi::TaskId g_audio_task_id;
umi::TaskId g_control_task_id;
umi::TaskId g_idle_task_id;

// ============================================================================
// Task stacks and hardware TCBs
// ============================================================================

constexpr uint32_t AUDIO_TASK_STACK_SIZE = 2048;
constexpr uint32_t CONTROL_TASK_STACK_SIZE = 2048;
constexpr uint32_t IDLE_TASK_STACK_SIZE = 128;

uint32_t g_audio_task_stack[AUDIO_TASK_STACK_SIZE];
uint32_t g_control_task_stack[CONTROL_TASK_STACK_SIZE];
uint32_t g_idle_task_stack[IDLE_TASK_STACK_SIZE];

arch::TaskContext g_audio_tcb;
arch::TaskContext g_control_tcb;
arch::TaskContext g_idle_tcb;
arch::TaskContext* g_current_tcb = nullptr;

arch::TaskContext* task_id_to_hw_tcb(std::uint16_t idx) {
    if (idx == g_audio_task_id.value) return &g_audio_tcb;
    if (idx == g_control_task_id.value) return &g_control_tcb;
    if (idx == g_idle_task_id.value) return &g_idle_tcb;
    return nullptr;
}

// ============================================================================
// SpscQueue for DMA ISR → audio task
// ============================================================================

struct AudioBuffer {
    std::int32_t* tx;
    std::int32_t* rx;
};

umi::SpscQueue<AudioBuffer, 4> g_audio_ready_queue;

// ============================================================================
// USB MIDI
// ============================================================================

umiusb::Stm32HsHal usb_hal;
using UsbAudioMidi = umiusb::AudioFullDuplexMidi48k;
UsbAudioMidi usb_audio;

constexpr umiusb::DeviceInfo usb_device_info = {
    .vendor_id = 0x1209,
    .product_id = 0x000B,
    .device_version = 0x0100,
    .manufacturer_idx = 1,
    .product_idx = 2,
    .serial_idx = 0,
};

inline constexpr auto str_mfr = umiusb::StringDesc("UMI");
inline constexpr auto str_prod = umiusb::StringDesc("Daisy Pod Audio");

inline const std::array<std::span<const uint8_t>, 2> usb_strings = {
    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&str_mfr), str_mfr.size()),
    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&str_prod), str_prod.size()),
};

umiusb::Device<umiusb::Stm32HsHal, UsbAudioMidi> usb_device(usb_hal, usb_audio, usb_device_info);

// ============================================================================
// Pod HID + MIDI UART
// ============================================================================

umi::daisy::pod::PodHid pod_hid;
umi::daisy::MidiUartParser midi_uart_parser;

// ============================================================================
// umios infrastructure
// ============================================================================

[[maybe_unused]] float audio_float_in[2][AUDIO_BLOCK_SIZE];
[[maybe_unused]] float audio_float_out[2][AUDIO_BLOCK_SIZE];

umi::EventQueue<> audio_event_queue;
umi::EventQueue<> audio_output_events;
[[maybe_unused]] umi::SharedParamState shared_params;
[[maybe_unused]] umi::SharedChannelState shared_channel;
[[maybe_unused]] umi::SharedInputState shared_input;
umi::EventRouter event_router;

umi::sample_position_t audio_sample_pos = 0;

// ============================================================================
// Fallback Synth
// ============================================================================

DaisySynth g_synth;

// ============================================================================
// AppLoader + SharedMemory
// ============================================================================

// Linker-provided symbols for app regions
extern "C" {
extern std::uint8_t _app_image_start[];
extern std::uint8_t _app_ram_start[];
extern std::uint32_t _app_ram_size;
extern std::uint8_t _shared_start[];
}

__attribute__((section(".shared")))
umi::kernel::SharedMemory g_shared;

umi::kernel::AppLoader g_loader;

void init_shared_memory() {
    g_shared.set_sample_rate(AUDIO_SAMPLE_RATE);
    g_shared.buffer_size = AUDIO_BLOCK_SIZE;
    g_shared.sample_position = 0;

    event_router.set_shared_state(&g_shared.params, &g_shared.channel);
}

void init_loader() {
    g_loader.set_app_memory(_app_ram_start, reinterpret_cast<std::uintptr_t>(&_app_ram_size));
    g_loader.set_shared_memory(&g_shared);
}

// ============================================================================
// MPU Configuration (ARMv7-M, 16 regions)
// ============================================================================

void init_mpu() {
    // ARMv7-M MPU registers
    constexpr uint32_t mpu_base = 0xE000ED90;
    auto reg = [](uint32_t offset) {
        return reinterpret_cast<volatile uint32_t*>(mpu_base + offset);
    };
    auto* const TYPE = reg(0x00);
    auto* const CTRL = reg(0x04);
    auto* const RNR  = reg(0x08);
    auto* const RBAR = reg(0x0C);
    auto* const RASR = reg(0x10);

    // RASR helpers
    constexpr auto size_bits = [](uint32_t log2) { return (log2 - 1) << 1; };
    constexpr uint32_t ap_priv_rw    = 0x1 << 24;
    constexpr uint32_t ap_full_rw    = 0x3 << 24;
    constexpr uint32_t ap_ro         = 0x6 << 24;
    constexpr uint32_t tex_normal_wt = 0x0 << 19;
    constexpr uint32_t tex_device    = 0x2 << 19;
    constexpr uint32_t cacheable     = 1 << 17;
    constexpr uint32_t shareable     = 1 << 18;
    constexpr uint32_t xn            = 1 << 28;
    constexpr uint32_t enable        = 1 << 0;

    // Check MPU availability
    uint32_t type_val = *TYPE;
    if ((type_val & 0xFF00) == 0) {
        return;  // DREGION == 0 → no MPU
    }

    // Disable MPU during configuration
    *CTRL = 0;
    __asm__ volatile("dsb" ::: "memory");

    // Region 0: Kernel SRAM_D1 (0x24000000, 256KB) — Priv RW only, no exec
    *RNR  = 0;
    *RBAR = 0x24000000;
    // Note: No Shareable bit on D1 SRAM — Cortex-M7 single-core, S breaks ldrex/strex
    *RASR = ap_priv_rw | tex_normal_wt | cacheable | xn
          | size_bits(18) | enable;  // 2^18 = 256KB

    // Region 1: App QSPI Flash (0x90000000, 8MB) — RO + Exec
    *RNR  = 1;
    *RBAR = 0x90000000;
    *RASR = ap_ro | tex_normal_wt | cacheable
          | size_bits(23) | enable;  // 2^23 = 8MB

    // Region 2: App RAM (0x24040000, 256KB) — Full RW, no exec
    *RNR  = 2;
    *RBAR = 0x24040000;
    *RASR = ap_full_rw | tex_normal_wt | cacheable | xn
          | size_bits(18) | enable;  // 256KB

    // Region 3: Shared Memory (0x24070000, 64KB) — Full RW, no exec
    *RNR  = 3;
    *RBAR = 0x24070000;
    *RASR = ap_full_rw | tex_normal_wt | cacheable | xn
          | size_bits(16) | enable;  // 2^16 = 64KB

    // Region 4: D2 SRAM DMA (0x30000000, 32KB) — Full RW, Device (Strongly-Ordered)
    *RNR  = 4;
    *RBAR = 0x30000000;
    *RASR = ap_full_rw | tex_device | shareable | xn
          | size_bits(15) | enable;  // 2^15 = 32KB

    // Region 5: Peripherals (0x40000000, 512MB) — Priv RW, Device
    *RNR  = 5;
    *RBAR = 0x40000000;
    *RASR = ap_priv_rw | tex_device | shareable | xn
          | size_bits(29) | enable;  // 2^29 = 512MB

    // Region 6: Kernel Flash (0x08000000, 128KB) — RO + Exec
    *RNR  = 6;
    *RBAR = 0x08000000;
    *RASR = ap_ro | tex_normal_wt | cacheable
          | size_bits(17) | enable;  // 2^17 = 128KB

    // Region 7: DTCM (0x20000000, 128KB) — Priv RW only, no exec
    *RNR  = 7;
    *RBAR = 0x20000000;
    *RASR = ap_priv_rw | xn
          | size_bits(17) | enable;  // 2^17 = 128KB

    // Enable MPU: PRIVDEFENA=1 (default map for privileged), ENABLE=1
    *CTRL = (1 << 2) | (1 << 0);
    __asm__ volatile("dsb\nisb" ::: "memory");
}

bool qspi_accessible() {
    // Check RCC_AHB3ENR bit 14 (QSPIEN) — if clock not enabled, peripheral is inaccessible
    auto* rcc_ahb3enr = reinterpret_cast<volatile uint32_t*>(0x580244D4);
    if ((*rcc_ahb3enr & (1 << 14)) == 0) {
        return false;  // QSPI clock not enabled
    }
    // Check if QSPI is in memory-mapped mode: QUADSPI_CR bit 0 = EN, FMODE[27:26] = 11
    auto* quadspi_cr = reinterpret_cast<volatile uint32_t*>(0xA0001000);
    uint32_t cr = *quadspi_cr;
    return (cr & 1) != 0 && ((cr >> 26) & 3) == 3;
}

void* load_app() {
    // QSPI must be initialized in memory-mapped mode before accessing app image
    if (!qspi_accessible()) {
        return nullptr;
    }
    const auto* hdr = reinterpret_cast<const umi::kernel::AppHeader*>(_app_image_start);
    if (!hdr->valid_magic() || !hdr->compatible_abi()) {
        return nullptr;
    }
    auto entry_addr = reinterpret_cast<std::uintptr_t>(hdr->entry_point(_app_image_start)) | 1;
    auto app_entry = reinterpret_cast<void (*)()>(entry_addr);
    g_loader.set_entry(app_entry);
    return reinterpret_cast<void*>(app_entry);
}

// ============================================================================
// MIDI callback
// ============================================================================

void on_midi_message(std::uint8_t cable, const std::uint8_t* data, std::uint8_t len) {
    DBG_INC(6);
    DBG(7, (static_cast<std::uint32_t>(len) << 24)
          | (static_cast<std::uint32_t>(data[0]) << 16)
          | (static_cast<std::uint32_t>(len >= 2 ? data[1] : 0) << 8)
          | (static_cast<std::uint32_t>(len >= 3 ? data[2] : 0)));
    if (len < 2) return;
    std::uint8_t status = data[0] & 0xF0;
    std::uint8_t ch = data[0] & 0x0F;

    Event ev;
    ev.channel = ch;

    if (status == 0x90 && len >= 3 && data[2] > 0) {
        ev.type = EventType::NOTE_ON;
        ev.param = data[1];
        ev.value = data[2];
        g_synth.event_queue().push(ev);
    } else if (status == 0x80 || (status == 0x90 && len >= 3 && data[2] == 0)) {
        ev.type = EventType::NOTE_OFF;
        ev.param = data[1];
        ev.value = 0;
        g_synth.event_queue().push(ev);
    } else if (status == 0xB0 && len >= 3) {
        ev.type = EventType::CC;
        ev.param = data[1];
        ev.value = data[2];
        g_synth.event_queue().push(ev);
    }

    umi::RawInput raw;
    raw.hw_timestamp = 0;
    raw.source_id = cable;
    raw.size = len;
    for (std::uint8_t i = 0; i < len && i < 6; ++i) {
        raw.payload[i] = data[i];
    }
    event_router.receive(raw, 0, AUDIO_SAMPLE_RATE);
}

// ============================================================================
// Task entries
// ============================================================================

void audio_task_entry(void*) {
    while (true) {
        g_kernel.wait_block(g_audio_task_id, umi::KernelEvent::audio);

        auto buf = g_audio_ready_queue.try_pop();
        if (!buf.has_value()) continue;

        auto* out = buf->tx;
        DBG_INC(0);

        if (g_loader.state() == umi::kernel::AppState::RUNNING) {
            // App loaded: convert int32 → float, call app processor, convert back
            // TODO(Step 7): full AudioContext + float pipeline
            // For now, use simple call_process with float spans
            constexpr uint32_t stereo_samples = AUDIO_BLOCK_SIZE * 2;
            float fbuf_out[stereo_samples];
            float fbuf_in[stereo_samples];

            // int32 → float (24-bit codec)
            constexpr float scale_in = 1.0f / 8388608.0f;  // 2^23
            auto* rx = buf->rx;
            for (uint32_t i = 0; i < stereo_samples; ++i) {
                fbuf_in[i] = static_cast<float>(rx[i]) * scale_in;
            }

            g_loader.call_process(
                std::span<float>(fbuf_out, stereo_samples),
                std::span<const float>(fbuf_in, stereo_samples),
                audio_sample_pos, AUDIO_BLOCK_SIZE, g_shared.dt);

            // float → int32
            constexpr float scale_out = 8388608.0f;
            for (uint32_t i = 0; i < stereo_samples; ++i) {
                out[i] = static_cast<int32_t>(fbuf_out[i] * scale_out);
            }
        } else {
            // Fallback: built-in synth
            g_synth.process(out, AUDIO_BLOCK_SIZE);

            if (!g_synth.has_active_voice()) {
                usb_audio.read_audio(out, AUDIO_BLOCK_SIZE);
            }
        }

        usb_audio.write_audio_in(out, AUDIO_BLOCK_SIZE);

        // D-Cache clean: flush TX buffer to SRAM so DMA can read it
        {
            auto addr = reinterpret_cast<std::uintptr_t>(out);
            auto end = addr + (AUDIO_BLOCK_SIZE * 2) * sizeof(std::int32_t);
            for (; addr < end; addr += 32) {
                *umi::cm7::scb::DCCMVAC = static_cast<std::uint32_t>(addr);
            }
            __asm__ volatile("dsb sy" ::: "memory");
        }

        audio_sample_pos += AUDIO_BLOCK_SIZE;
    }
}

void control_task_entry(void*) {
    mm::DirectTransportT<> transport;
    constexpr float hid_rate = 1000.0f;
    std::uint32_t loop_counter = 0;

    while (true) {
        DBG_INC(5);
        usb_device.poll();

        if (++loop_counter >= 100) {
            loop_counter = 0;
            pod_hid.update_controls(transport, hid_rate);

            float knob_val = pod_hid.knobs.value(0);
            if (knob_val > 0.01f) {
                g_synth.volume = knob_val;
            }
            pod_hid.led1.set(g_synth.volume, 0.0f, 0.0f);
            pod_hid.led2.set(0.0f, 0.0f, pod_hid.knobs.value(1));

            if (pod_hid.encoder.click_just_pressed()) {
                umi::daisy::toggle_led();
            }

            if (pod_hid.encoder.click_just_pressed()) {
                g_synth.event_queue().push({EventType::BUTTON_DOWN, 0, 0, 127});
            }
            if (pod_hid.encoder.increment() != 0) {
                g_synth.event_queue().push({EventType::ENCODER_INCREMENT, 0, 0,
                    static_cast<std::uint8_t>(pod_hid.encoder.increment() > 0 ? 1 : 0xFF)});
            }
        }

        pod_hid.process_knobs();
        arch::yield();
    }
}

void idle_task_entry(void*) {
    while (true) {
        asm volatile("wfi");
    }
}

// ============================================================================
// Kernel callbacks
// ============================================================================

static void tick_callback() {
    g_tick_us += SYSTICK_PERIOD_US;
    g_kernel.resume_task(g_control_task_id);
}

static void switch_context_callback() {
    Stm32H7Hw::enter_critical();
    g_kernel.resolve_pending();
    auto next_opt = g_kernel.get_next_task();
    if (next_opt.has_value()) {
        g_kernel.prepare_switch(*next_opt);
        auto* next_hw_tcb = task_id_to_hw_tcb(*next_opt);
        g_current_tcb = next_hw_tcb;
        arch::current_tcb = next_hw_tcb;
    }
    Stm32H7Hw::exit_critical();
}

static void svc_callback(uint32_t* sp) {
    // Stack frame: R0, R1, R2, R3, R12, LR, PC, xPSR
    uint32_t arg0 = sp[0];
    [[maybe_unused]] uint32_t arg1 = sp[1];
    uint32_t syscall_nr = sp[4];  // R12

    namespace sc = ::umi::syscall::nr;
    int32_t result = 0;

    switch (syscall_nr) {
    case sc::exit:
        g_loader.terminate(static_cast<int>(arg0));
        break;

    case sc::yield:
        g_kernel.yield();
        break;

    case sc::register_proc:
        if (arg1 != 0) {
            result = g_loader.register_processor(
                reinterpret_cast<void*>(arg0),
                reinterpret_cast<umi::kernel::ProcessFn>(arg1));
        } else {
            result = g_loader.register_processor(reinterpret_cast<void*>(arg0));
        }
        break;

    case sc::wait_event:
        // Simplified: block control task on mask
        sp[0] = g_kernel.wait_block(g_control_task_id, arg0);
        return;

    case sc::get_time:
        sp[0] = static_cast<uint32_t>(g_tick_us & 0xFFFFFFFFu);
        sp[1] = static_cast<uint32_t>(g_tick_us >> 32);
        return;

    case sc::get_shared:
        sp[0] = reinterpret_cast<uint32_t>(&g_shared);
        return;

    case sc::sleep: {
        uint64_t timeout_us = arg0;
        if (timeout_us > 0) {
            (void)g_kernel.wait_block(g_control_task_id, 0);
        }
        break;
    }

    case sc::log:
        // stub
        break;

    case sc::set_route_table:
    case sc::set_param_mapping:
    case sc::set_input_mapping:
    case sc::configure_input:
    case sc::set_app_config:
    case sc::send_param_request:
        // stubs for now
        break;

    default:
        result = -1;
        break;
    }

    sp[0] = static_cast<uint32_t>(result);
}

// ============================================================================
// Public API
// ============================================================================

void init() {
#if UMI_DEBUG
    for (auto& d : d2_dbg) d = 0;
#endif
    init_mpu();
    init_shared_memory();
    init_loader();
    load_app();
}

void on_audio_buffer_ready(std::int32_t* tx, std::int32_t* rx) {
    AudioBuffer buf{tx, rx};
    g_audio_ready_queue.try_push(buf);
    g_kernel.signal(g_audio_task_id, umi::KernelEvent::audio);
    umi::port::arm::SCB::trigger_pendsv();
}

void init_usb() {
    ::umi::daisy::init_usb();
    umiusb::configure_hs_internal_phy();
    usb_audio.set_midi_callback(on_midi_message);
    usb_device.set_strings(usb_strings);
    usb_device.init();
    usb_hal.connect();
}

void init_hid(float update_rate) {
    mm::DirectTransportT<> transport;
    transport.modify(umi::stm32h7::RCC::D3CCIPR::ADCSEL::value(2));
    transport.modify(umi::stm32h7::RCC::AHB1ENR::ADC12EN::Set{});
    [[maybe_unused]] auto dummy = transport.read(umi::stm32h7::RCC::AHB1ENR{});
    transport.modify(umi::stm32h7::RCC::AHB4ENR::GPIOAEN::Set{});
    transport.modify(umi::stm32h7::RCC::AHB4ENR::GPIOBEN::Set{});
    transport.modify(umi::stm32h7::RCC::AHB4ENR::GPIOCEN::Set{});
    transport.modify(umi::stm32h7::RCC::AHB4ENR::GPIODEN::Set{});
    transport.modify(umi::stm32h7::RCC::AHB4ENR::GPIOGEN::Set{});
    dummy = transport.read(umi::stm32h7::RCC::AHB4ENR{});
    pod_hid.init(transport, update_rate);
}

void handle_usart1_irq() {
    using namespace umi::stm32h7;
    mm::DirectTransportT<> t;
    auto isr = t.read(USART1::ISR{});

    if (isr & ((1U << 3) | (1U << 1))) {
        umi::daisy::midi_uart_clear_errors();
    }

    if (isr & (1U << 5)) {
        auto byte = umi::daisy::midi_uart_read_byte();
        if (midi_uart_parser.feed(byte)) {
            std::uint8_t msg[3] = {midi_uart_parser.running_status,
                                    midi_uart_parser.data[0],
                                    midi_uart_parser.data[1]};
            on_midi_message(0, msg, midi_uart_parser.expected + 1);
        }
    }
}

[[noreturn]] void start_rtos() {
    g_audio_task_id = g_kernel.create_task({
        .entry = audio_task_entry,
        .arg = nullptr,
        .prio = umi::Priority::REALTIME,
        .uses_fpu = fpu_decl.audio,
        .name = "audio",
    });

    g_control_task_id = g_kernel.create_task({
        .entry = control_task_entry,
        .arg = nullptr,
        .prio = umi::Priority::USER,
        .uses_fpu = fpu_decl.control,
        .name = "control",
    });

    g_idle_task_id = g_kernel.create_task({
        .entry = idle_task_entry,
        .arg = nullptr,
        .prio = umi::Priority::IDLE,
        .uses_fpu = fpu_decl.idle,
        .name = "idle",
    });

    arch::init_task<audio_fpu_policy>(
        g_audio_tcb, g_audio_task_stack, AUDIO_TASK_STACK_SIZE, audio_task_entry, nullptr);
    arch::init_task<control_fpu_policy>(
        g_control_tcb, g_control_task_stack, CONTROL_TASK_STACK_SIZE, control_task_entry, nullptr);
    arch::init_task<idle_fpu_policy>(
        g_idle_tcb, g_idle_task_stack, IDLE_TASK_STACK_SIZE, idle_task_entry, nullptr);

    g_kernel.prepare_switch(g_control_task_id.value);
    g_current_tcb = &g_control_tcb;

    arch::set_tick_callback(tick_callback);
    arch::set_switch_context_callback(switch_context_callback);
    arch::set_svc_callback(svc_callback);

    arch::init_cycle_counter();
    arch::init_systick(480'000'000, SYSTICK_PERIOD_US);

    uint32_t* control_stack_top = g_control_task_stack + CONTROL_TASK_STACK_SIZE;
    arch::start_scheduler(&g_control_tcb, control_task_entry, nullptr, control_stack_top);
}

} // namespace daisy_kernel

// SPDX-License-Identifier: MIT
// STM32F4 Kernel Implementation
// RTOS scheduler, syscall handling, audio processing
// Uses umi::Kernel (scheduler/state), SpscQueue (IPC) from lib/umios

#include "kernel.hh"
#include "arch.hh"
#include "bsp.hh"
#include "mcu.hh"

#include <array>
#include <cstdint>
#include <cstring>
#include <span>

#include <app_header.hh>
#include <loader.hh>
#include <umios/core/audio_context.hh>
#include <umios/kernel/umi_kernel.hh>

// USB stack (needed for complete type)
#include <audio_interface.hh>
#include <hal/stm32_otg.hh>

#include <protocol/standard_io.hh>
#include <umios/kernel/shell_commands.hh>

namespace umi::kernel {

// ============================================================================
// Hardware Abstraction (Hw<Impl>)
// ============================================================================

struct Stm32F4Hw {
    static void set_timer_absolute(umi::usec) {}
    static umi::usec monotonic_time_usecs() { return g_tick_us; }

    // PRIMASK-based nestable critical section.
    // Disables all interrupts (including DMA) for the duration.
    // Critical sections are short (~10-20 cycles for bitmap ops),
    // so DMA latency is negligible.
    // Nesting is safe: PRIMASK save/restore via nest count.
    static inline volatile uint32_t nest_count_ = 0;
    static inline volatile uint32_t saved_primask_ = 0;

    static void enter_critical() {
        uint32_t prev;
        __asm__ volatile("mrs %0, primask" : "=r"(prev) :: "memory");
        __asm__ volatile("cpsid i" ::: "memory");
        if (nest_count_ == 0) saved_primask_ = prev;
        nest_count_ = nest_count_ + 1;
    }
    static void exit_critical() {
        nest_count_ = nest_count_ - 1;
        if (nest_count_ == 0) {
            uint32_t restore = saved_primask_;
            __asm__ volatile("msr primask, %0" :: "r"(restore) : "memory");
        }
    }

    static void trigger_ipi(std::uint8_t) {}
    static std::uint8_t current_core() { return 0; }

    // IMPORTANT: schedule() inside Kernel calls this, but we rely on
    // switch_context_callback (PendSV) for actual context switch.
    // This is fine — PendSV is deferred and executes after ISR/critical section exit.
    static void request_context_switch() {
        arch::cm4::request_context_switch();
    }

    static void save_fpu() {}
    static void restore_fpu() {}
    static void mute_audio_dma() { mcu::dma_i2s_disable(); }
    static void write_backup_ram(const void*, std::size_t) {}
    static void read_backup_ram(void*, std::size_t) {}
    static void configure_mpu_region(std::size_t, const void*, std::size_t, bool, bool) {}
    static void cache_clean(const void*, std::size_t) {}
    static void cache_invalidate(void*, std::size_t) {}
    static void cache_clean_invalidate(void*, std::size_t) {}
    static void system_reset() { mcu::system_reset(); }
    static void enter_sleep() { __asm__ volatile("wfi"); }
    [[noreturn]] static void start_first_task() {
        while (true) { __asm__ volatile("wfi"); }
    }
    static void watchdog_init(std::uint32_t) {}
    static void watchdog_feed() {}
    static std::uint32_t cycle_count() { return arch::cm4::dwt_cycle(); }
    static std::uint32_t cycles_per_usec() { return 168; }
};

using HW = umi::Hw<Stm32F4Hw>;

// ============================================================================
// Kernel Instance
// ============================================================================

umi::Kernel<8, 4, HW, 1> g_kernel;

umi::TaskId g_audio_task_id;
umi::TaskId g_system_task_id;
umi::TaskId g_control_task_id;
umi::TaskId g_idle_task_id;

// ============================================================================
// Configuration
// ============================================================================

constexpr size_t INPUT_EVENT_CAPACITY = 32;

// ============================================================================
// Task stacks and hardware TCBs (Kernel doesn't manage HW context)
// ============================================================================

constexpr uint32_t AUDIO_TASK_STACK_SIZE = 1024;
constexpr uint32_t SYSTEM_TASK_STACK_SIZE = 512;
constexpr uint32_t CONTROL_TASK_STACK_SIZE = 2048;
constexpr uint32_t IDLE_TASK_STACK_SIZE = 64;

__attribute__((section(".audio_ccm"))) uint32_t g_audio_task_stack[AUDIO_TASK_STACK_SIZE];
__attribute__((section(".audio_ccm"))) uint32_t g_system_task_stack[SYSTEM_TASK_STACK_SIZE];
__attribute__((section(".audio_ccm"))) uint32_t g_control_task_stack[CONTROL_TASK_STACK_SIZE];
__attribute__((section(".audio_ccm"))) uint32_t g_idle_task_stack[IDLE_TASK_STACK_SIZE];

arch::cm4::TaskContext g_audio_tcb;
arch::cm4::TaskContext g_system_tcb;
arch::cm4::TaskContext g_control_tcb;
arch::cm4::TaskContext g_idle_tcb;
arch::cm4::TaskContext* g_current_tcb = nullptr;

namespace bsp = umi::bsp::board;

// ============================================================================
// SpscQueues (lock-free IPC replacing manual ring buffers)
// ============================================================================

struct MidiMsg {
    uint8_t data[4];
    uint8_t len;
};

struct ButtonEvent {
    uint8_t type;
    uint8_t id;
};

umi::SpscQueue<MidiMsg, 64> g_midi_queue;
umi::SpscQueue<ButtonEvent, 16> g_button_queue;
umi::SpscQueue<uint16_t*, 4> g_audio_ready_queue;

// ============================================================================
// Global State
// ============================================================================

volatile bool g_rtos_started = false;

// PDM state
volatile bool g_pdm_ready = false;
volatile uint16_t* g_active_pdm_buf = nullptr;

// Timing / sample rate
volatile uint32_t g_tick_us = 0;
volatile uint32_t g_current_sample_rate = bsp::audio::default_sample_rate;
volatile bool g_sample_rate_change_pending = false;
volatile uint32_t g_new_sample_rate = bsp::audio::default_sample_rate;

// Control task wakeup (for timed wait)
volatile uint64_t g_control_task_wakeup_us = 0;

AppLoader g_loader;
__attribute__((section(".shared"))) SharedMemory g_shared;

// ============================================================================
// Button Debouncer
// ============================================================================

struct ButtonDebouncer {
    uint8_t stable_state = 0;
    uint8_t sample_count = 0;
    uint8_t last_sample = 0;
    static constexpr uint8_t DEBOUNCE_COUNT = 20;

    uint8_t update(bool raw_pressed) {
        uint8_t raw = raw_pressed ? 1 : 0;
        if (raw == last_sample) {
            if (sample_count < DEBOUNCE_COUNT) {
                ++sample_count;
            }
            if (sample_count >= DEBOUNCE_COUNT && raw != stable_state) {
                stable_state = raw;
                return raw ? 1 : 2;
            }
        } else {
            last_sample = raw;
            sample_count = 0;
        }
        return 0;
    }
};
ButtonDebouncer g_user_button;

// ============================================================================
// SysEx Shell
// ============================================================================

class Stm32StateProvider {
public:
    Stm32StateProvider() {
        config_.platform_name = "STM32F4-Discovery";
        std::strcpy(config_.serial_number, "STM32-00000001");
        config_.sample_rate = bsp::audio::default_sample_rate;
    }

    umi::os::KernelStateView& state() {
        state_.uptime_us = g_tick_us;
        state_.audio_running = true;
        state_.task_count = 4;
        return state_;
    }

    umi::os::ShellConfig& config() { return config_; }
    umi::os::ErrorLog<16>& error_log() { return error_log_; }
    umi::os::SystemMode& system_mode() { return mode_; }
    void reset_system() { mcu::system_reset(); }
    void feed_watchdog() {}
    void enable_watchdog(bool enable) { state_.watchdog_enabled = enable; }

private:
    umi::os::KernelStateView state_{};
    umi::os::ShellConfig config_{};
    umi::os::ErrorLog<16> error_log_{};
    umi::os::SystemMode mode_ = umi::os::SystemMode::Normal;
};

Stm32StateProvider g_state_provider;
umi::os::ShellCommands<Stm32StateProvider, 1024>* g_shell = nullptr;
umidi::protocol::StandardIO<256, 128> g_stdio;

constexpr size_t SYSEX_BUF_SIZE = 256;
uint8_t g_sysex_buf[SYSEX_BUF_SIZE];
volatile size_t g_sysex_len = 0;
volatile bool g_sysex_ready = false;

// ============================================================================
// Audio Buffers
// ============================================================================

int32_t i2s_work_buf[mcu::audio::buffer_size * 2];
umi::sample_t synth_out_mono[mcu::audio::buffer_size];
static std::array<umi::sample_t*, 1> synth_out_channels = {synth_out_mono};
static EventQueue<> g_app_event_queue;
int16_t last_synth_out[mcu::audio::buffer_size * 2];

// TCB for context switch (defined in arch.cc)
extern "C" arch::cm4::TaskContext* volatile umi_cm4_current_tcb;

// ============================================================================
// TaskId → Hardware TCB mapping
// ============================================================================

static arch::cm4::TaskContext* task_id_to_hw_tcb(uint16_t idx) {
    switch (idx) {
    case 0: return &g_audio_tcb;
    case 1: return &g_system_tcb;
    case 2: return &g_control_tcb;
    case 3: return &g_idle_tcb;
    default: return &g_idle_tcb;
    }
}

// ============================================================================
// Shell Output
// ============================================================================

static void shell_write(const char* str) {
    auto len = std::strlen(str);
    g_stdio.write_stdout(
        reinterpret_cast<const uint8_t*>(str), len, [](const uint8_t* data, size_t len) {
            mcu::usb_audio().send_sysex(mcu::usb_hal(), data, static_cast<uint16_t>(len));
        });
}

// ============================================================================
// Audio Processing
// ============================================================================

static inline int32_t clamp_i24(int32_t value) {
    if (value > 0x7FFFFF)
        return 0x7FFFFF;
    if (value < -0x800000)
        return -0x800000;
    return value;
}

static void pack_i2s_24(uint16_t* out, const int32_t* in, uint32_t frames, uint32_t channels) {
    uint32_t samples = frames * channels;
    for (uint32_t i = 0; i < samples; ++i) {
        int32_t clamped = clamp_i24(in[i]);
        uint32_t v = (static_cast<uint32_t>(clamped) & 0x00FFFFFFu) << 8;
        out[i * 2] = static_cast<uint16_t>((v >> 16) & 0xFFFF);
        out[i * 2 + 1] = static_cast<uint16_t>(v & 0xFFFF);
    }
}

static void process_audio_frame(uint16_t* buf) {
    const float dt = g_shared.dt;

    mcu::usb_audio().read_audio(i2s_work_buf, mcu::audio::buffer_size);
    pack_i2s_24(buf, i2s_work_buf, mcu::audio::buffer_size, 2);

    if (g_loader.state() == AppState::Running) {
        for (uint32_t i = 0; i < mcu::audio::buffer_size; ++i) {
            synth_out_mono[i] = 0.0f;
        }

        std::array<Event, INPUT_EVENT_CAPACITY> input_events{};
        size_t input_event_count = 0;

        // Drain MIDI queue (SpscQueue)
        while (input_event_count < input_events.size()) {
            auto msg = g_midi_queue.try_pop();
            if (!msg) break;

            uint8_t status = 0;
            uint8_t d1 = 0;
            uint8_t d2 = 0;

            if (msg->len >= 4) {
                status = msg->data[1];
                d1 = msg->data[2];
                d2 = msg->data[3];
            } else if (msg->len == 3) {
                status = msg->data[0];
                d1 = msg->data[1];
                d2 = msg->data[2];
            } else if (msg->len == 2) {
                status = msg->data[0];
                d1 = msg->data[1];
            }

            if (status != 0) {
                input_events[input_event_count++] = Event::make_midi(0, 0, status, d1, d2);
            }
        }

        // Drain button queue (SpscQueue)
        while (input_event_count < input_events.size()) {
            auto ev = g_button_queue.try_pop();
            if (!ev) break;
            if (ev->type == 0) {
                input_events[input_event_count++] = Event::button_down(0, ev->id);
            } else {
                input_events[input_event_count++] = Event::button_up(0, ev->id);
            }
        }

        std::span<const Event> input_span(input_events.data(), input_event_count);

        AudioContext ctx{
            std::span<const sample_t* const>{},
            std::span<sample_t* const>(synth_out_channels),
            input_span,
            g_app_event_queue,
            g_shared.sample_rate,
            mcu::audio::buffer_size,
            dt,
            g_shared.sample_position,
        };

        g_loader.call_process(ctx);
        g_shared.sample_position += mcu::audio::buffer_size;

        for (uint32_t i = 0; i < mcu::audio::buffer_size; ++i) {
            float synth_val = synth_out_mono[i];
            if (synth_val > 1.0f)
                synth_val = 1.0f;
            if (synth_val < -1.0f)
                synth_val = -1.0f;
            last_synth_out[i * 2] = static_cast<int16_t>(synth_val * 32767.0f);
            last_synth_out[i * 2 + 1] = last_synth_out[i * 2];
        }
    }

    if (mcu::usb_audio().is_audio_in_streaming()) {
        auto* stereo = mcu::stereo_buf();
        if (g_current_sample_rate >= 96000) {
            __builtin_memset(stereo, 0, mcu::audio::buffer_size * 2 * sizeof(int16_t));
        } else {
            for (uint32_t i = 0; i < mcu::audio::buffer_size; ++i) {
                stereo[i * 2] = mcu::pcm_buf()[i];
                stereo[i * 2 + 1] = last_synth_out[i * 2];
            }
        }
        mcu::usb_audio().write_audio_in(stereo, mcu::audio::buffer_size);
    }
}

// ============================================================================
// Task Entries
// ============================================================================

static void process_server_work();

static void audio_task_entry(void* arg) {
    (void)arg;

    while (true) {
        g_kernel.wait_block(g_audio_task_id, KernelEvent::AudioReady);

        while (auto buf_opt = g_audio_ready_queue.try_pop()) {
            process_audio_frame(*buf_opt);
        }

        // Run server-priority work inline to avoid starvation.
        // audio_task never truly blocks (DMA notifications arrive faster
        // than processing), so system_task never gets scheduled.
        // Executing its work here after each audio drain keeps latency
        // minimal while ensuring PDM, sample-rate, and SysEx processing.
        process_server_work();
    }
}

static void handle_sample_rate_change();

static void process_server_work() {
    handle_sample_rate_change();

    if (g_pdm_ready && g_current_sample_rate < 96000) {
        g_pdm_ready = false;
        uint16_t* pdm_data = const_cast<uint16_t*>(g_active_pdm_buf);
        mcu::cic_decimator().process_buffer(
            pdm_data, mcu::audio::pdm_buf_size, mcu::pcm_buf(), mcu::audio::pcm_buf_size);
    }

    if (g_sysex_ready) {
        g_stdio.process_message(g_sysex_buf, g_sysex_len);
        g_sysex_ready = false;
    }
}

static void system_task_entry(void* arg) {
    (void)arg;

    static umi::os::ShellCommands<Stm32StateProvider, 1024> shell(g_state_provider);
    g_shell = &shell;

    static umi::shell::LineBuffer<64> line_buffer;

    g_stdio.set_stdin_callback(
        [](const uint8_t* data, size_t len, void*) {
            for (size_t i = 0; i < len; ++i) {
                char c = static_cast<char>(data[i]);
                if (line_buffer.process_char(c)) {
                    const char* line = line_buffer.get_line();
                    if (line && line[0] != '\0') {
                        const char* result = g_shell->execute(line);
                        if (result && result[0] != '\0') {
                            shell_write(result);
                            shell_write("\n");
                        }
                    }
                    line_buffer.clear();
                }
            }
        },
        nullptr);

    while (true) {
        handle_sample_rate_change();

        if (g_pdm_ready && g_current_sample_rate < 96000) {
            g_pdm_ready = false;
            uint16_t* pdm_data = const_cast<uint16_t*>(g_active_pdm_buf);
            mcu::cic_decimator().process_buffer(
                pdm_data, mcu::audio::pdm_buf_size, mcu::pcm_buf(), mcu::audio::pcm_buf_size);
        }

        if (g_sysex_ready) {
            g_stdio.process_message(g_sysex_buf, g_sysex_len);
            g_sysex_ready = false;
        }

        g_kernel.suspend_task(g_system_task_id);
    }
}

static void control_task_entry(void* arg) {
    auto app_entry = reinterpret_cast<void (*)()>(arg);

    if (app_entry != nullptr) {
        app_entry();
    }

    while (true) {
        g_kernel.suspend_task(g_control_task_id);
    }
}

static void idle_task_entry(void* arg) {
    (void)arg;
    while (true) {
        arch::cm4::wait_for_interrupt();
    }
}

static void handle_sample_rate_change() {
    if (!g_sample_rate_change_pending) {
        return;
    }

    g_sample_rate_change_pending = false;
    uint32_t new_rate = g_new_sample_rate;

    if (new_rate == g_current_sample_rate) {
        return;
    }
    if (new_rate != 44100 && new_rate != 48000 && new_rate != 96000) {
        return;
    }

    mcu::usb_audio().block_audio_out_rx((new_rate >= 96000) ? 48 : 12);

    mcu::disable_i2s_irq();

    mcu::dma_i2s_disable();
    arch::cm4::delay_cycles(1000);
    mcu::i2s_disable();

    for (uint32_t i = 0; i < mcu::audio::i2s_dma_words; ++i) {
        mcu::audio_buf0()[i] = 0;
        mcu::audio_buf1()[i] = 0;
    }

    uint32_t actual_rate = mcu::configure_plli2s(new_rate);
    mcu::dma_i2s_init();

    // Drain audio queue on rate change
    while (g_audio_ready_queue.try_pop()) {}

    mcu::enable_i2s_irq();

    mcu::i2s_enable_dma();
    mcu::i2s_enable();
    mcu::dma_i2s_enable();

    g_current_sample_rate = new_rate;
    g_shared.set_sample_rate(new_rate);

    mcu::usb_audio().set_actual_rate(actual_rate);
    mcu::usb_audio().reset_audio_out(actual_rate);
}

// ============================================================================
// ISR Callbacks
// ============================================================================

void on_audio_buffer_ready(uint16_t* buf) {
    if (!g_rtos_started) {
        return;
    }

    if (g_audio_ready_queue.try_push(buf)) {
        g_kernel.notify(g_audio_task_id, KernelEvent::AudioReady);
    }
}

void on_pdm_buffer_ready(uint16_t* buf) {
    g_active_pdm_buf = buf;
    g_pdm_ready = true;
}

// ============================================================================
// Syscall Handler
// ============================================================================

namespace app_syscall {
inline constexpr uint32_t Exit         = 0;
inline constexpr uint32_t RegisterProc = 1;
inline constexpr uint32_t WaitEvent    = 2;
inline constexpr uint32_t Yield        = 5;
inline constexpr uint32_t GetTime      = 10;
inline constexpr uint32_t GetShared    = 40;
inline constexpr uint32_t MidiSend     = 50;
inline constexpr uint32_t MidiRecv     = 51;
}

static void svc_handler_impl(uint32_t* sp) {
    uint32_t syscall_nr = sp[0];
    uint32_t arg0 = sp[1];
    uint32_t arg1 = sp[2];
    int32_t result = 0;

    switch (syscall_nr) {
    case app_syscall::Exit:
        g_loader.terminate(static_cast<int>(arg0));
        result = 0;
        break;

    case app_syscall::RegisterProc:
        if (arg1 != 0) {
            result =
                g_loader.register_processor(reinterpret_cast<void*>(arg0), reinterpret_cast<ProcessFn>(arg1));
        } else {
            result = g_loader.register_processor(reinterpret_cast<void*>(arg0));
        }
        break;

    case app_syscall::WaitEvent: {
        uint64_t timeout_us = arg1;
        if (timeout_us > 0) {
            g_control_task_wakeup_us = g_tick_us + timeout_us;
        } else {
            g_control_task_wakeup_us = 0;
        }
        g_kernel.suspend_task(g_control_task_id);
        result = 0;
        break;
    }

    case app_syscall::Yield:
        arch::cm4::request_context_switch();
        result = 0;
        break;

    case app_syscall::GetTime:
        sp[0] = g_tick_us;
        return;

    case app_syscall::GetShared:
        sp[0] = reinterpret_cast<uint32_t>(&g_shared);
        return;

    case app_syscall::MidiRecv: {
        auto msg = g_midi_queue.try_pop();
        if (msg) {
            MidiMsg* out = reinterpret_cast<MidiMsg*>(arg0);
            *out = *msg;
            result = msg->len;
        } else {
            result = 0;
        }
        break;
    }

    case app_syscall::MidiSend:
        if (arg1 >= 1 && arg1 <= 3) {
            const uint8_t* data = reinterpret_cast<const uint8_t*>(arg0);
            uint8_t status = data[0];
            uint8_t d1 = (arg1 >= 2) ? data[1] : 0;
            uint8_t d2 = (arg1 >= 3) ? data[2] : 0;
            mcu::usb_audio().send_midi(mcu::usb_hal(), 0, status, d1, d2);
            result = 0;
        } else {
            result = -1;
        }
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

void setup_usb_callbacks() {
    mcu::usb_audio().set_midi_callback([](uint8_t, const uint8_t* data, uint8_t len) {
        MidiMsg msg{};
        msg.len = (len > 4) ? 4 : len;
        for (uint8_t i = 0; i < msg.len; ++i) {
            msg.data[i] = data[i];
        }
        g_midi_queue.try_push(msg);
    });

    mcu::usb_audio().set_sysex_callback([](const uint8_t* data, uint16_t len) {
        if (!g_sysex_ready && len <= SYSEX_BUF_SIZE) {
            std::memcpy(g_sysex_buf, data, len);
            g_sysex_len = len;
            g_sysex_ready = true;
            g_kernel.resume_task(g_system_task_id);
        }
    });

    mcu::usb_audio().set_sample_rate_callback([](uint32_t new_rate) {
        if (!g_sample_rate_change_pending && new_rate != g_current_sample_rate) {
            g_new_sample_rate = new_rate;
            g_sample_rate_change_pending = true;
        }
    });

    mcu::usb_audio().on_streaming_change = [](bool streaming) {
        if (streaming) {
            mcu::gpio(bsp::led::gpio_port).set(bsp::led::blue);
        } else {
            mcu::gpio(bsp::led::gpio_port).reset(bsp::led::blue);
        }
    };

    mcu::usb_audio().on_audio_in_change = [](bool streaming) {
        if (streaming) {
            mcu::gpio(bsp::led::gpio_port).set(bsp::led::orange);
        } else {
            mcu::gpio(bsp::led::gpio_port).reset(bsp::led::orange);
        }
    };

    mcu::usb_audio().on_audio_rx = []() {
        static uint8_t cnt = 0;
        if (++cnt >= 48) {
            cnt = 0;
            mcu::gpio(bsp::led::gpio_port).toggle(bsp::led::green);
        }
    };
}

void init_shared_memory() {
    mcu::usb_audio().set_actual_rate(47991);
    g_shared.set_sample_rate(bsp::audio::default_sample_rate);
    g_shared.buffer_size = mcu::audio::buffer_size;
    g_shared.sample_position = 0;
}

void init_loader(uint8_t* app_ram_start, uintptr_t app_ram_size) {
    g_loader.set_app_memory(app_ram_start, app_ram_size);
    g_loader.set_shared_memory(&g_shared);
}

void* load_app(const uint8_t* app_image_start) {
    const auto* app_header = reinterpret_cast<const AppHeader*>(app_image_start);
    if (!app_header->valid_magic() || !app_header->compatible_abi()) {
        return nullptr;
    }

    uintptr_t entry_addr = reinterpret_cast<uintptr_t>(app_header->entry_point(app_image_start)) | 1;
    auto app_entry = reinterpret_cast<void (*)()>(entry_addr);
    g_loader.set_entry(app_entry);

    return reinterpret_cast<void*>(app_entry);
}

// ============================================================================
// Scheduler Callbacks
// ============================================================================

static void tick_callback() {
    g_tick_us += 1000;

    if (!g_rtos_started) {
        return;
    }

    g_kernel.tick(1000);

    // System task: wake every tick (PDM polling, sample rate change)
    // resume_task() internally calls schedule() → request_context_switch() if needed
    g_kernel.resume_task(g_system_task_id);

    // Control task: timed wakeup or periodic poll when no timeout is set.
    // This matches the old behavior where control ran regularly to drive coroutines.
    if (g_control_task_wakeup_us == 0 || g_tick_us >= g_control_task_wakeup_us) {
        g_control_task_wakeup_us = 0;
        g_kernel.resume_task(g_control_task_id);
    }
}

static void switch_context_callback() {
    auto next_opt = g_kernel.get_next_task();
    if (next_opt.has_value()) {
        g_kernel.prepare_switch(*next_opt);
        auto* next_hw_tcb = task_id_to_hw_tcb(*next_opt);
        g_current_tcb = next_hw_tcb;
        arch::cm4::current_tcb = next_hw_tcb;
    }
}

static void svc_callback(uint32_t* sp) {
    svc_handler_impl(sp);
}

// ============================================================================
// Start RTOS
// ============================================================================

void start_rtos(void* app_entry) {
    // Create tasks in Kernel (software scheduling via bitmap)
    g_audio_task_id = g_kernel.create_task({
        .entry = audio_task_entry,
        .arg = nullptr,
        .prio = umi::Priority::Realtime,
        .fpu_policy = umi::FpuPolicy::LazyStack,
        .name = "audio",
    });

    g_system_task_id = g_kernel.create_task({
        .entry = system_task_entry,
        .arg = nullptr,
        .prio = umi::Priority::Server,
        .fpu_policy = umi::FpuPolicy::Forbidden,
        .name = "system",
    });

    g_control_task_id = g_kernel.create_task({
        .entry = control_task_entry,
        .arg = app_entry,
        .prio = umi::Priority::User,
        .fpu_policy = umi::FpuPolicy::LazyStack,
        .name = "control",
    });

    g_idle_task_id = g_kernel.create_task({
        .entry = idle_task_entry,
        .arg = nullptr,
        .prio = umi::Priority::Idle,
        .fpu_policy = umi::FpuPolicy::Forbidden,
        .name = "idle",
    });

    // Initialize hardware TCBs (port layer)
    arch::cm4::init_task(
        g_audio_tcb, g_audio_task_stack, AUDIO_TASK_STACK_SIZE, audio_task_entry, nullptr, true);

    arch::cm4::init_task(
        g_system_tcb, g_system_task_stack, SYSTEM_TASK_STACK_SIZE, system_task_entry, nullptr, false);

    arch::cm4::init_task(g_control_tcb,
                         g_control_task_stack,
                         CONTROL_TASK_STACK_SIZE,
                         control_task_entry,
                         app_entry,
                         true);

    arch::cm4::init_task(
        g_idle_tcb, g_idle_task_stack, IDLE_TASK_STACK_SIZE, idle_task_entry, nullptr, false);

    // Tasks start Ready from create_task(). Audio and system tasks will
    // self-block on first iteration (wait_block / suspend_task respectively),
    // so no explicit suspend is needed here. Suspending before wait_block()
    // would leave wait_mask=0, preventing notify() from waking them.

    // Control task starts as Running — set Kernel's current_per_core[0]
    g_kernel.prepare_switch(g_control_task_id.value);

    g_current_tcb = &g_control_tcb;

    // Set arch layer callbacks
    arch::cm4::set_tick_callback(tick_callback);
    arch::cm4::set_switch_context_callback(switch_context_callback);
    arch::cm4::set_svc_callback(svc_callback);

    g_rtos_started = true;

    // Start scheduler (does not return)
    uint32_t* control_stack_top = g_control_task_stack + CONTROL_TASK_STACK_SIZE;
    arch::cm4::start_scheduler(&g_control_tcb, control_task_entry, app_entry, control_stack_top);
}

} // namespace umi::kernel

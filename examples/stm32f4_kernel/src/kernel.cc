// SPDX-License-Identifier: MIT
// STM32F4 Kernel Implementation
// RTOS scheduler, syscall handling, audio processing
// Uses umi::Kernel (scheduler/state), SpscQueue (IPC) from lib/umios

#include "kernel.hh"

#include <app_header.hh>
#include <array>
#include <cstdint>
#include <cstring>
#include <loader.hh>
#include <span>
#include <umios/app/syscall.hh>
#include <umios/core/audio_context.hh>
#include <umios/core/event_router.hh>
#include <umios/kernel/fpu_policy.hh>
#include <umios/kernel/umi_kernel.hh>

#include "arch.hh"
#include "bsp.hh"
#include "mcu.hh"

// USB stack (needed for complete type)
#include <audio/audio_interface.hh>
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
    static inline volatile uint32_t nest_count_ = 0;
    static inline volatile uint32_t saved_primask_ = 0;

    static void enter_critical() {
        uint32_t prev;
        __asm__ volatile("mrs %0, primask" : "=r"(prev)::"memory");
        __asm__ volatile("cpsid i" ::: "memory");
        if (nest_count_ == 0)
            saved_primask_ = prev;
        nest_count_ = nest_count_ + 1;
    }
    static void exit_critical() {
        nest_count_ = nest_count_ - 1;
        if (nest_count_ == 0) {
            uint32_t restore = saved_primask_;
            __asm__ volatile("msr primask, %0" ::"r"(restore) : "memory");
        }
    }

    static void trigger_ipi(std::uint8_t) {}
    static std::uint8_t current_core() { return 0; }

    static void request_context_switch() { arch::cm4::request_context_switch(); }

    static void enter_sleep() { __asm__ volatile("wfi"); }
    static std::uint32_t cycle_count() { return arch::cm4::dwt_cycle(); }
    static std::uint32_t cycles_per_usec() { return 168; }
};

using HW = umi::Hw<Stm32F4Hw>;

// ============================================================================
// Kernel Instance
// ============================================================================

umi::Kernel<8, 4, HW, 1> g_kernel;

// Compile-time FPU policy determination
static constexpr umi::TaskFpuDecl fpu_decl {
    .audio   = true,
    .system  = false,
    .control = true,
    .idle    = false,
};
static constexpr int fpu_task_count = umi::count_fpu_tasks(fpu_decl);
static constexpr auto audio_fpu_policy   = umi::resolve_fpu_policy(fpu_decl.audio,   fpu_task_count);
static constexpr auto system_fpu_policy  = umi::resolve_fpu_policy(fpu_decl.system,  fpu_task_count);
static constexpr auto control_fpu_policy = umi::resolve_fpu_policy(fpu_decl.control, fpu_task_count);
static constexpr auto idle_fpu_policy    = umi::resolve_fpu_policy(fpu_decl.idle,    fpu_task_count);

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

umi::SpscQueue<uint16_t*, 4> g_audio_ready_queue;

// EventRouter (Phase 3c)
static EventRouter g_event_router;
static ControlEventQueue<> g_control_event_queue;
static EventQueue<INPUT_EVENT_CAPACITY> g_router_audio_queue;
static RouteTable g_route_table = RouteTable::make_default();

// ============================================================================
// Global State
// ============================================================================

volatile bool g_rtos_started = false;

// PDM state
volatile bool g_pdm_ready = false;
volatile uint16_t* g_active_pdm_buf = nullptr;

// Timing / sample rate
volatile uint64_t g_tick_us = 0;
volatile uint32_t g_current_sample_rate = bsp::audio::default_sample_rate;
volatile bool g_dma_stopped_for_rate_change = false;

// Control task wakeup (for timed wait)
volatile uint64_t g_control_task_wakeup_us = 0;
volatile bool g_control_task_waiting = false;
volatile uint32_t g_control_task_wait_mask = 0;
volatile uint32_t g_last_wait_bits = 0;

AppLoader g_loader;
__attribute__((section(".shared"))) SharedMemory g_shared;

// LED override: kernel can force specific LEDs regardless of app state
volatile uint8_t g_led_override = 0;       // Kernel forced LED bits
volatile uint8_t g_led_override_mask = 0xFF; // Bits under kernel control (default: all)

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
    case 0:
        return &g_audio_tcb;
    case 1:
        return &g_system_tcb;
    case 2:
        return &g_control_tcb;
    case 3:
        return &g_idle_tcb;
    default:
        return &g_idle_tcb;
    }
}

// ============================================================================
// Shell Output
// ============================================================================

static void shell_write(const char* str) {
    auto len = std::strlen(str);
    g_stdio.write_stdout(reinterpret_cast<const uint8_t*>(str), len, [](const uint8_t* data, size_t len) {
        mcu::usb_audio().send_sysex(mcu::usb_hal(), data, static_cast<uint16_t>(len));
    });
}

// ============================================================================
// Audio Processing
// ============================================================================

// Debug: DWT cycle counter for audio latency measurement
static volatile uint32_t g_dbg_isr_cycle = 0;         // DWT cycle at ISR entry
static volatile uint32_t g_dbg_latency_max = 0;       // Max ISR→task latency (cycles)
static volatile uint32_t g_dbg_process_max = 0;       // Max process_audio_frame time (cycles)
static volatile uint32_t g_dbg_audio_overrun = 0;     // Queue full count (missed buffers)
static volatile uint32_t g_dbg_audio_frame_count = 0; // Total frames processed

// Per-section timing (max cycles)
static volatile uint32_t g_dbg_t_pdm = 0;     // PDM decimation
static volatile uint32_t g_dbg_t_usb_out = 0; // read_audio only
static volatile uint32_t g_dbg_t_pack = 0;    // pack_i2s_24 only
static volatile uint32_t g_dbg_t_app = 0;     // app process + synth output
static volatile uint32_t g_dbg_t_usb_in = 0;  // write_audio_in

// USB OUT ring buffer level tracking
static volatile uint32_t g_dbg_usb_out_level_min = 0xFFFFFFFF;
static volatile uint32_t g_dbg_usb_out_level_max = 0;
static volatile uint32_t g_dbg_usb_out_underrun = 0;     // true underrun (primed but read < requested)
static volatile uint32_t g_dbg_usb_out_priming_miss = 0; // priming period (not yet primed)
static volatile uint32_t g_dbg_fb_value = 0;             // feedback value (10.14 fixed-point for UAC1)
static volatile float g_dbg_fb_rate = 0;                 // feedback rate in Hz
static volatile uint32_t g_dbg_usb_configured = 0;
static volatile uint32_t g_dbg_midi_configured = 0;
static volatile uint32_t g_dbg_usb_out_streaming = 0;
static volatile uint32_t g_dbg_usb_in_streaming = 0;
static volatile uint32_t g_dbg_uac2_get_cur = 0;
static volatile uint32_t g_dbg_uac2_set_cur = 0;
static volatile uint32_t g_dbg_uac2_get_range = 0;
static volatile uint32_t g_dbg_set_iface_count = 0;
static volatile uint32_t g_dbg_last_set_iface = 0;
static volatile uint32_t g_dbg_last_set_alt = 0;
static volatile uint32_t g_dbg_fb_sent_count = 0;
static volatile uint32_t g_dbg_out_rx_count = 0;
static volatile uint32_t g_dbg_hal_ep1_out_count = 0;
static volatile uint32_t g_dbg_hal_poll_count = 0; // Total IRQ/poll calls
static volatile uint32_t g_dbg_hal_sof_count = 0;
static volatile uint32_t g_dbg_hal_last_gintsts = 0;       // GINTSTS at last SOF
static volatile uint32_t g_dbg_hal_last_gintmsk = 0;       // GINTMSK at last SOF
static volatile uint32_t g_dbg_hal_last_gints = 0;         // gints at last SOF
static volatile uint32_t g_dbg_hal_sof_after_clear = 0;    // SOF flag after clear
static volatile uint32_t g_dbg_hal_sof_callback_count = 0; // Deduplicated SOF callbacks
static volatile uint32_t g_dbg_hal_sof_skip_count = 0;     // SOF skipped due to same frame
static volatile uint32_t g_dbg_ep_cfg_out_count = 0;
static volatile uint32_t g_dbg_ep1_cfg_count = 0;
static volatile uint32_t g_dbg_ep1_doepctl = 0;
static volatile uint32_t g_dbg_ep1_xfrc_count = 0;
static volatile uint32_t g_dbg_ep1_otepdis_count = 0;
static volatile uint32_t g_dbg_ep1_nak_count = 0;
static volatile uint32_t g_dbg_ep1_zero_count = 0;
static volatile uint32_t g_dbg_out_iface_num = 0;
static volatile uint32_t g_dbg_has_out = 0;
static volatile uint32_t g_dbg_out_match = 0;
static volatile uint32_t g_dbg_alt_valid = 0;
static volatile uint32_t g_dbg_last_alt_check = 0;
static volatile uint32_t g_dbg_alt_count = 0;
static volatile uint32_t g_dbg_read_total = 0;
static volatile uint32_t g_dbg_read_prime_fail = 0;
static volatile uint32_t g_dbg_read_prime_success = 0;
static volatile int32_t g_dbg_read_prime_level = 0;
static volatile int32_t g_dbg_read_prime_threshold = 0;
static volatile uint32_t g_dbg_on_rx_called = 0;
static volatile uint32_t g_dbg_on_rx_passed = 0;
static volatile uint8_t g_dbg_on_rx_last_ep = 0xFF;
static volatile uint32_t g_dbg_on_rx_last_len = 0;
static volatile uint8_t g_dbg_on_rx_has_out = 0;
static volatile uint8_t g_dbg_on_rx_ep_match = 0;
static volatile uint8_t g_dbg_on_rx_streaming = 0;
static volatile uint16_t g_dbg_on_rx_bytes_per_frame = 0;
static volatile uint8_t g_dbg_on_rx_bit_depth = 0;
static volatile uint32_t g_dbg_on_rx_bpf_zero_count = 0;
static volatile uint32_t g_dbg_on_rx_blocked_count = 0;
static volatile uint32_t g_dbg_on_rx_discard_count = 0;
static volatile uint32_t g_dbg_on_rx_processing = 0;
static volatile uint32_t g_dbg_on_sof_called = 0;
static volatile uint32_t g_dbg_on_sof_streaming = 0;
static volatile uint32_t g_dbg_on_sof_decrement = 0;
static volatile uint32_t g_dbg_configure_sync_windows_count = 0;
static volatile uint32_t g_dbg_in_buffered = 0;
static volatile uint32_t g_dbg_send_audio_in_count = 0;
static volatile uint32_t g_dbg_in_underrun = 0;
static volatile uint32_t g_dbg_in_overrun = 0;

static volatile uint32_t g_dbg_in_write_count = 0;     // write_audio_in call count
static volatile uint32_t g_dbg_dma_callback_count = 0; // DMA callback count
static volatile uint32_t g_dbg_out_mute = 0;
static volatile int32_t g_dbg_out_volume = 0;
static volatile uint32_t g_dbg_out_read_count = 0;     // read_audio return value
static volatile int32_t g_dbg_out_sample_max = 0;      // max abs sample in i2s_work_buf
static volatile uint32_t g_dbg_out_raw0 = 0;           // first 4 bytes of USB RX packet
static volatile uint32_t g_dbg_out_raw1 = 0;           // next 4 bytes of USB RX packet
static volatile uint32_t g_dbg_out_last_sample_l = 0;  // last decoded sample L
static volatile uint32_t g_dbg_hal_ep1_first_word = 0; // HAL: first word from EP1 RX buf
static volatile uint32_t g_dbg_out_read_zero_count = 0; // count of read_count==0
static volatile uint32_t g_dbg_out_read_short_count = 0; // count of read_count < buffer_size
static volatile int32_t g_dbg_out_decoded0 = 0;  // first decoded sample from on_rx
static volatile int32_t g_dbg_out_decoded1 = 0;  // second decoded sample from on_rx
static volatile uint32_t g_dbg_out_buf_addr = 0; // DMA buf pointer passed to process_audio_frame
static volatile uint32_t g_dbg_out_buf_word0 = 0; // first word written to DMA buf after pack
static volatile int32_t g_dbg_out_sample_max_ever = 0; // persistent max (never reset by process)
static volatile uint32_t g_dbg_out_nonzero_frames = 0; // count of frames where sample_max > 0
static volatile uint32_t g_dbg_out_zero_frames = 0;    // count of frames where sample_max == 0
static volatile uint32_t g_dbg_out_raw0_nonzero = 0;   // count of on_rx with raw0 != 0
static volatile uint32_t g_dbg_hal_ep1_zero_count = 0;  // HAL: EP1 OUT_DATA with bcnt==0
static volatile uint32_t g_dbg_hal_ep1_bcnt_last = 0;   // HAL: last EP1 bcnt
static volatile uint32_t g_dbg_hal_ep1_bcnt_min = 0;    // HAL: min EP1 bcnt
static volatile uint32_t g_dbg_hal_ep1_bcnt_max = 0;    // HAL: max EP1 bcnt
static volatile uint32_t g_dbg_hal_rxflvl_out = 0;      // HAL: RXFLVL OUT_DATA total
static volatile uint32_t g_dbg_ring_write_pos = 0;
static volatile uint32_t g_dbg_ring_read_pos = 0;
static volatile uint32_t g_dbg_ring_buffered = 0;
static volatile uint32_t g_dbg_ring_overrun = 0;
static volatile uint32_t g_dbg_ring_underrun = 0;

// Event log for debugging USB sequence
// Each entry: [event_type(8) | data(24)]
// Types: 1=SET_CUR(rate/1000), 2=SET_IFACE_OUT(alt), 3=SET_IFACE_IN(alt),
//        4=streaming_change(0/1), 5=apply_rate(rate/1000), 6=restart_dma
static constexpr uint32_t EVT_LOG_SIZE = 64;
static volatile uint32_t g_evt_log[EVT_LOG_SIZE * 2] = {}; // [timestamp, event] pairs
static volatile uint32_t g_evt_log_idx = 0;

static void evt_log(uint8_t type, uint32_t data) {
    uint32_t idx = g_evt_log_idx % EVT_LOG_SIZE;
    uint32_t base = idx * 2;
    g_evt_log[base] = *reinterpret_cast<volatile uint32_t*>(0xE0001004); // DWT CYCCNT
    g_evt_log[base + 1] = (static_cast<uint32_t>(type) << 24) | (data & 0xFFFFFF);
    g_evt_log_idx = g_evt_log_idx + 1;
}

static inline uint32_t dwt_cyccnt() {
    return *reinterpret_cast<volatile uint32_t*>(0xE0001004);
}

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
    g_dbg_out_buf_addr = reinterpret_cast<uint32_t>(buf);
    g_dbg_dma_callback_count = g_dbg_dma_callback_count + 1;
    const float dt = g_shared.dt;

    // --- Section 1: PDM decimation ---
    uint32_t t1 = dwt_cyccnt();
    if (g_pdm_ready && g_current_sample_rate < 96000) {
        g_pdm_ready = false;
        uint16_t* pdm_data = const_cast<uint16_t*>(g_active_pdm_buf);
        mcu::cic_decimator().process_buffer(
            pdm_data, mcu::audio::pdm_buf_size, mcu::pcm_buf(), mcu::audio::pcm_buf_size);
    }
    uint32_t t2 = dwt_cyccnt();

    // --- Section 2: USB Audio OUT read + I2S pack ---
    bool is_streaming = mcu::usb_audio().is_streaming();
    if (is_streaming) {
        uint32_t lvl = mcu::usb_audio().buffered_frames();
        if (lvl < g_dbg_usb_out_level_min)
            g_dbg_usb_out_level_min = lvl;
        if (lvl > g_dbg_usb_out_level_max)
            g_dbg_usb_out_level_max = lvl;
    }
    bool was_primed = mcu::usb_audio().is_out_primed();
#ifdef USB_AUDIO_ADAPTIVE
    uint32_t read_count = mcu::usb_audio().read_audio_asrc(i2s_work_buf, mcu::audio::buffer_size);
#else
    uint32_t read_count = mcu::usb_audio().read_audio(i2s_work_buf, mcu::audio::buffer_size);
#endif
    if (is_streaming && read_count < mcu::audio::buffer_size) {
        if (was_primed) {
            g_dbg_usb_out_underrun = g_dbg_usb_out_underrun + 1;
        } else {
            g_dbg_usb_out_priming_miss = g_dbg_usb_out_priming_miss + 1;
        }
    }
    // Update feedback debug values
    g_dbg_fb_value = mcu::usb_audio().current_feedback();
    g_dbg_fb_rate = mcu::usb_audio().feedback_rate();
    g_dbg_out_mute = mcu::usb_audio().is_muted() ? 1 : 0;
    g_dbg_out_volume = mcu::usb_audio().volume_db256();
    g_dbg_out_read_count = read_count;
    if (read_count == 0) g_dbg_out_read_zero_count = g_dbg_out_read_zero_count + 1;
    if (read_count < mcu::audio::buffer_size) g_dbg_out_read_short_count = g_dbg_out_read_short_count + 1;
    // Track max abs sample in i2s_work_buf
    {
        int32_t max_abs = 0;
        for (uint32_t i = 0; i < read_count * 2; ++i) {
            int32_t v = i2s_work_buf[i];
            if (v < 0) v = -v;
            if (v > max_abs) max_abs = v;
        }
        g_dbg_out_sample_max = max_abs;
        if (max_abs > g_dbg_out_sample_max_ever) g_dbg_out_sample_max_ever = max_abs;
        if (max_abs > 0) {
            g_dbg_out_nonzero_frames = g_dbg_out_nonzero_frames + 1;
        } else {
            g_dbg_out_zero_frames = g_dbg_out_zero_frames + 1;
        }
    }
    g_dbg_out_raw0 = mcu::usb_audio().dbg_out_rx_packet_raw0();
    if (g_dbg_out_raw0 != 0) g_dbg_out_raw0_nonzero = g_dbg_out_raw0_nonzero + 1;
    g_dbg_out_raw1 = mcu::usb_audio().dbg_out_rx_packet_raw1();
    g_dbg_out_last_sample_l = static_cast<uint32_t>(mcu::usb_audio().dbg_out_rx_last_sample_l());
    g_dbg_hal_ep1_first_word = mcu::usb_hal().dbg_ep1_out_first_word_;
    g_dbg_out_decoded0 = mcu::usb_audio().dbg_out_decoded_sample0();
    g_dbg_out_decoded1 = mcu::usb_audio().dbg_out_decoded_sample1();
    g_dbg_hal_ep1_zero_count = mcu::usb_hal().dbg_ep1_out_zero_count_;
    g_dbg_hal_ep1_bcnt_last = mcu::usb_hal().dbg_ep1_out_bcnt_last_;
    g_dbg_hal_ep1_bcnt_min = mcu::usb_hal().dbg_ep1_out_bcnt_min_;
    g_dbg_hal_ep1_bcnt_max = mcu::usb_hal().dbg_ep1_out_bcnt_max_;
    g_dbg_hal_rxflvl_out = mcu::usb_hal().dbg_rxflvl_out_count_;
    g_dbg_ring_write_pos = mcu::usb_audio().out_ring_write_pos();
    g_dbg_ring_read_pos = mcu::usb_audio().out_ring_read_pos();
    g_dbg_ring_buffered = mcu::usb_audio().buffered_frames();
    g_dbg_ring_overrun = mcu::usb_audio().out_ring_overrun();
    g_dbg_ring_underrun = mcu::usb_audio().out_ring_underrun();

    uint32_t t2b = dwt_cyccnt();
#if 0 // TEST TONE: bypass USB audio, output 440Hz sine via I2S
    {
        static uint32_t tone_phase = 0;
        // 440Hz sine wave at 48kHz, 24-bit amplitude
        // Phase increment: 440/48000 * 2^32 ≈ 39370534
        constexpr uint32_t phase_inc = 39370534;
        for (uint32_t i = 0; i < mcu::audio::buffer_size; ++i) {
            // Simple sine approximation: 3rd-order polynomial
            // sin(x) ≈ x - x^3/6 for x in [-pi, pi]
            int32_t phase_s = static_cast<int32_t>(tone_phase); // -2^31 to 2^31
            // Scale to [-1, 1] in Q31, then to 24-bit
            int64_t x = phase_s; // Q31
            int64_t x3 = (x * x >> 31) * x >> 31;
            int64_t sine_q31 = (3 * x - (x3 >> 1)) >> 1; // rough sine approx
            int32_t sample = static_cast<int32_t>(sine_q31 >> 8); // Q31 -> Q23
            i2s_work_buf[i * 2] = sample;
            i2s_work_buf[i * 2 + 1] = sample;
            tone_phase += phase_inc;
        }
    }
#endif
    pack_i2s_24(buf, i2s_work_buf, mcu::audio::buffer_size, 2);
    g_dbg_out_buf_word0 = *reinterpret_cast<volatile uint32_t*>(buf);
    uint32_t t3 = dwt_cyccnt();

    // --- Section 3: App process + synth output ---
    if (g_loader.state() == AppState::Running) {
        for (uint32_t i = 0; i < mcu::audio::buffer_size; ++i) {
            synth_out_mono[i] = 0.0f;
        }

        std::array<Event, INPUT_EVENT_CAPACITY> input_events{};
        size_t input_event_count = 0;

        // Drain EventRouter audio queue (routed MIDI + button events with sample-accurate timing)
        {
            Event ev;
            while (input_event_count < input_events.size() && g_router_audio_queue.pop(ev)) {
                input_events[input_event_count++] = ev;
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
            &g_shared.param_state,
            &g_shared.channel_state,
            &g_shared.input_state,
            0,
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
    uint32_t t4 = dwt_cyccnt();

    // --- Section 4: USB Audio IN write ---
    if (mcu::usb_audio().is_audio_in_streaming()) {
        // Write directly as int32_t to avoid i16→i32 conversion in write_audio_in_overwrite
        static int32_t in_buf_i32[mcu::audio::buffer_size * 2];
        if (g_current_sample_rate >= 96000) {
            __builtin_memset(in_buf_i32, 0, sizeof(in_buf_i32));
        } else {
            for (uint32_t i = 0; i < mcu::audio::buffer_size; ++i) {
                in_buf_i32[i * 2] = static_cast<int32_t>(mcu::pcm_buf()[i]) << 8;
                in_buf_i32[i * 2 + 1] = static_cast<int32_t>(last_synth_out[i * 2]) << 8;
            }
        }

        g_dbg_in_write_count = g_dbg_in_write_count + 1;

        // Write to ring buffer only; USB IN send is driven by SOF (1kHz)
        mcu::usb_audio().write_audio_in(in_buf_i32, mcu::audio::buffer_size);
    }
    uint32_t t5 = dwt_cyccnt();

    // Update per-section max timing
    uint32_t d_pdm = t2 - t1;
    uint32_t d_usb_out = t2b - t2;
    uint32_t d_pack = t3 - t2b;
    uint32_t d_app = t4 - t3;
    uint32_t d_usb_in = t5 - t4;
    if (d_pdm > g_dbg_t_pdm)
        g_dbg_t_pdm = d_pdm;
    if (d_usb_out > g_dbg_t_usb_out)
        g_dbg_t_usb_out = d_usb_out;
    if (d_pack > g_dbg_t_pack)
        g_dbg_t_pack = d_pack;
    if (d_app > g_dbg_t_app)
        g_dbg_t_app = d_app;
    if (d_usb_in > g_dbg_t_usb_in)
        g_dbg_t_usb_in = d_usb_in;
}

// ============================================================================
// Task Entries
// ============================================================================

static void audio_task_entry(void* arg) {
    (void)arg;

    while (true) {
        g_kernel.wait_block(g_audio_task_id, KernelEvent::AudioReady);

        // NOTE: Sample rate change is handled synchronously in USB ISR
        // (sample_rate_callback → apply_sample_rate) so that PLLI2S is
        // reconfigured before the host sends SET_INTERFACE.

        while (auto buf_opt = g_audio_ready_queue.try_pop()) {
            uint32_t t0 = dwt_cyccnt();
            uint32_t latency = t0 - g_dbg_isr_cycle;
            if (latency > g_dbg_latency_max)
                g_dbg_latency_max = latency;

            process_audio_frame(*buf_opt);

            uint32_t elapsed = dwt_cyccnt() - t0;
            if (elapsed > g_dbg_process_max)
                g_dbg_process_max = elapsed;
            g_dbg_audio_frame_count = g_dbg_audio_frame_count + 1;
        }
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
        // NOTE: Sample rate change is handled synchronously in USB ISR
        // (sample_rate_callback → apply_sample_rate).

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

/// Reconfigure PLLI2S for a new sample rate.
/// Called synchronously from USB ISR (sample_rate_callback).
/// Only stops DMA/I2S and reconfigures the PLL — does NOT restart.
/// DMA/I2S restart happens in restart_audio_dma(), called from
/// on_streaming_change(true) when SET_INTERFACE re-enables streaming.
static void apply_sample_rate(uint32_t new_rate) {
    if (new_rate == g_current_sample_rate) {
        return;
    }
    if (new_rate != 44100 && new_rate != 48000 && new_rate != 96000) {
        return;
    }

    evt_log(5, new_rate / 1000);

    mcu::disable_i2s_irq();
    mcu::dma_i2s_disable();
    arch::cm4::delay_cycles(1000);
    mcu::i2s_disable();

    for (uint32_t i = 0; i < mcu::audio::i2s_dma_words; ++i) {
        mcu::audio_buf0()[i] = 0;
        mcu::audio_buf1()[i] = 0;
    }

    uint32_t actual_rate = mcu::configure_plli2s(new_rate);

    // Drain audio queue — stale buffers from old rate
    while (g_audio_ready_queue.try_pop()) {
    }

    g_current_sample_rate = new_rate;
    g_shared.set_sample_rate(new_rate);

    mcu::usb_audio().set_actual_rate(actual_rate);
    mcu::usb_audio().reset_audio_out(actual_rate);
    // Clear sample_rate_changed_ so that the subsequent set_interface()
    // does NOT reset the ring buffer again. The reset above is sufficient;
    // a second reset in set_interface() would empty the buffer right after
    // we start receiving new-rate packets, causing prolonged underruns.
    mcu::usb_audio().clear_sample_rate_changed();

    // DMA/I2S intentionally left stopped.
    // restart_audio_dma() will re-enable them when streaming resumes.
    g_dma_stopped_for_rate_change = true;
}

/// Restart DMA/I2S after sample rate change.
/// Called from on_streaming_change(true) in USB ISR context.
/// Only acts if apply_sample_rate() previously stopped DMA.
static void restart_audio_dma() {
    if (!g_dma_stopped_for_rate_change) {
        return;
    }
    g_dma_stopped_for_rate_change = false;
    evt_log(6, 0);
    mcu::dma_i2s_init();
    mcu::enable_i2s_irq();
    mcu::i2s_enable_dma();
    mcu::i2s_enable();
    mcu::dma_i2s_enable();
}

// ============================================================================
// ISR Callbacks
// ============================================================================

void on_audio_buffer_ready(uint16_t* buf) {
    if (!g_rtos_started) {
        return;
    }

    g_dbg_isr_cycle = dwt_cyccnt();
    if (!g_audio_ready_queue.try_push(buf)) {
        g_dbg_audio_overrun = g_dbg_audio_overrun + 1;
    }
    g_kernel.notify(g_audio_task_id, KernelEvent::AudioReady);
}

void on_pdm_buffer_ready(uint16_t* buf) {
    g_active_pdm_buf = buf;
    g_pdm_ready = true;
}

// ============================================================================
// Syscall Handler
// ============================================================================

// Syscall numbers — must match lib/umios/app/syscall.hh nr::*
namespace app_syscall {
inline constexpr uint32_t exit = 0;
inline constexpr uint32_t yield = 1;
inline constexpr uint32_t wait_event = 2;
inline constexpr uint32_t get_time = 3;
inline constexpr uint32_t get_shared = 4;
inline constexpr uint32_t register_proc = 5;
inline constexpr uint32_t set_route_table = 20;
inline constexpr uint32_t set_param_mapping = 21;
inline constexpr uint32_t set_input_mapping = 22;
inline constexpr uint32_t configure_input = 23;
inline constexpr uint32_t set_app_config = 24;
inline constexpr uint32_t send_param_request = 25;
} // namespace app_syscall

static void svc_handler_impl(uint32_t* sp) {
    // Cortex-M exception frame: {r0, r1, r2, r3, r12, lr, pc, xpsr}
    // r12 convention: sp[4] = syscall number, sp[0]-sp[3] = arguments
    uint32_t syscall_nr = sp[4];
    uint32_t arg0 = sp[0];
    uint32_t arg1 = sp[1];
    int32_t result = 0;

    switch (syscall_nr) {
    case app_syscall::exit:
        g_loader.terminate(static_cast<int>(arg0));
        result = 0;
        break;

    case app_syscall::yield:
        arch::cm4::request_context_switch();
        result = 0;
        break;

    case app_syscall::wait_event: {
        uint32_t mask = arg0;
        uint64_t timeout_us = arg1;
        g_control_task_waiting = true;
        g_control_task_wait_mask = mask;
        if (g_last_wait_bits == 0) {
            g_last_wait_bits = 0x80000000u | mask;
        }
        if (timeout_us > 0) {
            g_control_task_wakeup_us = g_tick_us + timeout_us;
        } else {
            g_control_task_wakeup_us = 0;
        }
        uint32_t bits = g_kernel.wait_block(g_control_task_id, mask);
        if (bits != 0 && (g_last_wait_bits & 0x80000000u) != 0) {
            g_last_wait_bits = bits;
        }
        g_control_task_wakeup_us = 0;
        g_control_task_waiting = false;
        g_control_task_wait_mask = 0;
        sp[0] = bits;
        return;
    }

    case app_syscall::get_time:
        sp[0] = static_cast<uint32_t>(g_tick_us & 0xFFFFFFFFu);
        sp[1] = static_cast<uint32_t>(g_tick_us >> 32);
        return;

    case app_syscall::get_shared:
        sp[0] = reinterpret_cast<uint32_t>(&g_shared);
        return;

    case app_syscall::register_proc:
        if (arg1 != 0) {
            result = g_loader.register_processor(reinterpret_cast<void*>(arg0), reinterpret_cast<ProcessFn>(arg1));
        } else {
            result = g_loader.register_processor(reinterpret_cast<void*>(arg0));
        }
        break;

    case app_syscall::set_route_table: {
        auto* table = reinterpret_cast<const RouteTable*>(arg0);
        if (table != nullptr) {
            g_route_table = *table;
        }
        result = 0;
        break;
    }

    case app_syscall::set_param_mapping: {
        // Store pointer directly — app memory is accessible to kernel
        auto* mapping = reinterpret_cast<const ParamMapping*>(arg0);
        g_event_router.set_param_mapping(mapping);
        result = 0;
        break;
    }

    case app_syscall::set_input_mapping: {
        // TODO: implement InputParamMapping storage
        result = 0;
        break;
    }

    case app_syscall::configure_input: {
        // TODO: implement InputConfig storage
        result = 0;
        break;
    }

    case app_syscall::set_app_config: {
        // TODO: implement full AppConfig (requires more RAM for double-buffer)
        result = 0;
        break;
    }

    case app_syscall::send_param_request: {
        if (arg0 < SharedParamState::MAX_PARAMS) {
            float value;
            __builtin_memcpy(&value, &arg1, sizeof(value));
            g_shared.param_state.values[arg0] = value;
            g_shared.param_state.changed_flags |= (1u << arg0);
            ++g_shared.param_state.version;
        }
        result = 0;
        break;
    }

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
        // EventRouter path: create RawInput and route
        RawInput raw{};
        raw.hw_timestamp = static_cast<uint32_t>(g_tick_us & 0xFFFFFFFF);
        raw.source_id = static_cast<uint8_t>(InputSource::USB);
        // USB MIDI: data[0] = CIN/cable, data[1..3] = MIDI bytes
        if (len >= 4) {
            raw.payload[0] = data[1];
            raw.payload[1] = data[2];
            raw.payload[2] = data[3];
            raw.size = 3;
        } else {
            for (uint8_t i = 0; i < len && i < 6; ++i) {
                raw.payload[i] = data[i];
            }
            raw.size = (len > 6) ? 6 : len;
        }
        uint64_t buffer_start = g_shared.sample_position * 1'000'000ULL / g_shared.sample_rate;
        g_event_router.receive(raw, buffer_start, g_shared.sample_rate);

        g_kernel.notify(g_control_task_id, umi::syscall::event::Midi);
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
        evt_log(1, new_rate / 1000);
        apply_sample_rate(new_rate);
    });

    mcu::usb_audio().on_streaming_change = [](bool streaming) {
        evt_log(4, streaming ? 1 : 0);
        if (streaming) {
            restart_audio_dma();
            // Blue LED: kernel override for USB OUT streaming
            g_led_override |= (1 << 3);
        } else {
            // Zero DMA buffers immediately when streaming stops
            // to prevent stale audio from being output by I2S
            for (uint32_t i = 0; i < mcu::audio::i2s_dma_words; ++i) {
                mcu::audio_buf0()[i] = 0;
                mcu::audio_buf1()[i] = 0;
            }
            // Drain audio queue — stale buffers
            while (g_audio_ready_queue.try_pop()) {
            }
            g_led_override &= ~(1 << 3);
        }
    };

    mcu::usb_audio().on_audio_in_change = [](bool streaming) {
        if (streaming) {
            g_led_override |= (1 << 1); // Orange LED: USB IN streaming
        } else {
            g_led_override &= ~(1 << 1);
        }
    };

    mcu::usb_audio().on_audio_rx = []() {
        // No direct GPIO — LED state driven by tick_callback
    };
}

void init_shared_memory() {
    mcu::usb_audio().set_actual_rate(47991);
    g_shared.set_sample_rate(bsp::audio::default_sample_rate);
    g_shared.buffer_size = mcu::audio::buffer_size;
    g_shared.sample_position = 0;

    // Initialize EventRouter
    g_event_router.set_route_table(&g_route_table);
    g_event_router.set_shared_state(&g_shared.param_state, &g_shared.channel_state);
    g_event_router.set_audio_queue(&g_router_audio_queue);
    g_event_router.set_control_queue(&g_control_event_queue);
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

    // App loaded: release LED control to app, keep only Blue (USB streaming) + Orange (USB IN) for kernel
    g_led_override_mask = (1 << 3) | (1 << 1);

    return reinterpret_cast<void*>(app_entry);
}

// ============================================================================
// Scheduler Callbacks
// ============================================================================

static void tick_callback() {
    g_tick_us += 1000;

    g_dbg_usb_configured = mcu::usb_is_configured() ? 1U : 0U;
    g_dbg_midi_configured = mcu::usb_audio().is_midi_configured() ? 1U : 0U;
    g_dbg_usb_out_streaming = mcu::usb_audio().is_streaming() ? 1U : 0U;
    g_dbg_usb_in_streaming = mcu::usb_audio().is_audio_in_streaming() ? 1U : 0U;
    g_dbg_uac2_get_cur = mcu::usb_audio().dbg_uac2_get_cur();
    g_dbg_uac2_set_cur = mcu::usb_audio().dbg_uac2_set_cur();
    g_dbg_uac2_get_range = mcu::usb_audio().dbg_uac2_get_range();
    g_dbg_set_iface_count = mcu::usb_audio().dbg_set_interface_count();
    g_dbg_last_set_iface = mcu::usb_audio().dbg_last_set_iface();
    g_dbg_last_set_alt = mcu::usb_audio().dbg_last_set_alt();
    g_dbg_fb_sent_count = mcu::usb_audio().dbg_fb_sent_count();
    g_dbg_out_rx_count = mcu::usb_audio().dbg_out_rx_packet_index();
    g_dbg_hal_ep1_out_count = mcu::usb_hal().dbg_ep1_out_count_;
    g_dbg_ep1_xfrc_count = mcu::usb_hal().dbg_ep1_doepint_xfrc_count_;
    g_dbg_ep1_otepdis_count = mcu::usb_hal().dbg_ep1_doepint_otepdis_count_;
    g_dbg_ep1_nak_count = mcu::usb_hal().dbg_ep1_doepint_nak_count_;
    g_dbg_ep1_zero_count = mcu::usb_hal().dbg_ep1_out_zero_count_;
    g_dbg_hal_poll_count = mcu::usb_hal().dbg_poll_count_;
    g_dbg_hal_sof_count = mcu::usb_hal().dbg_sof_count_;
    g_dbg_hal_last_gintsts = mcu::usb_hal().dbg_last_gintsts_;
    g_dbg_hal_last_gintmsk = mcu::usb_hal().dbg_last_gintmsk_;
    g_dbg_hal_last_gints = mcu::usb_hal().dbg_last_gints_;
    g_dbg_hal_sof_after_clear = mcu::usb_hal().dbg_sof_after_clear_;
    g_dbg_hal_sof_callback_count = mcu::usb_hal().dbg_sof_callback_count_;
    g_dbg_hal_sof_skip_count = mcu::usb_hal().dbg_sof_skip_count_;
    g_dbg_ep_cfg_out_count = mcu::usb_hal().dbg_ep_configure_out_count_;
    g_dbg_ep1_cfg_count = mcu::usb_hal().dbg_ep1_configure_count_;
    g_dbg_ep1_doepctl = mcu::usb_hal().dbg_ep1_doepctl_after_cfg_;
    g_dbg_out_iface_num = mcu::usb_audio().dbg_audio_out_iface_num();
    g_dbg_has_out = mcu::usb_audio().dbg_set_iface_has_out();
    g_dbg_out_match = mcu::usb_audio().dbg_set_iface_out_match();
    g_dbg_alt_valid = mcu::usb_audio().dbg_set_iface_alt_valid();
    g_dbg_last_alt_check = mcu::usb_audio().dbg_last_alt_check();
    g_dbg_alt_count = mcu::usb_audio().dbg_alt_count();
    g_dbg_read_total = mcu::usb_audio().dbg_read_audio_total();
    g_dbg_read_prime_fail = mcu::usb_audio().dbg_read_prime_fail();
    g_dbg_read_prime_success = mcu::usb_audio().dbg_read_prime_success();
    g_dbg_read_prime_level = mcu::usb_audio().dbg_read_prime_level();
    g_dbg_read_prime_threshold = mcu::usb_audio().dbg_read_prime_threshold();
    g_dbg_on_rx_called = mcu::usb_audio().dbg_on_rx_called();
    g_dbg_on_rx_passed = mcu::usb_audio().dbg_on_rx_passed();
    g_dbg_on_rx_last_ep = mcu::usb_audio().dbg_on_rx_last_ep();
    g_dbg_on_rx_last_len = mcu::usb_audio().dbg_on_rx_last_len();
    g_dbg_on_rx_has_out = mcu::usb_audio().dbg_on_rx_has_out();
    g_dbg_on_rx_ep_match = mcu::usb_audio().dbg_on_rx_ep_match();
    g_dbg_on_rx_streaming = mcu::usb_audio().dbg_on_rx_streaming();
    g_dbg_on_rx_bytes_per_frame = mcu::usb_audio().dbg_on_rx_bytes_per_frame();
    g_dbg_on_rx_bit_depth = mcu::usb_audio().dbg_on_rx_bit_depth();
    g_dbg_on_rx_bpf_zero_count = mcu::usb_audio().dbg_on_rx_bpf_zero_count();
    g_dbg_on_rx_blocked_count = mcu::usb_audio().dbg_on_rx_blocked_count();
    g_dbg_on_rx_discard_count = mcu::usb_audio().dbg_on_rx_discard_count();
    g_dbg_on_rx_processing = mcu::usb_audio().dbg_on_rx_processing();
    g_dbg_on_sof_called = mcu::usb_audio().dbg_on_sof_called();
    g_dbg_on_sof_streaming = mcu::usb_audio().dbg_on_sof_streaming();
    g_dbg_on_sof_decrement = mcu::usb_audio().dbg_on_sof_decrement();
    g_dbg_configure_sync_windows_count = mcu::usb_audio().dbg_configure_sync_windows_count();
    g_dbg_in_buffered = mcu::usb_audio().in_buffered_frames();
    g_dbg_send_audio_in_count = mcu::usb_audio().dbg_send_audio_in_count();
    g_dbg_in_underrun = mcu::usb_audio().in_underrun_count();
    g_dbg_in_overrun = mcu::usb_audio().in_overrun_count();

    if (!g_rtos_started) {
        return;
    }

    // --- Button GPIO read → debounce → EventRouter + SharedInputState ---
    {
        bool raw = mcu::gpio(bsp::button::gpio_port).read(bsp::button::user);
        uint8_t result = g_user_button.update(raw);
        if (result != 0) {
            uint16_t value = (result == 1) ? 0xFFFF : 0x0000;
            g_event_router.receive_input(0, value, true, ROUTE_AUDIO | ROUTE_CONTROL);
            // Update SharedInputState so app controller_task can poll button state
            g_shared.input_state.raw[0] = value;
        }
    }

    // --- LED GPIO drive: merge app state with kernel override ---
    {
        uint8_t app_leds = g_shared.led_state.load(std::memory_order_relaxed);
        uint8_t final_leds = (app_leds & ~g_led_override_mask) | (g_led_override & g_led_override_mask);
        auto& port = mcu::gpio(bsp::led::gpio_port);
        (final_leds & (1 << 0)) ? port.set(bsp::led::green) : port.reset(bsp::led::green);
        (final_leds & (1 << 1)) ? port.set(bsp::led::orange) : port.reset(bsp::led::orange);
        (final_leds & (1 << 2)) ? port.set(bsp::led::red) : port.reset(bsp::led::red);
        (final_leds & (1 << 3)) ? port.set(bsp::led::blue) : port.reset(bsp::led::blue);
    }

    g_kernel.tick(1000);

    // System task: wake every tick (PDM polling, sample rate change)
    // resume_task() internally calls schedule() → request_context_switch() if needed
    g_kernel.resume_task(g_system_task_id);

    // Timer event (ControlTask)
    g_kernel.notify(g_control_task_id, umi::syscall::event::Timer);

    // Control task: timed wakeup or periodic poll when no timeout is set.
    // This matches the old behavior where control ran regularly to drive coroutines.
    if (g_control_task_waiting) {
        if (g_control_task_wakeup_us != 0 && g_tick_us >= g_control_task_wakeup_us) {
            g_control_task_wakeup_us = 0;
            g_kernel.resume_task(g_control_task_id);
        }
    } else {
        g_kernel.resume_task(g_control_task_id);
    }
}

static void switch_context_callback() {
    // PendSV is lowest-priority ISR, but DMA ISR can preempt and
    // modify kernel state (via notify). Use critical section to
    // ensure get_next_task + prepare_switch are atomic.
    Stm32F4Hw::enter_critical();
    auto next_opt = g_kernel.get_next_task();
    if (next_opt.has_value()) {
        g_kernel.prepare_switch(*next_opt);
        auto* next_hw_tcb = task_id_to_hw_tcb(*next_opt);
        g_current_tcb = next_hw_tcb;
        arch::cm4::current_tcb = next_hw_tcb;
    }
    Stm32F4Hw::exit_critical();
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
        .uses_fpu = fpu_decl.audio,
        .name = "audio",
    });

    g_system_task_id = g_kernel.create_task({
        .entry = system_task_entry,
        .arg = nullptr,
        .prio = umi::Priority::Server,
        .uses_fpu = fpu_decl.system,
        .name = "system",
    });

    g_control_task_id = g_kernel.create_task({
        .entry = control_task_entry,
        .arg = app_entry,
        .prio = umi::Priority::User,
        .uses_fpu = fpu_decl.control,
        .name = "control",
    });

    g_idle_task_id = g_kernel.create_task({
        .entry = idle_task_entry,
        .arg = nullptr,
        .prio = umi::Priority::Idle,
        .uses_fpu = fpu_decl.idle,
        .name = "idle",
    });

    // Initialize hardware TCBs (port layer) — FPU policy resolved at compile time
    arch::cm4::init_task<audio_fpu_policy>(
        g_audio_tcb, g_audio_task_stack, AUDIO_TASK_STACK_SIZE, audio_task_entry, nullptr);

    arch::cm4::init_task<system_fpu_policy>(
        g_system_tcb, g_system_task_stack, SYSTEM_TASK_STACK_SIZE, system_task_entry, nullptr);

    arch::cm4::init_task<control_fpu_policy>(
        g_control_tcb, g_control_task_stack, CONTROL_TASK_STACK_SIZE, control_task_entry, app_entry);

    arch::cm4::init_task<idle_fpu_policy>(
        g_idle_tcb, g_idle_task_stack, IDLE_TASK_STACK_SIZE, idle_task_entry, nullptr);

    // System task starts Blocked, wakes on SysTick resume_task().
    // Audio task starts Ready — it immediately calls wait_block(AudioReady)
    // in audio_task_entry(), so it will block before any DMA buffer arrives.
    // NOTE: suspend_task() must NOT be used for audio task because notify()
    // requires wait_mask set by wait_block() to wake a Blocked task.
    g_kernel.suspend_task(g_system_task_id);

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

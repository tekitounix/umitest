// SPDX-License-Identifier: MIT
// STM32F4-Discovery Kernel with Audio/USB Support
// Loads and runs .umiapp applications, handles audio I/O and USB MIDI
// Full feature parity with original stm32f4_synth:
// - PDM Microphone input (CIC decimation @ 47,991Hz - same as I2S)
// - USB Audio IN: L=mic, R=synth
// - USB Audio OUT -> I2S output
// - USB MIDI -> App via syscall
// PDM and I2S are now perfectly synchronized - no resampling needed!

#include <array>
#include <cstdint>
#include <cstring>
#include <span>

// Kernel components
#include <app_header.hh>
#include <loader.hh>
#include <mpu_config.hh>
#include <port/cm4/context.hh>
#include <umios/backend/cm/platform/privilege.hh>
#include <umios/core/audio_context.hh>

// Platform drivers
#include <umios/backend/cm/common/irq.hh>
#include <umios/backend/cm/common/nvic.hh>
#include <umios/backend/cm/common/scb.hh>
#include <umios/backend/cm/stm32f4/cs43l22.hh>
#include <umios/backend/cm/stm32f4/gpio.hh>
#include <umios/backend/cm/stm32f4/i2c.hh>
#include <umios/backend/cm/stm32f4/i2s.hh>
#include <umios/backend/cm/stm32f4/irq_num.hh>
#include <umios/backend/cm/stm32f4/pdm_mic.hh> // PDM microphone
#include <umios/backend/cm/stm32f4/rcc.hh>

// USB stack (umiusb)
#include <audio_interface.hh>
#include <hal/stm32_otg.hh>
#include <umiusb.hh>

// SysEx shell protocol
#include <protocol/standard_io.hh>
#include <umios/kernel/shell_commands.hh>

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
inline constexpr uint32_t Exit = 0;
inline constexpr uint32_t RegisterProc = 1;
inline constexpr uint32_t WaitEvent = 2;
inline constexpr uint32_t SendEvent = 3;
inline constexpr uint32_t PeekEvent = 4;
inline constexpr uint32_t Yield = 5;         // Return control to kernel
inline constexpr uint32_t GetTime = 10;
inline constexpr uint32_t Sleep = 11;
inline constexpr uint32_t Log = 20;
inline constexpr uint32_t Panic = 21;
inline constexpr uint32_t GetParam = 30;
inline constexpr uint32_t SetParam = 31;
inline constexpr uint32_t GetShared = 40;
inline constexpr uint32_t MidiSend = 50;
inline constexpr uint32_t MidiRecv = 51;
} // namespace umi::kernel::app_syscall

// ============================================================================
// Configuration
// ============================================================================

constexpr uint32_t SAMPLE_RATE = 48000;
constexpr uint32_t BUFFER_SIZE = 64;   // Samples per channel per buffer
constexpr bool USB_ONLY_DEBUG = false; // Skip audio init to isolate USB enumeration
constexpr size_t INPUT_EVENT_CAPACITY = 32;

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
using UsbAudioDevice = umiusb::AudioFullDuplexMidi96kMaxAsyncFixedEps;
UsbAudioDevice usb_audio;
umiusb::Device<umiusb::Stm32FsHal, UsbAudioDevice>
    usb_device(usb_hal,
               usb_audio,
               {
                   .vendor_id = 0x1209,
                   .product_id = 0x000A,     // Bump PID to invalidate macOS descriptor cache
                   .device_version = 0x0305, // Version 3.05 to invalidate macOS descriptor cache
                   .manufacturer_idx = 1,
                   .product_idx = 2,
                   .serial_idx = 0,
               });

} // namespace

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
} // namespace usb_config

// ============================================================================
// Audio Buffers (DMA double-buffering)
// ============================================================================

// DMA buffers must be in SRAM, not CCM
constexpr uint32_t I2S_WORDS_PER_FRAME = 4; // 24-bit packed into 2x16-bit words per channel
constexpr uint32_t I2S_DMA_WORDS = BUFFER_SIZE * I2S_WORDS_PER_FRAME;

__attribute__((section(".dma_buffer"))) uint16_t audio_buf0[I2S_DMA_WORDS];

__attribute__((section(".dma_buffer"))) uint16_t audio_buf1[I2S_DMA_WORDS];

// PDM microphone DMA buffers
// PDM @ 3.0714MHz, DMA 256 words = 256*16 bits = 4096 PDM samples
// CIC 64x decimation = 4096/64 = 64 PCM samples @ 47,991Hz per DMA interrupt
// This matches I2S DMA (64 samples), so both ISRs fire at same rate (~1.33ms)
constexpr uint32_t PDM_BUF_SIZE = 256;

__attribute__((section(".dma_buffer"))) uint16_t pdm_buf0[PDM_BUF_SIZE];

__attribute__((section(".dma_buffer"))) uint16_t pdm_buf1[PDM_BUF_SIZE];

// PCM buffer for decimated audio (from PDM @ 47,991Hz)
constexpr uint32_t PCM_BUF_SIZE = 64; // 4096 bits / 64 = 64 samples per DMA
int16_t pcm_buf[PCM_BUF_SIZE];

// Stereo buffer for USB Audio IN (mic L + synth R) @ 47,991Hz
constexpr uint32_t STEREO_BUF_SIZE = 128; // 64 samples * 2 channels
int16_t stereo_buf[STEREO_BUF_SIZE];

// ============================================================================
// Global State
// ============================================================================

umi::kernel::AppLoader g_loader;

__attribute__((section(".shared"))) umi::kernel::SharedMemory g_shared;

// ============================================================================
// RTOS Task Management
// ============================================================================

// Task priorities (from umi_kernel.hh):
//   Realtime (0): Audio processing, DMA callbacks - highest
//   Server   (1): Drivers, I/O handlers (shell, USB SysEx)
//   User     (2): Application tasks
//   Idle     (3): Background, sleep management - lowest

// Task stacks in CCM (fast memory, no DMA needed for stacks)
constexpr uint32_t AUDIO_TASK_STACK_SIZE = 1024;    // words (4KB) - Realtime
constexpr uint32_t SYSTEM_TASK_STACK_SIZE = 512;    // words (2KB) - Server
constexpr uint32_t CONTROL_TASK_STACK_SIZE = 2048;  // words (8KB) - User
constexpr uint32_t IDLE_TASK_STACK_SIZE = 64;       // words (256B) - Idle

__attribute__((section(".audio_ccm"))) uint32_t g_audio_task_stack[AUDIO_TASK_STACK_SIZE];
__attribute__((section(".audio_ccm"))) uint32_t g_system_task_stack[SYSTEM_TASK_STACK_SIZE];
__attribute__((section(".audio_ccm"))) uint32_t g_control_task_stack[CONTROL_TASK_STACK_SIZE];
__attribute__((section(".audio_ccm"))) uint32_t g_idle_task_stack[IDLE_TASK_STACK_SIZE];

// Task contexts (TCBs)
umi::port::cm4::TaskContext g_audio_tcb;    // Realtime priority
umi::port::cm4::TaskContext g_system_tcb;   // Server priority
umi::port::cm4::TaskContext g_control_tcb;  // User priority
umi::port::cm4::TaskContext g_idle_tcb;     // Idle priority
umi::port::cm4::TaskContext* g_current_tcb = nullptr;

// Task notification flags
constexpr uint32_t NOTIFY_AUDIO_READY = (1 << 0);
constexpr uint32_t NOTIFY_SYSEX_READY = (1 << 1);
constexpr uint32_t NOTIFY_SHUTDOWN = (1 << 31);
volatile uint32_t g_audio_task_notify = 0;
volatile uint32_t g_system_task_notify = 0;
volatile uint32_t g_control_task_notify = 0;

// Task state for scheduler
enum class TaskState : uint8_t { Ready, Running, Blocked };
volatile TaskState g_audio_task_state = TaskState::Blocked;
volatile TaskState g_system_task_state = TaskState::Blocked;
volatile TaskState g_control_task_state = TaskState::Blocked;
volatile TaskState g_idle_task_state = TaskState::Ready;  // Idle always ready

// RTOS started flag - prevents SysTick from touching tasks before RTOS init
volatile bool g_rtos_started = false;

// Audio buffer pending for audio task
volatile uint16_t* g_pending_audio_buf = nullptr;

volatile uint32_t g_current_buffer = 0;
static constexpr uint8_t AUDIO_READY_QUEUE_SIZE = 2;
volatile uint8_t g_audio_ready_count = 0;
volatile uint8_t g_audio_ready_w = 0;
volatile uint8_t g_audio_ready_r = 0;
volatile uint16_t* g_audio_ready_bufs[AUDIO_READY_QUEUE_SIZE] = {};
volatile bool g_pdm_ready = false;
volatile uint16_t* g_active_pdm_buf = nullptr;

// Debug counters (not volatile for GDB inspection only)
uint32_t dbg_i2s_isr_count = 0;
uint32_t dbg_fill_audio_count = 0;
uint32_t dbg_out_buffered = 0;
uint32_t dbg_in_buffered = 0;
uint32_t dbg_read_frames = 0;
uint32_t dbg_underrun = 0;
uint32_t dbg_streaming = 0;                 // USB Audio OUT streaming flag
int32_t dbg_pll_ppm = 0;                    // ASRC adjustment in ppm
uint32_t dbg_overrun = 0;                   // USB Audio OUT overrun count
uint32_t dbg_missed = 0;                    // Missed audio frames (processing too slow)
uint32_t dbg_process_time = 0;              // fill_audio_buffer processing time (cycles)
uint32_t dbg_usb_rx_count = 0;              // USB Audio OUT packets received
int32_t dbg_sample_l = 0;                   // Debug: last sample L
int32_t dbg_sample_r = 0;                   // Debug: last sample R
uint32_t dbg_set_iface_count = 0;           // USB set_interface calls
uint32_t dbg_sof_count = 0;                 // USB SOF count
uint32_t dbg_feedback = 0;                  // Feedback value being sent to host
uint32_t dbg_fb_count = 0;                  // Feedback EP write count
uint32_t dbg_fb_actual = 0;                 // Actual feedback value sent (from HAL)
uint32_t dbg_fb_sent = 0;                   // Feedback sent from AudioInterface
uint32_t dbg_fb_xfrc = 0;                   // Feedback EP XFRC count
uint32_t dbg_fb_int_count = 0;              // Feedback EP IN interrupt count
uint32_t dbg_fb_int_last = 0;               // Last Feedback EP DIEPINT snapshot
uint32_t dbg_fb_diepctl = 0;                // Feedback EP DIEPCTL
uint32_t dbg_fb_diepint = 0;                // Feedback EP DIEPINT
uint32_t dbg_fb_fifo_before = 0;            // Feedback EP DTXFSTS before write
uint32_t dbg_fb_fifo_after = 0;             // Feedback EP DTXFSTS after write
uint32_t dbg_iepint_count = 0;              // Global IEPINT count
uint32_t dbg_last_daint = 0;                // Last DAINT snapshot
uint32_t dbg_last_daintmsk = 0;             // Last DAINTMSK snapshot
uint32_t dbg_set_iface_val = 0;             // (iface << 8) | alt
uint32_t dbg_out_iface_num = 0;             // Audio OUT interface number
uint32_t dbg_out_rx_last_len = 0;           // Audio OUT last packet length (bytes)
uint32_t dbg_out_rx_min_len = 0;            // Audio OUT min packet length (bytes)
uint32_t dbg_out_rx_max_len = 0;            // Audio OUT max packet length (bytes)
uint32_t dbg_out_rx_short = 0;              // Audio OUT short packet count
uint32_t dbg_fb_override_enable = 0;        // Feedback override enable (0/1)
uint32_t dbg_fb_override_value = 0;         // Feedback override value (10.14)
uint32_t dbg_fb_test_mode = 0;              // Auto toggle feedback override (0/1)
uint32_t dbg_fb_mode = 0;                   // Feedback mode (0=fifo, 1=delta)
uint32_t dbg_fb_delta_gain = 0;             // Delta gain Q16.16 (0=auto)
uint32_t dbg_playback_started = 0;          // playback_started_ flag
uint32_t dbg_muted = 0;                     // fu_out_mute_ flag
uint32_t dbg_actual_rate = 0;               // Actual rate passed to set_actual_rate
int16_t dbg_volume = 0;                     // fu_out_volume_ value
int32_t dbg_ring_sample0 = 0;               // Raw ring buffer sample[0]
int32_t dbg_ring_sample1 = 0;               // Raw ring buffer sample[1]
int32_t dbg_hal_rx_sample0 = 0;             // HAL received sample[0]
int32_t dbg_hal_rx_sample1 = 0;             // HAL received sample[1]
uint32_t dbg_fifo_word = 0;                 // First word read from RX FIFO
uint32_t dbg_ep1_fifo_word = 0;             // First word from EP1 specifically
uint32_t dbg_audio_in_write = 0;            // Audio IN write count
uint32_t dbg_asrc_rate = 0;                 // ASRC rate Q16.16
uint32_t dbg_out_buf_min = 0;               // Min buffered frames since start
uint32_t dbg_out_buf_max = 0;               // Max buffered frames since start
int32_t dbg_audio_in_level = 0;             // Audio IN ring buffer level
uint32_t dbg_out_buf_level = 0;             // Audio OUT ring buffer level
uint32_t dbg_in_buf_level = 0;              // Audio IN ring buffer level
uint32_t dbg_sample_rate_change_count = 0;  // Sample rate change count
uint32_t dbg_current_sample_rate = 48000;   // Current sample rate for debugging
uint32_t dbg_sr_get_cur = 0;                // GET CUR sample rate requests
uint32_t dbg_sr_set_cur = 0;                // SET CUR sample rate requests
uint32_t dbg_sr_ep0_rx = 0;                 // EP0 RX for sample rate
uint32_t dbg_sr_last_req = 0;               // Last requested sample rate
uint32_t dbg_sr_ep_req = 0;                 // Endpoint sample rate requests
uint32_t dbg_out_rx_disc_count = 0;         // USB OUT packet discontinuity count
uint32_t dbg_usb_out_doepint = 0;           // DOEPINT(1) last snapshot
uint32_t dbg_usb_out_doepint_err_count = 0; // DOEPINT(1) unhandled bits count
uint32_t dbg_usb_out_doepint_err_last = 0;  // DOEPINT(1) last unhandled bits
uint32_t dbg_usb_out_doepint_xfrc = 0;
uint32_t dbg_usb_out_doepint_stup = 0;
uint32_t dbg_usb_out_doepint_otepdis = 0;
uint32_t dbg_usb_out_doepint_stsphsrx = 0;
uint32_t dbg_usb_out_doepint_nak = 0;
uint32_t dbg_usb_rxflvl_count = 0;  // RXFLVL events
uint32_t dbg_usb_rxflvl_out = 0;    // RXFLVL OUT_DATA events
uint32_t dbg_usb_rxflvl_setup = 0;  // RXFLVL SETUP_DATA events
uint32_t dbg_usb_rxflvl_other = 0;  // RXFLVL other events
uint32_t dbg_usb_ep1_bcnt_last = 0; // EP1 OUT last byte count
uint32_t dbg_usb_ep1_bcnt_min = 0;  // EP1 OUT min byte count
uint32_t dbg_usb_ep1_bcnt_max = 0;  // EP1 OUT max byte count
uint32_t dbg_usb_ep1_zero = 0;      // EP1 OUT zero-length count
uint32_t dbg_usb_ep1_zero_outdata = 0;
uint32_t dbg_usb_ep1_zero_setup = 0;
uint32_t dbg_usb_ep1_zero_other = 0;
uint32_t dbg_usb_ep1_zero_rxsts_last = 0;
uint32_t dbg_usb_ep1_pktsts = 0;     // EP1 last pktsts
uint32_t dbg_usb_ep1_first_word = 0; // EP1 OUT first word
uint32_t dbg_usb_ep1_last_word = 0;  // EP1 OUT last word
uint32_t dbg_usb_rxflvl_pktsts_0 = 0;
uint32_t dbg_usb_rxflvl_pktsts_1 = 0;
uint32_t dbg_usb_rxflvl_pktsts_2 = 0;
uint32_t dbg_usb_rxflvl_pktsts_3 = 0;
uint32_t dbg_usb_rxflvl_pktsts_4 = 0;
uint32_t dbg_usb_rxflvl_pktsts_5 = 0;
uint32_t dbg_usb_rxflvl_pktsts_6 = 0;
uint32_t dbg_usb_rxflvl_pktsts_7 = 0;
uint32_t dbg_usb_rxflvl_pktsts_8 = 0;
uint32_t dbg_usb_rxflvl_pktsts_9 = 0;
uint32_t dbg_usb_rxflvl_pktsts_a = 0;
uint32_t dbg_usb_rxflvl_pktsts_b = 0;
uint32_t dbg_usb_rxflvl_pktsts_c = 0;
uint32_t dbg_usb_rxflvl_pktsts_d = 0;
uint32_t dbg_usb_rxflvl_pktsts_e = 0;
uint32_t dbg_usb_rxflvl_pktsts_f = 0;
uint32_t dbg_out_rx_intra_event_count = 0;        // Intra-packet event count
uint32_t dbg_out_rx_intra_event_len = 0;          // Packet length at event
int32_t dbg_out_rx_intra_event_cur_l = 0;         // Current L sample at event
int32_t dbg_out_rx_intra_event_cur_r = 0;         // Current R sample at event
int32_t dbg_out_rx_intra_event_prev_l = 0;        // Previous L sample at event
int32_t dbg_out_rx_intra_event_prev_r = 0;        // Previous R sample at event
int32_t dbg_out_rx_intra_event_dl = 0;            // Delta L at event
int32_t dbg_out_rx_intra_event_dr = 0;            // Delta R at event
uint32_t dbg_out_rx_intra_log_threshold = 0x2000; // Log threshold for intra spikes
uint32_t dbg_out_rx_debug_enable = 0;             // Enable USB OUT debug tracking
uint32_t dbg_out_rx_log_write = 0;                // Ring buffer write index
uint32_t dbg_out_rx_log_count = 0;                // Ring buffer entry count
uint32_t dbg_audio_ready_level = 0;               // Pending audio buffers
uint32_t dbg_audio_ready_max = 0;                 // Max pending audio buffers
uint32_t dbg_audio_ready_overflow = 0;            // Audio ready queue overflow count
uint32_t dbg_proc_cycles_last = 0;                // process_audio_frame cycles (last)
uint32_t dbg_proc_cycles_max = 0;                 // process_audio_frame cycles (max)
uint32_t dbg_proc_cycles_over = 0;                // process_audio_frame over budget count
uint32_t dbg_proc_cycles_budget = 0;              // process_audio_frame budget cycles

struct OutRxLogEntry {
    uint32_t packet_index;
    uint32_t len;
    int32_t max_abs;
    uint32_t raw0;
    uint32_t raw1;
    int32_t cur_l;
    int32_t cur_r;
    int32_t prev_l;
    int32_t prev_r;
    int32_t dl;
    int32_t dr;
};

static constexpr uint32_t OUT_RX_LOG_SIZE = 64;  // Reduced from 128 to save SRAM
OutRxLogEntry dbg_out_rx_log[OUT_RX_LOG_SIZE] = {};

void on_audio_out_packet(const UsbAudioDevice::OutPacketStats& stats) {
    uint32_t idx = dbg_out_rx_log_write % OUT_RX_LOG_SIZE;
    dbg_out_rx_log[idx] = {
        stats.packet_index,
        stats.len,
        stats.max_abs,
        stats.raw0,
        stats.raw1,
        stats.cur_l,
        stats.cur_r,
        stats.prev_l,
        stats.prev_r,
        stats.dl,
        stats.dr,
    };
    ++dbg_out_rx_log_write;
    if (dbg_out_rx_log_count < OUT_RX_LOG_SIZE) {
        ++dbg_out_rx_log_count;
    }
}
uint32_t dbg_out_rx_disc_max = 0;             // Max discontinuity at packet boundary
int32_t dbg_out_rx_disc_last = 0;             // Last discontinuity delta (L)
uint32_t dbg_out_rx_disc_threshold = 0x20000; // Threshold for discontinuity
int32_t dbg_dma_prev_samples[8] = {};         // Last 4 frames (L/R interleaved) of previous buffer
int32_t dbg_dma_next_samples[8] = {};         // First 4 frames (L/R interleaved) of current buffer
uint32_t dbg_dma_boundary_abs = 0;            // Absolute delta at boundary (max of L/R)
uint32_t dbg_dma_interior_max = 0;            // Max absolute delta inside buffer
uint32_t dbg_glitch_count = 0;                // In-buffer glitch detections
uint32_t dbg_glitch_index = 0;                // Frame index where glitch detected
uint32_t dbg_glitch_threshold = 0x10000;      // Threshold for glitch capture
uint32_t dbg_glitch_window_len = 128;         // Frames captured around glitch
__attribute__((section(".debug_ccm"))) int32_t dbg_glitch_window[256] = {}; // 128 frames (L/R interleaved)
uint32_t dbg_dma_packed_prev[16] = {};       // Last 4 frames of packed DMA buffer (32-bit words)
uint32_t dbg_dma_packed_next[16] = {};       // First 4 frames of packed DMA buffer (32-bit words)
uint32_t dbg_out_rx_intra_count = 0;         // USB OUT intra-packet discontinuity count
uint32_t dbg_out_rx_intra_max = 0;           // Max intra-packet discontinuity
int32_t dbg_out_rx_intra_last = 0;           // Last intra-packet delta (L)
uint32_t dbg_dma_spike_count = 0;            // DMA boundary spike count
uint32_t dbg_dma_spike_max = 0;              // Max absolute delta at boundary
int32_t dbg_dma_spike_last = 0;              // Last boundary delta (L channel)
uint32_t dbg_dma_spike_threshold = 0x200000; // Spike threshold (24-bit)
uint32_t dbg_midi_rx_count = 0;              // MIDI OUT packets received
uint8_t dbg_midi_last[4] = {};               // Last MIDI packet bytes
uint8_t dbg_midi_last_len = 0;               // Last MIDI packet length

// Descriptor debug dump (for host-side inspection via pyocd)
constexpr size_t DBG_DESC_MAX = 512;
uint32_t dbg_desc_size = 0;
__attribute__((section(".debug_ccm"))) uint8_t dbg_desc_buf[DBG_DESC_MAX] = {};

// MIDI queue (ISR -> App)
struct MidiMsg {
    uint8_t data[4];
    uint8_t len;
};
constexpr uint32_t MIDI_QUEUE_SIZE = 64;
MidiMsg g_midi_queue[MIDI_QUEUE_SIZE];
volatile uint32_t g_midi_write = 0;
volatile uint32_t g_midi_read = 0;

// ============================================================================
// SysEx Shell (USB MIDI <-> Shell via System Task)
// ============================================================================

// Forward declarations
void shell_write(const char* str);
extern volatile uint32_t g_tick_us;

// StateProvider implementation for ShellCommands
// Provides: state(), config(), error_log(), system_mode(), reset_system(), feed_watchdog(), enable_watchdog()
class Stm32StateProvider {
public:
    Stm32StateProvider() {
        // Initialize config
        config_.platform_name = "STM32F4-Discovery";
        std::strcpy(config_.serial_number, "STM32-00000001");
        config_.sample_rate = SAMPLE_RATE;
    }

    umi::os::KernelStateView& state() {
        // Update dynamic state before returning
        state_.uptime_us = g_tick_us;
        state_.audio_running = true;
        state_.audio_buffer_count = dbg_fill_audio_count;
        state_.audio_drop_count = dbg_underrun;
        state_.midi_rx_count = dbg_midi_rx_count;
        state_.usb_connected = true;
        state_.task_count = 4;  // audio, system, control, idle
        state_.task_ready = 1;
        state_.task_blocked = 2;
        state_.context_switches = 0;  // Not tracked currently
        state_.sram_total = 128 * 1024;  // 128KB SRAM
        state_.flash_total = 1024 * 1024; // 1MB Flash
        return state_;
    }

    umi::os::ShellConfig& config() {
        return config_;
    }

    umi::os::ErrorLog<16>& error_log() {
        return error_log_;
    }

    umi::os::SystemMode& system_mode() {
        return mode_;
    }

    void reset_system() {
        constexpr uint32_t AIRCR = 0xE000ED0C;
        constexpr uint32_t VECTKEY = 0x05FA0000;
        constexpr uint32_t SYSRESETREQ = 0x00000004;
        *reinterpret_cast<volatile uint32_t*>(AIRCR) = VECTKEY | SYSRESETREQ;
        while (true) {}
    }

    void feed_watchdog() {
        // IWDG feed (not implemented - just placeholder)
    }

    void enable_watchdog(bool enable) {
        state_.watchdog_enabled = enable;
    }

private:
    umi::os::KernelStateView state_{};
    umi::os::ShellConfig config_{};
    umi::os::ErrorLog<16> error_log_{};
    umi::os::SystemMode mode_ = umi::os::SystemMode::Normal;
};

Stm32StateProvider g_state_provider;
// Use 1024-byte output buffer to save SRAM (default is 2048)
umi::os::ShellCommands<Stm32StateProvider, 1024>* g_shell = nullptr;

// SysEx Standard IO for shell output
umidi::protocol::StandardIO<256, 128> g_stdio;

// SysEx receive buffer (from USB ISR -> System Task)
constexpr size_t SYSEX_BUF_SIZE = 256;
uint8_t g_sysex_buf[SYSEX_BUF_SIZE];
volatile size_t g_sysex_len = 0;
volatile bool g_sysex_ready = false;

// Debug counters for SysEx/shell (for GDB inspection)
uint32_t dbg_sysex_rx_count = 0;
uint32_t dbg_sysex_dropped = 0;
uint32_t dbg_sysex_processed = 0;
uint32_t dbg_sysex_wakeup = 0;

// Tick counter for GetTime syscall
volatile uint32_t g_tick_us = 0;

// Current sample rate (may be changed by USB host)
volatile uint32_t g_current_sample_rate = SAMPLE_RATE;
volatile bool g_sample_rate_change_pending = false;
volatile uint32_t g_new_sample_rate = SAMPLE_RATE;

// Debug: sample rate change progress (0=idle, 1=start, 2=dma_stop, 3=pll_done, 4=i2s_restart, 5=complete)
volatile uint32_t dbg_sr_change_step = 0;

// ============================================================================
// PLLI2S Configuration for Different Sample Rates
// ============================================================================

/// Configure PLLI2S for specified sample rate
/// @param rate Target sample rate (44100, 48000, or 96000)
/// @return Actual achieved sample rate
static uint32_t configure_plli2s(uint32_t rate) {
    constexpr uint32_t RCC_PLLI2SCFGR = 0x40023884;
    constexpr uint32_t RCC_CR = 0x40023800;

    // Disable PLLI2S
    *reinterpret_cast<volatile uint32_t*>(RCC_CR) &= ~(1U << 26);

    // Wait for PLL to fully disable (with timeout)
    for (int i = 0; i < 10000; ++i) {
        if (!(*reinterpret_cast<volatile uint32_t*>(RCC_CR) & (1U << 27)))
            break;
    }

    // PLL configuration values (HSE = 8MHz, PLLM = 8, so PLL input = 1MHz)
    // I2S uses PLLI2SCLK = 1MHz * PLLI2SN / PLLI2SR
    // Fs = PLLI2SCLK / [256 × (2×I2SDIV + ODD)]

    uint32_t plli2sn, plli2sr;
    uint32_t actual_rate;
    uint8_t i2sdiv, odd;

    switch (rate) {
    case 44100:
        // Target: 44.1kHz
        // PLLI2SCLK = 1MHz * 271 / 6 = 45.167 MHz
        // Fs = 45.167MHz / [256 × (2×2 + 0)] = 45.167MHz / 1024 = 44,108 Hz
        plli2sn = 271;
        plli2sr = 6;
        i2sdiv = 2;
        odd = 0;
        actual_rate = 44108;
        break;

    case 96000:
        // Target: 96kHz
        // PLLI2SCLK = 1MHz * 295 / 3 = 98.333 MHz
        // Fs = 98.333MHz / [256 × (2×2 + 0)] = 98.333MHz / 1024 = 96,028 Hz
        plli2sn = 295;
        plli2sr = 3;
        i2sdiv = 2;
        odd = 0;
        actual_rate = 96028;
        break;

    case 48000:
    default:
        // Target: 48kHz
        // PLLI2SCLK = 1MHz * 258 / 3 = 86 MHz
        // Fs = 86MHz / [256 × (2×3 + 1)] = 86MHz / 1792 = 47,991 Hz
        plli2sn = 258;
        plli2sr = 3;
        i2sdiv = 3;
        odd = 1;
        actual_rate = 47991;
        break;
    }

    *reinterpret_cast<volatile uint32_t*>(RCC_PLLI2SCFGR) = (plli2sr << 28) | // PLLI2SR
                                                            (plli2sn << 6);   // PLLI2SN

    // Enable PLLI2S
    *reinterpret_cast<volatile uint32_t*>(RCC_CR) |= (1U << 26);

    // Wait for lock (with timeout)
    for (int i = 0; i < 100000; ++i) {
        if (*reinterpret_cast<volatile uint32_t*>(RCC_CR) & (1U << 27))
            break;
    }

    // Update I2S divider - must be done while I2S is disabled
    i2s3.init_with_divider(i2sdiv, odd, true);

    return actual_rate;
}

/// Initialize PLLI2S for default 48kHz
static void init_plli2s() {
    configure_plli2s(48000);
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
    RCC::enable_spi2(); // For PDM mic
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
    for (int i = 0; i < 100000; ++i) {
        asm volatile("");
    }

    if (!codec.init(true)) { // Use 24-bit mode
        gpio_d.set(14);      // Red LED = error
        while (1) {
        }
    }

    init_plli2s();
    i2s3.init_48khz(true); // Use 24-bit mode

    __builtin_memset(audio_buf0, 0, sizeof(audio_buf0));
    __builtin_memset(audio_buf1, 0, sizeof(audio_buf1));

    dma_i2s.init(audio_buf0, audio_buf1, I2S_DMA_WORDS, i2s3.dr_addr());

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
    for (int i = 0; i < 10000; ++i) {
        asm volatile("");
    }

    // Force re-enumeration (macOS can ignore quick reconnects)
    usb_hal.disconnect();
    for (int i = 0; i < 500000; ++i) {
        asm volatile("");
    }

    usb_audio.on_streaming_change = [](bool streaming) {
        if (streaming) {
            gpio_d.set(15); // Blue LED ON
        } else {
            gpio_d.reset(15);
        }
    };

    usb_audio.on_audio_in_change = [](bool streaming) {
        if (streaming) {
            gpio_d.set(13); // Orange LED ON
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
            gpio_d.toggle(12); // Green LED toggle at ~1Hz when receiving
        }
    };
    usb_audio.set_audio_out_packet_callback(on_audio_out_packet);

    // Note: USB Audio IN is written directly from I2S DMA ISR (process_audio_frame)
    // No need for on_sof_app callback

    // USB MIDI -> MIDI queue (ISR context)
    usb_audio.set_midi_callback([](uint8_t /*cable*/, const uint8_t* data, uint8_t len) {
        dbg_midi_last_len = (len > 4) ? 4 : len;
        for (uint8_t i = 0; i < dbg_midi_last_len; ++i) {
            dbg_midi_last[i] = data[i];
        }
        ++dbg_midi_rx_count;
        uint32_t next = (g_midi_write + 1) % MIDI_QUEUE_SIZE;
        if (next != g_midi_read) { // Not full
            g_midi_queue[g_midi_write].len = (len > 4) ? 4 : len;
            for (uint8_t i = 0; i < g_midi_queue[g_midi_write].len; ++i) {
                g_midi_queue[g_midi_write].data[i] = data[i];
            }
            g_midi_write = next;
        }
    });

    // SysEx callback for shell (ISR context - notify System Task)
    usb_audio.set_sysex_callback([](const uint8_t* data, uint16_t len) {
        gpio_d.toggle(14);  // Red LED toggle on SysEx receive (debug)
        ++dbg_sysex_rx_count;  // Count all SysEx receives
        if (!g_sysex_ready && len <= SYSEX_BUF_SIZE) {
            std::memcpy(g_sysex_buf, data, len);
            g_sysex_len = len;
            g_sysex_ready = true;
            // Wake System Task (Server priority) to process SysEx
            g_system_task_notify |= NOTIFY_SYSEX_READY;
            ++dbg_sysex_wakeup;
            if (g_system_task_state == TaskState::Blocked) {
                g_system_task_state = TaskState::Ready;
                SCB::trigger_pendsv();  // Request context switch
            }
        } else {
            ++dbg_sysex_dropped;  // Dropped due to busy or size
        }
    });

    // Sample rate change callback (from USB ISR context)
    // Note: Actual hardware reconfiguration happens in main loop to avoid ISR complexity
    // Only set pending if not already pending and rate is actually different
    usb_audio.set_sample_rate_callback([](uint32_t new_rate) {
        if (!g_sample_rate_change_pending && new_rate != g_current_sample_rate) {
            g_new_sample_rate = new_rate;
            g_sample_rate_change_pending = true;
        }
    });

    usb_device.set_strings(usb_config::string_table);
    usb_device.init();
    {
        auto desc = usb_audio.config_descriptor();
        dbg_desc_size = static_cast<uint32_t>(desc.size());
        uint32_t copy = dbg_desc_size;
        if (copy > DBG_DESC_MAX)
            copy = DBG_DESC_MAX;
        for (uint32_t i = 0; i < copy; ++i) {
            dbg_desc_buf[i] = desc[i];
        }
    }
    usb_hal.connect();
    for (int i = 0; i < 500000; ++i) {
        asm volatile("");
    }

    // OTG_FS = IRQ 67, priority 6 (lower than audio DMA)
    NVIC::set_prio(67, 6);
    NVIC::enable(67);
}

// Shell output callback - sends via SysEx
void shell_write(const char* str) {
    auto len = std::strlen(str);
    g_stdio.write_stdout(reinterpret_cast<const uint8_t*>(str), len,
        [](const uint8_t* data, size_t len) {
            usb_audio.send_sysex(usb_hal, data, static_cast<uint16_t>(len));
        });
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
    *reinterpret_cast<volatile uint32_t*>(SYST_CSR) = 0x07; // Enable, IRQ, use core clock
}

static inline uint32_t dwt_cycle() {
    return *reinterpret_cast<volatile uint32_t*>(0xE0001004);
}

static void init_cycle_counter() {
    constexpr uint32_t DEMCR = 0xE000EDFC;
    constexpr uint32_t DWT_CTRL = 0xE0001000;
    constexpr uint32_t DWT_CYCCNT = 0xE0001004;
    *reinterpret_cast<volatile uint32_t*>(DEMCR) |= (1u << 24);
    *reinterpret_cast<volatile uint32_t*>(DWT_CYCCNT) = 0;
    *reinterpret_cast<volatile uint32_t*>(DWT_CTRL) |= 1u;
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

// I2S DMA: Notify audio task when buffer ready
extern "C" void DMA1_Stream5_IRQHandler() {
    if (dma_i2s.transfer_complete()) {
        dma_i2s.clear_tc();

        // DMA double-buffer: when ISR fires, DMA has already switched to next buffer
        // We need to fill the buffer that JUST finished
        // current_buffer() returns the active DMA target, so fill the other one
        uint16_t* completed_buf = (dma_i2s.current_buffer() == 1) ? audio_buf0 : audio_buf1;

        uint8_t count = g_audio_ready_count;
        if (count < AUDIO_READY_QUEUE_SIZE) {
            g_audio_ready_bufs[g_audio_ready_w] = reinterpret_cast<uint16_t*>(completed_buf);
            g_audio_ready_w = static_cast<uint8_t>((g_audio_ready_w + 1) % AUDIO_READY_QUEUE_SIZE);
            g_audio_ready_count = static_cast<uint8_t>(count + 1);
            uint32_t level = count + 1;
            if (level > dbg_audio_ready_max) {
                dbg_audio_ready_max = level;
            }

            // Notify audio task and trigger context switch if task is waiting
            if (g_audio_task_state == TaskState::Blocked) {
                g_audio_task_notify |= NOTIFY_AUDIO_READY;
                g_audio_task_state = TaskState::Ready;
                // Trigger PendSV for context switch (low priority, runs after ISR)
                SCB::trigger_pendsv();
            }
        } else {
            ++dbg_audio_ready_overflow;
        }

        ++dbg_i2s_isr_count;
    }
}

extern "C" void OTG_FS_IRQHandler() {
    usb_device.poll();
}

// Debug counters for task scheduling (not volatile - for GDB inspection only)
uint32_t dbg_systick_wakeup = 0;
uint32_t dbg_pendsv_count = 0;

extern "C" void SysTick_Handler() {
    g_tick_us += 1000; // 1ms = 1000us

    // Wake control task periodically (for WaitEvent timeout handling)
    // This allows the scheduler in the app to check timeouts
    // Only do this after RTOS has started to avoid touching uninitialized tasks
    if (g_rtos_started && g_control_task_state == TaskState::Blocked) {
        g_control_task_state = TaskState::Ready;
        ++dbg_systick_wakeup;
        SCB::trigger_pendsv();
    }
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
        if (arg1 != 0) {
            using ProcFn = umi::kernel::ProcessFn;
            result = g_loader.register_processor(reinterpret_cast<void*>(arg0), reinterpret_cast<ProcFn>(arg1));
        } else {
            result = g_loader.register_processor(reinterpret_cast<void*>(arg0));
        }
        break;

    case WaitEvent:
        // Block control task and yield to allow other tasks to run
        // This is critical for cooperative multitasking - without yielding,
        // control_task would monopolize CPU and system_task (shell) can't run
        g_control_task_state = TaskState::Blocked;
        SCB::trigger_pendsv();
        result = 0;
        break;

    case Yield:
        // Yield: trigger context switch via PendSV
        // Called from unprivileged task to voluntarily give up CPU
        SCB::trigger_pendsv();
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
            result = 0; // No data
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
        result = 0; // Not implemented yet
        break;

    default:
        result = -1; // Unknown syscall
        break;
    }

    sp[0] = static_cast<uint32_t>(result);
}

// Simplified SVC handler - no Yield processing needed
extern "C" void svc_handler_c(uint32_t* sp) {
    svc_handler_impl(sp);
}

// Naked SVC handler - must get SP before any stack operations
extern "C" [[gnu::naked]] void SVC_Handler() {
    __asm__ volatile(
        // Get correct stack pointer (MSP or PSP based on EXC_RETURN)
        "tst lr, #4\n"             // Check bit 4 of LR (EXC_RETURN)
        "ite eq\n"
        "mrseq r0, msp\n"          // If 0: use MSP
        "mrsne r0, psp\n"          // If 1: use PSP
        "push {lr}\n"              // Save EXC_RETURN
        "bl svc_handler_c\n"       // Call C handler
        "pop {pc}\n"               // Return with saved EXC_RETURN
        :::
    );
}

// ============================================================================
// RTOS Scheduler and Context Switch
// ============================================================================

// External symbol for port layer PendSV handler
extern "C" umi::port::cm4::TaskContext* umi_cm4_current_tcb = nullptr;

// Select next task to run (simple 2-task scheduler)
// Audio task has higher priority than Control task
// Select next task to run (4-task priority scheduler)
// Priority: Realtime (Audio) > Server (System) > User (Control) > Idle
static umi::port::cm4::TaskContext* select_next_task() {
    // Audio task: Realtime priority (highest)
    if (g_audio_task_state == TaskState::Ready) {
        return &g_audio_tcb;
    }
    // System task: Server priority (shell, SysEx, drivers)
    if (g_system_task_state == TaskState::Ready) {
        return &g_system_tcb;
    }
    // Control task: User priority (application)
    if (g_control_task_state == TaskState::Ready) {
        return &g_control_tcb;
    }
    // Idle task: always ready (lowest priority)
    return &g_idle_tcb;
}

// Context switch function called from PendSV handler
// Updates umi_cm4_current_tcb to point to next task
extern "C" void umi_cm4_switch_context() {
    // Update current task state (Running -> Ready, unless Blocked)
    if (g_current_tcb == &g_audio_tcb && g_audio_task_state == TaskState::Running) {
        g_audio_task_state = TaskState::Ready;
    } else if (g_current_tcb == &g_system_tcb && g_system_task_state == TaskState::Running) {
        g_system_task_state = TaskState::Ready;
    } else if (g_current_tcb == &g_control_tcb && g_control_task_state == TaskState::Running) {
        g_control_task_state = TaskState::Ready;
    }
    // Idle task is always Ready

    // Select next task
    auto* next = select_next_task();
    g_current_tcb = next;
    umi_cm4_current_tcb = next;

    // Update state to Running
    if (next == &g_audio_tcb) {
        g_audio_task_state = TaskState::Running;
    } else if (next == &g_system_tcb) {
        g_system_task_state = TaskState::Running;
    } else if (next == &g_control_tcb) {
        g_control_task_state = TaskState::Running;
    }
    // Idle task doesn't need Running state tracking
}

// PendSV handler for context switching
// This is a custom implementation that handles the 2-task case
extern "C" [[gnu::naked]] void PendSV_Handler() {
    __asm__ volatile(
        ".syntax unified\n"

        // Save current context if there's a current task
        "ldr     r3, =umi_cm4_current_tcb\n"
        "ldr     r2, [r3]\n"
        "cbz     r2, 1f\n"          // Skip save if no current task

        // Get PSP
        "mrs     r0, psp\n"
        "isb\n"

        // Save FPU high registers if used (bit 4 of LR = 0 means FPU)
        "tst     lr, #0x10\n"
        "it      eq\n"
        "vstmdbeq r0!, {s16-s31}\n"

        // Save R4-R11 and EXC_RETURN (LR)
        "stmdb   r0!, {r4-r11, lr}\n"

        // Store stack pointer to TCB
        "str     r0, [r2]\n"

    "1:\n"
        // Critical section
        "mov     r0, #0x50\n"       // BASEPRI threshold
        "msr     basepri, r0\n"
        "dsb\n"
        "isb\n"

        // Call context switch function
        "bl      umi_cm4_switch_context\n"

        // End critical section
        "mov     r0, #0\n"
        "msr     basepri, r0\n"

        // Get new TCB
        "ldr     r3, =umi_cm4_current_tcb\n"
        "ldr     r1, [r3]\n"
        "cbz     r1, 2f\n"          // No task to switch to

        // Restore context
        "ldr     r0, [r1]\n"        // Stack pointer

        // Restore R4-R11 and EXC_RETURN
        "ldmia   r0!, {r4-r11, lr}\n"

        // Restore FPU high registers if needed
        "tst     lr, #0x10\n"
        "it      eq\n"
        "vldmiaeq r0!, {s16-s31}\n"

        // Set PSP and return
        "msr     psp, r0\n"
        "isb\n"
        "bx      lr\n"

    "2:\n"
        // No task - return to caller (kernel idle loop)
        "bx      lr\n"

        ".align 4\n"
        ::: "memory"
    );
}

// ============================================================================
// Audio Processing
// ============================================================================

// USB Audio IN buffer (separate from I2S DMA buffer)
int16_t usb_in_buf[BUFFER_SIZE * 2];

// Float buffer for synth processing
umi::sample_t synth_out_mono[BUFFER_SIZE];
static std::array<umi::sample_t*, 1> synth_out_channels = {synth_out_mono};
static umi::EventQueue<> g_app_event_queue;

// I2S work buffer (32-bit internal audio)
int32_t i2s_work_buf[BUFFER_SIZE * 2];

// Last synth output (for USB Audio IN)
int16_t last_synth_out[BUFFER_SIZE * 2];

// Debug: track what fill_audio_buffer returns
uint32_t dbg_fill_ret = 0;      // Return value from read_audio_asrc
uint32_t dbg_fill_buf_addr = 0; // Buffer address passed to fill
int32_t dbg_buf_sample0 = 0;    // First sample in buffer after fill
int32_t dbg_buf_sample1 = 0;    // Second sample
uint32_t dbg_synth_called = 0;  // Synth process called count

// Process audio (called from main loop)
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
        uint32_t v = (static_cast<uint32_t>(clamped) & 0x00FFFFFFu) << 8; // left-align 24-bit
        // Send MSW first for STM32 I2S 24-bit in 32-bit channel length
        out[i * 2] = static_cast<uint16_t>((v >> 16) & 0xFFFF);
        out[i * 2 + 1] = static_cast<uint16_t>(v & 0xFFFF);
    }
}

static void process_audio_frame(uint16_t* buf) {
    // Use dt from shared memory (updated when sample rate changes)
    const float dt = g_shared.dt;

    usb_audio.read_audio(i2s_work_buf, BUFFER_SIZE);

    dbg_buf_sample0 = i2s_work_buf[0];
    dbg_buf_sample1 = i2s_work_buf[1];
    dbg_sample_l = i2s_work_buf[0];
    dbg_sample_r = i2s_work_buf[1];
    // DMA boundary spike detection (compare last sample of previous buffer)
    static bool has_last = false;
    static int32_t last_l = 0;
    static int32_t last_r = 0;
    static int32_t last_tail[8] = {};
    if (has_last) {
        int32_t delta_l = i2s_work_buf[0] - last_l;
        int32_t delta_r = i2s_work_buf[1] - last_r;
        int32_t abs_l = (delta_l < 0) ? -delta_l : delta_l;
        int32_t abs_r = (delta_r < 0) ? -delta_r : delta_r;
        int32_t abs_max = (abs_l > abs_r) ? abs_l : abs_r;
        dbg_dma_spike_last = delta_l;
        dbg_dma_boundary_abs = static_cast<uint32_t>(abs_max);
        if (static_cast<uint32_t>(abs_max) > dbg_dma_spike_threshold) {
            ++dbg_dma_spike_count;
            if (static_cast<uint32_t>(abs_max) > dbg_dma_spike_max) {
                dbg_dma_spike_max = static_cast<uint32_t>(abs_max);
            }
        }
    }
    last_l = i2s_work_buf[(BUFFER_SIZE - 1) * 2];
    last_r = i2s_work_buf[(BUFFER_SIZE - 1) * 2 + 1];
    // Dump boundary samples (last 4 frames of previous buffer, first 4 of current)
    if (has_last) {
        for (uint32_t i = 0; i < 8; ++i) {
            dbg_dma_prev_samples[i] = last_tail[i];
        }
    }
    for (uint32_t i = 0; i < 8; ++i) {
        dbg_dma_next_samples[i] = i2s_work_buf[i];
    }
    // Update tail for next boundary
    for (uint32_t i = 0; i < 8; ++i) {
        last_tail[i] = i2s_work_buf[(BUFFER_SIZE - 4) * 2 + i];
    }
    // Max interior delta within current buffer (L/R)
    uint32_t interior_max = 0;
    bool captured = false;
    for (uint32_t i = 1; i < BUFFER_SIZE; ++i) {
        int32_t cur_l = i2s_work_buf[i * 2];
        int32_t cur_r = i2s_work_buf[i * 2 + 1];
        int32_t prev_l = i2s_work_buf[(i - 1) * 2];
        int32_t prev_r = i2s_work_buf[(i - 1) * 2 + 1];
        int32_t dl = cur_l - prev_l;
        int32_t dr = cur_r - prev_r;
        uint32_t abs_l = static_cast<uint32_t>((dl < 0) ? -dl : dl);
        uint32_t abs_r = static_cast<uint32_t>((dr < 0) ? -dr : dr);
        uint32_t abs_max = (abs_l > abs_r) ? abs_l : abs_r;
        if (abs_max > interior_max) {
            interior_max = abs_max;
        }
        if (!captured && abs_max > dbg_glitch_threshold) {
            // Capture 128 frames around the glitch
            uint32_t span = dbg_glitch_window_len;
            uint32_t pre = span / 2;
            uint32_t start = (i > pre) ? (i - pre) : 0;
            if (start + span > BUFFER_SIZE) {
                start = (BUFFER_SIZE >= span) ? (BUFFER_SIZE - span) : 0;
            }
            for (uint32_t j = 0; j < span; ++j) {
                dbg_glitch_window[j * 2] = i2s_work_buf[(start + j) * 2];
                dbg_glitch_window[j * 2 + 1] = i2s_work_buf[(start + j) * 2 + 1];
            }
            dbg_glitch_index = i;
            ++dbg_glitch_count;
            captured = true;
        }
    }
    dbg_dma_interior_max = interior_max;
    has_last = true;
    pack_i2s_24(buf, i2s_work_buf, BUFFER_SIZE, 2);
    // Dump packed DMA buffer boundary (4 frames => 16 words)
    static uint32_t last_packed_tail[16] = {};
    if (has_last) {
        for (uint32_t i = 0; i < 16; ++i) {
            dbg_dma_packed_prev[i] = last_packed_tail[i];
        }
    }
    // First 4 frames (16 words)
    for (uint32_t i = 0; i < 16; ++i) {
        dbg_dma_packed_next[i] = buf[i];
    }
    // Save last 4 frames as tail for next boundary
    uint32_t base = I2S_DMA_WORDS - 16;
    for (uint32_t i = 0; i < 16; ++i) {
        last_packed_tail[i] = buf[base + i];
    }
    // buf now contains packed 24-bit data -> goes to I2S DAC

    // 2. Call app synth if registered
    if (g_loader.state() == umi::kernel::AppState::Running) {
        // Clear output buffer
        for (uint32_t i = 0; i < BUFFER_SIZE; ++i) {
            synth_out_mono[i] = 0.0f;
        }

        std::array<umi::Event, INPUT_EVENT_CAPACITY> input_events{};
        size_t input_event_count = 0;
        while (g_midi_read != g_midi_write && input_event_count < input_events.size()) {
            const MidiMsg& msg = g_midi_queue[g_midi_read];
            uint8_t status = 0;
            uint8_t d1 = 0;
            uint8_t d2 = 0;

            if (msg.len >= 4) {
                // USB-MIDI 4-byte event packet: [CIN|Cable, Status, Data1, Data2]
                status = msg.data[1];
                d1 = msg.data[2];
                d2 = msg.data[3];
            } else if (msg.len == 3) {
                status = msg.data[0];
                d1 = msg.data[1];
                d2 = msg.data[2];
            } else if (msg.len == 2) {
                status = msg.data[0];
                d1 = msg.data[1];
            }

            if (status != 0) {
                input_events[input_event_count++] = umi::Event::make_midi(0, 0, status, d1, d2);
            }
            g_midi_read = (g_midi_read + 1) % MIDI_QUEUE_SIZE;
        }
        std::span<const umi::Event> input_span(input_events.data(), input_event_count);

        umi::AudioContext ctx{
            std::span<const umi::sample_t* const>{},
            std::span<umi::sample_t* const>(synth_out_channels),
            input_span,
            g_app_event_queue,
            g_shared.sample_rate,
            BUFFER_SIZE,
            dt,
            g_shared.sample_position,
        };

        // Call synth process (AudioContext-based)
        g_loader.call_process(ctx);
        g_shared.sample_position += BUFFER_SIZE;
        ++dbg_synth_called;

        // Save synth output for USB Audio IN (don't mix with I2S output)
        for (uint32_t i = 0; i < BUFFER_SIZE; ++i) {
            float synth_val = synth_out_mono[i];
            if (synth_val > 1.0f)
                synth_val = 1.0f;
            if (synth_val < -1.0f)
                synth_val = -1.0f;
            last_synth_out[i * 2] = static_cast<int16_t>(synth_val * 32767.0f);
            last_synth_out[i * 2 + 1] = last_synth_out[i * 2];
        }
    }

    // 3. Write to USB Audio IN (L=mic, R=synth) - directly, no extra buffering
    if (usb_audio.is_audio_in_streaming()) {
        if (g_current_sample_rate >= 96000) {
            __builtin_memset(stereo_buf, 0, sizeof(stereo_buf));
        } else {
            for (uint32_t i = 0; i < BUFFER_SIZE; ++i) {
                stereo_buf[i * 2] = pcm_buf[i];                // L = mic
                stereo_buf[i * 2 + 1] = last_synth_out[i * 2]; // R = synth (mono from L)
            }
        }
        usb_audio.write_audio_in(stereo_buf, BUFFER_SIZE);
        ++dbg_audio_in_write;
    }
}

// ============================================================================
// RTOS Task Entry Points
// ============================================================================

// Yield syscall wrapper for tasks (calls SVC to trigger PendSV from privileged mode)
static inline void task_yield() {
    using namespace umi::kernel::app_syscall;
    __asm__ volatile(
        "mov r0, %0\n"
        "svc 0\n"
        :: "i"(Yield)
        : "r0"
    );
}

// Control task entry: runs app's _start() which calls main()
// This task is lower priority (User) and runs when audio task is blocked
// The app's main() can run a main loop or return immediately
static void control_task_entry(void* arg) {
    // arg is the app's entry function (_start)
    auto app_entry = reinterpret_cast<void (*)()>(arg);

    if (app_entry != nullptr) {
        // Call app's _start() which initializes .data/.bss and calls main()
        // If app's main() has a main loop, it will run here
        // If app's main() just registers processor and returns, that's fine too
        app_entry();
    }

    // App's main() returned - enter idle loop
    // This allows apps that just register a processor to work correctly
    while (true) {
        // Block until shutdown notification
        g_control_task_state = TaskState::Blocked;

        // Yield to allow audio task to run (SVC -> PendSV)
        task_yield();

        // Check for shutdown
        if (g_control_task_notify & NOTIFY_SHUTDOWN) {
            break;
        }
    }
}

// Audio task entry: processes audio buffers when notified by I2S DMA ISR
// This task has highest priority (Realtime) and runs whenever audio is ready
static void audio_task_entry(void* arg) {
    (void)arg;

    while (true) {
        // Wait for audio buffer notification from ISR
        if (g_audio_ready_count == 0) {
            // No buffer ready - block and wait for notification
            g_audio_task_notify &= ~NOTIFY_AUDIO_READY;
            g_audio_task_state = TaskState::Blocked;

            // Yield to allow control task to run (SVC -> PendSV)
            task_yield();

            // Check if we got shutdown signal
            if (g_audio_task_notify & NOTIFY_SHUTDOWN) {
                break;
            }
            continue;
        }

        // Process audio buffer
        uint16_t* buf = const_cast<uint16_t*>(g_audio_ready_bufs[g_audio_ready_r]);
        g_audio_ready_r = static_cast<uint8_t>((g_audio_ready_r + 1) % AUDIO_READY_QUEUE_SIZE);
        uint8_t count = g_audio_ready_count;
        g_audio_ready_count = static_cast<uint8_t>(count - 1);

        // Process audio buffer
        // Note: dwt_cycle() cannot be used in unprivileged mode (DWT is privileged-only)
        process_audio_frame(buf);
    }
}

// System task entry: handles shell, SysEx, kernel services
// This task runs at Server priority (higher than User, lower than Realtime)
static void system_task_entry(void* arg) {
    (void)arg;

    // Initialize shell using ShellCommands (same as simulator)
    // Use 512-byte output buffer to save SRAM
    static umi::os::ShellCommands<Stm32StateProvider, 1024> shell(g_state_provider);
    g_shell = &shell;

    // Line buffer for command input (64 bytes is enough for shell commands)
    static umi::shell::LineBuffer<64> line_buffer;

    // Set up stdin callback for SysEx shell
    g_stdio.set_stdin_callback([](const uint8_t* data, size_t len, void*) {
        for (size_t i = 0; i < len; ++i) {
            char c = static_cast<char>(data[i]);
            if (line_buffer.process_char(c)) {
                // Line complete - execute command
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
    }, nullptr);

    while (true) {
        // Process pending SysEx shell input (from USB ISR)
        if (g_sysex_ready) {
            gpio_d.set(13);  // Orange LED ON: processing SysEx
            ++dbg_sysex_processed;
            g_stdio.process_message(g_sysex_buf, g_sysex_len);
            gpio_d.reset(13);  // Orange LED OFF: done
            g_sysex_ready = false;
        }

        // Block until SysEx ready or shutdown
        g_system_task_notify &= ~NOTIFY_SYSEX_READY;
        g_system_task_state = TaskState::Blocked;
        task_yield();

        // Check for shutdown
        if (g_system_task_notify & NOTIFY_SHUTDOWN) {
            break;
        }
    }
}

// Idle task entry: runs when no other task is ready
// Implements WFI (Wait For Interrupt) for power saving
static void idle_task_entry(void* arg) {
    (void)arg;

    while (true) {
        // Wait for interrupt - CPU sleeps until next interrupt
        __asm__ volatile("wfi");
        // After WFI, an interrupt has occurred
        // PendSV will switch to a higher priority task if one became ready
    }
}

static void audio_loop() {
    // Main loop - process audio when buffer ready
    while (true) {
        // Handle sample rate change request (from USB callback)
        if (g_sample_rate_change_pending) {
            g_sample_rate_change_pending = false;
            uint32_t new_rate = g_new_sample_rate;

            // Only change if rate is different and valid
            if (new_rate != g_current_sample_rate && (new_rate == 44100 || new_rate == 48000 || new_rate == 96000)) {

                dbg_sr_change_step = 1; // Starting

                // Block USB audio OUT RX during reconfiguration to avoid stale packets
                usb_audio.block_audio_out_rx((new_rate >= 96000) ? 48 : 12);

                // Disable NVIC for DMA to prevent interrupts during reconfiguration
                constexpr uint32_t NVIC_ICER0 = 0xE000E180;
                *reinterpret_cast<volatile uint32_t*>(NVIC_ICER0) = (1U << 16); // DMA1_Stream5

                // Stop audio DMA
                dma_i2s.disable();

                // Wait for DMA to fully stop
                for (int i = 0; i < 1000; ++i) {
                    __asm__ volatile("nop");
                }

                i2s3.disable();

                dbg_sr_change_step = 2; // DMA/I2S stopped

                // Clear audio buffers to prevent glitches
                for (uint32_t i = 0; i < I2S_DMA_WORDS; ++i) {
                    audio_buf0[i] = 0;
                    audio_buf1[i] = 0;
                }

                // Reconfigure PLL and I2S divider for new sample rate
                // configure_plli2s() also reinitializes I2S with correct divider
                uint32_t actual_rate = configure_plli2s(new_rate);

                dbg_sr_change_step = 3; // PLL configured

                // Fully reinitialize DMA with clean state
                dma_i2s.init(audio_buf0, audio_buf1, I2S_DMA_WORDS, i2s3.dr_addr());

                // Clear any pending audio buffers
                g_audio_ready_count = 0;
                g_audio_ready_w = 0;
                g_audio_ready_r = 0;

                // Re-enable DMA interrupt in NVIC
                constexpr uint32_t NVIC_ISER0 = 0xE000E100;
                *reinterpret_cast<volatile uint32_t*>(NVIC_ISER0) = (1U << 16); // DMA1_Stream5

                // Re-enable audio
                i2s3.enable_dma();
                i2s3.enable();
                dma_i2s.enable();

                dbg_sr_change_step = 4; // I2S restarted

                // Update shared memory for application
                g_current_sample_rate = new_rate;
                g_shared.set_sample_rate(new_rate);

                // Update debug counters
                ++dbg_sample_rate_change_count;
                dbg_current_sample_rate = new_rate;

                // Update USB feedback calculator with actual I2S rate
                dbg_actual_rate = actual_rate;
                usb_audio.set_actual_rate(actual_rate);
                usb_audio.reset_audio_out(actual_rate);

                dbg_sr_change_step = 5; // Complete
            }
        }

        // Process PDM decimation when ready (moved from ISR)
        if (g_pdm_ready && g_current_sample_rate < 96000) {
            g_pdm_ready = false;
            uint16_t* pdm_data = const_cast<uint16_t*>(g_active_pdm_buf);
            cic_decimator.process_buffer(pdm_data, PDM_BUF_SIZE, pcm_buf, PCM_BUF_SIZE);
        }

        // Process audio when I2S buffer ready
        uint32_t rate = g_current_sample_rate;
        if (rate != 0) {
            dbg_proc_cycles_budget = static_cast<uint32_t>((168000000ull * BUFFER_SIZE + rate - 1) / rate);
        }
        while (g_audio_ready_count > 0) {
            uint16_t* buf = const_cast<uint16_t*>(g_audio_ready_bufs[g_audio_ready_r]);
            g_audio_ready_r = static_cast<uint8_t>((g_audio_ready_r + 1) % AUDIO_READY_QUEUE_SIZE);
            uint8_t count = g_audio_ready_count;
            g_audio_ready_count = static_cast<uint8_t>(count - 1);
            uint32_t start = dwt_cycle();
            process_audio_frame(buf);
            uint32_t elapsed = dwt_cycle() - start;
            dbg_proc_cycles_last = elapsed;
            if (elapsed > dbg_proc_cycles_max) {
                dbg_proc_cycles_max = elapsed;
            }
            if (dbg_proc_cycles_budget != 0 && elapsed > dbg_proc_cycles_budget) {
                ++dbg_proc_cycles_over;
            }
        }

        // Update debug counters
        dbg_underrun = usb_audio.underrun_count();
        dbg_overrun = usb_audio.overrun_count();
        dbg_streaming = usb_audio.is_streaming() ? 1 : 0;
        if (dbg_streaming == 0) {
            dbg_out_buf_min = 0;
            dbg_out_buf_max = 0;
            dbg_audio_ready_max = 0;
        }
        dbg_audio_ready_level = g_audio_ready_count;
        dbg_out_buf_level = usb_audio.buffered_frames();
        dbg_in_buf_level = usb_audio.in_buffered_frames();
        dbg_feedback = usb_audio.current_feedback();
        dbg_fb_count = usb_hal.dbg_ep2_fb_count_;
        dbg_fb_xfrc = usb_hal.dbg_ep2_fb_xfrc_;
        dbg_fb_int_count = usb_hal.dbg_ep2_int_count_;
        dbg_fb_int_last = usb_hal.dbg_ep2_int_last_;
        dbg_fb_actual = usb_hal.dbg_ep2_last_fb_;
        dbg_fb_sent = usb_audio.dbg_fb_sent_count();
        dbg_set_iface_val = (static_cast<uint32_t>(usb_audio.dbg_last_set_iface()) << 8) | usb_audio.dbg_last_set_alt();
        dbg_out_iface_num = usb_audio.audio_out_interface_num();
        dbg_out_rx_last_len = usb_audio.dbg_out_rx_last_len();
        dbg_out_rx_min_len = usb_audio.dbg_out_rx_min_len();
        dbg_out_rx_max_len = usb_audio.dbg_out_rx_max_len();
        dbg_out_rx_short = usb_audio.dbg_out_rx_short_count();
        dbg_out_rx_disc_count = usb_audio.dbg_out_rx_disc_count();
        dbg_out_rx_disc_max = usb_audio.dbg_out_rx_disc_max();
        dbg_out_rx_disc_last = usb_audio.dbg_out_rx_disc_last();
        usb_audio.set_dbg_out_rx_enabled(dbg_out_rx_debug_enable != 0);
        usb_audio.set_dbg_out_rx_disc_threshold(dbg_out_rx_disc_threshold);
        dbg_out_rx_intra_count = usb_audio.dbg_out_rx_intra_count();
        dbg_out_rx_intra_max = usb_audio.dbg_out_rx_intra_max();
        dbg_out_rx_intra_last = usb_audio.dbg_out_rx_intra_last();
        usb_audio.set_dbg_out_rx_intra_log_threshold(dbg_out_rx_intra_log_threshold);
        dbg_out_rx_intra_event_count = usb_audio.dbg_out_rx_intra_event_count();
        dbg_out_rx_intra_event_len = usb_audio.dbg_out_rx_intra_event_len();
        dbg_out_rx_intra_event_cur_l = usb_audio.dbg_out_rx_intra_event_cur_l();
        dbg_out_rx_intra_event_cur_r = usb_audio.dbg_out_rx_intra_event_cur_r();
        dbg_out_rx_intra_event_prev_l = usb_audio.dbg_out_rx_intra_event_prev_l();
        dbg_out_rx_intra_event_prev_r = usb_audio.dbg_out_rx_intra_event_prev_r();
        dbg_out_rx_intra_event_dl = usb_audio.dbg_out_rx_intra_event_dl();
        dbg_out_rx_intra_event_dr = usb_audio.dbg_out_rx_intra_event_dr();
        if (dbg_out_rx_debug_enable != 0) {
            usb_audio.set_audio_out_packet_callback(on_audio_out_packet);
        } else {
            usb_audio.set_audio_out_packet_callback(nullptr);
        }
        dbg_usb_out_doepint = usb_hal.dbg_ep1_doepint_;
        dbg_usb_out_doepint_err_count = usb_hal.dbg_ep1_doepint_err_count_;
        dbg_usb_out_doepint_err_last = usb_hal.dbg_ep1_doepint_err_last_;
        dbg_usb_out_doepint_xfrc = usb_hal.dbg_ep1_doepint_xfrc_count_;
        dbg_usb_out_doepint_stup = usb_hal.dbg_ep1_doepint_stup_count_;
        dbg_usb_out_doepint_otepdis = usb_hal.dbg_ep1_doepint_otepdis_count_;
        dbg_usb_out_doepint_stsphsrx = usb_hal.dbg_ep1_doepint_stsphsrx_count_;
        dbg_usb_out_doepint_nak = usb_hal.dbg_ep1_doepint_nak_count_;
        dbg_usb_rxflvl_count = usb_hal.dbg_rxflvl_count_;
        dbg_usb_rxflvl_out = usb_hal.dbg_rxflvl_out_count_;
        dbg_usb_rxflvl_setup = usb_hal.dbg_rxflvl_setup_count_;
        dbg_usb_rxflvl_other = usb_hal.dbg_rxflvl_other_count_;
        dbg_usb_ep1_bcnt_last = usb_hal.dbg_ep1_out_bcnt_last_;
        dbg_usb_ep1_bcnt_min = usb_hal.dbg_ep1_out_bcnt_min_;
        dbg_usb_ep1_bcnt_max = usb_hal.dbg_ep1_out_bcnt_max_;
        dbg_usb_ep1_zero = usb_hal.dbg_ep1_out_zero_count_;
        dbg_usb_ep1_zero_outdata = usb_hal.dbg_ep1_out_zero_outdata_;
        dbg_usb_ep1_zero_setup = usb_hal.dbg_ep1_out_zero_setup_;
        dbg_usb_ep1_zero_other = usb_hal.dbg_ep1_out_zero_other_;
        dbg_usb_ep1_zero_rxsts_last = usb_hal.dbg_ep1_out_zero_rxsts_last_;
        dbg_usb_ep1_pktsts = usb_hal.dbg_ep1_pktsts_last_;
        dbg_usb_ep1_first_word = usb_hal.dbg_ep1_out_first_word_;
        dbg_usb_ep1_last_word = usb_hal.dbg_ep1_out_last_word_;
        dbg_usb_rxflvl_pktsts_0 = usb_hal.dbg_rxflvl_pktsts_count_[0];
        dbg_usb_rxflvl_pktsts_1 = usb_hal.dbg_rxflvl_pktsts_count_[1];
        dbg_usb_rxflvl_pktsts_2 = usb_hal.dbg_rxflvl_pktsts_count_[2];
        dbg_usb_rxflvl_pktsts_3 = usb_hal.dbg_rxflvl_pktsts_count_[3];
        dbg_usb_rxflvl_pktsts_4 = usb_hal.dbg_rxflvl_pktsts_count_[4];
        dbg_usb_rxflvl_pktsts_5 = usb_hal.dbg_rxflvl_pktsts_count_[5];
        dbg_usb_rxflvl_pktsts_6 = usb_hal.dbg_rxflvl_pktsts_count_[6];
        dbg_usb_rxflvl_pktsts_7 = usb_hal.dbg_rxflvl_pktsts_count_[7];
        dbg_usb_rxflvl_pktsts_8 = usb_hal.dbg_rxflvl_pktsts_count_[8];
        dbg_usb_rxflvl_pktsts_9 = usb_hal.dbg_rxflvl_pktsts_count_[9];
        dbg_usb_rxflvl_pktsts_a = usb_hal.dbg_rxflvl_pktsts_count_[10];
        dbg_usb_rxflvl_pktsts_b = usb_hal.dbg_rxflvl_pktsts_count_[11];
        dbg_usb_rxflvl_pktsts_c = usb_hal.dbg_rxflvl_pktsts_count_[12];
        dbg_usb_rxflvl_pktsts_d = usb_hal.dbg_rxflvl_pktsts_count_[13];
        dbg_usb_rxflvl_pktsts_e = usb_hal.dbg_rxflvl_pktsts_count_[14];
        dbg_usb_rxflvl_pktsts_f = usb_hal.dbg_rxflvl_pktsts_count_[15];

        if (dbg_fb_mode == 0) {
            usb_audio.set_feedback_mode(umiusb::FeedbackMode::FifoLevel);
        } else {
            usb_audio.set_feedback_mode(umiusb::FeedbackMode::BufferDelta);
        }
        if (dbg_fb_delta_gain != 0) {
            usb_audio.set_feedback_delta_gain(dbg_fb_delta_gain);
        } else {
            usb_audio.set_feedback_delta_gain_auto();
        }

        if (dbg_fb_test_mode != 0) {
            uint32_t phase = (g_tick_us / 1000000) & 1U;
            uint32_t fb = phase ? 0x160000 : 0x1A0000; // ~88kHz / ~104kHz
            usb_audio.set_feedback_override(true, fb);
        } else if (dbg_fb_override_enable != 0) {
            usb_audio.set_feedback_override(true, dbg_fb_override_value);
        } else {
            usb_audio.set_feedback_override(false, 0);
        }
        dbg_fb_diepctl = usb_hal.dbg_ep2_diepctl_;
        dbg_fb_diepint = usb_hal.dbg_ep2_diepint_;
        dbg_fb_fifo_before = usb_hal.dbg_ep2_fifo_before_;
        dbg_fb_fifo_after = usb_hal.dbg_ep2_fifo_after_;
        dbg_iepint_count = usb_hal.dbg_gintsts_iepint_count_;
        dbg_last_daint = usb_hal.dbg_last_daint_;
        dbg_last_daintmsk = usb_hal.dbg_last_daintmsk_;
        dbg_pll_ppm = usb_audio.pll_adjustment_ppm();
        dbg_asrc_rate = usb_audio.current_asrc_rate();
        if (dbg_out_buf_min == 0 || dbg_out_buf_level < dbg_out_buf_min) {
            dbg_out_buf_min = dbg_out_buf_level;
        }
        if (dbg_out_buf_level > dbg_out_buf_max) {
            dbg_out_buf_max = dbg_out_buf_level;
        }

        // Sample rate debug counters
        dbg_sr_get_cur = usb_audio.dbg_sr_get_cur();
        dbg_sr_set_cur = usb_audio.dbg_sr_set_cur();
        dbg_sr_ep0_rx = usb_audio.dbg_sr_ep0_rx();
        dbg_sr_last_req = usb_audio.dbg_sr_last_req();
        dbg_sr_ep_req = usb_audio.dbg_sr_ep_req();
    }
}

// ============================================================================
// Kernel Initialization (after app startup or on direct boot)
// ============================================================================

static void kernel_init_after_app() {
    // Initialize USB early (ensure enumeration even if audio init stalls)
    init_usb();

    // Initialize SysTick
    init_systick();
    init_cycle_counter();

    if (USB_ONLY_DEBUG) {
        gpio_d.set(12); // Green LED on: USB-only mode
        while (true) {
            __asm__ volatile("wfi");
        }
    }

    // Initialize audio hardware
    init_audio();
    init_pdm_mic();

    gpio_d.reset(15); // Turn off blue
    gpio_d.reset(12); // Green LED off - will turn on when synth outputs

    // Enter audio processing loop
    audio_loop();
}

// Continue from Yield syscall entry point
// This function is called via exception return after app's Yield syscall
extern "C" void continue_from_yield() {
    kernel_init_after_app();
}

// ============================================================================
// Main
// ============================================================================

int main() {
    RCC::init_168mhz();
    init_gpio();

    gpio_d.set(15); // Blue LED - startup

    // Set initial actual sample rate for USB feedback calculation
    // 48000Hz nominal → 47991Hz actual due to PLLI2S limitations
    usb_audio.set_actual_rate(47991);
    dbg_actual_rate = 47991;

    // Initialize shared memory with sample rate and dt
    g_shared.set_sample_rate(SAMPLE_RATE); // Sets both sample_rate and dt
    g_shared.buffer_size = BUFFER_SIZE;
    g_shared.sample_position = 0;

    // Configure app loader
    g_loader.set_app_memory(_app_ram_start, reinterpret_cast<uintptr_t>(&_app_ram_size));
    g_loader.set_shared_memory(&g_shared);

    // Direct XIP execution using .umiapp header entry point
    using EntryFn = void (*)();
    const auto* app_header = reinterpret_cast<const umi::kernel::AppHeader*>(_app_image_start);
    bool app_valid = app_header->valid_magic() && app_header->compatible_abi();
    EntryFn app_entry = nullptr;

    if (app_valid) {
        uintptr_t entry_addr = reinterpret_cast<uintptr_t>(app_header->entry_point(_app_image_start)) | 1;
        app_entry = reinterpret_cast<EntryFn>(entry_addr);

        // Mark app as running (needed for syscalls)
        g_loader.set_entry(app_entry);

        // Set up app stack pointer (top of app RAM)
        uint32_t app_sp = reinterpret_cast<uint32_t>(_app_ram_start) +
                          reinterpret_cast<uintptr_t>(&_app_ram_size);

        // For XIP (eXecute In Place) apps, configure MPU regions directly
        // App code executes from flash, data in app RAM
        // Configure MPU for unprivileged app execution
        // Region 0: Kernel SRAM (read-write for now - needed for g_loader access)
        // TODO: Move processor info to shared memory for proper isolation
        umi::kernel::mpu::configure_region(umi::kernel::mpu::Region::Kernel, {
            .base = reinterpret_cast<void*>(0x20000000),
            .size = 48 * 1024,  // 48KB kernel SRAM
            .readable = true,
            .writable = true,
            .executable = false,
            .privileged_only = false,  // Allow task access to kernel data (temporary)
            .device_memory = false,
        });

        // Region 1: App code in flash (read-only, executable, user accessible)
        // App image: 0x08060000 - 0x08080000 (128KB)
        umi::kernel::mpu::configure_region(umi::kernel::mpu::Region::AppText, {
            .base = const_cast<uint8_t*>(_app_image_start),
            .size = 128 * 1024,  // 128KB for app code/data
            .readable = true,
            .writable = false,
            .executable = true,
            .privileged_only = false,
            .device_memory = false,
        });

        // Region 2: App data in RAM - lower 32KB (read-write, non-executable)
        // Base 0x2000C000, size 32KB (0x8000)
        umi::kernel::mpu::configure_region(umi::kernel::mpu::Region::AppData, {
            .base = _app_ram_start,
            .size = 32 * 1024,  // Lower 32KB of app RAM
            .readable = true,
            .writable = true,
            .executable = false,
            .privileged_only = false,
            .device_memory = false,
        });

        // Region 3: App data in RAM - upper 16KB (read-write, non-executable)
        // Base 0x20014000, size 16KB (0x4000)
        umi::kernel::mpu::configure_region(umi::kernel::mpu::Region::AppStack, {
            .base = reinterpret_cast<uint8_t*>(_app_ram_start) + 32 * 1024,
            .size = 16 * 1024,  // Upper 16KB of app RAM (stack area)
            .readable = true,
            .writable = true,
            .executable = false,
            .privileged_only = false,
            .device_memory = false,
        });

        // Region 3: Shared memory (read-write, non-executable)
        // Use full SHARED region (16KB) as sizeof may not be power of 2
        umi::kernel::mpu::configure_region(umi::kernel::mpu::Region::Shared, {
            .base = reinterpret_cast<void*>(0x20018000),
            .size = 16 * 1024,  // 16KB SHARED region
            .readable = true,
            .writable = true,
            .executable = false,
            .privileged_only = false,
            .device_memory = false,
        });

        // Region 5: Peripherals (device memory, allow user access for now)
        // APB1/APB2/AHB1 peripherals: 0x40000000 - 0x50000000
        umi::kernel::mpu::configure_region(umi::kernel::mpu::Region::Peripherals, {
            .base = reinterpret_cast<void*>(0x40000000),
            .size = 256 * 1024 * 1024,  // 256MB covers all peripherals
            .readable = true,
            .writable = true,
            .executable = false,
            .privileged_only = false,  // Allow app access to peripherals
            .device_memory = true,
        });

        // Region 6: CCM (Core Coupled Memory) for task stacks
        // CCM: 0x10000000 - 0x10010000 (64KB)
        umi::kernel::mpu::configure_region(static_cast<umi::kernel::mpu::Region>(6), {
            .base = reinterpret_cast<void*>(0x10000000),
            .size = 64 * 1024,  // 64KB CCM
            .readable = true,
            .writable = true,
            .executable = false,
            .privileged_only = false,  // Allow task access to stacks
            .device_memory = false,
        });

        // Region 7: Kernel flash (read-only, executable)
        umi::kernel::mpu::configure_region(static_cast<umi::kernel::mpu::Region>(7), {
            .base = reinterpret_cast<void*>(0x08000000),
            .size = 384 * 1024,  // 384KB kernel flash
            .readable = true,
            .writable = false,
            .executable = true,
            .privileged_only = false,  // Allow task to call kernel code
            .device_memory = false,
        });

        // Enable MPU with default memory map for privileged mode
        umi::kernel::mpu::enable(true);

        // Mark app as loaded (needed for syscalls) - don't call yet
        // App will be started as a Control task in unprivileged mode
        (void)app_sp;  // Will be used for task stacks when MPU is enabled
    }

    // Initialize hardware before starting RTOS tasks
    init_usb();
    init_systick();
    init_cycle_counter();

    if (USB_ONLY_DEBUG) {
        gpio_d.set(12); // Green LED on: USB-only mode
        while (true) {
            __asm__ volatile("wfi");
        }
    }

    init_audio();
    init_pdm_mic();

    gpio_d.reset(15); // Turn off blue
    gpio_d.reset(12); // Green LED off

    // Initialize RTOS task contexts (4 tasks)
    // Priority order: Realtime (Audio) > Server (System) > User (Control) > Idle

    // Audio task: Realtime priority, processes audio when notified by DMA ISR
    umi::port::cm4::init_task_context(
        g_audio_tcb,
        g_audio_task_stack,
        AUDIO_TASK_STACK_SIZE,
        audio_task_entry,
        nullptr,
        true  // uses FPU
    );

    // System task: Server priority, handles shell/SysEx/drivers
    umi::port::cm4::init_task_context(
        g_system_tcb,
        g_system_task_stack,
        SYSTEM_TASK_STACK_SIZE,
        system_task_entry,
        nullptr,
        false  // no FPU needed for shell
    );

    // Control task: User priority, runs app's _start() -> main()
    // Pass app_entry as argument so control_task_entry can call it
    void* app_entry_arg = app_valid ? reinterpret_cast<void*>(app_entry) : nullptr;
    umi::port::cm4::init_task_context(
        g_control_tcb,
        g_control_task_stack,
        CONTROL_TASK_STACK_SIZE,
        control_task_entry,
        app_entry_arg,
        true  // May use FPU if app uses it
    );

    // Idle task: lowest priority, runs WFI when no other task is ready
    umi::port::cm4::init_task_context(
        g_idle_tcb,
        g_idle_task_stack,
        IDLE_TASK_STACK_SIZE,
        idle_task_entry,
        nullptr,
        false  // no FPU needed
    );

    // Set initial task states
    g_audio_task_state = TaskState::Blocked;   // Wait for first DMA interrupt
    g_system_task_state = TaskState::Blocked;  // Wait for first SysEx
    g_control_task_state = TaskState::Ready;   // Ready to run app
    g_idle_task_state = TaskState::Ready;      // Always ready

    // Initial context is Control task (highest priority ready task)
    g_current_tcb = &g_control_tcb;
    umi_cm4_current_tcb = &g_control_tcb;
    g_control_task_state = TaskState::Running;

    // Mark RTOS as started - allows SysTick to manage task wakeups
    g_rtos_started = true;

    // Set up PSP for control task and switch to thread mode
    __asm__ volatile("msr psp, %0" :: "r"(g_control_tcb.stack_ptr));
    __asm__ volatile("isb");

    // Enable PSP usage in thread mode
    // CONTROL[1] = 1 (use PSP), CONTROL[0] = 0 (privileged for kernel tasks)
    // Note: App task runs with MPU restrictions
    __asm__ volatile(
        "mov r0, #2\n"
        "msr control, r0\n"
        "isb\n"
        ::: "r0"
    );

    // Jump to control task - this never returns
    // When control task blocks, PendSV switches to other tasks
    control_task_entry(app_entry_arg);

    // Should never reach here
    return 0;
}

// ============================================================================
// Vector Table and Startup
// ============================================================================

extern "C" {
[[noreturn]] void Reset_Handler();
void NMI_Handler() {
    while (true) {
    }
}
void HardFault_Handler() {
    gpio_d.set(14);
    while (true) {
    }
}
void MemManage_Handler() {
    gpio_d.set(14);
    while (true) {
    }
}
void BusFault_Handler() {
    gpio_d.set(14);
    while (true) {
    }
}
void UsageFault_Handler() {
    gpio_d.set(14);
    while (true) {
    }
}
void DebugMon_Handler() {
    while (true) {
    }
}
// Note: PendSV_Handler is defined above for RTOS context switching
}

// Boot Vector Table (minimal - only SP and Reset needed in Flash)
// The full vector table is in SRAM and managed by umi::irq system.
// VTOR is updated to point to SRAM table early in Reset_Handler.
__attribute__((section(".isr_vector"), used)) const void* const g_boot_vectors[2] = {
    reinterpret_cast<const void*>(&_estack),      // 0: Initial SP
    reinterpret_cast<const void*>(Reset_Handler), // 1: Reset
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

    // Initialize dynamic IRQ system (SRAM vector table)
    umi::irq::init();
    namespace exc = umi::backend::cm::exc;
    umi::irq::set_handler(exc::HardFault, HardFault_Handler);
    umi::irq::set_handler(exc::MemManage, MemManage_Handler);
    umi::irq::set_handler(exc::BusFault, BusFault_Handler);
    umi::irq::set_handler(exc::UsageFault, UsageFault_Handler);
    umi::irq::set_handler(exc::SVCall, SVC_Handler);
    umi::irq::set_handler(exc::PendSV, PendSV_Handler);
    umi::irq::set_handler(exc::SysTick, SysTick_Handler);
    namespace irqn = umi::stm32f4::irq;
    umi::irq::set_handler(irqn::DMA1_Stream3, DMA1_Stream3_IRQHandler);
    umi::irq::set_handler(irqn::DMA1_Stream5, DMA1_Stream5_IRQHandler);
    umi::irq::set_handler(irqn::OTG_FS, OTG_FS_IRQHandler);

    // Call C++ global constructors (init_array)
    for (void (**p)(void) = __init_array_start; p < __init_array_end; ++p) {
        (*p)();
    }

    // Call main
    main();

    // Should not return
    while (true) {
    }
}

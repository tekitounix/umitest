// SPDX-License-Identifier: MIT
// =====================================================================
// UMI-OS Web Adapter - Real Kernel Integration
// =====================================================================
//
// This adapter allows running embedded UMI-OS applications in browser
// using the ACTUAL umi::Kernel code, not a simulation.
//
// Architecture:
//   ┌─────────────────────────────────────────┐
//   │ Application (synth_processor.hh)        │  ← Identical to embedded
//   └─────────────────────────────────────────┘
//             ↓ process(AudioContext&)
//   ┌─────────────────────────────────────────┐
//   │ WebKernelAdapter                        │  ← This file
//   │ - Creates AudioContext                  │
//   │ - Manages event queues                  │
//   │ - Tracks DSP load                       │
//   └─────────────────────────────────────────┘
//             ↓
//   ┌─────────────────────────────────────────┐
//   │ umi::Kernel<4, 8, HW>                   │  ← Real kernel (optional)
//   └─────────────────────────────────────────┘
//             ↓
//   ┌─────────────────────────────────────────┐
//   │ umi::Hw<WasmHwImpl>                     │  ← web_hal.hh
//   └─────────────────────────────────────────┘
//
// Usage:
//   1. Include your embedded app's processor
//   2. Use UMI_WEB_ADAPTER_EXPORT() macro
//   3. Build with emcc
//   4. Load in browser with web_sim.js runtime
//
// =====================================================================

#pragma once

#include "web_hal.hh"

// Include umios types directly - same types as embedded
#include <umios/types.hh>
#include <umios/event.hh>
#include <umios/audio_context.hh>
#include <umios/umi_kernel.hh>

#include <array>
#include <cstdint>
#include <span>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define UMI_WEB_EXPORT __attribute__((used, visibility("default")))
#else
#define UMI_WEB_EXPORT
#endif

namespace umi::web {

// =====================================================================
// Type aliases for umios compatibility
// =====================================================================
// These are now using actual umios types, ensuring binary compatibility

using HW = umi::Hw<WasmHwImpl>;

// =====================================================================
// Configuration
// =====================================================================

struct WebAdapterConfig {
    uint32_t sample_rate = 48000;
    uint32_t buffer_size = 128;
    uint8_t num_inputs = 0;
    uint8_t num_outputs = 1;
};

// =====================================================================
// Web Kernel Adapter
// =====================================================================
//
// Bridges embedded processors to the WASM environment.
// Uses actual umios types (AudioContext, Event, EventQueue) ensuring
// identical code paths between embedded and web builds.
//

template<typename Processor, WebAdapterConfig Config = WebAdapterConfig{}>
class WebKernelAdapter {
public:
    WebKernelAdapter() = default;

    void init() {
        sample_rate_ = Config.sample_rate;
        dt_ = 1.0f / static_cast<float>(sample_rate_);
        sample_position_ = 0;

        // Initialize HAL
        WasmHwImpl::reset();
        WasmHwImpl::set_audio_params(sample_rate_, Config.buffer_size);

        // Initialize processor (same call as embedded)
        processor_.init(static_cast<float>(sample_rate_));
        initialized_ = true;

        midi_rx_count_ = 0;
        audio_buffer_count_ = 0;
    }

    void reset() {
        sample_position_ = 0;
        input_events_.clear();
        output_events_.clear();
        WasmHwImpl::reset();

        if (initialized_) {
            processor_.init(static_cast<float>(sample_rate_));
        }

        midi_rx_count_ = 0;
        audio_buffer_count_ = 0;
    }

    void process(const float* input, float* output, uint32_t frames, uint32_t sample_rate) {
        // Handle sample rate change
        if (sample_rate != sample_rate_) {
            sample_rate_ = sample_rate;
            dt_ = 1.0f / static_cast<float>(sample_rate_);
            WasmHwImpl::set_audio_params(sample_rate_, frames);
            processor_.init(static_cast<float>(sample_rate_));
        }

        if (!initialized_) {
            init();
        }

        // Setup buffer pointers
        input_ptrs_[0] = input;
        output_ptrs_[0] = output;

        // Snapshot and sort input events
        process_input_events(frames);

        // Build AudioContext using actual umios types
        umi::AudioContext ctx{
            .inputs = std::span<const sample_t* const>(input_ptrs_.data(), Config.num_inputs),
            .outputs = std::span<sample_t* const>(output_ptrs_.data(), Config.num_outputs),
            .input_events = std::span<const umi::Event>(input_events_snapshot_.data(), input_events_count_),
            .output_events = output_events_,
            .sample_rate = sample_rate_,
            .buffer_size = frames,
            .dt = dt_,
            .sample_position = sample_position_,
        };

        // Clear output buffer
        for (uint32_t i = 0; i < frames; ++i) {
            output[i] = 0.0f;
        }

        // Begin DSP load measurement
        WasmHwImpl::begin_audio_process();
        WasmHwImpl::add_events(static_cast<uint32_t>(input_events_count_));

        // Process audio (identical call to embedded)
        processor_.process(ctx);

        // Count active voices for DSP load
        uint32_t active_voices = count_active_voices();
        WasmHwImpl::set_active_voices(active_voices);

        // End DSP load measurement
        WasmHwImpl::end_audio_process();

        // Advance time
        sample_position_ += frames;
        uint64_t delta_us = (frames * 1000000ULL) / sample_rate_;
        WasmHwImpl::advance_time(delta_us);

        // Update stats
        audio_buffer_count_++;

        // Feed watchdog
        WasmHwImpl::watchdog_feed();

        // Clear processed events
        input_events_count_ = 0;
    }

    // =========================================================================
    // MIDI Input (using umios Event)
    // =========================================================================

    void send_midi(uint8_t status, uint8_t data1, uint8_t data2, uint32_t sample_offset = 0) {
        umi::Event ev = umi::Event::make_midi(0, sample_offset, status, data1, data2);
        (void)input_events_.push(ev);
        midi_rx_count_++;
    }

    void note_on(uint8_t note, uint8_t velocity, uint8_t channel = 0) {
        send_midi(0x90 | (channel & 0x0F), note, velocity);
    }

    void note_off(uint8_t note, uint8_t velocity = 0, uint8_t channel = 0) {
        send_midi(0x80 | (channel & 0x0F), note, velocity);
    }

    void control_change(uint8_t cc, uint8_t value, uint8_t channel = 0) {
        send_midi(0xB0 | (channel & 0x0F), cc, value);
    }

    // =========================================================================
    // Accessors
    // =========================================================================

    uint64_t sample_position() const { return sample_position_; }
    uint32_t sample_rate() const { return sample_rate_; }
    Processor& processor() { return processor_; }
    const Processor& processor() const { return processor_; }

    // Stats
    uint32_t midi_rx_count() const { return midi_rx_count_; }
    uint32_t audio_buffer_count() const { return audio_buffer_count_; }
    uint32_t dsp_load() const { return WasmHwImpl::dsp_load(); }
    uint32_t dsp_peak() const { return WasmHwImpl::dsp_peak(); }

    // Active voice count (for DSP load display)
    uint32_t count_active_voices() const {
        return get_active_voices_impl(nullptr);
    }

private:
    // SFINAE: use if processor has active_voice_count()
    template<typename P = Processor>
    auto get_active_voices_impl(int*) const
        -> decltype(std::declval<const P&>().active_voice_count()) {
        return processor_.active_voice_count();
    }

    // Fallback: use if processor doesn't have active_voice_count()
    uint32_t get_active_voices_impl(...) const {
        return 1;  // Assume 1 voice as default
    }

    void process_input_events(uint32_t frames) {
        input_events_count_ = 0;
        umi::Event ev;
        // Pop events in FIFO order
        while (input_events_.pop(ev) && input_events_count_ < input_events_snapshot_.size()) {
            if (ev.sample_pos >= frames) {
                ev.sample_pos = frames - 1;
            }
            input_events_snapshot_[input_events_count_++] = ev;
        }

        // Sort by sample position (insertion sort for small arrays)
        for (size_t i = 1; i < input_events_count_; ++i) {
            umi::Event key = input_events_snapshot_[i];
            size_t j = i;
            while (j > 0 && input_events_snapshot_[j-1].sample_pos > key.sample_pos) {
                input_events_snapshot_[j] = input_events_snapshot_[j-1];
                --j;
            }
            input_events_snapshot_[j] = key;
        }
    }

private:
    Processor processor_{};
    uint32_t sample_rate_ = Config.sample_rate;
    float dt_ = 1.0f / Config.sample_rate;
    uint64_t sample_position_ = 0;
    bool initialized_ = false;

    // Buffer pointers
    std::array<const sample_t*, 2> input_ptrs_{};
    std::array<sample_t*, 2> output_ptrs_{};

    // Event queues (using actual umios EventQueue)
    umi::EventQueue<> input_events_;
    umi::EventQueue<> output_events_;

    // Event snapshot for processing
    std::array<umi::Event, umi::MAX_EVENTS_PER_BUFFER> input_events_snapshot_{};
    size_t input_events_count_ = 0;

    // Stats
    uint32_t midi_rx_count_ = 0;
    uint32_t audio_buffer_count_ = 0;
};

// Backward compatibility alias
template<typename Proc, WebAdapterConfig Config = WebAdapterConfig{}>
using WebSimAdapter = WebKernelAdapter<Proc, Config>;

} // namespace umi::web

// ============================================================================
// Export Macros
// ============================================================================
// These macros create the WASM exports that the JavaScript runtime calls.

#define UMI_WEB_ADAPTER_EXPORT(ProcessorType) \
    static umi::web::WebKernelAdapter<ProcessorType> g_web_adapter; \
    \
    extern "C" { \
        UMI_WEB_EXPORT void umi_sim_init(void) { \
            g_web_adapter.init(); \
        } \
        UMI_WEB_EXPORT void umi_sim_reset(void) { \
            g_web_adapter.reset(); \
        } \
        UMI_WEB_EXPORT void umi_sim_process(const float* in, float* out, \
                                             uint32_t frames, uint32_t sr) { \
            g_web_adapter.process(in, out, frames, sr); \
        } \
        UMI_WEB_EXPORT void umi_sim_note_on(uint8_t note, uint8_t vel) { \
            g_web_adapter.note_on(note, vel); \
        } \
        UMI_WEB_EXPORT void umi_sim_note_off(uint8_t note) { \
            g_web_adapter.note_off(note); \
        } \
        UMI_WEB_EXPORT void umi_sim_cc(uint8_t cc, uint8_t value) { \
            g_web_adapter.control_change(cc, value); \
        } \
        UMI_WEB_EXPORT void umi_sim_midi(uint8_t status, uint8_t d1, uint8_t d2) { \
            g_web_adapter.send_midi(status, d1, d2); \
        } \
        UMI_WEB_EXPORT uint32_t umi_sim_position_lo(void) { \
            return static_cast<uint32_t>(g_web_adapter.sample_position()); \
        } \
        UMI_WEB_EXPORT uint32_t umi_sim_position_hi(void) { \
            return static_cast<uint32_t>(g_web_adapter.sample_position() >> 32); \
        } \
        UMI_WEB_EXPORT uint32_t umi_sim_sample_rate(void) { \
            return g_web_adapter.sample_rate(); \
        } \
        /* DSP Load (0-10000 = 0.00%-100.00%) */ \
        UMI_WEB_EXPORT uint32_t umi_kernel_dsp_load(void) { \
            return g_web_adapter.dsp_load(); \
        } \
        UMI_WEB_EXPORT uint32_t umi_kernel_dsp_peak(void) { \
            return g_web_adapter.dsp_peak(); \
        } \
        UMI_WEB_EXPORT void umi_kernel_reset_dsp_peak(void) { \
            umi::web::WasmHwImpl::reset_dsp_peak(); \
        } \
        /* Stats */ \
        UMI_WEB_EXPORT uint32_t umi_kernel_midi_rx(void) { \
            return g_web_adapter.midi_rx_count(); \
        } \
        UMI_WEB_EXPORT uint32_t umi_kernel_audio_buffers(void) { \
            return g_web_adapter.audio_buffer_count(); \
        } \
        UMI_WEB_EXPORT uint64_t umi_kernel_uptime_us(void) { \
            return umi::web::WasmHwImpl::monotonic_time_usecs(); \
        } \
        /* HW Simulation Parameters */ \
        UMI_WEB_EXPORT uint32_t umi_hw_cpu_freq(void) { \
            return umi::web::g_hw_sim_params.cpu_freq_mhz; \
        } \
        UMI_WEB_EXPORT void umi_hw_set_cpu_freq(uint32_t mhz) { \
            umi::web::g_hw_sim_params.cpu_freq_mhz = mhz; \
        } \
        UMI_WEB_EXPORT uint32_t umi_hw_isr_overhead(void) { \
            return umi::web::g_hw_sim_params.isr_overhead_cycles; \
        } \
        UMI_WEB_EXPORT void umi_hw_set_isr_overhead(uint32_t cycles) { \
            umi::web::g_hw_sim_params.isr_overhead_cycles = cycles; \
        } \
        UMI_WEB_EXPORT uint32_t umi_hw_base_cycles(void) { \
            return umi::web::g_hw_sim_params.base_cycles_per_sample; \
        } \
        UMI_WEB_EXPORT void umi_hw_set_base_cycles(uint32_t cycles) { \
            umi::web::g_hw_sim_params.base_cycles_per_sample = cycles; \
        } \
        UMI_WEB_EXPORT uint32_t umi_hw_voice_cycles(void) { \
            return umi::web::g_hw_sim_params.voice_cycles_per_sample; \
        } \
        UMI_WEB_EXPORT void umi_hw_set_voice_cycles(uint32_t cycles) { \
            umi::web::g_hw_sim_params.voice_cycles_per_sample = cycles; \
        } \
        UMI_WEB_EXPORT uint32_t umi_hw_event_cycles(void) { \
            return umi::web::g_hw_sim_params.event_cycles; \
        } \
        UMI_WEB_EXPORT void umi_hw_set_event_cycles(uint32_t cycles) { \
            umi::web::g_hw_sim_params.event_cycles = cycles; \
        } \
    }

#define UMI_WEB_ADAPTER_EXPORT_PARAMS(ProcessorType) \
    extern "C" { \
        UMI_WEB_EXPORT uint32_t umi_sim_param_count(void) { \
            return ProcessorType::param_count(); \
        } \
        UMI_WEB_EXPORT void umi_sim_set_param(uint32_t id, float value) { \
            g_web_adapter.processor().set_param(id, value); \
        } \
        UMI_WEB_EXPORT float umi_sim_get_param(uint32_t id) { \
            return g_web_adapter.processor().get_param(id); \
        } \
        UMI_WEB_EXPORT const char* umi_sim_param_name(uint32_t id) { \
            auto* desc = ProcessorType::get_param_descriptor(id); \
            return desc ? desc->name.data() : ""; \
        } \
        UMI_WEB_EXPORT float umi_sim_param_min(uint32_t id) { \
            auto* desc = ProcessorType::get_param_descriptor(id); \
            return desc ? desc->min_value : 0.0f; \
        } \
        UMI_WEB_EXPORT float umi_sim_param_max(uint32_t id) { \
            auto* desc = ProcessorType::get_param_descriptor(id); \
            return desc ? desc->max_value : 1.0f; \
        } \
        UMI_WEB_EXPORT float umi_sim_param_default(uint32_t id) { \
            auto* desc = ProcessorType::get_param_descriptor(id); \
            return desc ? desc->default_value : 0.0f; \
        } \
    }

#define UMI_WEB_ADAPTER_EXPORT_NAMED(ProcessorType, name, vendor, version) \
    UMI_WEB_ADAPTER_EXPORT(ProcessorType) \
    UMI_WEB_ADAPTER_EXPORT_PARAMS(ProcessorType) \
    extern "C" { \
        UMI_WEB_EXPORT const char* umi_sim_get_name(void) { return name; } \
        UMI_WEB_EXPORT const char* umi_sim_get_vendor(void) { return vendor; } \
        UMI_WEB_EXPORT const char* umi_sim_get_version(void) { return version; } \
    }

// Backward compatibility aliases
#define UMI_WEB_SIM_EXPORT(ProcessorType) UMI_WEB_ADAPTER_EXPORT(ProcessorType)
#define UMI_WEB_SIM_EXPORT_NAMED(ProcessorType, name, vendor, version) \
    UMI_WEB_ADAPTER_EXPORT_NAMED(ProcessorType, name, vendor, version)

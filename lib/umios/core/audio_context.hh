// SPDX-License-Identifier: MIT
// UMI-OS - Universal Musical Instruments Operating System
// Audio Context for process() callback

#pragma once

#include "types.hh"
#include "event.hh"
#include "error.hh"
#include "shared_state.hh"
#include <span>
#include <cstdint>
#include <algorithm>
#include <array>

namespace umi {

/// Stream configuration
struct StreamConfig {
    uint32_t sample_rate = 48000;   ///< Sample rate in Hz
    uint32_t buffer_size = 256;     ///< Buffer size in samples
};

/// Audio context passed to Processor::process()
/// Contains all information needed for sample-accurate audio processing
///
/// UMIP仕様準拠: std::spanによるバッファアクセス、入出力イベント分離
/// std::spanはARM Cortex-M4でもゼロオーバーヘッド（生ポインタと同一アセンブリ）
struct AudioContext {
    // === Buffer access (std::span for type safety) ===

    /// Input audio buffers (span of channel pointers)
    std::span<const sample_t* const> inputs;

    /// Output audio buffers (span of channel pointers)
    std::span<sample_t* const> outputs;

    // === Events (separated input/output) ===

    /// Input events (read-only span for iteration)
    std::span<const Event> input_events;

    /// Output event queue (for emitting events)
    EventQueue<>& output_events;

    // === Timing ===

    /// Current sample rate in Hz
    uint32_t sample_rate;

    /// Buffer size in samples
    uint32_t buffer_size;

    /// Delta time per sample (1.0 / sample_rate)
    /// Pre-calculated for embedded efficiency
    float dt;

    /// Absolute sample position in the stream
    /// For DAW sync and LFO phase management
    sample_position_t sample_position;

    // === Shared state (read-only in process()) ===

    /// Parameter values (denormalized, written by EventRouter)
    const SharedParamState* params = nullptr;

    /// MIDI channel state (program, pressure, pitch bend per channel)
    const SharedChannelState* channel = nullptr;

    /// Hardware input state (raw ADC/GPIO values)
    const SharedInputState* input_state = nullptr;

    /// Number of output events written by Processor
    uint32_t output_event_count = 0;

    // === Convenience accessors ===

    /// Number of input channels
    [[nodiscard]] size_t num_inputs() const noexcept { return inputs.size(); }

    /// Number of output channels
    [[nodiscard]] size_t num_outputs() const noexcept { return outputs.size(); }

    /// Get input channel buffer (nullptr if out of range or unconnected)
    [[nodiscard]] const sample_t* input(size_t ch) const noexcept {
        return ch < inputs.size() ? inputs[ch] : nullptr;
    }

    /// Get output channel buffer (nullptr if out of range)
    [[nodiscard]] sample_t* output(size_t ch) const noexcept {
        return ch < outputs.size() ? outputs[ch] : nullptr;
    }

    /// Get input channel buffer with error handling
    [[nodiscard]] umi::Result<const sample_t*> input_checked(size_t ch) const noexcept {
        if (ch >= inputs.size()) return umi::Err(Error::InvalidParam);
        return umi::Ok(inputs[ch]);
    }

    /// Get output channel buffer with error handling
    [[nodiscard]] umi::Result<sample_t*> output_checked(size_t ch) const noexcept {
        if (ch >= outputs.size()) return umi::Err(Error::InvalidParam);
        return umi::Ok(outputs[ch]);
    }

    /// Clear all output buffers to zero
    void clear_outputs() noexcept {
        for (sample_t* out : outputs) {
            if (out) {
                for (size_t i = 0; i < buffer_size; ++i) {
                    out[i] = 0.0f;
                }
            }
        }
    }

    /// Copy input to output (up to min of channels)
    void passthrough() noexcept {
        const size_t n = std::min(inputs.size(), outputs.size());
        for (size_t ch = 0; ch < n; ++ch) {
            if (inputs[ch] && outputs[ch]) {
                for (size_t i = 0; i < buffer_size; ++i) {
                    outputs[ch][i] = inputs[ch][i];
                }
            }
        }
    }
};

/// Parameter state container for control thread
/// Thread-safe parameter storage with dirty flags
template<size_t MaxParams = 32>
struct ParamState {
    std::array<float, MaxParams> values{};
    std::array<bool, MaxParams> dirty{};

    /// Set parameter value and mark dirty
    void set(param_id_t id, float value) noexcept {
        if (id < MaxParams) {
            values[id] = value;
            dirty[id] = true;
        }
    }

    /// Get parameter value
    [[nodiscard]] float get(param_id_t id) const noexcept {
        return id < MaxParams ? values[id] : 0.0f;
    }

    /// Check if parameter changed since last clear
    [[nodiscard]] bool is_dirty(param_id_t id) const noexcept {
        return id < MaxParams && dirty[id];
    }

    /// Clear dirty flag
    void clear_dirty(param_id_t id) noexcept {
        if (id < MaxParams) dirty[id] = false;
    }

    /// Clear all dirty flags
    void clear_all_dirty() noexcept {
        dirty.fill(false);
    }
};

/// Control context for Processor::control() callback
/// Used for non-realtime operations (parameter smoothing, UI sync, etc.)
struct ControlContext {
    /// Time since last control() call in seconds
    float delta_time = 0.0f;

    /// Current sample position (reference value)
    sample_position_t sample_pos = 0;

    /// Event queue for non-time-critical events
    EventQueue<>* events = nullptr;

    /// Parameter state (optional)
    ParamState<>* params = nullptr;
};

} // namespace umi

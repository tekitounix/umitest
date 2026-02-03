// SPDX-License-Identifier: MIT
// UMI-OS Event Router
// Routes MIDI and hardware input events to audio, control, and parameter paths

#pragma once

#include "event.hh"
#include "param_mapping.hh"
#include "shared_state.hh"
#include <cstdint>
#include <cstring>

// RouteTable, RouteFlags, MidiCommandIndex are defined in route_table.hh
// (included transitively via param_mapping.hh → route_table.hh)

namespace umi {

// ============================================================================
// Raw Input — Universal event entry point
// ============================================================================

/// Source identifiers for RawInput
enum class InputSource : uint8_t {
    USB = 0,
    UART = 1,
    GPIO = 2,
};

/// Universal raw input event from any transport
/// All transports convert their data into this format before routing
struct RawInput {
    uint32_t hw_timestamp;      ///< Event receive time (μs, monotonic)
    uint8_t source_id;          ///< Transport source (InputSource)
    uint8_t size;               ///< Payload bytes used (1-6)
    uint8_t payload[6];         ///< UMP32 (4 bytes) or InputEvent data
};

static_assert(sizeof(RawInput) == 12);

// ============================================================================
// Control Event — Events delivered to Controller task
// ============================================================================

/// Control event types delivered to the Controller via ControlEventQueue
enum class ControlEventType : uint8_t {
    MIDI = 0,               ///< Raw UMP32 (ROUTE_CONTROL_RAW)
    INPUT_CHANGE = 1,       ///< Hardware input or CC converted to uint16_t
    MODE_CHANGE = 2,        ///< System mode change notification
    SYSEX_RECEIVED = 3,     ///< SysEx message available via read_sysex()
};

/// Fixed-size control event (8 bytes)
struct ControlEvent {
    ControlEventType type;
    uint8_t source_id;      ///< InputSource or CC number
    uint16_t value;         ///< Normalized value (INPUT_CHANGE) or data
    uint32_t data;          ///< UMP32 (MIDI) or extended data

    static ControlEvent make_input_change(uint8_t source, uint16_t val) noexcept {
        return {ControlEventType::INPUT_CHANGE, source, val, 0};
    }

    static ControlEvent make_midi_raw(uint32_t ump32) noexcept {
        return {ControlEventType::MIDI, 0, 0, ump32};
    }

    static ControlEvent make_sysex_notification(uint8_t source) noexcept {
        return {ControlEventType::SYSEX_RECEIVED, source, 0, 0};
    }
};

static_assert(sizeof(ControlEvent) == 8);

// ============================================================================
// Event Router — Central event distribution engine
// ============================================================================

/// SPSC queue for ControlEvents (System Task → Controller)
template<size_t Capacity = 32>
class ControlEventQueue {
public:
    [[nodiscard]] bool push(const ControlEvent& ev) noexcept {
        size_t next = (write_pos + 1) % Capacity;
        if (next == read_pos) return false;
        events[write_pos] = ev;
        write_pos = next;
        return true;
    }

    [[nodiscard]] bool pop(ControlEvent& ev) noexcept {
        if (read_pos == write_pos) return false;
        ev = events[read_pos];
        read_pos = (read_pos + 1) % Capacity;
        return true;
    }

    [[nodiscard]] bool empty() const noexcept { return read_pos == write_pos; }

    void clear() noexcept {
        read_pos = 0;
        write_pos = 0;
    }

private:
    ControlEvent events[Capacity]{};
    size_t read_pos = 0;
    size_t write_pos = 0;
};

/// Event Router: receives RawInput, routes via RouteTable to queues/state
///
/// Responsibilities:
/// 1. Parse RawInput payload as MIDI status + data
/// 2. Look up RouteTable for routing decision
/// 3. Distribute to AudioEventQueue, ControlEventQueue, SharedParamState
/// 4. Update SharedChannelState for channel messages
class EventRouter {
public:
    /// Set the active route table (pointer, caller manages lifetime)
    void set_route_table(const RouteTable* table) noexcept { route_table = table; }

    /// Set shared state pointers (for ROUTE_PARAM writes and channel state)
    void set_shared_state(SharedParamState* params, SharedChannelState* channel) noexcept {
        shared_params = params;
        shared_channel = channel;
    }

    /// Set audio output queue (type-erased via push function pointer)
    template<size_t N>
    void set_audio_queue(EventQueue<N>* queue) noexcept {
        audio_push = [](void* q, const Event& ev) { return static_cast<EventQueue<N>*>(q)->push(ev); };
        audio_queue_ptr = queue;
    }

    /// Set control output queue (type-erased via push function pointer)
    template<size_t N>
    void set_control_queue(ControlEventQueue<N>* queue) noexcept {
        control_push = [](void* q, const ControlEvent& ev) { return static_cast<ControlEventQueue<N>*>(q)->push(ev); };
        control_queue_ptr = queue;
    }

    /// Set parameter mapping (for ROUTE_PARAM path)
    void set_param_mapping(const ParamMapping* mapping) noexcept { param_mapping = mapping; }

    /// Process a hardware input event (button, potentiometer, encoder)
    /// Routes via RouteFlags to audio queue (as ButtonDown/Up or Param events),
    /// control queue (as INPUT_CHANGE), and/or SharedParamState (via InputParamMapping)
    /// @param input_id Hardware input index (0-15)
    /// @param value 16-bit value: 0xFFFF=pressed, 0x0000=released for buttons; 0-65535 for analog
    /// @param is_button True if this is a button (generates ButtonDown/Up events), false for analog
    /// @param route Route flags for this input
    void receive_input(uint8_t input_id, uint16_t value, bool is_button, RouteFlags route) noexcept {
        if (route == ROUTE_NONE) return;

        // Route to audio event queue (buttons as ButtonDown/Up events)
        if ((static_cast<uint8_t>(route) & ROUTE_AUDIO) != 0 && audio_push != nullptr) {
            if (is_button) {
                if (value != 0) {
                    (void)audio_push(audio_queue_ptr, Event::button_down(0, input_id));
                } else {
                    (void)audio_push(audio_queue_ptr, Event::button_up(0, input_id));
                }
            }
        }

        // Route to control queue as INPUT_CHANGE
        if ((static_cast<uint8_t>(route) & ROUTE_CONTROL) != 0 && control_push != nullptr) {
            (void)control_push(control_queue_ptr, ControlEvent::make_input_change(input_id, value));
        }

        // Route to parameter state via InputParamMapping
        if ((static_cast<uint8_t>(route) & ROUTE_PARAM) != 0 &&
            input_param_mapping != nullptr && shared_params != nullptr) {
            apply_input_to_param(input_id, value, *input_param_mapping, *shared_params);
        }
    }

    /// Set input parameter mapping (for ROUTE_PARAM path on HW inputs)
    void set_input_param_mapping(const InputParamMapping* mapping) noexcept { input_param_mapping = mapping; }

    /// Process a raw input event through the routing table
    /// @param raw The raw input event
    /// @param buffer_start_us Timestamp of current audio buffer start (μs)
    /// @param sample_rate Current sample rate for timestamp→sample_pos conversion
    void receive(const RawInput& raw, uint64_t buffer_start_us, uint32_t sample_rate) noexcept {
        if (route_table == nullptr) return;
        if (raw.size < 1) return;

        uint8_t status = raw.payload[0];
        uint8_t data1 = raw.size > 1 ? raw.payload[1] : 0;
        uint8_t data2 = raw.size > 2 ? raw.payload[2] : 0;

        RouteFlags flags = route_table->lookup(status, data1);
        if (flags == ROUTE_NONE) return;

        // Convert hw_timestamp to sample position within buffer
        uint32_t sample_pos = timestamp_to_sample_pos(raw.hw_timestamp, buffer_start_us, sample_rate);

        // Update channel state for channel voice messages
        if (status >= 0x80 && status < 0xF0 && shared_channel != nullptr) {
            update_channel_state(status, data1, data2);
        }

        // Route to audio event queue
        if ((static_cast<uint8_t>(flags) & ROUTE_AUDIO) != 0 && audio_push != nullptr) {
            (void)audio_push(audio_queue_ptr, Event::make_midi(0, sample_pos, status, data1, data2));
        }

        // Route to control queue (converted to INPUT_CHANGE for CC)
        if ((static_cast<uint8_t>(flags) & ROUTE_CONTROL) != 0 && control_push != nullptr) {
            if ((status & 0xF0) == 0xB0) {
                // CC → INPUT_CHANGE: normalize 7-bit to 16-bit
                uint16_t normalized = static_cast<uint16_t>(data2) << 9;
                (void)control_push(control_queue_ptr, ControlEvent::make_input_change(data1, normalized));
            } else {
                // Non-CC channel voice → raw UMP32 to control
                uint32_t ump = (static_cast<uint32_t>(status) << 16) | (static_cast<uint32_t>(data1) << 8) | data2;
                (void)control_push(control_queue_ptr, ControlEvent::make_midi_raw(ump));
            }
        }

        // Route to control queue as raw UMP32
        if ((static_cast<uint8_t>(flags) & ROUTE_CONTROL_RAW) != 0 && control_push != nullptr) {
            uint32_t ump = (static_cast<uint32_t>(status) << 16) | (static_cast<uint32_t>(data1) << 8) | data2;
            (void)control_push(control_queue_ptr, ControlEvent::make_midi_raw(ump));
        }

        // Route to parameter state via ParamMapping
        if ((static_cast<uint8_t>(flags) & ROUTE_PARAM) != 0 &&
            param_mapping != nullptr && shared_params != nullptr) {
            if ((status & 0xF0) == 0xB0) {
                apply_cc_to_param(data1, data2, *param_mapping, *shared_params);
            }
        }
    }

private:
    /// Convert microsecond timestamp to sample position within current buffer
    static uint32_t timestamp_to_sample_pos(uint32_t hw_timestamp_us, uint64_t buffer_start_us,
                                            uint32_t sample_rate) noexcept {
        if (hw_timestamp_us <= static_cast<uint32_t>(buffer_start_us & 0xFFFFFFFF)) {
            return 0;
        }
        uint32_t elapsed_us = hw_timestamp_us - static_cast<uint32_t>(buffer_start_us & 0xFFFFFFFF);
        uint32_t sample_offset = (elapsed_us * sample_rate) / 1'000'000;
        // Clamp to buffer bounds (256 samples max)
        return sample_offset < 256 ? sample_offset : 255;
    }

    /// Update SharedChannelState from channel voice messages
    void update_channel_state(uint8_t status, uint8_t data1, uint8_t data2) noexcept {
        uint8_t ch = status & 0x0F;
        uint8_t cmd = status & 0xF0;
        auto& channel = shared_channel->channels[ch];

        switch (cmd) {
        case 0xC0: // Program Change
            channel.program = data1;
            break;
        case 0xD0: // Channel Aftertouch
            channel.pressure = data1;
            break;
        case 0xE0: // Pitch Bend
            channel.pitch_bend = static_cast<int16_t>(((data2 << 7) | data1) - 8192);
            break;
        default:
            break;
        }
    }

    using AudioPushFn = bool (*)(void*, const Event&);
    using ControlPushFn = bool (*)(void*, const ControlEvent&);

    const RouteTable* route_table = nullptr;
    const ParamMapping* param_mapping = nullptr;
    const InputParamMapping* input_param_mapping = nullptr;
    SharedParamState* shared_params = nullptr;
    SharedChannelState* shared_channel = nullptr;
    AudioPushFn audio_push = nullptr;
    void* audio_queue_ptr = nullptr;
    ControlPushFn control_push = nullptr;
    void* control_queue_ptr = nullptr;
};

} // namespace umi

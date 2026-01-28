// SPDX-License-Identifier: MIT
// UMI-OS - Universal Musical Instruments Operating System
// Event types and EventQueue

#pragma once

#include "types.hh"
#include <cstdint>
#include <cstring>

namespace umi {

/// Event type discriminator
enum class EventType : uint8_t {
    Midi,         ///< MIDI message
    Param,        ///< Parameter change
    Raw,          ///< Raw data
    ButtonDown,   ///< Button pressed
    ButtonUp,     ///< Button released
};

/// MIDI event data
struct MidiData {
    uint8_t bytes[3] = {0, 0, 0};
    uint8_t size = 0;
    
    // MIDI message parsing helpers
    [[nodiscard]] constexpr uint8_t status() const noexcept { return bytes[0]; }
    [[nodiscard]] constexpr uint8_t channel() const noexcept { return bytes[0] & 0x0F; }
    [[nodiscard]] constexpr uint8_t command() const noexcept { return bytes[0] & 0xF0; }
    [[nodiscard]] constexpr uint8_t note() const noexcept { return bytes[1]; }
    [[nodiscard]] constexpr uint8_t velocity() const noexcept { return bytes[2]; }
    [[nodiscard]] constexpr uint8_t cc_number() const noexcept { return bytes[1]; }
    [[nodiscard]] constexpr uint8_t cc_value() const noexcept { return bytes[2]; }
    
    // MIDI command constants
    static constexpr uint8_t NOTE_OFF = 0x80;
    static constexpr uint8_t NOTE_ON = 0x90;
    static constexpr uint8_t POLY_AFTERTOUCH = 0xA0;
    static constexpr uint8_t CONTROL_CHANGE = 0xB0;
    static constexpr uint8_t PROGRAM_CHANGE = 0xC0;
    static constexpr uint8_t CHANNEL_AFTERTOUCH = 0xD0;
    static constexpr uint8_t PITCH_BEND = 0xE0;
    
    [[nodiscard]] constexpr bool is_note_on() const noexcept {
        return command() == NOTE_ON && velocity() > 0;
    }
    [[nodiscard]] constexpr bool is_note_off() const noexcept {
        return command() == NOTE_OFF || (command() == NOTE_ON && velocity() == 0);
    }
    [[nodiscard]] constexpr bool is_cc() const noexcept {
        return command() == CONTROL_CHANGE;
    }
};

/// Parameter change data
struct ParamData {
    param_id_t id = 0;
    float value = 0.0f;
};

/// Raw event data
struct RawData {
    static constexpr size_t MAX_SIZE = 8;
    uint8_t data[MAX_SIZE] = {};
    uint8_t size = 0;
};

/// Button event data
struct ButtonData {
    uint8_t button_id = 0;   ///< Button index (0-7)
    uint8_t _pad[3] = {};    ///< Padding for alignment
};

/// Sample-accurate event
struct Event {
    port_id_t port_id = 0;          ///< Port this event belongs to
    uint32_t sample_pos = 0;        ///< Sample position within buffer
    EventType type = EventType::Midi;
    
    union {
        MidiData midi;
        ParamData param;
        RawData raw;
        ButtonData button;
    };
    
    Event() noexcept : midi{} {}
    
    /// Create MIDI event
    static Event make_midi(port_id_t port, uint32_t pos, 
                           uint8_t status, uint8_t d1, uint8_t d2 = 0) noexcept {
        Event e;
        e.port_id = port;
        e.sample_pos = pos;
        e.type = EventType::Midi;
        e.midi.bytes[0] = status;
        e.midi.bytes[1] = d1;
        e.midi.bytes[2] = d2;
        e.midi.size = (status >= 0xC0 && status < 0xE0) ? 2 : 3;
        return e;
    }
    
    /// Create parameter change event
    static Event make_param(param_id_t id, uint32_t pos, float value) noexcept {
        Event e;
        e.port_id = 0;  // Param events don't use port
        e.sample_pos = pos;
        e.type = EventType::Param;
        e.param.id = id;
        e.param.value = value;
        return e;
    }
    
    /// Create note on event
    static Event note_on(port_id_t port, uint32_t pos, 
                         uint8_t channel, uint8_t note, uint8_t velocity) noexcept {
        return make_midi(port, pos, MidiData::NOTE_ON | (channel & 0x0F), note, velocity);
    }
    
    /// Create note off event
    static Event note_off(port_id_t port, uint32_t pos,
                          uint8_t channel, uint8_t note, uint8_t velocity = 0) noexcept {
        return make_midi(port, pos, MidiData::NOTE_OFF | (channel & 0x0F), note, velocity);
    }
    
    /// Create CC event
    static Event cc(port_id_t port, uint32_t pos,
                    uint8_t channel, uint8_t cc_num, uint8_t value) noexcept {
        return make_midi(port, pos, MidiData::CONTROL_CHANGE | (channel & 0x0F), cc_num, value);
    }

    /// Create button down event
    static Event button_down(uint32_t pos, uint8_t button_id) noexcept {
        Event e;
        e.port_id = 0;
        e.sample_pos = pos;
        e.type = EventType::ButtonDown;
        e.button.button_id = button_id;
        return e;
    }

    /// Create button up event
    static Event button_up(uint32_t pos, uint8_t button_id) noexcept {
        Event e;
        e.port_id = 0;
        e.sample_pos = pos;
        e.type = EventType::ButtonUp;
        e.button.button_id = button_id;
        return e;
    }
};

/// Event queue for sample-accurate event processing
/// Thread-safe for single producer, single consumer (SPSC)
template<size_t Capacity = MAX_EVENTS_PER_BUFFER>
class EventQueue {
public:
    EventQueue() noexcept = default;
    
    /// Push an event to the queue
    [[nodiscard]] bool push(const Event& e) noexcept {
        const size_t next = (write_pos_ + 1) % Capacity;
        if (next == read_pos_) {
            return false;  // Full
        }
        events_[write_pos_] = e;
        write_pos_ = next;
        return true;
    }
    
    /// Push MIDI event
    [[nodiscard]] bool push_midi(port_id_t port, uint32_t sample_pos,
                                  uint8_t status, uint8_t d1, uint8_t d2 = 0) noexcept {
        return push(Event::make_midi(port, sample_pos, status, d1, d2));
    }
    
    /// Push parameter change
    [[nodiscard]] bool push_param(param_id_t param_id, uint32_t sample_pos, float value) noexcept {
        return push(Event::make_param(param_id, sample_pos, value));
    }
    
    /// Pop next event
    [[nodiscard]] bool pop(Event& out) noexcept {
        if (read_pos_ == write_pos_) {
            return false;  // Empty
        }
        out = events_[read_pos_];
        read_pos_ = (read_pos_ + 1) % Capacity;
        return true;
    }
    
    /// Pop events until sample position (inclusive)
    [[nodiscard]] bool pop_until(uint32_t sample_pos, Event& out) noexcept {
        if (read_pos_ == write_pos_) {
            return false;
        }
        if (events_[read_pos_].sample_pos > sample_pos) {
            return false;
        }
        return pop(out);
    }
    
    /// Pop event from specific port
    /// @note O(n) operation - use sparingly, prefer pop_until() for hot paths
    [[nodiscard]] bool pop_from(port_id_t port_id, Event& out) noexcept {
        // Simple implementation: linear scan for matching port
        size_t pos = read_pos_;
        while (pos != write_pos_) {
            if (events_[pos].port_id == port_id) {
                out = events_[pos];
                // Shift remaining events (not efficient but correct)
                while (pos != read_pos_) {
                    size_t prev = (pos == 0) ? Capacity - 1 : pos - 1;
                    events_[pos] = events_[prev];
                    pos = prev;
                }
                read_pos_ = (read_pos_ + 1) % Capacity;
                return true;
            }
            pos = (pos + 1) % Capacity;
        }
        return false;
    }
    
    /// Peek at next event without removing
    [[nodiscard]] bool peek(Event& out) const noexcept {
        if (read_pos_ == write_pos_) {
            return false;
        }
        out = events_[read_pos_];
        return true;
    }
    
    /// Check if queue is empty
    [[nodiscard]] bool empty() const noexcept {
        return read_pos_ == write_pos_;
    }
    
    /// Get number of events in queue
    [[nodiscard]] size_t size() const noexcept {
        if (write_pos_ >= read_pos_) {
            return write_pos_ - read_pos_;
        }
        return Capacity - read_pos_ + write_pos_;
    }
    
    /// Clear all events
    void clear() noexcept {
        read_pos_ = 0;
        write_pos_ = 0;
    }
    
private:
    Event events_[Capacity] = {};
    size_t read_pos_ = 0;
    size_t write_pos_ = 0;
};

} // namespace umi

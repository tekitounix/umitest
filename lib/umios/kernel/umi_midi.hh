#pragma once
#include "umi_kernel.hh"
#include <cmath>

// UMI MIDI: Platform-independent MIDI message definitions
// Designed for cross-platform audio processing (Embedded, VST/AU, WebAudio)
// C++23, header-only, no allocations

namespace umi::midi {

// =====================================
// MIDI Status Bytes
// =====================================

enum class Status : std::uint8_t {
    // Channel Voice Messages (high nibble, low nibble = channel)
    NoteOff         = 0x80,
    NoteOn          = 0x90,
    PolyPressure    = 0xA0,  // Polyphonic Aftertouch
    ControlChange   = 0xB0,
    ProgramChange   = 0xC0,
    ChannelPressure = 0xD0,  // Monophonic Aftertouch
    PitchBend       = 0xE0,
    
    // System Messages
    SysEx           = 0xF0,
    TimeCode        = 0xF1,
    SongPosition    = 0xF2,
    SongSelect      = 0xF3,
    TuneRequest     = 0xF6,
    SysExEnd        = 0xF7,
    
    // System Real-Time
    TimingClock     = 0xF8,
    Start           = 0xFA,
    Continue        = 0xFB,
    Stop            = 0xFC,
    ActiveSensing   = 0xFE,
    Reset           = 0xFF,
};

// =====================================
// Common Control Change Numbers
// =====================================

namespace CC {
    constexpr std::uint8_t BankSelectMSB    = 0;
    constexpr std::uint8_t ModWheel         = 1;
    constexpr std::uint8_t Breath           = 2;
    constexpr std::uint8_t FootController   = 4;
    constexpr std::uint8_t PortamentoTime   = 5;
    constexpr std::uint8_t DataEntryMSB     = 6;
    constexpr std::uint8_t Volume           = 7;
    constexpr std::uint8_t Balance          = 8;
    constexpr std::uint8_t Pan              = 10;
    constexpr std::uint8_t Expression       = 11;
    
    constexpr std::uint8_t BankSelectLSB    = 32;
    constexpr std::uint8_t DataEntryLSB     = 38;
    
    constexpr std::uint8_t Sustain          = 64;
    constexpr std::uint8_t Portamento       = 65;
    constexpr std::uint8_t Sostenuto        = 66;
    constexpr std::uint8_t SoftPedal        = 67;
    constexpr std::uint8_t Legato           = 68;
    constexpr std::uint8_t Hold2            = 69;
    
    // Sound Controllers
    constexpr std::uint8_t SoundVariation   = 70;
    constexpr std::uint8_t Timbre           = 71;
    constexpr std::uint8_t ReleaseTime      = 72;
    constexpr std::uint8_t AttackTime       = 73;
    constexpr std::uint8_t Brightness       = 74;
    constexpr std::uint8_t DecayTime        = 75;
    constexpr std::uint8_t VibratoRate      = 76;
    constexpr std::uint8_t VibratoDepth     = 77;
    constexpr std::uint8_t VibratoDelay     = 78;
    
    // Channel Mode Messages
    constexpr std::uint8_t AllSoundOff      = 120;
    constexpr std::uint8_t ResetAllControllers = 121;
    constexpr std::uint8_t LocalControl     = 122;
    constexpr std::uint8_t AllNotesOff      = 123;
    constexpr std::uint8_t OmniOff          = 124;
    constexpr std::uint8_t OmniOn           = 125;
    constexpr std::uint8_t MonoOn           = 126;
    constexpr std::uint8_t PolyOn           = 127;
}

// =====================================
// MIDI Message (Compact, 4 bytes)
// =====================================

struct Message {
    std::uint8_t status {0};   // Status byte (includes channel for voice msgs)
    std::uint8_t data1 {0};    // First data byte
    std::uint8_t data2 {0};    // Second data byte (may be unused)
    std::uint8_t reserved {0}; // Padding / future use
    
    // =====================================
    // Factory Methods
    // =====================================
    
    static constexpr Message note_on(std::uint8_t channel, std::uint8_t note, std::uint8_t velocity) {
        return {static_cast<std::uint8_t>(0x90 | (channel & 0x0F)), note, velocity, 0};
    }
    
    static constexpr Message note_off(std::uint8_t channel, std::uint8_t note, std::uint8_t velocity = 0) {
        return {static_cast<std::uint8_t>(0x80 | (channel & 0x0F)), note, velocity, 0};
    }
    
    static constexpr Message control_change(std::uint8_t channel, std::uint8_t cc, std::uint8_t value) {
        return {static_cast<std::uint8_t>(0xB0 | (channel & 0x0F)), cc, value, 0};
    }
    
    static constexpr Message program_change(std::uint8_t channel, std::uint8_t program) {
        return {static_cast<std::uint8_t>(0xC0 | (channel & 0x0F)), program, 0, 0};
    }
    
    static constexpr Message pitch_bend(std::uint8_t channel, std::int16_t value) {
        // value: -8192 to +8191, maps to 14-bit (0-16383, center = 8192)
        std::uint16_t bent = static_cast<std::uint16_t>(value + 8192);
        return {
            static_cast<std::uint8_t>(0xE0 | (channel & 0x0F)),
            static_cast<std::uint8_t>(bent & 0x7F),          // LSB
            static_cast<std::uint8_t>((bent >> 7) & 0x7F),   // MSB
            0
        };
    }
    
    static constexpr Message channel_pressure(std::uint8_t channel, std::uint8_t pressure) {
        return {static_cast<std::uint8_t>(0xD0 | (channel & 0x0F)), pressure, 0, 0};
    }
    
    static constexpr Message poly_pressure(std::uint8_t channel, std::uint8_t note, std::uint8_t pressure) {
        return {static_cast<std::uint8_t>(0xA0 | (channel & 0x0F)), note, pressure, 0};
    }
    
    // System Real-Time
    static constexpr Message timing_clock() { return {0xF8, 0, 0, 0}; }
    static constexpr Message start() { return {0xFA, 0, 0, 0}; }
    static constexpr Message continue_() { return {0xFB, 0, 0, 0}; }
    static constexpr Message stop() { return {0xFC, 0, 0, 0}; }
    
    // =====================================
    // Accessors
    // =====================================
    
    constexpr bool is_channel_message() const { return status >= 0x80 && status < 0xF0; }
    constexpr bool is_system_message() const { return status >= 0xF0; }
    constexpr bool is_realtime() const { return status >= 0xF8; }
    
    constexpr std::uint8_t channel() const {
        return is_channel_message() ? (status & 0x0F) : 0;
    }
    
    constexpr std::uint8_t type() const {
        return is_channel_message() ? (status & 0xF0) : status;
    }
    
    constexpr bool is_note_on() const { return type() == 0x90 && data2 > 0; }
    constexpr bool is_note_off() const { return type() == 0x80 || (type() == 0x90 && data2 == 0); }
    constexpr bool is_cc() const { return type() == 0xB0; }
    constexpr bool is_program_change() const { return type() == 0xC0; }
    constexpr bool is_pitch_bend() const { return type() == 0xE0; }
    
    constexpr std::uint8_t note() const { return data1; }
    constexpr std::uint8_t velocity() const { return data2; }
    constexpr std::uint8_t cc_number() const { return data1; }
    constexpr std::uint8_t cc_value() const { return data2; }
    constexpr std::uint8_t program() const { return data1; }
    
    // Pitch bend as signed value (-8192 to +8191)
    constexpr std::int16_t pitch_bend_value() const {
        return static_cast<std::int16_t>((data2 << 7) | data1) - 8192;
    }
    
    // Pitch bend normalized (-1.0 to +1.0)
    constexpr float pitch_bend_normalized() const {
        return static_cast<float>(pitch_bend_value()) / 8192.0f;
    }
    
    // CC value normalized (0.0 to 1.0)
    constexpr float cc_normalized() const {
        return static_cast<float>(data2) / 127.0f;
    }
};

static_assert(sizeof(Message) == 4, "MidiMessage should be 4 bytes");

// =====================================
// Timestamped MIDI Event
// =====================================

// Sample-accurate event for sequencing within audio buffer
struct Event {
    Message msg;
    std::uint32_t offset {0};  // Sample offset within current buffer
    
    constexpr Event() = default;
    constexpr Event(Message m, std::uint32_t off = 0) : msg(m), offset(off) {}
};

static_assert(sizeof(Event) == 8, "MidiEvent should be 8 bytes");

// =====================================
// MIDI Event Queue (alias to kernel SpscQueue)
// =====================================

// Lock-free SPSC queue for MIDI events.
// Uses kernel's generic SpscQueue with MIDI-specific convenience methods.

template <std::size_t Capacity>
class EventQueue : public umi::SpscQueue<Event, Capacity> {
    using Base = umi::SpscQueue<Event, Capacity>;
public:
    // Convenience: push with Message + sample offset
    bool try_push(const Message& msg, std::uint32_t offset = 0) {
        return Base::try_push(Event{msg, offset});
    }
};

// =====================================
// Sample-Accurate Event Reader
// =====================================

// Helper for processing events at exact sample positions within a buffer.
// Use this in audio callback:
//
//   EventReader reader(queue, buffer_size);
//   for (std::size_t sample = 0; sample < buffer_size; ++sample) {
//       for (auto& event : reader.events_at(sample)) {
//           process_event(event);
//       }
//       output[sample] = synthesize();
//   }

template <std::size_t MaxEvents = 16>
class EventReader {
public:
    // Read events from queue for this buffer period
    // Call once at start of audio callback
    template <std::size_t QueueCapacity>
    void read_from(EventQueue<QueueCapacity>& queue) {
        count_ = 0;
        while (count_ < MaxEvents) {
            auto event = queue.try_pop();
            if (!event) break;
            events_[count_++] = *event;
        }
        current_ = 0;
    }
    
    // Get all events at specific sample offset
    // Returns span of events (possibly empty)
    std::span<const Event> events_at(std::uint32_t sample_offset) {
        // Skip events before this sample
        while (current_ < count_ && events_[current_].offset < sample_offset) {
            ++current_;
        }
        
        // Find events at exactly this sample
        std::size_t end = current_;
        while (end < count_ && events_[end].offset == sample_offset) {
            ++end;
        }
        
        std::size_t matched = end - current_;
        const Event* ptr = (matched > 0) ? &events_[current_] : nullptr;
        current_ = end;
        
        return std::span<const Event>(ptr, matched);
    }
    
    // Get all remaining unprocessed events
    std::span<const Event> remaining() const {
        return std::span<const Event>(events_.data() + current_, count_ - current_);
    }
    
    // Get all events read this period
    std::span<const Event> all() const {
        return std::span<const Event>(events_.data(), count_);
    }
    
    std::size_t size() const { return count_; }
    bool empty() const { return count_ == 0; }

private:
    std::array<Event, MaxEvents> events_ {};
    std::size_t count_ {0};
    std::size_t current_ {0};
};

// =====================================
// Legacy EventBuffer (for simple use cases)
// =====================================

// Simple static buffer for testing or non-realtime scenarios
template <std::size_t Capacity>
class EventBuffer {
public:
    constexpr EventBuffer() = default;
    
    constexpr bool push(const Event& event) {
        if (count_ >= Capacity) return false;
        events_[count_++] = event;
        return true;
    }
    
    constexpr bool push(const Message& msg, std::uint32_t offset = 0) {
        return push(Event{msg, offset});
    }
    
    constexpr void clear() { count_ = 0; }
    
    constexpr std::span<const Event> events() const {
        return std::span<const Event>(events_.data(), count_);
    }
    
    constexpr std::size_t size() const { return count_; }
    constexpr bool empty() const { return count_ == 0; }
    constexpr bool full() const { return count_ >= Capacity; }
    constexpr std::size_t capacity() const { return Capacity; }
    
    constexpr auto begin() const { return events_.begin(); }
    constexpr auto end() const { return events_.begin() + count_; }

private:
    std::array<Event, Capacity> events_ {};
    std::size_t count_ {0};
};

// =====================================
// Utilities
// =====================================

// MIDI note to frequency (A4 = 440Hz)
inline float note_to_freq(std::uint8_t note, float a4_freq = 440.0f) {
    // f = 440 * 2^((note - 69) / 12)
    return a4_freq * static_cast<float>(std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0));
}

// Velocity to linear amplitude (simple curve)
inline constexpr float velocity_to_amplitude(std::uint8_t velocity) {
    return static_cast<float>(velocity) / 127.0f;
}

// Velocity to amplitude with curve (0 = linear, 1 = square, etc.)
inline float velocity_to_amplitude_curved(std::uint8_t velocity, float curve = 0.5f) {
    float linear = static_cast<float>(velocity) / 127.0f;
    return std::pow(linear, 1.0f + curve);
}

} // namespace umi::midi

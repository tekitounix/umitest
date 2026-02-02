// SPDX-License-Identifier: MIT
// UsbMidiInput — USB MIDI transport adapter (05-midi.md)
// Satisfies umidi::MidiInput concept.
#pragma once

#include <cstdint>
#include <umidi/core/parser.hh>
#include <umidi/core/transport.hh>

namespace umi::backend::cm {

/// Raw input queue entry with hardware timestamp
struct RawInputEntry {
    uint64_t hw_timestamp;
    uint8_t source_id;
    umidi::UMP32 ump;
};

/// Forward declaration — concrete queue type provided by kernel
template <size_t N>
class RawInputQueue;

/// USB MIDI input adapter.
/// Callback-driven: on_midi_rx() is called from USB ISR.
/// Satisfies umidi::MidiInput.
template <typename TimerFn, typename QueueType>
class UsbMidiInput {
public:
    UsbMidiInput(TimerFn timer, QueueType& queue, uint8_t source_id) noexcept
        : timer_(timer), queue_(&queue), source_id_(source_id) {}

    [[nodiscard]] bool is_connected() const noexcept { return connected_; }

    void poll() noexcept {
        // USB is callback-driven, no polling needed
    }

    /// Called from USB MIDI ISR when data is received.
    /// @param cable USB MIDI cable number (ignored for now)
    /// @param data Raw MIDI bytes
    /// @param len Number of bytes
    void on_midi_rx(uint8_t /*cable*/, const uint8_t* data, uint8_t len) noexcept {
        auto ts = timer_();
        for (uint8_t i = 0; i < len; ++i) {
            umidi::UMP32 ump;
            if (parser_.parse(data[i], ump)) {
                queue_->push({ts, source_id_, ump});
            }
        }
    }

    void set_connected(bool connected) noexcept { connected_ = connected; }

private:
    umidi::Parser parser_{};
    TimerFn timer_;
    QueueType* queue_ = nullptr;
    uint8_t source_id_ = 0;
    bool connected_ = false;
};

} // namespace umi::backend::cm

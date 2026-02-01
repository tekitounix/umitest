// SPDX-License-Identifier: MIT
// UMI-USB: umidi adapter for USB MIDI packets
// DEPRECATED: Use UsbMidiClass with EventRouter integration instead.
#pragma once

#include <cstdint>
#include <umidi.hh>

namespace umiusb {

template<size_t Capacity = 256>
class UmidiUsbMidiAdapter {
public:
    using Queue = umidi::EventQueue<Capacity>;

    /// Bind this adapter to an AudioInterface instance (single instance global hook)
    template<typename AudioIf>
    void bind(AudioIf& audio) {
        active_ = this;
        audio.set_midi_callback(&UmidiUsbMidiAdapter::midi_callback);
        audio.set_sysex_callback(&UmidiUsbMidiAdapter::sysex_callback);
    }

    /// Access the internal event queue
    Queue& queue() { return queue_; }
    const Queue& queue() const { return queue_; }

    /// Optional sample position provider (for sample-accurate events)
    void set_sample_pos_provider(uint32_t (*fn)()) { sample_pos_fn_ = fn; }

    void on_midi_bytes(const uint8_t* data, uint8_t len) {
        if (!data || len == 0) return;
        for (uint8_t i = 0; i < len; ++i) {
            umidi::UMP32 ump;
            if (parser_.parse_running(data[i], ump)) {
                uint32_t pos = sample_pos_fn_ ? sample_pos_fn_() : 0;
                queue_.push({pos, ump});
            }
        }
    }

    void on_sysex_data(const uint8_t* /*data*/, uint16_t /*len*/) {
        // umidi handles SysEx via higher-level protocol; leave as a hook.
    }

private:
    static void midi_callback(uint8_t /*cable*/, const uint8_t* data, uint8_t len) {
        if (active_ != nullptr) {
            active_->on_midi_bytes(data, len);
        }
    }

    static void sysex_callback(const uint8_t* data, uint16_t len) {
        if (active_ != nullptr) {
            active_->on_sysex_data(data, len);
        }
    }

    inline static UmidiUsbMidiAdapter* active_ = nullptr;
    umidi::Parser parser_{};
    Queue queue_{};
    uint32_t (*sample_pos_fn_)() = nullptr;
};

}  // namespace umiusb

// SPDX-License-Identifier: MIT
// UMI-USB: MIDI Type Definitions
// Separated from audio_types.hh for standalone MIDI usage
#pragma once

#include <array>
#include <cstdint>

namespace umiusb {

// ============================================================================
// MIDI Processing
// ============================================================================

/// USB MIDI packet processing (shared between UAC1 and UAC2)
class MidiProcessor {
  public:
    using MidiCallback = void (*)(uint8_t cable, const uint8_t* data, uint8_t len);
    using SysExCallback = void (*)(const uint8_t* data, uint16_t len);

    MidiCallback on_midi = nullptr;
    SysExCallback on_sysex = nullptr;

    void process_packet(uint8_t header, uint8_t b0, uint8_t b1, uint8_t b2) {
        uint8_t cin = header & 0x0F;
        uint8_t cable = header >> 4;

        switch (cin) {
        case 0x04: // SysEx start/continue
            if (!in_sysex_) {
                in_sysex_ = true;
                sysex_pos_ = 0;
            }
            if (sysex_pos_ + 3 <= sysex_buf_.size()) {
                sysex_buf_[sysex_pos_++] = b0;
                sysex_buf_[sysex_pos_++] = b1;
                sysex_buf_[sysex_pos_++] = b2;
            }
            break;

        case 0x05: // SysEx ends with 1 byte
            if (sysex_pos_ < sysex_buf_.size()) {
                sysex_buf_[sysex_pos_++] = b0;
            }
            complete_sysex();
            break;

        case 0x06: // SysEx ends with 2 bytes
            if (sysex_pos_ + 2 <= sysex_buf_.size()) {
                sysex_buf_[sysex_pos_++] = b0;
                sysex_buf_[sysex_pos_++] = b1;
            }
            complete_sysex();
            break;

        case 0x07: // SysEx ends with 3 bytes
            if (sysex_pos_ + 3 <= sysex_buf_.size()) {
                sysex_buf_[sysex_pos_++] = b0;
                sysex_buf_[sysex_pos_++] = b1;
                sysex_buf_[sysex_pos_++] = b2;
            }
            complete_sysex();
            break;

        case 0x08: // Note Off
        case 0x09: // Note On
        case 0x0A: // Poly Aftertouch
        case 0x0B: // Control Change
        case 0x0E: // Pitch Bend
            if (on_midi != nullptr) {
                std::array<uint8_t, 3> msg = {b0, b1, b2};
                on_midi(cable, msg.data(), 3);
            }
            break;

        case 0x0C: // Program Change
        case 0x0D: // Channel Aftertouch
            if (on_midi != nullptr) {
                std::array<uint8_t, 2> msg = {b0, b1};
                on_midi(cable, msg.data(), 2);
            }
            break;

        default:
            break;
        }
    }

    static constexpr uint8_t status_to_cin(uint8_t status) {
        switch (status & 0xF0) {
        case 0x80: return 0x08;
        case 0x90: return 0x09;
        case 0xA0: return 0x0A;
        case 0xB0: return 0x0B;
        case 0xC0: return 0x0C;
        case 0xD0: return 0x0D;
        case 0xE0: return 0x0E;
        case 0xF0: return 0x04;
        default: return 0x0F;
        }
    }

  private:
    void complete_sysex() {
        if (on_sysex != nullptr && sysex_pos_ > 0) {
            on_sysex(sysex_buf_.data(), sysex_pos_);
        }
        in_sysex_ = false;
        sysex_pos_ = 0;
    }

    std::array<uint8_t, 256> sysex_buf_{};
    uint16_t sysex_pos_ = 0;
    bool in_sysex_ = false;
};

// ============================================================================
// MIDI Port Configuration
// ============================================================================

/// MIDI port configuration (for IN or OUT)
template <uint8_t Cables_, uint8_t Endpoint_, uint16_t PacketSize_ = 64>
struct MidiPort {
    static constexpr bool ENABLED = true;
    static constexpr uint8_t CABLES = Cables_;
    static constexpr uint8_t ENDPOINT = Endpoint_;
    static constexpr uint16_t PACKET_SIZE = PacketSize_;
};

/// Disabled MIDI port
struct NoMidiPort {
    static constexpr bool ENABLED = false;
    static constexpr uint8_t CABLES = 0;
    static constexpr uint8_t ENDPOINT = 0;
    static constexpr uint16_t PACKET_SIZE = 0;
};

}  // namespace umiusb

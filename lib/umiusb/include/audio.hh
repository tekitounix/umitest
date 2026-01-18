// SPDX-License-Identifier: MIT
// UMI-USB: Audio Class Implementation (UAC1 with MIDI Streaming)
#pragma once

#include <cstdint>
#include <span>
#include <array>
#include <functional>
#include "types.hh"

namespace umiusb {

// ============================================================================
// Audio Class Constants (UAC 1.0)
// ============================================================================

namespace audio {

// Audio interface subclasses
inline constexpr uint8_t SUBCLASS_AUDIOCONTROL = 0x01;
inline constexpr uint8_t SUBCLASS_AUDIOSTREAMING = 0x02;
inline constexpr uint8_t SUBCLASS_MIDISTREAMING = 0x03;

// Audio Control descriptor subtypes
namespace ac {
    inline constexpr uint8_t HEADER = 0x01;
    inline constexpr uint8_t INPUT_TERMINAL = 0x02;
    inline constexpr uint8_t OUTPUT_TERMINAL = 0x03;
    inline constexpr uint8_t MIXER_UNIT = 0x04;
    inline constexpr uint8_t SELECTOR_UNIT = 0x05;
    inline constexpr uint8_t FEATURE_UNIT = 0x06;
    inline constexpr uint8_t PROCESSING_UNIT = 0x07;
    inline constexpr uint8_t EXTENSION_UNIT = 0x08;
}

// MIDI Streaming descriptor subtypes
namespace ms {
    inline constexpr uint8_t HEADER = 0x01;
    inline constexpr uint8_t MIDI_IN_JACK = 0x02;
    inline constexpr uint8_t MIDI_OUT_JACK = 0x03;
    inline constexpr uint8_t ELEMENT = 0x04;
    inline constexpr uint8_t GENERAL = 0x01;
}

// Jack types
inline constexpr uint8_t JACK_EMBEDDED = 0x01;
inline constexpr uint8_t JACK_EXTERNAL = 0x02;

}  // namespace audio

// ============================================================================
// MIDI Streaming Configuration
// ============================================================================

/// Configuration for a MIDI port (IN/OUT pair)
struct MidiPortConfig {
    uint8_t endpoint;           // Endpoint number (1-15)
    uint16_t max_packet = 64;   // Max packet size
};

// ============================================================================
// Audio Class - MIDI Only (UAC1)
// ============================================================================

/// USB Audio Class with MIDI Streaming
/// Generates configuration descriptor at compile time
template<uint8_t MidiEp = 1, uint16_t MidiPacketSize = 64>
class AudioMidi {
public:
    static constexpr uint8_t EP_MIDI_OUT = MidiEp;
    static constexpr uint8_t EP_MIDI_IN = MidiEp;
    static constexpr uint16_t MIDI_PACKET_SIZE = MidiPacketSize;

    // MIDI callbacks
    using MidiCallback = std::function<void(uint8_t cable, const uint8_t* data, uint8_t len)>;
    using SysExCallback = std::function<void(const uint8_t* data, uint16_t len)>;

    MidiCallback on_midi;
    SysExCallback on_sysex;

private:
    // ========================================================================
    // Compile-time Configuration Descriptor Generation
    // ========================================================================

    // Descriptor sizes
    static constexpr std::size_t CONFIG_SIZE = 9;
    static constexpr std::size_t IF_SIZE = 9;
    static constexpr std::size_t AC_HEADER_SIZE = 9;
    static constexpr std::size_t MS_HEADER_SIZE = 7;
    static constexpr std::size_t MIDI_IN_JACK_SIZE = 6;
    static constexpr std::size_t MIDI_OUT_JACK_SIZE = 9;
    static constexpr std::size_t AUDIO_EP_SIZE = 9;
    static constexpr std::size_t MS_EP_SIZE = 5;

    // Total: Config(9) + IF0(9) + AC_Header(9) + IF1(9) + MS_Header(7) +
    //        IN_Jack_Emb(6) + IN_Jack_Ext(6) + OUT_Jack_Emb(9) + OUT_Jack_Ext(9) +
    //        EP_OUT(9) + EP_OUT_CS(5) + EP_IN(9) + EP_IN_CS(5) = 101
    static constexpr std::size_t TOTAL_SIZE = 101;

    static constexpr auto build_descriptor() {
        std::array<uint8_t, TOTAL_SIZE> desc{};
        std::size_t p = 0;

        // Helper lambda for writing bytes
        auto w = [&desc, &p](auto... bytes) {
            ((desc[p++] = static_cast<uint8_t>(bytes)), ...);
        };

        // === Configuration Descriptor (9 bytes) ===
        w(9, bDescriptorType::Configuration);
        w(TOTAL_SIZE & 0xFF, TOTAL_SIZE >> 8);  // wTotalLength
        w(2);               // bNumInterfaces
        w(1);               // bConfigurationValue
        w(0);               // iConfiguration
        w(0xC0);            // bmAttributes (self-powered)
        w(50);              // bMaxPower (100mA)

        // === Interface 0: Audio Control ===
        w(9, bDescriptorType::Interface);
        w(0);               // bInterfaceNumber
        w(0);               // bAlternateSetting
        w(0);               // bNumEndpoints
        w(bDeviceClass::Audio);
        w(audio::SUBCLASS_AUDIOCONTROL);
        w(0);               // bInterfaceProtocol
        w(0);               // iInterface

        // CS Audio Control Interface Header (9 bytes)
        w(9, bDescriptorType::CsInterface, audio::ac::HEADER);
        w(0x00, 0x01);      // bcdADC = 1.00
        w(9, 0);            // wTotalLength (just header)
        w(1);               // bInCollection
        w(1);               // baInterfaceNr (MIDI interface)

        // === Interface 1: MIDI Streaming ===
        w(9, bDescriptorType::Interface);
        w(1);               // bInterfaceNumber
        w(0);               // bAlternateSetting
        w(2);               // bNumEndpoints
        w(bDeviceClass::Audio);
        w(audio::SUBCLASS_MIDISTREAMING);
        w(0);               // bInterfaceProtocol
        w(0);               // iInterface

        // CS MIDI Streaming Interface Header (7 bytes)
        // wTotalLength = 7 + 6 + 6 + 9 + 9 + 9 + 5 + 9 + 5 = 65
        w(7, bDescriptorType::CsInterface, audio::ms::HEADER);
        w(0x00, 0x01);      // bcdMSC = 1.00
        w(65, 0);           // wTotalLength

        // MIDI IN Jack - Embedded (receives from USB host)
        w(6, bDescriptorType::CsInterface, audio::ms::MIDI_IN_JACK);
        w(audio::JACK_EMBEDDED);
        w(1);               // bJackID
        w(0);               // iJack

        // MIDI IN Jack - External (physical MIDI input)
        w(6, bDescriptorType::CsInterface, audio::ms::MIDI_IN_JACK);
        w(audio::JACK_EXTERNAL);
        w(2);               // bJackID
        w(0);               // iJack

        // MIDI OUT Jack - Embedded (sends to USB host)
        w(9, bDescriptorType::CsInterface, audio::ms::MIDI_OUT_JACK);
        w(audio::JACK_EMBEDDED);
        w(3);               // bJackID
        w(1);               // bNrInputPins
        w(2);               // baSourceID (External IN Jack)
        w(1);               // baSourcePin
        w(0);               // iJack

        // MIDI OUT Jack - External (physical MIDI output)
        w(9, bDescriptorType::CsInterface, audio::ms::MIDI_OUT_JACK);
        w(audio::JACK_EXTERNAL);
        w(4);               // bJackID
        w(1);               // bNrInputPins
        w(1);               // baSourceID (Embedded IN Jack)
        w(1);               // baSourcePin
        w(0);               // iJack

        // === Bulk OUT Endpoint (Host -> Device) ===
        w(9, bDescriptorType::Endpoint);
        w(EP_MIDI_OUT);     // bEndpointAddress (OUT)
        w(static_cast<uint8_t>(TransferType::Bulk));
        w(MidiPacketSize & 0xFF, MidiPacketSize >> 8);
        w(0);               // bInterval
        w(0);               // bRefresh
        w(0);               // bSynchAddress

        // CS MS Bulk OUT Endpoint
        w(5, bDescriptorType::CsEndpoint, audio::ms::GENERAL);
        w(1);               // bNumEmbMIDIJack
        w(1);               // baAssocJackID (Embedded IN)

        // === Bulk IN Endpoint (Device -> Host) ===
        w(9, bDescriptorType::Endpoint);
        w(0x80 | EP_MIDI_IN);  // bEndpointAddress (IN)
        w(static_cast<uint8_t>(TransferType::Bulk));
        w(MidiPacketSize & 0xFF, MidiPacketSize >> 8);
        w(0);               // bInterval
        w(0);               // bRefresh
        w(0);               // bSynchAddress

        // CS MS Bulk IN Endpoint
        w(5, bDescriptorType::CsEndpoint, audio::ms::GENERAL);
        w(1);               // bNumEmbMIDIJack
        w(3);               // baAssocJackID (Embedded OUT)

        return desc;
    }

    static constexpr auto config_desc_ = build_descriptor();

    // SysEx accumulation
    std::array<uint8_t, 256> sysex_buf_{};
    uint16_t sysex_pos_ = 0;
    bool in_sysex_ = false;

public:
    // ========================================================================
    // Class Interface (for Device core)
    // ========================================================================

    /// Get configuration descriptor
    [[nodiscard]] std::span<const uint8_t> config_descriptor() const {
        return std::span<const uint8_t>(config_desc_);
    }

    /// Called when device is configured/unconfigured
    void on_configured(bool /*configured*/) {
        // Endpoint configuration is done by caller with HAL
    }

    /// Configure endpoints (call after device configured)
    template<typename HalT>
    void configure_endpoints(HalT& hal) {
        hal.ep_configure({EP_MIDI_OUT, Direction::Out, TransferType::Bulk, MidiPacketSize});
        hal.ep_configure({EP_MIDI_IN, Direction::In, TransferType::Bulk, MidiPacketSize});
    }

    /// Handle class-specific request
    bool handle_request(const SetupPacket& /*setup*/, std::span<uint8_t>& /*response*/) {
        // USB MIDI typically has no class-specific control requests
        return false;
    }

    /// Handle data received on endpoint
    void on_rx(uint8_t ep, std::span<const uint8_t> data) {
        if (ep != EP_MIDI_OUT) return;

        // Process USB-MIDI Event Packets (4 bytes each)
        for (std::size_t i = 0; i + 3 < data.size(); i += 4) {
            process_packet(data[i], data[i + 1], data[i + 2], data[i + 3]);
        }
    }

    // ========================================================================
    // MIDI API
    // ========================================================================

    /// Send MIDI message (1-3 bytes)
    template<typename HalT>
    void send_midi(HalT& hal, uint8_t cable, uint8_t status,
                   uint8_t data1 = 0, uint8_t data2 = 0) {
        uint8_t cin = status_to_cin(status);
        std::array<uint8_t, 4> packet = {
            static_cast<uint8_t>((cable << 4) | cin),
            status, data1, data2
        };
        hal.ep_write(EP_MIDI_IN, packet.data(), 4);
    }

    /// Send Note On
    template<typename HalT>
    void send_note_on(HalT& hal, uint8_t ch, uint8_t note, uint8_t vel, uint8_t cable = 0) {
        send_midi(hal, cable, 0x90 | (ch & 0x0F), note & 0x7F, vel & 0x7F);
    }

    /// Send Note Off
    template<typename HalT>
    void send_note_off(HalT& hal, uint8_t ch, uint8_t note, uint8_t vel = 0, uint8_t cable = 0) {
        send_midi(hal, cable, 0x80 | (ch & 0x0F), note & 0x7F, vel & 0x7F);
    }

    /// Send Control Change
    template<typename HalT>
    void send_cc(HalT& hal, uint8_t ch, uint8_t cc, uint8_t val, uint8_t cable = 0) {
        send_midi(hal, cable, 0xB0 | (ch & 0x0F), cc & 0x7F, val & 0x7F);
    }

    /// Send SysEx message
    template<typename HalT>
    void send_sysex(HalT& hal, std::span<const uint8_t> data, uint8_t cable = 0) {
        std::array<uint8_t, 64> packet{};
        std::size_t pkt_pos = 0;
        std::size_t pos = 0;

        while (pos < data.size()) {
            std::size_t rem = data.size() - pos;
            uint8_t cin;
            uint8_t b0 = 0, b1 = 0, b2 = 0;

            if (rem >= 3) {
                cin = 0x04;  // SysEx start/continue
                b0 = data[pos++];
                b1 = data[pos++];
                b2 = data[pos++];
            } else if (rem == 2) {
                cin = 0x06;  // SysEx ends with 2 bytes
                b0 = data[pos++];
                b1 = data[pos++];
            } else {
                cin = 0x05;  // SysEx ends with 1 byte
                b0 = data[pos++];
            }

            packet[pkt_pos++] = static_cast<uint8_t>((cable << 4) | cin);
            packet[pkt_pos++] = b0;
            packet[pkt_pos++] = b1;
            packet[pkt_pos++] = b2;

            if (pkt_pos >= 60 || pos >= data.size()) {
                hal.ep_write(EP_MIDI_IN, packet.data(), static_cast<uint16_t>(pkt_pos));
                pkt_pos = 0;
            }
        }
    }

private:
    void process_packet(uint8_t header, uint8_t b0, uint8_t b1, uint8_t b2) {
        uint8_t cin = header & 0x0F;
        uint8_t cable = header >> 4;

        switch (cin) {
            case 0x04:  // SysEx start/continue
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

            case 0x05:  // SysEx ends with 1 byte
                if (sysex_pos_ < sysex_buf_.size()) {
                    sysex_buf_[sysex_pos_++] = b0;
                }
                complete_sysex();
                break;

            case 0x06:  // SysEx ends with 2 bytes
                if (sysex_pos_ + 2 <= sysex_buf_.size()) {
                    sysex_buf_[sysex_pos_++] = b0;
                    sysex_buf_[sysex_pos_++] = b1;
                }
                complete_sysex();
                break;

            case 0x07:  // SysEx ends with 3 bytes
                if (sysex_pos_ + 3 <= sysex_buf_.size()) {
                    sysex_buf_[sysex_pos_++] = b0;
                    sysex_buf_[sysex_pos_++] = b1;
                    sysex_buf_[sysex_pos_++] = b2;
                }
                complete_sysex();
                break;

            case 0x08:  // Note Off
            case 0x09:  // Note On
            case 0x0A:  // Poly Aftertouch
            case 0x0B:  // Control Change
            case 0x0E:  // Pitch Bend
                if (on_midi) {
                    std::array<uint8_t, 3> msg = {b0, b1, b2};
                    on_midi(cable, msg.data(), 3);
                }
                break;

            case 0x0C:  // Program Change
            case 0x0D:  // Channel Aftertouch
                if (on_midi) {
                    std::array<uint8_t, 2> msg = {b0, b1};
                    on_midi(cable, msg.data(), 2);
                }
                break;

            default:
                break;
        }
    }

    void complete_sysex() {
        if (on_sysex && sysex_pos_ > 0) {
            on_sysex(sysex_buf_.data(), sysex_pos_);
        }
        in_sysex_ = false;
        sysex_pos_ = 0;
    }

    static constexpr uint8_t status_to_cin(uint8_t status) {
        switch (status & 0xF0) {
            case 0x80: return 0x08;  // Note Off
            case 0x90: return 0x09;  // Note On
            case 0xA0: return 0x0A;  // Poly Aftertouch
            case 0xB0: return 0x0B;  // Control Change
            case 0xC0: return 0x0C;  // Program Change
            case 0xD0: return 0x0D;  // Channel Aftertouch
            case 0xE0: return 0x0E;  // Pitch Bend
            case 0xF0: return 0x04;  // SysEx start
            default: return 0x0F;    // Single byte
        }
    }
};

}  // namespace umiusb

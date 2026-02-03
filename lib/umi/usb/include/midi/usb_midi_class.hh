// SPDX-License-Identifier: MIT
// UMI-USB: Standalone USB MIDI Class
// Satisfies the Class concept independently of AudioInterface
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <span>

#include "midi/midi_types.hh"
#include "core/descriptor.hh"
#include "core/types.hh"

namespace umiusb {

/// MIDI version for USB MIDI class
enum class UsbMidiVersion : uint8_t {
    MIDI_1_0 = 0,  ///< Alt Setting 0: USB MIDI 1.0 (CIN packets)
    MIDI_2_0 = 1,  ///< Alt Setting 1: USB MIDI 2.0 (UMP native)
};

/// Get UMP word count from Message Type (MT)
/// @return Number of 32-bit words in this UMP message
static constexpr uint8_t ump_word_count(uint8_t mt) {
    constexpr uint8_t table[] = {1, 1, 1, 2, 2, 4, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4};
    return (mt < 16) ? table[mt] : 1;
}

/// Standalone USB MIDI Class satisfying the Class concept.
/// Can be used alone or composed with AudioClass via CompositeClass.
///
/// @tparam MidiOut_ MidiPort config for host-to-device (OUT)
/// @tparam MidiIn_  MidiPort config for device-to-host (IN)
template<typename MidiOut_ = MidiPort<1, 1>,
         typename MidiIn_ = MidiPort<1, 1>>
class UsbMidiClass {
public:
    using MidiOut = MidiOut_;
    using MidiIn = MidiIn_;

    static constexpr bool HAS_MIDI_OUT = MidiOut::ENABLED;
    static constexpr bool HAS_MIDI_IN = MidiIn::ENABLED;
    static constexpr uint8_t EP_MIDI_OUT = MidiOut::ENDPOINT;
    static constexpr uint8_t EP_MIDI_IN = MidiIn::ENDPOINT;

    // Callbacks
    using MidiCallback = void (*)(uint8_t cable, const uint8_t* data, uint8_t len);
    using SysExCallback = void (*)(const uint8_t* data, uint16_t len);

    void set_midi_callback(MidiCallback cb) { midi_processor_.on_midi = cb; }
    void set_sysex_callback(SysExCallback cb) { midi_processor_.on_sysex = cb; }

    // --- Class concept required methods ---

    [[nodiscard]] std::span<const uint8_t> config_descriptor() const {
        return {descriptor_.data(), descriptor_size_};
    }

    [[nodiscard]] std::span<const uint8_t> bos_descriptor() const {
        return {};
    }

    bool handle_vendor_request(const SetupPacket& /*setup*/, std::span<uint8_t>& /*response*/) {
        return false;
    }

    void on_configured(bool configured) {
        configured_ = configured;
    }

    bool handle_request(const SetupPacket& setup, std::span<uint8_t>& /*response*/) {
        // MIDI class requests are minimal — only handle if addressed to our interface
        if (setup.recipient() == 1) {  // Interface recipient
            uint8_t iface = setup.wIndex & 0xFF;
            if (iface == ms_iface_) {
                // No MIDI class-specific control requests defined in MIDI 1.0
                return false;
            }
        }
        return false;
    }

    void on_rx(uint8_t ep, std::span<const uint8_t> data) {
        if constexpr (HAS_MIDI_OUT) {
            if (ep == EP_MIDI_OUT) {
                if (active_version_ == UsbMidiVersion::MIDI_2_0) {
                    process_ump_stream(data.data(), static_cast<uint16_t>(data.size()));
                } else {
                    // Parse USB-MIDI 1.0 packets (4 bytes each)
                    for (std::size_t i = 0; i + 3 < data.size(); i += 4) {
                        midi_processor_.process_packet(data[i], data[i + 1], data[i + 2], data[i + 3]);
                    }
                }
            }
        }
    }

    /// Process UMP native stream (MIDI 2.0 Alt Setting 1)
    void process_ump_stream(const uint8_t* data, uint16_t len) {
        uint16_t pos = 0;
        while (pos + 3 < len) {
            uint32_t word0 = static_cast<uint32_t>(data[pos])
                           | (static_cast<uint32_t>(data[pos + 1]) << 8)
                           | (static_cast<uint32_t>(data[pos + 2]) << 16)
                           | (static_cast<uint32_t>(data[pos + 3]) << 24);
            uint8_t mt = (word0 >> 28) & 0x0F;
            uint8_t words = ump_word_count(mt);
            uint16_t bytes_needed = static_cast<uint16_t>(words * 4);
            if (pos + bytes_needed > len) break;

            // Store UMP words in ump_rx_buf_ for callback/queue
            if (ump_rx_callback_) {
                ump_rx_callback_(data + pos, bytes_needed);
            }

            pos += bytes_needed;
        }
    }

    // Optional Class concept methods

    template<typename HalT>
    void configure_endpoints(HalT& hal) {
        if constexpr (HAS_MIDI_OUT) {
            hal.ep_configure(EndpointConfig{
                .number = EP_MIDI_OUT,
                .direction = Direction::OUT,
                .type = TransferType::BULK,
                .max_packet_size = MidiOut::PACKET_SIZE,
            });
        }
        if constexpr (HAS_MIDI_IN) {
            hal.ep_configure(EndpointConfig{
                .number = EP_MIDI_IN,
                .direction = Direction::IN,
                .type = TransferType::BULK,
                .max_packet_size = MidiIn::PACKET_SIZE,
            });
        }
    }

    template<typename HalT>
    void on_sof(HalT& /*hal*/) {}

    template<typename HalT>
    void on_tx_complete(HalT& /*hal*/, uint8_t ep) {
        if constexpr (HAS_MIDI_IN) {
            if (ep == EP_MIDI_IN) {
                midi_tx_busy_ = false;
            }
        }
    }

    // --- Set Interface (Alt Setting) ---

    template<typename HalT>
    void set_interface(HalT& /*hal*/, uint8_t interface, uint8_t alt_setting) {
        if (interface == ms_iface_) {
            alt_setting_ = alt_setting;
            active_version_ = (alt_setting == 1) ? UsbMidiVersion::MIDI_2_0 : UsbMidiVersion::MIDI_1_0;
        }
    }

    /// Current MIDI streaming alt setting (0=MIDI 1.0, 1=MIDI 2.0 future)
    [[nodiscard]] uint8_t current_alt_setting() const { return alt_setting_; }

    // --- Raw Input Queue (EventRouter integration) ---

    /// Set raw input queue for EventRouter integration.
    /// When set, received MIDI data is pushed to the queue instead of callbacks.
    void set_raw_input_queue(void* queue) { raw_input_queue_ = queue; }

    /// Set callback for UMP native data (MIDI 2.0 mode)
    using UmpRxCallback = void (*)(const uint8_t* data, uint16_t len);
    void set_ump_rx_callback(UmpRxCallback cb) { ump_rx_callback_ = cb; }

    /// Current active MIDI version (depends on Alt Setting)
    [[nodiscard]] UsbMidiVersion active_version() const { return active_version_; }

    // --- MIDI TX API ---

    /// Send a MIDI message (3 bytes) on a cable.
    /// @return true if packet was queued
    template<typename HalT>
    bool send_midi(HalT& hal, uint8_t cable, const uint8_t* data, uint8_t len) {
        if constexpr (!HAS_MIDI_IN) return false;
        if (midi_tx_busy_ || len == 0) return false;

        uint8_t cin = MidiProcessor::status_to_cin(data[0]);
        uint8_t header = static_cast<uint8_t>((cable << 4) | cin);

        tx_buf_[0] = header;
        tx_buf_[1] = (len > 0) ? data[0] : 0;
        tx_buf_[2] = (len > 1) ? data[1] : 0;
        tx_buf_[3] = (len > 2) ? data[2] : 0;

        midi_tx_busy_ = true;
        hal.ep_write(EP_MIDI_IN, tx_buf_, 4);
        return true;
    }

    // --- Interface number management (for CompositeClass) ---

    void set_interface_base(uint8_t base) { interface_base_ = base; }
    [[nodiscard]] uint8_t interface_base() const { return interface_base_; }

    /// Number of interfaces consumed by this class
    [[nodiscard]] static constexpr uint8_t interface_count() {
        return 2;  // AC interface + MS interface
    }

    // --- Descriptor builder ---

    void build_descriptor(uint8_t ac_iface, uint8_t ms_iface) {
        ms_iface_ = ms_iface;
        using namespace desc;
        std::size_t pos = 0;

        auto w = [&](uint8_t b) {
            if (pos < descriptor_.size()) descriptor_[pos++] = b;
        };

        auto w16 = [&](uint16_t v) {
            w(v & 0xFF);
            w((v >> 8) & 0xFF);
        };

        // Configuration descriptor header (placeholder, filled at end)
        std::size_t cfg_offset = pos;
        for (int i = 0; i < 9; ++i) w(0);

        // Audio Control Interface (required as parent for MIDI Streaming)
        w(9); w(dtype::Interface);
        w(ac_iface); w(0); w(0);  // iface, alt, num_ep
        w(0x01); w(0x01); w(0x00); w(0);  // Audio, AudioControl, 0, iInterface

        // AC Header (minimal)
        w(9); w(dtype::CsInterface); w(0x01);  // AC Header subtype
        w16(0x0100);  // bcdADC 1.0
        w16(9);  // wTotalLength (header only)
        w(1);  // bInCollection
        w(ms_iface);  // baInterfaceNr

        // MIDI Streaming Interface
        uint8_t num_eps = (HAS_MIDI_OUT ? 1 : 0) + (HAS_MIDI_IN ? 1 : 0);
        w(9); w(dtype::Interface);
        w(ms_iface); w(0); w(num_eps);
        w(0x01); w(0x03); w(0x00); w(0);  // Audio, MidiStreaming, 0, iInterface

        // MS Header
        constexpr uint16_t ms_total = 7
            + (HAS_MIDI_OUT ? (6 + 6) : 0)
            + (HAS_MIDI_IN ? (6 + 6) : 0)
            + (HAS_MIDI_OUT ? (9 + 5) : 0)
            + (HAS_MIDI_IN ? (9 + 5) : 0);
        w(7); w(dtype::CsInterface); w(0x01);  // MS Header
        w16(0x0100);  // bcdMSC 1.0
        w16(ms_total);

        uint8_t jack_id = 1;

        // MIDI OUT jacks (host -> device)
        uint8_t emb_in_jack = 0;
        uint8_t ext_in_jack = 0;
        if constexpr (HAS_MIDI_OUT) {
            // Embedded IN Jack
            emb_in_jack = jack_id++;
            w(6); w(dtype::CsInterface); w(0x02);  // MIDI_IN_JACK
            w(0x01);  // Embedded
            w(emb_in_jack); w(0);

            // External IN Jack
            ext_in_jack = jack_id++;
            w(6); w(dtype::CsInterface); w(0x02);
            w(0x02);  // External
            w(ext_in_jack); w(0);
        }

        // MIDI IN jacks (device -> host)
        uint8_t emb_out_jack = 0;
        uint8_t ext_out_jack = 0;
        if constexpr (HAS_MIDI_IN) {
            // Embedded OUT Jack
            emb_out_jack = jack_id++;
            w(9); w(dtype::CsInterface); w(0x03);  // MIDI_OUT_JACK
            w(0x01);  // Embedded
            w(emb_out_jack);
            w(1);  // bNrInputPins
            w(ext_in_jack ? ext_in_jack : jack_id);  // Source (External IN)
            w(1);  // Pin
            w(0);  // iJack

            // External OUT Jack
            ext_out_jack = jack_id++;
            w(9); w(dtype::CsInterface); w(0x03);
            w(0x02);  // External
            w(ext_out_jack);
            w(1);
            w(emb_in_jack ? emb_in_jack : jack_id);  // Source (Embedded IN)
            w(1); w(0);
        }

        // Endpoints
        if constexpr (HAS_MIDI_OUT) {
            // Bulk OUT
            w(9); w(dtype::Endpoint);
            w(EP_MIDI_OUT);  // OUT
            w(0x02);  // Bulk
            w16(MidiOut::PACKET_SIZE);
            w(0); w(0); w(0);  // bInterval, bRefresh, bSynchAddress

            // CS Endpoint
            w(5); w(dtype::CsEndpoint); w(0x01);  // MS_GENERAL
            w(1); w(emb_in_jack);  // bNumEmbMIDIJack, baAssocJackID
        }

        if constexpr (HAS_MIDI_IN) {
            // Bulk IN
            w(9); w(dtype::Endpoint);
            w(static_cast<uint8_t>(0x80 | EP_MIDI_IN));  // IN
            w(0x02);  // Bulk
            w16(MidiIn::PACKET_SIZE);
            w(0); w(0); w(0);

            // CS Endpoint
            w(5); w(dtype::CsEndpoint); w(0x01);
            w(1); w(emb_out_jack);
        }

        // --- Alt Setting 1: USB MIDI 2.0 (UMP native) ---
        // MIDI Streaming Interface Alt Setting 1
        w(9); w(dtype::Interface);
        w(ms_iface); w(1); w(num_eps);  // alt_setting=1
        w(0x01); w(0x03); w(0x00); w(0);  // Audio, MidiStreaming, 0, iInterface

        // MS 2.0 Header (class-specific)
        w(7); w(dtype::CsInterface); w(0x01);  // MS Header
        w16(0x0200);  // bcdMSC 2.0
        w16(7);       // wTotalLength (header only, GTB is separate)

        // Endpoints for Alt Setting 1 (same EPs, same config)
        if constexpr (HAS_MIDI_OUT) {
            w(9); w(dtype::Endpoint);
            w(EP_MIDI_OUT);
            w(0x02);  // Bulk
            w16(MidiOut::PACKET_SIZE);
            w(0); w(0); w(0);

            // MS 2.0 CS Endpoint (MS_GENERAL_2_0)
            w(4); w(dtype::CsEndpoint); w(0x02);  // MS_GENERAL_2_0
            w(1);  // bNumGrpTrmBlock
        }

        if constexpr (HAS_MIDI_IN) {
            w(9); w(dtype::Endpoint);
            w(static_cast<uint8_t>(0x80 | EP_MIDI_IN));
            w(0x02);  // Bulk
            w16(MidiIn::PACKET_SIZE);
            w(0); w(0); w(0);

            // MS 2.0 CS Endpoint
            w(4); w(dtype::CsEndpoint); w(0x02);  // MS_GENERAL_2_0
            w(1);  // bNumGrpTrmBlock
        }

        descriptor_size_ = static_cast<uint16_t>(pos);

        // Fill configuration header
        descriptor_[cfg_offset + 0] = 9;
        descriptor_[cfg_offset + 1] = dtype::Configuration;
        descriptor_[cfg_offset + 2] = static_cast<uint8_t>(pos & 0xFF);
        descriptor_[cfg_offset + 3] = static_cast<uint8_t>((pos >> 8) & 0xFF);
        descriptor_[cfg_offset + 4] = 2;  // bNumInterfaces
        descriptor_[cfg_offset + 5] = 1;  // bConfigurationValue
        descriptor_[cfg_offset + 6] = 0;  // iConfiguration
        descriptor_[cfg_offset + 7] = 0xC0;  // bmAttributes (self-powered)
        descriptor_[cfg_offset + 8] = 50;  // bMaxPower (100mA)
    }

private:
    MidiProcessor midi_processor_;
    bool configured_ = false;
    bool midi_tx_busy_ = false;
    uint8_t interface_base_ = 0;
    uint8_t ms_iface_ = 0;
    uint8_t alt_setting_ = 0;
    UsbMidiVersion active_version_ = UsbMidiVersion::MIDI_1_0;
    void* raw_input_queue_ = nullptr;
    alignas(4) uint8_t tx_buf_[4]{};

    UmpRxCallback ump_rx_callback_ = nullptr;

    // Max descriptor size estimate
    static constexpr std::size_t MAX_DESC_SIZE = 256;
    std::array<uint8_t, MAX_DESC_SIZE> descriptor_{};
    uint16_t descriptor_size_ = 0;
};

}  // namespace umiusb

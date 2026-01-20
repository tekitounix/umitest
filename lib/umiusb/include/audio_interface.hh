// SPDX-License-Identifier: MIT
// UMI-USB: Professional Audio/MIDI Interface Class
// Supports UAC1/UAC2 with any combination of Audio IN/OUT and MIDI IN/OUT
#pragma once

#include <cstdint>
#include <span>
#include <array>
#include "types.hh"
#include "audio_types.hh"

namespace umiusb {

// ============================================================================
// Port Configuration Types
// ============================================================================

/// Audio port configuration (for IN or OUT)
template<uint8_t Channels_, uint8_t BitDepth_, uint32_t SampleRate_, uint8_t Endpoint_>
struct AudioPort {
    static constexpr bool ENABLED = true;
    static constexpr uint8_t CHANNELS = Channels_;
    static constexpr uint8_t BIT_DEPTH = BitDepth_;
    static constexpr uint32_t SAMPLE_RATE = SampleRate_;
    static constexpr uint8_t ENDPOINT = Endpoint_;
    static constexpr uint16_t BYTES_PER_FRAME = Channels_ * (BitDepth_ / 8);
    static constexpr uint16_t PACKET_SIZE = ((SampleRate_ / 1000) + 1) * BYTES_PER_FRAME;
};

/// Disabled audio port
struct NoAudioPort {
    static constexpr bool ENABLED = false;
    static constexpr uint8_t CHANNELS = 0;
    static constexpr uint8_t BIT_DEPTH = 0;
    static constexpr uint32_t SAMPLE_RATE = 0;
    static constexpr uint8_t ENDPOINT = 0;
    static constexpr uint16_t BYTES_PER_FRAME = 0;
    static constexpr uint16_t PACKET_SIZE = 0;
};

/// MIDI port configuration (for IN or OUT)
template<uint8_t Cables_, uint8_t Endpoint_, uint16_t PacketSize_ = 64>
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

// Common audio port presets
using AudioStereo48k = AudioPort<2, 16, 48000, 1>;
using AudioStereo44k = AudioPort<2, 16, 44100, 1>;
using AudioStereo96k = AudioPort<2, 24, 96000, 1>;
using AudioMono48k = AudioPort<1, 16, 48000, 1>;

// ============================================================================
// USB Audio/MIDI Interface Class
// ============================================================================

/// Flexible USB Audio/MIDI Interface
/// Supports any combination of Audio OUT, Audio IN, MIDI OUT, MIDI IN
template<
    UacVersion Version = UacVersion::Uac1,
    typename AudioOut_ = AudioStereo48k,
    typename AudioIn_ = NoAudioPort,
    typename MidiOut_ = NoMidiPort,
    typename MidiIn_ = NoMidiPort,
    uint8_t FeedbackEp_ = 2
>
class AudioInterface {
public:
    // Version
    static constexpr UacVersion UAC_VERSION = Version;
    static constexpr bool IS_UAC2 = (Version == UacVersion::Uac2);

    // Port types
    using AudioOut = AudioOut_;
    using AudioIn = AudioIn_;
    using MidiOut = MidiOut_;
    using MidiIn = MidiIn_;

    // Feature flags
    static constexpr bool HAS_AUDIO_OUT = AudioOut::ENABLED;
    static constexpr bool HAS_AUDIO_IN = AudioIn::ENABLED;
    static constexpr bool HAS_AUDIO = HAS_AUDIO_OUT || HAS_AUDIO_IN;
    static constexpr bool HAS_MIDI_OUT = MidiOut::ENABLED;
    static constexpr bool HAS_MIDI_IN = MidiIn::ENABLED;
    static constexpr bool HAS_MIDI = HAS_MIDI_OUT || HAS_MIDI_IN;

    // Endpoint assignments
    static constexpr uint8_t EP_AUDIO_OUT = AudioOut::ENDPOINT;
    static constexpr uint8_t EP_AUDIO_IN = AudioIn::ENDPOINT;
    static constexpr uint8_t EP_FEEDBACK = FeedbackEp_;
    static constexpr uint8_t EP_MIDI_OUT = MidiOut::ENDPOINT;
    static constexpr uint8_t EP_MIDI_IN = MidiIn::ENDPOINT;

    // Audio OUT configuration (for backward compatibility)
    static constexpr uint32_t SAMPLE_RATE = HAS_AUDIO_OUT ? AudioOut::SAMPLE_RATE : (HAS_AUDIO_IN ? AudioIn::SAMPLE_RATE : 48000);
    static constexpr uint8_t CHANNELS = HAS_AUDIO_OUT ? AudioOut::CHANNELS : (HAS_AUDIO_IN ? AudioIn::CHANNELS : 2);
    static constexpr uint8_t BIT_DEPTH = HAS_AUDIO_OUT ? AudioOut::BIT_DEPTH : (HAS_AUDIO_IN ? AudioIn::BIT_DEPTH : 16);
    static constexpr uint16_t BYTES_PER_FRAME = CHANNELS * (BIT_DEPTH / 8);

    // UAC constants
    static constexpr uint8_t SUBCLASS_AUDIOCONTROL = 0x01;
    static constexpr uint8_t SUBCLASS_AUDIOSTREAMING = 0x02;
    static constexpr uint8_t SUBCLASS_MIDISTREAMING = 0x03;

    // Callbacks
    using StatusCallback = void(*)(bool streaming);
    using MidiCallback = void(*)(uint8_t cable, const uint8_t* data, uint8_t len);
    using SysExCallback = void(*)(const uint8_t* data, uint16_t len);

    StatusCallback on_streaming_change = nullptr;  // Audio OUT streaming state
    StatusCallback on_audio_in_change = nullptr;   // Audio IN streaming state
    void (*on_audio_rx)(void) = nullptr;           // Called on each Audio OUT packet
    void (*on_feedback_sent)(void) = nullptr;      // Called when feedback EP transmits
    void (*on_sof_app)(void) = nullptr;            // Called on USB SOF (1ms) - for app to supply Audio IN data

    void set_midi_callback(MidiCallback cb) { midi_processor_.on_midi = cb; }
    void set_sysex_callback(SysExCallback cb) { midi_processor_.on_sysex = cb; }

private:
    // ========================================================================
    // Descriptor Size Calculation
    // ========================================================================

    static constexpr std::size_t calc_descriptor_size() {
        std::size_t size = 9;  // Configuration descriptor

        if constexpr (HAS_AUDIO) {
            // Audio Control Interface (shared for IN and OUT)
            size += 9;  // Interface descriptor

            if constexpr (IS_UAC2) {
                // UAC2 AC Header
                size += 9;   // AC Header
                size += 8;   // Clock Source

                if constexpr (HAS_AUDIO_OUT) {
                    size += 17;  // Input Terminal (USB streaming -> device)
                    size += 12;  // Output Terminal (device -> speaker)
                }
                if constexpr (HAS_AUDIO_IN) {
                    size += 17;  // Input Terminal (microphone -> device)
                    size += 12;  // Output Terminal (device -> USB streaming)
                }
            } else {
                // UAC1 AC Header
                std::size_t ac_size = 9;  // Header base (8 + bInCollection)
                if constexpr (HAS_AUDIO_OUT) {
                    ac_size += 12 + 10 + 9;  // IT + FU + OT
                }
                if constexpr (HAS_AUDIO_IN) {
                    ac_size += 12 + 10 + 9;  // IT + FU + OT
                }
                // Add interface count to header
                uint8_t num_streaming = (HAS_AUDIO_OUT ? 1 : 0) + (HAS_AUDIO_IN ? 1 : 0);
                ac_size += num_streaming - 1;  // baInterfaceNr array
                size += ac_size;
            }

            // Audio Streaming Interfaces
            if constexpr (HAS_AUDIO_OUT) {
                size += 9 + 9;  // Alt 0 + Alt 1 interfaces
                if constexpr (IS_UAC2) {
                    size += 16 + 6 + 7 + 8;  // AS General + Format + EP + CS EP
                    size += 7;  // Feedback EP (async)
                } else {
                    size += 7 + 11 + 9 + 7;  // AS General + Format + EP + CS EP
                    size += 9;  // Feedback EP (async)
                }
            }

            if constexpr (HAS_AUDIO_IN) {
                size += 9 + 9;  // Alt 0 + Alt 1 interfaces
                if constexpr (IS_UAC2) {
                    size += 16 + 6 + 7 + 8;  // AS General + Format + EP + CS EP
                } else {
                    size += 7 + 11 + 9 + 7;  // AS General + Format + EP + CS EP
                }
            }
        }

        // MIDI Streaming Interface
        if constexpr (HAS_MIDI) {
            size += 9;   // Interface
            size += 7;   // MS Header

            // Jacks for MIDI OUT (host -> device)
            if constexpr (HAS_MIDI_OUT) {
                size += 6;   // IN Jack Embedded
                size += 6;   // IN Jack External
                size += 9;   // OUT Jack Embedded
                size += 9;   // OUT Jack External
            }

            // Jacks for MIDI IN (device -> host)
            if constexpr (HAS_MIDI_IN) {
                size += 6;   // IN Jack Embedded
                size += 6;   // IN Jack External
                size += 9;   // OUT Jack Embedded
                size += 9;   // OUT Jack External
            }

            // Endpoints
            if constexpr (HAS_MIDI_OUT) {
                size += 9 + 5;  // Bulk OUT EP + CS
            }
            if constexpr (HAS_MIDI_IN) {
                size += 9 + 5;  // Bulk IN EP + CS
            }
        }

        return size;
    }

    static constexpr std::size_t MAX_DESC_SIZE = calc_descriptor_size();

    // ========================================================================
    // Descriptor Builder
    // ========================================================================

    static constexpr auto build_descriptor() {
        std::array<uint8_t, MAX_DESC_SIZE> desc{};
        std::size_t p = 0;

        auto w = [&desc, &p](auto... bytes) {
            ((desc[p++] = static_cast<uint8_t>(bytes)), ...);
        };

        auto w16 = [&desc, &p](uint16_t val) {
            desc[p++] = val & 0xFF;
            desc[p++] = val >> 8;
        };

        auto w24 = [&desc, &p](uint32_t val) {
            desc[p++] = val & 0xFF;
            desc[p++] = (val >> 8) & 0xFF;
            desc[p++] = (val >> 16) & 0xFF;
        };

        auto w32 = [&desc, &p](uint32_t val) {
            desc[p++] = val & 0xFF;
            desc[p++] = (val >> 8) & 0xFF;
            desc[p++] = (val >> 16) & 0xFF;
            desc[p++] = (val >> 24) & 0xFF;
        };

        // Interface numbering
        uint8_t iface_num = 0;
        uint8_t audio_ctrl_iface = 0;
        uint8_t audio_out_iface = 0;
        uint8_t audio_in_iface = 0;
        uint8_t midi_iface = 0;

        if constexpr (HAS_AUDIO) {
            audio_ctrl_iface = iface_num++;
            if constexpr (HAS_AUDIO_OUT) audio_out_iface = iface_num++;
            if constexpr (HAS_AUDIO_IN) audio_in_iface = iface_num++;
        }
        if constexpr (HAS_MIDI) {
            midi_iface = iface_num++;
        }

        std::size_t total_size = calc_descriptor_size();

        // === Configuration Descriptor ===
        w(9, bDescriptorType::Configuration);
        w16(total_size);
        w(iface_num);  // bNumInterfaces
        w(1);          // bConfigurationValue
        w(0);          // iConfiguration
        w(0x80);       // bmAttributes (bus powered)
        w(100);        // bMaxPower (200mA)

        // ================================================================
        // Audio Control Interface
        // ================================================================
        if constexpr (HAS_AUDIO) {
            // Interface descriptor
            w(9, bDescriptorType::Interface);
            w(audio_ctrl_iface);
            w(0);  // bAlternateSetting
            w(0);  // bNumEndpoints
            w(bDeviceClass::Audio);
            w(SUBCLASS_AUDIOCONTROL);
            w(IS_UAC2 ? uac::uac2::IP_VERSION_02_00 : 0);
            w(0);  // iInterface

            if constexpr (IS_UAC2) {
                // UAC2: Calculate AC header total length
                constexpr uint16_t ac_total = 9 + 8 +
                    (HAS_AUDIO_OUT ? (17 + 12) : 0) +
                    (HAS_AUDIO_IN ? (17 + 12) : 0);

                // AC Header
                w(9, bDescriptorType::CsInterface, uac::ac::HEADER);
                w16(0x0200);  // bcdADC = 2.0
                w(uac::uac2::FUNCTION_SUBCLASS);
                w16(ac_total);
                w(0x00);  // bmControls

                // Clock Source (shared)
                w(8, bDescriptorType::CsInterface, uac::ac::CLOCK_SOURCE);
                w(1);  // bClockID
                w(uac::uac2::CLOCK_INTERNAL_FIXED);
                w(0x07);  // bmControls
                w(0);  // bAssocTerminal
                w(0);  // iClockSource

                // Audio OUT path: IT(2) -> OT(3)
                if constexpr (HAS_AUDIO_OUT) {
                    // Input Terminal (USB streaming)
                    w(17, bDescriptorType::CsInterface, uac::ac::INPUT_TERMINAL);
                    w(2);  // bTerminalID
                    w16(uac::TERMINAL_USB_STREAMING);
                    w(0);  // bAssocTerminal
                    w(1);  // bCSourceID
                    w(AudioOut::CHANNELS);
                    w32(AudioOut::CHANNELS == 2 ? 0x00000003 : 0x00000000);
                    w(0);  // iChannelNames
                    w16(0x0000);  // bmControls
                    w(0);  // iTerminal

                    // Output Terminal (speaker)
                    w(12, bDescriptorType::CsInterface, uac::ac::OUTPUT_TERMINAL);
                    w(3);  // bTerminalID
                    w16(uac::TERMINAL_SPEAKER);
                    w(0);  // bAssocTerminal
                    w(2);  // bSourceID
                    w(1);  // bCSourceID
                    w16(0x0000);
                    w(0);
                }

                // Audio IN path: IT(4) -> OT(5)
                if constexpr (HAS_AUDIO_IN) {
                    // Input Terminal (microphone)
                    w(17, bDescriptorType::CsInterface, uac::ac::INPUT_TERMINAL);
                    w(4);  // bTerminalID
                    w16(uac::TERMINAL_MICROPHONE);
                    w(0);
                    w(1);  // bCSourceID
                    w(AudioIn::CHANNELS);
                    w32(AudioIn::CHANNELS == 2 ? 0x00000003 : 0x00000000);
                    w(0);
                    w16(0x0000);
                    w(0);

                    // Output Terminal (USB streaming)
                    w(12, bDescriptorType::CsInterface, uac::ac::OUTPUT_TERMINAL);
                    w(5);  // bTerminalID
                    w16(uac::TERMINAL_USB_STREAMING);
                    w(0);
                    w(4);  // bSourceID
                    w(1);  // bCSourceID
                    w16(0x0000);
                    w(0);
                }
            } else {
                // UAC1: AC Header with feature units
                // Entity IDs:
                //   Audio OUT: IT=1, FU=2, OT=3
                //   Audio IN:  IT=4, FU=5, OT=6
                uint8_t num_streaming = (HAS_AUDIO_OUT ? 1 : 0) + (HAS_AUDIO_IN ? 1 : 0);
                std::size_t ac_total = 8 + num_streaming +
                    (HAS_AUDIO_OUT ? (12 + 10 + 9) : 0) +
                    (HAS_AUDIO_IN ? (12 + 10 + 9) : 0);

                // AC Header
                w(static_cast<uint8_t>(8 + num_streaming), bDescriptorType::CsInterface, uac::ac::HEADER);
                w16(0x0100);  // bcdADC = 1.0
                w16(ac_total);
                w(num_streaming);  // bInCollection
                if constexpr (HAS_AUDIO_OUT) w(audio_out_iface);
                if constexpr (HAS_AUDIO_IN) w(audio_in_iface);

                // Audio OUT path: IT(1) -> FU(2) -> OT(3)
                if constexpr (HAS_AUDIO_OUT) {
                    // Input Terminal
                    w(12, bDescriptorType::CsInterface, uac::ac::INPUT_TERMINAL);
                    w(1);  // bTerminalID
                    w16(uac::TERMINAL_USB_STREAMING);
                    w(0);
                    w(AudioOut::CHANNELS);
                    w16(AudioOut::CHANNELS == 2 ? 0x0003 : 0x0000);
                    w(0);
                    w(0);

                    // Feature Unit
                    w(10, bDescriptorType::CsInterface, uac::ac::FEATURE_UNIT);
                    w(2);     // bUnitID
                    w(1);     // bSourceID
                    w(1);     // bControlSize
                    w(0x03);  // bmaControls[0] Master: Mute + Volume
                    w(0x00);  // bmaControls[1]
                    w(0x00);  // bmaControls[2]
                    w(0);

                    // Output Terminal
                    w(9, bDescriptorType::CsInterface, uac::ac::OUTPUT_TERMINAL);
                    w(3);  // bTerminalID
                    w16(uac::TERMINAL_SPEAKER);
                    w(0);
                    w(2);  // bSourceID (Feature Unit)
                    w(0);
                }

                // Audio IN path: IT(4) -> FU(5) -> OT(6)
                if constexpr (HAS_AUDIO_IN) {
                    // Input Terminal (microphone)
                    w(12, bDescriptorType::CsInterface, uac::ac::INPUT_TERMINAL);
                    w(4);  // bTerminalID
                    w16(uac::TERMINAL_MICROPHONE);
                    w(0);
                    w(AudioIn::CHANNELS);
                    w16(AudioIn::CHANNELS == 2 ? 0x0003 : 0x0000);
                    w(0);
                    w(0);

                    // Feature Unit
                    w(10, bDescriptorType::CsInterface, uac::ac::FEATURE_UNIT);
                    w(5);     // bUnitID
                    w(4);     // bSourceID
                    w(1);
                    w(0x03);  // Mute + Volume
                    w(0x00);
                    w(0x00);
                    w(0);

                    // Output Terminal (USB streaming)
                    w(9, bDescriptorType::CsInterface, uac::ac::OUTPUT_TERMINAL);
                    w(6);  // bTerminalID
                    w16(uac::TERMINAL_USB_STREAMING);
                    w(0);
                    w(5);  // bSourceID (Feature Unit)
                    w(0);
                }
            }

            // ============================================================
            // Audio OUT Streaming Interface
            // ============================================================
            if constexpr (HAS_AUDIO_OUT) {
                // Alt 0 (zero bandwidth)
                w(9, bDescriptorType::Interface);
                w(audio_out_iface);
                w(0);  // bAlternateSetting
                w(0);  // bNumEndpoints
                w(bDeviceClass::Audio);
                w(SUBCLASS_AUDIOSTREAMING);
                w(IS_UAC2 ? uac::uac2::IP_VERSION_02_00 : 0);
                w(0);

                // Alt 1 (active)
                w(9, bDescriptorType::Interface);
                w(audio_out_iface);
                w(1);  // bAlternateSetting
                w(2);  // bNumEndpoints (audio + feedback)
                w(bDeviceClass::Audio);
                w(SUBCLASS_AUDIOSTREAMING);
                w(IS_UAC2 ? uac::uac2::IP_VERSION_02_00 : 0);
                w(0);

                if constexpr (IS_UAC2) {
                    // AS General
                    w(16, bDescriptorType::CsInterface, uac::as::GENERAL);
                    w(2);  // bTerminalLink (IT)
                    w(0x00);
                    w(uac::FORMAT_TYPE_I);
                    w32(0x00000001);  // PCM
                    w(AudioOut::CHANNELS);
                    w32(AudioOut::CHANNELS == 2 ? 0x00000003 : 0x00000000);
                    w(0);

                    // Format Type I
                    w(6, bDescriptorType::CsInterface, uac::as::FORMAT_TYPE);
                    w(uac::FORMAT_TYPE_I);
                    w(AudioOut::BIT_DEPTH / 8);
                    w(AudioOut::BIT_DEPTH);

                    // Audio Endpoint
                    w(7, bDescriptorType::Endpoint);
                    w(EP_AUDIO_OUT);
                    w(static_cast<uint8_t>(AudioSyncMode::Async));
                    w16(AudioOut::PACKET_SIZE);
                    w(1);
                    w(0);

                    // CS Audio Endpoint
                    w(8, bDescriptorType::CsEndpoint, uac::as::GENERAL);
                    w(0x00);
                    w(0x00);
                    w(0);
                    w16(0);

                    // Feedback Endpoint
                    w(7, bDescriptorType::Endpoint);
                    w(0x80 | EP_FEEDBACK);
                    w(0x11);  // Iso, Feedback
                    w16(4);   // 4 bytes for UAC2
                    w(4);     // bInterval
                    w(0);
                } else {
                    // UAC1 AS General
                    w(7, bDescriptorType::CsInterface, uac::as::GENERAL);
                    w(1);  // bTerminalLink (IT)
                    w(1);  // bDelay
                    w16(uac::FORMAT_PCM);

                    // Format Type I
                    w(11, bDescriptorType::CsInterface, uac::as::FORMAT_TYPE);
                    w(uac::FORMAT_TYPE_I);
                    w(AudioOut::CHANNELS);
                    w(AudioOut::BIT_DEPTH / 8);
                    w(AudioOut::BIT_DEPTH);
                    w(1);  // bSamFreqType
                    w24(AudioOut::SAMPLE_RATE);

                    // Audio Endpoint - Async mode with feedback
                    w(9, bDescriptorType::Endpoint);
                    w(EP_AUDIO_OUT);
                    w(static_cast<uint8_t>(AudioSyncMode::Async));
                    w16(AudioOut::PACKET_SIZE);
                    w(1);     // bInterval
                    w(0);     // bRefresh
                    w(0x80 | EP_FEEDBACK);  // bSynchAddress = feedback EP

                    // CS Audio Endpoint
                    w(7, bDescriptorType::CsEndpoint, uac::as::GENERAL);
                    w(0x00);
                    w(0);
                    w16(0);

                    // Feedback Endpoint (Async mode)
                    w(9, bDescriptorType::Endpoint);
                    w(0x80 | EP_FEEDBACK);  // IN endpoint
                    w(0x01);  // bmAttributes: Isochronous (TinyUSB uses 0x01, not 0x11)
                    w16(3);   // wMaxPacketSize: 3 bytes for UAC1 (10.14 format)
                    w(1);     // bInterval
                    w(4);     // bRefresh = 2^4 = 16 SOF frames (16ms)
                    w(0);     // bSynchAddress
                }
            }

            // ============================================================
            // Audio IN Streaming Interface
            // ============================================================
            if constexpr (HAS_AUDIO_IN) {
                // Alt 0 (zero bandwidth)
                w(9, bDescriptorType::Interface);
                w(audio_in_iface);
                w(0);
                w(0);
                w(bDeviceClass::Audio);
                w(SUBCLASS_AUDIOSTREAMING);
                w(IS_UAC2 ? uac::uac2::IP_VERSION_02_00 : 0);
                w(0);

                // Alt 1 (active)
                w(9, bDescriptorType::Interface);
                w(audio_in_iface);
                w(1);
                w(1);  // 1 endpoint (no feedback for IN)
                w(bDeviceClass::Audio);
                w(SUBCLASS_AUDIOSTREAMING);
                w(IS_UAC2 ? uac::uac2::IP_VERSION_02_00 : 0);
                w(0);

                if constexpr (IS_UAC2) {
                    // AS General
                    w(16, bDescriptorType::CsInterface, uac::as::GENERAL);
                    w(5);  // bTerminalLink (OT for IN)
                    w(0x00);
                    w(uac::FORMAT_TYPE_I);
                    w32(0x00000001);
                    w(AudioIn::CHANNELS);
                    w32(AudioIn::CHANNELS == 2 ? 0x00000003 : 0x00000000);
                    w(0);

                    // Format Type I
                    w(6, bDescriptorType::CsInterface, uac::as::FORMAT_TYPE);
                    w(uac::FORMAT_TYPE_I);
                    w(AudioIn::BIT_DEPTH / 8);
                    w(AudioIn::BIT_DEPTH);

                    // Audio Endpoint (IN)
                    w(7, bDescriptorType::Endpoint);
                    w(0x80 | EP_AUDIO_IN);
                    w(static_cast<uint8_t>(AudioSyncMode::Async));
                    w16(AudioIn::PACKET_SIZE);
                    w(1);
                    w(0);

                    // CS Audio Endpoint
                    w(8, bDescriptorType::CsEndpoint, uac::as::GENERAL);
                    w(0x00);
                    w(0x00);
                    w(0);
                    w16(0);
                } else {
                    // UAC1 AS General
                    w(7, bDescriptorType::CsInterface, uac::as::GENERAL);
                    w(6);  // bTerminalLink (OT)
                    w(1);
                    w16(uac::FORMAT_PCM);

                    // Format Type I
                    w(11, bDescriptorType::CsInterface, uac::as::FORMAT_TYPE);
                    w(uac::FORMAT_TYPE_I);
                    w(AudioIn::CHANNELS);
                    w(AudioIn::BIT_DEPTH / 8);
                    w(AudioIn::BIT_DEPTH);
                    w(1);
                    w24(AudioIn::SAMPLE_RATE);

                    // Audio Endpoint (IN) - Asynchronous mode (TinyUSB compatible)
                    // bmAttributes: Isochronous (01), Asynchronous (01 << 2) = 0x05
                    w(9, bDescriptorType::Endpoint);
                    w(0x80 | EP_AUDIO_IN);
                    w(0x05);  // bmAttributes: Isochronous, Asynchronous
                    w16(AudioIn::PACKET_SIZE);
                    w(1);     // bInterval
                    w(0);     // bRefresh (unused for data EP)
                    w(0);     // bSynchAddress: 0 (no sync EP for async IN)

                    // CS Audio Endpoint
                    w(7, bDescriptorType::CsEndpoint, uac::as::GENERAL);
                    w(0x01);  // bmAttributes: Sampling Frequency control
                    w(0x01);  // bLockDelayUnits: milliseconds
                    w16(1);   // wLockDelay: 1ms
                }
            }
        }

        // ================================================================
        // MIDI Streaming Interface
        // ================================================================
        if constexpr (HAS_MIDI) {
            // Calculate MS header total length
            constexpr uint16_t ms_total = 7 +
                (HAS_MIDI_OUT ? (6 + 6 + 9 + 9) : 0) +
                (HAS_MIDI_IN ? (6 + 6 + 9 + 9) : 0) +
                (HAS_MIDI_OUT ? (9 + 5) : 0) +
                (HAS_MIDI_IN ? (9 + 5) : 0);

            uint8_t num_midi_eps = (HAS_MIDI_OUT ? 1 : 0) + (HAS_MIDI_IN ? 1 : 0);

            // Interface
            w(9, bDescriptorType::Interface);
            w(midi_iface);
            w(0);
            w(num_midi_eps);
            w(bDeviceClass::Audio);
            w(SUBCLASS_MIDISTREAMING);
            w(0);
            w(0);

            // MS Header
            w(7, bDescriptorType::CsInterface, uac::ms::HEADER);
            w16(0x0100);
            w16(ms_total);

            // Jack IDs:
            //   MIDI OUT: IN_EMB=1, IN_EXT=2, OUT_EMB=3, OUT_EXT=4
            //   MIDI IN:  IN_EMB=5, IN_EXT=6, OUT_EMB=7, OUT_EXT=8
            if constexpr (HAS_MIDI_OUT) {
                // IN Jack Embedded (receives from host)
                w(6, bDescriptorType::CsInterface, uac::ms::MIDI_IN_JACK);
                w(uac::JACK_EMBEDDED);
                w(1);  // bJackID
                w(0);

                // IN Jack External
                w(6, bDescriptorType::CsInterface, uac::ms::MIDI_IN_JACK);
                w(uac::JACK_EXTERNAL);
                w(2);
                w(0);

                // OUT Jack Embedded (connected to external IN)
                w(9, bDescriptorType::CsInterface, uac::ms::MIDI_OUT_JACK);
                w(uac::JACK_EMBEDDED);
                w(3);
                w(1);  // bNrInputPins
                w(2);  // baSourceID (external IN)
                w(1);  // baSourcePin
                w(0);

                // OUT Jack External (connected to embedded IN)
                w(9, bDescriptorType::CsInterface, uac::ms::MIDI_OUT_JACK);
                w(uac::JACK_EXTERNAL);
                w(4);
                w(1);
                w(1);  // baSourceID (embedded IN)
                w(1);
                w(0);
            }

            if constexpr (HAS_MIDI_IN) {
                // IN Jack Embedded (receives from device HW)
                w(6, bDescriptorType::CsInterface, uac::ms::MIDI_IN_JACK);
                w(uac::JACK_EMBEDDED);
                w(5);
                w(0);

                // IN Jack External
                w(6, bDescriptorType::CsInterface, uac::ms::MIDI_IN_JACK);
                w(uac::JACK_EXTERNAL);
                w(6);
                w(0);

                // OUT Jack Embedded (sends to host)
                w(9, bDescriptorType::CsInterface, uac::ms::MIDI_OUT_JACK);
                w(uac::JACK_EMBEDDED);
                w(7);
                w(1);
                w(6);  // From external IN
                w(1);
                w(0);

                // OUT Jack External
                w(9, bDescriptorType::CsInterface, uac::ms::MIDI_OUT_JACK);
                w(uac::JACK_EXTERNAL);
                w(8);
                w(1);
                w(5);  // From embedded IN
                w(1);
                w(0);
            }

            // MIDI OUT Endpoint (host -> device, Bulk OUT)
            if constexpr (HAS_MIDI_OUT) {
                w(9, bDescriptorType::Endpoint);
                w(EP_MIDI_OUT);
                w(static_cast<uint8_t>(TransferType::Bulk));
                w16(MidiOut::PACKET_SIZE);
                w(0);
                w(0);
                w(0);

                // CS MS Endpoint
                w(5, bDescriptorType::CsEndpoint, uac::ms::GENERAL);
                w(1);  // bNumEmbMIDIJack
                w(1);  // baAssocJackID (embedded IN jack)
            }

            // MIDI IN Endpoint (device -> host, Bulk IN)
            if constexpr (HAS_MIDI_IN) {
                w(9, bDescriptorType::Endpoint);
                w(0x80 | EP_MIDI_IN);
                w(static_cast<uint8_t>(TransferType::Bulk));
                w16(MidiIn::PACKET_SIZE);
                w(0);
                w(0);
                w(0);

                // CS MS Endpoint
                w(5, bDescriptorType::CsEndpoint, uac::ms::GENERAL);
                w(1);
                w(7);  // baAssocJackID (embedded OUT jack)
            }
        }

        return desc;
    }

    static constexpr auto descriptor_ = build_descriptor();

    // ========================================================================
    // State
    // ========================================================================

    bool audio_out_streaming_ = false;
    bool audio_in_streaming_ = false;
    bool midi_configured_ = false;
    bool audio_in_pending_ = false;  // Ready to send next Audio IN packet
    uint32_t sof_count_ = 0;         // SOF frame counter for feedback interval

    // Feature Unit state (UAC1) - Audio OUT
    bool fu_out_mute_ = false;
    int16_t fu_out_volume_ = 0;  // 1/256 dB

    // Feature Unit state (UAC1) - Audio IN
    bool fu_in_mute_ = false;
    int16_t fu_in_volume_ = 0;

    // Pending SET request
    uint8_t pending_set_entity_ = 0;
    uint8_t pending_set_ctrl_ = 0;
    uint8_t pending_set_len_ = 0;

    // Audio processing
    AudioRingBuffer<256, HAS_AUDIO_OUT ? AudioOut::CHANNELS : 2> out_ring_buffer_;
    AudioRingBuffer<256, HAS_AUDIO_IN ? AudioIn::CHANNELS : 2> in_ring_buffer_;
    FeedbackCalculator<Version> feedback_calc_;
    PllRateController pll_controller_;
    PllRateController in_pll_controller_;  // ASRC for Audio IN
    MidiProcessor midi_processor_;

    // Interface numbers (runtime, matches descriptor)
    static constexpr uint8_t audio_out_iface_num_ = HAS_AUDIO ? 1 : 0;
    static constexpr uint8_t audio_in_iface_num_ = HAS_AUDIO ? (HAS_AUDIO_OUT ? 2 : 1) : 0;

public:
    // ========================================================================
    // Initialization
    // ========================================================================

    AudioInterface() {
        if constexpr (HAS_AUDIO_OUT) {
            feedback_calc_.reset(AudioOut::SAMPLE_RATE);
        }
        pll_controller_.reset();
        in_pll_controller_.reset();
    }

    void reset() {
        if constexpr (HAS_AUDIO_OUT) {
            out_ring_buffer_.reset();
            feedback_calc_.reset(AudioOut::SAMPLE_RATE);
        }
        if constexpr (HAS_AUDIO_IN) {
            in_ring_buffer_.reset();
        }
        pll_controller_.reset();
        in_pll_controller_.reset();
        audio_out_streaming_ = false;
        audio_in_streaming_ = false;
        midi_configured_ = false;
        sof_count_ = 0;
    }

    // ========================================================================
    // USB Class Interface
    // ========================================================================

    [[nodiscard]] std::span<const uint8_t> config_descriptor() const {
        return {descriptor_.data(), calc_descriptor_size()};
    }

    void on_configured(bool configured) {
        if (!configured) {
            audio_out_streaming_ = false;
            audio_in_streaming_ = false;
            midi_configured_ = false;
            sof_count_ = 0;
            audio_in_pending_ = false;
            if (on_streaming_change) on_streaming_change(false);
            if (on_audio_in_change) on_audio_in_change(false);
        }
    }

    template<typename HalT>
    void configure_endpoints(HalT& hal) {
        if constexpr (HAS_MIDI_OUT) {
            hal.ep_configure({EP_MIDI_OUT, Direction::Out, TransferType::Bulk, MidiOut::PACKET_SIZE});
        }
        if constexpr (HAS_MIDI_IN) {
            hal.ep_configure({EP_MIDI_IN, Direction::In, TransferType::Bulk, MidiIn::PACKET_SIZE});
        }
        midi_configured_ = HAS_MIDI;
    }

    template<typename HalT>
    void set_interface(HalT& hal, uint8_t interface, uint8_t alt_setting) {
        ++dbg_set_interface_count_;
        dbg_last_set_iface_ = interface;
        dbg_last_set_alt_ = alt_setting;

        // Audio OUT streaming interface
        if constexpr (HAS_AUDIO_OUT) {
            if (interface == audio_out_iface_num_) {
                bool was_streaming = audio_out_streaming_;

                if (alt_setting == 1) {
                    hal.ep_configure({EP_AUDIO_OUT, Direction::Out,
                                     TransferType::Isochronous, AudioOut::PACKET_SIZE});

                    // Async mode: configure feedback endpoint
                    constexpr uint16_t fb_size = IS_UAC2 ? 4 : 3;
                    hal.ep_configure({EP_FEEDBACK, Direction::In,
                                     TransferType::Isochronous, fb_size});

                    audio_out_streaming_ = true;
                    out_ring_buffer_.reset();
                    feedback_calc_.reset(AudioOut::SAMPLE_RATE);
                    // Set actual I2S rate (47,991 Hz due to PLLI2S limitations)
                    feedback_calc_.set_actual_rate(47991);
                    pll_controller_.reset();
                } else {
                    audio_out_streaming_ = false;
                }

                if (was_streaming != audio_out_streaming_ && on_streaming_change) {
                    on_streaming_change(audio_out_streaming_);
                }
                return;
            }
        }

        // Audio IN streaming interface
        if constexpr (HAS_AUDIO_IN) {
            if (interface == audio_in_iface_num_) {
                bool was_streaming = audio_in_streaming_;

                if (alt_setting == 1) {
                    hal.ep_configure({EP_AUDIO_IN, Direction::In,
                                     TransferType::Isochronous, AudioIn::PACKET_SIZE});
                    audio_in_streaming_ = true;
                    audio_in_pending_ = true;  // Ready to send first packet
                    // Audio IN: immediately enable reading to avoid buffer overrun
                    // (Unlike Audio OUT which needs prebuffer for smooth playback)
                    in_ring_buffer_.reset_and_start();
                    in_pll_controller_.reset();  // Reset ASRC controller
                } else {
                    audio_in_streaming_ = false;
                    audio_in_pending_ = false;
                }

                if (was_streaming != audio_in_streaming_ && on_audio_in_change) {
                    on_audio_in_change(audio_in_streaming_);
                }
                return;
            }
        }

        (void)hal; (void)interface; (void)alt_setting;
    }

    // ========================================================================
    // Class Request Handling
    // ========================================================================

    bool handle_request(const SetupPacket& setup, std::span<uint8_t>& response) {
        // UAC2 Clock Source requests
        if constexpr (IS_UAC2 && HAS_AUDIO) {
            if ((setup.bmRequestType & 0x1F) == 0x01) {
                uint8_t ctrl = setup.wValue >> 8;
                uint8_t entity = setup.wIndex >> 8;

                if (entity == 1) {  // Clock Source
                    if (ctrl == 0x01 && setup.bRequest == 0x01) {  // GET CUR freq
                        uint32_t rate = HAS_AUDIO_OUT ? AudioOut::SAMPLE_RATE : AudioIn::SAMPLE_RATE;
                        response[0] = rate & 0xFF;
                        response[1] = (rate >> 8) & 0xFF;
                        response[2] = (rate >> 16) & 0xFF;
                        response[3] = (rate >> 24) & 0xFF;
                        response = response.subspan(0, 4);
                        return true;
                    }
                    if (ctrl == 0x02 && setup.bRequest == 0x02) {  // GET RANGE
                        uint32_t rate = HAS_AUDIO_OUT ? AudioOut::SAMPLE_RATE : AudioIn::SAMPLE_RATE;
                        response[0] = 1; response[1] = 0;  // 1 subrange
                        response[2] = rate & 0xFF;
                        response[3] = (rate >> 8) & 0xFF;
                        response[4] = (rate >> 16) & 0xFF;
                        response[5] = (rate >> 24) & 0xFF;
                        response[6] = rate & 0xFF;
                        response[7] = (rate >> 8) & 0xFF;
                        response[8] = (rate >> 16) & 0xFF;
                        response[9] = (rate >> 24) & 0xFF;
                        response[10] = 0; response[11] = 0;
                        response[12] = 0; response[13] = 0;
                        response = response.subspan(0, 14);
                        return true;
                    }
                }
            }
        }

        // UAC1 Feature Unit requests
        if constexpr (!IS_UAC2 && HAS_AUDIO) {
            uint8_t entity = setup.wIndex >> 8;
            uint8_t ctrl = setup.wValue >> 8;

            constexpr uint8_t kSetCur = 0x01;
            constexpr uint8_t kGetCur = 0x81;
            constexpr uint8_t kGetMin = 0x82;
            constexpr uint8_t kGetMax = 0x83;
            constexpr uint8_t kGetRes = 0x84;
            bool is_get = (setup.bmRequestType & 0x80) != 0;

            // Feature Unit for Audio OUT (entity 2)
            if constexpr (HAS_AUDIO_OUT) {
                if (entity == 2) {
                    if (is_get) {
                        if (ctrl == 0x01) {  // Mute
                            if (setup.bRequest == kGetCur) {
                                response[0] = fu_out_mute_ ? 1 : 0;
                                response = response.subspan(0, 1);
                                return true;
                            }
                            if (setup.bRequest == kGetMin || setup.bRequest == kGetMax || setup.bRequest == kGetRes) {
                                response[0] = (setup.bRequest == kGetMin) ? 0 : 1;
                                response = response.subspan(0, 1);
                                return true;
                            }
                        }
                        if (ctrl == 0x02) {  // Volume
                            if (setup.bRequest == kGetCur) {
                                response[0] = fu_out_volume_ & 0xFF;
                                response[1] = (fu_out_volume_ >> 8) & 0xFF;
                                response = response.subspan(0, 2);
                                return true;
                            }
                            if (setup.bRequest == kGetMin) {
                                response[0] = 0x00; response[1] = 0x81;  // -127dB
                                response = response.subspan(0, 2);
                                return true;
                            }
                            if (setup.bRequest == kGetMax) {
                                response[0] = 0x00; response[1] = 0x00;  // 0dB
                                response = response.subspan(0, 2);
                                return true;
                            }
                            if (setup.bRequest == kGetRes) {
                                response[0] = 0x00; response[1] = 0x01;  // 1dB
                                response = response.subspan(0, 2);
                                return true;
                            }
                        }
                    } else {
                        if (setup.bRequest == kSetCur) {
                            pending_set_entity_ = 2;
                            pending_set_ctrl_ = ctrl;
                            pending_set_len_ = (ctrl == 0x01) ? 1 : 2;
                            response = response.subspan(0, 0);
                            return true;
                        }
                    }
                }
            }

            // Feature Unit for Audio IN (entity 5)
            if constexpr (HAS_AUDIO_IN) {
                if (entity == 5) {
                    if (is_get) {
                        if (ctrl == 0x01) {
                            if (setup.bRequest == kGetCur) {
                                response[0] = fu_in_mute_ ? 1 : 0;
                                response = response.subspan(0, 1);
                                return true;
                            }
                            if (setup.bRequest == kGetMin || setup.bRequest == kGetMax || setup.bRequest == kGetRes) {
                                response[0] = (setup.bRequest == kGetMin) ? 0 : 1;
                                response = response.subspan(0, 1);
                                return true;
                            }
                        }
                        if (ctrl == 0x02) {
                            if (setup.bRequest == kGetCur) {
                                response[0] = fu_in_volume_ & 0xFF;
                                response[1] = (fu_in_volume_ >> 8) & 0xFF;
                                response = response.subspan(0, 2);
                                return true;
                            }
                            if (setup.bRequest == kGetMin) {
                                response[0] = 0x00; response[1] = 0x81;
                                response = response.subspan(0, 2);
                                return true;
                            }
                            if (setup.bRequest == kGetMax) {
                                response[0] = 0x00; response[1] = 0x00;
                                response = response.subspan(0, 2);
                                return true;
                            }
                            if (setup.bRequest == kGetRes) {
                                response[0] = 0x00; response[1] = 0x01;
                                response = response.subspan(0, 2);
                                return true;
                            }
                        }
                    } else {
                        if (setup.bRequest == kSetCur) {
                            pending_set_entity_ = 5;
                            pending_set_ctrl_ = ctrl;
                            pending_set_len_ = (ctrl == 0x01) ? 1 : 2;
                            response = response.subspan(0, 0);
                            return true;
                        }
                    }
                }
            }

            // Accept other GET requests with zeros
            if (entity != 0 && is_get && setup.wLength > 0) {
                uint16_t len = setup.wLength;
                if (len > response.size()) len = static_cast<uint16_t>(response.size());
                for (uint16_t i = 0; i < len; ++i) response[i] = 0;
                response = response.subspan(0, len);
                return true;
            }

            // Accept any SET request (return empty response = ZLP)
            if (!is_get) {
                response = response.subspan(0, 0);
                return true;
            }
        }

        // Accept any unhandled class request with ZLP/zeros to avoid STALL
        bool is_get = (setup.bmRequestType & 0x80) != 0;
        if (is_get && setup.wLength > 0) {
            uint16_t len = setup.wLength;
            if (len > response.size()) len = static_cast<uint16_t>(response.size());
            for (uint16_t i = 0; i < len; ++i) response[i] = 0;
            response = response.subspan(0, len);
            return true;
        }
        response = response.subspan(0, 0);
        return true;
    }

    void on_ep0_rx(std::span<const uint8_t> data) {
        if constexpr (!IS_UAC2 && HAS_AUDIO) {
            if (pending_set_ctrl_ != 0 && data.size() >= pending_set_len_) {
                bool* mute_ptr = nullptr;
                int16_t* vol_ptr = nullptr;

                if (pending_set_entity_ == 2 && HAS_AUDIO_OUT) {
                    mute_ptr = &fu_out_mute_;
                    vol_ptr = &fu_out_volume_;
                } else if (pending_set_entity_ == 5 && HAS_AUDIO_IN) {
                    mute_ptr = &fu_in_mute_;
                    vol_ptr = &fu_in_volume_;
                }

                if (mute_ptr && vol_ptr) {
                    if (pending_set_ctrl_ == 0x01) {
                        *mute_ptr = (data[0] != 0);
                    } else if (pending_set_ctrl_ == 0x02 && data.size() >= 2) {
                        *vol_ptr = static_cast<int16_t>(data[0] | (data[1] << 8));
                    }
                }

                pending_set_entity_ = 0;
                pending_set_ctrl_ = 0;
                pending_set_len_ = 0;
            }
        }
    }

    void on_rx(uint8_t ep, std::span<const uint8_t> data) {
        if constexpr (HAS_AUDIO_OUT) {
            if (ep == EP_AUDIO_OUT && audio_out_streaming_) {
                uint16_t frame_count = data.size() / AudioOut::BYTES_PER_FRAME;
                out_ring_buffer_.write(reinterpret_cast<const int16_t*>(data.data()), frame_count);

                // TEMPORARY: Disable FIFO-based feedback for debugging
                // Use fixed feedback based on actual I2S clock rate (set in set_interface)
                // The feedback value is set by set_actual_rate(47991) and remains constant
                // This helps isolate whether the issue is in feedback calculation vs transmission

                if (on_audio_rx) on_audio_rx();
            }
        }

        if constexpr (HAS_MIDI_OUT) {
            if (ep == EP_MIDI_OUT) {
                for (std::size_t i = 0; i + 3 < data.size(); i += 4) {
                    midi_processor_.process_packet(data[i], data[i+1], data[i+2], data[i+3]);
                }
            }
        }
    }

    // ========================================================================
    // SOF and Feedback
    // ========================================================================

    template<typename HalT>
    void on_sof(HalT& hal) {
        // Async mode: Send feedback every SOF when streaming
        // cbb4b78 style: don't use feedback_pending_, just send every SOF
        // This works because the host polls at its own rate
        if constexpr (HAS_AUDIO_OUT) {
            if (audio_out_streaming_) {
                feedback_calc_.on_sof();
                auto fb = feedback_calc_.get_feedback_bytes();
                hal.ep_write(EP_FEEDBACK, fb.data(), static_cast<uint16_t>(fb.size()));
                if (on_feedback_sent) on_feedback_sent();
            }
        }

        // Audio IN: First packet sent from SOF, subsequent packets sent from XFRC
        // (TinyUSB-style: chain transfers from xfer_complete callback)
        if constexpr (HAS_AUDIO_IN) {
            ++dbg_sof_count_;
            
            // App callback for supplying Audio IN data (1ms timing)
            // Called BEFORE send_audio_in so app can write to ring buffer
            if (on_sof_app != nullptr) {
                on_sof_app();
            }
            
            if (audio_in_streaming_ && audio_in_pending_) {
                ++dbg_sof_streaming_count_;
                // Send first packet on SOF (only when pending is set by set_interface)
                audio_in_pending_ = false;
                send_audio_in(hal);  // Use ring buffer data from PDM mic
            }
        }
        (void)hal;
    }

    /// Called when TX completes on an endpoint
    template<typename HalT>
    void on_tx_complete(HalT& hal, uint8_t ep) {
        if constexpr (HAS_AUDIO_OUT) {
            // Feedback EP: No action needed on XFRC
            // (feedback is sent at bRefresh interval from on_sof, not based on XFRC)
            (void)ep;
        }
        if constexpr (HAS_AUDIO_IN) {
            if (ep == EP_AUDIO_IN && audio_in_streaming_) {
                // TinyUSB-style: immediately schedule next transfer on XFRC
                // Don't wait for SOF - send directly from ISR
                send_audio_in(hal);  // Use ring buffer data from PDM mic
            }
        }
        (void)hal;
        (void)ep;
    }

    void on_samples_consumed(uint32_t frame_count) {
        if constexpr (HAS_AUDIO_OUT) {
            feedback_calc_.add_consumed_samples(frame_count);
        }
        (void)frame_count;
    }

    /// DEBUG: Send test audio pattern for Audio IN
    // Test tone phase index (persistent across calls)
    mutable uint32_t test_phase_idx_ = 0;

    template<typename HalT>
    void send_test_audio_in(HalT& hal) {
        if constexpr (!HAS_AUDIO_IN) {
            (void)hal;
            return;
        } else {
            // Use pre-computed sine table for faster ISR execution
            // 1kHz at 48kHz = 48 samples per cycle
            // Table covers one full cycle at max amplitude
            static constexpr int16_t kSineTable[48] = {
                0, 4277, 8481, 12539, 16384, 19947, 23170, 25996,
                28377, 30273, 31650, 32487, 32767, 32487, 31650, 30273,
                28377, 25996, 23170, 19947, 16384, 12539, 8481, 4277,
                0, -4277, -8481, -12539, -16384, -19947, -23170, -25996,
                -28377, -30273, -31650, -32487, -32767, -32487, -31650, -30273,
                -28377, -25996, -23170, -19947, -16384, -12539, -8481, -4277
            };

            constexpr uint32_t kSamplesPerPacket = (AudioIn::SAMPLE_RATE / 1000) + 1;  // 49
            std::array<int16_t, kSamplesPerPacket * AudioIn::CHANNELS> test_buf{};

            for (uint32_t i = 0; i < kSamplesPerPacket; ++i) {
                int16_t sample = kSineTable[test_phase_idx_];
                test_buf[i * 2] = sample;      // Left
                test_buf[i * 2 + 1] = sample;  // Right
                test_phase_idx_ = (test_phase_idx_ + 1) % 48;
            }

            hal.ep_write(EP_AUDIO_IN, reinterpret_cast<const uint8_t*>(test_buf.data()),
                         AudioIn::PACKET_SIZE);
        }
    }

    // ========================================================================
    // Audio OUT Buffer Access
    // ========================================================================

    uint32_t read_audio(int16_t* dest, uint32_t frame_count) {
        if constexpr (!HAS_AUDIO_OUT) {
            (void)dest; (void)frame_count;
            return 0;
        } else {
            uint32_t read = out_ring_buffer_.read(dest, frame_count);
            apply_volume_out(dest, read);
            return read;
        }
    }

    uint32_t read_audio_asrc(int16_t* dest, uint32_t frame_count) {
        if constexpr (!HAS_AUDIO_OUT) {
            (void)dest; (void)frame_count;
            return 0;
        } else {
            int32_t level = out_ring_buffer_.buffer_level();
            int32_t ppm = pll_controller_.update(level);
            uint32_t rate = AudioRingBuffer<256, AudioOut::CHANNELS>::ppm_to_rate_q16(ppm);

            uint32_t read = out_ring_buffer_.read_interpolated(dest, frame_count, rate);
            // Don't report samples here - use fixed feedback instead
            // The feedback value is set to nominal rate in reset()
            apply_volume_out(dest, read);
            return read;
        }
    }

private:
    void apply_volume_out(int16_t* dest, uint32_t read) {
        if constexpr (!HAS_AUDIO_OUT) return;
        if (read == 0) return;

        if (fu_out_mute_) {
            __builtin_memset(dest, 0, static_cast<size_t>(read) * AudioOut::BYTES_PER_FRAME);
            return;
        }

        if (fu_out_volume_ < 0) {
            int32_t neg_vol = -static_cast<int32_t>(fu_out_volume_);
            int32_t db_256 = (neg_vol * 48) / 127;

            if (db_256 >= 12288) {
                __builtin_memset(dest, 0, static_cast<size_t>(read) * AudioOut::BYTES_PER_FRAME);
                return;
            }

            int32_t shift = db_256 / 1541;
            int32_t frac = db_256 % 1541;
            int32_t gain = 32768 >> shift;
            gain = gain - ((gain * frac) / 3082);
            if (gain < 1) gain = 1;

            uint32_t samples = read * AudioOut::CHANNELS;
            for (uint32_t i = 0; i < samples; ++i) {
                int32_t sample = dest[i];
                sample = (sample * gain) >> 15;
                dest[i] = static_cast<int16_t>(sample);
            }
        }
    }

public:
    // ========================================================================
    // Audio IN Buffer Access
    // ========================================================================

    uint32_t write_audio_in(const int16_t* src, uint32_t frame_count) {
        if constexpr (!HAS_AUDIO_IN) {
            (void)src; (void)frame_count;
            return 0;
        } else {
            return in_ring_buffer_.write(src, frame_count);
        }
    }

    template<typename HalT>
    void send_audio_in(HalT& hal) {
        if constexpr (!HAS_AUDIO_IN) {
            (void)hal;
            return;
        } else {
            if (!audio_in_streaming_) return;

            ++dbg_send_audio_in_count_;  // Debug: track calls

            constexpr uint32_t frames_per_packet = AudioIn::SAMPLE_RATE / 1000;
            int16_t buf[frames_per_packet * AudioIn::CHANNELS];

            // ASRC: Use PI controller to track buffer level and adjust read rate
            // For Audio IN, we invert polarity: if buffer is filling up, speed up read
            int32_t level = in_ring_buffer_.buffer_level();
            int32_t ppm = in_pll_controller_.update(level);
            // Invert: positive level error = buffer filling = need to read faster
            uint32_t rate = AudioRingBuffer<256, AudioIn::CHANNELS>::ppm_to_rate_q16(ppm);
            
            uint32_t read = in_ring_buffer_.read_interpolated(buf, frames_per_packet, rate);

            // For isochronous IN, we must send data every frame even if buffer is empty
            // Send silence if no data available
            if (read < frames_per_packet) {
                __builtin_memset(buf + (read * AudioIn::CHANNELS), 0,
                                (frames_per_packet - read) * AudioIn::BYTES_PER_FRAME);
                read = frames_per_packet;
            }

            // Apply mute/volume
            if (fu_in_mute_) {
                __builtin_memset(buf, 0, read * AudioIn::BYTES_PER_FRAME);
            } else if (fu_in_volume_ < 0) {
                // Volume attenuation (same algorithm as OUT)
                int32_t neg_vol = -static_cast<int32_t>(fu_in_volume_);
                int32_t db_256 = (neg_vol * 48) / 127;
                if (db_256 < 12288) {
                    int32_t shift = db_256 / 1541;
                    int32_t frac = db_256 % 1541;
                    int32_t gain = 32768 >> shift;
                    gain = gain - ((gain * frac) / 3082);
                    if (gain < 1) { gain = 1; }
                    for (uint32_t i = 0; i < read * AudioIn::CHANNELS; ++i) {
                        buf[i] = static_cast<int16_t>((buf[i] * gain) >> 15);
                    }
                } else {
                    __builtin_memset(buf, 0, read * AudioIn::BYTES_PER_FRAME);
                }
            }

            hal.ep_write(EP_AUDIO_IN, reinterpret_cast<uint8_t*>(buf),
                        read * AudioIn::BYTES_PER_FRAME);
        }
    }

    // ========================================================================
    // MIDI API
    // ========================================================================

    template<typename HalT>
    void send_midi(HalT& hal, uint8_t cable, uint8_t status, uint8_t data1 = 0, uint8_t data2 = 0) {
        if constexpr (!HAS_MIDI_IN) {
            (void)hal; (void)cable; (void)status; (void)data1; (void)data2;
            return;
        } else {
            uint8_t cin = MidiProcessor::status_to_cin(status);
            std::array<uint8_t, 4> packet = {
                static_cast<uint8_t>((cable << 4) | cin),
                status, data1, data2
            };
            hal.ep_write(EP_MIDI_IN, packet.data(), 4);
        }
    }

    template<typename HalT>
    void send_note_on(HalT& hal, uint8_t ch, uint8_t note, uint8_t vel, uint8_t cable = 0) {
        send_midi(hal, cable, 0x90 | (ch & 0x0F), note & 0x7F, vel & 0x7F);
    }

    template<typename HalT>
    void send_note_off(HalT& hal, uint8_t ch, uint8_t note, uint8_t vel = 0, uint8_t cable = 0) {
        send_midi(hal, cable, 0x80 | (ch & 0x0F), note & 0x7F, vel & 0x7F);
    }

    template<typename HalT>
    void send_cc(HalT& hal, uint8_t ch, uint8_t cc, uint8_t val, uint8_t cable = 0) {
        send_midi(hal, cable, 0xB0 | (ch & 0x0F), cc & 0x7F, val & 0x7F);
    }

    // ========================================================================
    // Status
    // ========================================================================

    [[nodiscard]] bool is_streaming() const { return audio_out_streaming_; }
    [[nodiscard]] bool is_audio_in_streaming() const { return audio_in_streaming_; }
    [[nodiscard]] bool is_midi_configured() const { return midi_configured_; }

    [[nodiscard]] bool is_playback_started() const {
        if constexpr (HAS_AUDIO_OUT) return out_ring_buffer_.is_playback_started();
        return false;
    }
    [[nodiscard]] uint32_t buffered_frames() const {
        if constexpr (HAS_AUDIO_OUT) return out_ring_buffer_.buffered_frames();
        return 0;
    }
    [[nodiscard]] uint32_t underrun_count() const {
        if constexpr (HAS_AUDIO_OUT) return out_ring_buffer_.underrun_count();
        return 0;
    }
    [[nodiscard]] uint32_t overrun_count() const {
        if constexpr (HAS_AUDIO_OUT) return out_ring_buffer_.overrun_count();
        return 0;
    }
    [[nodiscard]] uint32_t current_feedback() const {
        if constexpr (HAS_AUDIO_OUT) return feedback_calc_.get_feedback();
        return 0;
    }
    [[nodiscard]] float feedback_rate() const {
        if constexpr (HAS_AUDIO_OUT) return feedback_calc_.get_feedback_rate();
        return 0.0f;
    }
    [[nodiscard]] int32_t pll_adjustment_ppm() const {
        return pll_controller_.current_ppm();
    }

    [[nodiscard]] uint32_t current_asrc_rate() const {
        if constexpr (HAS_AUDIO_OUT) {
            return AudioRingBuffer<256, AudioOut::CHANNELS>::ppm_to_rate_q16(pll_controller_.current_ppm());
        }
        return 0x10000;
    }

    [[nodiscard]] bool is_muted() const { return fu_out_mute_; }
    [[nodiscard]] int16_t volume_db256() const { return fu_out_volume_; }

    [[nodiscard]] static constexpr AudioSyncMode sync_mode() { return AudioSyncMode::Async; }

    // Audio IN debug accessors
    [[nodiscard]] bool is_audio_in_pending() const { return audio_in_pending_; }
    [[nodiscard]] bool is_in_muted() const { return fu_in_mute_; }
    [[nodiscard]] int16_t in_volume_db256() const { return fu_in_volume_; }
    [[nodiscard]] uint32_t in_buffered_frames() const {
        if constexpr (HAS_AUDIO_IN) return in_ring_buffer_.buffered_frames();
        return 0;
    }
    [[nodiscard]] bool is_in_playback_started() const {
        if constexpr (HAS_AUDIO_IN) return in_ring_buffer_.is_playback_started();
        return false;
    }

    // Debug counters for Audio IN diagnostics
    mutable uint32_t dbg_send_audio_in_count_ = 0;  // send_audio_in() calls
    mutable uint32_t dbg_sof_count_ = 0;            // SOF interrupts (with HAS_AUDIO_IN)
    mutable uint32_t dbg_sof_streaming_count_ = 0;  // SOF with audio_in_streaming_ true
    mutable uint32_t dbg_in_buffered_ = 0;          // Last seen buffered frames
    mutable uint32_t dbg_set_interface_count_ = 0;  // set_interface() calls
    mutable uint8_t dbg_last_set_iface_ = 0;        // Last interface number
    mutable uint8_t dbg_last_set_alt_ = 0;          // Last alt setting
    mutable uint32_t dbg_force_streaming_count_ = 0; // Force streaming attempts

};

// ============================================================================
// Convenience Type Aliases (backward compatible)
// ============================================================================

// Audio OUT only
using AudioInterface48kAsync = AudioInterface<UacVersion::Uac1, AudioStereo48k, NoAudioPort, NoMidiPort, NoMidiPort, 2>;
using AudioInterface44kAsync = AudioInterface<UacVersion::Uac1, AudioStereo44k, NoAudioPort, NoMidiPort, NoMidiPort, 2>;
using AudioInterface48kAsyncV2 = AudioInterface<UacVersion::Uac2, AudioStereo48k, NoAudioPort, NoMidiPort, NoMidiPort, 2>;
using AudioInterface96kAsyncV2 = AudioInterface<UacVersion::Uac2, AudioStereo96k, NoAudioPort, NoMidiPort, NoMidiPort, 2>;

// Audio OUT + MIDI
using AudioMidiInterface48k = AudioInterface<UacVersion::Uac1, AudioStereo48k, NoAudioPort, MidiPort<1, 3>, MidiPort<1, 3>, 2>;
using AudioMidiInterface48kV2 = AudioInterface<UacVersion::Uac2, AudioStereo48k, NoAudioPort, MidiPort<1, 3>, MidiPort<1, 3>, 2>;

// MIDI only
using MidiInterface = AudioInterface<UacVersion::Uac1, NoAudioPort, NoAudioPort, MidiPort<1, 1>, MidiPort<1, 1>, 0>;
using MidiInterfaceV2 = AudioInterface<UacVersion::Uac2, NoAudioPort, NoAudioPort, MidiPort<1, 1>, MidiPort<1, 1>, 0>;

// Audio IN/OUT (full duplex)
// EP1=Audio OUT, EP2=Feedback, EP3=Audio IN
using AudioFullDuplex48k = AudioInterface<UacVersion::Uac1, AudioStereo48k, AudioPort<2, 16, 48000, 3>, NoMidiPort, NoMidiPort, 2>;
using AudioFullDuplex48kV2 = AudioInterface<UacVersion::Uac2, AudioStereo48k, AudioPort<2, 16, 48000, 3>, NoMidiPort, NoMidiPort, 2>;

// Audio IN/OUT + MIDI (full duplex with MIDI)
// STM32 OTG FS has EP0-3, with IN and OUT being independent:
// EP1 OUT=Audio OUT, EP1 IN=MIDI IN, EP2 IN=Feedback, EP3 OUT=MIDI OUT, EP3 IN=Audio IN
using AudioFullDuplexMidi48k = AudioInterface<UacVersion::Uac1, AudioStereo48k, AudioPort<2, 16, 48000, 3>, MidiPort<1, 3>, MidiPort<1, 1>, 2>;

// Audio IN only (microphone)
using AudioInOnly48k = AudioInterface<UacVersion::Uac1, NoAudioPort, AudioPort<2, 16, 48000, 1>, NoMidiPort, NoMidiPort, 0>;

}  // namespace umiusb

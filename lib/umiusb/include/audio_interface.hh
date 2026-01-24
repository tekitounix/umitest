// SPDX-License-Identifier: MIT
// UMI-USB: Professional Audio/MIDI Interface Class
// Supports UAC1/UAC2 with any combination of Audio IN/OUT and MIDI IN/OUT
#pragma once

#include <array>
#include <audio/rate/pi_controller.hh>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

#include "audio_types.hh"
#include "types.hh"

namespace umiusb {

// ============================================================================
// Port Configuration Types
// ============================================================================

template <uint8_t Channels_>
struct DefaultChannelConfig {
    static constexpr uint32_t value = (Channels_ == 2) ? 0x00000003 : 0x00000000;
};

/// Audio port configuration (for IN or OUT)
/// @tparam Channels_ Number of audio channels (1=mono, 2=stereo)
/// @tparam BitDepth_ Bits per sample (16 or 24)
/// @tparam SampleRate_ Nominal sample rate (used in descriptors)
/// @tparam Endpoint_ Endpoint number
/// @tparam MaxSampleRate_ Maximum sample rate for packet size calculation (default=SampleRate_)
/// @tparam Rates_ Discrete sample rates list (default = SampleRate_)
/// @tparam AltSettings_ Alternate format settings (UAC1), default uses BitDepth_ + Rates_
/// @tparam ChannelConfig_ UAC2 channel config bits
template <uint8_t Channels_,
          uint8_t BitDepth_,
          uint32_t SampleRate_,
          uint8_t Endpoint_,
          uint32_t MaxSampleRate_ = SampleRate_,
          typename Rates_ = AudioRates<SampleRate_>,
          typename AltSettings_ = DefaultAltList<BitDepth_, Rates_>,
          uint32_t ChannelConfig_ = DefaultChannelConfig<Channels_>::value>
struct AudioPort {
    static constexpr bool ENABLED = true;
    static constexpr uint8_t CHANNELS = Channels_;
    static constexpr uint8_t BIT_DEPTH = BitDepth_;
    static constexpr uint32_t SAMPLE_RATE = SampleRate_;
    static constexpr uint32_t MAX_SAMPLE_RATE = MaxSampleRate_; // For buffer size calculation
    using Rates = Rates_;
    using AltSettings = AltSettings_;
    static constexpr size_t RATES_COUNT = Rates::count;
    static constexpr auto RATES = Rates::values;
    static constexpr size_t ALT_COUNT = AltSettings::count;
    template <size_t Index>
    using Alt = typename AltSettings::template at<Index>;
    static constexpr uint32_t ALT_MAX_RATE = AltSettings::max_rate;
    static constexpr uint32_t CHANNEL_CONFIG = ChannelConfig_;
    static constexpr uint8_t ENDPOINT = Endpoint_;
    static constexpr uint16_t BYTES_PER_FRAME = Channels_ * (BitDepth_ / 8);
    // Use MaxSampleRate_ for packet size to support runtime rate changes.
    // Add +1 sample headroom so feedback can request extra frames (e.g. 96k -> 97).
    static constexpr uint16_t PACKET_SIZE =
        (static_cast<uint16_t>(((MaxSampleRate_ + 999) / 1000) + 1) * BYTES_PER_FRAME);
    // Buffer size: ~4-8ms at max sample rate, rounded up to power of 2
    // 96kHz -> 384 frames -> 1024, 48kHz -> 192 frames -> 256
    static constexpr uint32_t BUFFER_FRAMES = (MaxSampleRate_ >= 96000)   ? 1024
                                              : (MaxSampleRate_ >= 88200) ? 512
                                              : (MaxSampleRate_ >= 44100) ? 256
                                                                          : 128;

    static_assert(RATES_COUNT == 0 || MAX_SAMPLE_RATE >= Rates::max_rate,
                  "MAX_SAMPLE_RATE must be >= max rate in Rates list");
    static_assert(RATES_COUNT > 0, "AudioPort requires at least one sample rate");
    static_assert(MAX_SAMPLE_RATE >= ALT_MAX_RATE, "MAX_SAMPLE_RATE must be >= max rate in AltSettings list");
    static_assert(ALT_COUNT > 0, "AudioPort requires at least one alt setting");
};

/// Disabled audio port
struct NoAudioPort {
    static constexpr bool ENABLED = false;
    static constexpr uint8_t CHANNELS = 0;
    static constexpr uint8_t BIT_DEPTH = 0;
    static constexpr uint32_t SAMPLE_RATE = 0;
    static constexpr uint32_t MAX_SAMPLE_RATE = 0;
    using Rates = AudioRates<0>;
    using AltSettings = DefaultAltList<0, AudioRates<0>>;
    static constexpr size_t RATES_COUNT = 0;
    static constexpr auto RATES = Rates::values;
    static constexpr size_t ALT_COUNT = 0;
    template <size_t Index>
    using Alt = void;
    static constexpr uint32_t ALT_MAX_RATE = 0;
    static constexpr uint32_t CHANNEL_CONFIG = 0;
    static constexpr uint8_t ENDPOINT = 0;
    static constexpr uint16_t BYTES_PER_FRAME = 0;
    static constexpr uint16_t PACKET_SIZE = 0;
    static constexpr uint32_t BUFFER_FRAMES = 128;
};

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

// Common audio port presets
using AudioStereo48k = AudioPort<2, 16, 48000, 1>;
using AudioStereo44k = AudioPort<2, 16, 44100, 1>;
using AudioStereo96k = AudioPort<2, 16, 96000, 1>;
using AudioMono48k = AudioPort<1, 16, 48000, 1>;

// ============================================================================
// USB Audio/MIDI Interface Class
// ============================================================================

/// Flexible USB Audio/MIDI Interface
/// Supports any combination of Audio OUT, Audio IN, MIDI OUT, MIDI IN
template <UacVersion Version = UacVersion::Uac1,
          typename AudioOut_ = AudioStereo48k,
          typename AudioIn_ = NoAudioPort,
          typename MidiOut_ = NoMidiPort,
          typename MidiIn_ = NoMidiPort,
          uint8_t FeedbackEp_ = 2,
          AudioSyncMode SyncMode_ = AudioSyncMode::Async,
          bool SampleRateControlEnabled_ = true,
          typename SampleT_ = int32_t>
class AudioInterface {
  public:
    // Version
    static constexpr UacVersion UAC_VERSION = Version;
    static constexpr bool IS_UAC2 = (Version == UacVersion::Uac2);
    static constexpr AudioSyncMode SYNC_MODE = SyncMode_;
    static constexpr bool SAMPLE_RATE_CONTROL = SampleRateControlEnabled_;
    using SampleT = SampleT_;

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

    struct OutPacketStats;

    static_assert(!IS_UAC2 || !HAS_AUDIO_OUT || (AudioOut::ALT_COUNT == 1),
                  "UAC2 Audio OUT supports a single alt setting");
    static_assert(!IS_UAC2 || !HAS_AUDIO_IN || (AudioIn::ALT_COUNT == 1),
                  "UAC2 Audio IN supports a single alt setting");

    // Endpoint assignments
    static constexpr uint8_t EP_AUDIO_OUT = AudioOut::ENDPOINT;
    static constexpr uint8_t EP_AUDIO_IN = AudioIn::ENDPOINT;
    static constexpr uint8_t EP_FEEDBACK = FeedbackEp_;
    static constexpr uint8_t EP_MIDI_OUT = MidiOut::ENDPOINT;
    static constexpr uint8_t EP_MIDI_IN = MidiIn::ENDPOINT;

    // Audio OUT configuration (for backward compatibility)
    static constexpr uint32_t SAMPLE_RATE =
        HAS_AUDIO_OUT ? AudioOut::SAMPLE_RATE : (HAS_AUDIO_IN ? AudioIn::SAMPLE_RATE : 48000);
    static constexpr uint8_t CHANNELS = HAS_AUDIO_OUT ? AudioOut::CHANNELS : (HAS_AUDIO_IN ? AudioIn::CHANNELS : 2);
    static constexpr uint8_t BIT_DEPTH = HAS_AUDIO_OUT ? AudioOut::BIT_DEPTH : (HAS_AUDIO_IN ? AudioIn::BIT_DEPTH : 16);
    static constexpr uint16_t BYTES_PER_FRAME = CHANNELS * (BIT_DEPTH / 8);

    // UAC constants
    static constexpr uint8_t SUBCLASS_AUDIOCONTROL = 0x01;
    static constexpr uint8_t SUBCLASS_AUDIOSTREAMING = 0x02;
    static constexpr uint8_t SUBCLASS_MIDISTREAMING = 0x03;

    // Callbacks
    using StatusCallback = void (*)(bool streaming);
    using MidiCallback = void (*)(uint8_t cable, const uint8_t* data, uint8_t len);
    using SysExCallback = void (*)(const uint8_t* data, uint16_t len);
    using SampleRateCallback = void (*)(uint32_t new_rate); ///< Called when host requests sample rate change

    StatusCallback on_streaming_change = nullptr;       // Audio OUT streaming state
    StatusCallback on_audio_in_change = nullptr;        // Audio IN streaming state
    void (*on_audio_rx)(void) = nullptr;                // Called on each Audio OUT packet
    void (*on_feedback_sent)(void) = nullptr;           // Called when feedback EP transmits
    void (*on_sof_app)(void) = nullptr;                 // Called on USB SOF (1ms) - for app to supply Audio IN data
    SampleRateCallback on_sample_rate_change = nullptr; ///< Called when host sets new sample rate

    void set_midi_callback(MidiCallback cb) { midi_processor_.on_midi = cb; }
    void set_sysex_callback(SysExCallback cb) { midi_processor_.on_sysex = cb; }
    void set_sample_rate_callback(SampleRateCallback cb) { on_sample_rate_change = cb; }

    /// Get current runtime sample rate (may differ from template SAMPLE_RATE after host change)
    [[nodiscard]] uint32_t current_sample_rate() const noexcept { return current_sample_rate_; }

    /// Check if sample rate was changed by host
    [[nodiscard]] bool sample_rate_changed() const noexcept { return sample_rate_changed_; }

    /// Clear sample rate changed flag
    void clear_sample_rate_changed() noexcept { sample_rate_changed_ = false; }

    /// Set default mute/volume used on reset
    void set_feature_defaults(bool out_mute, int16_t out_db, bool in_mute, int16_t in_db) noexcept {
        default_out_mute_ = out_mute;
        default_out_volume_ = out_db;
        default_in_mute_ = in_mute;
        default_in_volume_ = in_db;
        if constexpr (HAS_AUDIO_OUT) {
            fu_out_mute_ = default_out_mute_;
            fu_out_volume_ = default_out_volume_;
        }
        if constexpr (HAS_AUDIO_IN) {
            fu_in_mute_ = default_in_mute_;
            fu_in_volume_ = default_in_volume_;
        }
    }

    /// Set default sample rate used before host negotiation
    void set_default_sample_rate(uint32_t rate) noexcept {
        current_sample_rate_ = rate;
        in_rate_accum_ = 0;
    }

  private:
    // ========================================================================
    // Descriptor Size Calculation
    // ========================================================================

    static constexpr std::size_t calc_descriptor_size() {
        std::size_t size = 9; // Configuration descriptor

        if constexpr (HAS_AUDIO && HAS_MIDI) {
            size += 8; // IAD
        }

        if constexpr (HAS_AUDIO) {
            // Audio Control Interface (shared for IN and OUT)
            size += 9; // Interface descriptor

            if constexpr (IS_UAC2) {
                // UAC2 AC Header
                size += 9; // AC Header
                size += 8; // Clock Source

                if constexpr (HAS_AUDIO_OUT) {
                    size += 17; // Input Terminal (USB streaming -> device)
                    size += 12; // Output Terminal (device -> speaker)
                }
                if constexpr (HAS_AUDIO_IN) {
                    size += 17; // Input Terminal (microphone -> device)
                    size += 12; // Output Terminal (device -> USB streaming)
                }
            } else {
                // UAC1 AC Header
                std::size_t ac_size = 9; // Header base (8 + bInCollection)
                if constexpr (HAS_AUDIO_OUT) {
                    ac_size += 12 + 10 + 9; // IT + FU + OT
                }
                if constexpr (HAS_AUDIO_IN) {
                    ac_size += 12 + 10 + 9; // IT + FU + OT
                }
                // Add interface count to header
                uint8_t num_streaming = (HAS_AUDIO_OUT ? 1 : 0) + (HAS_AUDIO_IN ? 1 : 0);
                ac_size += num_streaming - 1; // baInterfaceNr array
                size += ac_size;
            }

            // Audio Streaming Interfaces
            if constexpr (HAS_AUDIO_OUT) {
                constexpr size_t alt_count = AudioOut::ALT_COUNT;
                size += 9 + (alt_count * 9); // Alt 0 + active alts
                if constexpr (IS_UAC2) {
                    size += 16 + 6 + 7 + 8; // AS General + Format + EP + CS EP
                    if constexpr (SYNC_MODE == AudioSyncMode::Async) {
                        size += 7; // Feedback EP (UAC2 standard endpoint)
                    }
                } else {
                    constexpr std::size_t alt_total = []<size_t... Is>(std::index_sequence<Is...>) {
                        std::size_t total = 0;
                        ((total += (7 + (8 + (AudioOut::template Alt<Is>::RATES_COUNT * 3)) + 9 + 7 +
                                    (SYNC_MODE == AudioSyncMode::Async ? 9 : 0))),
                         ...);
                        return total;
                    }(std::make_index_sequence<alt_count>{});
                    size += alt_total;
                }
            }

            if constexpr (HAS_AUDIO_IN) {
                constexpr size_t alt_count = AudioIn::ALT_COUNT;
                size += 9 + (alt_count * 9); // Alt 0 + active alts
                if constexpr (IS_UAC2) {
                    size += 16 + 6 + 7 + 8; // AS General + Format + EP + CS EP
                } else {
                    constexpr std::size_t alt_total = []<size_t... Is>(std::index_sequence<Is...>) {
                        std::size_t total = 0;
                        ((total += (7 + (8 + (AudioIn::template Alt<Is>::RATES_COUNT * 3)) + 9 + 7)), ...);
                        return total;
                    }(std::make_index_sequence<alt_count>{});
                    size += alt_total;
                }
            }
        }

        // MIDI Streaming Interface
        if constexpr (HAS_MIDI) {
            size += 9; // Interface
            size += 7; // MS Header

            // Jacks for MIDI OUT (host -> device)
            if constexpr (HAS_MIDI_OUT) {
                size += 6; // IN Jack Embedded
                size += 6; // IN Jack External
                size += 9; // OUT Jack Embedded
                size += 9; // OUT Jack External
            }

            // Jacks for MIDI IN (device -> host)
            if constexpr (HAS_MIDI_IN) {
                size += 6; // IN Jack Embedded
                size += 6; // IN Jack External
                size += 9; // OUT Jack Embedded
                size += 9; // OUT Jack External
            }

            // Endpoints
            if constexpr (HAS_MIDI_OUT) {
                size += 9 + 5; // Bulk OUT EP + CS
            }
            if constexpr (HAS_MIDI_IN) {
                size += 9 + 5; // Bulk IN EP + CS
            }
        }

        return size;
    }

    static constexpr std::size_t MAX_DESC_SIZE = calc_descriptor_size();
    // UAC1 bRefresh exponent; bRefresh=2 => 4ms (2^2 frames).
    static constexpr uint8_t FB_REFRESH = (SYNC_MODE == AudioSyncMode::Async && !IS_UAC2) ? 2 : 0;

    // ========================================================================
    // Descriptor Builder
    // ========================================================================

    static constexpr auto build_descriptor() {
        std::array<uint8_t, MAX_DESC_SIZE> desc{};
        std::size_t p = 0;

        auto w = [&desc, &p](auto... bytes) { ((desc[p++] = static_cast<uint8_t>(bytes)), ...); };

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
            if constexpr (HAS_AUDIO_OUT)
                audio_out_iface = iface_num++;
            if constexpr (HAS_AUDIO_IN)
                audio_in_iface = iface_num++;
        }
        if constexpr (HAS_MIDI) {
            midi_iface = iface_num++;
        }

        std::size_t total_size = calc_descriptor_size();

        // === Configuration Descriptor ===
        w(9, bDescriptorType::Configuration);
        w16(total_size);
        w(iface_num); // bNumInterfaces
        w(1);         // bConfigurationValue
        w(0);         // iConfiguration
        w(0x80);      // bmAttributes (bus powered)
        w(100);       // bMaxPower (200mA)

        // IAD for Audio + MIDI composite (single Audio function)
        if constexpr (HAS_AUDIO && HAS_MIDI) {
            w(8, bDescriptorType::InterfaceAssociation);
            w(audio_ctrl_iface);
            w(iface_num);
            w(bDeviceClass::Audio);
            w(0x00);
            w(0x00);
            w(0);
        }

        // ================================================================
        // Audio Control Interface
        // ================================================================
        if constexpr (HAS_AUDIO) {
            // Interface descriptor
            w(9, bDescriptorType::Interface);
            w(audio_ctrl_iface);
            w(0); // bAlternateSetting
            w(0); // bNumEndpoints
            w(bDeviceClass::Audio);
            w(SUBCLASS_AUDIOCONTROL);
            w(IS_UAC2 ? uac::uac2::IP_VERSION_02_00 : 0);
            w(0); // iInterface

            if constexpr (IS_UAC2) {
                // UAC2: Calculate AC header total length
                constexpr uint16_t ac_total = 9 + 8 + (HAS_AUDIO_OUT ? (17 + 12) : 0) + (HAS_AUDIO_IN ? (17 + 12) : 0);

                // AC Header
                w(9, bDescriptorType::CsInterface, uac::ac::HEADER);
                w16(0x0200); // bcdADC = 2.0
                w(uac::uac2::FUNCTION_SUBCLASS);
                w16(ac_total);
                w(0x00); // bmControls

                // Clock Source (shared)
                w(8, bDescriptorType::CsInterface, uac::ac::CLOCK_SOURCE);
                w(1); // bClockID
                w(uac::uac2::CLOCK_INTERNAL_FIXED);
                w(0x07); // bmControls
                w(0);    // bAssocTerminal
                w(0);    // iClockSource

                // Audio OUT path: IT(2) -> OT(3)
                if constexpr (HAS_AUDIO_OUT) {
                    // Input Terminal (USB streaming)
                    w(17, bDescriptorType::CsInterface, uac::ac::INPUT_TERMINAL);
                    w(2); // bTerminalID
                    w16(uac::TERMINAL_USB_STREAMING);
                    w(0); // bAssocTerminal
                    w(1); // bCSourceID
                    w(AudioOut::CHANNELS);
                    w32(AudioOut::CHANNEL_CONFIG);
                    w(0);        // iChannelNames
                    w16(0x0000); // bmControls
                    w(0);        // iTerminal

                    // Output Terminal (speaker)
                    w(12, bDescriptorType::CsInterface, uac::ac::OUTPUT_TERMINAL);
                    w(3); // bTerminalID
                    w16(uac::TERMINAL_SPEAKER);
                    w(0); // bAssocTerminal
                    w(2); // bSourceID
                    w(1); // bCSourceID
                    w16(0x0000);
                    w(0);
                }

                // Audio IN path: IT(4) -> OT(5)
                if constexpr (HAS_AUDIO_IN) {
                    // Input Terminal (microphone)
                    w(17, bDescriptorType::CsInterface, uac::ac::INPUT_TERMINAL);
                    w(4); // bTerminalID
                    w16(uac::TERMINAL_MICROPHONE);
                    w(0);
                    w(1); // bCSourceID
                    w(AudioIn::CHANNELS);
                    w32(AudioIn::CHANNEL_CONFIG);
                    w(0);
                    w16(0x0000);
                    w(0);

                    // Output Terminal (USB streaming)
                    w(12, bDescriptorType::CsInterface, uac::ac::OUTPUT_TERMINAL);
                    w(5); // bTerminalID
                    w16(uac::TERMINAL_USB_STREAMING);
                    w(0);
                    w(4); // bSourceID
                    w(1); // bCSourceID
                    w16(0x0000);
                    w(0);
                }
            } else {
                // UAC1: AC Header with feature units
                // Entity IDs:
                //   Audio OUT: IT=1, FU=2, OT=3
                //   Audio IN:  IT=4, FU=5, OT=6
                uint8_t num_streaming = (HAS_AUDIO_OUT ? 1 : 0) + (HAS_AUDIO_IN ? 1 : 0);
                std::size_t ac_total =
                    8 + num_streaming + (HAS_AUDIO_OUT ? (12 + 10 + 9) : 0) + (HAS_AUDIO_IN ? (12 + 10 + 9) : 0);

                // AC Header
                w(static_cast<uint8_t>(8 + num_streaming), bDescriptorType::CsInterface, uac::ac::HEADER);
                w16(0x0100); // bcdADC = 1.0
                w16(ac_total);
                w(num_streaming); // bInCollection
                if constexpr (HAS_AUDIO_OUT)
                    w(audio_out_iface);
                if constexpr (HAS_AUDIO_IN)
                    w(audio_in_iface);

                // Audio OUT path: IT(1) -> FU(2) -> OT(3)
                if constexpr (HAS_AUDIO_OUT) {
                    // Input Terminal
                    w(12, bDescriptorType::CsInterface, uac::ac::INPUT_TERMINAL);
                    w(1); // bTerminalID
                    w16(uac::TERMINAL_USB_STREAMING);
                    w(0);
                    w(AudioOut::CHANNELS);
                    w16(AudioOut::CHANNELS == 2 ? 0x0003 : 0x0000);
                    w(0);
                    w(0);

                    // Feature Unit
                    w(10, bDescriptorType::CsInterface, uac::ac::FEATURE_UNIT);
                    w(2);    // bUnitID
                    w(1);    // bSourceID
                    w(1);    // bControlSize
                    w(0x03); // bmaControls[0] Master: Mute + Volume
                    w(0x00); // bmaControls[1]
                    w(0x00); // bmaControls[2]
                    w(0);

                    // Output Terminal
                    w(9, bDescriptorType::CsInterface, uac::ac::OUTPUT_TERMINAL);
                    w(3); // bTerminalID
                    w16(uac::TERMINAL_SPEAKER);
                    w(0);
                    w(2); // bSourceID (Feature Unit)
                    w(0);
                }

                // Audio IN path: IT(4) -> FU(5) -> OT(6)
                if constexpr (HAS_AUDIO_IN) {
                    // Input Terminal (microphone)
                    w(12, bDescriptorType::CsInterface, uac::ac::INPUT_TERMINAL);
                    w(4); // bTerminalID
                    w16(uac::TERMINAL_MICROPHONE);
                    w(0);
                    w(AudioIn::CHANNELS);
                    w16(AudioIn::CHANNELS == 2 ? 0x0003 : 0x0000);
                    w(0);
                    w(0);

                    // Feature Unit
                    w(10, bDescriptorType::CsInterface, uac::ac::FEATURE_UNIT);
                    w(5); // bUnitID
                    w(4); // bSourceID
                    w(1);
                    w(0x03); // Mute + Volume
                    w(0x00);
                    w(0x00);
                    w(0);

                    // Output Terminal (USB streaming)
                    w(9, bDescriptorType::CsInterface, uac::ac::OUTPUT_TERMINAL);
                    w(6); // bTerminalID
                    w16(uac::TERMINAL_USB_STREAMING);
                    w(0);
                    w(5); // bSourceID (Feature Unit)
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
                w(0); // bAlternateSetting
                w(0); // bNumEndpoints
                w(bDeviceClass::Audio);
                w(SUBCLASS_AUDIOSTREAMING);
                w(IS_UAC2 ? uac::uac2::IP_VERSION_02_00 : 0);
                w(0);

                if constexpr (IS_UAC2) {
                    // Alt 1 (active) - UAC2 single format
                    w(9, bDescriptorType::Interface);
                    w(audio_out_iface);
                    w(1); // bAlternateSetting
                    w((SYNC_MODE == AudioSyncMode::Async) ? 2 : 1);
                    w(bDeviceClass::Audio);
                    w(SUBCLASS_AUDIOSTREAMING);
                    w(uac::uac2::IP_VERSION_02_00);
                    w(0);

                    // AS General
                    w(16, bDescriptorType::CsInterface, uac::as::GENERAL);
                    w(2); // bTerminalLink (IT)
                    w(0x00);
                    w(uac::FORMAT_TYPE_I);
                    w32(0x00000001); // PCM
                    w(AudioOut::CHANNELS);
                    w32(AudioOut::CHANNEL_CONFIG);
                    w(0);

                    // Format Type I
                    w(6, bDescriptorType::CsInterface, uac::as::FORMAT_TYPE);
                    w(uac::FORMAT_TYPE_I);
                    w(AudioOut::BIT_DEPTH / 8);
                    w(AudioOut::BIT_DEPTH);

                    // Audio Endpoint
                    w(7, bDescriptorType::Endpoint);
                    w(EP_AUDIO_OUT);
                    w(static_cast<uint8_t>(SYNC_MODE));
                    w16(AudioOut::PACKET_SIZE);
                    w(1);
                    w(0);

                    // CS Audio Endpoint
                    w(8, bDescriptorType::CsEndpoint, uac::as::GENERAL);
                    w(0x00);
                    w(0x00);
                    w(0);
                    w16(0);

                    if constexpr (SYNC_MODE == AudioSyncMode::Async) {
                        // Feedback Endpoint
                        w(7, bDescriptorType::Endpoint);
                        w(0x80 | EP_FEEDBACK);
                        w(0x11); // Iso, Feedback
                        w16(4);  // 4 bytes for UAC2
                        w(4);    // bInterval
                        w(0);
                    }
                } else {
                    auto write_out_alt = [&]<size_t AltIndex>() {
                        using Alt = typename AudioOut::template Alt<AltIndex>;
                        constexpr uint8_t alt_setting = static_cast<uint8_t>(AltIndex + 1);
                        constexpr uint8_t bytes_per_frame =
                            static_cast<uint8_t>(AudioOut::CHANNELS * (Alt::BIT_DEPTH / 8));
                        constexpr uint16_t packet_size =
                            static_cast<uint16_t>((((Alt::MAX_RATE + 999) / 1000) + 1) * bytes_per_frame);
                        constexpr uint8_t out_rate_count = static_cast<uint8_t>(Alt::RATES_COUNT);

                        w(9, bDescriptorType::Interface);
                        w(audio_out_iface);
                        w(alt_setting); // bAlternateSetting
                        w((SYNC_MODE == AudioSyncMode::Async) ? 2 : 1);
                        w(bDeviceClass::Audio);
                        w(SUBCLASS_AUDIOSTREAMING);
                        w(0);
                        w(0);

                        // UAC1 AS General
                        w(7, bDescriptorType::CsInterface, uac::as::GENERAL);
                        w(1); // bTerminalLink (IT)
                        w(1); // bDelay
                        w16(uac::FORMAT_PCM);

                        // Format Type I - per-alt rates
                        w(static_cast<uint8_t>(8 + (out_rate_count * 3)),
                          bDescriptorType::CsInterface,
                          uac::as::FORMAT_TYPE);
                        w(uac::FORMAT_TYPE_I);
                        w(AudioOut::CHANNELS);
                        w(Alt::BIT_DEPTH / 8);
                        w(Alt::BIT_DEPTH);
                        w(out_rate_count);
                        for (uint32_t rate : Alt::RATES) {
                            w24(rate);
                        }

                        // Audio Endpoint
                        w(9, bDescriptorType::Endpoint);
                        w(EP_AUDIO_OUT);
                        w(static_cast<uint8_t>(SYNC_MODE));
                        w16(packet_size);
                        w(1);
                        w(0); // bRefresh is for feedback EP, not data EP
                        w((SYNC_MODE == AudioSyncMode::Async) ? (0x80 | EP_FEEDBACK) : 0);

                        // CS Audio Endpoint - with Sampling Frequency Control (optional)
                        w(7, bDescriptorType::CsEndpoint, uac::as::GENERAL);
                        w(SAMPLE_RATE_CONTROL ? 0x01 : 0x00);
                        w(0);
                        w16(0);

                        if constexpr (SYNC_MODE == AudioSyncMode::Async) {
                            // Feedback Endpoint (Async mode)
                            // UAC1 uses 9-byte isochronous sync endpoint descriptor
                            w(9, bDescriptorType::Endpoint);
                            w(0x80 | EP_FEEDBACK);
                            w(0x11); // Iso, Feedback
                            w16(3);  // 10.14 format (3 bytes)
                            w(1);    // bInterval (1ms)
                            w(FB_REFRESH);
                            w(0); // bSynchAddress
                        }
                    };

                    auto write_out_alts = [&]<size_t... Is>(std::index_sequence<Is...>) {
                        (write_out_alt.template operator()<Is>(), ...);
                    };

                    write_out_alts(std::make_index_sequence<AudioOut::ALT_COUNT>{});
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

                if constexpr (IS_UAC2) {
                    // Alt 1 (active) - UAC2 single format
                    w(9, bDescriptorType::Interface);
                    w(audio_in_iface);
                    w(1);
                    w(1);
                    w(bDeviceClass::Audio);
                    w(SUBCLASS_AUDIOSTREAMING);
                    w(uac::uac2::IP_VERSION_02_00);
                    w(0);

                    // AS General
                    w(16, bDescriptorType::CsInterface, uac::as::GENERAL);
                    w(5); // bTerminalLink (OT for IN)
                    w(0x00);
                    w(uac::FORMAT_TYPE_I);
                    w32(0x00000001);
                    w(AudioIn::CHANNELS);
                    w32(AudioIn::CHANNEL_CONFIG);
                    w(0);

                    // Format Type I
                    w(6, bDescriptorType::CsInterface, uac::as::FORMAT_TYPE);
                    w(uac::FORMAT_TYPE_I);
                    w(AudioIn::BIT_DEPTH / 8);
                    w(AudioIn::BIT_DEPTH);

                    // Audio Endpoint (IN)
                    w(7, bDescriptorType::Endpoint);
                    w(0x80 | EP_AUDIO_IN);
                    w(static_cast<uint8_t>(SYNC_MODE));
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
                    auto write_in_alt = [&]<size_t AltIndex>() {
                        using Alt = typename AudioIn::template Alt<AltIndex>;
                        constexpr uint8_t alt_setting = static_cast<uint8_t>(AltIndex + 1);
                        constexpr uint8_t bytes_per_frame =
                            static_cast<uint8_t>(AudioIn::CHANNELS * (Alt::BIT_DEPTH / 8));
                        constexpr uint16_t packet_size =
                            static_cast<uint16_t>((((Alt::MAX_RATE + 999) / 1000) + 1) * bytes_per_frame);
                        constexpr uint8_t in_rate_count = static_cast<uint8_t>(Alt::RATES_COUNT);

                        w(9, bDescriptorType::Interface);
                        w(audio_in_iface);
                        w(alt_setting);
                        w(1);
                        w(bDeviceClass::Audio);
                        w(SUBCLASS_AUDIOSTREAMING);
                        w(0);
                        w(0);

                        // UAC1 AS General
                        w(7, bDescriptorType::CsInterface, uac::as::GENERAL);
                        w(6); // bTerminalLink (OT)
                        w(1);
                        w16(uac::FORMAT_PCM);

                        // Format Type I - per-alt rates
                        w(static_cast<uint8_t>(8 + (in_rate_count * 3)),
                          bDescriptorType::CsInterface,
                          uac::as::FORMAT_TYPE);
                        w(uac::FORMAT_TYPE_I);
                        w(AudioIn::CHANNELS);
                        w(Alt::BIT_DEPTH / 8);
                        w(Alt::BIT_DEPTH);
                        w(in_rate_count);
                        for (uint32_t rate : Alt::RATES) {
                            w24(rate);
                        }

                        // Audio Endpoint (IN)
                        w(9, bDescriptorType::Endpoint);
                        w(0x80 | EP_AUDIO_IN);
                        w(static_cast<uint8_t>(SYNC_MODE));
                        w16(packet_size);
                        w(1);
                        w(0);
                        w(0);

                        // CS Audio Endpoint - with Sampling Frequency Control (optional)
                        w(7, bDescriptorType::CsEndpoint, uac::as::GENERAL);
                        w(SAMPLE_RATE_CONTROL ? 0x01 : 0x00);
                        w(0);
                        w16(0);
                    };

                    auto write_in_alts = [&]<size_t... Is>(std::index_sequence<Is...>) {
                        (write_in_alt.template operator()<Is>(), ...);
                    };

                    write_in_alts(std::make_index_sequence<AudioIn::ALT_COUNT>{});
                }
            }
        }

        // ================================================================
        // MIDI Streaming Interface
        // ================================================================
        if constexpr (HAS_MIDI) {
            // Calculate MS header total length
            constexpr uint16_t ms_total = 7 + (HAS_MIDI_OUT ? (6 + 6 + 9 + 9) : 0) +
                                          (HAS_MIDI_IN ? (6 + 6 + 9 + 9) : 0) + (HAS_MIDI_OUT ? (9 + 5) : 0) +
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
                w(1); // bJackID
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
                w(1); // bNrInputPins
                w(2); // baSourceID (external IN)
                w(1); // baSourcePin
                w(0);

                // OUT Jack External (connected to embedded IN)
                w(9, bDescriptorType::CsInterface, uac::ms::MIDI_OUT_JACK);
                w(uac::JACK_EXTERNAL);
                w(4);
                w(1);
                w(1); // baSourceID (embedded IN)
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
                w(6); // From external IN
                w(1);
                w(0);

                // OUT Jack External
                w(9, bDescriptorType::CsInterface, uac::ms::MIDI_OUT_JACK);
                w(uac::JACK_EXTERNAL);
                w(8);
                w(1);
                w(5); // From embedded IN
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
                w(1); // bNumEmbMIDIJack
                w(1); // baAssocJackID (embedded IN jack)
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
                w(7); // baAssocJackID (embedded OUT jack)
            }
        }

        return desc;
    }

    static constexpr auto descriptor_ = build_descriptor();

    struct AltRuntimeConfig {
        uint8_t bit_depth = 0;
        uint8_t bytes_per_frame = 0;
        uint16_t packet_size = 0;
        const uint32_t* rates = nullptr;
        uint8_t rates_count = 0;
    };

    template <typename Port, size_t Index>
    static constexpr AltRuntimeConfig make_alt_config() {
        using Alt = typename Port::template Alt<Index>;
        return {
            Alt::BIT_DEPTH,
            static_cast<uint8_t>(Port::CHANNELS * (Alt::BIT_DEPTH / 8)),
            static_cast<uint16_t>((((Alt::MAX_RATE + 999) / 1000) + 1) * (Port::CHANNELS * (Alt::BIT_DEPTH / 8))),
            Alt::RATES.data(),
            static_cast<uint8_t>(Alt::RATES_COUNT),
        };
    }

    template <typename Port, size_t... Is>
    static constexpr auto make_alt_configs(std::index_sequence<Is...>) {
        return std::array<AltRuntimeConfig, Port::ALT_COUNT>{make_alt_config<Port, Is>()...};
    }

    static constexpr auto OUT_ALT_CONFIGS = make_alt_configs<AudioOut>(std::make_index_sequence<AudioOut::ALT_COUNT>{});
    static constexpr auto IN_ALT_CONFIGS = make_alt_configs<AudioIn>(std::make_index_sequence<AudioIn::ALT_COUNT>{});

    static constexpr AltRuntimeConfig default_out_alt() {
        if constexpr (HAS_AUDIO_OUT && AudioOut::ALT_COUNT > 0) {
            return OUT_ALT_CONFIGS[0];
        } else {
            return {};
        }
    }

    static constexpr AltRuntimeConfig default_in_alt() {
        if constexpr (HAS_AUDIO_IN && AudioIn::ALT_COUNT > 0) {
            return IN_ALT_CONFIGS[0];
        } else {
            return {};
        }
    }

    static bool rate_in_list(const AltRuntimeConfig& cfg, uint32_t rate) {
        if (cfg.rates == nullptr || cfg.rates_count == 0)
            return false;
        for (uint8_t i = 0; i < cfg.rates_count; ++i) {
            if (cfg.rates[i] == rate)
                return true;
        }
        return false;
    }

    static constexpr int32_t clamp_i24(int32_t value) {
        if (value > 0x7FFFFF)
            return 0x7FFFFF;
        if (value < -0x800000)
            return -0x800000;
        return value;
    }

    static constexpr int32_t sample_from_i16(int16_t value) { return static_cast<int32_t>(value) << 8; }

    static constexpr int16_t sample_to_i16(int32_t value) { return static_cast<int16_t>(clamp_i24(value) >> 8); }

    static int32_t sample_from_i24(const uint8_t* data) {
        // USB Audio 24-bit is little-endian: [0]=LSB, [1]=mid, [2]=MSB
        int32_t value = static_cast<int32_t>(data[0]) | (static_cast<int32_t>(data[1]) << 8) |
                        (static_cast<int32_t>(data[2]) << 16);
        // Sign extend from bit 23
        if (value & 0x800000) {
            value |= 0xFF000000;
        }
        return value;
    }

    static void sample_to_i24(int32_t value, uint8_t* data) {
        int32_t v = clamp_i24(value);
        data[0] = static_cast<uint8_t>(v & 0xFF);
        data[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
        data[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    }

    static void configure_pll_controller(PllRateController& controller, uint32_t target_level) {
        if (target_level >= 512) {
            target_level = (target_level * 2) / 3; // 96kHz: slower, smoother tracking to reduce audible modulation
        }
        umidsp::PiConfig cfg = umidsp::PiConfig::stable(static_cast<int32_t>(target_level));
        if (target_level >= 512) {
            // 96kHz: slower, smoother tracking to reduce audible modulation
            cfg.max_ppm = 150;   // Reduced from 200 to minimize rate swings
            cfg.hysteresis = 48; // Increased from 32 to prevent oscillation around target
            cfg.kp_num = 1;
            cfg.kp_den = 12; // Reduced from 8 (slower proportional response)
            cfg.ki_num = 1;
            cfg.ki_den = 800;        // Reduced from 600 (slower integral buildup)
            cfg.integral_max = 6000; // Reduced from 8000
        } else {
            // 44.1/48kHz: gentler tracking to suppress tick noise
            cfg.max_ppm = 200;   // Reduced from 300
            cfg.hysteresis = 36; // Increased from 24
            cfg.kp_num = 1;
            cfg.kp_den = 6; // Reduced from 4 (slower response)
            cfg.ki_num = 1;
            cfg.ki_den = 500;        // Reduced from 400
            cfg.integral_max = 8000; // Reduced from 12000
        }
        controller.set_config(cfg);
        controller.reset();
    }

    static void samples_from_i16(const int16_t* src, SampleT* dst, uint32_t count) {
        if constexpr (std::is_same_v<SampleT, int16_t>) {
            __builtin_memcpy(dst, src, static_cast<size_t>(count) * sizeof(SampleT));
        } else {
            for (uint32_t i = 0; i < count; ++i) {
                dst[i] = sample_from_i16(src[i]);
            }
        }
    }

    static void samples_to_i16(const SampleT* src, int16_t* dst, uint32_t count) {
        if constexpr (std::is_same_v<SampleT, int16_t>) {
            __builtin_memcpy(dst, src, static_cast<size_t>(count) * sizeof(SampleT));
        } else {
            for (uint32_t i = 0; i < count; ++i) {
                dst[i] = sample_to_i16(src[i]);
            }
        }
    }

    // ========================================================================
    // State
    // ========================================================================

    bool audio_out_streaming_ = false;
    bool audio_in_streaming_ = false;
    bool midi_configured_ = false;
    bool audio_in_pending_ = false; // Ready to send next Audio IN packet
    uint32_t sof_count_ = 0;        // SOF frame counter for feedback interval

    // Feature Unit state (UAC1) - Audio OUT
    bool fu_out_mute_ = false;
    int16_t fu_out_volume_ = 0; // 1/256 dB

    // Feature Unit state (UAC1) - Audio IN
    bool fu_in_mute_ = false;
    int16_t fu_in_volume_ = 0;

    // Default Feature Unit settings
    bool default_out_mute_ = false;
    int16_t default_out_volume_ = 0;
    bool default_in_mute_ = false;
    int16_t default_in_volume_ = 0;

    // Pending SET request
    uint8_t pending_set_entity_ = 0;
    uint8_t pending_set_ctrl_ = 0;
    uint8_t pending_set_len_ = 0;

    // Dynamic sample rate support
    uint32_t current_sample_rate_ = SAMPLE_RATE; ///< Runtime sample rate (may change from template default)
    uint32_t actual_sample_rate_ = 0;            ///< Actual I2S rate (set by kernel after PLL config)
    bool sample_rate_changed_ = false;           ///< Flag indicating sample rate was changed by host
    bool pending_sample_rate_set_ = false;       ///< Waiting for SET CUR data phase
    uint8_t pending_sample_rate_ep_ = 0;         ///< Endpoint for pending SET CUR
    bool pending_uac2_sample_rate_set_ = false;  ///< Waiting for UAC2 SET CUR data phase
    uint32_t in_rate_accum_ = 0;                 ///< Accumulator for fractional IN frames (Hz % 1000)

    // Debug counters for sample rate change
    mutable uint32_t dbg_sr_get_cur_count_ = 0;    ///< GET CUR sample rate requests
    mutable uint32_t dbg_sr_set_cur_count_ = 0;    ///< SET CUR sample rate requests
    mutable uint32_t dbg_sr_ep0_rx_count_ = 0;     ///< on_ep0_rx calls with pending sample rate
    mutable uint32_t dbg_sr_last_requested_ = 0;   ///< Last requested sample rate
    mutable uint32_t dbg_sr_ep_request_count_ = 0; ///< Endpoint sample rate requests (UAC1)

    // Debug: Audio OUT packet boundary discontinuity (USB RX)
    int32_t dbg_out_rx_last_sample_l_ = 0;
    int32_t dbg_out_rx_last_sample_r_ = 0;
    bool dbg_out_rx_has_last_sample_ = false;
    bool dbg_out_rx_enabled_ = false;
    uint8_t out_discard_packets_ = 0;
    bool out_primed_ = false;
    uint32_t out_prime_frames_ = 0;
    uint16_t out_rx_blocked_frames_ = 0;
    static constexpr uint16_t kBlockFramesLowRate = 12;
    static constexpr uint16_t kBlockFramesHighRate = 48;
    static constexpr uint8_t kDiscardPacketsAfterReset = 8;
    uint32_t dbg_out_rx_disc_count_ = 0;
    uint32_t dbg_out_rx_disc_max_ = 0;
    int32_t dbg_out_rx_disc_last_ = 0;
    uint32_t dbg_out_rx_disc_threshold_ = 0x20000; // 24-bit scale
    uint32_t dbg_out_rx_intra_count_ = 0;
    uint32_t dbg_out_rx_intra_max_ = 0;
    int32_t dbg_out_rx_intra_last_ = 0;
    uint32_t dbg_out_rx_intra_event_count_ = 0;
    uint32_t dbg_out_rx_intra_event_len_ = 0;
    int32_t dbg_out_rx_intra_event_cur_l_ = 0;
    int32_t dbg_out_rx_intra_event_cur_r_ = 0;
    int32_t dbg_out_rx_intra_event_prev_l_ = 0;
    int32_t dbg_out_rx_intra_event_prev_r_ = 0;
    int32_t dbg_out_rx_intra_event_dl_ = 0;
    int32_t dbg_out_rx_intra_event_dr_ = 0;
    uint32_t dbg_out_rx_intra_log_threshold_ = 0x2000;
    uint32_t dbg_out_rx_packet_index_ = 0;
    uint32_t dbg_out_rx_packet_max_abs_ = 0;
    int32_t dbg_out_rx_packet_cur_l_ = 0;
    int32_t dbg_out_rx_packet_cur_r_ = 0;
    int32_t dbg_out_rx_packet_prev_l_ = 0;
    int32_t dbg_out_rx_packet_prev_r_ = 0;
    int32_t dbg_out_rx_packet_dl_ = 0;
    int32_t dbg_out_rx_packet_dr_ = 0;
    uint32_t dbg_out_rx_packet_raw0_ = 0;
    uint32_t dbg_out_rx_packet_raw1_ = 0;
    using AudioOutPacketCallback = void (*)(const OutPacketStats&);
    AudioOutPacketCallback on_audio_out_packet_ = nullptr;

    // Audio processing - ring buffer size based on max sample rate
    // Higher sample rates need larger buffers: 96kHz->512, 48kHz->256 frames
    // This gives ~4-5ms buffer at any rate for jitter absorption
    static constexpr uint32_t OUT_BUFFER_FRAMES = HAS_AUDIO_OUT ? AudioOut::BUFFER_FRAMES : 128;
    static constexpr uint32_t IN_BUFFER_FRAMES = HAS_AUDIO_IN ? AudioIn::BUFFER_FRAMES : 128;
    AudioRingBuffer<OUT_BUFFER_FRAMES, HAS_AUDIO_OUT ? AudioOut::CHANNELS : 2, SampleT> out_ring_buffer_;
    AudioRingBuffer<IN_BUFFER_FRAMES, HAS_AUDIO_IN ? AudioIn::CHANNELS : 2, SampleT> in_ring_buffer_;
    FeedbackCalculator<Version> feedback_calc_;
    PllRateController pll_controller_;
    PllRateController in_pll_controller_; // ASRC for Audio IN
    MidiProcessor midi_processor_;

    AltRuntimeConfig current_out_alt_ = default_out_alt();
    AltRuntimeConfig current_in_alt_ = default_in_alt();

    static constexpr uint32_t OUT_MAX_PACKET_FRAMES = HAS_AUDIO_OUT ? ((AudioOut::MAX_SAMPLE_RATE / 1000) + 1) : 1;
    static constexpr uint32_t IN_MAX_PACKET_FRAMES = HAS_AUDIO_IN ? ((AudioIn::MAX_SAMPLE_RATE / 1000) + 1) : 1;

    static constexpr uint32_t OUT_DECODE_SAMPLES = HAS_AUDIO_OUT ? (OUT_MAX_PACKET_FRAMES * AudioOut::CHANNELS) : 1;
    static constexpr uint32_t OUT_READ_SAMPLES = HAS_AUDIO_OUT ? (OUT_BUFFER_FRAMES * AudioOut::CHANNELS) : 1;
    static constexpr uint32_t IN_READ_SAMPLES = HAS_AUDIO_IN ? (IN_MAX_PACKET_FRAMES * AudioIn::CHANNELS) : 1;
    static constexpr uint32_t IN_PACKET_BYTES = HAS_AUDIO_IN ? (IN_MAX_PACKET_FRAMES * AudioIn::CHANNELS * 3) : 1;

    alignas(4) SampleT out_decode_buf_[OUT_DECODE_SAMPLES]{};
    alignas(4) SampleT out_read_buf_[OUT_READ_SAMPLES]{};
    alignas(4) SampleT in_read_buf_[IN_READ_SAMPLES]{};
    alignas(4) SampleT in_write_buf_[IN_READ_SAMPLES]{};
    alignas(4) uint8_t in_packet_buf_[IN_PACKET_BYTES]{};

    static constexpr uint32_t FB_UPDATE_INTERVAL =
        (SYNC_MODE == AudioSyncMode::Async && !IS_UAC2) ? (1U << FB_REFRESH) : 1;
    bool fb_last_valid_ = false;
    std::array<uint8_t, IS_UAC2 ? 4 : 3> fb_last_bytes_{};

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
        configure_pll_controller(pll_controller_, OUT_BUFFER_FRAMES / 2);
        configure_pll_controller(in_pll_controller_, IN_BUFFER_FRAMES / 2);
    }

    void reset() {
        if constexpr (HAS_AUDIO_OUT) {
            out_ring_buffer_.reset();
            feedback_calc_.reset(AudioOut::SAMPLE_RATE);
            fu_out_mute_ = default_out_mute_;
            fu_out_volume_ = default_out_volume_;
            current_out_alt_ = default_out_alt();
            out_primed_ = false;
            configure_out_sync_windows(AudioOut::SAMPLE_RATE);
        }
        if constexpr (HAS_AUDIO_IN) {
            in_ring_buffer_.reset();
            fu_in_mute_ = default_in_mute_;
            fu_in_volume_ = default_in_volume_;
            current_in_alt_ = default_in_alt();
        }
        configure_pll_controller(pll_controller_, OUT_BUFFER_FRAMES / 2);
        configure_pll_controller(in_pll_controller_, IN_BUFFER_FRAMES / 2);
        audio_out_streaming_ = false;
        audio_in_streaming_ = false;
        midi_configured_ = false;
        sof_count_ = 0;
        in_rate_accum_ = 0;
        fb_last_valid_ = false;
    }

    // Reset audio-out FIFO/feedback without dropping the streaming state.
    void reset_audio_out(uint32_t actual_rate) {
        if constexpr (HAS_AUDIO_OUT) {
            out_ring_buffer_.reset();
            feedback_calc_.reset(actual_rate);
            feedback_calc_.set_actual_rate(actual_rate);
            feedback_calc_.set_fifo_threshold(out_ring_buffer_.capacity() / 2);
            configure_pll_controller(pll_controller_, out_ring_buffer_.capacity() / 2);
            fb_last_valid_ = false;
            out_discard_packets_ = kDiscardPacketsAfterReset;
            out_primed_ = false;
            configure_out_sync_windows(actual_rate);
            dbg_out_rx_has_last_sample_ = false;
            dbg_out_rx_packet_index_ = 0;
        }
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
            if (on_streaming_change)
                on_streaming_change(false);
            if (on_audio_in_change)
                on_audio_in_change(false);
        }
    }

    template <typename HalT>
    void configure_endpoints(HalT& hal) {
        if constexpr (HAS_MIDI_OUT) {
            hal.ep_configure({EP_MIDI_OUT, Direction::Out, TransferType::Bulk, MidiOut::PACKET_SIZE});
        }
        if constexpr (HAS_MIDI_IN) {
            hal.ep_configure({EP_MIDI_IN, Direction::In, TransferType::Bulk, MidiIn::PACKET_SIZE});
        }
        midi_configured_ = HAS_MIDI;
    }

    template <typename HalT>
    void set_interface(HalT& hal, uint8_t interface, uint8_t alt_setting) {
        ++dbg_set_interface_count_;
        dbg_last_set_iface_ = interface;
        dbg_last_set_alt_ = alt_setting;

        // Audio OUT streaming interface
        if constexpr (HAS_AUDIO_OUT) {
            if (interface == audio_out_iface_num_) {
                bool was_streaming = audio_out_streaming_;

                if (alt_setting >= 1 && alt_setting <= AudioOut::ALT_COUNT) {
                    current_out_alt_ = OUT_ALT_CONFIGS[alt_setting - 1];
                    hal.ep_configure(
                        {EP_AUDIO_OUT, Direction::Out, TransferType::Isochronous, current_out_alt_.packet_size});
                    dbg_out_rx_last_len_ = 0;
                    dbg_out_rx_min_len_ = 0;
                    dbg_out_rx_max_len_ = 0;
                    dbg_out_rx_short_count_ = 0;

                    if constexpr (SYNC_MODE == AudioSyncMode::Async) {
                        // Async mode: configure feedback endpoint
                        constexpr uint16_t fb_size = IS_UAC2 ? 4 : 3;
                        hal.ep_configure({EP_FEEDBACK, Direction::In, TransferType::Isochronous, fb_size});
                    }

                    audio_out_streaming_ = true;
                    out_ring_buffer_.reset();
                    out_discard_packets_ = kDiscardPacketsAfterReset;
                    out_primed_ = false;
                    configure_out_sync_windows(current_sample_rate_);
                    dbg_out_rx_has_last_sample_ = false;
                    if (!rate_in_list(current_out_alt_, current_sample_rate_) && current_out_alt_.rates_count > 0) {
                        current_sample_rate_ = current_out_alt_.rates[0];
                        sample_rate_changed_ = true;
                        if (on_sample_rate_change) {
                            on_sample_rate_change(current_sample_rate_);
                        }
                    }
                    // Use actual_sample_rate_ if set, otherwise fall back to current_sample_rate_
                    uint32_t rate = (actual_sample_rate_ > 0) ? actual_sample_rate_ : current_sample_rate_;
                    feedback_calc_.reset(rate);
                    feedback_calc_.set_actual_rate(rate);
                    feedback_calc_.set_fifo_threshold(out_ring_buffer_.capacity() / 2);
                    configure_pll_controller(pll_controller_, out_ring_buffer_.capacity() / 2);
                    fb_last_bytes_ = feedback_calc_.get_feedback_bytes();
                    fb_last_valid_ = true;
                } else {
                    audio_out_streaming_ = false;
                    fb_last_valid_ = false;
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

                if (alt_setting >= 1 && alt_setting <= AudioIn::ALT_COUNT) {
                    current_in_alt_ = IN_ALT_CONFIGS[alt_setting - 1];
                    hal.ep_configure(
                        {EP_AUDIO_IN, Direction::In, TransferType::Isochronous, current_in_alt_.packet_size});
                    audio_in_streaming_ = true;
                    audio_in_pending_ = true; // Ready to send first packet
                    // Audio IN: immediately enable reading to avoid buffer overrun
                    // (Unlike Audio OUT which needs prebuffer for smooth playback)
                    in_ring_buffer_.reset_and_start();
                    configure_pll_controller(in_pll_controller_, in_ring_buffer_.capacity() / 2);
                    if (!rate_in_list(current_in_alt_, current_sample_rate_) && current_in_alt_.rates_count > 0) {
                        current_sample_rate_ = current_in_alt_.rates[0];
                        sample_rate_changed_ = true;
                        if (on_sample_rate_change) {
                            on_sample_rate_change(current_sample_rate_);
                        }
                    }
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

        (void)hal;
        (void)interface;
        (void)alt_setting;
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
                bool is_get = (setup.bmRequestType & 0x80) != 0;

                if (entity == 1) {                // Clock Source
                    if (ctrl == 0x01 && is_get) { // GET CUR freq
                        uint32_t rate = current_sample_rate_;
                        response[0] = rate & 0xFF;
                        response[1] = (rate >> 8) & 0xFF;
                        response[2] = (rate >> 16) & 0xFF;
                        response[3] = (rate >> 24) & 0xFF;
                        response = response.subspan(0, 4);
                        return true;
                    }
                    if (ctrl == 0x01 && !is_get && SAMPLE_RATE_CONTROL) { // SET CUR freq
                        pending_uac2_sample_rate_set_ = true;
                        response = response.subspan(0, 0);
                        return true;
                    }
                    if (ctrl == 0x02 && is_get) { // GET RANGE
                        constexpr auto& rates = HAS_AUDIO_OUT ? AudioOut::RATES : AudioIn::RATES;
                        constexpr uint16_t rate_count = HAS_AUDIO_OUT ? AudioOut::RATES_COUNT : AudioIn::RATES_COUNT;

                        uint16_t max_ranges = 0;
                        if (response.size() >= 2) {
                            max_ranges = static_cast<uint16_t>((response.size() - 2) / 12);
                        }
                        uint16_t count = (rate_count < max_ranges) ? rate_count : max_ranges;
                        response[0] = count & 0xFF;
                        response[1] = (count >> 8) & 0xFF;

                        size_t p = 2;
                        for (uint16_t i = 0; i < count; ++i) {
                            uint32_t rate = rates[i];
                            response[p++] = rate & 0xFF;
                            response[p++] = (rate >> 8) & 0xFF;
                            response[p++] = (rate >> 16) & 0xFF;
                            response[p++] = (rate >> 24) & 0xFF;
                            response[p++] = rate & 0xFF;
                            response[p++] = (rate >> 8) & 0xFF;
                            response[p++] = (rate >> 16) & 0xFF;
                            response[p++] = (rate >> 24) & 0xFF;
                            response[p++] = 0;
                            response[p++] = 0;
                            response[p++] = 0;
                            response[p++] = 0;
                        }
                        response = response.subspan(0, p);
                        return true;
                    }
                }
            }
        }

        // UAC1 Endpoint Sampling Frequency Control requests
        if constexpr (!IS_UAC2 && HAS_AUDIO_OUT && SAMPLE_RATE_CONTROL) {
            // bmRequestType: 0x22 = SET, class, endpoint
            //                0xA2 = GET, class, endpoint
            uint8_t recipient = setup.bmRequestType & 0x1F;
            bool is_endpoint_request = (recipient == 0x02); // Endpoint recipient

            if (is_endpoint_request) {
                uint8_t ep = setup.wIndex & 0x0F;
                uint8_t ctrl = setup.wValue >> 8;
                bool is_get = (setup.bmRequestType & 0x80) != 0;

                // Sampling Frequency Control = 0x01
                if (ctrl == 0x01 && (ep == EP_AUDIO_OUT || ep == EP_AUDIO_IN)) {
                    ++dbg_sr_ep_request_count_;

                    if (is_get) {
                        // GET CUR - return current sample rate (3 bytes for UAC1)
                        ++dbg_sr_get_cur_count_;
                        uint32_t rate = current_sample_rate_;
                        response[0] = rate & 0xFF;
                        response[1] = (rate >> 8) & 0xFF;
                        response[2] = (rate >> 16) & 0xFF;
                        response = response.subspan(0, 3);
                        return true;
                    } else {
                        // SET CUR - expect 3 bytes sample rate in DATA phase
                        ++dbg_sr_set_cur_count_;
                        pending_sample_rate_set_ = true;
                        pending_sample_rate_ep_ = ep;
                        response = response.subspan(0, 0);
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
                        if (ctrl == 0x01) { // Mute
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
                        if (ctrl == 0x02) { // Volume
                            if (setup.bRequest == kGetCur) {
                                response[0] = fu_out_volume_ & 0xFF;
                                response[1] = (fu_out_volume_ >> 8) & 0xFF;
                                response = response.subspan(0, 2);
                                return true;
                            }
                            if (setup.bRequest == kGetMin) {
                                response[0] = 0x00;
                                response[1] = 0x81; // -127dB
                                response = response.subspan(0, 2);
                                return true;
                            }
                            if (setup.bRequest == kGetMax) {
                                response[0] = 0x00;
                                response[1] = 0x00; // 0dB
                                response = response.subspan(0, 2);
                                return true;
                            }
                            if (setup.bRequest == kGetRes) {
                                response[0] = 0x00;
                                response[1] = 0x01; // 1dB
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
                                response[0] = 0x00;
                                response[1] = 0x81;
                                response = response.subspan(0, 2);
                                return true;
                            }
                            if (setup.bRequest == kGetMax) {
                                response[0] = 0x00;
                                response[1] = 0x00;
                                response = response.subspan(0, 2);
                                return true;
                            }
                            if (setup.bRequest == kGetRes) {
                                response[0] = 0x00;
                                response[1] = 0x01;
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
                if (len > response.size())
                    len = static_cast<uint16_t>(response.size());
                for (uint16_t i = 0; i < len; ++i)
                    response[i] = 0;
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
            if (len > response.size())
                len = static_cast<uint16_t>(response.size());
            for (uint16_t i = 0; i < len; ++i)
                response[i] = 0;
            response = response.subspan(0, len);
            return true;
        }
        response = response.subspan(0, 0);
        return true;
    }

    void on_ep0_rx(std::span<const uint8_t> data) {
        // UAC1 Endpoint Sampling Frequency Control - data phase
        if constexpr (!IS_UAC2 && HAS_AUDIO_OUT && SAMPLE_RATE_CONTROL) {
            if (pending_sample_rate_set_ && data.size() >= 3) {
                ++dbg_sr_ep0_rx_count_;
                uint32_t new_rate = data[0] | (data[1] << 8) | (data[2] << 16);
                dbg_sr_last_requested_ = new_rate;

                const AltRuntimeConfig* cfg = nullptr;
                if (pending_sample_rate_ep_ == EP_AUDIO_OUT) {
                    cfg = &current_out_alt_;
                } else if (pending_sample_rate_ep_ == EP_AUDIO_IN) {
                    cfg = &current_in_alt_;
                }

                if (cfg != nullptr && rate_in_list(*cfg, new_rate)) {
                    bool needs_change = (new_rate != current_sample_rate_);
                    if (!needs_change && actual_sample_rate_ > 0) {
                        uint32_t diff = (actual_sample_rate_ > new_rate) ? (actual_sample_rate_ - new_rate)
                                                                         : (new_rate - actual_sample_rate_);
                        // If hardware is still running at a different rate, force a change
                        needs_change = (diff > 100);
                    }

                    if (needs_change) {
                        current_sample_rate_ = new_rate;
                        sample_rate_changed_ = true;
                        in_rate_accum_ = 0;
                        if (on_sample_rate_change) {
                            on_sample_rate_change(new_rate);
                        }
                    }
                }
                pending_sample_rate_set_ = false;
                pending_sample_rate_ep_ = 0;
                return;
            }
        }

        if constexpr (IS_UAC2 && HAS_AUDIO && SAMPLE_RATE_CONTROL) {
            if (pending_uac2_sample_rate_set_ && data.size() >= 4) {
                uint32_t new_rate = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
                dbg_sr_last_requested_ = new_rate;
                if (new_rate != current_sample_rate_) {
                    current_sample_rate_ = new_rate;
                    sample_rate_changed_ = true;
                    in_rate_accum_ = 0;
                    if (on_sample_rate_change) {
                        on_sample_rate_change(new_rate);
                    }
                }
                pending_uac2_sample_rate_set_ = false;
                return;
            }
        }

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
                if (current_out_alt_.bytes_per_frame == 0)
                    return;
                bool dbg_enabled = dbg_out_rx_enabled_;
                if (out_rx_blocked_frames_ > 0) {
                    return;
                }
                if (out_discard_packets_ > 0) {
                    --out_discard_packets_;
                    dbg_out_rx_has_last_sample_ = false;
                    return;
                }
                if (dbg_enabled) {
                    dbg_out_rx_last_len_ = static_cast<uint32_t>(data.size());
                    if (dbg_out_rx_min_len_ == 0 || dbg_out_rx_last_len_ < dbg_out_rx_min_len_) {
                        dbg_out_rx_min_len_ = dbg_out_rx_last_len_;
                    }
                    if (dbg_out_rx_last_len_ > dbg_out_rx_max_len_) {
                        dbg_out_rx_max_len_ = dbg_out_rx_last_len_;
                    }
                    if (dbg_out_rx_last_len_ < current_out_alt_.packet_size) {
                        ++dbg_out_rx_short_count_;
                    }
                }
                uint16_t frame_count = static_cast<uint16_t>(data.size() / current_out_alt_.bytes_per_frame);
                if (frame_count > OUT_MAX_PACKET_FRAMES) {
                    frame_count = static_cast<uint16_t>(OUT_MAX_PACKET_FRAMES);
                }
                const uint8_t* src = data.data();
                uint32_t samples = static_cast<uint32_t>(frame_count) * AudioOut::CHANNELS;

                if (current_out_alt_.bit_depth == 16) {
                    if (dbg_enabled && data.size() >= 4) {
                        dbg_out_rx_packet_raw0_ =
                            static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
                            (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
                        if (data.size() >= 8) {
                            dbg_out_rx_packet_raw1_ =
                                static_cast<uint32_t>(data[4]) | (static_cast<uint32_t>(data[5]) << 8) |
                                (static_cast<uint32_t>(data[6]) << 16) | (static_cast<uint32_t>(data[7]) << 24);
                        } else {
                            dbg_out_rx_packet_raw1_ = 0;
                        }
                    }
                    for (uint32_t i = 0; i < samples; ++i) {
                        uint16_t raw = static_cast<uint16_t>(src[0] | (src[1] << 8));
                        if constexpr (std::is_same_v<SampleT, int16_t>) {
                            out_decode_buf_[i] = static_cast<int16_t>(raw);
                        } else {
                            out_decode_buf_[i] = sample_from_i16(static_cast<int16_t>(raw));
                        }
                        src += 2;
                    }
                } else if (current_out_alt_.bit_depth == 24) {
                    if (dbg_enabled && data.size() >= 4) {
                        dbg_out_rx_packet_raw0_ =
                            static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
                            (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
                        if (data.size() >= 8) {
                            dbg_out_rx_packet_raw1_ =
                                static_cast<uint32_t>(data[4]) | (static_cast<uint32_t>(data[5]) << 8) |
                                (static_cast<uint32_t>(data[6]) << 16) | (static_cast<uint32_t>(data[7]) << 24);
                        } else {
                            dbg_out_rx_packet_raw1_ = 0;
                        }
                    }
                    for (uint32_t i = 0; i < samples; ++i) {
                        int32_t v = sample_from_i24(src);
                        if constexpr (std::is_same_v<SampleT, int16_t>) {
                            out_decode_buf_[i] = sample_to_i16(v);
                        } else {
                            out_decode_buf_[i] = v;
                        }
                        src += 3;
                    }
                } else {
                    return;
                }

                // Detect discontinuity at USB packet boundary (compare with last sample)
                if (dbg_enabled && samples > 0) {
                    uint32_t packet_index = ++dbg_out_rx_packet_index_;
                    uint32_t max_abs = 0;
                    int32_t max_cur_l = 0;
                    int32_t max_cur_r = 0;
                    int32_t max_prev_l = 0;
                    int32_t max_prev_r = 0;
                    int32_t max_dl = 0;
                    int32_t max_dr = 0;
                    int32_t first_l = out_decode_buf_[0];
                    int32_t first_r = (AudioOut::CHANNELS > 1) ? out_decode_buf_[1] : first_l;
                    if (dbg_out_rx_has_last_sample_) {
                        int32_t delta_l = first_l - dbg_out_rx_last_sample_l_;
                        int32_t delta_r = first_r - dbg_out_rx_last_sample_r_;
                        int32_t abs_l = (delta_l < 0) ? -delta_l : delta_l;
                        int32_t abs_r = (delta_r < 0) ? -delta_r : delta_r;
                        int32_t abs_max = (abs_l > abs_r) ? abs_l : abs_r;
                        dbg_out_rx_disc_last_ = delta_l;
                        if (static_cast<uint32_t>(abs_max) > dbg_out_rx_disc_threshold_) {
                            ++dbg_out_rx_disc_count_;
                            if (static_cast<uint32_t>(abs_max) > dbg_out_rx_disc_max_) {
                                dbg_out_rx_disc_max_ = static_cast<uint32_t>(abs_max);
                            }
                        }
                    }
                    // Intra-packet discontinuity (max delta between consecutive samples)
                    for (uint32_t i = 1; i < samples; ++i) {
                        int32_t cur_l = out_decode_buf_[i * AudioOut::CHANNELS];
                        int32_t cur_r = (AudioOut::CHANNELS > 1) ? out_decode_buf_[i * AudioOut::CHANNELS + 1] : cur_l;
                        int32_t prev_l = out_decode_buf_[(i - 1) * AudioOut::CHANNELS];
                        int32_t prev_r =
                            (AudioOut::CHANNELS > 1) ? out_decode_buf_[(i - 1) * AudioOut::CHANNELS + 1] : prev_l;
                        int32_t dl = cur_l - prev_l;
                        int32_t dr = cur_r - prev_r;
                        int32_t abs_l = (dl < 0) ? -dl : dl;
                        int32_t abs_r = (dr < 0) ? -dr : dr;
                        int32_t abs_max = (abs_l > abs_r) ? abs_l : abs_r;
                        if (static_cast<uint32_t>(abs_max) > max_abs) {
                            max_abs = static_cast<uint32_t>(abs_max);
                            max_cur_l = cur_l;
                            max_cur_r = cur_r;
                            max_prev_l = prev_l;
                            max_prev_r = prev_r;
                            max_dl = dl;
                            max_dr = dr;
                        }
                        if (static_cast<uint32_t>(abs_max) > dbg_out_rx_disc_threshold_) {
                            ++dbg_out_rx_intra_count_;
                            dbg_out_rx_intra_last_ = dl;
                            if (static_cast<uint32_t>(abs_max) > dbg_out_rx_intra_max_) {
                                dbg_out_rx_intra_max_ = static_cast<uint32_t>(abs_max);
                            }
                            ++dbg_out_rx_intra_event_count_;
                            dbg_out_rx_intra_event_len_ = dbg_out_rx_last_len_;
                            dbg_out_rx_intra_event_cur_l_ = cur_l;
                            dbg_out_rx_intra_event_cur_r_ = cur_r;
                            dbg_out_rx_intra_event_prev_l_ = prev_l;
                            dbg_out_rx_intra_event_prev_r_ = prev_r;
                            dbg_out_rx_intra_event_dl_ = dl;
                            dbg_out_rx_intra_event_dr_ = dr;
                        }
                    }
                    uint32_t last_idx = (frame_count - 1) * AudioOut::CHANNELS;
                    dbg_out_rx_last_sample_l_ = out_decode_buf_[last_idx];
                    dbg_out_rx_last_sample_r_ =
                        (AudioOut::CHANNELS > 1) ? out_decode_buf_[last_idx + 1] : out_decode_buf_[last_idx];
                    dbg_out_rx_has_last_sample_ = true;

                    dbg_out_rx_packet_max_abs_ = max_abs;
                    dbg_out_rx_packet_cur_l_ = max_cur_l;
                    dbg_out_rx_packet_cur_r_ = max_cur_r;
                    dbg_out_rx_packet_prev_l_ = max_prev_l;
                    dbg_out_rx_packet_prev_r_ = max_prev_r;
                    dbg_out_rx_packet_dl_ = max_dl;
                    dbg_out_rx_packet_dr_ = max_dr;

                    if (on_audio_out_packet_ && max_abs > dbg_out_rx_intra_log_threshold_) {
                        OutPacketStats stats{
                            packet_index,
                            dbg_out_rx_last_len_,
                            static_cast<int32_t>(max_abs),
                            dbg_out_rx_packet_raw0_,
                            dbg_out_rx_packet_raw1_,
                            max_cur_l,
                            max_cur_r,
                            max_prev_l,
                            max_prev_r,
                            max_dl,
                            max_dr,
                        };
                        on_audio_out_packet_(stats);
                    }
                }

                out_ring_buffer_.write(out_decode_buf_, frame_count);

                // TEMPORARY: Disable FIFO-based feedback for debugging
                // Use fixed feedback based on actual I2S clock rate (set in set_interface)
                // The feedback value is set by set_actual_rate(47991) and remains constant
                // This helps isolate whether the issue is in feedback calculation vs transmission

                if (on_audio_rx)
                    on_audio_rx();
            }
        }

        if constexpr (HAS_MIDI_OUT) {
            if (ep == EP_MIDI_OUT) {
                for (std::size_t i = 0; i + 3 < data.size(); i += 4) {
                    midi_processor_.process_packet(data[i], data[i + 1], data[i + 2], data[i + 3]);
                }
            }
        }
    }

    // ========================================================================
    // SOF and Feedback
    // ========================================================================

    template <typename HalT>
    void on_sof(HalT& hal) {
        // Async mode: Update feedback from FIFO level and send every SOF when streaming
        // TinyUSB FIFO count method: adjust feedback based on buffer fill level
        if constexpr (HAS_AUDIO_OUT) {
            if (audio_out_streaming_) {
                if (out_rx_blocked_frames_ > 0) {
                    --out_rx_blocked_frames_;
                }
                if constexpr (SYNC_MODE == AudioSyncMode::Async) {
                    ++sof_count_;
                    const bool update = (!fb_last_valid_) || ((sof_count_ % FB_UPDATE_INTERVAL) == 0);
                    if (update && !dbg_fb_override_enable_) {
                        if (out_rx_blocked_frames_ == 0) {
                            // Update feedback based on current buffer level
                            uint32_t level = out_ring_buffer_.buffered_frames();
                            feedback_calc_.update_from_fifo_level(level);
                            fb_last_bytes_ = feedback_calc_.get_feedback_bytes();
                            fb_last_valid_ = true;
                        }
                    }

                    if (dbg_fb_override_enable_) {
                        if constexpr (UAC_VERSION == UacVersion::Uac1) {
                            uint32_t v = dbg_fb_override_value_ & 0x00FFFFFFu;
                            fb_last_bytes_ = {static_cast<uint8_t>(v & 0xFF),
                                              static_cast<uint8_t>((v >> 8) & 0xFF),
                                              static_cast<uint8_t>((v >> 16) & 0xFF)};
                        } else {
                            uint32_t v = dbg_fb_override_value_;
                            fb_last_bytes_ = {static_cast<uint8_t>(v & 0xFF),
                                              static_cast<uint8_t>((v >> 8) & 0xFF),
                                              static_cast<uint8_t>((v >> 16) & 0xFF),
                                              static_cast<uint8_t>((v >> 24) & 0xFF)};
                        }
                        fb_last_valid_ = true;
                    }

                    hal.ep_write(EP_FEEDBACK, fb_last_bytes_.data(), static_cast<uint16_t>(fb_last_bytes_.size()));
                    ++dbg_fb_sent_count_;
                    if (on_feedback_sent)
                        on_feedback_sent();
                }
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
                send_audio_in(hal); // Use ring buffer data from PDM mic
            }
        }
        (void)hal;
    }

    /// Called when TX completes on an endpoint
    template <typename HalT>
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
                send_audio_in(hal); // Use ring buffer data from PDM mic
            }
        }
        (void)hal;
        (void)ep;
    }

    void on_samples_consumed(uint32_t frame_count) {
        if constexpr (HAS_AUDIO_OUT && SYNC_MODE == AudioSyncMode::Async) {
            feedback_calc_.add_consumed_samples(frame_count);
        }
        (void)frame_count;
    }

    /// DEBUG: Send test audio pattern for Audio IN
    // Test tone phase index (persistent across calls)
    mutable uint32_t test_phase_idx_ = 0;

    template <typename HalT>
    void send_test_audio_in(HalT& hal) {
        if constexpr (!HAS_AUDIO_IN) {
            (void)hal;
            return;
        } else {
            // Use pre-computed sine table for faster ISR execution
            // 1kHz at 48kHz = 48 samples per cycle
            // Table covers one full cycle at max amplitude
            static constexpr int16_t kSineTable[48] = {
                0,      4277,   8481,   12539,  16384,  19947,  23170,  25996,  28377,  30273,  31650,  32487,
                32767,  32487,  31650,  30273,  28377,  25996,  23170,  19947,  16384,  12539,  8481,   4277,
                0,      -4277,  -8481,  -12539, -16384, -19947, -23170, -25996, -28377, -30273, -31650, -32487,
                -32767, -32487, -31650, -30273, -28377, -25996, -23170, -19947, -16384, -12539, -8481,  -4277};

            uint32_t frames_per_packet = current_sample_rate_ / 1000;
            uint32_t remainder = current_sample_rate_ % 1000;
            in_rate_accum_ += remainder;
            if (in_rate_accum_ >= 1000) {
                in_rate_accum_ -= 1000;
                ++frames_per_packet;
            }
            if (frames_per_packet > IN_MAX_PACKET_FRAMES) {
                frames_per_packet = IN_MAX_PACKET_FRAMES;
            }

            for (uint32_t i = 0; i < frames_per_packet; ++i) {
                int16_t sample = kSineTable[test_phase_idx_];
                in_read_buf_[i * 2] = sample_from_i16(sample);     // Left
                in_read_buf_[i * 2 + 1] = sample_from_i16(sample); // Right
                test_phase_idx_ = (test_phase_idx_ + 1) % 48;
            }

            uint32_t sample_count = frames_per_packet * AudioIn::CHANNELS;
            uint8_t* out = in_packet_buf_;
            if (current_in_alt_.bit_depth == 16) {
                for (uint32_t i = 0; i < sample_count; ++i) {
                    int16_t s = 0;
                    if constexpr (std::is_same_v<SampleT, int16_t>) {
                        s = in_read_buf_[i];
                    } else {
                        s = sample_to_i16(in_read_buf_[i]);
                    }
                    out[0] = static_cast<uint8_t>(s & 0xFF);
                    out[1] = static_cast<uint8_t>((s >> 8) & 0xFF);
                    out += 2;
                }
            } else if (current_in_alt_.bit_depth == 24) {
                for (uint32_t i = 0; i < sample_count; ++i) {
                    int32_t v = 0;
                    if constexpr (std::is_same_v<SampleT, int16_t>) {
                        v = sample_from_i16(in_read_buf_[i]);
                    } else {
                        v = in_read_buf_[i];
                    }
                    sample_to_i24(v, out);
                    out += 3;
                }
            } else {
                return;
            }

            hal.ep_write(EP_AUDIO_IN, in_packet_buf_, frames_per_packet * current_in_alt_.bytes_per_frame);
        }
    }

    // ========================================================================
    // Audio OUT Buffer Access
    // ========================================================================

    uint32_t read_audio(int16_t* dest, uint32_t frame_count) {
        if constexpr (!HAS_AUDIO_OUT) {
            (void)dest;
            (void)frame_count;
            return 0;
        } else {
            if (!out_primed_) {
                int32_t level = out_ring_buffer_.buffer_level();
                if (level < static_cast<int32_t>(out_prime_frames_)) {
                    __builtin_memset(dest, 0, static_cast<size_t>(frame_count) * AudioOut::CHANNELS * sizeof(int16_t));
                    return 0;
                }
                out_primed_ = true;
            }
            uint32_t read = out_ring_buffer_.read(out_read_buf_, frame_count);
            apply_volume_out(out_read_buf_, read);
            samples_to_i16(out_read_buf_, dest, read * AudioOut::CHANNELS);
            if (read < frame_count) {
                uint32_t remaining = (frame_count - read) * AudioOut::CHANNELS;
                __builtin_memset(
                    dest + (read * AudioOut::CHANNELS), 0, static_cast<size_t>(remaining) * sizeof(int16_t));
            }
            return read;
        }
    }

    template <typename T = SampleT, typename = std::enable_if_t<!std::is_same_v<T, int16_t>>>
    uint32_t read_audio(SampleT* dest, uint32_t frame_count) {
        if constexpr (!HAS_AUDIO_OUT) {
            (void)dest;
            (void)frame_count;
            return 0;
        } else {
            if (!out_primed_) {
                int32_t level = out_ring_buffer_.buffer_level();
                if (level < static_cast<int32_t>(out_prime_frames_)) {
                    __builtin_memset(dest, 0, static_cast<size_t>(frame_count) * AudioOut::CHANNELS * sizeof(SampleT));
                    return 0;
                }
                out_primed_ = true;
            }
            uint32_t read = out_ring_buffer_.read(dest, frame_count);
            apply_volume_out(dest, read);
            if (read < frame_count) {
                uint32_t remaining = (frame_count - read) * AudioOut::CHANNELS;
                __builtin_memset(
                    dest + (read * AudioOut::CHANNELS), 0, static_cast<size_t>(remaining) * sizeof(SampleT));
            }
            return read;
        }
    }

    uint32_t read_audio_asrc(int16_t* dest, uint32_t frame_count) {
        if constexpr (!HAS_AUDIO_OUT) {
            (void)dest;
            (void)frame_count;
            return 0;
        } else {
            if constexpr (SYNC_MODE == AudioSyncMode::Sync || SYNC_MODE == AudioSyncMode::Async) {
                return read_audio(dest, frame_count);
            }
            if (!out_primed_) {
                int32_t level = out_ring_buffer_.buffer_level();
                if (level < static_cast<int32_t>(out_prime_frames_)) {
                    __builtin_memset(dest, 0, static_cast<size_t>(frame_count) * AudioOut::CHANNELS * sizeof(int16_t));
                    return 0;
                }
                out_primed_ = true;
            }
            int32_t level = out_ring_buffer_.buffer_level();
            int32_t ppm = pll_controller_.update(level);
            uint32_t rate = AudioRingBuffer<OUT_BUFFER_FRAMES, AudioOut::CHANNELS, SampleT>::ppm_to_rate_q16(ppm);

            uint32_t read = out_ring_buffer_.read_interpolated(out_read_buf_, frame_count, rate);
            // Note: Feedback is now calculated from FIFO level in on_sof(), not from consumed samples
            apply_volume_out(out_read_buf_, read);
            samples_to_i16(out_read_buf_, dest, read * AudioOut::CHANNELS);
            return read;
        }
    }

    template <typename T = SampleT, typename = std::enable_if_t<!std::is_same_v<T, int16_t>>>
    uint32_t read_audio_asrc(SampleT* dest, uint32_t frame_count) {
        if constexpr (!HAS_AUDIO_OUT) {
            (void)dest;
            (void)frame_count;
            return 0;
        } else {
            if constexpr (SYNC_MODE == AudioSyncMode::Sync || SYNC_MODE == AudioSyncMode::Async) {
                return read_audio(dest, frame_count);
            }
            if (!out_primed_) {
                int32_t level = out_ring_buffer_.buffer_level();
                if (level < static_cast<int32_t>(out_prime_frames_)) {
                    __builtin_memset(dest, 0, static_cast<size_t>(frame_count) * AudioOut::CHANNELS * sizeof(SampleT));
                    return 0;
                }
                out_primed_ = true;
            }
            int32_t level = out_ring_buffer_.buffer_level();
            int32_t ppm = pll_controller_.update(level);
            uint32_t rate = AudioRingBuffer<OUT_BUFFER_FRAMES, AudioOut::CHANNELS, SampleT>::ppm_to_rate_q16(ppm);

            uint32_t read = out_ring_buffer_.read_interpolated(dest, frame_count, rate);
            apply_volume_out(dest, read);
            return read;
        }
    }

  private:
    void apply_volume_out(SampleT* dest, uint32_t read) {
        if constexpr (!HAS_AUDIO_OUT)
            return;
        if (read == 0)
            return;

        if (fu_out_mute_) {
            __builtin_memset(dest, 0, static_cast<size_t>(read) * AudioOut::CHANNELS * sizeof(SampleT));
            return;
        }

        if (fu_out_volume_ < 0) {
            int32_t neg_vol = -static_cast<int32_t>(fu_out_volume_);
            int32_t db_256 = (neg_vol * 48) / 127;

            if (db_256 >= 12288) {
                __builtin_memset(dest, 0, static_cast<size_t>(read) * AudioOut::CHANNELS * sizeof(SampleT));
                return;
            }

            int32_t shift = db_256 / 1541;
            int32_t frac = db_256 % 1541;
            int32_t gain = 32768 >> shift;
            gain = gain - ((gain * frac) / 3082);
            if (gain < 1)
                gain = 1;

            uint32_t samples = read * AudioOut::CHANNELS;
            for (uint32_t i = 0; i < samples; ++i) {
                int64_t sample = static_cast<int64_t>(dest[i]);
                sample = (sample * gain) >> 15;
                if constexpr (std::is_same_v<SampleT, int16_t>) {
                    if (sample > 32767)
                        sample = 32767;
                    if (sample < -32768)
                        sample = -32768;
                    dest[i] = static_cast<int16_t>(sample);
                } else {
                    dest[i] = static_cast<SampleT>(clamp_i24(static_cast<int32_t>(sample)));
                }
            }
        }
    }

  public:
    // ========================================================================
    // Audio IN Buffer Access
    // ========================================================================

    uint32_t write_audio_in(const int16_t* src, uint32_t frame_count) {
        if constexpr (!HAS_AUDIO_IN) {
            (void)src;
            (void)frame_count;
            return 0;
        } else {
            uint32_t samples = frame_count * AudioIn::CHANNELS;
            samples_from_i16(src, in_write_buf_, samples);
            return in_ring_buffer_.write(in_write_buf_, frame_count);
        }
    }

    template <typename T = SampleT, typename = std::enable_if_t<!std::is_same_v<T, int16_t>>>
    uint32_t write_audio_in(const SampleT* src, uint32_t frame_count) {
        if constexpr (!HAS_AUDIO_IN) {
            (void)src;
            (void)frame_count;
            return 0;
        } else {
            return in_ring_buffer_.write(src, frame_count);
        }
    }

    template <typename HalT>
    void send_audio_in(HalT& hal) {
        if constexpr (!HAS_AUDIO_IN) {
            (void)hal;
            return;
        } else {
            if (!audio_in_streaming_)
                return;

            ++dbg_send_audio_in_count_; // Debug: track calls

            uint32_t frames_per_packet = current_sample_rate_ / 1000;
            uint32_t remainder = current_sample_rate_ % 1000;
            in_rate_accum_ += remainder;
            if (in_rate_accum_ >= 1000) {
                in_rate_accum_ -= 1000;
                ++frames_per_packet;
            }
            if (frames_per_packet > IN_MAX_PACKET_FRAMES) {
                frames_per_packet = IN_MAX_PACKET_FRAMES;
            }

            // ASRC DISABLED FOR TESTING: Use direct read without interpolation/rate adjustment
            // The ASRC PI controller may be causing 4Hz modulation - bypass to test
            // int32_t level = in_ring_buffer_.buffer_level();
            // int32_t ppm = in_pll_controller_.update(level);
            // uint32_t rate = AudioRingBuffer<IN_BUFFER_FRAMES, AudioIn::CHANNELS, SampleT>::ppm_to_rate_q16(ppm);
            // uint32_t read = in_ring_buffer_.read_interpolated(in_read_buf_, frames_per_packet, rate);

            // Direct read without ASRC (rate = 1.0, no interpolation)
            uint32_t read = in_ring_buffer_.read(in_read_buf_, frames_per_packet);

            // Debug: track buffer level
            dbg_in_buffered_ = in_ring_buffer_.buffered_frames();

            // For isochronous IN, we must send data every frame even if buffer is empty
            // Send silence if no data available
            if (read < frames_per_packet) {
                __builtin_memset(in_read_buf_ + (read * AudioIn::CHANNELS),
                                 0,
                                 (frames_per_packet - read) * AudioIn::CHANNELS * sizeof(SampleT));
                read = frames_per_packet;
            }

            // Apply mute/volume
            if (fu_in_mute_) {
                __builtin_memset(in_read_buf_, 0, read * AudioIn::CHANNELS * sizeof(SampleT));
            } else if (fu_in_volume_ < 0) {
                // Volume attenuation (same algorithm as OUT)
                int32_t neg_vol = -static_cast<int32_t>(fu_in_volume_);
                int32_t db_256 = (neg_vol * 48) / 127;
                if (db_256 < 12288) {
                    int32_t shift = db_256 / 1541;
                    int32_t frac = db_256 % 1541;
                    int32_t gain = 32768 >> shift;
                    gain = gain - ((gain * frac) / 3082);
                    if (gain < 1) {
                        gain = 1;
                    }
                    for (uint32_t i = 0; i < read * AudioIn::CHANNELS; ++i) {
                        int64_t sample = static_cast<int64_t>(in_read_buf_[i]);
                        sample = (sample * gain) >> 15;
                        if constexpr (std::is_same_v<SampleT, int16_t>) {
                            if (sample > 32767)
                                sample = 32767;
                            if (sample < -32768)
                                sample = -32768;
                            in_read_buf_[i] = static_cast<int16_t>(sample);
                        } else {
                            in_read_buf_[i] = static_cast<SampleT>(clamp_i24(static_cast<int32_t>(sample)));
                        }
                    }
                } else {
                    __builtin_memset(in_read_buf_, 0, read * AudioIn::CHANNELS * sizeof(SampleT));
                }
            }

            uint32_t sample_count = read * AudioIn::CHANNELS;
            uint8_t* out = in_packet_buf_;
            if (current_in_alt_.bit_depth == 16) {
                for (uint32_t i = 0; i < sample_count; ++i) {
                    int16_t s = 0;
                    if constexpr (std::is_same_v<SampleT, int16_t>) {
                        s = in_read_buf_[i];
                    } else {
                        s = sample_to_i16(in_read_buf_[i]);
                    }
                    out[0] = static_cast<uint8_t>(s & 0xFF);
                    out[1] = static_cast<uint8_t>((s >> 8) & 0xFF);
                    out += 2;
                }
            } else if (current_in_alt_.bit_depth == 24) {
                for (uint32_t i = 0; i < sample_count; ++i) {
                    int32_t v = 0;
                    if constexpr (std::is_same_v<SampleT, int16_t>) {
                        v = sample_from_i16(in_read_buf_[i]);
                    } else {
                        v = in_read_buf_[i];
                    }
                    sample_to_i24(v, out);
                    out += 3;
                }
            } else {
                return;
            }

            hal.ep_write(EP_AUDIO_IN, in_packet_buf_, read * current_in_alt_.bytes_per_frame);
        }
    }

    // ========================================================================
    // MIDI API
    // ========================================================================

    template <typename HalT>
    void send_midi(HalT& hal, uint8_t cable, uint8_t status, uint8_t data1 = 0, uint8_t data2 = 0) {
        if constexpr (!HAS_MIDI_IN) {
            (void)hal;
            (void)cable;
            (void)status;
            (void)data1;
            (void)data2;
            return;
        } else {
            uint8_t cin = MidiProcessor::status_to_cin(status);
            std::array<uint8_t, 4> packet = {static_cast<uint8_t>((cable << 4) | cin), status, data1, data2};
            hal.ep_write(EP_MIDI_IN, packet.data(), 4);
        }
    }

    template <typename HalT>
    void send_note_on(HalT& hal, uint8_t ch, uint8_t note, uint8_t vel, uint8_t cable = 0) {
        send_midi(hal, cable, 0x90 | (ch & 0x0F), note & 0x7F, vel & 0x7F);
    }

    template <typename HalT>
    void send_note_off(HalT& hal, uint8_t ch, uint8_t note, uint8_t vel = 0, uint8_t cable = 0) {
        send_midi(hal, cable, 0x80 | (ch & 0x0F), note & 0x7F, vel & 0x7F);
    }

    template <typename HalT>
    void send_cc(HalT& hal, uint8_t ch, uint8_t cc, uint8_t val, uint8_t cable = 0) {
        send_midi(hal, cable, 0xB0 | (ch & 0x0F), cc & 0x7F, val & 0x7F);
    }

    /// Send SysEx message (with F0 and F7 framing)
    /// @param hal USB HAL instance
    /// @param data SysEx data (including F0 at start and F7 at end)
    /// @param len Length of data
    /// @param cable MIDI cable number (0-15)
    ///
    /// USB MIDI SysEx uses Code Index Numbers (CIN):
    /// - 0x04: SysEx start/continue (3 bytes)
    /// - 0x05: SysEx ends with 1 byte (single-byte system common)
    /// - 0x06: SysEx ends with 2 bytes
    /// - 0x07: SysEx ends with 3 bytes
    template <typename HalT>
    void send_sysex(HalT& hal, const uint8_t* data, uint16_t len, uint8_t cable = 0) {
        if constexpr (!HAS_MIDI_IN) {
            (void)hal;
            (void)data;
            (void)len;
            (void)cable;
            return;
        } else {
            if (len == 0) return;

            uint16_t pos = 0;
            std::array<uint8_t, 4> packet{};

            // Send complete 3-byte packets (CIN = 0x04)
            while (pos + 3 <= len && (pos + 3 < len || data[len - 1] != 0xF7)) {
                // Still have 3+ bytes and not the final packet
                packet[0] = static_cast<uint8_t>((cable << 4) | 0x04);  // CIN = SysEx start/continue
                packet[1] = data[pos];
                packet[2] = data[pos + 1];
                packet[3] = data[pos + 2];
                hal.ep_write(EP_MIDI_IN, packet.data(), 4);
                pos += 3;
            }

            // Send final packet based on remaining bytes
            uint16_t remaining = len - pos;
            if (remaining > 0) {
                if (remaining == 1) {
                    // CIN = 0x05: SysEx ends with 1 byte
                    packet[0] = static_cast<uint8_t>((cable << 4) | 0x05);
                    packet[1] = data[pos];
                    packet[2] = 0;
                    packet[3] = 0;
                } else if (remaining == 2) {
                    // CIN = 0x06: SysEx ends with 2 bytes
                    packet[0] = static_cast<uint8_t>((cable << 4) | 0x06);
                    packet[1] = data[pos];
                    packet[2] = data[pos + 1];
                    packet[3] = 0;
                } else {  // remaining == 3
                    // CIN = 0x07: SysEx ends with 3 bytes
                    packet[0] = static_cast<uint8_t>((cable << 4) | 0x07);
                    packet[1] = data[pos];
                    packet[2] = data[pos + 1];
                    packet[3] = data[pos + 2];
                }
                hal.ep_write(EP_MIDI_IN, packet.data(), 4);
            }
        }
    }

    // ========================================================================
    // Status
    // ========================================================================

    [[nodiscard]] bool is_streaming() const { return audio_out_streaming_; }
    [[nodiscard]] bool is_audio_in_streaming() const { return audio_in_streaming_; }
    [[nodiscard]] bool is_midi_configured() const { return midi_configured_; }

    [[nodiscard]] bool is_playback_started() const {
        if constexpr (HAS_AUDIO_OUT)
            return out_ring_buffer_.is_playback_started();
        return false;
    }
    [[nodiscard]] uint32_t buffered_frames() const {
        if constexpr (HAS_AUDIO_OUT)
            return out_ring_buffer_.buffered_frames();
        return 0;
    }
    [[nodiscard]] uint32_t underrun_count() const {
        if constexpr (HAS_AUDIO_OUT)
            return out_ring_buffer_.underrun_count();
        return 0;
    }
    [[nodiscard]] uint32_t overrun_count() const {
        if constexpr (HAS_AUDIO_OUT)
            return out_ring_buffer_.overrun_count();
        return 0;
    }
    [[nodiscard]] uint32_t current_feedback() const {
        if constexpr (HAS_AUDIO_OUT)
            return feedback_calc_.get_feedback();
        return 0;
    }
    [[nodiscard]] float feedback_rate() const {
        if constexpr (HAS_AUDIO_OUT)
            return feedback_calc_.get_feedback_rate();
        return 0.0f;
    }
    [[nodiscard]] int32_t pll_adjustment_ppm() const { return pll_controller_.current_ppm(); }

    [[nodiscard]] uint32_t current_asrc_rate() const {
        if constexpr (HAS_AUDIO_OUT) {
            return AudioRingBuffer<OUT_BUFFER_FRAMES, AudioOut::CHANNELS, SampleT>::ppm_to_rate_q16(
                pll_controller_.current_ppm());
        }
        return 0x10000;
    }

    [[nodiscard]] bool is_muted() const { return fu_out_mute_; }
    [[nodiscard]] int16_t volume_db256() const { return fu_out_volume_; }

    [[nodiscard]] static constexpr AudioSyncMode sync_mode() { return SYNC_MODE; }

    // Debug: get raw sample from ring buffer at index 0
    [[nodiscard]] SampleT dbg_ring_sample0() const {
        if constexpr (HAS_AUDIO_OUT)
            return out_ring_buffer_.dbg_sample_at(0);
        return SampleT{};
    }
    [[nodiscard]] SampleT dbg_ring_sample1() const {
        if constexpr (HAS_AUDIO_OUT)
            return out_ring_buffer_.dbg_sample_at(1);
        return SampleT{};
    }

    // Audio IN debug accessors
    [[nodiscard]] bool is_audio_in_pending() const { return audio_in_pending_; }
    [[nodiscard]] bool is_in_muted() const { return fu_in_mute_; }
    [[nodiscard]] int16_t in_volume_db256() const { return fu_in_volume_; }
    [[nodiscard]] uint32_t in_buffered_frames() const {
        if constexpr (HAS_AUDIO_IN)
            return in_ring_buffer_.buffered_frames();
        return 0;
    }
    [[nodiscard]] bool is_in_playback_started() const {
        if constexpr (HAS_AUDIO_IN)
            return in_ring_buffer_.is_playback_started();
        return false;
    }

    // Debug counters for Audio IN diagnostics
    mutable uint32_t dbg_send_audio_in_count_ = 0;   // send_audio_in() calls
    mutable uint32_t dbg_sof_count_ = 0;             // SOF interrupts (with HAS_AUDIO_IN)
    mutable uint32_t dbg_sof_streaming_count_ = 0;   // SOF with audio_in_streaming_ true
    mutable uint32_t dbg_in_buffered_ = 0;           // Last seen buffered frames
    mutable uint32_t dbg_set_interface_count_ = 0;   // set_interface() calls
    mutable uint32_t dbg_fb_sent_count_ = 0;         // Feedback packets sent
    mutable uint8_t dbg_last_set_iface_ = 0;         // Last interface number
    mutable uint8_t dbg_last_set_alt_ = 0;           // Last alt setting
    mutable uint32_t dbg_force_streaming_count_ = 0; // Force streaming attempts
    mutable uint32_t dbg_out_rx_last_len_ = 0;       // Last Audio OUT packet size (bytes)
    mutable uint32_t dbg_out_rx_min_len_ = 0;        // Min Audio OUT packet size (bytes)
    mutable uint32_t dbg_out_rx_max_len_ = 0;        // Max Audio OUT packet size (bytes)
    mutable uint32_t dbg_out_rx_short_count_ = 0;    // Count of packets < expected size
    volatile bool dbg_fb_override_enable_ = false;
    volatile uint32_t dbg_fb_override_value_ = 0;

    struct OutPacketStats {
        uint32_t packet_index;
        uint32_t len;
        int32_t max_abs;
        uint32_t raw0;
        uint32_t raw1;
        int32_t cur_l;
        int32_t cur_r;
        int32_t prev_l;
        int32_t prev_r;
        int32_t dl;
        int32_t dr;
    };

    // Debug getter for audio out interface number
    uint8_t audio_out_interface_num() const {
        if constexpr (HAS_AUDIO_OUT) {
            return audio_out_iface_num_;
        }
        return 0xFF;
    }

    // Debug getter for feedback send count
    uint32_t dbg_fb_sent_count() const { return dbg_fb_sent_count_; }

    // Debug getters for last set_interface values
    uint8_t dbg_last_set_iface() const { return dbg_last_set_iface_; }
    uint8_t dbg_last_set_alt() const { return dbg_last_set_alt_; }
    uint32_t dbg_out_rx_last_len() const { return dbg_out_rx_last_len_; }
    uint32_t dbg_out_rx_min_len() const { return dbg_out_rx_min_len_; }
    uint32_t dbg_out_rx_max_len() const { return dbg_out_rx_max_len_; }
    uint32_t dbg_out_rx_short_count() const { return dbg_out_rx_short_count_; }
    uint32_t dbg_out_rx_disc_count() const { return dbg_out_rx_disc_count_; }
    uint32_t dbg_out_rx_disc_max() const { return dbg_out_rx_disc_max_; }
    int32_t dbg_out_rx_disc_last() const { return dbg_out_rx_disc_last_; }
    uint32_t dbg_out_rx_disc_threshold() const { return dbg_out_rx_disc_threshold_; }
    void set_dbg_out_rx_disc_threshold(uint32_t threshold) { dbg_out_rx_disc_threshold_ = threshold; }
    uint32_t dbg_out_rx_intra_count() const { return dbg_out_rx_intra_count_; }
    uint32_t dbg_out_rx_intra_max() const { return dbg_out_rx_intra_max_; }
    int32_t dbg_out_rx_intra_last() const { return dbg_out_rx_intra_last_; }
    uint32_t dbg_out_rx_intra_log_threshold() const { return dbg_out_rx_intra_log_threshold_; }
    void set_dbg_out_rx_intra_log_threshold(uint32_t threshold) { dbg_out_rx_intra_log_threshold_ = threshold; }
    void set_dbg_out_rx_enabled(bool enabled) {
        dbg_out_rx_enabled_ = enabled;
        if (!enabled) {
            dbg_out_rx_has_last_sample_ = false;
        }
    }
    uint32_t dbg_out_rx_packet_index() const { return dbg_out_rx_packet_index_; }
    uint32_t dbg_out_rx_packet_max_abs() const { return dbg_out_rx_packet_max_abs_; }
    int32_t dbg_out_rx_packet_cur_l() const { return dbg_out_rx_packet_cur_l_; }
    int32_t dbg_out_rx_packet_cur_r() const { return dbg_out_rx_packet_cur_r_; }
    int32_t dbg_out_rx_packet_prev_l() const { return dbg_out_rx_packet_prev_l_; }
    int32_t dbg_out_rx_packet_prev_r() const { return dbg_out_rx_packet_prev_r_; }
    int32_t dbg_out_rx_packet_dl() const { return dbg_out_rx_packet_dl_; }
    int32_t dbg_out_rx_packet_dr() const { return dbg_out_rx_packet_dr_; }
    uint32_t dbg_out_rx_packet_raw0() const { return dbg_out_rx_packet_raw0_; }
    uint32_t dbg_out_rx_packet_raw1() const { return dbg_out_rx_packet_raw1_; }
    void set_audio_out_packet_callback(AudioOutPacketCallback cb) { on_audio_out_packet_ = cb; }
    uint32_t dbg_out_rx_intra_event_count() const { return dbg_out_rx_intra_event_count_; }
    uint32_t dbg_out_rx_intra_event_len() const { return dbg_out_rx_intra_event_len_; }
    int32_t dbg_out_rx_intra_event_cur_l() const { return dbg_out_rx_intra_event_cur_l_; }
    int32_t dbg_out_rx_intra_event_cur_r() const { return dbg_out_rx_intra_event_cur_r_; }
    int32_t dbg_out_rx_intra_event_prev_l() const { return dbg_out_rx_intra_event_prev_l_; }
    int32_t dbg_out_rx_intra_event_prev_r() const { return dbg_out_rx_intra_event_prev_r_; }
    int32_t dbg_out_rx_intra_event_dl() const { return dbg_out_rx_intra_event_dl_; }
    int32_t dbg_out_rx_intra_event_dr() const { return dbg_out_rx_intra_event_dr_; }
    void set_feedback_override(bool enable, uint32_t value) {
        dbg_fb_override_enable_ = enable;
        dbg_fb_override_value_ = value;
    }

    void set_feedback_mode(FeedbackMode mode) {
        if constexpr (HAS_AUDIO_OUT) {
            feedback_calc_.set_feedback_mode(mode);
        }
    }

    void set_feedback_delta_gain(uint32_t gain_q16) {
        if constexpr (HAS_AUDIO_OUT) {
            feedback_calc_.set_delta_gain_q16(gain_q16);
        }
    }

    void set_feedback_delta_gain_auto() {
        if constexpr (HAS_AUDIO_OUT) {
            feedback_calc_.set_delta_gain_auto();
        }
    }

    /// Set actual hardware sample rate (for USB feedback calculation)
    /// Call this after reconfiguring I2S/codec for a new sample rate
    void set_actual_rate(uint32_t rate) {
        actual_sample_rate_ = rate; // Save for feedback calculation only
        if constexpr (HAS_AUDIO_OUT) {
            // set_actual_rate recalculates nominal_feedback_, min/max, and rate_const
            feedback_calc_.set_actual_rate(rate);
        }
    }

    /// Temporarily block Audio OUT RX (drop packets) for re-sync.
    void block_audio_out_rx(uint16_t frames) noexcept {
        if constexpr (HAS_AUDIO_OUT) {
            out_rx_blocked_frames_ = frames;
            out_primed_ = false;
        }
    }

    void configure_out_sync_windows(uint32_t rate) noexcept {
        if constexpr (HAS_AUDIO_OUT) {
            if (rate >= 96000) {
                out_prime_frames_ = out_ring_buffer_.capacity() / 2;
                out_rx_blocked_frames_ = kBlockFramesHighRate;
            } else {
                out_prime_frames_ = out_ring_buffer_.capacity() / 4;
                out_rx_blocked_frames_ = kBlockFramesLowRate;
            }
        }
    }

    // Debug accessors for sample rate change diagnostics
    uint32_t dbg_sr_get_cur() const { return dbg_sr_get_cur_count_; }
    uint32_t dbg_sr_set_cur() const { return dbg_sr_set_cur_count_; }
    uint32_t dbg_sr_ep0_rx() const { return dbg_sr_ep0_rx_count_; }
    uint32_t dbg_sr_last_req() const { return dbg_sr_last_requested_; }
    uint32_t dbg_sr_ep_req() const { return dbg_sr_ep_request_count_; }
};

// ============================================================================
// Convenience Type Aliases (backward compatible)
// ============================================================================

// Audio OUT only
using AudioInterface48kAsync = AudioInterface<UacVersion::Uac1, AudioStereo48k, NoAudioPort, NoMidiPort, NoMidiPort, 2>;
using AudioInterface44kAsync = AudioInterface<UacVersion::Uac1, AudioStereo44k, NoAudioPort, NoMidiPort, NoMidiPort, 2>;
using AudioInterface48kAsyncV2 =
    AudioInterface<UacVersion::Uac2, AudioStereo48k, NoAudioPort, NoMidiPort, NoMidiPort, 2>;
using AudioInterface96kAsyncV2 =
    AudioInterface<UacVersion::Uac2, AudioStereo96k, NoAudioPort, NoMidiPort, NoMidiPort, 2>;

// Audio OUT + MIDI
using AudioMidiInterface48k =
    AudioInterface<UacVersion::Uac1, AudioStereo48k, NoAudioPort, MidiPort<1, 3>, MidiPort<1, 3>, 2>;
using AudioMidiInterface48kV2 =
    AudioInterface<UacVersion::Uac2, AudioStereo48k, NoAudioPort, MidiPort<1, 3>, MidiPort<1, 3>, 2>;

// MIDI only
using MidiInterface = AudioInterface<UacVersion::Uac1, NoAudioPort, NoAudioPort, MidiPort<1, 1>, MidiPort<1, 1>, 0>;
using MidiInterfaceV2 = AudioInterface<UacVersion::Uac2, NoAudioPort, NoAudioPort, MidiPort<1, 1>, MidiPort<1, 1>, 0>;

// Audio IN/OUT (full duplex)
// EP1=Audio OUT, EP2=Feedback, EP3=Audio IN
using AudioFullDuplex48k =
    AudioInterface<UacVersion::Uac1, AudioStereo48k, AudioPort<2, 16, 48000, 3>, NoMidiPort, NoMidiPort, 2>;
using AudioFullDuplex48kV2 =
    AudioInterface<UacVersion::Uac2, AudioStereo48k, AudioPort<2, 16, 48000, 3>, NoMidiPort, NoMidiPort, 2>;

// Audio IN/OUT + MIDI (full duplex with MIDI)
// STM32 OTG FS has EP0-3, with IN and OUT being independent:
// EP1 OUT=Audio OUT, EP1 IN=MIDI IN, EP2 IN=Feedback, EP3 OUT=MIDI OUT, EP3 IN=Audio IN
using AudioFullDuplexMidi48k =
    AudioInterface<UacVersion::Uac1, AudioStereo48k, AudioPort<2, 16, 48000, 3>, MidiPort<1, 3>, MidiPort<1, 1>, 2>;

// UAC1 Alt settings: expose 16-bit and 24-bit with full rate list
using AudioAlt16_All = AudioAltSetting<16, AudioRates<44100, 48000, 96000>>;
using AudioAlt24_All = AudioAltSetting<24, AudioRates<44100, 48000, 96000>>;
using AudioAltList24_16 = AudioAltList<AudioAlt16_All, AudioAlt24_All>;
// Audio IN alt list for FS full-duplex: 24-bit limited to 44.1/48 to fit TX FIFO
using AudioAlt24_Lo = AudioAltSetting<24, AudioRates<44100, 48000>>;
using AudioAltList24Lo_16All = AudioAltList<AudioAlt16_All, AudioAlt24_Lo>;

// Full duplex + MIDI with 96kHz max packet size support
// Audio OUT: 16/24-bit with 44.1/48/96k. Audio IN: 16-bit 96k, 24-bit 44.1/48k only.
using AudioFullDuplexMidi96kMaxAsync =
    AudioInterface<UacVersion::Uac1,
                   AudioPort<2, 24, 48000, 1, 96000, AudioRates<44100, 48000, 96000>, AudioAltList24_16>,
                   AudioPort<2, 24, 48000, 3, 96000, AudioRates<44100, 48000, 96000>, AudioAltList24Lo_16All>,
                   MidiPort<1, 3>, // MIDI OUT on EP3
                   MidiPort<1, 1>, // MIDI IN on EP1
                   2,
                   AudioSyncMode::Async>;

using AudioFullDuplexMidi96kMaxAsyncFixedEps =
    AudioInterface<UacVersion::Uac1,
                   AudioPort<2, 24, 48000, 1, 96000, AudioRates<44100, 48000, 96000>, AudioAltList24_16>,
                   AudioPort<2, 24, 48000, 3, 96000, AudioRates<44100, 48000, 96000>, AudioAltList24Lo_16All>,
                   MidiPort<1, 2>, // MIDI OUT on EP2 (OUT)
                   MidiPort<1, 1>, // MIDI IN on EP1 (IN)
                   2,
                   AudioSyncMode::Async>;

using AudioFullDuplexMidi96kMaxAdaptive =
    AudioInterface<UacVersion::Uac1,
                   AudioPort<2, 24, 48000, 1, 96000, AudioRates<44100, 48000, 96000>, AudioAltList24_16>,
                   AudioPort<2, 24, 48000, 3, 96000, AudioRates<44100, 48000, 96000>, AudioAltList24Lo_16All>,
                   MidiPort<1, 3>,
                   MidiPort<1, 1>,
                   2,
                   AudioSyncMode::Adaptive>;

using AudioFullDuplexMidi96kMaxSync =
    AudioInterface<UacVersion::Uac1,
                   AudioPort<2, 24, 48000, 1, 96000, AudioRates<44100, 48000, 96000>, AudioAltList24_16>,
                   AudioPort<2, 24, 48000, 3, 96000, AudioRates<44100, 48000, 96000>, AudioAltList24Lo_16All>,
                   MidiPort<1, 3>,
                   MidiPort<1, 1>,
                   2,
                   AudioSyncMode::Sync>;

// Audio OUT + MIDI with 96kHz max packet size support (Audio IN disabled)
using AudioOutMidi96kMaxAdaptive =
    AudioInterface<UacVersion::Uac1,
                   AudioPort<2, 24, 48000, 1, 96000, AudioRates<44100, 48000, 96000>, AudioAltList24_16>,
                   NoAudioPort,
                   MidiPort<1, 3>,
                   MidiPort<1, 1>,
                   2,
                   AudioSyncMode::Adaptive>;

using AudioOutMidi96kMaxAsync =
    AudioInterface<UacVersion::Uac1,
                   AudioPort<2, 24, 48000, 1, 96000, AudioRates<44100, 48000, 96000>, AudioAltList24_16>,
                   NoAudioPort,
                   MidiPort<1, 3>,
                   MidiPort<1, 1>,
                   2,
                   AudioSyncMode::Async>;

// Audio OUT only with 96kHz max packet size support (no MIDI)
using AudioOut96kMaxAsync =
    AudioInterface<UacVersion::Uac1,
                   AudioPort<2, 24, 48000, 1, 96000, AudioRates<44100, 48000, 96000>, AudioAltList24_16>,
                   NoAudioPort,
                   NoMidiPort,
                   NoMidiPort,
                   2,
                   AudioSyncMode::Async>;

// Audio OUT only with 96kHz max packet size support (no MIDI, adaptive sync)
using AudioOut96kMaxAdaptive =
    AudioInterface<UacVersion::Uac1,
                   AudioPort<2, 24, 48000, 1, 96000, AudioRates<44100, 48000, 96000>, AudioAltList24_16>,
                   NoAudioPort,
                   NoMidiPort,
                   NoMidiPort,
                   2,
                   AudioSyncMode::Adaptive>;

// Audio IN only (microphone)
using AudioInOnly48k =
    AudioInterface<UacVersion::Uac1, NoAudioPort, AudioPort<2, 16, 48000, 1>, NoMidiPort, NoMidiPort, 0>;

} // namespace umiusb

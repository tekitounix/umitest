// SPDX-License-Identifier: MIT
// UMI-USB: High-level Audio Device Configuration (modern, compact)
#pragma once

#include "audio/audio_interface.hh"

namespace umiusb {

// USB speed (used for policy decisions, not for HAL selection)
enum class UsbSpeed : uint8_t {
    FULL_SPEED,
    HIGH_SPEED,
};

// UAC version switching policy
enum class UacModeSwitch : uint8_t {
    FIXED,
    MANUAL_SWITCH,
    AUTO_SWITCH,
};

// Sample rate control policy
enum class SampleRateControl : uint8_t {
    FIXED,
    SELECTABLE,
};

// Stream configuration (Audio IN/OUT)
template<
    uint32_t DefaultRate,
    uint8_t Channels,
    uint8_t BitDepth,
    typename Rates,
    uint8_t Endpoint,
    typename AltSettings = DefaultAltList<BitDepth, Rates>,
    uint32_t ChannelConfig = DefaultChannelConfig<Channels>::value
>
struct StreamConfig {
    static constexpr bool enabled = true;
    static constexpr uint32_t default_rate = DefaultRate;
    static constexpr uint8_t channels = Channels;
    static constexpr uint8_t bit_depth = BitDepth;
    static constexpr uint8_t endpoint = Endpoint;
    using alt_settings = AltSettings;
    static constexpr uint32_t channel_config = ChannelConfig;
    using rates = Rates;
};

struct NoStreamConfig {
    static constexpr bool enabled = false;
    static constexpr uint32_t default_rate = 0;
    static constexpr uint8_t channels = 0;
    static constexpr uint8_t bit_depth = 0;
    static constexpr uint8_t endpoint = 0;
    static constexpr uint32_t channel_config = 0;
    using alt_settings = DefaultAltList<0, AudioRates<0>>;
    using rates = AudioRates<0>;
};

struct FeatureUnitConfig {
    static constexpr bool mute_enabled = true;
    static constexpr bool volume_enabled = true;
    static constexpr bool default_mute = false;
    static constexpr int16_t default_db = 0;  // 0dB
    static constexpr int16_t min_db = -127;
    static constexpr int16_t max_db = 0;
    static constexpr int16_t step_db = 1;
};

struct DefaultsConfig {
    static constexpr uint32_t sample_rate = 0;  // 0 = use stream default
    static constexpr AudioSyncMode sync_mode = AudioSyncMode::ADAPTIVE;
};

struct PolicyConfig {
    static constexpr bool allow_96k_fs = false;
    static constexpr bool allow_duplex_high_rate = false;
    static constexpr uint8_t safe_bandwidth_margin = 20;  // percent
};

struct MidiConfig {
    static constexpr bool enabled = false;
    static constexpr uint8_t cables_out = 0;
    static constexpr uint8_t cables_in = 0;
    static constexpr uint8_t ep_out = 0;
    static constexpr uint8_t ep_in = 0;
};

template<uint8_t CablesOut, uint8_t CablesIn, uint8_t EpOut, uint8_t EpIn>
struct MidiConfigT {
    static constexpr bool enabled = true;
    static constexpr uint8_t cables_out = CablesOut;
    static constexpr uint8_t cables_in = CablesIn;
    static constexpr uint8_t ep_out = EpOut;
    static constexpr uint8_t ep_in = EpIn;
};

template<
    UacVersion Version,
    UsbSpeed Speed,
    AudioSyncMode SyncMode,
    typename AudioOutCfg = NoStreamConfig,
    typename AudioInCfg = NoStreamConfig,
    typename MidiCfg = MidiConfig,
    SampleRateControl RateControl = SampleRateControl::SELECTABLE,
    UacModeSwitch ModeSwitch = UacModeSwitch::FIXED,
    typename FeatureCfg = FeatureUnitConfig,
    typename DefaultsCfg = DefaultsConfig,
    typename PolicyCfg = PolicyConfig,
    typename SampleT = int32_t
>
struct AudioDeviceConfig {
    static constexpr UacVersion uac_version = Version;
    static constexpr UsbSpeed usb_speed = Speed;
    static constexpr AudioSyncMode sync_mode = SyncMode;
    static constexpr SampleRateControl sample_rate_control = RateControl;
    static constexpr UacModeSwitch uac_mode_switch = ModeSwitch;

    using audio_out = AudioOutCfg;
    using audio_in = AudioInCfg;
    using midi = MidiCfg;
    using feature = FeatureCfg;
    using defaults = DefaultsCfg;
    using policy = PolicyCfg;
    using sample_t = SampleT;
};

// Presets (examples; extend as needed)
struct Preset {
    using Uac1_48k_FullDuplex_Midi = AudioDeviceConfig<
        UacVersion::UAC1,
        UsbSpeed::FULL_SPEED,
        AudioSyncMode::ADAPTIVE,
        StreamConfig<48000, 2, 16, AudioRates<44100, 48000>, 1>,
        StreamConfig<48000, 2, 16, AudioRates<44100, 48000>, 3>,
        MidiConfig,
        SampleRateControl::SELECTABLE
    >;
};

// Map config to AudioInterface type
template<typename Config>
struct AudioDeviceTraits {
private:
    using OutCfg = typename Config::audio_out;
    using InCfg = typename Config::audio_in;
    using MidiCfg = typename Config::midi;

    using AudioOutPort = std::conditional_t<
        OutCfg::enabled,
        AudioPort<
            OutCfg::channels,
            OutCfg::bit_depth,
            OutCfg::default_rate,
            OutCfg::endpoint,
            OutCfg::rates::max_rate,
            typename OutCfg::rates,
            typename OutCfg::alt_settings,
            OutCfg::channel_config
        >,
        NoAudioPort
    >;

    using AudioInPort = std::conditional_t<
        InCfg::enabled,
        AudioPort<
            InCfg::channels,
            InCfg::bit_depth,
            InCfg::default_rate,
            InCfg::endpoint,
            InCfg::rates::max_rate,
            typename InCfg::rates,
            typename InCfg::alt_settings,
            InCfg::channel_config
        >,
        NoAudioPort
    >;

    using MidiOutPort = std::conditional_t<
        MidiCfg::enabled,
        MidiPort<MidiCfg::cables_out, MidiCfg::ep_out>,
        NoMidiPort
    >;

    using MidiInPort = std::conditional_t<
        MidiCfg::enabled,
        MidiPort<MidiCfg::cables_in, MidiCfg::ep_in>,
        NoMidiPort
    >;

public:
    using Interface = AudioInterface<
        Config::uac_version,
        AudioOutPort,
        AudioInPort,
        MidiOutPort,
        MidiInPort,
        2,
        Config::sync_mode,
        (Config::sample_rate_control == SampleRateControl::SELECTABLE),
        typename Config::sample_t
    >;
};

template<typename Config>
using AudioInterfaceFromConfig = typename AudioDeviceTraits<Config>::Interface;

}  // namespace umiusb

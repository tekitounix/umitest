// SPDX-License-Identifier: MIT
// UMI-USB: AudioBridge Concept — HW-dependent audio operations
#pragma once

#include <concepts>
#include <cstdint>

namespace umiusb {

/// Audio hardware bridge (I2S, SAI, etc.)
/// Application implements this concept and passes to AudioInterface.
/// The library drives the sequence: stop → clear → configure → start.
template<typename T>
concept AudioBridge = requires(T& bridge, uint32_t rate) {
    // Clock/DMA control — library calls these during sample rate change
    { bridge.stop_audio() } -> std::same_as<void>;
    { bridge.configure_clock(rate) } -> std::same_as<uint32_t>;  // returns actual rate
    { bridge.start_audio() } -> std::same_as<void>;
    { bridge.clear_buffers() } -> std::same_as<void>;
};

/// Null bridge for when no HW bridge is needed (e.g., host tests)
struct NullAudioBridge {
    void stop_audio() {}
    uint32_t configure_clock(uint32_t rate) { return rate; }
    void start_audio() {}
    void clear_buffers() {}
};

static_assert(AudioBridge<NullAudioBridge>, "NullAudioBridge must satisfy AudioBridge concept");

}  // namespace umiusb

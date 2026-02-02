#pragma once
#include <concepts>
#include <cstdint>
#include "hal/result.hh"

namespace hal {

/// AudioCodec concept - external audio codec (I2C/SPI controlled)
template <typename T>
concept AudioCodec = requires(T& codec, int vol_db, bool mute_state) {
    // Initialization
    { codec.init() } -> std::convertible_to<bool>;

    // Power control
    { codec.power_on() } -> std::same_as<void>;
    { codec.power_off() } -> std::same_as<void>;

    // Volume control
    { codec.set_volume(vol_db) } -> std::same_as<void>;
    { codec.mute(mute_state) } -> std::same_as<void>;
};

} // namespace hal

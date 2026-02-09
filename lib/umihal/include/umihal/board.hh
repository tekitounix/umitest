// SPDX-License-Identifier: MIT
#pragma once
#include <concepts>
#include <cstdint>

namespace umi::hal {

/// BoardSpec - static board characteristics
template <typename T>
concept BoardSpec = requires {
    { T::system_clock_hz } -> std::convertible_to<std::uint32_t>;
    { T::hse_clock_hz } -> std::convertible_to<std::uint32_t>;
};

/// McuInit - board-level MCU initialization
template <typename T>
concept McuInit = requires(T& init) {
    { init.init_clocks() } -> std::same_as<void>;
    { init.init_gpio() } -> std::same_as<void>;
};

} // namespace umi::hal

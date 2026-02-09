// SPDX-License-Identifier: MIT
#pragma once
#include <concepts>
#include <cstdint>

namespace umi::hal {

/// クロックソース
template <typename T>
concept ClockSource = requires {
    { T::enable() } -> std::same_as<void>;
    { T::is_ready() } -> std::same_as<bool>;
    { T::get_frequency() } -> std::same_as<uint32_t>;
};

/// クロックツリー — ボードが必ず定義
template <typename T>
concept ClockTree = requires {
    { T::init() } -> std::same_as<void>;
    { T::system_clock() } -> std::same_as<uint32_t>;
    { T::ahb_clock() } -> std::same_as<uint32_t>;
    { T::apb1_clock() } -> std::same_as<uint32_t>;
};

} // namespace umi::hal

// SPDX-License-Identifier: MIT
#pragma once

/// @file
/// @brief Timer concept used by measurement and runner APIs.

#include <concepts>

namespace umi::bench {

/// @brief Concept for monotonic timer backends.
/// @tparam T Timer backend type.
template <typename T>
concept TimerLike = requires {
    typename T::Counter;
    { T::enable() } -> std::same_as<void>;
    { T::now() } -> std::same_as<typename T::Counter>;
};

} // namespace umi::bench

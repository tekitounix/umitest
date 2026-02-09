// SPDX-License-Identifier: MIT
#pragma once
#include <concepts>

namespace umi::hal {

/// 出力デバイス — putc による1文字出力を提供
template <typename T>
concept OutputDevice = requires(char c) {
    { T::init() } -> std::same_as<void>;
    { T::putc(c) } -> std::same_as<void>;
};

/// Platform — 全ボードが満たすべき契約
template <typename T>
concept Platform = requires {
    requires OutputDevice<typename T::Output>;
    { T::init() } -> std::same_as<void>;
};

/// PlatformWithTimer — Timer 型を持つ Platform（umibench 等が要求）
template <typename T>
concept PlatformWithTimer = Platform<T> && requires {
    typename T::Timer;
};

} // namespace umi::hal

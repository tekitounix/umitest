// SPDX-License-Identifier: MIT
#pragma once
#include <concepts>

namespace umi::hal {

/// 最小限: 初期化のみ（AK4556 等のパッシブコーデック対応）
template <typename T>
concept CodecBasic = requires(T& c) {
    { c.init() } -> std::convertible_to<bool>;
};

/// 音量制御あり
template <typename T>
concept CodecWithVolume = CodecBasic<T> && requires(T& c, int db) {
    { c.set_volume(db) } -> std::same_as<void>;
};

/// フル機能コーデック（電源管理 + ミュート）
template <typename T>
concept AudioCodec = CodecWithVolume<T> && requires(T& c, bool m) {
    { c.power_on() } -> std::same_as<void>;
    { c.power_off() } -> std::same_as<void>;
    { c.mute(m) } -> std::same_as<void>;
};

} // namespace umi::hal

// SPDX-License-Identifier: MIT
#pragma once
#include <concepts>
#include <cstdint>
#include <umihal/result.hh>

namespace umi::hal {
namespace timer {

// タイマーモード（全マイコン共通）
enum class Mode : std::uint8_t {
    ONE_SHOT,       // ワンショット
    CONTINUOUS      // 連続
};

// クロックソース（多くのマイコンで共通）
enum class ClockSource : std::uint8_t {
    INTERNAL,       // 内部クロック
    EXTERNAL        // 外部クロック
};

// カウント方向（多くのマイコンで共通）
enum class Direction : std::uint8_t {
    UP,             // アップカウント
    DOWN            // ダウンカウント
};

// タイマーコールバック
using Callback = void(*)();

// キャプチャイベント（多くのマイコンで共通）
enum class CaptureEvent : std::uint8_t {
    RISING,         // 立ち上がりエッジ
    FALLING,        // 立ち下がりエッジ
    BOTH            // 両エッジ
};

// キャプチャコールバック
using CaptureCallback = void(*)(std::uint32_t captured_value);

} // namespace timer

// タイマーインターフェース（共通操作＋オプション機能）
template <typename T>
concept Timer = requires(T& timer, std::uint32_t period_us,
                        umi::hal::timer::Mode mode, umi::hal::timer::Callback callback,
                        umi::hal::timer::ClockSource clock_source,
                        umi::hal::timer::Direction direction,
                        umi::hal::timer::CaptureEvent capture_event,
                        umi::hal::timer::CaptureCallback capture_cb) {
    // 基本操作（必須）
    { timer.start() } -> std::same_as<umi::hal::Result<void>>;
    { timer.stop() } -> std::same_as<umi::hal::Result<void>>;
    { timer.reset() } -> std::same_as<umi::hal::Result<void>>;

    // 周期設定（必須）
    { timer.set_period_us(period_us) } -> std::same_as<umi::hal::Result<void>>;

    // モード設定（必須）
    { timer.set_mode(mode) } -> std::same_as<umi::hal::Result<void>>;

    // コールバック設定（必須 - 割り込み使用時）
    { timer.set_callback(callback) } -> std::same_as<umi::hal::Result<void>>;

    // カウンタ操作（必須）
    { timer.get_counter() } -> std::same_as<umi::hal::Result<std::uint32_t>>;
    { timer.set_counter(period_us) } -> std::same_as<umi::hal::Result<void>>;

    // クロック設定（オプション - NOT_SUPPORTEDを返してもよい）
    { timer.set_clock_source(clock_source) } -> std::same_as<umi::hal::Result<void>>;
    { timer.set_prescaler(period_us) } -> std::same_as<umi::hal::Result<void>>;

    // カウント方向（オプション - NOT_SUPPORTEDを返してもよい）
    { timer.set_direction(direction) } -> std::same_as<umi::hal::Result<void>>;

    // キャプチャ機能（オプション - NOT_SUPPORTEDを返してもよい）
    { timer.enable_capture(capture_event, capture_cb) } -> std::same_as<umi::hal::Result<void>>;
    { timer.disable_capture() } -> std::same_as<umi::hal::Result<void>>;
    { timer.get_capture_value() } -> std::same_as<umi::hal::Result<std::uint32_t>>;

    // 状態確認（必須）
    { timer.is_running() } -> std::convertible_to<bool>;
    { timer.get_error() } -> std::same_as<umi::hal::ErrorCode>;
};

// 遅延タイマーインターフェース（ブロッキング遅延用）
template <typename T>
concept DelayTimer = requires(T& timer, std::uint32_t us, std::uint32_t ms) {
    // マイクロ秒単位の遅延（必須）
    { timer.delay_us(us) } -> std::same_as<void>;

    // ミリ秒単位の遅延（必須）
    { timer.delay_ms(ms) } -> std::same_as<void>;
};

// PWMインターフェース（多くのマイコンで共通）
template <typename T>
concept PwmTimer = requires(T& timer, std::uint32_t frequency_hz,
                           std::uint8_t duty_percent,
                           std::uint32_t duty_value) {
    // PWM設定（オプション - NOT_SUPPORTEDを返してもよい）
    { timer.set_pwm_frequency(frequency_hz) } -> std::same_as<umi::hal::Result<void>>;
    { timer.set_duty_cycle_percent(duty_percent) } -> std::same_as<umi::hal::Result<void>>;
    { timer.set_duty_cycle_raw(duty_value) } -> std::same_as<umi::hal::Result<void>>;
    { timer.start_pwm() } -> std::same_as<umi::hal::Result<void>>;
    { timer.stop_pwm() } -> std::same_as<umi::hal::Result<void>>;
};

} // namespace umi::hal

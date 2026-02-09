// SPDX-License-Identifier: MIT
#pragma once
#include <concepts>
#include <cstdint>
#include <umihal/result.hh>

namespace umi::hal {
namespace gpio {

// ピン方向（全マイコン共通）
enum class Direction : std::uint8_t {
    INPUT,          // 入力
    OUTPUT          // 出力
};

// プルアップ/プルダウン設定（ほぼ全マイコン共通）
enum class Pull : std::uint8_t {
    NONE,           // プルなし
    UP,             // プルアップ
    DOWN            // プルダウン
};

// ピン状態
enum class State : std::uint8_t {
    LOW = 0,        // Low
    HIGH = 1        // High
};

// 出力タイプ（多くのマイコンで共通、ない場合は無視）
enum class OutputType : std::uint8_t {
    PUSH_PULL,      // プッシュプル
    OPEN_DRAIN      // オープンドレイン
};

// 割り込みトリガー（多くのマイコンで共通）
enum class Trigger : std::uint8_t {
    NONE,           // 割り込みなし
    RISING,         // 立ち上がりエッジ
    FALLING,        // 立ち下がりエッジ
    BOTH            // 両エッジ
};

// 割り込みコールバック
using InterruptCallback = void(*)();

} // namespace gpio

// GPIOピンインターフェース（共通操作＋オプション機能）
template <typename T>
concept GpioPin = requires(T& pin, umi::hal::gpio::Direction dir, umi::hal::gpio::Pull pull,
                          umi::hal::gpio::State state, umi::hal::gpio::OutputType output_type,
                          umi::hal::gpio::Trigger trigger, umi::hal::gpio::InterruptCallback callback) {
    // 基本設定（必須）
    { pin.set_direction(dir) } -> std::same_as<umi::hal::Result<void>>;
    { pin.set_pull(pull) } -> std::same_as<umi::hal::Result<void>>;

    // 読み書き操作（必須）
    { pin.write(state) } -> std::same_as<umi::hal::Result<void>>;
    { pin.read() } -> std::same_as<umi::hal::Result<umi::hal::gpio::State>>;

    // 拡張設定（オプション - NOT_SUPPORTEDを返してもよい）
    { pin.set_output_type(output_type) } -> std::same_as<umi::hal::Result<void>>;

    // 割り込み設定（オプション - NOT_SUPPORTEDを返してもよい）
    { pin.set_interrupt(trigger, callback) } -> std::same_as<umi::hal::Result<void>>;
    { pin.enable_interrupt() } -> std::same_as<umi::hal::Result<void>>;
    { pin.disable_interrupt() } -> std::same_as<umi::hal::Result<void>>;

    // 便利関数（オプション）
    { pin.toggle() } -> std::same_as<umi::hal::Result<void>>;
};

// GPIOポートインターフェース（ポート幅はテンプレートパラメータ）
template <typename T, typename PortType = std::uint8_t>
concept GpioPort = requires(T& port, PortType value, PortType mask) {
    // ポート全体の操作（必須）
    { port.write(value) } -> std::same_as<umi::hal::Result<void>>;
    { port.read() } -> std::same_as<umi::hal::Result<PortType>>;

    // マスク操作（オプション - NOT_SUPPORTEDを返してもよい）
    { port.write_masked(mask, value) } -> std::same_as<umi::hal::Result<void>>;
    { port.set_bits(mask) } -> std::same_as<umi::hal::Result<void>>;
    { port.clear_bits(mask) } -> std::same_as<umi::hal::Result<void>>;
    { port.toggle_bits(mask) } -> std::same_as<umi::hal::Result<void>>;
};

} // namespace umi::hal

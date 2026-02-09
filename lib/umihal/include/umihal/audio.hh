// SPDX-License-Identifier: MIT
#pragma once
#include <concepts>
#include <cstdint>
#include <functional>
#include <span>
#include <umihal/result.hh>

namespace umi::hal {
namespace audio {

// オーディオサンプルフォーマット
enum class SampleFormat : std::uint8_t {
    INT16,      // 16ビット整数
    INT24,      // 24ビット整数
    INT32,      // 32ビット整数
    FLOAT32     // 32ビット浮動小数点
};

// オーディオストリーム方向
enum class Direction : std::uint8_t {
    OUTPUT,     // 出力（再生）
    INPUT,      // 入力（録音）
    DUPLEX      // 双方向
};

// オーディオデバイス状態
enum class State : std::uint8_t {
    STOPPED,    // 停止中
    RUNNING,    // 動作中
    PAUSED      // 一時停止中
};

// オーディオ設定
struct Config {
    std::uint32_t sample_rate = 48000;          // サンプルレート
    SampleFormat format = SampleFormat::INT16;   // サンプルフォーマット
    std::uint8_t channels = 2;                   // チャンネル数
    std::uint16_t buffer_size = 256;            // バッファサイズ（フレーム数）
    Direction direction = Direction::OUTPUT;      // ストリーム方向
};

// オーディオバッファ
template <typename T>
struct Buffer {
    std::span<T> data;           // サンプルデータ
    std::size_t frame_count;     // フレーム数
    std::uint8_t channels;       // チャンネル数
};

// コールバック結果
enum class CallbackResult : std::uint8_t {
    CONTINUE,   // 処理を継続
    STOP        // 処理を停止
};

// オーディオコールバック
template <typename T>
using Callback = std::function<CallbackResult(Buffer<T>& buffer)>;

// オーディオデバイスインターフェース
template <typename T>
concept AudioDevice = requires(T& device, const umi::hal::audio::Config& config) {
    // 設定と初期化
    { device.configure(config) } -> std::same_as<umi::hal::Result<void>>;
    { device.get_config() } -> std::same_as<umi::hal::audio::Config>;
    { device.is_config_supported(config) } -> std::convertible_to<bool>;

    // 状態管理
    { device.start() } -> std::same_as<umi::hal::Result<void>>;
    { device.stop() } -> std::same_as<umi::hal::Result<void>>;
    { device.pause() } -> std::same_as<umi::hal::Result<void>>;
    { device.resume() } -> std::same_as<umi::hal::Result<void>>;
    { device.get_state() } -> std::same_as<umi::hal::audio::State>;

    // バッファサイズ情報
    { device.get_buffer_size() } -> std::convertible_to<std::size_t>;
    { device.get_available_buffer_sizes() } -> std::convertible_to<std::span<const std::uint16_t>>;

    // レイテンシ情報
    { device.get_latency() } -> std::convertible_to<std::uint32_t>;  // マイクロ秒単位
};

// コールバック対応オーディオデバイス
template <typename T, typename SampleType>
concept CallbackAudioDevice = AudioDevice<T> && requires(T& device, umi::hal::audio::Callback<SampleType> callback) {
    { device.set_callback(callback) } -> std::same_as<umi::hal::Result<void>>;
};

// ブロッキングI/Oオーディオデバイス
template <typename T, typename SampleType>
concept BlockingAudioDevice = AudioDevice<T> && requires(T& device, std::span<SampleType> buffer) {
    { device.write(buffer) } -> std::same_as<umi::hal::Result<std::size_t>>;  // 書き込んだフレーム数
    { device.read(buffer) } -> std::same_as<umi::hal::Result<std::size_t>>;   // 読み込んだフレーム数
};

} // namespace audio
} // namespace umi::hal

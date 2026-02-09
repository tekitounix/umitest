// SPDX-License-Identifier: MIT
#pragma once
#include <concepts>
#include <cstdint>
#include <span>
#include <umihal/result.hh>

namespace umi::hal {

// I2S共通の型定義
namespace i2s {

// I2S標準
enum class Standard : std::uint8_t {
    PHILIPS,        // Philips I2S標準
    MSB_JUSTIFIED,  // 左詰め（MSB）
    LSB_JUSTIFIED,  // 右詰め（LSB）
    PCM_SHORT,      // PCMショートフレーム
    PCM_LONG        // PCMロングフレーム
};

// データフォーマット
enum class DataFormat : std::uint8_t {
    DATA_16BIT,     // 16ビットデータ
    DATA_24BIT,     // 24ビットデータ
    DATA_32BIT      // 32ビットデータ
};

// 動作モード
enum class Mode : std::uint8_t {
    MASTER_TX,      // マスター送信
    MASTER_RX,      // マスター受信
    SLAVE_TX,       // スレーブ送信
    SLAVE_RX        // スレーブ受信
};

// I2S設定構造体
struct Config {
    Standard standard = Standard::PHILIPS;
    DataFormat data_format = DataFormat::DATA_16BIT;
    Mode mode = Mode::MASTER_TX;
    std::uint8_t channels = 2;  // チャンネル数（1=モノラル, 2=ステレオ, etc.）
    std::uint32_t sample_rate = 48000;
    bool mck_output = false;  // マスタークロック出力有効化
};

// 転送完了コールバック（フレーム数、エラーコード）
using TransferCallback = void(*)(std::size_t frames_transferred, ErrorCode error);

} // namespace i2s

// バッファ準備コールバック（ダブルバッファリング用）
using BufferCallback = void(*)(std::span<std::uint16_t> buffer);

// I2Sインターフェース（ハードウェア非依存）
template <typename T>
concept I2sMaster = requires(T& i2s, const i2s::Config& config,
                            std::span<const std::uint16_t> tx_data,
                            std::span<std::uint16_t> rx_data,
                            i2s::TransferCallback callback,
                            BufferCallback buffer_callback) {
    // 初期化と設定
    { i2s.init(config) } -> std::same_as<umi::hal::Result<void>>;
    { i2s.deinit() } -> std::same_as<umi::hal::Result<void>>;

    // 同期I2S操作（ブロッキング）
    { i2s.transmit(tx_data) } -> std::same_as<umi::hal::Result<void>>;
    { i2s.receive(rx_data) } -> std::same_as<umi::hal::Result<void>>;
    { i2s.transmit_receive(tx_data, rx_data) } -> std::same_as<umi::hal::Result<void>>;

    // 非同期I2S操作（ノンブロッキング - DMA/割り込み）
    { i2s.transmit_async(tx_data, callback) } -> std::same_as<umi::hal::Result<void>>;
    { i2s.receive_async(rx_data, callback) } -> std::same_as<umi::hal::Result<void>>;
    { i2s.transmit_receive_async(tx_data, rx_data, callback) } -> std::same_as<umi::hal::Result<void>>;

    // 連続転送（循環バッファ/ダブルバッファリング）
    { i2s.start_continuous_transmit(tx_data, buffer_callback) } -> std::same_as<umi::hal::Result<void>>;
    { i2s.start_continuous_receive(rx_data, buffer_callback) } -> std::same_as<umi::hal::Result<void>>;
    { i2s.stop_continuous() } -> std::same_as<umi::hal::Result<void>>;

    // 状態確認
    { i2s.is_busy() } -> std::convertible_to<bool>;
    { i2s.get_error() } -> std::same_as<umi::hal::ErrorCode>;
    { i2s.abort() } -> std::same_as<umi::hal::Result<void>>;
};

} // namespace umi::hal

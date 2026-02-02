#pragma once
#include <concepts>
#include <cstdint>
#include <span>
#include "hal/result.hh"

namespace hal {

// I2C共通の型定義
namespace i2c {

// アドレスモード
enum class AddressMode : std::uint8_t {
    SEVEN_BIT,
    TEN_BIT
};

// I2C速度モード
enum class Speed : std::uint32_t {
    STANDARD = 100'000,     // 100 kHz
    FAST = 400'000,         // 400 kHz
    FAST_PLUS = 1'000'000,  // 1 MHz
    HIGH_SPEED = 3'400'000  // 3.4 MHz
};

// I2Cトランザクション操作
enum class Operation : std::uint8_t {
    WRITE,
    READ,
    WRITE_READ  // Combined write-then-read without STOP
};

// 転送完了コールバック
using TransferCallback = void(*)(hal::ErrorCode error);

} // namespace i2c

// I2Cマスターインターフェース（ハードウェア非依存）
template <typename T>
concept I2cMaster = requires(T& i2c, std::uint16_t address, 
                             std::span<const std::uint8_t> tx_data,
                             std::span<std::uint8_t> rx_data,
                             hal::i2c::Speed speed,
                             hal::i2c::TransferCallback callback) {
    // 初期化と設定
    { i2c.init(speed) } -> std::same_as<hal::Result<void>>;
    { i2c.deinit() } -> std::same_as<hal::Result<void>>;
    
    // 同期I2C操作（ブロッキング）
    { i2c.write(address, tx_data) } -> std::same_as<hal::Result<void>>;
    { i2c.read(address, rx_data) } -> std::same_as<hal::Result<void>>;
    { i2c.write_read(address, tx_data, rx_data) } -> std::same_as<hal::Result<void>>;
    
    // 非同期I2C操作（ノンブロッキング - オプション）
    { i2c.write_async(address, tx_data, callback) } -> std::same_as<hal::Result<void>>;
    { i2c.read_async(address, rx_data, callback) } -> std::same_as<hal::Result<void>>;
    { i2c.write_read_async(address, tx_data, rx_data, callback) } -> std::same_as<hal::Result<void>>;
    
    // 転送状態確認
    { i2c.is_busy() } -> std::convertible_to<bool>;
    { i2c.abort() } -> std::same_as<hal::Result<void>>;
    
    // バス管理
    { i2c.reset() } -> std::same_as<hal::Result<void>>;
    { i2c.is_device_ready(address) } -> std::same_as<hal::Result<void>>;
};

} // namespace hal
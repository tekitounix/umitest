#pragma once
#include <concepts>
#include <cstdint>
#include <span>
#include "hal/result.hh"

namespace hal {
namespace uart {

// データビット数（ほぼ全マイコン共通）
enum class DataBits : std::uint8_t {
    BITS_7 = 7,
    BITS_8 = 8,
    BITS_9 = 9
};

// パリティ（全マイコン共通）
enum class Parity : std::uint8_t {
    NONE,
    EVEN,
    ODD
};

// ストップビット（全マイコン共通）
enum class StopBits : std::uint8_t {
    BITS_1,
    BITS_1_5,       // 1.5ビット（一部のマイコン）
    BITS_2
};

// フロー制御（多くのマイコンで共通）
enum class FlowControl : std::uint8_t {
    NONE,
    RTS_CTS,        // ハードウェアフロー制御
    XON_XOFF        // ソフトウェアフロー制御
};

// 基本設定（全マイコン共通）
struct Config {
    std::uint32_t baudrate = 115200;
    DataBits data_bits = DataBits::BITS_8;
    Parity parity = Parity::NONE;
    StopBits stop_bits = StopBits::BITS_1;
    FlowControl flow_control = FlowControl::NONE;  // オプション
};

// 送受信コールバック
using TxCallback = void(*)(std::size_t bytes_sent);
using RxCallback = void(*)(std::size_t bytes_received);

} // namespace uart

// UARTインターフェース（共通操作＋オプション機能）
template <typename T>
concept Uart = requires(T& uart, const hal::uart::Config& config,
                       std::uint8_t byte,
                       std::span<const std::uint8_t> tx_data,
                       std::span<std::uint8_t> rx_data,
                       std::uint32_t timeout_ms,
                       uart::TxCallback tx_cb,
                       uart::RxCallback rx_cb) {
    // 初期化（必須）
    { uart.init(config) } -> std::same_as<hal::Result<void>>;
    { uart.deinit() } -> std::same_as<hal::Result<void>>;
    
    // 単一バイト送受信（必須）
    { uart.write_byte(byte) } -> std::same_as<hal::Result<void>>;
    { uart.read_byte() } -> std::same_as<hal::Result<std::uint8_t>>;
    
    // 複数バイト送受信（ブロッキング）
    { uart.write(tx_data) } -> std::same_as<hal::Result<void>>;
    { uart.read(rx_data) } -> std::same_as<hal::Result<std::size_t>>;
    
    // タイムアウト付き操作（オプション）
    { uart.write_with_timeout(tx_data, timeout_ms) } -> std::same_as<hal::Result<std::size_t>>;
    { uart.read_with_timeout(rx_data, timeout_ms) } -> std::same_as<hal::Result<std::size_t>>;
    
    // 非同期操作（オプション - NOT_SUPPORTEDを返してもよい）
    { uart.write_async(tx_data, tx_cb) } -> std::same_as<hal::Result<void>>;
    { uart.read_async(rx_data, rx_cb) } -> std::same_as<hal::Result<void>>;
    
    // 状態確認（必須）
    { uart.is_readable() } -> std::convertible_to<bool>;
    { uart.is_writable() } -> std::convertible_to<bool>;
    
    // バッファ制御（オプション）
    { uart.flush_tx() } -> std::same_as<hal::Result<void>>;
    { uart.flush_rx() } -> std::same_as<hal::Result<void>>;
    
    // エラー状態（オプション）
    { uart.get_error() } -> std::same_as<hal::ErrorCode>;
    { uart.clear_error() } -> std::same_as<hal::Result<void>>;
};

} // namespace hal
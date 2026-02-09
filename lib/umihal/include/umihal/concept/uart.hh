// SPDX-License-Identifier: MIT
#pragma once
#include <concepts>
#include <cstdint>
#include <umihal/result.hh>
#include <umihal/uart.hh>

namespace umi::hal {

/// 最小限の UART Concept — すべての UART 実装が満たす
template <typename T>
concept UartBasic = requires(T& u, const uart::Config& cfg, std::uint8_t byte) {
    { u.init(cfg) } -> std::same_as<Result<void>>;
    { u.write_byte(byte) } -> std::same_as<Result<void>>;
    { u.read_byte() } -> std::same_as<Result<std::uint8_t>>;
};

/// 非同期転送をサポートする UART
template <typename T>
concept UartAsync = UartBasic<T> && requires(T& u, std::span<const std::uint8_t> data) {
    { u.write_async(data) } -> std::same_as<Result<void>>;
};

} // namespace umi::hal

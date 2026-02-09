// SPDX-License-Identifier: MIT
#pragma once
#include <concepts>
#include <cstdint>
#include <span>
#include <umihal/result.hh>

namespace umi::hal {

/// I2C トランスポート — 外部デバイスドライバが要求する
template <typename T>
concept I2cTransport = requires(T& t, uint8_t addr, uint8_t reg,
                                std::span<const uint8_t> tx,
                                std::span<uint8_t> rx) {
    { t.write(addr, reg, tx) } -> std::same_as<Result<void>>;
    { t.read(addr, reg, rx) } -> std::same_as<Result<void>>;
};

/// SPI トランスポート
template <typename T>
concept SpiTransport = requires(T& t,
                                std::span<const uint8_t> tx,
                                std::span<uint8_t> rx) {
    { t.transfer(tx, rx) } -> std::same_as<Result<void>>;
    { t.select() } -> std::same_as<void>;
    { t.deselect() } -> std::same_as<void>;
};

} // namespace umi::hal

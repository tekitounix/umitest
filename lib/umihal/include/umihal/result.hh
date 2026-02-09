// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <expected>

namespace umi::hal {

// 共通エラー型
enum class ErrorCode : std::uint8_t {
    OK = 0,
    INVALID_PARAMETER,
    TIMEOUT,
    BUSY,
    NOT_READY,
    NO_MEMORY,
    HARDWARE_ERROR,
    NOT_SUPPORTED,
    ABORTED,
    // Transport/Bus errors
    NACK,
    BUS_ERROR,
    OVERRUN,
    INVALID_CONFIG,
};

// Result型
template <typename T>
using Result = std::expected<T, ErrorCode>;

} // namespace umi::hal

// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <span>

namespace umi::rt::detail {

/// @brief Output function provided by the board/target layer.
/// @note umirtm requires this symbol to be defined at link time.
extern void write_bytes(std::span<const std::byte> data);

} // namespace umi::rt::detail

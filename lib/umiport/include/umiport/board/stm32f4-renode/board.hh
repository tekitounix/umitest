// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>

namespace umi::board {

/// @brief Board constants for STM32F4 Renode virtual board.
struct Stm32f4Renode {
    static constexpr uint32_t hse_frequency = 25'000'000;
    static constexpr uint32_t system_clock  = 168'000'000;

    struct Pin {
        static constexpr uint32_t console_tx = 9;   // PA9
        static constexpr uint32_t console_rx = 10;  // PA10
    };

    struct Memory {
        static constexpr uint32_t flash_base = 0x08000000;
        static constexpr uint32_t flash_size = 1024 * 1024;
        static constexpr uint32_t ram_base   = 0x20000000;
        static constexpr uint32_t ram_size   = 128 * 1024;
    };
};

} // namespace umi::board

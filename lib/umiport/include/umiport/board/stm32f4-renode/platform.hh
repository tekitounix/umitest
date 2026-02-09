// SPDX-License-Identifier: MIT
#pragma once

/// @file
/// @brief STM32F4 Renode virtual board platform definition.
/// @note UART output via USART1 for Renode simulation.

#include <umihal/concept/platform.hh>
#include <umiport/arm/cortex-m/dwt.hh>
#include <umiport/mcu/stm32f4/uart_output.hh>

namespace umi::port {

/// @brief STM32F4 Renode platform definition.
struct Platform {
    using Output = umi::port::stm32f4::RenodeUartOutput;
    using Timer = umi::port::cortex_m::DwtTimer;

    static void init() { Output::init(); }

    /// @brief Platform name for reports.
    static constexpr const char* name() { return "stm32f4-renode"; }
};

static_assert(umi::hal::Platform<Platform>,
    "Platform must satisfy umi::hal::Platform concept");
static_assert(umi::hal::PlatformWithTimer<Platform>,
    "Platform must satisfy umi::hal::PlatformWithTimer concept");

} // namespace umi::port

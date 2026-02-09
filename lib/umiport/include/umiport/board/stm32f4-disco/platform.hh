// SPDX-License-Identifier: MIT
#pragma once

/// @file
/// @brief STM32F4 Discovery board platform definition.
/// @note RTT output via Monitor ring buffer for J-Link/OpenOCD.

#include <cstddef>
#include <span>
#include <umihal/concept/platform.hh>
#include <umirtm/rtm.hh>

namespace umi::port {

/// @brief STM32F4 Discovery platform definition.
/// @note Output routed through RTT ring buffer (Monitor::write).
struct Platform {
    struct Output {
        static void init() { rtm::init("RT MONITOR"); }

        static void putc(char c) {
            auto byte = std::byte{static_cast<unsigned char>(c)};
            std::ignore = rtm::write(std::span{&byte, 1});
        }
    };

    static void init() { Output::init(); }
};

static_assert(umi::hal::Platform<Platform>,
    "Platform must satisfy umi::hal::Platform concept");

} // namespace umi::port

// SPDX-License-Identifier: MIT
#pragma once

/// @file
/// @brief Host (macOS/Linux) platform definition.
/// @note Output via POSIX write(2) to stdout.

#include <unistd.h>

#include <umihal/concept/platform.hh>

namespace umi::port {

/// @brief Host platform definition for native unit tests and CLI tools.
struct Platform {
    struct Output {
        static void init() {}

        static void putc(char c) { ::write(1, &c, 1); }
    };

    static void init() { Output::init(); }

    /// @brief Platform name for reports.
    static constexpr const char* name() { return "host"; }
};

static_assert(umi::hal::Platform<Platform>, "Platform must satisfy umi::hal::Platform concept");

} // namespace umi::port

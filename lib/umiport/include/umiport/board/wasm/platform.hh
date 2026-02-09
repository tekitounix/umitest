// SPDX-License-Identifier: MIT
#pragma once

/// @file
/// @brief WASM platform definition.
/// @note Output via fwrite to stdout (Emscripten maps to console).

#include <cstdio>
#include <umihal/concept/platform.hh>

namespace umi::port {

/// @brief WASM platform definition for browser/Node.js targets.
struct Platform {
    struct Output {
        static void init() {}

        static void putc(char c) { std::fputc(c, stdout); }
    };

    static void init() { Output::init(); }

    /// @brief Platform name for reports.
    static constexpr const char* name() { return "wasm"; }
};

static_assert(umi::hal::Platform<Platform>, "Platform must satisfy umi::hal::Platform concept");

} // namespace umi::port

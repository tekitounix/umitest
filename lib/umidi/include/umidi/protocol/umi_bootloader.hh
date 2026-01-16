// SPDX-License-Identifier: MIT
// Compatibility layer - forwards to umi_boot
#pragma once
#include <umiboot/bootloader.hh>

// Namespace alias for backward compatibility
namespace umidi::protocol {
    using namespace umiboot;
}

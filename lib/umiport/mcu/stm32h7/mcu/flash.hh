// SPDX-License-Identifier: MIT
// STM32H750 FLASH - mmio register definitions (access control only)
#pragma once

#include <mmio/mmio.hh>

namespace umi::stm32h7 {

// NOLINTBEGIN(readability-identifier-naming)

/// STM32H750 FLASH interface register block (RM0433 Section 4)
/// Base address: 0x5200'2000
struct FLASH : mm::Device<mm::RW, mm::DirectTransportTag> {
    static constexpr mm::Addr base_address = 0x5200'2000;

    /// Access control register
    struct ACR : mm::Register<FLASH, 0x00, 32> {
        struct LATENCY : mm::Field<ACR, 0, 4> {};      // Flash latency (wait states)
        struct WRHIGHFREQ : mm::Field<ACR, 4, 2> {};   // Flash signal delay
    };
};

// Flash latency values
namespace flash_latency {
constexpr std::uint32_t WS0 = 0;
constexpr std::uint32_t WS1 = 1;
constexpr std::uint32_t WS2 = 2;  // 400MHz @ VOS1
constexpr std::uint32_t WS3 = 3;
constexpr std::uint32_t WS4 = 4;  // 480MHz @ VOS0
} // namespace flash_latency

// NOLINTEND(readability-identifier-naming)

} // namespace umi::stm32h7

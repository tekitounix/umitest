// SPDX-License-Identifier: MIT
// UMI-OS Backend: STM32F4 Unique Device ID
//
// Reads the 96-bit factory-programmed unique ID from OTP memory.
// Address: 0x1FFF7A10 (RM0090 §39.1)
#pragma once

#include <array>
#include <cstdint>

namespace umi::backend::stm32f4 {

/// 96-bit unique device ID (factory-programmed, read-only)
struct Uid {
    uint32_t word[3]; // word[0]=UID[31:0], word[1]=UID[63:32], word[2]=UID[95:64]
};

/// Read the 96-bit unique device ID from OTP memory
inline Uid read_uid() {
    static constexpr uintptr_t UID_BASE = 0x1FFF7A10;
    const auto* p = reinterpret_cast<const volatile uint32_t*>(UID_BASE);
    return {p[0], p[1], p[2]};
}

/// Format UID as full hex string (24 characters + null terminator)
/// Output: "XXXXXXXXXXXXXXXXXXXXXXXX" (uppercase hex, MSB first)
inline void uid_to_hex(const Uid& uid, char* out) {
    static constexpr char hex[] = "0123456789ABCDEF";
    // MSB first: word[2], word[1], word[0]
    for (int w = 2; w >= 0; --w) {
        uint32_t v = uid.word[w];
        for (int i = 7; i >= 0; --i) {
            *out++ = hex[(v >> (i * 4)) & 0xF];
        }
    }
    *out = '\0';
}

/// Generate DFU-compatible short serial number (12 characters + null terminator)
/// Uses the same algorithm as STM32 system bootloader DFU mode:
///   3 x 16-bit words derived from UID half-words via addition.
/// This ensures the serial number matches the DFU bootloader's serial.
inline void uid_to_serial(const Uid& uid, char* out) {
    static constexpr char hex[] = "0123456789ABCDEF";

    // Extract 16-bit half-words from 32-bit UID words
    // hw[0..5] maps to: word[0] low, word[0] high, word[1] low, word[1] high, word[2] low, word[2] high
    auto hw = [&](int i) -> uint16_t {
        return static_cast<uint16_t>(uid.word[i / 2] >> ((i & 1) * 16));
    };

    // STM32 DFU algorithm: uid[] indexed as 16-bit half-words
    //   word0 = hw[1] + hw[5]
    //   word1 = hw[0] + hw[4]
    //   word2 = hw[3]
    const std::array<uint16_t, 3> words = {
        static_cast<uint16_t>(hw(1) + hw(5)),
        static_cast<uint16_t>(hw(0) + hw(4)),
        hw(3),
    };

    for (uint16_t v : words) {
        *out++ = hex[(v >> 12) & 0xF];
        *out++ = hex[(v >> 8) & 0xF];
        *out++ = hex[(v >> 4) & 0xF];
        *out++ = hex[v & 0xF];
    }
    *out = '\0';
}

} // namespace umi::backend::stm32f4

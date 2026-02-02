// SPDX-License-Identifier: MIT
// UMI-OS Privilege Control for WASM (no-op)
#pragma once

#include <cstdint>

namespace umi::privilege {

// ============================================================================
// WASM Privilege (No-op)
// ============================================================================
// WASM has no hardware privilege levels. All code runs at the same level.
// Isolation is provided by the WASM sandbox.

inline bool is_privileged() { return true; }
inline bool is_using_psp() { return false; }
inline void drop_privilege() {}
inline void use_psp() {}
inline void set_psp(uint32_t) {}
inline uint32_t get_psp() { return 0; }
inline uint32_t get_msp() { return 0; }

[[noreturn]] inline void enter_user_mode(uint32_t, void (*entry)()) {
    entry();
    while (true) {}
}

}  // namespace umi::privilege

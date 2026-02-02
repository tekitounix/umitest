// SPDX-License-Identifier: MIT
// UMI-OS Memory Protection for WASM (sandbox-based)
#pragma once

#include <cstdint>

namespace umi::mpu {

// ============================================================================
// WASM Protection (No-op)
// ============================================================================
// WASM provides isolation via its sandbox model. There's no hardware MPU,
// and memory protection is enforced by the WASM runtime.

/// MPU Region IDs (stub for API compatibility)
/// These match the Cortex-M implementation but are not used on WASM.
enum class RegionId : uint8_t {
    FLASH = 0,
    KERNEL_RAM = 1,
    SHARED = 2,
    USER_STACK = 3,
    PERIPHERALS = 4,
};

struct RegionConfig {
    uint32_t base;
    uint32_t size;
    uint32_t attr;
};

inline void disable() {}
inline void enable() {}
inline void configure_region(RegionId, const RegionConfig&) {}
inline void disable_region(RegionId) {}
inline bool is_available() { return false; }
inline uint8_t region_count() { return 0; }
inline void init_umi_regions() {}

}  // namespace umi::mpu

// SPDX-License-Identifier: MIT
// UMI-OS Memory Protection for Cortex-M4 (MPU-based)
#pragma once

#include <cstdint>

namespace umi::mpu {

// ============================================================================
// MPU Register Definitions (Cortex-M4)
// ============================================================================

namespace reg {
inline constexpr uint32_t MPU_BASE = 0xE000ED90;

// MPU Type Register
inline volatile uint32_t& TYPE() { return *reinterpret_cast<volatile uint32_t*>(MPU_BASE + 0x00); }
// MPU Control Register
inline volatile uint32_t& CTRL() { return *reinterpret_cast<volatile uint32_t*>(MPU_BASE + 0x04); }
// MPU Region Number Register
inline volatile uint32_t& RNR() { return *reinterpret_cast<volatile uint32_t*>(MPU_BASE + 0x08); }
// MPU Region Base Address Register
inline volatile uint32_t& RBAR() { return *reinterpret_cast<volatile uint32_t*>(MPU_BASE + 0x0C); }
// MPU Region Attribute and Size Register
inline volatile uint32_t& RASR() { return *reinterpret_cast<volatile uint32_t*>(MPU_BASE + 0x10); }
}  // namespace reg

// ============================================================================
// MPU Constants
// ============================================================================

// CTRL bits
inline constexpr uint32_t CTRL_ENABLE = (1U << 0);
inline constexpr uint32_t CTRL_HFNMIENA = (1U << 1);     // Enable during HardFault/NMI
inline constexpr uint32_t CTRL_PRIVDEFENA = (1U << 2);   // Enable default map for privileged

// RASR bits
inline constexpr uint32_t RASR_ENABLE = (1U << 0);
inline constexpr uint32_t RASR_XN = (1U << 28);  // Execute never

// Access permissions (bits 26:24)
inline constexpr uint32_t AP_NO_ACCESS = (0U << 24);
inline constexpr uint32_t AP_PRIV_RW = (1U << 24);
inline constexpr uint32_t AP_PRIV_RW_USER_RO = (2U << 24);
inline constexpr uint32_t AP_FULL_ACCESS = (3U << 24);
inline constexpr uint32_t AP_PRIV_RO = (5U << 24);
inline constexpr uint32_t AP_PRIV_RO_USER_RO = (6U << 24);

// Memory types (TEX, C, B bits)
inline constexpr uint32_t MEM_STRONGLY_ORDERED = (0U << 16);                       // TEX=0, C=0, B=0
inline constexpr uint32_t MEM_DEVICE = (0U << 16) | (1U << 16);                    // TEX=0, C=0, B=1
inline constexpr uint32_t MEM_NORMAL_WT = (0U << 19) | (1U << 17);                 // TEX=0, C=1, B=0
inline constexpr uint32_t MEM_NORMAL_WB = (0U << 19) | (1U << 17) | (1U << 16);    // TEX=0, C=1, B=1
inline constexpr uint32_t MEM_NORMAL_NC = (1U << 19);                              // TEX=1, C=0, B=0

// Size encoding: SIZE = log2(bytes) - 1
// Minimum size is 32 bytes (SIZE=4), max is 4GB (SIZE=31)
inline constexpr uint32_t size_bits(uint32_t bytes) {
    // Find log2, bytes must be power of 2
    uint32_t n = 0;
    while ((1U << n) < bytes) n++;
    return (n - 1) << 1;  // SIZE field is bits 5:1
}

// ============================================================================
// MPU Region Configuration
// ============================================================================

/// MPU Region IDs
/// Note: Cortex-M4 has 8 MPU regions. We use them efficiently:
///   - Shared memory uses 1 region (all SharedRegionIds within one contiguous block)
///   - This allows SharedRegionId (logical) to have many IDs without MPU exhaustion
///   - Regions 5-7 are available for platform-specific needs
enum class RegionId : uint8_t {
    FLASH = 0,       // Code (RO/RW, executable)
    KERNEL_RAM = 1,  // Kernel data (privileged only)
    SHARED = 2,      // All shared memory regions (RW for user, single contiguous block)
    USER_STACK = 3,  // User stack/heap (RW for user)
    PERIPHERALS = 4, // Hardware registers (privileged only)
    // 5-7: Available for platform-specific use
};

struct RegionConfig {
    uint32_t base;   // Base address (must be aligned to size)
    uint32_t size;   // Size in bytes (must be power of 2, min 32)
    uint32_t attr;   // Attributes (AP + MEM + flags)
};

// ============================================================================
// MPU Control Functions
// ============================================================================

/// Disable MPU
inline void disable() {
    reg::CTRL() = 0;
    asm volatile("dsb\n isb" ::: "memory");
}

/// Enable MPU with privileged default map
inline void enable() {
    reg::CTRL() = CTRL_ENABLE | CTRL_PRIVDEFENA;
    asm volatile("dsb\n isb" ::: "memory");
}

/// Configure a single MPU region
inline void configure_region(RegionId region, const RegionConfig& cfg) {
    reg::RNR() = static_cast<uint32_t>(region);
    reg::RBAR() = cfg.base & ~0x1FU;  // Clear low 5 bits
    reg::RASR() = cfg.attr | size_bits(cfg.size) | RASR_ENABLE;
    asm volatile("dsb" ::: "memory");
}

/// Disable a region
inline void disable_region(RegionId region) {
    reg::RNR() = static_cast<uint32_t>(region);
    reg::RASR() = 0;
    asm volatile("dsb" ::: "memory");
}

/// Check if MPU is available
inline bool is_available() {
    return (reg::TYPE() & 0xFF00) != 0;  // DREGION field
}

/// Get number of MPU regions
inline uint8_t region_count() {
    return (reg::TYPE() >> 8) & 0xFF;
}

// ============================================================================
// Default UMI-OS MPU Configuration
// ============================================================================

/// Initialize MPU for UMI-OS
/// Memory map (STM32F4 example):
///   0x08000000: Flash (code, executable)
///   0x20000000: Kernel RAM (privileged only)
///   0x20008000: Shared memory (all SharedRegionIds, user accessible)
///   0x2000C000: User stack/heap
///   0x40000000: Peripherals (privileged only)
///
/// Key design: All shared memory (AUDIO, MIDI, HW_STATE, DISPLAY, APP_*)
/// is placed in one contiguous 16KB block, covered by a single MPU region.
/// This allows 16+ logical SharedRegionIds with only 1 MPU region.
inline void init_umi_regions() {
    disable();

    // Region 0: Flash (1MB at 0x08000000) - All access, executable
    configure_region(RegionId::FLASH, {
        .base = 0x08000000,
        .size = 0x100000,  // 1MB
        .attr = AP_FULL_ACCESS | MEM_NORMAL_WT
    });

    // Region 1: Kernel RAM (32KB at 0x20000000) - Privileged only
    configure_region(RegionId::KERNEL_RAM, {
        .base = 0x20000000,
        .size = 0x8000,  // 32KB
        .attr = AP_PRIV_RW | MEM_NORMAL_WB | RASR_XN
    });

    // Region 2: Shared Memory (16KB at 0x20008000) - User RW
    // Contains all SharedRegionIds in one contiguous block:
    //   0x20008000: Audio (8KB)
    //   0x2000A000: MIDI (2KB)
    //   0x2000A800: HwState (1KB)
    //   0x2000AC00: Display (2KB)
    //   0x2000B400: App regions (~3KB)
    configure_region(RegionId::SHARED, {
        .base = 0x20008000,
        .size = 0x4000,  // 16KB
        .attr = AP_FULL_ACCESS | MEM_NORMAL_WB | RASR_XN
    });

    // Region 3: User Stack/Heap (64KB at 0x2000C000) - User RW
    configure_region(RegionId::USER_STACK, {
        .base = 0x2000C000,
        .size = 0x10000,  // 64KB (up to 0x2001C000)
        .attr = AP_FULL_ACCESS | MEM_NORMAL_WB | RASR_XN
    });

    // Region 4: Peripherals (512MB at 0x40000000) - Privileged only
    configure_region(RegionId::PERIPHERALS, {
        .base = 0x40000000,
        .size = 0x20000000,  // 512MB
        .attr = AP_PRIV_RW | MEM_DEVICE | RASR_XN
    });

    enable();
}

}  // namespace umi::mpu

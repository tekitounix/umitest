// SPDX-License-Identifier: MIT
// UMI-OS MPU Configuration
// Memory Protection Unit setup for application isolation

#pragma once

#include "app_header.hh"
#include "loader.hh"
#include <cstdint>
#include <cstddef>

namespace umi::kernel::mpu {

// ============================================================================
// MPU Region Configuration
// ============================================================================

/// MPU region numbers
enum class Region : uint8_t {
    KERNEL      = 0,  ///< Kernel code/data (privileged only)
    APP_TEXT    = 1,  ///< Application .text (read-only, executable)
    APP_DATA   = 2,  ///< Application .data/.bss (read-write, non-executable)
    APP_STACK  = 3,  ///< Application stack (read-write, non-executable)
    SHARED     = 4,  ///< Shared memory (read-write, non-executable)
    PERIPHERALS = 5,  ///< Peripheral region (device memory)
    // Regions 6-7 available for future use
};

/// MPU region attributes
struct RegionConfig {
    void* base;         ///< Region base address (must be aligned to size)
    uint32_t size;      ///< Region size (must be power of 2, minimum 32 bytes)
    bool readable;      ///< Allow read access
    bool writable;      ///< Allow write access
    bool executable;    ///< Allow execution
    bool privileged_only; ///< Restrict to privileged mode only
    bool device_memory; ///< Device memory (non-cacheable, non-bufferable)
};

// ============================================================================
// ARM Cortex-M MPU Registers
// ============================================================================

#if defined(__ARM_ARCH)

namespace detail {

/// MPU Type Register
inline volatile uint32_t& MPU_TYPE = *reinterpret_cast<volatile uint32_t*>(0xE000ED90);
/// MPU Control Register
inline volatile uint32_t& MPU_CTRL = *reinterpret_cast<volatile uint32_t*>(0xE000ED94);
/// MPU Region Number Register
inline volatile uint32_t& MPU_RNR  = *reinterpret_cast<volatile uint32_t*>(0xE000ED98);
/// MPU Region Base Address Register
inline volatile uint32_t& MPU_RBAR = *reinterpret_cast<volatile uint32_t*>(0xE000ED9C);
/// MPU Region Attribute and Size Register
inline volatile uint32_t& MPU_RASR = *reinterpret_cast<volatile uint32_t*>(0xE000EDA0);

/// MPU_CTRL bits
inline constexpr uint32_t MPU_CTRL_ENABLE     = 1 << 0;  ///< Enable MPU
inline constexpr uint32_t MPU_CTRL_HFNMIENA   = 1 << 1;  ///< Enable MPU during HardFault/NMI
inline constexpr uint32_t MPU_CTRL_PRIVDEFENA = 1 << 2;  ///< Enable default memory map for privileged

/// MPU_RASR access permission field values
inline constexpr uint32_t AP_NO_ACCESS    = 0b000 << 24; ///< No access
inline constexpr uint32_t AP_PRIV_RW      = 0b001 << 24; ///< Privileged RW
inline constexpr uint32_t AP_PRIV_RW_USER_RO = 0b010 << 24; ///< Privileged RW, User RO
inline constexpr uint32_t AP_FULL_ACCESS  = 0b011 << 24; ///< Full access
inline constexpr uint32_t AP_PRIV_RO      = 0b101 << 24; ///< Privileged RO
inline constexpr uint32_t AP_RO           = 0b110 << 24; ///< RO for both

/// MPU_RASR other attributes
inline constexpr uint32_t RASR_XN         = 1 << 28;     ///< Execute Never
inline constexpr uint32_t RASR_ENABLE     = 1 << 0;      ///< Region enable

/// MPU_RASR TEX/S/C/B fields for memory type
inline constexpr uint32_t MEM_STRONGLY_ORDERED = 0b000000 << 16; ///< Strongly ordered
inline constexpr uint32_t MEM_DEVICE           = 0b000001 << 16; ///< Device (shareable)
inline constexpr uint32_t MEM_NORMAL_WT        = 0b000010 << 16; ///< Normal, write-through
inline constexpr uint32_t MEM_NORMAL_WB        = 0b000011 << 16; ///< Normal, write-back

/// Calculate size field value from byte size
/// @param size_bytes Region size in bytes (must be power of 2, >= 32)
/// @return Size field value for MPU_RASR
constexpr uint32_t size_field(uint32_t size_bytes) noexcept {
    // SIZE field = log2(size) - 1
    // Minimum size is 32 bytes (SIZE = 4)
    if (size_bytes < 32) return 4 << 1;
    
    uint32_t n = 0;
    uint32_t s = size_bytes;
    while (s > 1) {
        s >>= 1;
        n++;
    }
    return (n - 1) << 1;
}

} // namespace detail

#endif // __ARM_ARCH

// ============================================================================
// MPU Configuration Functions
// ============================================================================

/// Check if MPU is available
inline bool is_available() noexcept {
#if defined(__ARM_ARCH)
    // Check DREGION field in MPU_TYPE (bits 15:8)
    uint32_t regions = (detail::MPU_TYPE >> 8) & 0xFF;
    return regions > 0;
#else
    return false;
#endif
}

/// Get number of MPU regions available
inline uint32_t region_count() noexcept {
#if defined(__ARM_ARCH)
    return (detail::MPU_TYPE >> 8) & 0xFF;
#else
    return 0;
#endif
}

/// Disable MPU
inline void disable() noexcept {
#if defined(__ARM_ARCH)
    __asm__ volatile("dmb" ::: "memory");
    detail::MPU_CTRL = 0;
    __asm__ volatile("dsb\n isb" ::: "memory");
#endif
}

/// Enable MPU
/// @param enable_default Allow privileged access to default memory map
inline void enable(bool enable_default = true) noexcept {
#if defined(__ARM_ARCH)
    __asm__ volatile("dmb" ::: "memory");
    detail::MPU_CTRL = detail::MPU_CTRL_ENABLE |
                       (enable_default ? detail::MPU_CTRL_PRIVDEFENA : 0);
    __asm__ volatile("dsb\n isb" ::: "memory");
#else
    (void)enable_default;
#endif
}

/// Configure an MPU region
/// @param region Region number
/// @param config Region configuration
inline void configure_region(Region region, const RegionConfig& config) noexcept {
#if defined(__ARM_ARCH)
    using namespace detail;
    
    uint32_t region_num = static_cast<uint32_t>(region);
    
    // Calculate attributes
    uint32_t rasr = RASR_ENABLE;
    
    // Size
    rasr |= size_field(config.size);
    
    // Access permissions
    if (config.privileged_only) {
        if (config.writable) {
            rasr |= AP_PRIV_RW;
        } else {
            rasr |= AP_PRIV_RO;
        }
    } else {
        if (config.writable) {
            rasr |= AP_FULL_ACCESS;
        } else if (config.readable) {
            rasr |= AP_RO;
        } else {
            rasr |= AP_NO_ACCESS;
        }
    }
    
    // Execute Never
    if (!config.executable) {
        rasr |= RASR_XN;
    }
    
    // Memory type
    if (config.device_memory) {
        rasr |= MEM_DEVICE;
    } else {
        rasr |= MEM_NORMAL_WB;
    }
    
    // Disable interrupts during configuration
    __asm__ volatile("cpsid i" ::: "memory");
    
    // Select region
    MPU_RNR = region_num;
    
    // Configure region
    MPU_RBAR = reinterpret_cast<uint32_t>(config.base) & ~0x1F;  // Align to 32 bytes
    MPU_RASR = rasr;
    
    __asm__ volatile("dsb\n isb" ::: "memory");
    __asm__ volatile("cpsie i" ::: "memory");
#else
    (void)region;
    (void)config;
#endif
}

/// Disable an MPU region
inline void disable_region(Region region) noexcept {
#if defined(__ARM_ARCH)
    using namespace detail;
    
    __asm__ volatile("cpsid i" ::: "memory");
    MPU_RNR = static_cast<uint32_t>(region);
    MPU_RASR = 0;  // Disable region
    __asm__ volatile("dsb\n isb" ::: "memory");
    __asm__ volatile("cpsie i" ::: "memory");
#else
    (void)region;
#endif
}

// ============================================================================
// Application MPU Setup
// ============================================================================

/// Configure MPU regions for application isolation
/// @param runtime Application runtime information
/// @param shared Shared memory region
/// @param kernel_base Kernel memory base
/// @param kernel_size Kernel memory size
inline void configure_app_regions(
    const AppRuntime& runtime,
    void* shared_base,
    size_t shared_size,
    void* kernel_base,
    size_t kernel_size
) noexcept {
    // Disable MPU during configuration
    disable();
    
    // Region 0: Kernel (privileged only)
    configure_region(Region::KERNEL, {
        .base = kernel_base,
        .size = static_cast<uint32_t>(kernel_size),
        .readable = true,
        .writable = true,
        .executable = true,
        .privileged_only = true,
        .device_memory = false,
    });
    
    // Region 1: Application .text (read-only, executable)
    configure_region(Region::APP_TEXT, {
        .base = runtime.text_start,
        .size = 32 * 1024,  // TODO: Get from header
        .readable = true,
        .writable = false,
        .executable = true,
        .privileged_only = false,
        .device_memory = false,
    });
    
    // Region 2: Application .data/.bss (read-write, non-executable)
    // AppData = 32KB (0x2000C000–0x20014000)
    configure_region(Region::APP_DATA, {
        .base = runtime.data_start,
        .size = 32 * 1024,
        .readable = true,
        .writable = true,
        .executable = false,
        .privileged_only = false,
        .device_memory = false,
    });

    // Region 3: Application stack+heap (read-write, non-executable)
    // AppStack = 16KB (0x20014000–0x20018000)
    configure_region(Region::APP_STACK, {
        .base = runtime.stack_base,
        .size = 16 * 1024,
        .readable = true,
        .writable = true,
        .executable = false,
        .privileged_only = false,
        .device_memory = false,
    });
    
    // Region 4: Shared memory (read-write, non-executable)
    configure_region(Region::SHARED, {
        .base = shared_base,
        .size = static_cast<uint32_t>(shared_size),
        .readable = true,
        .writable = true,
        .executable = false,
        .privileged_only = false,
        .device_memory = false,
    });
    
    // Region 5: Peripherals (if needed, configure as device memory)
    // This is typically handled by the default memory map
    
    // Enable MPU with default memory map for privileged mode
    enable(true);
}

/// Switch to unprivileged mode (for running application code)
inline void drop_privilege() noexcept {
#if defined(__ARM_ARCH)
    // Set CONTROL.nPRIV bit to switch to unprivileged mode
    // Also use PSP (Process Stack Pointer)
    __asm__ volatile(
        "mrs r0, control\n"
        "orr r0, r0, #3\n"    // Set nPRIV (bit 0) and SPSEL (bit 1)
        "msr control, r0\n"
        "isb\n"
        ::: "r0", "memory"
    );
#endif
}

/// Elevate to privileged mode (only works from exception handler)
inline void elevate_privilege() noexcept {
#if defined(__ARM_ARCH)
    __asm__ volatile(
        "mrs r0, control\n"
        "bic r0, r0, #1\n"    // Clear nPRIV bit
        "msr control, r0\n"
        "isb\n"
        ::: "r0", "memory"
    );
#endif
}

} // namespace umi::kernel::mpu

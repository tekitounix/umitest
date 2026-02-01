// SPDX-License-Identifier: MIT
// UMI-OS Kernel: Memory Protection Abstraction Layer
//
// Provides unified API for MPU-based protection that works on:
// - MCUs with MPU: Full isolation between kernel and app
// - MCUs without MPU: Privileged mode execution
// - Development: Optional MPU for debugging
#pragma once

#include <cstdint>
#include <cstddef>

namespace umi::kernel {

// ============================================================================
// Protection Mode Selection
// ============================================================================

/// Memory protection policy
enum class ProtectionMode : std::uint8_t {
    /// MPU enabled, unprivileged mode execution
    /// Maximum security, some overhead
    /// Use for: Production on MPU-capable MCUs
    FULL,

    /// MPU disabled, privileged mode execution
    /// For MCUs without MPU, or when protection not needed
    /// Use for: MPU-less MCUs, trusted single-app systems
    PRIVILEGED,

    /// MPU enabled, privileged mode execution
    /// MPU faults but no mode switch - useful for debugging
    /// Use for: Development, debugging memory issues
    PRIVILEGED_WITH_MPU,
};

// ============================================================================
// Memory Region Configuration
// ============================================================================

/// Memory region attributes
struct MemoryRegion {
    const void* base {nullptr};
    std::size_t size {0};
    bool readable {true};
    bool writable {false};
    bool executable {false};
    bool cacheable {true};
    bool shareable {false};
};

/// Standard region indices for kernel layout
enum class RegionIndex : std::uint8_t {
    KERNEL = 0,        ///< Kernel code and data (privileged only)
    APP_TEXT = 1,      ///< Application .text (RX, unprivileged)
    APP_DATA = 2,      ///< Application .data/.bss (RW, unprivileged)
    APP_STACK = 3,     ///< Application stack (RW, unprivileged)
    SHARED = 4,        ///< Shared memory (RW, both)
    PERIPHERALS = 5,   ///< Peripheral space (device, privileged only)
    RESERVED1 = 6,
    RESERVED2 = 7,
};

// ============================================================================
// MPU Hardware Interface (Cortex-M)
// ============================================================================

namespace mpu {

/// MPU registers (Cortex-M4/M7)
struct Regs {
    static constexpr std::uint32_t BASE = 0xE000ED90;

    static constexpr std::uint32_t TYPE = 0x00;   ///< MPU Type Register
    static constexpr std::uint32_t CTRL = 0x04;   ///< MPU Control Register
    static constexpr std::uint32_t RNR = 0x08;    ///< Region Number Register
    static constexpr std::uint32_t RBAR = 0x0C;   ///< Region Base Address Register
    static constexpr std::uint32_t RASR = 0x10;   ///< Region Attribute and Size Register

    static volatile std::uint32_t& reg(std::uint32_t offset) {
        return *reinterpret_cast<volatile std::uint32_t*>(BASE + offset);
    }
};

// CTRL register bits
constexpr std::uint32_t CTRL_ENABLE = 1U << 0;
constexpr std::uint32_t CTRL_HFNMIENA = 1U << 1;  // Enable during HardFault/NMI
constexpr std::uint32_t CTRL_PRIVDEFENA = 1U << 2;  // Enable default map for privileged

// RASR register bits/fields
constexpr std::uint32_t RASR_ENABLE = 1U << 0;
constexpr std::uint32_t RASR_XN = 1U << 28;  // Execute never

// Access permission encoding (AP field, bits 26:24)
constexpr std::uint32_t AP_NO_ACCESS = 0U << 24;
constexpr std::uint32_t AP_PRIV_RW = 1U << 24;     // Privileged RW, unprivileged no access
constexpr std::uint32_t AP_PRIV_RW_UNPRIV_RO = 2U << 24;
constexpr std::uint32_t AP_FULL_ACCESS = 3U << 24;  // Both RW
constexpr std::uint32_t AP_PRIV_RO = 5U << 24;     // Privileged RO, unprivileged no access
constexpr std::uint32_t AP_RO = 6U << 24;          // Both RO

// TEX/S/C/B encoding for memory type (simplified)
constexpr std::uint32_t ATTR_NORMAL_CACHED = (1U << 19) | (1U << 17);  // TEX=001, C=1, B=0
constexpr std::uint32_t ATTR_DEVICE = (0U << 19) | (1U << 16);  // TEX=000, C=0, B=1

/// Check if MPU is available
inline bool is_available() {
    return (Regs::reg(Regs::TYPE) & 0xFF00) != 0;
}

/// Get number of MPU regions
inline std::uint8_t region_count() {
    return (Regs::reg(Regs::TYPE) >> 8) & 0xFF;
}

/// Enable MPU
inline void enable(bool enable_in_faults = true, bool enable_default_map = true) {
    std::uint32_t ctrl = CTRL_ENABLE;
    if (enable_in_faults) ctrl |= CTRL_HFNMIENA;
    if (enable_default_map) ctrl |= CTRL_PRIVDEFENA;

    // Memory barriers before enabling
    asm volatile("dsb" ::: "memory");
    asm volatile("isb" ::: "memory");

    Regs::reg(Regs::CTRL) = ctrl;

    // Memory barriers after enabling
    asm volatile("dsb" ::: "memory");
    asm volatile("isb" ::: "memory");
}

/// Disable MPU
inline void disable() {
    asm volatile("dsb" ::: "memory");
    Regs::reg(Regs::CTRL) = 0;
    asm volatile("dsb" ::: "memory");
    asm volatile("isb" ::: "memory");
}

/// Calculate size field from byte size (must be power of 2, >= 32)
inline std::uint8_t size_to_field(std::size_t bytes) {
    if (bytes < 32) bytes = 32;
    // SIZE field = log2(bytes) - 1
    std::uint8_t n = 0;
    while ((1UL << (n + 1)) < bytes) n++;
    return n;
}

/// Configure a single MPU region
inline void configure_region(std::uint8_t region,
                              const void* base,
                              std::size_t size,
                              std::uint32_t access_perm,
                              std::uint32_t attrs,
                              bool executable) {
    Regs::reg(Regs::RNR) = region;

    auto base_addr = reinterpret_cast<std::uint32_t>(base);
    Regs::reg(Regs::RBAR) = (base_addr & 0xFFFFFFE0) | (1U << 4) | region;

    std::uint32_t rasr = RASR_ENABLE
                       | (static_cast<std::uint32_t>(size_to_field(size)) << 1)
                       | access_perm
                       | attrs;
    if (!executable) rasr |= RASR_XN;

    Regs::reg(Regs::RASR) = rasr;
}

/// Disable a single MPU region
inline void disable_region(std::uint8_t region) {
    Regs::reg(Regs::RNR) = region;
    Regs::reg(Regs::RASR) = 0;
}

}  // namespace mpu

// ============================================================================
// Protection Abstraction Template
// ============================================================================

/// Memory protection abstraction
/// @tparam HW Hardware abstraction layer
/// @tparam Mode Protection mode (compile-time selection)
template <class HW, ProtectionMode Mode = ProtectionMode::FULL>
class Protection {
public:
    /// Initialize memory protection
    static void init() noexcept {
        if constexpr (uses_mpu()) {
            if (mpu::is_available()) {
                mpu::disable();  // Start fresh
                // Default configuration done by configure_kernel()
            }
        }
    }

    /// Configure kernel memory region (call during init)
    static void configure_kernel(const void* base, std::size_t size) noexcept {
        if constexpr (uses_mpu()) {
            if (!mpu::is_available()) return;

            mpu::configure_region(
                static_cast<std::uint8_t>(RegionIndex::KERNEL),
                base, size,
                mpu::AP_PRIV_RW,  // Privileged only
                mpu::ATTR_NORMAL_CACHED,
                true  // Executable
            );
        }
    }

    /// Configure peripheral region (device memory, privileged only)
    static void configure_peripherals(const void* base, std::size_t size) noexcept {
        if constexpr (uses_mpu()) {
            if (!mpu::is_available()) return;

            mpu::configure_region(
                static_cast<std::uint8_t>(RegionIndex::PERIPHERALS),
                base, size,
                mpu::AP_PRIV_RW,  // Privileged only
                mpu::ATTR_DEVICE,
                false  // Never execute
            );
        }
    }

    /// Configure application memory regions
    static void configure_app(const void* text_base, std::size_t text_size,
                               const void* data_base, std::size_t data_size,
                               const void* stack_base, std::size_t stack_size) noexcept {
        if constexpr (uses_mpu()) {
            if (!mpu::is_available()) return;

            // App .text (RX)
            mpu::configure_region(
                static_cast<std::uint8_t>(RegionIndex::APP_TEXT),
                text_base, text_size,
                mpu::AP_FULL_ACCESS,  // Both can read
                mpu::ATTR_NORMAL_CACHED,
                true  // Executable
            );

            // App .data/.bss (RW)
            mpu::configure_region(
                static_cast<std::uint8_t>(RegionIndex::APP_DATA),
                data_base, data_size,
                mpu::AP_FULL_ACCESS,  // Both RW
                mpu::ATTR_NORMAL_CACHED,
                false  // Not executable
            );

            // App stack (RW)
            mpu::configure_region(
                static_cast<std::uint8_t>(RegionIndex::APP_STACK),
                stack_base, stack_size,
                mpu::AP_FULL_ACCESS,  // Both RW
                mpu::ATTR_NORMAL_CACHED,
                false  // Not executable
            );
        }
    }

    /// Configure shared memory region
    static void configure_shared(const void* base, std::size_t size) noexcept {
        if constexpr (uses_mpu()) {
            if (!mpu::is_available()) return;

            mpu::configure_region(
                static_cast<std::uint8_t>(RegionIndex::SHARED),
                base, size,
                mpu::AP_FULL_ACCESS,  // Both RW
                mpu::ATTR_NORMAL_CACHED,
                false  // Not executable
            );
        }
    }

    /// Enable protection after configuration
    static void enable() noexcept {
        if constexpr (uses_mpu()) {
            if (mpu::is_available()) {
                mpu::enable(true, true);
            }
        }
    }

    /// Check if syscall is needed for kernel calls
    static constexpr bool needs_syscall() noexcept {
        return Mode == ProtectionMode::FULL;
    }

    /// Check if running in privileged mode
    static constexpr bool is_privileged() noexcept {
        return Mode != ProtectionMode::FULL;
    }

    /// Check if MPU is used in this mode
    static constexpr bool uses_mpu() noexcept {
        return Mode == ProtectionMode::FULL || Mode == ProtectionMode::PRIVILEGED_WITH_MPU;
    }

    /// Get protection mode name for debugging
    static constexpr const char* mode_name() noexcept {
        switch (Mode) {
            case ProtectionMode::FULL: return "Full";
            case ProtectionMode::PRIVILEGED: return "Privileged";
            case ProtectionMode::PRIVILEGED_WITH_MPU: return "PrivilegedWithMpu";
            default: return "Unknown";
        }
    }
};

// ============================================================================
// Compile-time Mode Selection Helper
// ============================================================================

/// Default protection mode based on platform
#if defined(__ARM_ARCH) && defined(UMIOS_USE_MPU)
    inline constexpr ProtectionMode DEFAULT_PROTECTION_MODE = ProtectionMode::FULL;
#elif defined(__ARM_ARCH)
    inline constexpr ProtectionMode DEFAULT_PROTECTION_MODE = ProtectionMode::PRIVILEGED;
#else
    // Non-ARM or simulation
    inline constexpr ProtectionMode DEFAULT_PROTECTION_MODE = ProtectionMode::PRIVILEGED;
#endif

}  // namespace umi::kernel

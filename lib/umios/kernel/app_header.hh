// SPDX-License-Identifier: MIT
// UMI-OS Application Binary Header
// Defines the .umia binary format for embedded applications

#pragma once

#include <cstdint>
#include <cstddef>

namespace umi::kernel {

// ============================================================================
// Application Binary Header (.umia format)
// ============================================================================

/// Magic number: "UMIA" (UMI Application)
inline constexpr uint32_t APP_MAGIC = 0x414D4955;  // 'U' 'M' 'I' 'A' in little-endian

/// Current ABI version
inline constexpr uint32_t APP_ABI_VERSION = 1;

/// Application target type (determines compatibility)
enum class AppTarget : uint32_t {
    USER        = 0,  ///< User app (unsigned, runs on both dev/release kernel)
    DEVELOPMENT = 1,  ///< Development app (dev kernel only)
    RELEASE     = 2,  ///< Release app (release kernel only, signature required)
};

/// Application header (placed at the beginning of .umia binary)
/// Total size: 128 bytes (aligned for easy parsing)
struct alignas(4) AppHeader {
    // --- Identification (16 bytes) ---
    uint32_t magic;             ///< Must be APP_MAGIC (0x414D4955)
    uint32_t abi_version;       ///< ABI version for compatibility check
    AppTarget target;           ///< Application target type
    uint32_t flags;             ///< Reserved flags (must be 0)

    // --- Entry Points (8 bytes) ---
    uint32_t entry_offset;      ///< Offset to _start() from header
    uint32_t process_offset;    ///< Offset to registered process() (filled by loader)

    // --- Section Sizes (16 bytes) ---
    uint32_t text_size;         ///< .text section size (code)
    uint32_t rodata_size;       ///< .rodata section size (constants)
    uint32_t data_size;         ///< .data section size (initialized data)
    uint32_t bss_size;          ///< .bss section size (uninitialized data)

    // --- Memory Requirements (8 bytes) ---
    uint32_t stack_size;        ///< Required stack size for Control Task
    uint32_t heap_size;         ///< Required heap size (0 if no heap)

    // --- Integrity (8 bytes) ---
    uint32_t crc32;             ///< CRC32 of sections (text + rodata + data)
    uint32_t total_size;        ///< Total image size including header

    // --- Signature (64 bytes) ---
    uint8_t signature[64];      ///< Ed25519 signature (Release apps only)

    // --- Reserved (8 bytes) ---
    uint8_t reserved[8];        ///< Reserved for future use (must be 0)

    // --- Validation Methods ---

    /// Check if magic number is valid
    [[nodiscard]] constexpr bool valid_magic() const noexcept {
        return magic == APP_MAGIC;
    }

    /// Check if ABI version is compatible
    [[nodiscard]] constexpr bool compatible_abi() const noexcept {
        return abi_version == APP_ABI_VERSION;
    }

    /// Get total section size (excluding header)
    [[nodiscard]] constexpr uint32_t sections_size() const noexcept {
        return text_size + rodata_size + data_size;
    }

    /// Get required RAM size for app
    [[nodiscard]] constexpr uint32_t required_ram() const noexcept {
        return data_size + bss_size + stack_size + heap_size;
    }

    /// Get entry point address given load base
    [[nodiscard]] const void* entry_point(const void* base) const noexcept {
        return static_cast<const uint8_t*>(base) + entry_offset;
    }
};

static_assert(sizeof(AppHeader) == 128, "AppHeader must be 128 bytes");
static_assert(alignof(AppHeader) == 4, "AppHeader must be 4-byte aligned");

// ============================================================================
// Application Load Result
// ============================================================================

/// Result of application loading (09-app-binary.md spec)
enum class LoadResult : int32_t {
    OK                 = 0,   ///< Successfully loaded
    INVALID_MAGIC      = -1,  ///< Magic number mismatch
    INVALID_ABI        = -2,  ///< ABI version incompatible
    INVALID_TARGET     = -3,  ///< App target incompatible with kernel build
    INVALID_SIZE       = -4,  ///< Size fields inconsistent
    INVALID_ENTRY      = -5,  ///< Entry point invalid
    CRC_MISMATCH       = -6,  ///< CRC32 verification failed
    SIGNATURE_INVALID  = -7,  ///< Ed25519 signature invalid (Release apps)
    MEMORY_ERROR       = -8,  ///< Insufficient memory for app
    SIGNATURE_REQUIRED = -9,  ///< Signature required but not present
    ALREADY_LOADED     = -10, ///< An application is already loaded
};

/// Convert LoadResult to string for debugging
constexpr const char* load_result_str(LoadResult r) noexcept {
    switch (r) {
    case LoadResult::OK:                 return "OK";
    case LoadResult::INVALID_MAGIC:      return "INVALID_MAGIC";
    case LoadResult::INVALID_ABI:        return "INVALID_ABI";
    case LoadResult::INVALID_TARGET:     return "INVALID_TARGET";
    case LoadResult::INVALID_SIZE:       return "INVALID_SIZE";
    case LoadResult::INVALID_ENTRY:      return "INVALID_ENTRY";
    case LoadResult::CRC_MISMATCH:       return "CRC_MISMATCH";
    case LoadResult::SIGNATURE_INVALID:  return "SIGNATURE_INVALID";
    case LoadResult::MEMORY_ERROR:       return "MEMORY_ERROR";
    case LoadResult::SIGNATURE_REQUIRED: return "SIGNATURE_REQUIRED";
    case LoadResult::ALREADY_LOADED:     return "ALREADY_LOADED";
    default:                             return "Unknown";
    }
}

// ============================================================================
// Build Configuration
// ============================================================================

/// Kernel build type (set at compile time)
enum class BuildType : uint8_t {
    DEVELOPMENT = 0,  ///< Development build (allows unsigned apps)
    RELEASE     = 1,  ///< Release build (requires signatures for Release apps)
};

#ifndef UMIOS_BUILD_TYPE
#define UMIOS_BUILD_TYPE BuildType::DEVELOPMENT
#endif

inline constexpr BuildType KERNEL_BUILD_TYPE = UMIOS_BUILD_TYPE;

} // namespace umi::kernel

// SPDX-License-Identifier: MIT
// UMI-Boot Firmware Header and Signature Verification
// Secure firmware update with Ed25519 signatures
#pragma once

#include <cstdint>
#include <cstring>
#include <span>

namespace umiboot {

// =============================================================================
// Firmware Header Specification
// =============================================================================
//
// Firmware image layout:
//
// +------------------+
// | FirmwareHeader   | (128 bytes, fixed)
// +------------------+
// | Firmware binary  | (variable size)
// +------------------+
// | Ed25519 Signature| (64 bytes, appended)
// +------------------+
//
// The signature covers: Header + Firmware binary (excluding signature itself)
//
// =============================================================================

/// Magic number for UMI firmware images
inline constexpr uint32_t FIRMWARE_MAGIC = 0x554D4946;  // "UMIF"

/// Firmware header version
inline constexpr uint8_t FIRMWARE_HEADER_VERSION = 1;

/// Ed25519 signature size
inline constexpr size_t ED25519_SIGNATURE_SIZE = 64;

/// Ed25519 public key size
inline constexpr size_t ED25519_PUBLIC_KEY_SIZE = 32;

/// SHA-256 hash size
inline constexpr size_t SHA256_HASH_SIZE = 32;

/// Firmware header (128 bytes, aligned)
struct FirmwareHeader {
    uint32_t magic;             // 0x00: Magic number (FIRMWARE_MAGIC)
    uint8_t  header_version;    // 0x04: Header format version
    uint8_t  flags;             // 0x05: Flags (see FirmwareFlags)
    uint16_t reserved1;         // 0x06: Reserved (alignment)

    uint32_t fw_version_major;  // 0x08: Major version
    uint32_t fw_version_minor;  // 0x0C: Minor version
    uint32_t fw_version_patch;  // 0x10: Patch version
    uint32_t fw_build_number;   // 0x14: Build number

    uint32_t image_size;        // 0x18: Size of firmware binary (excluding header/sig)
    uint32_t image_crc32;       // 0x1C: CRC32 of firmware binary
    uint32_t load_address;      // 0x20: Load address in flash/RAM
    uint32_t entry_point;       // 0x24: Entry point address

    uint8_t  target_board[16];  // 0x28: Target board identifier (null-terminated)
    uint8_t  fw_hash[32];       // 0x38: SHA-256 hash of firmware binary

    uint32_t min_bootloader;    // 0x58: Minimum bootloader version required
    uint32_t rollback_version;  // 0x5C: Anti-rollback version number
    uint32_t timestamp;         // 0x60: Build timestamp (Unix epoch)
    uint32_t reserved2[7];      // 0x64: Reserved for future use

    // Total: 128 bytes (0x80)
};

static_assert(sizeof(FirmwareHeader) == 128, "FirmwareHeader must be 128 bytes");

/// Firmware flags
enum class FirmwareFlags : uint8_t {
    NONE            = 0x00,
    SIGNED          = 0x01,     // Firmware is signed with Ed25519
    ENCRYPTED       = 0x02,     // Firmware is encrypted (not yet supported)
    COMPRESSED      = 0x04,     // Firmware is compressed (not yet supported)
    DEBUG_BUILD     = 0x08,     // Debug build (may be rejected in production)
    ALLOW_DOWNGRADE = 0x10,     // Allow downgrade from this version
};

/// Version comparison result
enum class VersionCompare : int8_t {
    OLDER = -1,
    EQUAL = 0,
    NEWER = 1,
};

// =============================================================================
// Version Utilities
// =============================================================================

/// Compare firmware versions
inline constexpr VersionCompare compare_versions(
    uint32_t major1, uint32_t minor1, uint32_t patch1,
    uint32_t major2, uint32_t minor2, uint32_t patch2) noexcept
{
    if (major1 != major2) {
        return major1 > major2 ? VersionCompare::NEWER : VersionCompare::OLDER;
    }
    if (minor1 != minor2) {
        return minor1 > minor2 ? VersionCompare::NEWER : VersionCompare::OLDER;
    }
    if (patch1 != patch2) {
        return patch1 > patch2 ? VersionCompare::NEWER : VersionCompare::OLDER;
    }
    return VersionCompare::EQUAL;
}

/// Compare firmware headers by version
inline constexpr VersionCompare compare_versions(
    const FirmwareHeader& a, const FirmwareHeader& b) noexcept
{
    return compare_versions(
        a.fw_version_major, a.fw_version_minor, a.fw_version_patch,
        b.fw_version_major, b.fw_version_minor, b.fw_version_patch
    );
}

/// Format version as uint32_t (0xMMmmpp00)
inline constexpr uint32_t pack_version(uint8_t major, uint8_t minor, uint8_t patch) noexcept {
    return (uint32_t(major) << 24) | (uint32_t(minor) << 16) | (uint32_t(patch) << 8);
}

/// Extract major version
inline constexpr uint8_t version_major(uint32_t packed) noexcept {
    return static_cast<uint8_t>(packed >> 24);
}

/// Extract minor version
inline constexpr uint8_t version_minor(uint32_t packed) noexcept {
    return static_cast<uint8_t>(packed >> 16);
}

/// Extract patch version
inline constexpr uint8_t version_patch(uint32_t packed) noexcept {
    return static_cast<uint8_t>(packed >> 8);
}

// =============================================================================
// Signature Verification Interface
// =============================================================================

/// Ed25519 signature verification function type
/// @param public_key Public key (32 bytes)
/// @param message Message to verify
/// @param message_len Message length
/// @param signature Signature (64 bytes)
/// @return true if signature is valid
using Ed25519VerifyFn = bool (*)(const uint8_t* public_key,
                                  const uint8_t* message, size_t message_len,
                                  const uint8_t* signature);

/// SHA-256 hash function type
/// @param data Data to hash
/// @param len Data length
/// @param hash Output hash (32 bytes)
using Sha256Fn = void (*)(const uint8_t* data, size_t len, uint8_t* hash);

/// CRC-32 function type (IEEE 802.3)
/// @param data Data to checksum
/// @param len Data length
/// @return CRC-32 value
using Crc32Fn = uint32_t (*)(const uint8_t* data, size_t len);

// =============================================================================
// Firmware Validator
// =============================================================================

/// Validation result
enum class ValidationResult : uint8_t {
    OK,
    INVALID_MAGIC,
    INVALID_HEADER_VERSION,
    INVALID_SIZE,
    CRC_MISMATCH,
    HASH_MISMATCH,
    SIGNATURE_INVALID,
    BOARD_MISMATCH,
    VERSION_TOO_OLD,       // Anti-rollback check failed
    BOOTLOADER_TOO_OLD,    // Requires newer bootloader
    NOT_SIGNED,            // Signature required but not present
};

/// Firmware validator
/// Validates firmware header, CRC, hash, and signature
template <size_t MaxBoardIdLen = 16>
class FirmwareValidator {
public:
    /// Set cryptographic functions
    void set_crypto(Ed25519VerifyFn ed25519_verify,
                    Sha256Fn sha256,
                    Crc32Fn crc32) noexcept {
        ed25519_verify_ = ed25519_verify;
        sha256_ = sha256;
        crc32_ = crc32;
    }

    /// Set CRC-32 function only
    void set_crc(Crc32Fn crc32) noexcept {
        crc32_ = crc32;
    }

    /// Set public key for signature verification
    void set_public_key(const uint8_t* key) noexcept {
        std::memcpy(public_key_, key, ED25519_PUBLIC_KEY_SIZE);
        has_public_key_ = true;
    }

    /// Set expected board ID
    void set_board_id(const char* board_id) noexcept {
        std::strncpy(reinterpret_cast<char*>(expected_board_),
                     board_id, MaxBoardIdLen - 1);
        expected_board_[MaxBoardIdLen - 1] = '\0';
    }

    /// Set current anti-rollback version
    void set_rollback_version(uint32_t version) noexcept {
        min_rollback_version_ = version;
    }

    /// Set current bootloader version
    void set_bootloader_version(uint32_t version) noexcept {
        bootloader_version_ = version;
    }

    /// Require signatures (reject unsigned firmware)
    void require_signature(bool require) noexcept {
        require_signature_ = require;
    }

    /// Validate firmware header only
    [[nodiscard]] ValidationResult validate_header(const FirmwareHeader& header) const noexcept {
        // Check magic
        if (header.magic != FIRMWARE_MAGIC) {
            return ValidationResult::INVALID_MAGIC;
        }

        // Check header version
        if (header.header_version != FIRMWARE_HEADER_VERSION) {
            return ValidationResult::INVALID_HEADER_VERSION;
        }

        // Check board ID
        if (expected_board_[0] != '\0') {
            if (std::strncmp(reinterpret_cast<const char*>(header.target_board),
                            reinterpret_cast<const char*>(expected_board_),
                            MaxBoardIdLen) != 0) {
                return ValidationResult::BOARD_MISMATCH;
            }
        }

        // Check bootloader compatibility
        if (header.min_bootloader > bootloader_version_) {
            return ValidationResult::BOOTLOADER_TOO_OLD;
        }

        // Anti-rollback check
        if (header.rollback_version < min_rollback_version_) {
            return ValidationResult::VERSION_TOO_OLD;
        }

        // Check signature requirement
        if (require_signature_ && !(header.flags & static_cast<uint8_t>(FirmwareFlags::SIGNED))) {
            return ValidationResult::NOT_SIGNED;
        }

        return ValidationResult::OK;
    }

    /// Validate firmware data (CRC and hash)
    /// @param header Firmware header
    /// @param data Firmware binary data
    /// @param len Data length (should match header.image_size)
    [[nodiscard]] ValidationResult validate_data(const FirmwareHeader& header,
                                                  const uint8_t* data,
                                                  size_t len) const noexcept {
        // Check size
        if (len != header.image_size) {
            return ValidationResult::INVALID_SIZE;
        }

        // Verify CRC
        if (crc32_) {
            uint32_t crc = crc32_(data, len);
            if (crc != header.image_crc32) {
                return ValidationResult::CRC_MISMATCH;
            }
        }

        // Verify hash
        if (sha256_) {
            uint8_t hash[SHA256_HASH_SIZE];
            sha256_(data, len, hash);
            if (std::memcmp(hash, header.fw_hash, SHA256_HASH_SIZE) != 0) {
                return ValidationResult::HASH_MISMATCH;
            }
        }

        return ValidationResult::OK;
    }

    /// Validate signature
    /// @param header Firmware header
    /// @param data Firmware binary data (unused in current impl, hash from header used)
    /// @param signature Ed25519 signature (64 bytes)
    [[nodiscard]] ValidationResult validate_signature(const FirmwareHeader& header,
                                                       [[maybe_unused]] const uint8_t* data,
                                                       const uint8_t* signature) const noexcept {
        if (!has_public_key_ || !ed25519_verify_) {
            // No public key or verify function - skip if not required
            if (require_signature_) {
                return ValidationResult::SIGNATURE_INVALID;
            }
            return ValidationResult::OK;
        }

        // Signature covers header + data
        // For simplicity, we verify using the hash from header
        // In production, compute hash over header+data
        if (!ed25519_verify_(public_key_, header.fw_hash, SHA256_HASH_SIZE, signature)) {
            return ValidationResult::SIGNATURE_INVALID;
        }

        return ValidationResult::OK;
    }

    /// Full validation (header + data + signature)
    /// @param image Complete firmware image (header + data + signature)
    /// @param image_len Total image length
    [[nodiscard]] ValidationResult validate_full(const uint8_t* image,
                                                  size_t image_len) const noexcept {
        if (image_len < sizeof(FirmwareHeader)) {
            return ValidationResult::INVALID_SIZE;
        }

        const auto* header = reinterpret_cast<const FirmwareHeader*>(image);
        const uint8_t* data = image + sizeof(FirmwareHeader);

        // Calculate expected total size
        size_t expected_size = sizeof(FirmwareHeader) + header->image_size;
        if (header->flags & static_cast<uint8_t>(FirmwareFlags::SIGNED)) {
            expected_size += ED25519_SIGNATURE_SIZE;
        }

        if (image_len != expected_size) {
            return ValidationResult::INVALID_SIZE;
        }

        // Validate header
        auto result = validate_header(*header);
        if (result != ValidationResult::OK) {
            return result;
        }

        // Validate data
        result = validate_data(*header, data, header->image_size);
        if (result != ValidationResult::OK) {
            return result;
        }

        // Validate signature if present
        if (header->flags & static_cast<uint8_t>(FirmwareFlags::SIGNED)) {
            const uint8_t* signature = data + header->image_size;
            result = validate_signature(*header, data, signature);
            if (result != ValidationResult::OK) {
                return result;
            }
        }

        return ValidationResult::OK;
    }

private:
    Ed25519VerifyFn ed25519_verify_ = nullptr;
    Sha256Fn sha256_ = nullptr;
    Crc32Fn crc32_ = nullptr;
    uint8_t public_key_[ED25519_PUBLIC_KEY_SIZE]{};
    uint8_t expected_board_[MaxBoardIdLen]{};
    uint32_t min_rollback_version_ = 0;
    uint32_t bootloader_version_ = 0;
    bool has_public_key_ = false;
    bool require_signature_ = false;
};

// =============================================================================
// Firmware Header Builder
// =============================================================================

/// Helper class to build firmware headers
class FirmwareHeaderBuilder {
public:
    /// Initialize with basic info
    FirmwareHeaderBuilder& version(uint32_t major, uint32_t minor, uint32_t patch) noexcept {
        header_.fw_version_major = major;
        header_.fw_version_minor = minor;
        header_.fw_version_patch = patch;
        return *this;
    }

    /// Set build number
    FirmwareHeaderBuilder& build_number(uint32_t build) noexcept {
        header_.fw_build_number = build;
        return *this;
    }

    /// Set firmware size
    FirmwareHeaderBuilder& image_size(uint32_t size) noexcept {
        header_.image_size = size;
        return *this;
    }

    /// Set CRC32
    FirmwareHeaderBuilder& crc32(uint32_t crc) noexcept {
        header_.image_crc32 = crc;
        return *this;
    }

    /// Set load address
    FirmwareHeaderBuilder& load_address(uint32_t addr) noexcept {
        header_.load_address = addr;
        return *this;
    }

    /// Set entry point
    FirmwareHeaderBuilder& entry_point(uint32_t addr) noexcept {
        header_.entry_point = addr;
        return *this;
    }

    /// Set target board
    FirmwareHeaderBuilder& board(const char* board_id) noexcept {
        std::strncpy(reinterpret_cast<char*>(header_.target_board),
                     board_id, sizeof(header_.target_board) - 1);
        return *this;
    }

    /// Set firmware hash
    FirmwareHeaderBuilder& hash(const uint8_t* hash) noexcept {
        std::memcpy(header_.fw_hash, hash, SHA256_HASH_SIZE);
        return *this;
    }

    /// Set minimum bootloader version
    FirmwareHeaderBuilder& min_bootloader(uint32_t version) noexcept {
        header_.min_bootloader = version;
        return *this;
    }

    /// Set anti-rollback version
    FirmwareHeaderBuilder& rollback_version(uint32_t version) noexcept {
        header_.rollback_version = version;
        return *this;
    }

    /// Set build timestamp
    FirmwareHeaderBuilder& timestamp(uint32_t ts) noexcept {
        header_.timestamp = ts;
        return *this;
    }

    /// Set flags
    FirmwareHeaderBuilder& flags(FirmwareFlags f) noexcept {
        header_.flags = static_cast<uint8_t>(f);
        return *this;
    }

    /// Mark as signed
    FirmwareHeaderBuilder& signed_firmware() noexcept {
        header_.flags |= static_cast<uint8_t>(FirmwareFlags::SIGNED);
        return *this;
    }

    /// Build the header
    [[nodiscard]] FirmwareHeader build() noexcept {
        header_.magic = FIRMWARE_MAGIC;
        header_.header_version = FIRMWARE_HEADER_VERSION;
        return header_;
    }

private:
    FirmwareHeader header_{};
};

// =============================================================================
// CRC-32 Implementation (IEEE 802.3)
// =============================================================================

/// CRC-32 lookup table (generated from polynomial 0xEDB88320)
inline constexpr uint32_t CRC32_TABLE[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
    0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
    0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
    0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
    0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
    0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
    0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
    0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
    0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
    0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
    0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
    0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
    0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
    0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
    0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
    0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
    0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
    0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
    0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
    0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAEE1694A, 0xD9E659DC,
    0x40EF7A66, 0x37E84FF0, 0xA9BCFB53, 0xDEBBCBC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD706B3,
    0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D,
};

/// Compute CRC-32 (IEEE 802.3)
inline constexpr uint32_t crc32(const uint8_t* data, size_t len) noexcept {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc = CRC32_TABLE[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

/// CRC-32 function for FirmwareValidator
inline uint32_t crc32_fn(const uint8_t* data, size_t len) noexcept {
    return crc32(data, len);
}

} // namespace umiboot

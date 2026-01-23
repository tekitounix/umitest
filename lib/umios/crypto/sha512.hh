// SPDX-License-Identifier: MIT
// SHA-512 Hash Function (FIPS 180-4)
// Minimal implementation for Ed25519 signature verification

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace umi::crypto {

/// SHA-512 hash output size in bytes
inline constexpr size_t SHA512_HASH_SIZE = 64;

/// SHA-512 block size in bytes
inline constexpr size_t SHA512_BLOCK_SIZE = 128;

/// SHA-512 context for incremental hashing
struct Sha512Context {
    std::array<uint64_t, 8> state;
    std::array<uint8_t, SHA512_BLOCK_SIZE> buffer;
    uint64_t count_low;
    uint64_t count_high;
    size_t buffer_len;
};

/// Initialize SHA-512 context
void sha512_init(Sha512Context& ctx) noexcept;

/// Update SHA-512 context with data
void sha512_update(Sha512Context& ctx, const uint8_t* data, size_t len) noexcept;

/// Finalize SHA-512 and get hash
void sha512_final(Sha512Context& ctx, uint8_t* hash) noexcept;

/// One-shot SHA-512 hash
void sha512(const uint8_t* data, size_t len, uint8_t* hash) noexcept;

/// Convenience overload with spans
inline void sha512(std::span<const uint8_t> data, std::span<uint8_t, SHA512_HASH_SIZE> hash) noexcept {
    sha512(data.data(), data.size(), hash.data());
}

} // namespace umi::crypto

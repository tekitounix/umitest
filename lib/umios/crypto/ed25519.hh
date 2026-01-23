// SPDX-License-Identifier: MIT
// Ed25519 Signature Verification (RFC 8032)
// Minimal implementation for application signature verification

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace umi::crypto {

/// Ed25519 public key size in bytes
inline constexpr size_t ED25519_PUBLIC_KEY_SIZE = 32;

/// Ed25519 signature size in bytes
inline constexpr size_t ED25519_SIGNATURE_SIZE = 64;

/// Ed25519 signature verification context for incremental verification
struct Ed25519Context {
    std::array<uint8_t, 32> public_key;
    std::array<uint8_t, 64> signature;
    // Internal state for SHA-512
    std::array<uint8_t, 64 + 32 + 32> prefix; // R || A || (first message bytes)
    size_t prefix_len;
    bool initialized;
};

/// Verify Ed25519 signature (one-shot)
/// @param signature 64-byte signature (R || S)
/// @param public_key 32-byte public key (A)
/// @param message Message to verify
/// @return true if signature is valid
bool ed25519_verify(const uint8_t* signature, const uint8_t* public_key, const uint8_t* message,
                    size_t message_len) noexcept;

/// Convenience overload with spans
inline bool ed25519_verify(std::span<const uint8_t, ED25519_SIGNATURE_SIZE> signature,
                           std::span<const uint8_t, ED25519_PUBLIC_KEY_SIZE> public_key,
                           std::span<const uint8_t> message) noexcept {
    return ed25519_verify(signature.data(), public_key.data(), message.data(), message.size());
}

/// Initialize Ed25519 verification context for incremental verification
/// @param ctx Context to initialize
/// @param signature 64-byte signature
/// @param public_key 32-byte public key
void ed25519_verify_init(Ed25519Context& ctx, const uint8_t* signature,
                         const uint8_t* public_key) noexcept;

/// Update Ed25519 verification context with message data
/// @param ctx Context
/// @param data Message data
/// @param len Length of data
void ed25519_verify_update(Ed25519Context& ctx, const uint8_t* data, size_t len) noexcept;

/// Finalize Ed25519 verification
/// @param ctx Context
/// @return true if signature is valid
bool ed25519_verify_final(Ed25519Context& ctx) noexcept;

} // namespace umi::crypto

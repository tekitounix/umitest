// SPDX-License-Identifier: MIT
// Ed25519 Public Key for Release Build Signature Verification
// IMPORTANT: Replace with actual release public key before deployment!

#pragma once

#include <cstdint>

namespace umi::crypto {

/// Release build public key (32 bytes)
/// This is a placeholder - generate real keys using lib/umi/crypto/tools/keygen.py
/// and replace this with your release public key.
///
/// SECURITY WARNING:
/// - Keep the corresponding private key SECRET and secure
/// - Never commit private keys to version control
/// - Use different key pairs for development and production
inline constexpr uint8_t RELEASE_PUBLIC_KEY[32] = {
    // Placeholder key (all zeros) - MUST BE REPLACED for production
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/// Development build public key (32 bytes)
/// Used for testing signature verification during development
/// This can be a well-known test key since it's only for development
inline constexpr uint8_t DEVELOPMENT_PUBLIC_KEY[32] = {
    // Test key - DO NOT use in production
    // Corresponds to private key: 9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60
    // (RFC 8032 Test Vector #1)
    0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe, 0xd3, 0xc9, 0x64, 0x07, 0x3a,
    0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6, 0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a,
};

} // namespace umi::crypto

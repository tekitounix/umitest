// SPDX-License-Identifier: MIT
// UMI-Boot Authentication Protocol
// Challenge-Response authentication using HMAC-SHA256
#pragma once

#include <cstdint>
#include <cstring>
#include <span>

namespace umiboot {

// =============================================================================
// Authentication Protocol
// =============================================================================
//
// Challenge-Response authentication flow:
//
// 1. Host → Device: AUTH_CHALLENGE_REQ
// 2. Device → Host: AUTH_CHALLENGE (random 32-byte challenge)
// 3. Host: response = HMAC-SHA256(challenge, shared_key)
// 4. Host → Device: AUTH_RESPONSE (32-byte response)
// 5. Device: verify HMAC, store session token
// 6. Device → Host: AUTH_OK or AUTH_FAIL
//
// Session token is used for subsequent operations.
// Session expires after timeout or explicit logout.
//
// =============================================================================

// Authentication commands (0x30-0x3F range)
enum class AuthCommand : uint8_t {
    AUTH_CHALLENGE_REQ  = 0x30,  // Request challenge
    AUTH_CHALLENGE      = 0x31,  // Challenge (32 bytes)
    AUTH_RESPONSE       = 0x32,  // Response (32 bytes)
    AUTH_OK             = 0x33,  // Authentication successful
    AUTH_FAIL           = 0x34,  // Authentication failed
    AUTH_LOGOUT         = 0x35,  // End session
    AUTH_STATUS         = 0x36,  // Query auth status
};

// Authentication error codes
enum class AuthError : uint8_t {
    OK                  = 0x00,
    INVALID_CHALLENGE   = 0x01,
    INVALID_RESPONSE    = 0x02,
    SESSION_EXPIRED     = 0x03,
    NOT_AUTHENTICATED   = 0x04,
    ALREADY_AUTHENTICATED = 0x05,
};

// =============================================================================
// HMAC-SHA256 Interface
// =============================================================================
// Platform must provide these via template parameter or callbacks

/// HMAC-SHA256 function type
/// @param key Shared secret key
/// @param key_len Key length
/// @param data Data to authenticate
/// @param data_len Data length
/// @param out Output buffer (32 bytes)
using HmacSha256Fn = void (*)(const uint8_t* key, size_t key_len,
                               const uint8_t* data, size_t data_len,
                               uint8_t* out);

/// Random number generator type
/// @param out Output buffer
/// @param len Requested bytes
using RandomFn = void (*)(uint8_t* out, size_t len);

/// Constant-time comparison to prevent timing attacks
inline constexpr bool secure_compare(const uint8_t* a, const uint8_t* b,
                                      size_t len) noexcept {
    uint8_t diff = 0;
    for (size_t i = 0; i < len; ++i) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0;
}

// =============================================================================
// Authentication State
// =============================================================================

enum class AuthState : uint8_t {
    IDLE,               // No authentication in progress
    CHALLENGE_SENT,     // Challenge sent, waiting for response
    AUTHENTICATED,      // Successfully authenticated
};

// =============================================================================
// Authenticator
// =============================================================================

/// Authentication handler for device side
/// @tparam KeySize Size of shared secret key (default 32 bytes)
/// @tparam SessionTimeoutMs Session timeout in milliseconds (0 = no timeout)
template <size_t KeySize = 32, uint32_t SessionTimeoutMs = 300000>
class Authenticator {
public:
    static constexpr size_t CHALLENGE_SIZE = 32;
    static constexpr size_t RESPONSE_SIZE = 32;

    /// Initialize with shared secret and crypto functions
    /// @param key Shared secret key
    /// @param hmac HMAC-SHA256 function
    /// @param rng Random number generator
    void init(const uint8_t* key, HmacSha256Fn hmac, RandomFn rng) noexcept {
        std::memcpy(key_, key, KeySize);
        hmac_fn_ = hmac;
        random_fn_ = rng;
        state_ = AuthState::IDLE;
        session_start_ = 0;
    }

    /// Generate a new challenge
    /// @param out Challenge output buffer (CHALLENGE_SIZE bytes)
    void generate_challenge(uint8_t* out) noexcept {
        if (random_fn_) {
            random_fn_(out, CHALLENGE_SIZE);
        } else {
            // Fallback: simple LFSR (NOT cryptographically secure!)
            uint32_t lfsr = 0xACE1u ^ static_cast<uint32_t>(session_start_);
            for (size_t i = 0; i < CHALLENGE_SIZE; ++i) {
                lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xD0000001u);
                out[i] = static_cast<uint8_t>(lfsr);
            }
        }
        std::memcpy(pending_challenge_, out, CHALLENGE_SIZE);
        state_ = AuthState::CHALLENGE_SENT;
    }

    /// Verify response from host
    /// @param response Response from host (RESPONSE_SIZE bytes)
    /// @param current_time Current timestamp in milliseconds
    /// @return true if response is valid
    bool verify_response(const uint8_t* response, uint32_t current_time) noexcept {
        if (state_ != AuthState::CHALLENGE_SENT) {
            last_error_ = AuthError::INVALID_CHALLENGE;
            return false;
        }

        // Compute expected response
        uint8_t expected[RESPONSE_SIZE];
        if (hmac_fn_) {
            hmac_fn_(key_, KeySize, pending_challenge_, CHALLENGE_SIZE, expected);
        } else {
            // No HMAC function - reject
            last_error_ = AuthError::INVALID_RESPONSE;
            state_ = AuthState::IDLE;
            return false;
        }

        // Constant-time comparison
        if (!secure_compare(response, expected, RESPONSE_SIZE)) {
            last_error_ = AuthError::INVALID_RESPONSE;
            state_ = AuthState::IDLE;
            return false;
        }

        // Success
        state_ = AuthState::AUTHENTICATED;
        session_start_ = current_time;
        last_error_ = AuthError::OK;
        return true;
    }

    /// Check if currently authenticated
    /// @param current_time Current timestamp in milliseconds
    /// @return true if authenticated and session not expired
    bool is_authenticated(uint32_t current_time) const noexcept {
        if (state_ != AuthState::AUTHENTICATED) {
            return false;
        }

        if constexpr (SessionTimeoutMs > 0) {
            if (current_time - session_start_ > SessionTimeoutMs) {
                return false;
            }
        }

        return true;
    }

    /// End current session
    void logout() noexcept {
        state_ = AuthState::IDLE;
        session_start_ = 0;
        std::memset(pending_challenge_, 0, CHALLENGE_SIZE);
    }

    /// Refresh session timeout
    /// @param current_time Current timestamp
    void refresh_session(uint32_t current_time) noexcept {
        if (state_ == AuthState::AUTHENTICATED) {
            session_start_ = current_time;
        }
    }

    /// Get current authentication state
    [[nodiscard]] AuthState state() const noexcept { return state_; }

    /// Get last error
    [[nodiscard]] AuthError last_error() const noexcept { return last_error_; }

    /// Get session start time
    [[nodiscard]] uint32_t session_start() const noexcept { return session_start_; }

private:
    uint8_t key_[KeySize]{};
    uint8_t pending_challenge_[CHALLENGE_SIZE]{};
    HmacSha256Fn hmac_fn_ = nullptr;
    RandomFn random_fn_ = nullptr;
    AuthState state_ = AuthState::IDLE;
    AuthError last_error_ = AuthError::OK;
    uint32_t session_start_ = 0;
};

// =============================================================================
// Client-side Authentication Helper
// =============================================================================

/// Authentication client helper for host side
template <size_t KeySize = 32>
class AuthClient {
public:
    static constexpr size_t CHALLENGE_SIZE = 32;
    static constexpr size_t RESPONSE_SIZE = 32;

    /// Initialize with shared secret and HMAC function
    void init(const uint8_t* key, HmacSha256Fn hmac) noexcept {
        std::memcpy(key_, key, KeySize);
        hmac_fn_ = hmac;
    }

    /// Compute response to challenge
    /// @param challenge Challenge from device (CHALLENGE_SIZE bytes)
    /// @param response Output response buffer (RESPONSE_SIZE bytes)
    /// @return true if response computed successfully
    bool compute_response(const uint8_t* challenge, uint8_t* response) noexcept {
        if (!hmac_fn_) return false;
        hmac_fn_(key_, KeySize, challenge, CHALLENGE_SIZE, response);
        return true;
    }

private:
    uint8_t key_[KeySize]{};
    HmacSha256Fn hmac_fn_ = nullptr;
};

// =============================================================================
// Portable Crypto Implementations (for constrained platforms)
// =============================================================================

#ifdef UMI_INCLUDE_SOFTWARE_CRYPTO

#include <umios/crypto/sha256.hh>

/// Software HMAC-SHA256 implementation (delegates to umi::crypto)
inline void hmac_sha256_soft(const uint8_t* key, size_t key_len,
                              const uint8_t* data, size_t data_len,
                              uint8_t* out) noexcept {
    umi::crypto::hmac_sha256(key, key_len, data, data_len, out);
}

#endif // UMI_INCLUDE_SOFTWARE_CRYPTO

} // namespace umiboot


.. _program_listing_file_protocol_umi_auth.hh:

Program Listing for File umi_auth.hh
====================================

|exhale_lsh| :ref:`Return to documentation for file <file_protocol_umi_auth.hh>` (``protocol/umi_auth.hh``)

.. |exhale_lsh| unicode:: U+021B0 .. UPWARDS ARROW WITH TIP LEFTWARDS

.. code-block:: cpp

   // SPDX-License-Identifier: MIT
   // UMI-OS Authentication Protocol
   // Challenge-Response authentication using HMAC-SHA256
   #pragma once
   
   #include <cstdint>
   #include <cstring>
   #include <span>
   
   namespace umidi::protocol {
   
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
   
   using HmacSha256Fn = void (*)(const uint8_t* key, size_t key_len,
                                  const uint8_t* data, size_t data_len,
                                  uint8_t* out);
   
   using RandomFn = void (*)(uint8_t* out, size_t len);
   
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
   
   template <size_t KeySize = 32, uint32_t SessionTimeoutMs = 300000>
   class Authenticator {
   public:
       static constexpr size_t CHALLENGE_SIZE = 32;
       static constexpr size_t RESPONSE_SIZE = 32;
   
       void init(const uint8_t* key, HmacSha256Fn hmac, RandomFn rng) noexcept {
           std::memcpy(key_, key, KeySize);
           hmac_fn_ = hmac;
           random_fn_ = rng;
           state_ = AuthState::IDLE;
           session_start_ = 0;
       }
   
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
   
       void logout() noexcept {
           state_ = AuthState::IDLE;
           session_start_ = 0;
           std::memset(pending_challenge_, 0, CHALLENGE_SIZE);
       }
   
       void refresh_session(uint32_t current_time) noexcept {
           if (state_ == AuthState::AUTHENTICATED) {
               session_start_ = current_time;
           }
       }
   
       [[nodiscard]] AuthState state() const noexcept { return state_; }
   
       [[nodiscard]] AuthError last_error() const noexcept { return last_error_; }
   
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
   
   template <size_t KeySize = 32>
   class AuthClient {
   public:
       static constexpr size_t CHALLENGE_SIZE = 32;
       static constexpr size_t RESPONSE_SIZE = 32;
   
       void init(const uint8_t* key, HmacSha256Fn hmac) noexcept {
           std::memcpy(key_, key, KeySize);
           hmac_fn_ = hmac;
       }
   
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
   
   namespace detail {
   
   // SHA-256 constants
   inline constexpr uint32_t SHA256_K[64] = {
       0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
       0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
       0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
       0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
       0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
       0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
       0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
       0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
       0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
       0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
       0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
       0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
       0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
       0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
       0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
       0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
   };
   
   inline constexpr uint32_t rotr(uint32_t x, int n) noexcept {
       return (x >> n) | (x << (32 - n));
   }
   
   inline constexpr uint32_t sha256_ch(uint32_t x, uint32_t y, uint32_t z) noexcept {
       return (x & y) ^ (~x & z);
   }
   
   inline constexpr uint32_t sha256_maj(uint32_t x, uint32_t y, uint32_t z) noexcept {
       return (x & y) ^ (x & z) ^ (y & z);
   }
   
   inline constexpr uint32_t sha256_sig0(uint32_t x) noexcept {
       return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
   }
   
   inline constexpr uint32_t sha256_sig1(uint32_t x) noexcept {
       return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
   }
   
   inline constexpr uint32_t sha256_ep0(uint32_t x) noexcept {
       return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
   }
   
   inline constexpr uint32_t sha256_ep1(uint32_t x) noexcept {
       return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
   }
   
   struct Sha256 {
       uint32_t state[8]{
           0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
           0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
       };
       uint8_t buffer[64]{};
       size_t buffer_len = 0;
       uint64_t total_len = 0;
   
       void transform(const uint8_t* block) noexcept {
           uint32_t w[64];
   
           // Prepare message schedule
           for (int i = 0; i < 16; ++i) {
               w[i] = (uint32_t(block[i * 4]) << 24) |
                      (uint32_t(block[i * 4 + 1]) << 16) |
                      (uint32_t(block[i * 4 + 2]) << 8) |
                      block[i * 4 + 3];
           }
           for (int i = 16; i < 64; ++i) {
               w[i] = sha256_ep1(w[i - 2]) + w[i - 7] +
                      sha256_ep0(w[i - 15]) + w[i - 16];
           }
   
           // Initialize working variables
           uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
           uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
   
           // Compression
           for (int i = 0; i < 64; ++i) {
               uint32_t t1 = h + sha256_sig1(e) + sha256_ch(e, f, g) + SHA256_K[i] + w[i];
               uint32_t t2 = sha256_sig0(a) + sha256_maj(a, b, c);
               h = g; g = f; f = e; e = d + t1;
               d = c; c = b; b = a; a = t1 + t2;
           }
   
           // Update state
           state[0] += a; state[1] += b; state[2] += c; state[3] += d;
           state[4] += e; state[5] += f; state[6] += g; state[7] += h;
       }
   
       void update(const uint8_t* data, size_t len) noexcept {
           for (size_t i = 0; i < len; ++i) {
               buffer[buffer_len++] = data[i];
               if (buffer_len == 64) {
                   transform(buffer);
                   buffer_len = 0;
               }
           }
           total_len += len;
       }
   
       void finalize(uint8_t* hash) noexcept {
           // Padding
           buffer[buffer_len++] = 0x80;
           if (buffer_len > 56) {
               while (buffer_len < 64) buffer[buffer_len++] = 0;
               transform(buffer);
               buffer_len = 0;
           }
           while (buffer_len < 56) buffer[buffer_len++] = 0;
   
           // Length in bits (big endian)
           uint64_t bits = total_len * 8;
           buffer[56] = static_cast<uint8_t>(bits >> 56);
           buffer[57] = static_cast<uint8_t>(bits >> 48);
           buffer[58] = static_cast<uint8_t>(bits >> 40);
           buffer[59] = static_cast<uint8_t>(bits >> 32);
           buffer[60] = static_cast<uint8_t>(bits >> 24);
           buffer[61] = static_cast<uint8_t>(bits >> 16);
           buffer[62] = static_cast<uint8_t>(bits >> 8);
           buffer[63] = static_cast<uint8_t>(bits);
           transform(buffer);
   
           // Output hash (big endian)
           for (int i = 0; i < 8; ++i) {
               hash[i * 4]     = static_cast<uint8_t>(state[i] >> 24);
               hash[i * 4 + 1] = static_cast<uint8_t>(state[i] >> 16);
               hash[i * 4 + 2] = static_cast<uint8_t>(state[i] >> 8);
               hash[i * 4 + 3] = static_cast<uint8_t>(state[i]);
           }
       }
   };
   
   } // namespace detail
   
   inline void hmac_sha256_soft(const uint8_t* key, size_t key_len,
                                 const uint8_t* data, size_t data_len,
                                 uint8_t* out) noexcept {
       constexpr size_t BLOCK_SIZE = 64;
       constexpr size_t HASH_SIZE = 32;
   
       uint8_t k_pad[BLOCK_SIZE]{};
   
       // If key is longer than block size, hash it
       if (key_len > BLOCK_SIZE) {
           detail::Sha256 hash;
           hash.update(key, key_len);
           hash.finalize(k_pad);
           key_len = HASH_SIZE;
       } else {
           std::memcpy(k_pad, key, key_len);
       }
   
       // Inner hash: H((K ^ ipad) || data)
       uint8_t inner_pad[BLOCK_SIZE];
       for (size_t i = 0; i < BLOCK_SIZE; ++i) {
           inner_pad[i] = k_pad[i] ^ 0x36;
       }
   
       detail::Sha256 inner;
       inner.update(inner_pad, BLOCK_SIZE);
       inner.update(data, data_len);
       uint8_t inner_hash[HASH_SIZE];
       inner.finalize(inner_hash);
   
       // Outer hash: H((K ^ opad) || inner_hash)
       uint8_t outer_pad[BLOCK_SIZE];
       for (size_t i = 0; i < BLOCK_SIZE; ++i) {
           outer_pad[i] = k_pad[i] ^ 0x5c;
       }
   
       detail::Sha256 outer;
       outer.update(outer_pad, BLOCK_SIZE);
       outer.update(inner_hash, HASH_SIZE);
       outer.finalize(out);
   }
   
   #endif // UMI_INCLUDE_SOFTWARE_CRYPTO
   
   } // namespace umidi::protocol

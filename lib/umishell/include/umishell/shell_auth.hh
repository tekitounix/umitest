// =====================================================================
// UMI Shell Auth - Authentication system for shell access control
// =====================================================================
//
// Provides:
//   - Access levels (USER, ADMIN, FACTORY)
//   - Password verification
//   - Session timeout
//   - Brute-force protection (lockout)
//
// This library has NO dependencies on kernel, hardware, or OS.
// Password storage and time source are provided by the application.
//
// =====================================================================

#pragma once

#include <cstdint>
#include <cstring>

namespace umi::shell {

// ============================================================================
// Access Level
// ============================================================================

/// Shell access levels (least to most privileged)
enum class AccessLevel : uint8_t {
    USER = 0,     // Basic commands only
    ADMIN = 1,    // Configuration allowed
    FACTORY = 2,  // Full access including production tests
};

// ============================================================================
// Authentication State
// ============================================================================

/// Authentication configuration
struct AuthConfig {
    uint64_t session_timeout_us = 5ULL * 60 * 1000000;  // 5 minutes
    uint64_t lockout_duration_us = 30ULL * 1000000;     // 30 seconds
    uint8_t max_failed_attempts = 3;
};

/// Password verification callback
/// Returns true if password matches for the given level
using PasswordVerifier = bool (*)(AccessLevel level, const char* password, void* ctx);

/// Authentication state manager
class AuthState {
public:
    explicit AuthState(const AuthConfig& config = {})
        : config_(config) {}

    /// Get current access level
    [[nodiscard]] AccessLevel level() const { return level_; }

    /// Check if current level meets required level
    [[nodiscard]] bool has_access(AccessLevel required) const {
        return static_cast<uint8_t>(level_) >= static_cast<uint8_t>(required);
    }

    /// Attempt authentication
    /// Returns true if successful
    bool authenticate(AccessLevel target, const char* password,
                      uint64_t now_us, PasswordVerifier verifier, void* ctx) {
        // Check lockout
        if (is_locked_out(now_us)) {
            return false;
        }

        // Verify password
        if (verifier != nullptr && verifier(target, password, ctx)) {
            level_ = target;
            last_activity_us_ = now_us;
            failed_attempts_ = 0;
            return true;
        }

        // Record failure
        record_failure(now_us);
        return false;
    }

    /// Logout - return to USER level
    void logout() {
        level_ = AccessLevel::USER;
    }

    /// Update activity timestamp (call on each command)
    void touch(uint64_t now_us) {
        last_activity_us_ = now_us;
    }

    /// Check and apply session timeout
    void check_timeout(uint64_t now_us) {
        if (level_ != AccessLevel::USER) {
            if (now_us - last_activity_us_ > config_.session_timeout_us) {
                level_ = AccessLevel::USER;
            }
        }
    }

    /// Check if locked out due to failed attempts
    [[nodiscard]] bool is_locked_out(uint64_t now_us) const {
        if (failed_attempts_ >= config_.max_failed_attempts) {
            return now_us < lockout_until_us_;
        }
        return false;
    }

    /// Get remaining lockout time in seconds (0 if not locked)
    [[nodiscard]] uint32_t lockout_remaining_sec(uint64_t now_us) const {
        if (!is_locked_out(now_us)) {
            return 0;
        }
        return static_cast<uint32_t>((lockout_until_us_ - now_us) / 1000000);
    }

    /// Get number of failed attempts
    [[nodiscard]] uint8_t failed_attempts() const { return failed_attempts_; }

private:
    void record_failure(uint64_t now_us) {
        failed_attempts_++;
        if (failed_attempts_ >= config_.max_failed_attempts) {
            lockout_until_us_ = now_us + config_.lockout_duration_us;
        }
    }

    AuthConfig config_;
    AccessLevel level_ = AccessLevel::USER;
    uint64_t last_activity_us_ = 0;
    uint8_t failed_attempts_ = 0;
    uint64_t lockout_until_us_ = 0;
};

// ============================================================================
// Simple Password Checker (for development/simulation)
// ============================================================================

/// Simple hardcoded password checker (NOT for production!)
/// Admin: "admin", Factory: "factory"
inline bool simple_password_check(AccessLevel level, const char* password, void* /*ctx*/) {
    switch (level) {
        case AccessLevel::ADMIN:
            return std::strcmp(password, "admin") == 0;
        case AccessLevel::FACTORY:
            return std::strcmp(password, "factory") == 0;
        default:
            return false;
    }
}

}  // namespace umi::shell

// SPDX-License-Identifier: MIT
// umi_boot Authentication Tests

#include <umitest.hh>
#include <umiboot/auth.hh>

using namespace umiboot;
using namespace umitest;

// =============================================================================
// Test Utilities
// =============================================================================

// Simple mock HMAC (just XOR for testing - NOT secure!)
static void mock_hmac(const uint8_t* key, size_t key_len,
                      const uint8_t* data, size_t data_len,
                      uint8_t* out) {
    // Simple XOR-based "hash" for testing
    for (size_t i = 0; i < 32; ++i) {
        out[i] = 0;
        if (i < key_len) out[i] ^= key[i];
        if (i < data_len) out[i] ^= data[i];
    }
}

// Mock RNG (deterministic for testing)
static void mock_rng(uint8_t* out, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        out[i] = static_cast<uint8_t>(i * 7 + 13);
    }
}

// =============================================================================
// Authentication Tests
// =============================================================================

bool test_secure_compare_equal(TestContext& t) {
    uint8_t a[] = {1, 2, 3, 4, 5};
    uint8_t b[] = {1, 2, 3, 4, 5};
    t.assert_true(secure_compare(a, b, 5));
    return true;
}

bool test_secure_compare_not_equal(TestContext& t) {
    uint8_t a[] = {1, 2, 3, 4, 5};
    uint8_t b[] = {1, 2, 3, 4, 6};
    t.assert_true(!secure_compare(a, b, 5));
    return true;
}

bool test_secure_compare_empty(TestContext& t) {
    uint8_t a[1] = {0};
    uint8_t b[1] = {0};
    t.assert_true(secure_compare(a, b, 0));
    return true;
}

bool test_authenticator_initial_state(TestContext& t) {
    Authenticator<32, 60000> auth;
    uint8_t key[32] = {0};
    auth.init(key, mock_hmac, mock_rng);

    t.assert_eq(auth.state(), AuthState::IDLE);
    t.assert_eq(auth.last_error(), AuthError::OK);
    t.assert_true(!auth.is_authenticated(0));
    return true;
}

bool test_authenticator_challenge_generation(TestContext& t) {
    Authenticator<32, 60000> auth;
    uint8_t key[32] = {0};
    auth.init(key, mock_hmac, mock_rng);

    uint8_t challenge[32];
    auth.generate_challenge(challenge);

    t.assert_eq(auth.state(), AuthState::CHALLENGE_SENT);

    // Verify challenge was generated (not all zeros)
    bool has_nonzero = false;
    for (int i = 0; i < 32; ++i) {
        if (challenge[i] != 0) has_nonzero = true;
    }
    t.assert_true(has_nonzero);
    return true;
}

bool test_authenticator_verify_correct_response(TestContext& t) {
    Authenticator<32, 60000> auth;
    uint8_t key[32] = {1, 2, 3, 4};  // Simple key
    auth.init(key, mock_hmac, mock_rng);

    // Generate challenge
    uint8_t challenge[32];
    auth.generate_challenge(challenge);

    // Compute correct response
    uint8_t response[32];
    mock_hmac(key, 32, challenge, 32, response);

    // Verify
    t.assert_true(auth.verify_response(response, 1000));
    t.assert_eq(auth.state(), AuthState::AUTHENTICATED);
    t.assert_true(auth.is_authenticated(1000));
    return true;
}

bool test_authenticator_verify_wrong_response(TestContext& t) {
    Authenticator<32, 60000> auth;
    uint8_t key[32] = {1, 2, 3, 4};
    auth.init(key, mock_hmac, mock_rng);

    uint8_t challenge[32];
    auth.generate_challenge(challenge);

    // Wrong response
    uint8_t wrong_response[32] = {0};

    t.assert_true(!auth.verify_response(wrong_response, 1000));
    t.assert_eq(auth.state(), AuthState::IDLE);
    t.assert_eq(auth.last_error(), AuthError::INVALID_RESPONSE);
    return true;
}

bool test_authenticator_session_timeout(TestContext& t) {
    Authenticator<32, 1000> auth;  // 1 second timeout
    uint8_t key[32] = {0};
    auth.init(key, mock_hmac, mock_rng);

    uint8_t challenge[32];
    auth.generate_challenge(challenge);

    uint8_t response[32];
    mock_hmac(key, 32, challenge, 32, response);
    auth.verify_response(response, 100);

    // Should be authenticated
    t.assert_true(auth.is_authenticated(100));

    // After timeout
    t.assert_true(!auth.is_authenticated(1200));
    return true;
}

bool test_authenticator_logout(TestContext& t) {
    Authenticator<32, 60000> auth;
    uint8_t key[32] = {0};
    auth.init(key, mock_hmac, mock_rng);

    uint8_t challenge[32];
    auth.generate_challenge(challenge);

    uint8_t response[32];
    mock_hmac(key, 32, challenge, 32, response);
    auth.verify_response(response, 1000);

    t.assert_true(auth.is_authenticated(1000));

    auth.logout();

    t.assert_eq(auth.state(), AuthState::IDLE);
    t.assert_true(!auth.is_authenticated(1000));
    return true;
}

bool test_authenticator_refresh_session(TestContext& t) {
    Authenticator<32, 1000> auth;
    uint8_t key[32] = {0};
    auth.init(key, mock_hmac, mock_rng);

    uint8_t challenge[32];
    auth.generate_challenge(challenge);

    uint8_t response[32];
    mock_hmac(key, 32, challenge, 32, response);
    auth.verify_response(response, 0);

    // Refresh at 500ms
    auth.refresh_session(500);

    // Should still be authenticated at 1400ms (500 + 1000 - 100)
    t.assert_true(auth.is_authenticated(1400));

    // But not at 1600ms
    t.assert_true(!auth.is_authenticated(1600));
    return true;
}

bool test_auth_client_compute_response(TestContext& t) {
    AuthClient<32> client;
    uint8_t key[32] = {1, 2, 3, 4};
    client.init(key, mock_hmac);

    uint8_t challenge[32] = {10, 20, 30};
    uint8_t response[32];

    t.assert_true(client.compute_response(challenge, response));

    // Verify response matches expected
    uint8_t expected[32];
    mock_hmac(key, 32, challenge, 32, expected);
    t.assert_true(std::memcmp(response, expected, 32) == 0);
    return true;
}

bool test_auth_client_no_hmac(TestContext& t) {
    AuthClient<32> client;
    uint8_t key[32] = {0};
    client.init(key, nullptr);  // No HMAC function

    uint8_t challenge[32] = {0};
    uint8_t response[32];

    t.assert_true(!client.compute_response(challenge, response));
    return true;
}

bool test_auth_commands_values(TestContext& t) {
    t.assert_eq(static_cast<uint8_t>(AuthCommand::AUTH_CHALLENGE_REQ), 0x30);
    t.assert_eq(static_cast<uint8_t>(AuthCommand::AUTH_CHALLENGE), 0x31);
    t.assert_eq(static_cast<uint8_t>(AuthCommand::AUTH_RESPONSE), 0x32);
    t.assert_eq(static_cast<uint8_t>(AuthCommand::AUTH_OK), 0x33);
    t.assert_eq(static_cast<uint8_t>(AuthCommand::AUTH_FAIL), 0x34);
    return true;
}

// =============================================================================
// Main
// =============================================================================

int main() {
    Suite s("umiboot_auth");

    s.section("Secure Compare");
    s.run("secure_compare_equal", test_secure_compare_equal);
    s.run("secure_compare_not_equal", test_secure_compare_not_equal);
    s.run("secure_compare_empty", test_secure_compare_empty);

    s.section("Authenticator");
    s.run("authenticator_initial_state", test_authenticator_initial_state);
    s.run("authenticator_challenge_generation", test_authenticator_challenge_generation);
    s.run("authenticator_verify_correct_response", test_authenticator_verify_correct_response);
    s.run("authenticator_verify_wrong_response", test_authenticator_verify_wrong_response);
    s.run("authenticator_session_timeout", test_authenticator_session_timeout);
    s.run("authenticator_logout", test_authenticator_logout);
    s.run("authenticator_refresh_session", test_authenticator_refresh_session);

    s.section("Auth Client");
    s.run("auth_client_compute_response", test_auth_client_compute_response);
    s.run("auth_client_no_hmac", test_auth_client_no_hmac);

    s.section("Protocol Constants");
    s.run("auth_commands_values", test_auth_commands_values);

    return s.summary();
}

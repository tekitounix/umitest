// SPDX-License-Identifier: MIT
// umi_boot Authentication Tests

#include "test_framework.hh"
#include <umiboot/auth.hh>

using namespace umiboot;
using namespace umiboot::test;

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

TEST(secure_compare_equal) {
    uint8_t a[] = {1, 2, 3, 4, 5};
    uint8_t b[] = {1, 2, 3, 4, 5};
    ASSERT(secure_compare(a, b, 5));
    TEST_PASS();
}

TEST(secure_compare_not_equal) {
    uint8_t a[] = {1, 2, 3, 4, 5};
    uint8_t b[] = {1, 2, 3, 4, 6};
    ASSERT(!secure_compare(a, b, 5));
    TEST_PASS();
}

TEST(secure_compare_empty) {
    uint8_t a[1] = {0};
    uint8_t b[1] = {0};
    ASSERT(secure_compare(a, b, 0));
    TEST_PASS();
}

TEST(authenticator_initial_state) {
    Authenticator<32, 60000> auth;
    uint8_t key[32] = {0};
    auth.init(key, mock_hmac, mock_rng);

    ASSERT_EQ(auth.state(), AuthState::IDLE);
    ASSERT_EQ(auth.last_error(), AuthError::OK);
    ASSERT(!auth.is_authenticated(0));
    TEST_PASS();
}

TEST(authenticator_challenge_generation) {
    Authenticator<32, 60000> auth;
    uint8_t key[32] = {0};
    auth.init(key, mock_hmac, mock_rng);

    uint8_t challenge[32];
    auth.generate_challenge(challenge);

    ASSERT_EQ(auth.state(), AuthState::CHALLENGE_SENT);

    // Verify challenge was generated (not all zeros)
    bool has_nonzero = false;
    for (int i = 0; i < 32; ++i) {
        if (challenge[i] != 0) has_nonzero = true;
    }
    ASSERT(has_nonzero);
    TEST_PASS();
}

TEST(authenticator_verify_correct_response) {
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
    ASSERT(auth.verify_response(response, 1000));
    ASSERT_EQ(auth.state(), AuthState::AUTHENTICATED);
    ASSERT(auth.is_authenticated(1000));
    TEST_PASS();
}

TEST(authenticator_verify_wrong_response) {
    Authenticator<32, 60000> auth;
    uint8_t key[32] = {1, 2, 3, 4};
    auth.init(key, mock_hmac, mock_rng);

    uint8_t challenge[32];
    auth.generate_challenge(challenge);

    // Wrong response
    uint8_t wrong_response[32] = {0};

    ASSERT(!auth.verify_response(wrong_response, 1000));
    ASSERT_EQ(auth.state(), AuthState::IDLE);
    ASSERT_EQ(auth.last_error(), AuthError::INVALID_RESPONSE);
    TEST_PASS();
}

TEST(authenticator_session_timeout) {
    Authenticator<32, 1000> auth;  // 1 second timeout
    uint8_t key[32] = {0};
    auth.init(key, mock_hmac, mock_rng);

    uint8_t challenge[32];
    auth.generate_challenge(challenge);

    uint8_t response[32];
    mock_hmac(key, 32, challenge, 32, response);
    auth.verify_response(response, 100);

    // Should be authenticated
    ASSERT(auth.is_authenticated(100));

    // After timeout
    ASSERT(!auth.is_authenticated(1200));
    TEST_PASS();
}

TEST(authenticator_logout) {
    Authenticator<32, 60000> auth;
    uint8_t key[32] = {0};
    auth.init(key, mock_hmac, mock_rng);

    uint8_t challenge[32];
    auth.generate_challenge(challenge);

    uint8_t response[32];
    mock_hmac(key, 32, challenge, 32, response);
    auth.verify_response(response, 1000);

    ASSERT(auth.is_authenticated(1000));

    auth.logout();

    ASSERT_EQ(auth.state(), AuthState::IDLE);
    ASSERT(!auth.is_authenticated(1000));
    TEST_PASS();
}

TEST(authenticator_refresh_session) {
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
    ASSERT(auth.is_authenticated(1400));

    // But not at 1600ms
    ASSERT(!auth.is_authenticated(1600));
    TEST_PASS();
}

TEST(auth_client_compute_response) {
    AuthClient<32> client;
    uint8_t key[32] = {1, 2, 3, 4};
    client.init(key, mock_hmac);

    uint8_t challenge[32] = {10, 20, 30};
    uint8_t response[32];

    ASSERT(client.compute_response(challenge, response));

    // Verify response matches expected
    uint8_t expected[32];
    mock_hmac(key, 32, challenge, 32, expected);
    ASSERT(std::memcmp(response, expected, 32) == 0);
    TEST_PASS();
}

TEST(auth_client_no_hmac) {
    AuthClient<32> client;
    uint8_t key[32] = {0};
    client.init(key, nullptr);  // No HMAC function

    uint8_t challenge[32] = {0};
    uint8_t response[32];

    ASSERT(!client.compute_response(challenge, response));
    TEST_PASS();
}

TEST(auth_commands_values) {
    ASSERT_EQ(static_cast<uint8_t>(AuthCommand::AUTH_CHALLENGE_REQ), 0x30);
    ASSERT_EQ(static_cast<uint8_t>(AuthCommand::AUTH_CHALLENGE), 0x31);
    ASSERT_EQ(static_cast<uint8_t>(AuthCommand::AUTH_RESPONSE), 0x32);
    ASSERT_EQ(static_cast<uint8_t>(AuthCommand::AUTH_OK), 0x33);
    ASSERT_EQ(static_cast<uint8_t>(AuthCommand::AUTH_FAIL), 0x34);
    TEST_PASS();
}

// =============================================================================
// Main
// =============================================================================

int main() {
    printf("umi_boot Authentication Tests\n");
    printf("==============================\n");

    SECTION("Secure Compare");
    RUN_TEST(secure_compare_equal);
    RUN_TEST(secure_compare_not_equal);
    RUN_TEST(secure_compare_empty);

    SECTION("Authenticator");
    RUN_TEST(authenticator_initial_state);
    RUN_TEST(authenticator_challenge_generation);
    RUN_TEST(authenticator_verify_correct_response);
    RUN_TEST(authenticator_verify_wrong_response);
    RUN_TEST(authenticator_session_timeout);
    RUN_TEST(authenticator_logout);
    RUN_TEST(authenticator_refresh_session);

    SECTION("Auth Client");
    RUN_TEST(auth_client_compute_response);
    RUN_TEST(auth_client_no_hmac);

    SECTION("Protocol Constants");
    RUN_TEST(auth_commands_values);

    return summary();
}

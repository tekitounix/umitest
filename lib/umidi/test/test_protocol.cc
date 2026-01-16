// SPDX-License-Identifier: MIT
// umidi Protocol Tests - SysEx Protocol, Auth, Session, Bootloader
#include "test_framework.hh"
#include "umidi/protocol/umi_sysex.hh"
#include "umidi/protocol/umi_auth.hh"
#include "umidi/protocol/umi_firmware.hh"
#include "umidi/protocol/umi_bootloader.hh"
#include "umidi/protocol/umi_session.hh"

using namespace umidi;
using namespace umidi::protocol;
using namespace umidi::test;

// =============================================================================
// 7-bit Encoding Tests
// =============================================================================

TEST(encoding_7bit_basic) {
    uint8_t input[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    uint8_t encoded[16];
    uint8_t decoded[16];

    size_t enc_len = encode_7bit(input, sizeof(input), encoded);
    ASSERT_EQ(enc_len, 8);  // 7 bytes -> 8 bytes

    size_t dec_len = decode_7bit(encoded, enc_len, decoded);
    ASSERT_EQ(dec_len, sizeof(input));

    for (size_t i = 0; i < sizeof(input); ++i) {
        ASSERT_EQ(decoded[i], input[i]);
    }
    TEST_PASS();
}

TEST(encoding_7bit_with_high_bits) {
    uint8_t input[] = {0x80, 0x81, 0xFF, 0x00, 0x7F};
    uint8_t encoded[16];
    uint8_t decoded[16];

    size_t enc_len = encode_7bit(input, sizeof(input), encoded);
    size_t dec_len = decode_7bit(encoded, enc_len, decoded);

    ASSERT_EQ(dec_len, sizeof(input));
    for (size_t i = 0; i < sizeof(input); ++i) {
        ASSERT_EQ(decoded[i], input[i]);
    }
    TEST_PASS();
}

TEST(encoding_7bit_various_lengths) {
    for (size_t len = 1; len <= 14; ++len) {
        uint8_t input[14];
        for (size_t i = 0; i < len; ++i) {
            input[i] = static_cast<uint8_t>(i * 17);  // Some pattern
        }

        uint8_t encoded[24];
        uint8_t decoded[14];

        size_t enc_len = encode_7bit(input, len, encoded);
        size_t dec_len = decode_7bit(encoded, enc_len, decoded);

        ASSERT_EQ(dec_len, len);
        for (size_t i = 0; i < len; ++i) {
            ASSERT_EQ(decoded[i], input[i]);
        }
    }
    TEST_PASS();
}

TEST(encoding_size_calculation) {
    ASSERT_EQ(encoded_size(7), 8);
    ASSERT_EQ(encoded_size(14), 16);
    ASSERT_EQ(encoded_size(1), 2);
    ASSERT_EQ(encoded_size(8), 10);  // 7+1 -> 8+2

    ASSERT_EQ(decoded_size(8), 7);
    ASSERT_EQ(decoded_size(16), 14);
    TEST_PASS();
}

// =============================================================================
// Checksum Tests
// =============================================================================

TEST(checksum_basic) {
    uint8_t data1[] = {0x01, 0x02, 0x03};
    uint8_t checksum1 = calculate_checksum(data1, sizeof(data1));

    // Same data should give same checksum
    uint8_t checksum1b = calculate_checksum(data1, sizeof(data1));
    ASSERT_EQ(checksum1, checksum1b);

    // Different data should give different checksum
    uint8_t data2[] = {0x01, 0x02, 0x04};
    uint8_t checksum2 = calculate_checksum(data2, sizeof(data2));
    ASSERT_NE(checksum1, checksum2);

    // Checksum should be 7-bit (< 128)
    ASSERT_LT(checksum1, 128);
    TEST_PASS();
}

// =============================================================================
// Message Builder Tests
// =============================================================================

TEST(message_builder_basic) {
    MessageBuilder<64> builder;

    builder.begin(Command::PING, 0);
    size_t len = builder.finalize();

    ASSERT_GT(len, 0);

    // Verify SysEx framing
    const uint8_t* data = builder.data();
    ASSERT_EQ(data[0], 0xF0);
    ASSERT_EQ(data[len - 1], 0xF7);
    TEST_PASS();
}

TEST(message_builder_with_data) {
    MessageBuilder<64> builder;

    builder.begin(Command::STDOUT_DATA, 5);
    uint8_t payload[] = {0x48, 0x65, 0x6C, 0x6C, 0x6F};  // "Hello"
    builder.add_data(payload, sizeof(payload));
    size_t len = builder.finalize();

    // Parse it back
    auto msg = parse_message(builder.data(), len);
    ASSERT(msg.valid);
    ASSERT_EQ(msg.command, Command::STDOUT_DATA);
    ASSERT_EQ(msg.sequence, 5);

    // Decode payload
    uint8_t decoded[16];
    size_t dec_len = msg.decode_payload(decoded, sizeof(decoded));
    ASSERT_EQ(dec_len, sizeof(payload));
    ASSERT_EQ(decoded[0], 'H');
    ASSERT_EQ(decoded[4], 'o');
    TEST_PASS();
}

TEST(message_builder_u32) {
    MessageBuilder<64> builder;

    builder.begin(Command::FW_BEGIN, 0);
    builder.add_u32(0x12345678);
    size_t len = builder.finalize();

    auto msg = parse_message(builder.data(), len);
    ASSERT(msg.valid);

    uint8_t decoded[8];
    size_t dec_len = msg.decode_payload(decoded, sizeof(decoded));
    ASSERT_EQ(dec_len, 4);

    uint32_t value = (uint32_t(decoded[0]) << 24) | (uint32_t(decoded[1]) << 16) |
                     (uint32_t(decoded[2]) << 8) | decoded[3];
    ASSERT_EQ(value, 0x12345678u);
    TEST_PASS();
}

// =============================================================================
// Message Parser Tests
// =============================================================================

TEST(message_parser_valid) {
    MessageBuilder<64> builder;
    builder.begin(Command::PONG, 42);
    size_t len = builder.finalize();

    auto msg = parse_message(builder.data(), len);

    ASSERT(msg.valid);
    ASSERT_EQ(msg.command, Command::PONG);
    ASSERT_EQ(msg.sequence, 42);
    TEST_PASS();
}

TEST(message_parser_invalid_framing) {
    uint8_t bad_start[] = {0x00, 0x7E, 0x7F, 0x00, 0x20, 0x00, 0x20, 0xF7};
    auto msg1 = parse_message(bad_start, sizeof(bad_start));
    ASSERT(!msg1.valid);

    uint8_t bad_end[] = {0xF0, 0x7E, 0x7F, 0x00, 0x20, 0x00, 0x20, 0x00};
    auto msg2 = parse_message(bad_end, sizeof(bad_end));
    ASSERT(!msg2.valid);
    TEST_PASS();
}

TEST(message_parser_invalid_checksum) {
    MessageBuilder<64> builder;
    builder.begin(Command::PING, 0);
    size_t len = builder.finalize();

    // Corrupt checksum
    uint8_t* data = const_cast<uint8_t*>(builder.data());
    data[len - 2] ^= 0x01;

    auto msg = parse_message(data, len);
    ASSERT(!msg.valid);
    TEST_PASS();
}

TEST(message_parser_too_short) {
    uint8_t short_msg[] = {0xF0, 0x7E, 0xF7};
    auto msg = parse_message(short_msg, sizeof(short_msg));
    ASSERT(!msg.valid);
    TEST_PASS();
}

// =============================================================================
// Standard IO Tests
// =============================================================================

TEST(stdio_basic) {
    StandardIO<128, 128> io;

    bool callback_called = false;

    io.set_stdin_callback([](const uint8_t* /*data*/, size_t /*len*/, void* ctx) {
        auto* called = static_cast<bool*>(ctx);
        *called = true;
    }, &callback_called);

    // Build STDIN message
    MessageBuilder<64> builder;
    builder.begin(Command::STDIN_DATA, 0);
    uint8_t test_data[] = {0x01, 0x02, 0x03};
    builder.add_data(test_data, sizeof(test_data));
    size_t len = builder.finalize();

    bool handled = io.process_message(builder.data(), len);
    ASSERT(handled);
    ASSERT(callback_called);
    TEST_PASS();
}

TEST(stdio_flow_control) {
    StandardIO<128, 128> io;

    ASSERT(!io.is_paused());

    // Build XOFF message
    MessageBuilder<16> builder;
    builder.begin(Command::FLOW_CTRL, 0);
    builder.add_byte(static_cast<uint8_t>(FlowControl::XOFF));
    size_t len = builder.finalize();

    io.process_message(builder.data(), len);
    ASSERT(io.is_paused());

    // Build XON message
    builder.begin(Command::FLOW_CTRL, 1);
    builder.add_byte(static_cast<uint8_t>(FlowControl::XON));
    len = builder.finalize();

    io.process_message(builder.data(), len);
    ASSERT(!io.is_paused());
    TEST_PASS();
}

TEST(stdio_reset) {
    StandardIO<128, 128> io;

    // Receive some messages, then reset
    io.reset();

    ASSERT(!io.is_paused());
    ASSERT(!io.eof());
    TEST_PASS();
}

// =============================================================================
// Firmware Update Tests
// =============================================================================

TEST(firmware_update_state_machine) {
    FirmwareUpdate<256> fw;

    ASSERT_EQ(fw.state(), UpdateState::IDLE);
    ASSERT_EQ(fw.received_bytes(), 0u);
    ASSERT_EQ(fw.total_size(), 0u);
    TEST_PASS();
}

TEST(firmware_header_size) {
    ASSERT_EQ(sizeof(FirmwareHeader), 128);
    TEST_PASS();
}

TEST(firmware_header_builder) {
    FirmwareHeaderBuilder builder;
    builder.version(1, 2, 3)
           .board("TEST_BOARD")
           .image_size(1024)
           .flags(FirmwareFlags::COMPRESSED);

    FirmwareHeader header = builder.build();

    ASSERT_EQ(header.magic, FIRMWARE_MAGIC);
    ASSERT_EQ(header.fw_version_major, 1u);
    ASSERT_EQ(header.fw_version_minor, 2u);
    ASSERT_EQ(header.fw_version_patch, 3u);
    ASSERT_EQ(header.image_size, 1024u);
    ASSERT(header.flags & static_cast<uint8_t>(FirmwareFlags::COMPRESSED));
    TEST_PASS();
}

TEST(firmware_version_compare) {
    uint32_t v1_0_0 = pack_version(1, 0, 0);
    uint32_t v1_0_1 = pack_version(1, 0, 1);
    uint32_t v1_1_0 = pack_version(1, 1, 0);
    uint32_t v2_0_0 = pack_version(2, 0, 0);

    ASSERT_LT(v1_0_0, v1_0_1);
    ASSERT_LT(v1_0_1, v1_1_0);
    ASSERT_LT(v1_1_0, v2_0_0);

    uint32_t v1_2_3 = pack_version(1, 2, 3);
    ASSERT_EQ(version_major(v1_2_3), 1);
    ASSERT_EQ(version_minor(v1_2_3), 2);
    ASSERT_EQ(version_patch(v1_2_3), 3);
    TEST_PASS();
}

TEST(firmware_validator_header) {
    FirmwareValidator<16> validator;
    validator.set_board_id("TEST_BOARD");

    FirmwareHeaderBuilder builder;
    builder.version(1, 0, 0)
           .board("TEST_BOARD")
           .image_size(1024);
    FirmwareHeader header = builder.build();

    auto result = validator.validate_header(header);
    ASSERT_EQ(result, ValidationResult::OK);
    TEST_PASS();
}

TEST(firmware_validator_board_mismatch) {
    FirmwareValidator<16> validator;
    validator.set_board_id("BOARD_A");

    FirmwareHeaderBuilder builder;
    builder.version(1, 0, 0)
           .board("BOARD_B")
           .image_size(1024);
    FirmwareHeader header = builder.build();

    auto result = validator.validate_header(header);
    ASSERT_EQ(result, ValidationResult::BOARD_MISMATCH);
    TEST_PASS();
}

// =============================================================================
// CRC32 Tests
// =============================================================================

TEST(crc32_calculation) {
    uint8_t data[] = "123456789";
    uint32_t crc = crc32_fn(data, 9);

    // CRC-32 of "123456789" should be 0xCBF43926
    ASSERT_EQ(crc, 0xCBF43926u);
    TEST_PASS();
}

TEST(crc32_different_data) {
    uint8_t data1[] = {0x01, 0x02, 0x03};
    uint8_t data2[] = {0x01, 0x02, 0x04};

    uint32_t crc1 = crc32_fn(data1, 3);
    uint32_t crc2 = crc32_fn(data2, 3);

    ASSERT_NE(crc1, crc2);
    TEST_PASS();
}

// =============================================================================
// Bootloader Tests
// =============================================================================

TEST(boot_config_size) {
    ASSERT_EQ(sizeof(BootConfig), 64);
    TEST_PASS();
}

TEST(platform_configs) {
    // Check platform configs exist and have valid regions
    ASSERT_GT(platforms::STM32F4_512K.slot_a.size, 0u);
    ASSERT_GT(platforms::STM32F4_512K.slot_b.size, 0u);
    ASSERT(platforms::STM32F4_512K.name != nullptr);
    TEST_PASS();
}

TEST(slot_regions) {
    // Verify slot A and B have same size
    ASSERT_EQ(platforms::STM32F4_512K.slot_a.size,
              platforms::STM32F4_512K.slot_b.size);
    TEST_PASS();
}

// =============================================================================
// Session Tests
// =============================================================================

TEST(session_timer_timeout) {
    SessionTimer timer;
    timer.init(DEFAULT_TIMEOUTS);

    timer.start_session(0);
    ASSERT_EQ(timer.check(100), TimeoutEvent::NONE);
    // Idle timeout check - activity recorded at start
    ASSERT_EQ(timer.check(59999), TimeoutEvent::NONE);
    ASSERT_EQ(timer.check(60001), TimeoutEvent::IDLE_TIMEOUT);
    TEST_PASS();
}

TEST(session_timer_chunk_timeout) {
    SessionTimer timer;
    timer.init(DEFAULT_TIMEOUTS);

    timer.start_session(1);          // Start at t=1
    timer.record_chunk(1);           // Record chunk at t=1 (must be > 0 for check)
    ASSERT_EQ(timer.check(5000), TimeoutEvent::NONE);
    ASSERT_EQ(timer.check(5002), TimeoutEvent::CHUNK_TIMEOUT);  // 5002 - 1 = 5001 >= 5000
    TEST_PASS();
}

TEST(flow_control_sender) {
    FlowControlSender<4> fc;
    fc.init(DEFAULT_FLOW);

    // Should allow window_size messages without ACK
    uint8_t data[] = {0x01};
    for (int i = 0; i < 4; ++i) {
        ASSERT(fc.can_send());
        int seq = fc.enqueue(data, 1);
        ASSERT(seq >= 0);  // enqueue returns sequence number or -1 on failure
    }

    // Window full
    ASSERT(!fc.can_send());

    // ACK allows more
    fc.process_ack(2);  // ACK up to seq 2
    ASSERT(fc.can_send());
    TEST_PASS();
}

TEST(flow_control_receiver) {
    FlowControlReceiver fc;
    fc.reset();

    // Receive in order
    ASSERT(fc.process_packet(0));
    ASSERT(fc.process_packet(1));
    ASSERT(fc.process_packet(2));

    // Out of order - future packet (should be accepted but buffered)
    ASSERT(fc.process_packet(4));

    // Duplicate - reject
    ASSERT(!fc.process_packet(1));
    TEST_PASS();
}

// =============================================================================
// Authentication Tests
// =============================================================================

TEST(auth_state_machine) {
    Authenticator<32, 300000> auth;

    // Not authenticated initially
    ASSERT(!auth.is_authenticated(0));

    // Generate challenge
    uint8_t challenge[32];
    auth.generate_challenge(challenge);

    // Verify with wrong response (should fail)
    uint8_t bad_response[32] = {0};
    ASSERT(!auth.verify_response(bad_response, 0));
    TEST_PASS();
}

TEST(auth_logout) {
    Authenticator<32, 300000> auth;

    auth.logout();
    ASSERT(!auth.is_authenticated(0));
    TEST_PASS();
}

TEST(secure_compare) {
    uint8_t a[] = {1, 2, 3, 4, 5};
    uint8_t b[] = {1, 2, 3, 4, 5};
    uint8_t c[] = {1, 2, 3, 4, 6};

    ASSERT(secure_compare(a, b, 5));
    ASSERT(!secure_compare(a, c, 5));
    TEST_PASS();
}

// =============================================================================
// Main
// =============================================================================

int main() {
    printf("=== umidi Protocol Tests ===\n");

    SECTION("7-bit Encoding");
    RUN_TEST(encoding_7bit_basic);
    RUN_TEST(encoding_7bit_with_high_bits);
    RUN_TEST(encoding_7bit_various_lengths);
    RUN_TEST(encoding_size_calculation);

    SECTION("Checksum");
    RUN_TEST(checksum_basic);

    SECTION("Message Builder");
    RUN_TEST(message_builder_basic);
    RUN_TEST(message_builder_with_data);
    RUN_TEST(message_builder_u32);

    SECTION("Message Parser");
    RUN_TEST(message_parser_valid);
    RUN_TEST(message_parser_invalid_framing);
    RUN_TEST(message_parser_invalid_checksum);
    RUN_TEST(message_parser_too_short);

    SECTION("Standard IO");
    RUN_TEST(stdio_basic);
    RUN_TEST(stdio_flow_control);
    RUN_TEST(stdio_reset);

    SECTION("Firmware Update");
    RUN_TEST(firmware_update_state_machine);
    RUN_TEST(firmware_header_size);
    RUN_TEST(firmware_header_builder);
    RUN_TEST(firmware_version_compare);
    RUN_TEST(firmware_validator_header);
    RUN_TEST(firmware_validator_board_mismatch);

    SECTION("CRC32");
    RUN_TEST(crc32_calculation);
    RUN_TEST(crc32_different_data);

    SECTION("Bootloader");
    RUN_TEST(boot_config_size);
    RUN_TEST(platform_configs);
    RUN_TEST(slot_regions);

    SECTION("Session");
    RUN_TEST(session_timer_timeout);
    RUN_TEST(session_timer_chunk_timeout);
    RUN_TEST(flow_control_sender);
    RUN_TEST(flow_control_receiver);

    SECTION("Authentication");
    RUN_TEST(auth_state_machine);
    RUN_TEST(auth_logout);
    RUN_TEST(secure_compare);

    return summary();
}

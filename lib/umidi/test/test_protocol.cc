// SPDX-License-Identifier: MIT
// umidi Protocol Tests - SysEx Protocol, Auth, Session, Bootloader
#include <umitest.hh>
#include "protocol/umi_sysex.hh"
#include "protocol/umi_auth.hh"
#include "protocol/umi_firmware.hh"
#include "protocol/umi_bootloader.hh"
#include "protocol/umi_session.hh"

using namespace umidi;
using namespace umidi::protocol;
using namespace umitest;

// =============================================================================
// 7-bit Encoding Tests
// =============================================================================

bool test_encoding_7bit_basic(TestContext& t) {
    uint8_t input[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    uint8_t encoded[16];
    uint8_t decoded[16];

    size_t enc_len = encode_7bit(input, sizeof(input), encoded);
    t.assert_eq(enc_len, 8);  // 7 bytes -> 8 bytes

    size_t dec_len = decode_7bit(encoded, enc_len, decoded);
    t.assert_eq(dec_len, sizeof(input));

    for (size_t i = 0; i < sizeof(input); ++i) {
        t.assert_eq(decoded[i], input[i]);
    }
    return true;
}

bool test_encoding_7bit_with_high_bits(TestContext& t) {
    uint8_t input[] = {0x80, 0x81, 0xFF, 0x00, 0x7F};
    uint8_t encoded[16];
    uint8_t decoded[16];

    size_t enc_len = encode_7bit(input, sizeof(input), encoded);
    size_t dec_len = decode_7bit(encoded, enc_len, decoded);

    t.assert_eq(dec_len, sizeof(input));
    for (size_t i = 0; i < sizeof(input); ++i) {
        t.assert_eq(decoded[i], input[i]);
    }
    return true;
}

bool test_encoding_7bit_various_lengths(TestContext& t) {
    for (size_t len = 1; len <= 14; ++len) {
        uint8_t input[14];
        for (size_t i = 0; i < len; ++i) {
            input[i] = static_cast<uint8_t>(i * 17);  // Some pattern
        }

        uint8_t encoded[24];
        uint8_t decoded[14];

        size_t enc_len = encode_7bit(input, len, encoded);
        size_t dec_len = decode_7bit(encoded, enc_len, decoded);

        t.assert_eq(dec_len, len);
        for (size_t i = 0; i < len; ++i) {
            t.assert_eq(decoded[i], input[i]);
        }
    }
    return true;
}

bool test_encoding_size_calculation(TestContext& t) {
    t.assert_eq(encoded_size(7), 8);
    t.assert_eq(encoded_size(14), 16);
    t.assert_eq(encoded_size(1), 2);
    t.assert_eq(encoded_size(8), 10);  // 7+1 -> 8+2

    t.assert_eq(decoded_size(8), 7);
    t.assert_eq(decoded_size(16), 14);
    return true;
}

// =============================================================================
// Checksum Tests
// =============================================================================

bool test_checksum_basic(TestContext& t) {
    uint8_t data1[] = {0x01, 0x02, 0x03};
    uint8_t checksum1 = calculate_checksum(data1, sizeof(data1));

    // Same data should give same checksum
    uint8_t checksum1b = calculate_checksum(data1, sizeof(data1));
    t.assert_eq(checksum1, checksum1b);

    // Different data should give different checksum
    uint8_t data2[] = {0x01, 0x02, 0x04};
    uint8_t checksum2 = calculate_checksum(data2, sizeof(data2));
    t.assert_ne(checksum1, checksum2);

    // Checksum should be 7-bit (< 128)
    t.assert_lt(checksum1, 128);
    return true;
}

// =============================================================================
// Message Builder Tests
// =============================================================================

bool test_message_builder_basic(TestContext& t) {
    MessageBuilder<64> builder;

    builder.begin(Command::PING, 0);
    size_t len = builder.finalize();

    t.assert_gt(len, 0);

    // Verify SysEx framing
    const uint8_t* data = builder.data();
    t.assert_eq(data[0], 0xF0);
    t.assert_eq(data[len - 1], 0xF7);
    return true;
}

bool test_message_builder_with_data(TestContext& t) {
    MessageBuilder<64> builder;

    builder.begin(Command::STDOUT_DATA, 5);
    uint8_t payload[] = {0x48, 0x65, 0x6C, 0x6C, 0x6F};  // "Hello"
    builder.add_data(payload, sizeof(payload));
    size_t len = builder.finalize();

    // Parse it back
    auto msg = parse_message(builder.data(), len);
    t.assert_true(msg.valid);
    t.assert_eq(msg.command, Command::STDOUT_DATA);
    t.assert_eq(msg.sequence, 5);

    // Decode payload
    uint8_t decoded[16];
    size_t dec_len = msg.decode_payload(decoded, sizeof(decoded));
    t.assert_eq(dec_len, sizeof(payload));
    t.assert_eq(decoded[0], 'H');
    t.assert_eq(decoded[4], 'o');
    return true;
}

bool test_message_builder_u32(TestContext& t) {
    MessageBuilder<64> builder;

    builder.begin(Command::FW_BEGIN, 0);
    builder.add_u32(0x12345678);
    size_t len = builder.finalize();

    auto msg = parse_message(builder.data(), len);
    t.assert_true(msg.valid);

    uint8_t decoded[8];
    size_t dec_len = msg.decode_payload(decoded, sizeof(decoded));
    t.assert_eq(dec_len, 4);

    uint32_t value = (uint32_t(decoded[0]) << 24) | (uint32_t(decoded[1]) << 16) |
                     (uint32_t(decoded[2]) << 8) | decoded[3];
    t.assert_eq(value, 0x12345678u);
    return true;
}

// =============================================================================
// Message Parser Tests
// =============================================================================

bool test_message_parser_valid(TestContext& t) {
    MessageBuilder<64> builder;
    builder.begin(Command::PONG, 42);
    size_t len = builder.finalize();

    auto msg = parse_message(builder.data(), len);

    t.assert_true(msg.valid);
    t.assert_eq(msg.command, Command::PONG);
    t.assert_eq(msg.sequence, 42);
    return true;
}

bool test_message_parser_invalid_framing(TestContext& t) {
    uint8_t bad_start[] = {0x00, 0x7E, 0x7F, 0x00, 0x20, 0x00, 0x20, 0xF7};
    auto msg1 = parse_message(bad_start, sizeof(bad_start));
    t.assert_true(!msg1.valid);

    uint8_t bad_end[] = {0xF0, 0x7E, 0x7F, 0x00, 0x20, 0x00, 0x20, 0x00};
    auto msg2 = parse_message(bad_end, sizeof(bad_end));
    t.assert_true(!msg2.valid);
    return true;
}

bool test_message_parser_invalid_checksum(TestContext& t) {
    MessageBuilder<64> builder;
    builder.begin(Command::PING, 0);
    size_t len = builder.finalize();

    // Corrupt checksum
    uint8_t* data = const_cast<uint8_t*>(builder.data());
    data[len - 2] ^= 0x01;

    auto msg = parse_message(data, len);
    t.assert_true(!msg.valid);
    return true;
}

bool test_message_parser_too_short(TestContext& t) {
    uint8_t short_msg[] = {0xF0, 0x7E, 0xF7};
    auto msg = parse_message(short_msg, sizeof(short_msg));
    t.assert_true(!msg.valid);
    return true;
}

// =============================================================================
// Standard IO Tests
// =============================================================================

bool test_stdio_basic(TestContext& t) {
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
    t.assert_true(handled);
    t.assert_true(callback_called);
    return true;
}

bool test_stdio_flow_control(TestContext& t) {
    StandardIO<128, 128> io;

    t.assert_true(!io.is_paused());

    // Build XOFF message
    MessageBuilder<16> builder;
    builder.begin(Command::FLOW_CTRL, 0);
    builder.add_byte(static_cast<uint8_t>(FlowControl::XOFF));
    size_t len = builder.finalize();

    io.process_message(builder.data(), len);
    t.assert_true(io.is_paused());

    // Build XON message
    builder.begin(Command::FLOW_CTRL, 1);
    builder.add_byte(static_cast<uint8_t>(FlowControl::XON));
    len = builder.finalize();

    io.process_message(builder.data(), len);
    t.assert_true(!io.is_paused());
    return true;
}

bool test_stdio_reset(TestContext& t) {
    StandardIO<128, 128> io;

    // Receive some messages, then reset
    io.reset();

    t.assert_true(!io.is_paused());
    t.assert_true(!io.eof());
    return true;
}

// =============================================================================
// Firmware Update Tests
// =============================================================================

bool test_firmware_update_state_machine(TestContext& t) {
    FirmwareUpdate<256> fw;

    t.assert_eq(fw.state(), UpdateState::IDLE);
    t.assert_eq(fw.received_bytes(), 0u);
    t.assert_eq(fw.total_size(), 0u);
    return true;
}

bool test_firmware_header_size(TestContext& t) {
    t.assert_eq(sizeof(FirmwareHeader), 128);
    return true;
}

bool test_firmware_header_builder(TestContext& t) {
    FirmwareHeaderBuilder builder;
    builder.version(1, 2, 3)
           .board("TEST_BOARD")
           .image_size(1024)
           .flags(FirmwareFlags::COMPRESSED);

    FirmwareHeader header = builder.build();

    t.assert_eq(header.magic, FIRMWARE_MAGIC);
    t.assert_eq(header.fw_version_major, 1u);
    t.assert_eq(header.fw_version_minor, 2u);
    t.assert_eq(header.fw_version_patch, 3u);
    t.assert_eq(header.image_size, 1024u);
    t.assert_true(header.flags & static_cast<uint8_t>(FirmwareFlags::COMPRESSED));
    return true;
}

bool test_firmware_version_compare(TestContext& t) {
    uint32_t v1_0_0 = pack_version(1, 0, 0);
    uint32_t v1_0_1 = pack_version(1, 0, 1);
    uint32_t v1_1_0 = pack_version(1, 1, 0);
    uint32_t v2_0_0 = pack_version(2, 0, 0);

    t.assert_lt(v1_0_0, v1_0_1);
    t.assert_lt(v1_0_1, v1_1_0);
    t.assert_lt(v1_1_0, v2_0_0);

    uint32_t v1_2_3 = pack_version(1, 2, 3);
    t.assert_eq(version_major(v1_2_3), 1);
    t.assert_eq(version_minor(v1_2_3), 2);
    t.assert_eq(version_patch(v1_2_3), 3);
    return true;
}

bool test_firmware_validator_header(TestContext& t) {
    FirmwareValidator<16> validator;
    validator.set_board_id("TEST_BOARD");

    FirmwareHeaderBuilder builder;
    builder.version(1, 0, 0)
           .board("TEST_BOARD")
           .image_size(1024);
    FirmwareHeader header = builder.build();

    auto result = validator.validate_header(header);
    t.assert_eq(result, ValidationResult::OK);
    return true;
}

bool test_firmware_validator_board_mismatch(TestContext& t) {
    FirmwareValidator<16> validator;
    validator.set_board_id("BOARD_A");

    FirmwareHeaderBuilder builder;
    builder.version(1, 0, 0)
           .board("BOARD_B")
           .image_size(1024);
    FirmwareHeader header = builder.build();

    auto result = validator.validate_header(header);
    t.assert_eq(result, ValidationResult::BOARD_MISMATCH);
    return true;
}

// =============================================================================
// CRC32 Tests
// =============================================================================

bool test_crc32_calculation(TestContext& t) {
    uint8_t data[] = "123456789";
    uint32_t crc = crc32_fn(data, 9);

    // CRC-32 of "123456789" should be 0xCBF43926
    t.assert_eq(crc, 0xCBF43926u);
    return true;
}

bool test_crc32_different_data(TestContext& t) {
    uint8_t data1[] = {0x01, 0x02, 0x03};
    uint8_t data2[] = {0x01, 0x02, 0x04};

    uint32_t crc1 = crc32_fn(data1, 3);
    uint32_t crc2 = crc32_fn(data2, 3);

    t.assert_ne(crc1, crc2);
    return true;
}

// =============================================================================
// Bootloader Tests
// =============================================================================

bool test_boot_config_size(TestContext& t) {
    t.assert_eq(sizeof(BootConfig), 64);
    return true;
}

bool test_platform_configs(TestContext& t) {
    // Check platform configs exist and have valid regions
    t.assert_gt(platforms::STM32F4_512K.slot_a.size, 0u);
    t.assert_gt(platforms::STM32F4_512K.slot_b.size, 0u);
    t.assert_true(platforms::STM32F4_512K.name != nullptr);
    return true;
}

bool test_slot_regions(TestContext& t) {
    // Verify slot A and B have same size
    t.assert_eq(platforms::STM32F4_512K.slot_a.size,
              platforms::STM32F4_512K.slot_b.size);
    return true;
}

// =============================================================================
// Session Tests
// =============================================================================

bool test_session_timer_timeout(TestContext& t) {
    SessionTimer timer;
    timer.init(DEFAULT_TIMEOUTS);

    timer.start_session(0);
    t.assert_eq(timer.check(100), TimeoutEvent::NONE);
    // Idle timeout check - activity recorded at start
    t.assert_eq(timer.check(59999), TimeoutEvent::NONE);
    t.assert_eq(timer.check(60001), TimeoutEvent::IDLE_TIMEOUT);
    return true;
}

bool test_session_timer_chunk_timeout(TestContext& t) {
    SessionTimer timer;
    timer.init(DEFAULT_TIMEOUTS);

    timer.start_session(1);          // Start at t=1
    timer.record_chunk(1);           // Record chunk at t=1 (must be > 0 for check)
    t.assert_eq(timer.check(5000), TimeoutEvent::NONE);
    t.assert_eq(timer.check(5002), TimeoutEvent::CHUNK_TIMEOUT);  // 5002 - 1 = 5001 >= 5000
    return true;
}

bool test_flow_control_sender(TestContext& t) {
    FlowControlSender<4> fc;
    fc.init(DEFAULT_FLOW);

    // Should allow window_size messages without ACK
    uint8_t data[] = {0x01};
    for (int i = 0; i < 4; ++i) {
        t.assert_true(fc.can_send());
        int seq = fc.enqueue(data, 1);
        t.assert_true(seq >= 0);  // enqueue returns sequence number or -1 on failure
    }

    // Window full
    t.assert_true(!fc.can_send());

    // ACK allows more
    fc.process_ack(2);  // ACK up to seq 2
    t.assert_true(fc.can_send());
    return true;
}

bool test_flow_control_receiver(TestContext& t) {
    FlowControlReceiver fc;
    fc.reset();

    // Receive in order
    t.assert_true(fc.process_packet(0));
    t.assert_true(fc.process_packet(1));
    t.assert_true(fc.process_packet(2));

    // Out of order - future packet (should be accepted but buffered)
    t.assert_true(fc.process_packet(4));

    // Duplicate - reject
    t.assert_true(!fc.process_packet(1));
    return true;
}

// =============================================================================
// Authentication Tests
// =============================================================================

bool test_auth_state_machine(TestContext& t) {
    Authenticator<32, 300000> auth;

    // Not authenticated initially
    t.assert_true(!auth.is_authenticated(0));

    // Generate challenge
    uint8_t challenge[32];
    auth.generate_challenge(challenge);

    // Verify with wrong response (should fail)
    uint8_t bad_response[32] = {0};
    t.assert_true(!auth.verify_response(bad_response, 0));
    return true;
}

bool test_auth_logout(TestContext& t) {
    Authenticator<32, 300000> auth;

    auth.logout();
    t.assert_true(!auth.is_authenticated(0));
    return true;
}

bool test_secure_compare(TestContext& t) {
    uint8_t a[] = {1, 2, 3, 4, 5};
    uint8_t b[] = {1, 2, 3, 4, 5};
    uint8_t c[] = {1, 2, 3, 4, 6};

    t.assert_true(secure_compare(a, b, 5));
    t.assert_true(!secure_compare(a, c, 5));
    return true;
}

// =============================================================================
// Main
// =============================================================================

int main() {
    Suite s("umidi_protocol");

    s.section("7-bit Encoding");
    s.run("encoding_7bit_basic", test_encoding_7bit_basic);
    s.run("encoding_7bit_with_high_bits", test_encoding_7bit_with_high_bits);
    s.run("encoding_7bit_various_lengths", test_encoding_7bit_various_lengths);
    s.run("encoding_size_calculation", test_encoding_size_calculation);

    s.section("Checksum");
    s.run("checksum_basic", test_checksum_basic);

    s.section("Message Builder");
    s.run("message_builder_basic", test_message_builder_basic);
    s.run("message_builder_with_data", test_message_builder_with_data);
    s.run("message_builder_u32", test_message_builder_u32);

    s.section("Message Parser");
    s.run("message_parser_valid", test_message_parser_valid);
    s.run("message_parser_invalid_framing", test_message_parser_invalid_framing);
    s.run("message_parser_invalid_checksum", test_message_parser_invalid_checksum);
    s.run("message_parser_too_short", test_message_parser_too_short);

    s.section("Standard IO");
    s.run("stdio_basic", test_stdio_basic);
    s.run("stdio_flow_control", test_stdio_flow_control);
    s.run("stdio_reset", test_stdio_reset);

    s.section("Firmware Update");
    s.run("firmware_update_state_machine", test_firmware_update_state_machine);
    s.run("firmware_header_size", test_firmware_header_size);
    s.run("firmware_header_builder", test_firmware_header_builder);
    s.run("firmware_version_compare", test_firmware_version_compare);
    s.run("firmware_validator_header", test_firmware_validator_header);
    s.run("firmware_validator_board_mismatch", test_firmware_validator_board_mismatch);

    s.section("CRC32");
    s.run("crc32_calculation", test_crc32_calculation);
    s.run("crc32_different_data", test_crc32_different_data);

    s.section("Bootloader");
    s.run("boot_config_size", test_boot_config_size);
    s.run("platform_configs", test_platform_configs);
    s.run("slot_regions", test_slot_regions);

    s.section("Session");
    s.run("session_timer_timeout", test_session_timer_timeout);
    s.run("session_timer_chunk_timeout", test_session_timer_chunk_timeout);
    s.run("flow_control_sender", test_flow_control_sender);
    s.run("flow_control_receiver", test_flow_control_receiver);

    s.section("Authentication");
    s.run("auth_state_machine", test_auth_state_machine);
    s.run("auth_logout", test_auth_logout);
    s.run("secure_compare", test_secure_compare);

    return s.summary();
}

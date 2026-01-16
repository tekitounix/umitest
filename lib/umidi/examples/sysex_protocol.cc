// SPDX-License-Identifier: MIT
/// @file sysex_protocol.cc
/// @brief Example: UMI SysEx protocol usage
///
/// This example demonstrates the UMI SysEx protocol for
/// bidirectional communication over MIDI.

#include "umidi/protocol/commands.hh"
#include "umidi/protocol/message.hh"
#include "umidi/protocol/encoding.hh"

#include <cstdio>
#include <cstring>

// Mock MIDI send function
void midi_send(const uint8_t* data, size_t len) {
    printf("TX [%zu bytes]: F0", len);
    for (size_t i = 1; i < len - 1 && i < 16; ++i) {
        printf(" %02X", data[i]);
    }
    if (len > 16) printf(" ...");
    printf(" F7\n");
}

int main() {
    using namespace umidi::protocol;

    printf("=== SysEx Protocol Example ===\n\n");

    // 1. Build a simple ping message
    printf("--- Building PING message ---\n");
    MessageBuilder<64> builder;
    builder.begin(Command::PING, 0x01);
    size_t len = builder.finalize();
    midi_send(builder.data(), len);

    // 2. Build a message with payload
    printf("\n--- Building message with data ---\n");
    uint8_t payload[] = {0x01, 0x02, 0x80, 0xFF};  // Contains high bits

    builder.begin(Command::STDOUT_DATA, 0x02);
    builder.add_data(payload, sizeof(payload));
    len = builder.finalize();

    printf("Original data: ");
    for (auto b : payload) printf("%02X ", b);
    printf("\n");

    midi_send(builder.data(), len);

    // 3. Parse a received message
    printf("\n--- Parsing received message ---\n");

    // Simulated received PING response (PONG)
    uint8_t rx_data[] = {
        0xF0,              // SysEx start
        0x7E, 0x7F, 0x00,  // UMI ID
        0x21,              // PONG command
        0x01,              // Sequence
        0x20,              // Checksum
        0xF7               // SysEx end
    };

    auto msg = parse_message(rx_data, sizeof(rx_data));
    if (msg.valid) {
        printf("Valid message received:\n");
        printf("  Command: 0x%02X\n", static_cast<uint8_t>(msg.command));
        printf("  Sequence: %d\n", msg.sequence);
        printf("  Payload length: %zu\n", msg.payload_len);
    } else {
        printf("Invalid message\n");
    }

    // 4. 7-bit encoding example
    printf("\n--- 7-bit Encoding Example ---\n");

    uint8_t raw_data[] = {0x00, 0x7F, 0x80, 0xFF, 0x55, 0xAA};
    uint8_t encoded[16];
    uint8_t decoded[8];

    size_t enc_len = encode_7bit(raw_data, sizeof(raw_data), encoded);
    printf("Encoded %zu bytes -> %zu bytes\n", sizeof(raw_data), enc_len);

    size_t dec_len = decode_7bit(encoded, enc_len, decoded);
    printf("Decoded back to %zu bytes\n", dec_len);

    bool match = (dec_len == sizeof(raw_data)) &&
                 (memcmp(raw_data, decoded, dec_len) == 0);
    printf("Round-trip: %s\n", match ? "OK" : "FAILED");

    return 0;
}

// SPDX-License-Identifier: MIT
// umidi Comprehensive Test Suite
// Tests all MIDI message types for correctness

#include <umidi/umidi.hh>
#include <cstdio>
#include <cstring>
#include <vector>

// =============================================================================
// Test Framework
// =============================================================================

static int tests_passed = 0;
static int tests_failed = 0;
static const char* current_section = "";

#define SECTION(name) do { current_section = name; printf("\n%s:\n", name); } while(0)

#define TEST(name) static void test_##name()
#define RUN_TEST(name) do { \
    printf("  " #name "... "); \
    test_##name(); \
    printf("OK\n"); \
    tests_passed++; \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    Assertion failed: %s\n    at %s:%d\n", #cond, __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))

// =============================================================================
// 1. Channel Voice Messages (全7種)
// =============================================================================

// 1.1 Note Off (0x80)
TEST(channel_note_off_all_channels) {
    for (uint8_t ch = 0; ch < 16; ++ch) {
        auto ump = umidi::UMP32::note_off(ch, 60, 64);
        ASSERT(ump.is_note_off());
        ASSERT(!ump.is_note_on());
        ASSERT_EQ(ump.channel(), ch);
        ASSERT_EQ(ump.note(), 60);
        ASSERT_EQ(ump.velocity(), 64);
    }
}

TEST(channel_note_off_all_notes) {
    for (uint8_t note = 0; note < 128; ++note) {
        auto ump = umidi::UMP32::note_off(0, note, 0);
        ASSERT(ump.is_note_off());
        ASSERT_EQ(ump.note(), note);
    }
}

// 1.2 Note On (0x90)
TEST(channel_note_on_all_channels) {
    for (uint8_t ch = 0; ch < 16; ++ch) {
        auto ump = umidi::UMP32::note_on(ch, 60, 100);
        ASSERT(ump.is_note_on());
        ASSERT_EQ(ump.channel(), ch);
    }
}

TEST(channel_note_on_all_velocities) {
    // Velocity 0 should be Note Off equivalent
    auto ump0 = umidi::UMP32::note_on(0, 60, 0);
    ASSERT(ump0.is_note_off());
    ASSERT(!ump0.is_note_on());

    // Velocity 1-127 should be Note On
    for (uint8_t vel = 1; vel < 128; ++vel) {
        auto ump = umidi::UMP32::note_on(0, 60, vel);
        ASSERT(ump.is_note_on());
        ASSERT_EQ(ump.velocity(), vel);
    }
}

// 1.3 Polyphonic Key Pressure (0xA0)
TEST(channel_poly_pressure) {
    for (uint8_t ch = 0; ch < 16; ++ch) {
        umidi::UMP32 ump(2, 0, 0xA0 | ch, 60, 100);
        ASSERT(ump.is_poly_pressure());
        ASSERT_EQ(ump.channel(), ch);
        ASSERT_EQ(ump.note(), 60);
        ASSERT_EQ(ump.data2(), 100);
    }
}

// 1.4 Control Change (0xB0)
TEST(channel_cc_all_controllers) {
    for (uint8_t cc = 0; cc < 128; ++cc) {
        auto ump = umidi::UMP32::cc(0, cc, 64);
        ASSERT(ump.is_cc());
        ASSERT_EQ(ump.cc_number(), cc);
        ASSERT_EQ(ump.cc_value(), 64);
    }
}

TEST(channel_cc_all_values) {
    for (uint8_t val = 0; val < 128; ++val) {
        auto ump = umidi::UMP32::cc(0, 7, val);
        ASSERT(ump.is_cc());
        ASSERT_EQ(ump.cc_value(), val);
    }
}

// 1.5 Program Change (0xC0) - 2バイトメッセージ
TEST(channel_program_change) {
    for (uint8_t prog = 0; prog < 128; ++prog) {
        auto ump = umidi::UMP32::program_change(0, prog);
        ASSERT(ump.is_program_change());
        ASSERT_EQ(ump.data1(), prog);
    }
}

// 1.6 Channel Pressure (0xD0) - 2バイトメッセージ
TEST(channel_pressure) {
    for (uint8_t pressure = 0; pressure < 128; ++pressure) {
        umidi::UMP32 ump(2, 0, 0xD0, pressure, 0);
        ASSERT(ump.is_channel_pressure());
        ASSERT_EQ(ump.data1(), pressure);
    }
}

// 1.7 Pitch Bend (0xE0) - 14ビット値
TEST(channel_pitch_bend_range) {
    // Min
    auto ump_min = umidi::UMP32::pitch_bend(0, 0);
    ASSERT(ump_min.is_pitch_bend());
    ASSERT_EQ(ump_min.pitch_bend_value(), 0);

    // Center
    auto ump_center = umidi::UMP32::pitch_bend(0, 8192);
    ASSERT_EQ(ump_center.pitch_bend_value(), 8192);

    // Max
    auto ump_max = umidi::UMP32::pitch_bend(0, 16383);
    ASSERT_EQ(ump_max.pitch_bend_value(), 16383);
}

// =============================================================================
// 2. System Common Messages
// =============================================================================

// 2.1 MTC Quarter Frame (0xF1)
TEST(system_mtc_quarter_frame) {
    auto msg = umidi::message::MidiTimeCode::create(0x71);  // type=7, value=1
    ASSERT(msg.is_valid());
    ASSERT_EQ(msg.ump.status(), 0xF1);
    ASSERT_EQ(msg.mtc_type(), 7);
    ASSERT_EQ(msg.mtc_value(), 1);
}

// 2.2 Song Position Pointer (0xF2)
TEST(system_song_position) {
    // 14-bit value: 0-16383
    auto msg = umidi::message::SongPosition::create(8192);
    ASSERT(msg.is_valid());
    ASSERT_EQ(msg.position(), 8192);

    auto msg_max = umidi::message::SongPosition::create(16383);
    ASSERT_EQ(msg_max.position(), 16383);
}

// 2.3 Song Select (0xF3)
TEST(system_song_select) {
    for (uint8_t song = 0; song < 128; ++song) {
        auto msg = umidi::message::SongSelect::create(song);
        ASSERT(msg.is_valid());
        ASSERT_EQ(msg.song(), song);
    }
}

// 2.4 Tune Request (0xF6)
TEST(system_tune_request) {
    auto msg = umidi::message::TuneRequest::create();
    ASSERT(msg.is_valid());
    ASSERT_EQ(msg.ump.status(), 0xF6);
}

// =============================================================================
// 3. System Real-Time Messages (全8種、うち2つは未定義)
// =============================================================================

TEST(realtime_timing_clock) {
    auto ump = umidi::UMP32::timing_clock();
    ASSERT(ump.is_realtime());
    ASSERT_EQ(ump.status(), 0xF8);
}

TEST(realtime_start) {
    auto ump = umidi::UMP32::start();
    ASSERT(ump.is_realtime());
    ASSERT_EQ(ump.status(), 0xFA);
}

TEST(realtime_continue) {
    auto ump = umidi::UMP32::continue_msg();
    ASSERT(ump.is_realtime());
    ASSERT_EQ(ump.status(), 0xFB);
}

TEST(realtime_stop) {
    auto ump = umidi::UMP32::stop();
    ASSERT(ump.is_realtime());
    ASSERT_EQ(ump.status(), 0xFC);
}

TEST(realtime_active_sensing) {
    auto msg = umidi::message::ActiveSensing::create();
    ASSERT(msg.is_valid());
    ASSERT_EQ(msg.ump.status(), 0xFE);
}

TEST(realtime_system_reset) {
    auto msg = umidi::message::SystemReset::create();
    ASSERT(msg.is_valid());
    ASSERT_EQ(msg.ump.status(), 0xFF);
}

// =============================================================================
// 4. Parser Round-Trip Tests
// =============================================================================

TEST(parser_roundtrip_note_on) {
    umidi::Parser parser;
    umidi::UMP32 ump;

    // Parse Note On C4 vel=100
    ASSERT(!parser.parse(0x90, ump));
    ASSERT(!parser.parse(60, ump));
    ASSERT(parser.parse(100, ump));

    // Serialize back
    uint8_t out[3];
    size_t len = umidi::Serializer::serialize(ump, out);
    ASSERT_EQ(len, 3u);
    ASSERT_EQ(out[0], 0x90);
    ASSERT_EQ(out[1], 60);
    ASSERT_EQ(out[2], 100);
}

TEST(parser_roundtrip_program_change) {
    umidi::Parser parser;
    umidi::UMP32 ump;

    ASSERT(!parser.parse(0xC5, ump));  // PC on channel 5
    ASSERT(parser.parse(42, ump));      // Program 42

    uint8_t out[3];
    size_t len = umidi::Serializer::serialize(ump, out);
    ASSERT_EQ(len, 2u);
    ASSERT_EQ(out[0], 0xC5);
    ASSERT_EQ(out[1], 42);
}

TEST(parser_roundtrip_pitch_bend) {
    umidi::Parser parser;
    umidi::UMP32 ump;

    // Pitch bend value 8192 (center)
    // LSB = 8192 & 0x7F = 0
    // MSB = (8192 >> 7) & 0x7F = 64
    ASSERT(!parser.parse(0xE0, ump));
    ASSERT(!parser.parse(0, ump));    // LSB
    ASSERT(parser.parse(64, ump));    // MSB

    ASSERT(ump.is_pitch_bend());
    ASSERT_EQ(ump.pitch_bend_value(), 8192);
}

TEST(parser_roundtrip_all_realtime) {
    umidi::Parser parser;
    umidi::UMP32 ump;

    const uint8_t realtime[] = {0xF8, 0xFA, 0xFB, 0xFC, 0xFE, 0xFF};
    for (uint8_t rt : realtime) {
        ASSERT(parser.parse(rt, ump));
        ASSERT(ump.is_realtime());
        ASSERT_EQ(ump.status(), rt);
    }
}

// Running status with realtime interleaving
TEST(parser_running_status_with_realtime) {
    umidi::Parser parser;
    umidi::UMP32 ump;

    // Start Note On
    ASSERT(!parser.parse_running(0x90, ump));
    ASSERT(!parser.parse_running(60, ump));

    // Timing clock interrupt
    ASSERT(parser.parse_running(0xF8, ump));
    ASSERT(ump.is_realtime());

    // Continue Note On (velocity)
    ASSERT(parser.parse_running(100, ump));
    ASSERT(ump.is_note_on());
    ASSERT_EQ(ump.note(), 60);
    ASSERT_EQ(ump.velocity(), 100);
}

// =============================================================================
// 5. Edge Cases
// =============================================================================

TEST(edge_case_max_values) {
    // Note 127, Velocity 127
    auto ump = umidi::UMP32::note_on(15, 127, 127);
    ASSERT_EQ(ump.channel(), 15);
    ASSERT_EQ(ump.note(), 127);
    ASSERT_EQ(ump.velocity(), 127);
}

TEST(edge_case_min_values) {
    // Note 0, Velocity 1
    auto ump = umidi::UMP32::note_on(0, 0, 1);
    ASSERT_EQ(ump.channel(), 0);
    ASSERT_EQ(ump.note(), 0);
    ASSERT_EQ(ump.velocity(), 1);
}

TEST(edge_case_group_field) {
    // UMP Group 0-15
    for (uint8_t g = 0; g < 16; ++g) {
        auto ump = umidi::UMP32::note_on(0, 60, 100, g);
        ASSERT_EQ(ump.group(), g);
    }
}

// =============================================================================
// 6. Type Safety Tests
// =============================================================================

TEST(type_safety_message_wrapper) {
    using namespace umidi::message;

    auto note_on = NoteOn::create(0, 60, 100);
    auto note_off = NoteOff::create(0, 60, 64);
    auto cc = ControlChange::create(0, 7, 100);
    auto pc = ProgramChange::create(0, 42);
    auto pb = PitchBend::create(0, 8192);

    // Verify type checks work correctly
    ASSERT(note_on.is_valid());
    ASSERT(!NoteOn::from_ump(note_off.ump).is_valid());
    ASSERT(!NoteOn::from_ump(cc.ump).is_valid());

    ASSERT(note_off.is_valid());
    ASSERT(cc.is_valid());
    ASSERT(pc.is_valid());
    ASSERT(pb.is_valid());
}

// =============================================================================
// 7. Memory Layout Tests
// =============================================================================

TEST(memory_ump32_size) {
    ASSERT_EQ(sizeof(umidi::UMP32), 4u);
}

TEST(memory_ump64_size) {
    ASSERT_EQ(sizeof(umidi::UMP64), 8u);
}

TEST(memory_event_size) {
    ASSERT_EQ(sizeof(umidi::Event), 8u);
}

TEST(memory_layout_ump32) {
    // Verify UMP32 word layout
    // [MT:4][Group:4][Status:8][Data1:8][Data2:8]
    umidi::UMP32 ump(0x12345678);
    ASSERT_EQ(ump.mt(), 0x1);
    ASSERT_EQ(ump.group(), 0x2);
    ASSERT_EQ(ump.status(), 0x34);
    ASSERT_EQ(ump.data1(), 0x56);
    ASSERT_EQ(ump.data2(), 0x78);
}

// =============================================================================
// 8. Template Static Decoder Tests
// =============================================================================

TEST(template_decoder_compile_time_support) {
    // SynthDecoder only supports NoteOn/NoteOff
    ASSERT(umidi::codec::SynthDecoder::is_supported<umidi::message::NoteOn>());
    ASSERT(umidi::codec::SynthDecoder::is_supported<umidi::message::NoteOff>());
    ASSERT(!umidi::codec::SynthDecoder::is_supported<umidi::message::ControlChange>());
    ASSERT(!umidi::codec::SynthDecoder::is_supported<umidi::message::ProgramChange>());

    // FullDecoder supports all
    ASSERT(umidi::codec::FullDecoder::is_supported<umidi::message::NoteOn>());
    ASSERT(umidi::codec::FullDecoder::is_supported<umidi::message::ControlChange>());
    ASSERT(umidi::codec::FullDecoder::is_supported<umidi::message::TimingClock>());
}

TEST(template_decoder_decode_note_on) {
    umidi::codec::SynthDecoder decoder;
    umidi::UMP32 ump;

    auto r1 = decoder.decode_byte(0x90, ump);  // Note On ch0
    ASSERT(r1.has_value() && !r1.value());

    auto r2 = decoder.decode_byte(60, ump);    // Note C4
    ASSERT(r2.has_value() && !r2.value());

    auto r3 = decoder.decode_byte(100, ump);   // Velocity
    ASSERT(r3.has_value() && r3.value());

    ASSERT_EQ(ump.status(), 0x90);
    ASSERT_EQ(ump.data1(), 60);
    ASSERT_EQ(ump.data2(), 100);
}

TEST(template_decoder_reject_unsupported) {
    umidi::codec::SynthDecoder decoder;
    umidi::UMP32 ump;

    // CC is not supported by SynthDecoder
    auto r = decoder.decode_byte(0xB0, ump);
    ASSERT(!r.has_value());  // Error
    ASSERT(r.error().code == umidi::ErrorCode::NotSupported);
}

TEST(template_decoder_channel_filtering) {
    // Single channel decoder for channel 5
    constexpr auto ch5_config = umidi::codec::single_channel_config(5);
    umidi::codec::Decoder<ch5_config, umidi::message::NoteOn> decoder;
    umidi::UMP32 ump;

    // Channel 0 should be filtered
    auto r1 = decoder.decode_byte(0x90, ump);
    ASSERT(!r1.has_value());
    ASSERT(r1.error().code == umidi::ErrorCode::ChannelFiltered);

    // Channel 5 should be accepted
    decoder.reset();
    auto r2 = decoder.decode_byte(0x95, ump);
    ASSERT(r2.has_value() && !r2.value());  // Waiting for data
}

// =============================================================================
// 9. SysEx Tests
// =============================================================================

TEST(sysex7_create_complete) {
    uint8_t data[] = {0x7E, 0x00, 0x06, 0x01};  // Identity Request
    auto sysex = umidi::message::SysEx7::create_complete(0, data, 4);

    ASSERT(sysex.is_valid());
    ASSERT_EQ(sysex.num_bytes(), 4u);
    ASSERT_EQ(sysex.sysex_status(), umidi::message::SysEx7::Status::COMPLETE);
    ASSERT_EQ(sysex.data_at(0), 0x7E);
    ASSERT_EQ(sysex.data_at(1), 0x00);
    ASSERT_EQ(sysex.data_at(2), 0x06);
    ASSERT_EQ(sysex.data_at(3), 0x01);
}

TEST(sysex7_create_multi_packet) {
    uint8_t data1[] = {0x7E, 0x00, 0x06, 0x01, 0x02, 0x03};
    uint8_t data2[] = {0x04, 0x05};

    auto start = umidi::message::SysEx7::create_start(0, data1, 6);
    auto end = umidi::message::SysEx7::create_end(0, data2, 2);

    ASSERT_EQ(start.sysex_status(), umidi::message::SysEx7::Status::START);
    ASSERT_EQ(start.num_bytes(), 6u);
    ASSERT_EQ(end.sysex_status(), umidi::message::SysEx7::Status::END);
    ASSERT_EQ(end.num_bytes(), 2u);
}

TEST(sysex_parser_complete) {
    umidi::message::SysExParser<64> parser;

    auto r = parser.parse(0xF0, 0);  // SysEx Start
    ASSERT(!r.complete);
    ASSERT(parser.in_sysex());

    (void)parser.parse(0x7E, 0);
    (void)parser.parse(0x00, 0);
    (void)parser.parse(0x06, 0);
    (void)parser.parse(0x01, 0);

    r = parser.parse(0xF7, 0);  // SysEx End
    ASSERT(r.complete);
    ASSERT_EQ(r.packet.num_bytes(), 4u);
    ASSERT_EQ(r.packet.sysex_status(), umidi::message::SysEx7::Status::COMPLETE);
}

TEST(sysex_serializer) {
    uint8_t data[] = {0x7E, 0x00, 0x06, 0x01};
    auto sysex = umidi::message::SysEx7::create_complete(0, data, 4);

    uint8_t out[8];
    size_t len = umidi::message::SysExSerializer::serialize(sysex, out, true);

    ASSERT_EQ(len, 6u);  // F0 + 4 data + F7
    ASSERT_EQ(out[0], 0xF0);
    ASSERT_EQ(out[1], 0x7E);
    ASSERT_EQ(out[4], 0x01);
    ASSERT_EQ(out[5], 0xF7);
}

// =============================================================================
// 10. JR Timestamp Tests (MIDI 2.0)
// =============================================================================

TEST(jr_timestamp_create) {
    auto ts = umidi::message::JRTimestamp::create(1000, 0);
    ASSERT(ts.is_valid());
    ASSERT_EQ(ts.timestamp(), 1000u);
    ASSERT_EQ(ts.group(), 0u);
}

TEST(jr_timestamp_from_microseconds) {
    // 1000us = 31.25 JR ticks (truncated to 31)
    auto ts = umidi::message::JRTimestamp::from_microseconds(1000, 0);
    ASSERT(ts.is_valid());
    ASSERT_EQ(ts.timestamp(), 31u);
}

TEST(jr_timestamp_to_microseconds) {
    auto ts = umidi::message::JRTimestamp::create(100, 0);
    uint32_t us = ts.to_microseconds();
    ASSERT_EQ(us, 3200u);  // 100 * 32
}

TEST(jr_timestamp_tracker) {
    umidi::message::JRTimestampTracker tracker;
    tracker.set_sample_rate(48000);

    ASSERT(!tracker.has_timestamp());

    auto ts = umidi::message::JRTimestamp::create(31, 0);  // ~992us
    tracker.process(ts);

    ASSERT(tracker.has_timestamp());
    uint32_t offset = tracker.get_sample_offset();
    // 31 ticks * 32us = 992us
    // 992us * 48000 / 1000000 ≈ 47.6 samples
    ASSERT(offset >= 40 && offset <= 60);

    tracker.clear();
    ASSERT(!tracker.has_timestamp());
}

TEST(jr_clock_create) {
    auto clk = umidi::message::JRClock::create(500, 0);
    ASSERT(clk.is_valid());
    ASSERT_EQ(clk.clock(), 500u);
}

TEST(noop_create) {
    auto noop = umidi::message::NOOP::create(0);
    ASSERT(noop.is_valid());
    ASSERT_EQ(noop.group(), 0u);
}

// =============================================================================
// 11. RPN/NRPN Tests
// =============================================================================

TEST(rpn_pitch_bend_sensitivity) {
    umidi::cc::ParameterNumberDecoder decoder;

    // Send RPN 0x0000 (Pitch Bend Sensitivity)
    auto r1 = decoder.decode(0, 101, 0);  // RPN MSB = 0
    ASSERT(!r1.complete);

    auto r2 = decoder.decode(0, 100, 0);  // RPN LSB = 0
    ASSERT(!r2.complete);

    auto r3 = decoder.decode(0, 6, 12);   // Data Entry MSB = 12 semitones
    ASSERT(r3.complete);
    ASSERT_EQ(r3.parameter_number, 0u);
    ASSERT(!r3.is_nrpn);
    ASSERT_EQ((r3.value >> 7), 12u);  // MSB = 12

    // Parse value
    auto pbs = umidi::cc::pitch_bend_sensitivity::parse(r3.value);
    ASSERT_EQ(pbs.semitones, 12u);
    ASSERT_EQ(pbs.cents, 0u);
}

TEST(rpn_fine_tune) {
    umidi::cc::ParameterNumberDecoder decoder;

    // Send RPN 0x0001 (Fine Tuning)
    (void)decoder.decode(0, 101, 0);  // RPN MSB = 0
    (void)decoder.decode(0, 100, 1);  // RPN LSB = 1

    auto r = decoder.decode(0, 6, 64);   // Data Entry MSB = 64 (center)
    ASSERT(r.complete);
    ASSERT_EQ(r.parameter_number, 1u);
    ASSERT(!r.is_nrpn);
}

TEST(nrpn_custom_parameter) {
    umidi::cc::ParameterNumberDecoder decoder;

    // Send NRPN 0x1234
    (void)decoder.decode(0, 99, 0x24);  // NRPN MSB
    (void)decoder.decode(0, 98, 0x34);  // NRPN LSB

    auto r = decoder.decode(0, 6, 100);
    ASSERT(r.complete);
    ASSERT_EQ(r.parameter_number, 0x1234u);
    ASSERT(r.is_nrpn);
}

TEST(rpn_data_increment_decrement) {
    umidi::cc::ParameterNumberDecoder decoder;

    // Setup RPN
    (void)decoder.decode(0, 101, 0);
    (void)decoder.decode(0, 100, 0);
    (void)decoder.decode(0, 6, 10);  // Value = 10 << 7 = 1280

    // Increment
    auto r1 = decoder.decode(0, 96, 0);  // Data Increment
    ASSERT(r1.complete);
    ASSERT_EQ(r1.value, 1281u);

    // Decrement
    auto r2 = decoder.decode(0, 97, 0);  // Data Decrement
    ASSERT(r2.complete);
    ASSERT_EQ(r2.value, 1280u);
}

TEST(rpn_null_reset) {
    umidi::cc::ParameterNumberState state;

    // Select RPN
    (void)state.process(101, 0);
    (void)state.process(100, 0);
    ASSERT(state.is_active());

    // Send RPN Null (0x7F7F)
    (void)state.process(101, 0x7F);
    (void)state.process(100, 0x7F);
    ASSERT(state.is_null());
    ASSERT(!state.is_active());  // Null is not active
}

// =============================================================================
// 12. UMI SysEx Protocol Tests
// =============================================================================

TEST(sysex_7bit_encoding) {
    using namespace umidi::protocol;

    // Test encoding: "Hello" (5 bytes) -> 6 bytes (1 MSB + 5 data)
    uint8_t input[] = {'H', 'e', 'l', 'l', 'o'};
    uint8_t encoded[16];
    size_t enc_len = encode_7bit(input, 5, encoded);

    ASSERT_EQ(enc_len, 6u);  // 1 MSB byte + 5 data bytes

    // Decode back
    uint8_t decoded[16];
    size_t dec_len = decode_7bit(encoded, enc_len, decoded);

    ASSERT_EQ(dec_len, 5u);
    ASSERT(memcmp(input, decoded, 5) == 0);
}

TEST(sysex_7bit_encoding_with_high_bits) {
    using namespace umidi::protocol;

    // Test with bytes that have MSB set
    uint8_t input[] = {0x80, 0xFF, 0x7F, 0x00, 0xAB};
    uint8_t encoded[16];
    size_t enc_len = encode_7bit(input, 5, encoded);

    // Decode back
    uint8_t decoded[16];
    size_t dec_len = decode_7bit(encoded, enc_len, decoded);

    ASSERT_EQ(dec_len, 5u);
    ASSERT(memcmp(input, decoded, 5) == 0);
}

TEST(sysex_message_builder) {
    using namespace umidi::protocol;

    MessageBuilder<64> builder;
    builder.begin(Command::STDOUT_DATA, 0);
    builder.add_data(reinterpret_cast<const uint8_t*>("Hi"), 2);
    size_t len = builder.finalize();

    const uint8_t* msg = builder.data();

    // Check structure: F0 <ID> CMD SEQ DATA CHECKSUM F7
    ASSERT_EQ(msg[0], 0xF0);
    ASSERT_EQ(msg[len - 1], 0xF7);

    // Parse the message
    auto parsed = parse_message(msg, len);
    ASSERT(parsed.valid);
    ASSERT_EQ(static_cast<uint8_t>(parsed.command),
              static_cast<uint8_t>(Command::STDOUT_DATA));
    ASSERT_EQ(parsed.sequence, 0u);
}

TEST(sysex_message_parse_invalid) {
    using namespace umidi::protocol;

    // Too short
    uint8_t short_msg[] = {0xF0, 0x7E, 0xF7};
    auto result = parse_message(short_msg, 3);
    ASSERT(!result.valid);

    // Wrong framing
    uint8_t wrong_frame[] = {0x90, 0x7E, 0x7F, 0x00, 0x01, 0x00, 0x01, 0xF7};
    result = parse_message(wrong_frame, 8);
    ASSERT(!result.valid);
}

TEST(sysex_standard_io_basic) {
    using namespace umidi::protocol;

    StandardIO<256, 256> io;

    // Track sent messages
    std::vector<std::vector<uint8_t>> sent_messages;
    auto send_fn = [&sent_messages](const uint8_t* data, size_t len) {
        sent_messages.emplace_back(data, data + len);
    };

    // Write to stdout
    const char* test_data = "Test";
    size_t written = io.write_stdout(
        reinterpret_cast<const uint8_t*>(test_data), 4, send_fn);

    ASSERT_EQ(written, 4u);
    ASSERT_EQ(sent_messages.size(), 1u);

    // Verify message is valid SysEx
    auto& msg = sent_messages[0];
    ASSERT_EQ(msg[0], 0xF0);
    ASSERT_EQ(msg[msg.size() - 1], 0xF7);

    // Parse and verify
    auto parsed = parse_message(msg.data(), msg.size());
    ASSERT(parsed.valid);
    ASSERT_EQ(static_cast<uint8_t>(parsed.command),
              static_cast<uint8_t>(Command::STDOUT_DATA));
}

TEST(sysex_firmware_update_state_machine) {
    using namespace umidi::protocol;

    FirmwareUpdate<256> fw;

    ASSERT_EQ(static_cast<uint8_t>(fw.state()),
              static_cast<uint8_t>(UpdateState::IDLE));

    // Build FW_BEGIN message with size = 1024
    MessageBuilder<64> builder;
    builder.begin(Command::FW_BEGIN, 0);
    builder.add_u32(1024);
    size_t len = builder.finalize();

    std::vector<std::vector<uint8_t>> responses;
    auto send_fn = [&responses](const uint8_t* data, size_t len) {
        responses.emplace_back(data, data + len);
    };

    // Process FW_BEGIN
    bool handled = fw.process_message(builder.data(), len, send_fn);
    ASSERT(handled);
    ASSERT_EQ(static_cast<uint8_t>(fw.state()),
              static_cast<uint8_t>(UpdateState::RECEIVING));
    ASSERT_EQ(fw.total_size(), 1024u);

    // Should have received ACK
    ASSERT_EQ(responses.size(), 1u);
    auto ack = parse_message(responses[0].data(), responses[0].size());
    ASSERT(ack.valid);
    ASSERT_EQ(static_cast<uint8_t>(ack.command),
              static_cast<uint8_t>(Command::FW_ACK));
}

TEST(sysex_checksum) {
    using namespace umidi::protocol;

    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t checksum = calculate_checksum(data, 4);

    // XOR: 0x01 ^ 0x02 ^ 0x03 ^ 0x04 = 0x04
    ASSERT_EQ(checksum, 0x04);
}

// =============================================================================
// 13. Firmware Header and Validation Tests
// =============================================================================

TEST(firmware_header_size) {
    using namespace umidi::protocol;

    // FirmwareHeader must be exactly 128 bytes
    ASSERT_EQ(sizeof(FirmwareHeader), 128u);
}

TEST(firmware_header_builder) {
    using namespace umidi::protocol;

    auto header = FirmwareHeaderBuilder()
        .version(1, 2, 3)
        .build_number(100)
        .image_size(65536)
        .crc32(0xDEADBEEF)
        .board("STM32F4")
        .load_address(0x08020000)
        .entry_point(0x08020100)
        .build();

    ASSERT_EQ(header.magic, FIRMWARE_MAGIC);
    ASSERT_EQ(header.header_version, FIRMWARE_HEADER_VERSION);
    ASSERT_EQ(header.fw_version_major, 1u);
    ASSERT_EQ(header.fw_version_minor, 2u);
    ASSERT_EQ(header.fw_version_patch, 3u);
    ASSERT_EQ(header.fw_build_number, 100u);
    ASSERT_EQ(header.image_size, 65536u);
    ASSERT_EQ(header.image_crc32, 0xDEADBEEF);
}

TEST(firmware_version_compare) {
    using namespace umidi::protocol;

    // Same version
    ASSERT_EQ(static_cast<int>(compare_versions(1, 0, 0, 1, 0, 0)),
              static_cast<int>(VersionCompare::EQUAL));

    // Major version difference
    ASSERT_EQ(static_cast<int>(compare_versions(2, 0, 0, 1, 0, 0)),
              static_cast<int>(VersionCompare::NEWER));
    ASSERT_EQ(static_cast<int>(compare_versions(1, 0, 0, 2, 0, 0)),
              static_cast<int>(VersionCompare::OLDER));

    // Minor version difference
    ASSERT_EQ(static_cast<int>(compare_versions(1, 2, 0, 1, 1, 0)),
              static_cast<int>(VersionCompare::NEWER));

    // Patch version difference
    ASSERT_EQ(static_cast<int>(compare_versions(1, 0, 5, 1, 0, 3)),
              static_cast<int>(VersionCompare::NEWER));
}

TEST(firmware_validator_header) {
    using namespace umidi::protocol;

    FirmwareValidator<16> validator;
    validator.set_board_id("TestBoard");

    // Valid header
    auto header = FirmwareHeaderBuilder()
        .version(1, 0, 0)
        .image_size(1024)
        .board("TestBoard")
        .build();

    auto result = validator.validate_header(header);
    ASSERT_EQ(static_cast<uint8_t>(result),
              static_cast<uint8_t>(ValidationResult::OK));

    // Invalid magic
    FirmwareHeader bad_magic{};
    bad_magic.magic = 0x12345678;
    result = validator.validate_header(bad_magic);
    ASSERT_EQ(static_cast<uint8_t>(result),
              static_cast<uint8_t>(ValidationResult::INVALID_MAGIC));

    // Board mismatch
    auto wrong_board = FirmwareHeaderBuilder()
        .version(1, 0, 0)
        .board("OtherBoard")
        .build();
    result = validator.validate_header(wrong_board);
    ASSERT_EQ(static_cast<uint8_t>(result),
              static_cast<uint8_t>(ValidationResult::BOARD_MISMATCH));
}

TEST(crc32_calculation) {
    using namespace umidi::protocol;

    // Test with known values
    uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    uint32_t crc = crc32(data, 9);

    // CRC-32 of "123456789" is well-known: 0xCBF43926
    ASSERT_EQ(crc, 0xCBF43926);
}

// =============================================================================
// 14. Bootloader Configuration Tests
// =============================================================================

TEST(boot_config_size) {
    using namespace umidi::protocol;

    // BootConfig must be exactly 64 bytes
    ASSERT_EQ(sizeof(BootConfig), 64u);
}

TEST(platform_configs) {
    using namespace umidi::protocol;

    // STM32F4 config
    ASSERT_EQ(platforms::STM32F4_512K.slot_a.base_address, 0x08020000u);
    ASSERT_EQ(platforms::STM32F4_512K.slot_b.base_address, 0x08050000u);

    // STM32H7 config
    ASSERT_EQ(platforms::STM32H7_2M.slot_a.base_address, 0x08060000u);

    // RP2040 config
    ASSERT_EQ(platforms::RP2040_2M.slot_a.base_address, 0x10012000u);
}

// =============================================================================
// 15. Session and Timeout Tests
// =============================================================================

TEST(session_timer_timeout) {
    using namespace umidi::protocol;

    SessionTimer timer;
    timer.init(DEFAULT_TIMEOUTS);

    timer.start_session(0);
    ASSERT(timer.is_active());

    // No timeout at start
    ASSERT_EQ(static_cast<uint8_t>(timer.check(0)),
              static_cast<uint8_t>(TimeoutEvent::NONE));

    // Session timeout after 5 minutes
    ASSERT_EQ(static_cast<uint8_t>(timer.check(300001)),
              static_cast<uint8_t>(TimeoutEvent::SESSION_EXPIRED));
}

TEST(session_timer_idle_timeout) {
    using namespace umidi::protocol;

    SessionTimer timer;
    timer.init(DEFAULT_TIMEOUTS);

    timer.start_session(0);
    timer.record_activity(0);

    // No timeout with activity
    ASSERT_EQ(static_cast<uint8_t>(timer.check(59000)),
              static_cast<uint8_t>(TimeoutEvent::NONE));

    // Idle timeout after 60 seconds
    ASSERT_EQ(static_cast<uint8_t>(timer.check(60001)),
              static_cast<uint8_t>(TimeoutEvent::IDLE_TIMEOUT));
}

TEST(flow_control_sender) {
    using namespace umidi::protocol;

    FlowControlSender<4> sender;
    FlowConfig config{.window_size = 2, .max_payload_size = 48};
    sender.init(config);

    // Can send initially
    ASSERT(sender.can_send());
    ASSERT(sender.all_acked());

    // Enqueue two packets
    uint8_t data1[] = {1, 2, 3};
    uint8_t data2[] = {4, 5, 6};
    int seq1 = sender.enqueue(data1, 3);
    int seq2 = sender.enqueue(data2, 3);

    ASSERT_EQ(seq1, 0);
    ASSERT_EQ(seq2, 1);
    ASSERT(!sender.can_send());  // Window full
    ASSERT_EQ(sender.pending_count(), 2u);

    // Can't enqueue more
    uint8_t data3[] = {7, 8, 9};
    int seq3 = sender.enqueue(data3, 3);
    ASSERT_EQ(seq3, -1);  // Window full

    // ACK first packet
    sender.process_ack(0);
    ASSERT(sender.can_send());
    ASSERT_EQ(sender.pending_count(), 1u);

    // ACK second packet
    sender.process_ack(1);
    ASSERT(sender.all_acked());
}

TEST(flow_control_receiver) {
    using namespace umidi::protocol;

    FlowControlReceiver receiver;

    // Process in-order packets
    ASSERT(receiver.process_packet(0));
    ASSERT_EQ(receiver.ack_sequence(), 0u);

    ASSERT(receiver.process_packet(1));
    ASSERT_EQ(receiver.ack_sequence(), 1u);

    // Out-of-order packet
    ASSERT(receiver.process_packet(3));  // Skip 2
    ASSERT_EQ(receiver.ack_sequence(), 1u);  // Still waiting for 2

    // Fill the gap
    ASSERT(receiver.process_packet(2));
    ASSERT_EQ(receiver.ack_sequence(), 3u);  // Now caught up
}

// =============================================================================
// 16. Authentication Tests
// =============================================================================

TEST(auth_state_machine) {
    using namespace umidi::protocol;

    Authenticator<32, 300000> auth;

    uint8_t key[32] = {0};
    auth.init(key, nullptr, nullptr);

    ASSERT_EQ(static_cast<uint8_t>(auth.state()),
              static_cast<uint8_t>(AuthState::IDLE));

    // Generate challenge
    uint8_t challenge[32];
    auth.generate_challenge(challenge);

    ASSERT_EQ(static_cast<uint8_t>(auth.state()),
              static_cast<uint8_t>(AuthState::CHALLENGE_SENT));

    // Without HMAC function, verification should fail
    uint8_t response[32] = {0};
    bool verified = auth.verify_response(response, 1000);
    ASSERT(!verified);

    // State should be back to IDLE on failure
    ASSERT_EQ(static_cast<uint8_t>(auth.state()),
              static_cast<uint8_t>(AuthState::IDLE));
}

TEST(auth_logout) {
    using namespace umidi::protocol;

    Authenticator<32, 0> auth;  // No timeout

    uint8_t key[32] = {0};
    auth.init(key, nullptr, nullptr);

    // Generate challenge (state changes to CHALLENGE_SENT)
    uint8_t challenge[32];
    auth.generate_challenge(challenge);

    ASSERT_EQ(static_cast<uint8_t>(auth.state()),
              static_cast<uint8_t>(AuthState::CHALLENGE_SENT));

    // Logout
    auth.logout();

    ASSERT_EQ(static_cast<uint8_t>(auth.state()),
              static_cast<uint8_t>(AuthState::IDLE));
}

TEST(secure_compare) {
    using namespace umidi::protocol;

    uint8_t a[] = {1, 2, 3, 4, 5};
    uint8_t b[] = {1, 2, 3, 4, 5};
    uint8_t c[] = {1, 2, 3, 4, 6};

    ASSERT(secure_compare(a, b, 5));
    ASSERT(!secure_compare(a, c, 5));
}

#ifdef UMI_INCLUDE_SOFTWARE_CRYPTO
TEST(hmac_sha256_software) {
    using namespace umidi::protocol;

    // Test with known values (RFC 4231 test vector 1)
    uint8_t key[20];
    memset(key, 0x0b, 20);

    const char* data = "Hi There";
    uint8_t result[32];

    hmac_sha256_soft(key, 20,
                      reinterpret_cast<const uint8_t*>(data), 8,
                      result);

    // Expected HMAC-SHA256 for this test vector
    // (truncated check for simplicity)
    ASSERT_EQ(result[0], 0xb0);
    ASSERT_EQ(result[1], 0x34);
}
#endif

// =============================================================================
// 17. Transport Abstraction
// =============================================================================

TEST(bulk_framing_encode_decode) {
    using namespace umidi::protocol;

    // Test data
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x80, 0xFF};
    uint8_t frame[32];
    uint8_t decoded[32];

    // Encode
    size_t frame_len = bulk::encode_frame(data, sizeof(data), frame);
    ASSERT(frame_len == sizeof(data) + bulk::FRAME_OVERHEAD);

    // Check length prefix
    size_t encoded_len = (size_t(frame[0]) << 8) | frame[1];
    ASSERT_EQ(encoded_len, sizeof(data));

    // Decode
    size_t decoded_len = bulk::decode_frame(frame, frame_len, decoded, sizeof(decoded));
    ASSERT_EQ(decoded_len, sizeof(data));

    // Verify data matches
    for (size_t i = 0; i < sizeof(data); ++i) {
        ASSERT_EQ(decoded[i], data[i]);
    }
}

TEST(bulk_crc16) {
    using namespace umidi::protocol;

    uint8_t data1[] = {0x00};
    uint16_t crc1 = bulk::crc16(data1, 1);
    ASSERT(crc1 != 0);  // CRC of any data shouldn't be 0

    // Same data should produce same CRC
    uint16_t crc1b = bulk::crc16(data1, 1);
    ASSERT_EQ(crc1, crc1b);

    // Different data should produce different CRC
    uint8_t data2[] = {0x01};
    uint16_t crc2 = bulk::crc16(data2, 1);
    ASSERT(crc1 != crc2);
}

TEST(bulk_invalid_frame) {
    using namespace umidi::protocol;

    uint8_t decoded[32];

    // Too short
    uint8_t short_frame[] = {0x00, 0x01, 0x00};  // Only 3 bytes, need at least 4
    size_t result = bulk::decode_frame(short_frame, 3, decoded, sizeof(decoded));
    ASSERT_EQ(result, 0);

    // Length mismatch
    uint8_t bad_len[] = {0x00, 0x10, 0x00, 0x00, 0x00, 0x00};  // Says 16 bytes but only has 2
    result = bulk::decode_frame(bad_len, sizeof(bad_len), decoded, sizeof(decoded));
    ASSERT_EQ(result, 0);
}

TEST(transport_capabilities) {
    using namespace umidi::protocol;

    // SysEx capabilities
    ASSERT_EQ(SYSEX_CAPABILITIES.supports_8bit, false);
    ASSERT_EQ(SYSEX_CAPABILITIES.requires_encoding, true);

    // Bulk capabilities
    ASSERT_EQ(BULK_CAPABILITIES.supports_8bit, true);
    ASSERT_EQ(BULK_CAPABILITIES.requires_encoding, false);
    ASSERT(BULK_CAPABILITIES.max_packet_size > SYSEX_CAPABILITIES.max_packet_size);
}

// =============================================================================
// 18. State Synchronization
// =============================================================================

TEST(state_report_size) {
    using namespace umidi::protocol;
    ASSERT_EQ(sizeof(StateReport), 16);
}

TEST(resume_info_size) {
    using namespace umidi::protocol;
    ASSERT_EQ(sizeof(ResumeInfo), 20);
}

TEST(boot_verification_size) {
    using namespace umidi::protocol;
    ASSERT_EQ(sizeof(BootVerification), 16);
}

TEST(state_manager_basic) {
    using namespace umidi::protocol;

    StateManager mgr;
    mgr.init();

    ASSERT_EQ(mgr.state(), DeviceState::IDLE);
    ASSERT_EQ(mgr.session_id(), 0u);

    // Start session
    uint32_t session_id = mgr.start_session(1000);
    ASSERT(session_id > 0);
    ASSERT_EQ(mgr.state(), DeviceState::UPDATE_STARTING);
    ASSERT_EQ(mgr.total_bytes(), 1000u);
}

TEST(state_manager_progress) {
    using namespace umidi::protocol;

    StateManager mgr;
    mgr.init();

    mgr.start_session(1000);
    mgr.set_state(DeviceState::UPDATE_RECEIVING);

    mgr.record_received(100, 1);
    auto report = mgr.build_report();
    ASSERT_EQ(report.progress_percent, 10);
    ASSERT_EQ(report.last_ack_seq, 1);

    mgr.record_received(400, 2);
    report = mgr.build_report();
    ASSERT_EQ(report.progress_percent, 50);
}

TEST(state_manager_flags) {
    using namespace umidi::protocol;

    StateManager mgr;
    mgr.init();

    mgr.set_flag(StateReport::FLAG_AUTHENTICATED);
    auto report = mgr.build_report();
    ASSERT(report.is_authenticated());
    ASSERT(!report.is_resumable());

    mgr.set_flag(StateReport::FLAG_RESUMABLE);
    report = mgr.build_report();
    ASSERT(report.is_authenticated());
    ASSERT(report.is_resumable());

    mgr.clear_flag(StateReport::FLAG_AUTHENTICATED);
    report = mgr.build_report();
    ASSERT(!report.is_authenticated());
    ASSERT(report.is_resumable());
}

TEST(boot_verification_init) {
    using namespace umidi::protocol;

    BootVerification boot;
    boot.init(3);

    ASSERT(boot.is_valid());
    ASSERT_EQ(boot.max_attempts, 3);
    ASSERT_EQ(boot.boot_count, 0);
    ASSERT_EQ(boot.verified, 1);
    ASSERT(!boot.should_rollback());
}

TEST(boot_verification_rollback) {
    using namespace umidi::protocol;

    BootVerification boot;
    boot.init(3);

    // Simulate multiple failed boots
    boot.increment_boot();
    ASSERT(!boot.should_rollback());
    boot.increment_boot();
    ASSERT(!boot.should_rollback());
    boot.increment_boot();
    ASSERT(boot.should_rollback());
}

TEST(boot_verification_success) {
    using namespace umidi::protocol;

    BootVerification boot;
    boot.init(3);

    boot.increment_boot();
    boot.increment_boot();
    ASSERT_EQ(boot.boot_count, 2);
    ASSERT_EQ(boot.verified, 0);

    boot.mark_success(12345);
    ASSERT_EQ(boot.boot_count, 0);
    ASSERT_EQ(boot.verified, 1);
    ASSERT_EQ(boot.last_success_time, 12345u);
    ASSERT(!boot.should_rollback());
}

// =============================================================================
// 19. Object Transfer Protocol
// =============================================================================

TEST(object_header_size) {
    using namespace umidi::protocol;
    ASSERT_EQ(sizeof(ObjectHeader), 80);
}

TEST(sequence_metadata_size) {
    using namespace umidi::protocol;
    ASSERT_EQ(sizeof(SequenceMetadata), 16);
}

TEST(sample_metadata_size) {
    using namespace umidi::protocol;
    ASSERT_EQ(sizeof(SampleMetadata), 16);
}

TEST(preset_metadata_size) {
    using namespace umidi::protocol;
    ASSERT_EQ(sizeof(PresetMetadata), 16);
}

TEST(config_metadata_size) {
    using namespace umidi::protocol;
    ASSERT_EQ(sizeof(ConfigMetadata), 16);
}

TEST(object_header_init) {
    using namespace umidi::protocol;

    ObjectHeader header;
    header.init(ObjectType::SEQUENCE, 42, "Test Sequence", 1024);

    ASSERT(header.is_valid());
    ASSERT_EQ(header.type(), ObjectType::SEQUENCE);
    ASSERT_EQ(header.object_id, 42u);
    ASSERT_EQ(header.data_size, 1024u);
    ASSERT(strcmp(header.get_name(), "Test Sequence") == 0);
}

TEST(object_header_flags) {
    using namespace umidi::protocol;

    ObjectHeader header;
    header.init(ObjectType::SAMPLE, 1, "Sample", 4096);

    header.set_flags(ObjectFlags::COMPRESSED | ObjectFlags::READONLY);
    auto flags = header.get_flags();

    ASSERT(flags & ObjectFlags::COMPRESSED);
    ASSERT(flags & ObjectFlags::READONLY);
    ASSERT(!(flags & ObjectFlags::ENCRYPTED));
}

TEST(sequence_metadata_bpm) {
    using namespace umidi::protocol;

    SequenceMetadata meta{};
    meta.set_bpm(120.0f);
    ASSERT_EQ(meta.bpm_x10, 1200);

    float bpm = meta.get_bpm();
    ASSERT(bpm >= 119.9f && bpm <= 120.1f);

    meta.set_bpm(145.5f);
    ASSERT_EQ(meta.bpm_x10, 1455);
}

TEST(sample_metadata_root_note) {
    using namespace umidi::protocol;

    SampleMetadata meta{};
    meta.set_root_note(60, 0);  // Middle C
    ASSERT_EQ(meta.get_root_note(), 60);
    ASSERT_EQ(meta.get_fine_tune(), 0);

    meta.set_root_note(69, 50);  // A4 + 50 cents
    ASSERT_EQ(meta.get_root_note(), 69);
    ASSERT_EQ(meta.get_fine_tune(), 50);
}

TEST(ram_object_storage) {
    using namespace umidi::protocol;

    RAMObjectStorage<8, 4096> storage;

    // Initially empty
    auto info = storage.get_storage_info();
    ASSERT_EQ(info.object_count, 0u);
    ASSERT_EQ(info.max_objects, 8u);

    // Create object
    ObjectHeader header;
    header.init(ObjectType::PRESET, storage.generate_id(), "Test Preset", 64);

    ASSERT(storage.write_begin(header));

    uint8_t data[64];
    for (int i = 0; i < 64; ++i) data[i] = static_cast<uint8_t>(i);
    ASSERT(storage.write_data(header.object_id, 0, data, 64));
    ASSERT(storage.write_commit(header.object_id));

    // Verify stored
    info = storage.get_storage_info();
    ASSERT_EQ(info.object_count, 1u);

    // Read back
    ObjectHeader read_header;
    ASSERT(storage.get_header(header.object_id, read_header));
    ASSERT(strcmp(read_header.get_name(), "Test Preset") == 0);

    uint8_t read_data[64];
    size_t read_len = storage.read_data(header.object_id, 0, read_data, 64);
    ASSERT_EQ(read_len, 64u);
    ASSERT_EQ(read_data[0], 0);
    ASSERT_EQ(read_data[63], 63);
}

TEST(ram_object_storage_delete) {
    using namespace umidi::protocol;

    RAMObjectStorage<8, 4096> storage;

    ObjectHeader header;
    header.init(ObjectType::CONFIG, storage.generate_id(), "Config", 16);
    storage.write_begin(header);
    uint8_t data[16] = {0};
    storage.write_data(header.object_id, 0, data, 16);
    storage.write_commit(header.object_id);

    ASSERT_EQ(storage.get_storage_info().object_count, 1u);

    ASSERT(storage.remove(header.object_id));
    ASSERT_EQ(storage.get_storage_info().object_count, 0u);

    // Can't get deleted header
    ObjectHeader deleted_header;
    ASSERT(!storage.get_header(header.object_id, deleted_header));
}

// =============================================================================
// Main
// =============================================================================

int main() {
    printf("=== umidi Comprehensive Test Suite ===\n");
    printf("Testing all MIDI message types for correctness\n");

    SECTION("1. Channel Voice Messages");
    RUN_TEST(channel_note_off_all_channels);
    RUN_TEST(channel_note_off_all_notes);
    RUN_TEST(channel_note_on_all_channels);
    RUN_TEST(channel_note_on_all_velocities);
    RUN_TEST(channel_poly_pressure);
    RUN_TEST(channel_cc_all_controllers);
    RUN_TEST(channel_cc_all_values);
    RUN_TEST(channel_program_change);
    RUN_TEST(channel_pressure);
    RUN_TEST(channel_pitch_bend_range);

    SECTION("2. System Common Messages");
    RUN_TEST(system_mtc_quarter_frame);
    RUN_TEST(system_song_position);
    RUN_TEST(system_song_select);
    RUN_TEST(system_tune_request);

    SECTION("3. System Real-Time Messages");
    RUN_TEST(realtime_timing_clock);
    RUN_TEST(realtime_start);
    RUN_TEST(realtime_continue);
    RUN_TEST(realtime_stop);
    RUN_TEST(realtime_active_sensing);
    RUN_TEST(realtime_system_reset);

    SECTION("4. Parser Round-Trip Tests");
    RUN_TEST(parser_roundtrip_note_on);
    RUN_TEST(parser_roundtrip_program_change);
    RUN_TEST(parser_roundtrip_pitch_bend);
    RUN_TEST(parser_roundtrip_all_realtime);
    RUN_TEST(parser_running_status_with_realtime);

    SECTION("5. Edge Cases");
    RUN_TEST(edge_case_max_values);
    RUN_TEST(edge_case_min_values);
    RUN_TEST(edge_case_group_field);

    SECTION("6. Type Safety");
    RUN_TEST(type_safety_message_wrapper);

    SECTION("7. Memory Layout");
    RUN_TEST(memory_ump32_size);
    RUN_TEST(memory_ump64_size);
    RUN_TEST(memory_event_size);
    RUN_TEST(memory_layout_ump32);

    SECTION("8. Template Static Decoder");
    RUN_TEST(template_decoder_compile_time_support);
    RUN_TEST(template_decoder_decode_note_on);
    RUN_TEST(template_decoder_reject_unsupported);
    RUN_TEST(template_decoder_channel_filtering);

    SECTION("9. SysEx");
    RUN_TEST(sysex7_create_complete);
    RUN_TEST(sysex7_create_multi_packet);
    RUN_TEST(sysex_parser_complete);
    RUN_TEST(sysex_serializer);

    SECTION("10. JR Timestamp (MIDI 2.0)");
    RUN_TEST(jr_timestamp_create);
    RUN_TEST(jr_timestamp_from_microseconds);
    RUN_TEST(jr_timestamp_to_microseconds);
    RUN_TEST(jr_timestamp_tracker);
    RUN_TEST(jr_clock_create);
    RUN_TEST(noop_create);

    SECTION("11. RPN/NRPN");
    RUN_TEST(rpn_pitch_bend_sensitivity);
    RUN_TEST(rpn_fine_tune);
    RUN_TEST(nrpn_custom_parameter);
    RUN_TEST(rpn_data_increment_decrement);
    RUN_TEST(rpn_null_reset);

    SECTION("12. UMI SysEx Protocol");
    RUN_TEST(sysex_7bit_encoding);
    RUN_TEST(sysex_7bit_encoding_with_high_bits);
    RUN_TEST(sysex_message_builder);
    RUN_TEST(sysex_message_parse_invalid);
    RUN_TEST(sysex_standard_io_basic);
    RUN_TEST(sysex_firmware_update_state_machine);
    RUN_TEST(sysex_checksum);

    SECTION("13. Firmware Header and Validation");
    RUN_TEST(firmware_header_size);
    RUN_TEST(firmware_header_builder);
    RUN_TEST(firmware_version_compare);
    RUN_TEST(firmware_validator_header);
    RUN_TEST(crc32_calculation);

    SECTION("14. Bootloader Configuration");
    RUN_TEST(boot_config_size);
    RUN_TEST(platform_configs);

    SECTION("15. Session and Timeout");
    RUN_TEST(session_timer_timeout);
    RUN_TEST(session_timer_idle_timeout);
    RUN_TEST(flow_control_sender);
    RUN_TEST(flow_control_receiver);

    SECTION("16. Authentication");
    RUN_TEST(auth_state_machine);
    RUN_TEST(auth_logout);
    RUN_TEST(secure_compare);

    SECTION("17. Transport Abstraction");
    RUN_TEST(bulk_framing_encode_decode);
    RUN_TEST(bulk_crc16);
    RUN_TEST(bulk_invalid_frame);
    RUN_TEST(transport_capabilities);

    SECTION("18. State Synchronization");
    RUN_TEST(state_report_size);
    RUN_TEST(resume_info_size);
    RUN_TEST(boot_verification_size);
    RUN_TEST(state_manager_basic);
    RUN_TEST(state_manager_progress);
    RUN_TEST(state_manager_flags);
    RUN_TEST(boot_verification_init);
    RUN_TEST(boot_verification_rollback);
    RUN_TEST(boot_verification_success);

    SECTION("19. Object Transfer Protocol");
    RUN_TEST(object_header_size);
    RUN_TEST(sequence_metadata_size);
    RUN_TEST(sample_metadata_size);
    RUN_TEST(preset_metadata_size);
    RUN_TEST(config_metadata_size);
    RUN_TEST(object_header_init);
    RUN_TEST(object_header_flags);
    RUN_TEST(sequence_metadata_bpm);
    RUN_TEST(sample_metadata_root_note);
    RUN_TEST(ram_object_storage);
    RUN_TEST(ram_object_storage_delete);

    printf("\n=================================\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("=================================\n");

    return tests_failed > 0 ? 1 : 0;
}

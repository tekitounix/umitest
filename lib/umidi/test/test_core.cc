// SPDX-License-Identifier: MIT
// umidi Core Tests - UMP, Parser, Result
#include "test_framework.hh"
#include "core/ump.hh"
#include "core/parser.hh"
#include "core/result.hh"
#include "core/sysex_buffer.hh"

using namespace umidi;
using namespace umidi::test;

// =============================================================================
// UMP32 Tests
// =============================================================================

TEST(ump32_size) {
    ASSERT_EQ(sizeof(UMP32), 4);
    TEST_PASS();
}

TEST(ump32_note_on) {
    auto ump = UMP32::note_on(0, 60, 100);
    ASSERT(ump.is_note_on());
    ASSERT(!ump.is_note_off());
    ASSERT_EQ(ump.channel(), 0);
    ASSERT_EQ(ump.note(), 60);
    ASSERT_EQ(ump.velocity(), 100);
    TEST_PASS();
}

TEST(ump32_note_off) {
    auto ump = UMP32::note_off(5, 72, 64);
    ASSERT(ump.is_note_off());
    ASSERT(!ump.is_note_on());
    ASSERT_EQ(ump.channel(), 5);
    ASSERT_EQ(ump.note(), 72);
    ASSERT_EQ(ump.velocity(), 64);
    TEST_PASS();
}

TEST(ump32_note_on_all_channels) {
    for (uint8_t ch = 0; ch < 16; ++ch) {
        auto ump = UMP32::note_on(ch, 60, 100);
        ASSERT_EQ(ump.channel(), ch);
    }
    TEST_PASS();
}

TEST(ump32_note_on_all_notes) {
    for (uint8_t note = 0; note < 128; ++note) {
        auto ump = UMP32::note_on(0, note, 100);
        ASSERT_EQ(ump.note(), note);
    }
    TEST_PASS();
}

TEST(ump32_note_on_all_velocities) {
    for (uint8_t vel = 0; vel < 128; ++vel) {
        auto ump = UMP32::note_on(0, 60, vel);
        ASSERT_EQ(ump.velocity(), vel);
    }
    TEST_PASS();
}

TEST(ump32_cc) {
    auto ump = UMP32::cc(3, 7, 100);  // Channel 3, CC7 (Volume), value 100
    ASSERT(ump.is_cc());
    ASSERT_EQ(ump.channel(), 3);
    ASSERT_EQ(ump.cc_number(), 7);
    ASSERT_EQ(ump.cc_value(), 100);
    TEST_PASS();
}

TEST(ump32_cc_all_controllers) {
    for (uint8_t cc = 0; cc < 128; ++cc) {
        auto ump = UMP32::cc(0, cc, 64);
        ASSERT_EQ(ump.cc_number(), cc);
    }
    TEST_PASS();
}

TEST(ump32_program_change) {
    auto ump = UMP32::program_change(7, 42);
    ASSERT(ump.is_program_change());
    ASSERT_EQ(ump.channel(), 7);
    ASSERT_EQ(ump.program(), 42);
    TEST_PASS();
}

TEST(ump32_pitch_bend) {
    auto ump = UMP32::pitch_bend(0, 8192);  // Center
    ASSERT(ump.is_pitch_bend());
    ASSERT_EQ(ump.channel(), 0);
    ASSERT_EQ(ump.pitch_bend_value(), 8192);

    // Min/Max
    auto min = UMP32::pitch_bend(0, 0);
    auto max = UMP32::pitch_bend(0, 16383);
    ASSERT_EQ(min.pitch_bend_value(), 0);
    ASSERT_EQ(max.pitch_bend_value(), 16383);
    TEST_PASS();
}

TEST(ump32_channel_pressure) {
    auto ump = UMP32::channel_pressure(2, 80);
    ASSERT(ump.is_channel_pressure());
    ASSERT_EQ(ump.channel(), 2);
    ASSERT_EQ(ump.pressure(), 80);
    TEST_PASS();
}

TEST(ump32_poly_pressure) {
    auto ump = UMP32::poly_pressure(4, 60, 90);
    ASSERT(ump.is_poly_pressure());
    ASSERT_EQ(ump.channel(), 4);
    ASSERT_EQ(ump.note(), 60);
    ASSERT_EQ(ump.data2(), 90);  // Poly pressure value is in data2
    TEST_PASS();
}

TEST(ump32_realtime_messages) {
    ASSERT(UMP32::timing_clock().is_timing_clock());
    ASSERT(UMP32::start().is_start());
    ASSERT(UMP32::continue_msg().is_continue());
    ASSERT(UMP32::stop().is_stop());
    ASSERT(UMP32::active_sensing().is_active_sensing());
    ASSERT(UMP32::system_reset().is_system_reset());
    TEST_PASS();
}

TEST(ump32_system_common) {
    auto tune = UMP32::tune_request();
    ASSERT(tune.is_tune_request());

    auto mtc = UMP32::mtc_quarter_frame(0x23);
    ASSERT_EQ(mtc.mtc_data(), 0x23);

    auto spp = UMP32::song_position(1234);
    ASSERT_EQ(spp.song_position(), 1234);

    auto ss = UMP32::song_select(5);
    ASSERT_EQ(ss.song_number(), 5);
    TEST_PASS();
}

// =============================================================================
// UMP64 Tests
// =============================================================================

TEST(ump64_size) {
    ASSERT_EQ(sizeof(UMP64), 8);
    TEST_PASS();
}

TEST(ump64_sysex) {
    uint8_t data[] = {0x7E, 0x00, 0x06, 0x01};
    auto ump = UMP64::sysex7_complete(0, data, 4);  // group=0
    ASSERT_EQ(ump.mt(), 3);  // MT=3 for SysEx7
    TEST_PASS();
}

// =============================================================================
// Parser Tests
// =============================================================================

TEST(parser_note_on) {
    Parser parser;
    UMP32 ump;

    // Note On: 0x90 0x3C 0x64 (ch0, note 60, vel 100)
    ASSERT(!parser.parse(0x90, ump));  // Status
    ASSERT(!parser.parse(0x3C, ump));  // Note
    ASSERT(parser.parse(0x64, ump));   // Velocity - complete

    ASSERT(ump.is_note_on());
    ASSERT_EQ(ump.channel(), 0);
    ASSERT_EQ(ump.note(), 60);
    ASSERT_EQ(ump.velocity(), 100);
    TEST_PASS();
}

TEST(parser_note_off) {
    Parser parser;
    UMP32 ump;

    ASSERT(!parser.parse(0x85, ump));  // Note Off ch5
    ASSERT(!parser.parse(0x48, ump));  // Note 72
    ASSERT(parser.parse(0x40, ump));   // Velocity 64

    ASSERT(ump.is_note_off());
    ASSERT_EQ(ump.channel(), 5);
    ASSERT_EQ(ump.note(), 72);
    TEST_PASS();
}

TEST(parser_cc) {
    Parser parser;
    UMP32 ump;

    ASSERT(!parser.parse(0xB3, ump));  // CC ch3
    ASSERT(!parser.parse(0x07, ump));  // CC7 (Volume)
    ASSERT(parser.parse(0x64, ump));   // Value 100

    ASSERT(ump.is_cc());
    ASSERT_EQ(ump.cc_number(), 7);
    ASSERT_EQ(ump.cc_value(), 100);
    TEST_PASS();
}

TEST(parser_running_status) {
    Parser parser;
    UMP32 ump;

    // First Note On - use parse_running for running status support
    ASSERT(!parser.parse_running(0x90, ump));
    ASSERT(!parser.parse_running(0x3C, ump));
    ASSERT(parser.parse_running(0x64, ump));
    ASSERT(ump.is_note_on());

    // Running status - another Note On without status byte
    ASSERT(!parser.parse_running(0x40, ump));  // Note 64
    ASSERT(parser.parse_running(0x50, ump));   // Velocity 80
    ASSERT(ump.is_note_on());
    ASSERT_EQ(ump.note(), 64);
    ASSERT_EQ(ump.velocity(), 80);
    TEST_PASS();
}

TEST(parser_realtime_interruption) {
    Parser parser;
    UMP32 ump;

    ASSERT(!parser.parse(0x90, ump));  // Note On start
    ASSERT(!parser.parse(0x3C, ump));  // Note byte

    // Timing clock in the middle
    ASSERT(parser.parse(0xF8, ump));
    ASSERT(ump.is_timing_clock());

    // Continue Note On
    ASSERT(parser.parse(0x64, ump));
    ASSERT(ump.is_note_on());
    ASSERT_EQ(ump.note(), 60);
    TEST_PASS();
}

TEST(parser_pitch_bend) {
    Parser parser;
    UMP32 ump;

    ASSERT(!parser.parse(0xE0, ump));  // Pitch Bend
    ASSERT(!parser.parse(0x00, ump));  // LSB
    ASSERT(parser.parse(0x40, ump));   // MSB (center = 0x2000 = 8192)

    ASSERT(ump.is_pitch_bend());
    ASSERT_EQ(ump.pitch_bend_value(), 8192);
    TEST_PASS();
}

TEST(parser_program_change) {
    Parser parser;
    UMP32 ump;

    ASSERT(!parser.parse(0xC7, ump));  // Program Change ch7
    ASSERT(parser.parse(0x2A, ump));   // Program 42

    ASSERT(ump.is_program_change());
    ASSERT_EQ(ump.channel(), 7);
    ASSERT_EQ(ump.program(), 42);
    TEST_PASS();
}

TEST(parser_reset) {
    Parser parser;
    UMP32 ump;

    ASSERT(!parser.parse(0x90, ump));  // Start Note On
    parser.reset();

    // Should need full message now
    ASSERT(!parser.parse(0x90, ump));
    ASSERT(!parser.parse(0x3C, ump));
    ASSERT(parser.parse(0x64, ump));
    TEST_PASS();
}

// =============================================================================
// Result Tests
// =============================================================================

TEST(result_ok) {
    Result<int> r = Ok(42);
    ASSERT(r.has_value());
    ASSERT_EQ(r.value(), 42);
    TEST_PASS();
}

TEST(result_err) {
    Result<int> r = Err<int>(ErrorCode::INVALID_STATUS);
    ASSERT(!r.has_value());
    ASSERT_EQ(r.error().code, ErrorCode::INVALID_STATUS);
    TEST_PASS();
}

TEST(result_error_context) {
    auto err = Error::invalid_status(0xF5);
    ASSERT_EQ(err.code, ErrorCode::INVALID_STATUS);
    ASSERT_EQ(err.context, 0xF5);
    TEST_PASS();
}

TEST(result_error_factories) {
    ASSERT_EQ(Error::incomplete().code, ErrorCode::INCOMPLETE_MESSAGE);
    ASSERT_EQ(Error::buffer_overflow().code, ErrorCode::BUFFER_OVERFLOW);
    ASSERT_EQ(Error::not_implemented().code, ErrorCode::NOT_IMPLEMENTED);
    TEST_PASS();
}

// =============================================================================
// SysEx Buffer Tests
// =============================================================================

TEST(sysex_buffer_basic) {
    SysExBuffer<64> buf;

    ASSERT(buf.push(0x7E));
    ASSERT(buf.push(0x00));
    ASSERT(buf.push(0x06));
    ASSERT(buf.push(0x01));

    ASSERT_EQ(buf.size(), 4);
    ASSERT_EQ(buf.data()[0], 0x7E);
    // Note: data()[3] would be out of contiguous range for ring buffer
    // Copy to linear buffer for validation
    uint8_t out[4];
    ASSERT_EQ(buf.copy_to(out, 4), 4);
    ASSERT_EQ(out[3], 0x01);
    TEST_PASS();
}

TEST(sysex_buffer_overflow) {
    SysExBuffer<4> buf;

    ASSERT(buf.push(0x01));
    ASSERT(buf.push(0x02));
    ASSERT(buf.push(0x03));
    ASSERT(buf.push(0x04));
    ASSERT(!buf.push(0x05));  // Overflow
    TEST_PASS();
}

TEST(sysex_buffer_clear) {
    SysExBuffer<16> buf;

    (void)buf.push(0x01);
    (void)buf.push(0x02);
    buf.clear();

    ASSERT_EQ(buf.size(), 0);
    TEST_PASS();
}

// =============================================================================
// Main
// =============================================================================

int main() {
    printf("=== umidi Core Tests ===\n");

    SECTION("UMP32");
    RUN_TEST(ump32_size);
    RUN_TEST(ump32_note_on);
    RUN_TEST(ump32_note_off);
    RUN_TEST(ump32_note_on_all_channels);
    RUN_TEST(ump32_note_on_all_notes);
    RUN_TEST(ump32_note_on_all_velocities);
    RUN_TEST(ump32_cc);
    RUN_TEST(ump32_cc_all_controllers);
    RUN_TEST(ump32_program_change);
    RUN_TEST(ump32_pitch_bend);
    RUN_TEST(ump32_channel_pressure);
    RUN_TEST(ump32_poly_pressure);
    RUN_TEST(ump32_realtime_messages);
    RUN_TEST(ump32_system_common);

    SECTION("UMP64");
    RUN_TEST(ump64_size);
    RUN_TEST(ump64_sysex);

    SECTION("Parser");
    RUN_TEST(parser_note_on);
    RUN_TEST(parser_note_off);
    RUN_TEST(parser_cc);
    RUN_TEST(parser_running_status);
    RUN_TEST(parser_realtime_interruption);
    RUN_TEST(parser_pitch_bend);
    RUN_TEST(parser_program_change);
    RUN_TEST(parser_reset);

    SECTION("Result");
    RUN_TEST(result_ok);
    RUN_TEST(result_err);
    RUN_TEST(result_error_context);
    RUN_TEST(result_error_factories);

    SECTION("SysEx Buffer");
    RUN_TEST(sysex_buffer_basic);
    RUN_TEST(sysex_buffer_overflow);
    RUN_TEST(sysex_buffer_clear);

    return summary();
}

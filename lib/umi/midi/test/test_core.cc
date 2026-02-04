// SPDX-License-Identifier: MIT
// umidi Core Tests - UMP, Parser, Result
#include <umitest.hh>

#include "core/parser.hh"
#include "core/result.hh"
#include "core/sysex_buffer.hh"
#include "core/ump.hh"

using namespace umidi;
using namespace umitest;

// =============================================================================
// UMP32 Tests
// =============================================================================

bool test_ump32_size(TestContext& t) {
    t.assert_eq(sizeof(UMP32), 4);
    return true;
}

bool test_ump32_note_on(TestContext& t) {
    auto ump = UMP32::note_on(0, 60, 100);
    t.assert_true(ump.is_note_on());
    t.assert_true(!ump.is_note_off());
    t.assert_eq(ump.channel(), 0);
    t.assert_eq(ump.note(), 60);
    t.assert_eq(ump.velocity(), 100);
    return true;
}

bool test_ump32_note_off(TestContext& t) {
    auto ump = UMP32::note_off(5, 72, 64);
    t.assert_true(ump.is_note_off());
    t.assert_true(!ump.is_note_on());
    t.assert_eq(ump.channel(), 5);
    t.assert_eq(ump.note(), 72);
    t.assert_eq(ump.velocity(), 64);
    return true;
}

bool test_ump32_note_on_all_channels(TestContext& t) {
    for (uint8_t ch = 0; ch < 16; ++ch) {
        auto ump = UMP32::note_on(ch, 60, 100);
        t.assert_eq(ump.channel(), ch);
    }
    return true;
}

bool test_ump32_note_on_all_notes(TestContext& t) {
    for (uint8_t note = 0; note < 128; ++note) {
        auto ump = UMP32::note_on(0, note, 100);
        t.assert_eq(ump.note(), note);
    }
    return true;
}

bool test_ump32_note_on_all_velocities(TestContext& t) {
    for (uint8_t vel = 0; vel < 128; ++vel) {
        auto ump = UMP32::note_on(0, 60, vel);
        t.assert_eq(ump.velocity(), vel);
    }
    return true;
}

bool test_ump32_cc(TestContext& t) {
    auto ump = UMP32::cc(3, 7, 100); // Channel 3, CC7 (Volume), value 100
    t.assert_true(ump.is_cc());
    t.assert_eq(ump.channel(), 3);
    t.assert_eq(ump.cc_number(), 7);
    t.assert_eq(ump.cc_value(), 100);
    return true;
}

bool test_ump32_cc_all_controllers(TestContext& t) {
    for (uint8_t cc = 0; cc < 128; ++cc) {
        auto ump = UMP32::cc(0, cc, 64);
        t.assert_eq(ump.cc_number(), cc);
    }
    return true;
}

bool test_ump32_program_change(TestContext& t) {
    auto ump = UMP32::program_change(7, 42);
    t.assert_true(ump.is_program_change());
    t.assert_eq(ump.channel(), 7);
    t.assert_eq(ump.program(), 42);
    return true;
}

bool test_ump32_pitch_bend(TestContext& t) {
    auto ump = UMP32::pitch_bend(0, 8192); // Center
    t.assert_true(ump.is_pitch_bend());
    t.assert_eq(ump.channel(), 0);
    t.assert_eq(ump.pitch_bend_value(), 8192);

    // Min/Max
    auto min = UMP32::pitch_bend(0, 0);
    auto max = UMP32::pitch_bend(0, 16383);
    t.assert_eq(min.pitch_bend_value(), 0);
    t.assert_eq(max.pitch_bend_value(), 16383);
    return true;
}

bool test_ump32_channel_pressure(TestContext& t) {
    auto ump = UMP32::channel_pressure(2, 80);
    t.assert_true(ump.is_channel_pressure());
    t.assert_eq(ump.channel(), 2);
    t.assert_eq(ump.pressure(), 80);
    return true;
}

bool test_ump32_poly_pressure(TestContext& t) {
    auto ump = UMP32::poly_pressure(4, 60, 90);
    t.assert_true(ump.is_poly_pressure());
    t.assert_eq(ump.channel(), 4);
    t.assert_eq(ump.note(), 60);
    t.assert_eq(ump.data2(), 90); // Poly pressure value is in data2
    return true;
}

bool test_ump32_realtime_messages(TestContext& t) {
    t.assert_true(UMP32::timing_clock().is_timing_clock());
    t.assert_true(UMP32::start().is_start());
    t.assert_true(UMP32::continue_msg().is_continue());
    t.assert_true(UMP32::stop().is_stop());
    t.assert_true(UMP32::active_sensing().is_active_sensing());
    t.assert_true(UMP32::system_reset().is_system_reset());
    return true;
}

bool test_ump32_system_common(TestContext& t) {
    auto tune = UMP32::tune_request();
    t.assert_true(tune.is_tune_request());

    auto mtc = UMP32::mtc_quarter_frame(0x23);
    t.assert_eq(mtc.mtc_data(), 0x23);

    auto spp = UMP32::song_position(1234);
    t.assert_eq(spp.song_position(), 1234);

    auto ss = UMP32::song_select(5);
    t.assert_eq(ss.song_number(), 5);
    return true;
}

// =============================================================================
// UMP64 Tests
// =============================================================================

bool test_ump64_size(TestContext& t) {
    t.assert_eq(sizeof(UMP64), 8);
    return true;
}

bool test_ump64_sysex(TestContext& t) {
    uint8_t data[] = {0x7E, 0x00, 0x06, 0x01};
    auto ump = UMP64::sysex7_complete(0, data, 4); // group=0
    t.assert_eq(ump.mt(), 3);                      // MT=3 for SysEx7
    return true;
}

// =============================================================================
// Parser Tests
// =============================================================================

bool test_parser_note_on(TestContext& t) {
    Parser parser;
    UMP32 ump;

    // Note On: 0x90 0x3C 0x64 (ch0, note 60, vel 100)
    t.assert_true(!parser.parse(0x90, ump)); // Status
    t.assert_true(!parser.parse(0x3C, ump)); // Note
    t.assert_true(parser.parse(0x64, ump));  // Velocity - complete

    t.assert_true(ump.is_note_on());
    t.assert_eq(ump.channel(), 0);
    t.assert_eq(ump.note(), 60);
    t.assert_eq(ump.velocity(), 100);
    return true;
}

bool test_parser_note_off(TestContext& t) {
    Parser parser;
    UMP32 ump;

    t.assert_true(!parser.parse(0x85, ump)); // Note Off ch5
    t.assert_true(!parser.parse(0x48, ump)); // Note 72
    t.assert_true(parser.parse(0x40, ump));  // Velocity 64

    t.assert_true(ump.is_note_off());
    t.assert_eq(ump.channel(), 5);
    t.assert_eq(ump.note(), 72);
    return true;
}

bool test_parser_cc(TestContext& t) {
    Parser parser;
    UMP32 ump;

    t.assert_true(!parser.parse(0xB3, ump)); // CC ch3
    t.assert_true(!parser.parse(0x07, ump)); // CC7 (Volume)
    t.assert_true(parser.parse(0x64, ump));  // Value 100

    t.assert_true(ump.is_cc());
    t.assert_eq(ump.cc_number(), 7);
    t.assert_eq(ump.cc_value(), 100);
    return true;
}

bool test_parser_running_status(TestContext& t) {
    Parser parser;
    UMP32 ump;

    // First Note On - use parse_running for running status support
    t.assert_true(!parser.parse_running(0x90, ump));
    t.assert_true(!parser.parse_running(0x3C, ump));
    t.assert_true(parser.parse_running(0x64, ump));
    t.assert_true(ump.is_note_on());

    // Running status - another Note On without status byte
    t.assert_true(!parser.parse_running(0x40, ump)); // Note 64
    t.assert_true(parser.parse_running(0x50, ump));  // Velocity 80
    t.assert_true(ump.is_note_on());
    t.assert_eq(ump.note(), 64);
    t.assert_eq(ump.velocity(), 80);
    return true;
}

bool test_parser_realtime_interruption(TestContext& t) {
    Parser parser;
    UMP32 ump;

    t.assert_true(!parser.parse(0x90, ump)); // Note On start
    t.assert_true(!parser.parse(0x3C, ump)); // Note byte

    // Timing clock in the middle
    t.assert_true(parser.parse(0xF8, ump));
    t.assert_true(ump.is_timing_clock());

    // Continue Note On
    t.assert_true(parser.parse(0x64, ump));
    t.assert_true(ump.is_note_on());
    t.assert_eq(ump.note(), 60);
    return true;
}

bool test_parser_pitch_bend(TestContext& t) {
    Parser parser;
    UMP32 ump;

    t.assert_true(!parser.parse(0xE0, ump)); // Pitch Bend
    t.assert_true(!parser.parse(0x00, ump)); // LSB
    t.assert_true(parser.parse(0x40, ump));  // MSB (center = 0x2000 = 8192)

    t.assert_true(ump.is_pitch_bend());
    t.assert_eq(ump.pitch_bend_value(), 8192);
    return true;
}

bool test_parser_program_change(TestContext& t) {
    Parser parser;
    UMP32 ump;

    t.assert_true(!parser.parse(0xC7, ump)); // Program Change ch7
    t.assert_true(parser.parse(0x2A, ump));  // Program 42

    t.assert_true(ump.is_program_change());
    t.assert_eq(ump.channel(), 7);
    t.assert_eq(ump.program(), 42);
    return true;
}

bool test_parser_reset(TestContext& t) {
    Parser parser;
    UMP32 ump;

    t.assert_true(!parser.parse(0x90, ump)); // Start Note On
    parser.reset();

    // Should need full message now
    t.assert_true(!parser.parse(0x90, ump));
    t.assert_true(!parser.parse(0x3C, ump));
    t.assert_true(parser.parse(0x64, ump));
    return true;
}

// =============================================================================
// Result Tests
// =============================================================================

bool test_result_ok(TestContext& t) {
    Result<int> r = Ok(42);
    t.assert_true(r.has_value());
    t.assert_eq(r.value(), 42);
    return true;
}

bool test_result_err(TestContext& t) {
    Result<int> r = Err<int>(ErrorCode::INVALID_STATUS);
    t.assert_true(!r.has_value());
    t.assert_eq(r.error().code, ErrorCode::INVALID_STATUS);
    return true;
}

bool test_result_error_context(TestContext& t) {
    auto err = Error::invalid_status(0xF5);
    t.assert_eq(err.code, ErrorCode::INVALID_STATUS);
    t.assert_eq(err.context, 0xF5);
    return true;
}

bool test_result_error_factories(TestContext& t) {
    t.assert_eq(Error::incomplete().code, ErrorCode::INCOMPLETE_MESSAGE);
    t.assert_eq(Error::buffer_overflow().code, ErrorCode::BUFFER_OVERFLOW);
    t.assert_eq(Error::not_implemented().code, ErrorCode::NOT_IMPLEMENTED);
    return true;
}

// =============================================================================
// SysEx Buffer Tests
// =============================================================================

bool test_sysex_buffer_basic(TestContext& t) {
    SysExBuffer<64> buf;

    t.assert_true(buf.push(0x7E));
    t.assert_true(buf.push(0x00));
    t.assert_true(buf.push(0x06));
    t.assert_true(buf.push(0x01));

    t.assert_eq(buf.size(), 4);
    t.assert_eq(buf.data()[0], 0x7E);
    // Note: data()[3] would be out of contiguous range for ring buffer
    // Copy to linear buffer for validation
    uint8_t out[4];
    t.assert_eq(buf.copy_to(out, 4), 4);
    t.assert_eq(out[3], 0x01);
    return true;
}

bool test_sysex_buffer_overflow(TestContext& t) {
    SysExBuffer<4> buf;

    t.assert_true(buf.push(0x01));
    t.assert_true(buf.push(0x02));
    t.assert_true(buf.push(0x03));
    t.assert_true(buf.push(0x04));
    t.assert_true(!buf.push(0x05)); // Overflow
    return true;
}

bool test_sysex_buffer_clear(TestContext& t) {
    SysExBuffer<16> buf;

    (void)buf.push(0x01);
    (void)buf.push(0x02);
    buf.clear();

    t.assert_eq(buf.size(), 0);
    return true;
}

// =============================================================================
// Main
// =============================================================================

int main() {
    Suite s("umidi_core");

    s.section("UMP32");
    s.run("ump32_size", test_ump32_size);
    s.run("ump32_note_on", test_ump32_note_on);
    s.run("ump32_note_off", test_ump32_note_off);
    s.run("ump32_note_on_all_channels", test_ump32_note_on_all_channels);
    s.run("ump32_note_on_all_notes", test_ump32_note_on_all_notes);
    s.run("ump32_note_on_all_velocities", test_ump32_note_on_all_velocities);
    s.run("ump32_cc", test_ump32_cc);
    s.run("ump32_cc_all_controllers", test_ump32_cc_all_controllers);
    s.run("ump32_program_change", test_ump32_program_change);
    s.run("ump32_pitch_bend", test_ump32_pitch_bend);
    s.run("ump32_channel_pressure", test_ump32_channel_pressure);
    s.run("ump32_poly_pressure", test_ump32_poly_pressure);
    s.run("ump32_realtime_messages", test_ump32_realtime_messages);
    s.run("ump32_system_common", test_ump32_system_common);

    s.section("UMP64");
    s.run("ump64_size", test_ump64_size);
    s.run("ump64_sysex", test_ump64_sysex);

    s.section("Parser");
    s.run("parser_note_on", test_parser_note_on);
    s.run("parser_note_off", test_parser_note_off);
    s.run("parser_cc", test_parser_cc);
    s.run("parser_running_status", test_parser_running_status);
    s.run("parser_realtime_interruption", test_parser_realtime_interruption);
    s.run("parser_pitch_bend", test_parser_pitch_bend);
    s.run("parser_program_change", test_parser_program_change);
    s.run("parser_reset", test_parser_reset);

    s.section("Result");
    s.run("result_ok", test_result_ok);
    s.run("result_err", test_result_err);
    s.run("result_error_context", test_result_error_context);
    s.run("result_error_factories", test_result_error_factories);

    s.section("SysEx Buffer");
    s.run("sysex_buffer_basic", test_sysex_buffer_basic);
    s.run("sysex_buffer_overflow", test_sysex_buffer_overflow);
    s.run("sysex_buffer_clear", test_sysex_buffer_clear);

    return s.summary();
}

// SPDX-License-Identifier: MIT
// umidi Message Tests - ChannelVoice, System, SysEx
#include <umitest.hh>
#include "messages/channel_voice.hh"
#include "messages/system.hh"
#include "messages/sysex.hh"
#include "messages/utility.hh"
#include "core/ump.hh"
#include <type_traits>

using namespace umidi;
using namespace umitest;
using namespace umidi::message;

// =============================================================================
// Channel Voice Message Tests
// =============================================================================

bool test_channel_voice_note_on_create(TestContext& t) {
    auto msg = NoteOn::create(5, 60, 100);
    t.assert_true(msg.is_valid());
    t.assert_eq(msg.channel(), 5);
    t.assert_eq(msg.note(), 60);
    t.assert_eq(msg.velocity(), 100);
    return true;
}

bool test_channel_voice_note_off_create(TestContext& t) {
    auto msg = NoteOff::create(3, 72, 64);
    t.assert_true(msg.is_valid());
    t.assert_eq(msg.channel(), 3);
    t.assert_eq(msg.note(), 72);
    t.assert_eq(msg.velocity(), 64);
    return true;
}

bool test_channel_voice_cc_create(TestContext& t) {
    auto msg = ControlChange::create(0, 7, 100);  // Volume
    t.assert_true(msg.is_valid());
    t.assert_eq(msg.channel(), 0);
    t.assert_eq(msg.controller(), 7);
    t.assert_eq(msg.value(), 100);
    return true;
}

bool test_channel_voice_program_change_create(TestContext& t) {
    auto msg = ProgramChange::create(9, 42);
    t.assert_true(msg.is_valid());
    t.assert_eq(msg.channel(), 9);
    t.assert_eq(msg.program(), 42);
    return true;
}

bool test_channel_voice_pitch_bend_create(TestContext& t) {
    auto msg = PitchBend::create(0, 8192);  // Center
    t.assert_true(msg.is_valid());
    t.assert_eq(msg.channel(), 0);
    t.assert_eq(msg.value(), 8192);
    t.assert_eq(msg.signed_value(), 0);
    return true;
}

bool test_channel_voice_pitch_bend_signed(TestContext& t) {
    auto msg = PitchBend::create_signed(0, -1000);
    t.assert_true(msg.is_valid());
    t.assert_eq(msg.signed_value(), -1000);
    return true;
}

bool test_channel_voice_channel_pressure_create(TestContext& t) {
    auto msg = ChannelPressure::create(2, 80);
    t.assert_true(msg.is_valid());
    t.assert_eq(msg.channel(), 2);
    t.assert_eq(msg.pressure(), 80);
    return true;
}

bool test_channel_voice_poly_pressure_create(TestContext& t) {
    auto msg = PolyPressure::create(4, 60, 90);
    t.assert_true(msg.is_valid());
    t.assert_eq(msg.channel(), 4);
    t.assert_eq(msg.note(), 60);
    t.assert_eq(msg.pressure(), 90);
    return true;
}

bool test_channel_voice_dispatch(TestContext& t) {
    auto ump = UMP32::note_on(0, 60, 100);
    bool handled = false;
    uint8_t note_val = 0;

    dispatch(ump, [&handled, &note_val](auto&& msg) {
        using T = std::decay_t<decltype(msg)>;
        if constexpr (std::is_same_v<T, NoteOn>) {
            handled = true;
            note_val = msg.note();
        }
    });

    t.assert_true(handled);
    t.assert_eq(note_val, 60);
    return true;
}

// =============================================================================
// System Message Tests
// =============================================================================

bool test_system_timing_clock(TestContext& t) {
    auto msg = TimingClock::create();
    t.assert_true(msg.is_valid());
    t.assert_true(msg.ump.is_timing_clock());
    return true;
}

bool test_system_start(TestContext& t) {
    auto msg = Start::create();
    t.assert_true(msg.is_valid());
    t.assert_true(msg.ump.is_start());
    return true;
}

bool test_system_continue(TestContext& t) {
    auto msg = Continue::create();
    t.assert_true(msg.is_valid());
    t.assert_true(msg.ump.is_continue());
    return true;
}

bool test_system_stop(TestContext& t) {
    auto msg = Stop::create();
    t.assert_true(msg.is_valid());
    t.assert_true(msg.ump.is_stop());
    return true;
}

bool test_system_active_sensing(TestContext& t) {
    auto msg = ActiveSensing::create();
    t.assert_true(msg.is_valid());
    t.assert_true(msg.ump.is_active_sensing());
    return true;
}

bool test_system_reset(TestContext& t) {
    auto msg = SystemReset::create();
    t.assert_true(msg.is_valid());
    t.assert_true(msg.ump.is_system_reset());
    return true;
}

bool test_system_mtc(TestContext& t) {
    auto msg = MidiTimeCode::create(0x23);
    t.assert_true(msg.is_valid());
    t.assert_eq(msg.type_and_value(), 0x23);
    return true;
}

bool test_system_song_position(TestContext& t) {
    auto msg = SongPosition::create(1234);
    t.assert_true(msg.is_valid());
    t.assert_eq(msg.position(), 1234);
    return true;
}

bool test_system_song_position_max(TestContext& t) {
    auto msg = SongPosition::create(16383);
    t.assert_true(msg.is_valid());
    t.assert_eq(msg.position(), 16383);
    return true;
}

bool test_system_song_select(TestContext& t) {
    auto msg = SongSelect::create(5);
    t.assert_true(msg.is_valid());
    t.assert_eq(msg.song(), 5);
    return true;
}

bool test_system_tune_request(TestContext& t) {
    auto msg = TuneRequest::create();
    t.assert_true(msg.is_valid());
    t.assert_true(msg.ump.is_tune_request());
    return true;
}

// =============================================================================
// SysEx Tests (using UMP64 directly)
// =============================================================================

bool test_sysex7_complete(TestContext& t) {
    uint8_t data[] = {0x7E, 0x00, 0x06, 0x01};
    auto ump = UMP64::sysex7_complete(0, data, 4);
    t.assert_eq(ump.mt(), 3);  // MT=3 for SysEx7
    return true;
}

bool test_sysex7_bytes_access(TestContext& t) {
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    auto ump = UMP64::sysex7_complete(0, data, 6);
    t.assert_eq(ump.sysex_num_bytes(), 6);
    return true;
}

// =============================================================================
// Utility Message Tests (using UMP32 directly)
// =============================================================================

bool test_utility_mt0(TestContext& t) {
    // MT=0 is Utility Message Type
    UMP32 ump(0, 0, 0, 0, 0);  // NOOP
    t.assert_eq(ump.mt(), 0);
    return true;
}

bool test_utility_jr_timestamp(TestContext& t) {
    // JR Timestamp: MT=0, Status=0x0020
    uint16_t ts = 1234;
    UMP32 ump(0, 0, 0x00, (ts >> 8) & 0xFF, ts & 0xFF);
    t.assert_eq(ump.mt(), 0);
    return true;
}

// =============================================================================
// Main
// =============================================================================

int main() {
    Suite s("umidi_messages");

    s.section("Channel Voice Messages");
    s.run("channel_voice_note_on_create", test_channel_voice_note_on_create);
    s.run("channel_voice_note_off_create", test_channel_voice_note_off_create);
    s.run("channel_voice_cc_create", test_channel_voice_cc_create);
    s.run("channel_voice_program_change_create", test_channel_voice_program_change_create);
    s.run("channel_voice_pitch_bend_create", test_channel_voice_pitch_bend_create);
    s.run("channel_voice_pitch_bend_signed", test_channel_voice_pitch_bend_signed);
    s.run("channel_voice_channel_pressure_create", test_channel_voice_channel_pressure_create);
    s.run("channel_voice_poly_pressure_create", test_channel_voice_poly_pressure_create);
    s.run("channel_voice_dispatch", test_channel_voice_dispatch);

    s.section("System Messages");
    s.run("system_timing_clock", test_system_timing_clock);
    s.run("system_start", test_system_start);
    s.run("system_continue", test_system_continue);
    s.run("system_stop", test_system_stop);
    s.run("system_active_sensing", test_system_active_sensing);
    s.run("system_reset", test_system_reset);
    s.run("system_mtc", test_system_mtc);
    s.run("system_song_position", test_system_song_position);
    s.run("system_song_position_max", test_system_song_position_max);
    s.run("system_song_select", test_system_song_select);
    s.run("system_tune_request", test_system_tune_request);

    s.section("SysEx");
    s.run("sysex7_complete", test_sysex7_complete);
    s.run("sysex7_bytes_access", test_sysex7_bytes_access);

    s.section("Utility");
    s.run("utility_mt0", test_utility_mt0);
    s.run("utility_jr_timestamp", test_utility_jr_timestamp);

    return s.summary();
}

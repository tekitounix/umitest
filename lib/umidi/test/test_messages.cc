// SPDX-License-Identifier: MIT
// umidi Message Tests - ChannelVoice, System, SysEx
#include "test_framework.hh"
#include "umidi/messages/channel_voice.hh"
#include "umidi/messages/system.hh"
#include "umidi/messages/sysex.hh"
#include "umidi/messages/utility.hh"
#include "umidi/core/ump.hh"
#include <type_traits>

using namespace umidi;
using namespace umidi::test;
using namespace umidi::message;

// =============================================================================
// Channel Voice Message Tests
// =============================================================================

TEST(channel_voice_note_on_create) {
    auto msg = NoteOn::create(5, 60, 100);
    ASSERT(msg.is_valid());
    ASSERT_EQ(msg.channel(), 5);
    ASSERT_EQ(msg.note(), 60);
    ASSERT_EQ(msg.velocity(), 100);
    TEST_PASS();
}

TEST(channel_voice_note_off_create) {
    auto msg = NoteOff::create(3, 72, 64);
    ASSERT(msg.is_valid());
    ASSERT_EQ(msg.channel(), 3);
    ASSERT_EQ(msg.note(), 72);
    ASSERT_EQ(msg.velocity(), 64);
    TEST_PASS();
}

TEST(channel_voice_cc_create) {
    auto msg = ControlChange::create(0, 7, 100);  // Volume
    ASSERT(msg.is_valid());
    ASSERT_EQ(msg.channel(), 0);
    ASSERT_EQ(msg.controller(), 7);
    ASSERT_EQ(msg.value(), 100);
    TEST_PASS();
}

TEST(channel_voice_program_change_create) {
    auto msg = ProgramChange::create(9, 42);
    ASSERT(msg.is_valid());
    ASSERT_EQ(msg.channel(), 9);
    ASSERT_EQ(msg.program(), 42);
    TEST_PASS();
}

TEST(channel_voice_pitch_bend_create) {
    auto msg = PitchBend::create(0, 8192);  // Center
    ASSERT(msg.is_valid());
    ASSERT_EQ(msg.channel(), 0);
    ASSERT_EQ(msg.value(), 8192);
    ASSERT_EQ(msg.signed_value(), 0);
    TEST_PASS();
}

TEST(channel_voice_pitch_bend_signed) {
    auto msg = PitchBend::create_signed(0, -1000);
    ASSERT(msg.is_valid());
    ASSERT_EQ(msg.signed_value(), -1000);
    TEST_PASS();
}

TEST(channel_voice_channel_pressure_create) {
    auto msg = ChannelPressure::create(2, 80);
    ASSERT(msg.is_valid());
    ASSERT_EQ(msg.channel(), 2);
    ASSERT_EQ(msg.pressure(), 80);
    TEST_PASS();
}

TEST(channel_voice_poly_pressure_create) {
    auto msg = PolyPressure::create(4, 60, 90);
    ASSERT(msg.is_valid());
    ASSERT_EQ(msg.channel(), 4);
    ASSERT_EQ(msg.note(), 60);
    ASSERT_EQ(msg.pressure(), 90);
    TEST_PASS();
}

TEST(channel_voice_dispatch) {
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

    ASSERT(handled);
    ASSERT_EQ(note_val, 60);
    TEST_PASS();
}

// =============================================================================
// System Message Tests
// =============================================================================

TEST(system_timing_clock) {
    auto msg = TimingClock::create();
    ASSERT(msg.is_valid());
    ASSERT(msg.ump.is_timing_clock());
    TEST_PASS();
}

TEST(system_start) {
    auto msg = Start::create();
    ASSERT(msg.is_valid());
    ASSERT(msg.ump.is_start());
    TEST_PASS();
}

TEST(system_continue) {
    auto msg = Continue::create();
    ASSERT(msg.is_valid());
    ASSERT(msg.ump.is_continue());
    TEST_PASS();
}

TEST(system_stop) {
    auto msg = Stop::create();
    ASSERT(msg.is_valid());
    ASSERT(msg.ump.is_stop());
    TEST_PASS();
}

TEST(system_active_sensing) {
    auto msg = ActiveSensing::create();
    ASSERT(msg.is_valid());
    ASSERT(msg.ump.is_active_sensing());
    TEST_PASS();
}

TEST(system_reset) {
    auto msg = SystemReset::create();
    ASSERT(msg.is_valid());
    ASSERT(msg.ump.is_system_reset());
    TEST_PASS();
}

TEST(system_mtc) {
    auto msg = MidiTimeCode::create(0x23);
    ASSERT(msg.is_valid());
    ASSERT_EQ(msg.type_and_value(), 0x23);
    TEST_PASS();
}

TEST(system_song_position) {
    auto msg = SongPosition::create(1234);
    ASSERT(msg.is_valid());
    ASSERT_EQ(msg.position(), 1234);
    TEST_PASS();
}

TEST(system_song_position_max) {
    auto msg = SongPosition::create(16383);
    ASSERT(msg.is_valid());
    ASSERT_EQ(msg.position(), 16383);
    TEST_PASS();
}

TEST(system_song_select) {
    auto msg = SongSelect::create(5);
    ASSERT(msg.is_valid());
    ASSERT_EQ(msg.song(), 5);
    TEST_PASS();
}

TEST(system_tune_request) {
    auto msg = TuneRequest::create();
    ASSERT(msg.is_valid());
    ASSERT(msg.ump.is_tune_request());
    TEST_PASS();
}

// =============================================================================
// SysEx Tests (using UMP64 directly)
// =============================================================================

TEST(sysex7_complete) {
    uint8_t data[] = {0x7E, 0x00, 0x06, 0x01};
    auto ump = UMP64::sysex7_complete(0, data, 4);
    ASSERT_EQ(ump.mt(), 3);  // MT=3 for SysEx7
    TEST_PASS();
}

TEST(sysex7_bytes_access) {
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    auto ump = UMP64::sysex7_complete(0, data, 6);
    ASSERT_EQ(ump.sysex_num_bytes(), 6);
    TEST_PASS();
}

// =============================================================================
// Utility Message Tests (using UMP32 directly)
// =============================================================================

TEST(utility_mt0) {
    // MT=0 is Utility Message Type
    UMP32 ump(0, 0, 0, 0, 0);  // NOOP
    ASSERT_EQ(ump.mt(), 0);
    TEST_PASS();
}

TEST(utility_jr_timestamp) {
    // JR Timestamp: MT=0, Status=0x0020
    uint16_t ts = 1234;
    UMP32 ump(0, 0, 0x00, (ts >> 8) & 0xFF, ts & 0xFF);
    ASSERT_EQ(ump.mt(), 0);
    TEST_PASS();
}

// =============================================================================
// Main
// =============================================================================

int main() {
    printf("=== umidi Message Tests ===\n");

    SECTION("Channel Voice Messages");
    RUN_TEST(channel_voice_note_on_create);
    RUN_TEST(channel_voice_note_off_create);
    RUN_TEST(channel_voice_cc_create);
    RUN_TEST(channel_voice_program_change_create);
    RUN_TEST(channel_voice_pitch_bend_create);
    RUN_TEST(channel_voice_pitch_bend_signed);
    RUN_TEST(channel_voice_channel_pressure_create);
    RUN_TEST(channel_voice_poly_pressure_create);
    RUN_TEST(channel_voice_dispatch);

    SECTION("System Messages");
    RUN_TEST(system_timing_clock);
    RUN_TEST(system_start);
    RUN_TEST(system_continue);
    RUN_TEST(system_stop);
    RUN_TEST(system_active_sensing);
    RUN_TEST(system_reset);
    RUN_TEST(system_mtc);
    RUN_TEST(system_song_position);
    RUN_TEST(system_song_position_max);
    RUN_TEST(system_song_select);
    RUN_TEST(system_tune_request);

    SECTION("SysEx");
    RUN_TEST(sysex7_complete);
    RUN_TEST(sysex7_bytes_access);

    SECTION("Utility");
    RUN_TEST(utility_mt0);
    RUN_TEST(utility_jr_timestamp);

    return summary();
}

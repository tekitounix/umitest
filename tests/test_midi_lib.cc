// SPDX-License-Identifier: MIT
// UMI-OS MIDI Library Tests
// Tests for UMP-Opt format MIDI library

#include <umidi/umidi.hh>
#include <cstdio>

// =============================================================================
// Test Framework (minimal)
// =============================================================================

static int tests_passed = 0;
static int tests_failed = 0;

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
// UMP32 Tests
// =============================================================================

TEST(ump32_size) {
    ASSERT_EQ(sizeof(umidi::UMP32), 4u);
}

TEST(ump32_note_on) {
    auto ump = umidi::UMP32::note_on(0, 60, 100);
    ASSERT(ump.is_note_on());
    ASSERT(!ump.is_note_off());
    ASSERT_EQ(ump.channel(), 0);
    ASSERT_EQ(ump.note(), 60);
    ASSERT_EQ(ump.velocity(), 100);
    ASSERT_EQ(ump.mt(), 2);
}

TEST(ump32_note_off) {
    auto ump = umidi::UMP32::note_off(1, 64, 0);
    ASSERT(ump.is_note_off());
    ASSERT(!ump.is_note_on());
    ASSERT_EQ(ump.channel(), 1);
    ASSERT_EQ(ump.note(), 64);
}

TEST(ump32_note_on_vel0_is_note_off) {
    auto ump = umidi::UMP32::note_on(0, 60, 0);
    ASSERT(ump.is_note_off());
    ASSERT(!ump.is_note_on());
}

TEST(ump32_cc) {
    auto ump = umidi::UMP32::cc(2, 7, 100);  // CC7 = Volume
    ASSERT(ump.is_cc());
    ASSERT_EQ(ump.channel(), 2);
    ASSERT_EQ(ump.cc_number(), 7);
    ASSERT_EQ(ump.cc_value(), 100);
}

TEST(ump32_pitch_bend) {
    auto ump = umidi::UMP32::pitch_bend(0, 8192);  // Center
    ASSERT(ump.is_pitch_bend());
    ASSERT_EQ(ump.pitch_bend_value(), 8192);
}

TEST(ump32_program_change) {
    auto ump = umidi::UMP32::program_change(0, 42);
    ASSERT(ump.is_program_change());
    ASSERT_EQ(ump.data1(), 42);
}

TEST(ump32_timing_clock) {
    auto ump = umidi::UMP32::timing_clock();
    ASSERT(ump.is_realtime());
    ASSERT(ump.is_system());
    ASSERT_EQ(ump.status(), 0xF8);
}

TEST(ump32_raw_word) {
    // Note On C4 velocity 100 on channel 0
    // MT=2, Group=0, Status=0x90, Data1=60, Data2=100
    // Expected: 0x20906064
    auto ump = umidi::UMP32::note_on(0, 60, 100);
    ASSERT_EQ(ump.raw(), 0x20903C64u);
}

// =============================================================================
// Parser Tests
// =============================================================================

TEST(parser_note_on) {
    umidi::Parser parser;
    umidi::UMP32 ump;

    ASSERT(!parser.parse(0x90, ump));  // Status
    ASSERT(!parser.parse(60, ump));    // Note
    ASSERT(parser.parse(100, ump));    // Velocity

    ASSERT(ump.is_note_on());
    ASSERT_EQ(ump.note(), 60);
    ASSERT_EQ(ump.velocity(), 100);
}

TEST(parser_note_off) {
    umidi::Parser parser;
    umidi::UMP32 ump;

    ASSERT(!parser.parse(0x80, ump));
    ASSERT(!parser.parse(64, ump));
    ASSERT(parser.parse(0, ump));

    ASSERT(ump.is_note_off());
    ASSERT_EQ(ump.note(), 64);
}

TEST(parser_cc) {
    umidi::Parser parser;
    umidi::UMP32 ump;

    ASSERT(!parser.parse(0xB0, ump));  // CC status
    ASSERT(!parser.parse(7, ump));     // CC number
    ASSERT(parser.parse(100, ump));    // Value

    ASSERT(ump.is_cc());
    ASSERT_EQ(ump.cc_number(), 7);
    ASSERT_EQ(ump.cc_value(), 100);
}

TEST(parser_program_change) {
    umidi::Parser parser;
    umidi::UMP32 ump;

    ASSERT(!parser.parse(0xC0, ump));  // PC status
    ASSERT(parser.parse(42, ump));     // Program number (2-byte message)

    ASSERT(ump.is_program_change());
    ASSERT_EQ(ump.data1(), 42);
}

TEST(parser_realtime) {
    umidi::Parser parser;
    umidi::UMP32 ump;

    // Realtime messages are single-byte
    ASSERT(parser.parse(0xF8, ump));  // Timing Clock

    ASSERT(ump.is_realtime());
    ASSERT_EQ(ump.status(), 0xF8);
}

TEST(parser_running_status) {
    umidi::Parser parser;
    umidi::UMP32 ump;

    // First note
    ASSERT(!parser.parse_running(0x90, ump));
    ASSERT(!parser.parse_running(60, ump));
    ASSERT(parser.parse_running(100, ump));
    ASSERT(ump.is_note_on());
    ASSERT_EQ(ump.note(), 60);

    // Second note with running status
    ASSERT(!parser.parse_running(64, ump));
    ASSERT(parser.parse_running(80, ump));
    ASSERT(ump.is_note_on());
    ASSERT_EQ(ump.note(), 64);
    ASSERT_EQ(ump.velocity(), 80);
}

// =============================================================================
// Serializer Tests
// =============================================================================

TEST(serializer_note_on) {
    auto ump = umidi::UMP32::note_on(0, 60, 100);
    uint8_t out[3];

    size_t len = umidi::Serializer::serialize(ump, out);
    ASSERT_EQ(len, 3u);
    ASSERT_EQ(out[0], 0x90);
    ASSERT_EQ(out[1], 60);
    ASSERT_EQ(out[2], 100);
}

TEST(serializer_program_change) {
    auto ump = umidi::UMP32::program_change(1, 42);
    uint8_t out[3];

    size_t len = umidi::Serializer::serialize(ump, out);
    ASSERT_EQ(len, 2u);  // 2-byte message
    ASSERT_EQ(out[0], 0xC1);
    ASSERT_EQ(out[1], 42);
}

TEST(serializer_realtime) {
    auto ump = umidi::UMP32::timing_clock();
    uint8_t out[3];

    size_t len = umidi::Serializer::serialize(ump, out);
    ASSERT_EQ(len, 1u);  // 1-byte message
    ASSERT_EQ(out[0], 0xF8);
}

// =============================================================================
// Event Tests
// =============================================================================

TEST(event_size) {
    ASSERT_EQ(sizeof(umidi::Event), 8u);
}

TEST(event_note_on) {
    auto e = umidi::Event::note_on(100, 0, 60, 100);
    ASSERT_EQ(e.sample_pos, 100u);
    ASSERT(e.is_note_on());
    ASSERT_EQ(e.note(), 60);
    ASSERT_EQ(e.velocity(), 100);
}

TEST(event_cc) {
    auto e = umidi::Event::cc(200, 1, 7, 64);
    ASSERT_EQ(e.sample_pos, 200u);
    ASSERT(e.is_cc());
    ASSERT_EQ(e.channel(), 1);
    ASSERT_EQ(e.cc_number(), 7);
    ASSERT_EQ(e.cc_value(), 64);
}

// =============================================================================
// EventQueue Tests
// =============================================================================

TEST(event_queue_push_pop) {
    umidi::EventQueue<16> queue;

    ASSERT(queue.empty());
    ASSERT_EQ(queue.size(), 0u);

    auto e1 = umidi::Event::note_on(0, 0, 60, 100);
    ASSERT(queue.push(e1));
    ASSERT_EQ(queue.size(), 1u);

    umidi::Event out;
    ASSERT(queue.pop(out));
    ASSERT_EQ(out.note(), 60);
    ASSERT(queue.empty());
}

TEST(event_queue_pop_until) {
    umidi::EventQueue<16> queue;

    (void)queue.push(umidi::Event::note_on(10, 0, 60, 100));
    (void)queue.push(umidi::Event::note_on(20, 0, 64, 80));
    (void)queue.push(umidi::Event::note_on(30, 0, 67, 90));

    umidi::Event out;

    // Pop events up to sample 15
    ASSERT(queue.pop_until(15, out));
    ASSERT_EQ(out.sample_pos, 10u);
    ASSERT(!queue.pop_until(15, out));  // Next event is at 20

    // Pop events up to sample 25
    ASSERT(queue.pop_until(25, out));
    ASSERT_EQ(out.sample_pos, 20u);

    // Pop remaining
    ASSERT(queue.pop_until(100, out));
    ASSERT_EQ(out.sample_pos, 30u);

    ASSERT(queue.empty());
}

// =============================================================================
// Message Type Tests
// =============================================================================

TEST(message_note_on) {
    auto msg = umidi::message::NoteOn::create(0, 60, 100);
    ASSERT(msg.is_valid());
    ASSERT_EQ(msg.channel(), 0);
    ASSERT_EQ(msg.note(), 60);
    ASSERT_EQ(msg.velocity(), 100);
}

TEST(message_cc) {
    auto msg = umidi::message::ControlChange::create(1, 7, 64);
    ASSERT(msg.is_valid());
    ASSERT_EQ(msg.channel(), 1);
    ASSERT_EQ(msg.controller(), 7);
    ASSERT_EQ(msg.value(), 64);
}

TEST(message_pitch_bend) {
    auto msg = umidi::message::PitchBend::create(0, 8192);
    ASSERT(msg.is_valid());
    ASSERT_EQ(msg.value(), 8192u);
    ASSERT_EQ(msg.signed_value(), 0);  // Center = 0

    auto msg2 = umidi::message::PitchBend::create_signed(0, -1000);
    ASSERT_EQ(msg2.signed_value(), -1000);
}

// =============================================================================
// Conversion Tests
// =============================================================================

TEST(convert_velocity) {
    using namespace umidi::convert;

    // 7-bit to 16-bit and back
    ASSERT_EQ(velocity_16_to_7(velocity_7_to_16(100)), 100);
    ASSERT_EQ(velocity_16_to_7(velocity_7_to_16(1)), 1);
    ASSERT_EQ(velocity_16_to_7(velocity_7_to_16(0)), 0);

    // Edge case: very low 16-bit velocity should not become 0
    ASSERT(velocity_16_to_7(1) >= 1);
}

TEST(convert_cc) {
    using namespace umidi::convert;

    // 7-bit to 14-bit and back
    ASSERT_EQ(cc_14_to_7(cc_7_to_14(64)), 64);

    // 7-bit to 32-bit and back
    ASSERT_EQ(cc_32_to_7(cc_7_to_32(127)), 127);
}

// =============================================================================
// CC Type Tests
// =============================================================================

TEST(cc_sustain_pedal) {
    using Sustain = umidi::cc::SustainPedal;

    ASSERT_EQ(Sustain::number(), 64);

    Sustain::State state;
    ASSERT(Sustain::parse(state, 127));   // On
    ASSERT(!Sustain::parse(state, 63));   // Off
    ASSERT(Sustain::parse(state, 64));    // On (threshold)
}

TEST(cc_14bit_volume) {
    using Volume = umidi::cc::ChannelVolume;

    ASSERT_EQ(Volume::number_msb(), 7);
    ASSERT_EQ(Volume::number_lsb(), 39);  // 7 + 32

    Volume::State state;
    uint16_t val1 = Volume::parse_msb(state, 100);
    ASSERT_EQ(val1, 100u << 7);

    uint16_t val2 = Volume::parse_lsb(state, 50);
    ASSERT_EQ(val2, (100u << 7) | 50);
}

// =============================================================================
// Compatibility Tests
// =============================================================================

TEST(compat_from_midi_bytes) {
    using namespace umidi::compat;

    // Note On
    auto e1 = from_midi_bytes(100, 0x90, 60, 100);
    ASSERT(e1.is_note_on());
    ASSERT_EQ(e1.sample_pos, 100u);
    ASSERT_EQ(e1.note(), 60);

    // Note On with velocity 0 should become Note Off
    auto e2 = from_midi_bytes(200, 0x90, 64, 0);
    ASSERT(e2.is_note_off());

    // CC
    auto e3 = from_midi_bytes(300, 0xB0, 7, 100);
    ASSERT(e3.is_cc());
}

TEST(compat_to_midi_bytes) {
    using namespace umidi::compat;

    auto e = umidi::Event::note_on(0, 1, 62, 80);
    uint8_t out[3];
    size_t len = to_midi_bytes(e, out);

    ASSERT_EQ(len, 3u);
    ASSERT_EQ(out[0], 0x91);  // Note On channel 1
    ASSERT_EQ(out[1], 62);
    ASSERT_EQ(out[2], 80);
}

// =============================================================================
// Main
// =============================================================================

int main() {
    printf("=== UMI-OS MIDI Library Tests ===\n\n");

    printf("UMP32 Tests:\n");
    RUN_TEST(ump32_size);
    RUN_TEST(ump32_note_on);
    RUN_TEST(ump32_note_off);
    RUN_TEST(ump32_note_on_vel0_is_note_off);
    RUN_TEST(ump32_cc);
    RUN_TEST(ump32_pitch_bend);
    RUN_TEST(ump32_program_change);
    RUN_TEST(ump32_timing_clock);
    RUN_TEST(ump32_raw_word);

    printf("\nParser Tests:\n");
    RUN_TEST(parser_note_on);
    RUN_TEST(parser_note_off);
    RUN_TEST(parser_cc);
    RUN_TEST(parser_program_change);
    RUN_TEST(parser_realtime);
    RUN_TEST(parser_running_status);

    printf("\nSerializer Tests:\n");
    RUN_TEST(serializer_note_on);
    RUN_TEST(serializer_program_change);
    RUN_TEST(serializer_realtime);

    printf("\nEvent Tests:\n");
    RUN_TEST(event_size);
    RUN_TEST(event_note_on);
    RUN_TEST(event_cc);

    printf("\nEventQueue Tests:\n");
    RUN_TEST(event_queue_push_pop);
    RUN_TEST(event_queue_pop_until);

    printf("\nMessage Type Tests:\n");
    RUN_TEST(message_note_on);
    RUN_TEST(message_cc);
    RUN_TEST(message_pitch_bend);

    printf("\nConversion Tests:\n");
    RUN_TEST(convert_velocity);
    RUN_TEST(convert_cc);

    printf("\nCC Type Tests:\n");
    RUN_TEST(cc_sustain_pedal);
    RUN_TEST(cc_14bit_volume);

    printf("\nCompatibility Tests:\n");
    RUN_TEST(compat_from_midi_bytes);
    RUN_TEST(compat_to_midi_bytes);

    printf("\n=================================\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    printf("=================================\n");

    return tests_failed > 0 ? 1 : 0;
}

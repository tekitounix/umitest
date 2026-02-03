// SPDX-License-Identifier: MIT
// UMI-OS Kernel MIDI Tests
// Tests for umi_midi.hh (kernel internal MIDI processing)

#include <array>
#include <cmath>
#include <umios/kernel/umi_midi.hh>
#include <umitest.hh>

using namespace umitest;

// =====================================
// Tests
// =====================================

bool test_midi_message(TestContext& t) {
    // Note On
    auto note_on = umi::midi::Message::note_on(0, 60, 100);
    t.assert_true(note_on.is_note_on(), "note_on is note on");
    t.assert_true(!note_on.is_note_off(), "note_on is not note off");
    t.assert_eq(note_on.channel(), 0u);
    t.assert_eq(note_on.note(), 60u);
    t.assert_eq(note_on.velocity(), 100u);

    // Note Off
    auto note_off = umi::midi::Message::note_off(1, 60, 64);
    t.assert_true(note_off.is_note_off(), "note_off is note off");
    t.assert_true(!note_off.is_note_on(), "note_off is not note on");
    t.assert_eq(note_off.channel(), 1u);

    // Note On with velocity 0 = Note Off
    auto note_on_v0 = umi::midi::Message::note_on(0, 60, 0);
    t.assert_true(note_on_v0.is_note_off(), "note_on v0 is note off");

    // Control Change
    auto cc = umi::midi::Message::control_change(2, 7, 100);
    t.assert_true(cc.is_cc(), "cc is control change");
    t.assert_eq(cc.channel(), 2u);
    t.assert_eq(cc.cc_number(), 7u);
    t.assert_eq(cc.cc_value(), 100u);

    // Pitch Bend
    auto pb = umi::midi::Message::pitch_bend(0, 0);
    t.assert_true(pb.is_pitch_bend(), "pitch_bend type");
    t.assert_eq(pb.pitch_bend_value(), 0);

    auto pb_up = umi::midi::Message::pitch_bend(0, 8191);
    t.assert_eq(pb_up.pitch_bend_value(), 8191);

    auto pb_down = umi::midi::Message::pitch_bend(0, -8192);
    t.assert_eq(pb_down.pitch_bend_value(), -8192);

    // Program Change
    auto pc = umi::midi::Message::program_change(3, 42);
    t.assert_true(pc.is_program_change(), "program_change type");
    t.assert_eq(pc.channel(), 3u);
    t.assert_eq(pc.program(), 42u);

    // Size check
    t.assert_eq(sizeof(umi::midi::Message), 4u);
    t.assert_eq(sizeof(umi::midi::Event), 8u);
    return true;
}

bool test_midi_utilities(TestContext& t) {
    // Note to frequency
    float a4 = umi::midi::note_to_freq(69);
    t.assert_near(a4, 440.0f, 0.01f);

    float a5 = umi::midi::note_to_freq(81);
    t.assert_near(a5, 880.0f, 0.1f);

    float c4 = umi::midi::note_to_freq(60);
    t.assert_near(c4, 261.63f, 0.1f);

    // Velocity to amplitude
    t.assert_eq(umi::midi::velocity_to_amplitude(127), 1.0f);
    t.assert_eq(umi::midi::velocity_to_amplitude(0), 0.0f);
    return true;
}

bool test_event_buffer(TestContext& t) {
    umi::midi::EventBuffer<16> buffer;
    t.assert_true(buffer.empty(), "buffer initially empty");
    t.assert_eq(buffer.capacity(), 16u);

    buffer.push(umi::midi::Message::note_on(0, 60, 100), 0);
    buffer.push(umi::midi::Message::note_off(0, 60), 64);

    t.assert_eq(buffer.size(), 2u);
    t.assert_true(!buffer.empty(), "buffer not empty");

    auto events = buffer.events();
    t.assert_true(events[0].msg.is_note_on(), "first event is note on");
    t.assert_eq(events[0].offset, 0u);
    t.assert_true(events[1].msg.is_note_off(), "second event is note off");
    t.assert_eq(events[1].offset, 64u);

    buffer.clear();
    t.assert_true(buffer.empty(), "buffer empty after clear");
    return true;
}

bool test_cross_platform_compatibility(TestContext& t) {
    t.assert_true(std::is_trivially_copyable_v<umi::midi::Message>, "Message is trivially copyable");
    t.assert_true(std::is_trivially_copyable_v<umi::midi::Event>, "Event is trivially copyable");
    t.assert_eq(sizeof(umi::midi::Message), 4u);
    t.assert_eq(sizeof(umi::midi::Event), 8u);

    // CC normalized values
    auto cc = umi::midi::Message::control_change(0, 1, 127);
    t.assert_near(cc.cc_normalized(), 1.0f, 0.001f);

    auto cc_half = umi::midi::Message::control_change(0, 1, 64);
    t.assert_near(cc_half.cc_normalized(), 0.504f, 0.01f);

    // Pitch bend normalized
    auto pb_center = umi::midi::Message::pitch_bend(0, 0);
    t.assert_near(pb_center.pitch_bend_normalized(), 0.0f, 0.001f);

    auto pb_max = umi::midi::Message::pitch_bend(0, 8191);
    t.assert_near(pb_max.pitch_bend_normalized(), 1.0f, 0.001f);
    return true;
}

bool test_system_messages(TestContext& t) {
    // Timing Clock
    auto clock = umi::midi::Message::timing_clock();
    t.assert_true(clock.is_realtime(), "timing_clock is realtime");
    t.assert_eq(clock.status, 0xF8u);

    // Start/Stop/Continue
    auto start = umi::midi::Message::start();
    t.assert_eq(start.status, 0xFAu);

    auto stop = umi::midi::Message::stop();
    t.assert_eq(stop.status, 0xFCu);

    auto cont = umi::midi::Message::continue_();
    t.assert_eq(cont.status, 0xFBu);
    return true;
}

bool test_event_queue_spsc(TestContext& t) {
    umi::midi::EventQueue<16> queue;

    t.assert_true(queue.empty_approx(), "queue initially empty");
    t.assert_eq(queue.size_approx(), 0u);
    t.assert_true(queue.has_space(), "queue has space");
    t.assert_true(!queue.try_pop().has_value(), "pop from empty returns nullopt");

    bool pushed = queue.try_push(umi::midi::Message::note_on(0, 60, 100), 0);
    t.assert_true(pushed, "push single event");
    t.assert_eq(queue.size_approx(), 1u);
    t.assert_true(!queue.empty_approx(), "queue not empty after push");

    auto peeked = queue.peek();
    t.assert_true(peeked.has_value(), "peek returns value");
    t.assert_eq(peeked->msg.note(), 60u);
    t.assert_eq(queue.size_approx(), 1u);

    auto popped = queue.try_pop();
    t.assert_true(popped.has_value(), "pop returns value");
    t.assert_true(popped->msg.is_note_on(), "popped event is note on");
    t.assert_eq(popped->msg.note(), 60u);
    t.assert_eq(popped->offset, 0u);
    t.assert_true(queue.empty_approx(), "queue empty after pop");

    for (std::size_t i = 0; i < 15; ++i) {
        bool ok = queue.try_push(umi::midi::Message::note_on(0, static_cast<std::uint8_t>(i), 100),
                                 static_cast<std::uint32_t>(i));
        t.assert_true(ok, "push to queue");
    }
    t.assert_eq(queue.size_approx(), 15u);
    t.assert_true(!queue.has_space(), "queue full");

    bool push_fail = queue.try_push(umi::midi::Message::note_on(0, 99, 99));
    t.assert_true(!push_fail, "push to full queue fails");

    for (std::size_t i = 0; i < 15; ++i) {
        auto event = queue.try_pop();
        t.assert_true(event.has_value(), "pop from queue");
        t.assert_eq(event->msg.note(), static_cast<uint8_t>(i));
        t.assert_eq(event->offset, static_cast<uint32_t>(i));
    }
    t.assert_true(queue.empty_approx(), "queue empty after popping all");
    return true;
}

bool test_event_reader(TestContext& t) {
    umi::midi::EventQueue<16> queue;

    queue.try_push(umi::midi::Message::note_on(0, 60, 100), 0);
    queue.try_push(umi::midi::Message::control_change(0, 1, 64), 32);
    queue.try_push(umi::midi::Message::note_on(0, 64, 100), 32);
    queue.try_push(umi::midi::Message::note_off(0, 60), 64);
    queue.try_push(umi::midi::Message::note_off(0, 64), 128);

    umi::midi::EventReader<16> reader;
    reader.read_from(queue);

    t.assert_eq(reader.size(), 5u);
    t.assert_true(!reader.empty(), "reader not empty");
    t.assert_true(queue.empty_approx(), "queue empty after read");

    auto at_0 = reader.events_at(0);
    t.assert_eq(at_0.size(), 1u);
    t.assert_true(at_0[0].msg.is_note_on(), "event at 0 is note on");
    t.assert_eq(at_0[0].msg.note(), 60u);

    auto at_32 = reader.events_at(32);
    t.assert_eq(at_32.size(), 2u);

    auto at_64 = reader.events_at(64);
    t.assert_eq(at_64.size(), 1u);
    t.assert_true(at_64[0].msg.is_note_off(), "event at 64 is note off");

    auto remaining = reader.remaining();
    t.assert_eq(remaining.size(), 1u);
    t.assert_eq(remaining[0].offset, 128u);

    auto all = reader.all();
    t.assert_eq(all.size(), 5u);
    return true;
}

bool test_read_all_batch(TestContext& t) {
    umi::midi::EventQueue<16> queue;

    for (std::size_t i = 0; i < 5; ++i) {
        queue.try_push(umi::midi::Message::note_on(0, static_cast<std::uint8_t>(60 + i), 100),
                       static_cast<std::uint32_t>(i * 10));
    }

    std::array<umi::midi::Event, 8> dest{};
    std::size_t read = queue.read_all(std::span(dest));

    t.assert_eq(read, 5u);
    t.assert_true(queue.empty_approx(), "queue empty after read_all");

    for (std::size_t i = 0; i < 5; ++i) {
        t.assert_eq(dest[i].msg.note(), static_cast<uint8_t>(60 + i));
        t.assert_eq(dest[i].offset, static_cast<uint32_t>(i * 10));
    }
    return true;
}

int main() {
    Suite s("umios/kernel/midi");

    s.section("MIDI Message");
    s.run("midi_message", test_midi_message);

    s.section("MIDI Utilities");
    s.run("midi_utilities", test_midi_utilities);

    s.section("Event Buffer");
    s.run("event_buffer", test_event_buffer);

    s.section("Cross-Platform Compatibility");
    s.run("cross_platform_compatibility", test_cross_platform_compatibility);

    s.section("System Messages");
    s.run("system_messages", test_system_messages);

    s.section("EventQueue (Lock-free SPSC)");
    s.run("event_queue_spsc", test_event_queue_spsc);

    s.section("EventReader (Sample-Accurate Processing)");
    s.run("event_reader", test_event_reader);

    s.section("read_all (batch read)");
    s.run("read_all_batch", test_read_all_batch);

    return s.summary();
}

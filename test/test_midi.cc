#include <core/umi_midi.hh>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <cmath>

// Test utilities
static int test_count = 0;
static int pass_count = 0;

void check(bool cond, const char* msg) {
    ++test_count;
    if (cond) {
        ++pass_count;
    } else {
        std::printf("FAIL: %s\n", msg);
    }
}

// =====================================
// Tests
// =====================================

void test_midi_message() {
    std::printf("Test: MIDI Message...\n");
    
    // Note On
    auto note_on = umi::midi::Message::note_on(0, 60, 100);
    check(note_on.is_note_on(), "note_on is note on");
    check(!note_on.is_note_off(), "note_on is not note off");
    check(note_on.channel() == 0, "note_on channel");
    check(note_on.note() == 60, "note_on note");
    check(note_on.velocity() == 100, "note_on velocity");
    
    // Note Off
    auto note_off = umi::midi::Message::note_off(1, 60, 64);
    check(note_off.is_note_off(), "note_off is note off");
    check(!note_off.is_note_on(), "note_off is not note on");
    check(note_off.channel() == 1, "note_off channel");
    
    // Note On with velocity 0 = Note Off
    auto note_on_v0 = umi::midi::Message::note_on(0, 60, 0);
    check(note_on_v0.is_note_off(), "note_on v0 is note off");
    
    // Control Change
    auto cc = umi::midi::Message::control_change(2, 7, 100);
    check(cc.is_cc(), "cc is control change");
    check(cc.channel() == 2, "cc channel");
    check(cc.cc_number() == 7, "cc number");
    check(cc.cc_value() == 100, "cc value");
    
    // Pitch Bend
    auto pb = umi::midi::Message::pitch_bend(0, 0);
    check(pb.is_pitch_bend(), "pitch_bend type");
    check(pb.pitch_bend_value() == 0, "pitch_bend center");
    
    auto pb_up = umi::midi::Message::pitch_bend(0, 8191);
    check(pb_up.pitch_bend_value() == 8191, "pitch_bend max up");
    
    auto pb_down = umi::midi::Message::pitch_bend(0, -8192);
    check(pb_down.pitch_bend_value() == -8192, "pitch_bend max down");
    
    // Program Change
    auto pc = umi::midi::Message::program_change(3, 42);
    check(pc.is_program_change(), "program_change type");
    check(pc.channel() == 3, "program_change channel");
    check(pc.program() == 42, "program_change program");
    
    // Size check
    check(sizeof(umi::midi::Message) == 4, "Message size is 4 bytes");
    check(sizeof(umi::midi::Event) == 8, "Event size is 8 bytes");
}

void test_midi_utilities() {
    std::printf("Test: MIDI Utilities...\n");
    
    // Note to frequency
    float a4 = umi::midi::note_to_freq(69);
    check(std::abs(a4 - 440.0f) < 0.01f, "A4 = 440Hz");
    
    float a5 = umi::midi::note_to_freq(81);
    check(std::abs(a5 - 880.0f) < 0.1f, "A5 = 880Hz");
    
    float c4 = umi::midi::note_to_freq(60);
    check(std::abs(c4 - 261.63f) < 0.1f, "C4 ~= 261.63Hz");
    
    // Velocity to amplitude
    check(umi::midi::velocity_to_amplitude(127) == 1.0f, "velocity 127 = 1.0");
    check(umi::midi::velocity_to_amplitude(0) == 0.0f, "velocity 0 = 0.0");
}

void test_event_buffer() {
    std::printf("Test: Event Buffer...\n");
    
    umi::midi::EventBuffer<16> buffer;
    check(buffer.empty(), "buffer initially empty");
    check(buffer.capacity() == 16, "buffer capacity");
    
    buffer.push(umi::midi::Message::note_on(0, 60, 100), 0);
    buffer.push(umi::midi::Message::note_off(0, 60), 64);
    
    check(buffer.size() == 2, "buffer has 2 events");
    check(!buffer.empty(), "buffer not empty");
    
    auto events = buffer.events();
    check(events[0].msg.is_note_on(), "first event is note on");
    check(events[0].offset == 0, "first event offset");
    check(events[1].msg.is_note_off(), "second event is note off");
    check(events[1].offset == 64, "second event offset");
    
    buffer.clear();
    check(buffer.empty(), "buffer empty after clear");
}

void test_cross_platform_compatibility() {
    std::printf("Test: Cross-Platform Compatibility...\n");
    
    // Verify structures are POD-like and have expected sizes
    check(std::is_trivially_copyable_v<umi::midi::Message>, "Message is trivially copyable");
    check(std::is_trivially_copyable_v<umi::midi::Event>, "Event is trivially copyable");
    check(sizeof(umi::midi::Message) == 4, "Message is 4 bytes (32-bit aligned)");
    check(sizeof(umi::midi::Event) == 8, "Event is 8 bytes (64-bit aligned)");
    
    // CC normalized values
    auto cc = umi::midi::Message::control_change(0, 1, 127);
    check(std::abs(cc.cc_normalized() - 1.0f) < 0.001f, "CC 127 normalized to 1.0");
    
    auto cc_half = umi::midi::Message::control_change(0, 1, 64);
    check(std::abs(cc_half.cc_normalized() - 0.504f) < 0.01f, "CC 64 normalized ~0.5");
    
    // Pitch bend normalized
    auto pb_center = umi::midi::Message::pitch_bend(0, 0);
    check(std::abs(pb_center.pitch_bend_normalized()) < 0.001f, "PB center normalized to 0");
    
    auto pb_max = umi::midi::Message::pitch_bend(0, 8191);
    check(std::abs(pb_max.pitch_bend_normalized() - 1.0f) < 0.001f, "PB max normalized to ~1.0");
}

void test_system_messages() {
    std::printf("Test: System Messages...\n");
    
    // Timing Clock
    auto clock = umi::midi::Message::timing_clock();
    check(clock.is_realtime(), "timing_clock is realtime");
    check(clock.status == 0xF8, "timing_clock status");
    
    // Start/Stop/Continue
    auto start = umi::midi::Message::start();
    check(start.status == 0xFA, "start status");
    
    auto stop = umi::midi::Message::stop();
    check(stop.status == 0xFC, "stop status");
    
    auto cont = umi::midi::Message::continue_();
    check(cont.status == 0xFB, "continue status");
}

void test_event_queue_spsc() {
    std::printf("Test: EventQueue (Lock-free SPSC)...\n");
    
    umi::midi::EventQueue<16> queue;  // Must be power of 2
    
    // Initially empty
    check(queue.empty_approx(), "queue initially empty");
    check(queue.size_approx() == 0, "queue size is 0");
    check(queue.has_space(), "queue has space");
    check(!queue.try_pop().has_value(), "pop from empty returns nullopt");
    
    // Push single event
    bool pushed = queue.try_push(umi::midi::Message::note_on(0, 60, 100), 0);
    check(pushed, "push single event");
    check(queue.size_approx() == 1, "queue size is 1");
    check(!queue.empty_approx(), "queue not empty after push");
    
    // Peek without consuming
    auto peeked = queue.peek();
    check(peeked.has_value(), "peek returns value");
    check(peeked->msg.note() == 60, "peek correct note");
    check(queue.size_approx() == 1, "size unchanged after peek");
    
    // Pop single event
    auto popped = queue.try_pop();
    check(popped.has_value(), "pop returns value");
    check(popped->msg.is_note_on(), "popped event is note on");
    check(popped->msg.note() == 60, "popped event has correct note");
    check(popped->offset == 0, "popped event has correct offset");
    check(queue.empty_approx(), "queue empty after pop");
    
    // Fill the queue (capacity - 1 elements, ring buffer needs 1 empty slot)
    for (std::size_t i = 0; i < 15; ++i) {
        bool ok = queue.try_push(umi::midi::Message::note_on(0, static_cast<std::uint8_t>(i), 100), static_cast<std::uint32_t>(i));
        check(ok, "push to queue");
    }
    check(queue.size_approx() == 15, "queue has 15 elements");
    check(!queue.has_space(), "queue full");
    
    // Push to full queue should fail
    bool push_fail = queue.try_push(umi::midi::Message::note_on(0, 99, 99));
    check(!push_fail, "push to full queue fails");
    
    // Pop all elements
    for (std::size_t i = 0; i < 15; ++i) {
        auto event = queue.try_pop();
        check(event.has_value(), "pop from queue");
        check(event->msg.note() == i, "correct FIFO order");
        check(event->offset == i, "correct offset");
    }
    check(queue.empty_approx(), "queue empty after popping all");
}

void test_event_reader() {
    std::printf("Test: EventReader (Sample-Accurate Processing)...\n");
    
    umi::midi::EventQueue<16> queue;
    
    // Push events at different sample offsets (not in order to test sorting)
    queue.try_push(umi::midi::Message::note_on(0, 60, 100), 0);    // Sample 0
    queue.try_push(umi::midi::Message::control_change(0, 1, 64), 32);  // Sample 32
    queue.try_push(umi::midi::Message::note_on(0, 64, 100), 32);   // Sample 32 (same offset)
    queue.try_push(umi::midi::Message::note_off(0, 60), 64);       // Sample 64
    queue.try_push(umi::midi::Message::note_off(0, 64), 128);      // Sample 128
    
    // Read events from queue
    umi::midi::EventReader<16> reader;
    reader.read_from(queue);
    
    check(reader.size() == 5, "reader has 5 events");
    check(!reader.empty(), "reader not empty");
    check(queue.empty_approx(), "queue empty after read");
    
    // Get events at sample 0
    auto at_0 = reader.events_at(0);
    check(at_0.size() == 1, "1 event at sample 0");
    check(at_0[0].msg.is_note_on(), "event at 0 is note on");
    check(at_0[0].msg.note() == 60, "event at 0 is note 60");
    
    // Get events at sample 32 (multiple events)
    auto at_32 = reader.events_at(32);
    check(at_32.size() == 2, "2 events at sample 32");
    
    // Get events at sample 64
    auto at_64 = reader.events_at(64);
    check(at_64.size() == 1, "1 event at sample 64");
    check(at_64[0].msg.is_note_off(), "event at 64 is note off");
    
    // Get remaining events
    auto remaining = reader.remaining();
    check(remaining.size() == 1, "1 remaining event");
    check(remaining[0].offset == 128, "remaining event at sample 128");
    
    // Get all events
    auto all = reader.all();
    check(all.size() == 5, "all() returns 5 events");
}

void test_read_all_batch() {
    std::printf("Test: EventQueue read_all (batch read)...\n");
    
    umi::midi::EventQueue<16> queue;
    
    // Push 5 events
    for (std::size_t i = 0; i < 5; ++i) {
        queue.try_push(umi::midi::Message::note_on(0, static_cast<std::uint8_t>(60 + i), 100), static_cast<std::uint32_t>(i * 10));
    }
    
    // Batch read
    std::array<umi::midi::Event, 8> dest{};
    std::size_t read = queue.read_all(std::span(dest));
    
    check(read == 5, "read_all returned 5 events");
    check(queue.empty_approx(), "queue empty after read_all");
    
    for (std::size_t i = 0; i < 5; ++i) {
        check(dest[i].msg.note() == 60 + i, "batch read correct order");
        check(dest[i].offset == i * 10, "batch read correct offset");
    }
}

int main() {
    std::printf("=== UMI MIDI Tests ===\n\n");
    
    test_midi_message();
    test_midi_utilities();
    test_event_buffer();
    test_cross_platform_compatibility();
    test_system_messages();
    test_event_queue_spsc();
    test_event_reader();
    test_read_all_batch();
    
    std::printf("\n=== Results: %d/%d tests passed ===\n", pass_count, test_count);
    
    if (pass_count == test_count) {
        std::printf("All MIDI tests passed\n");
        return 0;
    } else {
        return 1;
    }
}

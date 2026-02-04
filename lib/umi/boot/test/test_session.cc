// SPDX-License-Identifier: MIT
// umi_boot Session Management Tests

#include <umiboot/session.hh>
#include <umitest.hh>

using namespace umiboot;
using namespace umitest;

// =============================================================================
// Session Timer Tests
// =============================================================================

bool test_timer_initial_state(TestContext& t) {
    SessionTimer timer;
    timer.init(DEFAULT_TIMEOUTS);

    t.assert_true(!timer.is_active());
    t.assert_eq(timer.check(0), TimeoutEvent::NONE);
    return true;
}

bool test_timer_start_session(TestContext& t) {
    SessionTimer timer;
    timer.init(DEFAULT_TIMEOUTS);
    timer.start_session(1000);

    t.assert_true(timer.is_active());
    t.assert_eq(timer.check(1000), TimeoutEvent::NONE);
    return true;
}

bool test_timer_session_timeout(TestContext& t) {
    TimeoutConfig config = {
        .session_timeout_ms = 1000, .chunk_timeout_ms = 0, .ack_timeout_ms = 0, .idle_timeout_ms = 0, .max_retries = 3};

    SessionTimer timer;
    timer.init(config);
    timer.start_session(0);

    t.assert_eq(timer.check(500), TimeoutEvent::NONE);
    t.assert_eq(timer.check(1000), TimeoutEvent::SESSION_EXPIRED);
    return true;
}

bool test_timer_chunk_timeout(TestContext& t) {
    TimeoutConfig config = {
        .session_timeout_ms = 0, .chunk_timeout_ms = 500, .ack_timeout_ms = 0, .idle_timeout_ms = 0, .max_retries = 3};

    SessionTimer timer;
    timer.init(config);
    timer.start_session(100);
    timer.record_chunk(100);

    t.assert_eq(timer.check(599), TimeoutEvent::NONE);
    t.assert_eq(timer.check(601), TimeoutEvent::CHUNK_TIMEOUT);
    return true;
}

bool test_timer_ack_timeout(TestContext& t) {
    TimeoutConfig config = {
        .session_timeout_ms = 0, .chunk_timeout_ms = 0, .ack_timeout_ms = 200, .idle_timeout_ms = 0, .max_retries = 3};

    SessionTimer timer;
    timer.init(config);
    timer.start_session(0);
    timer.record_ack_sent(100);

    t.assert_eq(timer.check(250), TimeoutEvent::NONE);
    t.assert_eq(timer.check(300), TimeoutEvent::ACK_TIMEOUT);
    return true;
}

bool test_timer_idle_timeout(TestContext& t) {
    TimeoutConfig config = {
        .session_timeout_ms = 0, .chunk_timeout_ms = 0, .ack_timeout_ms = 0, .idle_timeout_ms = 1000, .max_retries = 3};

    SessionTimer timer;
    timer.init(config);
    timer.start_session(0);

    t.assert_eq(timer.check(500), TimeoutEvent::NONE);
    t.assert_eq(timer.check(1000), TimeoutEvent::IDLE_TIMEOUT);
    return true;
}

bool test_timer_activity_resets_idle(TestContext& t) {
    TimeoutConfig config = {
        .session_timeout_ms = 0, .chunk_timeout_ms = 0, .ack_timeout_ms = 0, .idle_timeout_ms = 1000, .max_retries = 3};

    SessionTimer timer;
    timer.init(config);
    timer.start_session(0);

    timer.record_activity(500);
    t.assert_eq(timer.check(1400), TimeoutEvent::NONE);
    t.assert_eq(timer.check(1500), TimeoutEvent::IDLE_TIMEOUT);
    return true;
}

bool test_timer_retry_count(TestContext& t) {
    SessionTimer timer;
    timer.init(DEFAULT_TIMEOUTS);
    timer.start_session(0);

    t.assert_eq(timer.retry_count(), 0);
    t.assert_true(timer.increment_retry());  // 1
    t.assert_true(timer.increment_retry());  // 2
    t.assert_true(timer.increment_retry());  // 3
    t.assert_true(!timer.increment_retry()); // 4 > max_retries
    return true;
}

bool test_timer_ack_received_clears(TestContext& t) {
    TimeoutConfig config = {
        .session_timeout_ms = 0, .chunk_timeout_ms = 0, .ack_timeout_ms = 100, .idle_timeout_ms = 0, .max_retries = 3};

    SessionTimer timer;
    timer.init(config);
    timer.start_session(0);
    timer.record_ack_sent(0);

    timer.record_ack_received();

    t.assert_eq(timer.check(200), TimeoutEvent::NONE); // No timeout
    t.assert_eq(timer.retry_count(), 0);
    return true;
}

bool test_timer_remaining_time(TestContext& t) {
    TimeoutConfig config = {
        .session_timeout_ms = 1000, .chunk_timeout_ms = 0, .ack_timeout_ms = 0, .idle_timeout_ms = 0, .max_retries = 3};

    SessionTimer timer;
    timer.init(config);
    timer.start_session(0);

    t.assert_eq(timer.remaining_session_time(300), 700u);
    t.assert_eq(timer.remaining_session_time(1000), 0u);
    return true;
}

// =============================================================================
// Flow Control Sender Tests
// =============================================================================

bool test_flow_sender_initial_state(TestContext& t) {
    FlowControlSender<4> sender;
    sender.init(DEFAULT_FLOW);

    t.assert_true(sender.can_send());
    t.assert_eq(sender.pending_count(), 0);
    t.assert_true(sender.all_acked());
    return true;
}

bool test_flow_sender_enqueue(TestContext& t) {
    FlowControlSender<4> sender;
    sender.init(DEFAULT_FLOW);

    uint8_t data[] = {1, 2, 3};
    int seq = sender.enqueue(data, 3);

    t.assert_eq(seq, 0);
    t.assert_eq(sender.pending_count(), 1);
    t.assert_true(!sender.all_acked());
    return true;
}

bool test_flow_sender_window_full(TestContext& t) {
    FlowConfig config = {.window_size = 2, .max_payload_size = 48};
    FlowControlSender<4> sender;
    sender.init(config);

    uint8_t data[] = {1, 2, 3};
    sender.enqueue(data, 3); // seq 0
    sender.enqueue(data, 3); // seq 1

    t.assert_true(!sender.can_send());
    t.assert_eq(sender.enqueue(data, 3), -1); // Window full
    return true;
}

bool test_flow_sender_get_next_to_send(TestContext& t) {
    FlowControlSender<4> sender;
    sender.init(DEFAULT_FLOW);

    uint8_t data[] = {1, 2, 3};
    sender.enqueue(data, 3);

    uint8_t out[128];
    uint8_t seq;
    uint8_t len = sender.get_next_to_send(out, seq);

    t.assert_eq(len, 3);
    t.assert_eq(seq, 0);
    t.assert_eq(out[0], 1);
    t.assert_eq(out[1], 2);
    t.assert_eq(out[2], 3);

    // Second call returns nothing (already sent)
    len = sender.get_next_to_send(out, seq);
    t.assert_eq(len, 0);
    return true;
}

bool test_flow_sender_process_ack(TestContext& t) {
    FlowControlSender<4> sender;
    sender.init(DEFAULT_FLOW);

    uint8_t data[] = {1, 2, 3};
    sender.enqueue(data, 3); // seq 0
    sender.enqueue(data, 3); // seq 1

    sender.process_ack(0); // ACK seq 0
    t.assert_eq(sender.pending_count(), 1);

    sender.process_ack(1); // ACK seq 1
    t.assert_eq(sender.pending_count(), 0);
    t.assert_true(sender.all_acked());
    return true;
}

bool test_flow_sender_retransmit(TestContext& t) {
    FlowControlSender<4> sender;
    sender.init(DEFAULT_FLOW);

    uint8_t data[] = {1, 2, 3};
    sender.enqueue(data, 3);

    uint8_t out[128];
    uint8_t seq;
    sender.get_next_to_send(out, seq); // Mark as sent

    // Retransmit
    sender.mark_for_retransmit();

    // Should be able to get it again
    uint8_t len = sender.get_next_to_send(out, seq);
    t.assert_eq(len, 3);
    return true;
}

// =============================================================================
// Flow Control Receiver Tests
// =============================================================================

bool test_flow_receiver_initial_state(TestContext& t) {
    FlowControlReceiver receiver;
    receiver.reset();

    t.assert_eq(receiver.expected_sequence(), 0);
    return true;
}

bool test_flow_receiver_in_order(TestContext& t) {
    FlowControlReceiver receiver;
    receiver.reset();

    t.assert_true(receiver.process_packet(0));
    t.assert_eq(receiver.expected_sequence(), 1);
    t.assert_eq(receiver.ack_sequence(), 0);

    t.assert_true(receiver.process_packet(1));
    t.assert_eq(receiver.expected_sequence(), 2);
    t.assert_eq(receiver.ack_sequence(), 1);
    return true;
}

bool test_flow_receiver_out_of_order(TestContext& t) {
    FlowControlReceiver receiver;
    receiver.reset();

    t.assert_true(receiver.process_packet(0));    // OK
    t.assert_true(receiver.process_packet(2));    // Out of order, buffered
    t.assert_eq(receiver.expected_sequence(), 1); // Still expecting 1

    t.assert_true(receiver.process_packet(1));    // Fill the gap
    t.assert_eq(receiver.expected_sequence(), 3); // Advanced past buffered
    return true;
}

bool test_flow_receiver_duplicate(TestContext& t) {
    FlowControlReceiver receiver;
    receiver.reset();

    t.assert_true(receiver.process_packet(0));
    t.assert_true(!receiver.process_packet(0)); // Duplicate
    return true;
}

// =============================================================================
// Firmware Update Session Tests
// =============================================================================

bool test_session_initial_state(TestContext& t) {
    FirmwareUpdateSession<4> session;
    session.init(DEFAULT_TIMEOUTS, DEFAULT_FLOW);

    t.assert_eq(session.state(), SessionState::IDLE);
    t.assert_near(session.progress(), 0.0f, 0.001f);
    return true;
}

bool test_session_start(TestContext& t) {
    FirmwareUpdateSession<4> session;
    session.init(DEFAULT_TIMEOUTS, DEFAULT_FLOW);
    session.start(0, 1000);

    t.assert_eq(session.state(), SessionState::UPDATING);
    t.assert_eq(session.total_bytes(), 1000u);
    t.assert_eq(session.received_bytes(), 0u);
    return true;
}

bool test_session_receive_data(TestContext& t) {
    FirmwareUpdateSession<4> session;
    session.init(DEFAULT_TIMEOUTS, DEFAULT_FLOW);
    session.start(0, 100);

    t.assert_true(session.receive_data(0, 25, 10));
    t.assert_eq(session.received_bytes(), 25u);
    t.assert_near(session.progress(), 0.25f, 0.001f);
    return true;
}

bool test_session_complete(TestContext& t) {
    FirmwareUpdateSession<4> session;
    session.init(DEFAULT_TIMEOUTS, DEFAULT_FLOW);
    session.start(0, 100);

    session.receive_data(0, 50, 10);
    session.receive_data(1, 50, 20);

    t.assert_true(session.is_complete());
    t.assert_near(session.progress(), 1.0f, 0.001f);
    return true;
}

bool test_session_timeout_handling(TestContext& t) {
    TimeoutConfig config = {
        .session_timeout_ms = 1000, .chunk_timeout_ms = 0, .ack_timeout_ms = 0, .idle_timeout_ms = 0, .max_retries = 3};

    FirmwareUpdateSession<4> session;
    session.init(config, DEFAULT_FLOW);
    session.start(0, 100);

    auto event = session.check_timeout(1000);
    t.assert_eq(event, TimeoutEvent::SESSION_EXPIRED);

    bool should_continue = session.handle_timeout(event);
    t.assert_true(!should_continue);
    t.assert_eq(session.state(), SessionState::ERROR);
    return true;
}

bool test_session_end(TestContext& t) {
    FirmwareUpdateSession<4> session;
    session.init(DEFAULT_TIMEOUTS, DEFAULT_FLOW);
    session.start(0, 100);

    session.end();

    t.assert_eq(session.state(), SessionState::IDLE);
    return true;
}

// =============================================================================
// Configuration Constants Tests
// =============================================================================

bool test_default_timeouts(TestContext& t) {
    t.assert_eq(DEFAULT_TIMEOUTS.session_timeout_ms, 300000u);
    t.assert_eq(DEFAULT_TIMEOUTS.chunk_timeout_ms, 5000u);
    t.assert_eq(DEFAULT_TIMEOUTS.ack_timeout_ms, 1000u);
    t.assert_eq(DEFAULT_TIMEOUTS.idle_timeout_ms, 60000u);
    t.assert_eq(DEFAULT_TIMEOUTS.max_retries, 3);
    return true;
}

bool test_default_flow(TestContext& t) {
    t.assert_eq(DEFAULT_FLOW.window_size, 4);
    t.assert_eq(DEFAULT_FLOW.max_payload_size, 48);
    return true;
}

// =============================================================================
// Main
// =============================================================================

int main() {
    Suite s("umiboot_session");

    s.section("Session Timer");
    s.run("timer_initial_state", test_timer_initial_state);
    s.run("timer_start_session", test_timer_start_session);
    s.run("timer_session_timeout", test_timer_session_timeout);
    s.run("timer_chunk_timeout", test_timer_chunk_timeout);
    s.run("timer_ack_timeout", test_timer_ack_timeout);
    s.run("timer_idle_timeout", test_timer_idle_timeout);
    s.run("timer_activity_resets_idle", test_timer_activity_resets_idle);
    s.run("timer_retry_count", test_timer_retry_count);
    s.run("timer_ack_received_clears", test_timer_ack_received_clears);
    s.run("timer_remaining_time", test_timer_remaining_time);

    s.section("Flow Control Sender");
    s.run("flow_sender_initial_state", test_flow_sender_initial_state);
    s.run("flow_sender_enqueue", test_flow_sender_enqueue);
    s.run("flow_sender_window_full", test_flow_sender_window_full);
    s.run("flow_sender_get_next_to_send", test_flow_sender_get_next_to_send);
    s.run("flow_sender_process_ack", test_flow_sender_process_ack);
    s.run("flow_sender_retransmit", test_flow_sender_retransmit);

    s.section("Flow Control Receiver");
    s.run("flow_receiver_initial_state", test_flow_receiver_initial_state);
    s.run("flow_receiver_in_order", test_flow_receiver_in_order);
    s.run("flow_receiver_out_of_order", test_flow_receiver_out_of_order);
    s.run("flow_receiver_duplicate", test_flow_receiver_duplicate);

    s.section("Firmware Update Session");
    s.run("session_initial_state", test_session_initial_state);
    s.run("session_start", test_session_start);
    s.run("session_receive_data", test_session_receive_data);
    s.run("session_complete", test_session_complete);
    s.run("session_timeout_handling", test_session_timeout_handling);
    s.run("session_end", test_session_end);

    s.section("Configuration Constants");
    s.run("default_timeouts", test_default_timeouts);
    s.run("default_flow", test_default_flow);

    return s.summary();
}

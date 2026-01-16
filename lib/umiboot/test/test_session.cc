// SPDX-License-Identifier: MIT
// umi_boot Session Management Tests

#include "test_framework.hh"
#include <umiboot/session.hh>

using namespace umiboot;
using namespace umiboot::test;

// =============================================================================
// Session Timer Tests
// =============================================================================

TEST(timer_initial_state) {
    SessionTimer timer;
    timer.init(DEFAULT_TIMEOUTS);

    ASSERT(!timer.is_active());
    ASSERT_EQ(timer.check(0), TimeoutEvent::NONE);
    TEST_PASS();
}

TEST(timer_start_session) {
    SessionTimer timer;
    timer.init(DEFAULT_TIMEOUTS);
    timer.start_session(1000);

    ASSERT(timer.is_active());
    ASSERT_EQ(timer.check(1000), TimeoutEvent::NONE);
    TEST_PASS();
}

TEST(timer_session_timeout) {
    TimeoutConfig config = {
        .session_timeout_ms = 1000,
        .chunk_timeout_ms = 0,
        .ack_timeout_ms = 0,
        .idle_timeout_ms = 0,
        .max_retries = 3
    };

    SessionTimer timer;
    timer.init(config);
    timer.start_session(0);

    ASSERT_EQ(timer.check(500), TimeoutEvent::NONE);
    ASSERT_EQ(timer.check(1000), TimeoutEvent::SESSION_EXPIRED);
    TEST_PASS();
}

TEST(timer_chunk_timeout) {
    TimeoutConfig config = {
        .session_timeout_ms = 0,
        .chunk_timeout_ms = 500,
        .ack_timeout_ms = 0,
        .idle_timeout_ms = 0,
        .max_retries = 3
    };

    SessionTimer timer;
    timer.init(config);
    timer.start_session(100);
    timer.record_chunk(100);

    ASSERT_EQ(timer.check(599), TimeoutEvent::NONE);
    ASSERT_EQ(timer.check(601), TimeoutEvent::CHUNK_TIMEOUT);
    TEST_PASS();
}

TEST(timer_ack_timeout) {
    TimeoutConfig config = {
        .session_timeout_ms = 0,
        .chunk_timeout_ms = 0,
        .ack_timeout_ms = 200,
        .idle_timeout_ms = 0,
        .max_retries = 3
    };

    SessionTimer timer;
    timer.init(config);
    timer.start_session(0);
    timer.record_ack_sent(100);

    ASSERT_EQ(timer.check(250), TimeoutEvent::NONE);
    ASSERT_EQ(timer.check(300), TimeoutEvent::ACK_TIMEOUT);
    TEST_PASS();
}

TEST(timer_idle_timeout) {
    TimeoutConfig config = {
        .session_timeout_ms = 0,
        .chunk_timeout_ms = 0,
        .ack_timeout_ms = 0,
        .idle_timeout_ms = 1000,
        .max_retries = 3
    };

    SessionTimer timer;
    timer.init(config);
    timer.start_session(0);

    ASSERT_EQ(timer.check(500), TimeoutEvent::NONE);
    ASSERT_EQ(timer.check(1000), TimeoutEvent::IDLE_TIMEOUT);
    TEST_PASS();
}

TEST(timer_activity_resets_idle) {
    TimeoutConfig config = {
        .session_timeout_ms = 0,
        .chunk_timeout_ms = 0,
        .ack_timeout_ms = 0,
        .idle_timeout_ms = 1000,
        .max_retries = 3
    };

    SessionTimer timer;
    timer.init(config);
    timer.start_session(0);

    timer.record_activity(500);
    ASSERT_EQ(timer.check(1400), TimeoutEvent::NONE);
    ASSERT_EQ(timer.check(1500), TimeoutEvent::IDLE_TIMEOUT);
    TEST_PASS();
}

TEST(timer_retry_count) {
    SessionTimer timer;
    timer.init(DEFAULT_TIMEOUTS);
    timer.start_session(0);

    ASSERT_EQ(timer.retry_count(), 0);
    ASSERT(timer.increment_retry());  // 1
    ASSERT(timer.increment_retry());  // 2
    ASSERT(timer.increment_retry());  // 3
    ASSERT(!timer.increment_retry()); // 4 > max_retries
    TEST_PASS();
}

TEST(timer_ack_received_clears) {
    TimeoutConfig config = {
        .session_timeout_ms = 0,
        .chunk_timeout_ms = 0,
        .ack_timeout_ms = 100,
        .idle_timeout_ms = 0,
        .max_retries = 3
    };

    SessionTimer timer;
    timer.init(config);
    timer.start_session(0);
    timer.record_ack_sent(0);

    timer.record_ack_received();

    ASSERT_EQ(timer.check(200), TimeoutEvent::NONE);  // No timeout
    ASSERT_EQ(timer.retry_count(), 0);
    TEST_PASS();
}

TEST(timer_remaining_time) {
    TimeoutConfig config = {
        .session_timeout_ms = 1000,
        .chunk_timeout_ms = 0,
        .ack_timeout_ms = 0,
        .idle_timeout_ms = 0,
        .max_retries = 3
    };

    SessionTimer timer;
    timer.init(config);
    timer.start_session(0);

    ASSERT_EQ(timer.remaining_session_time(300), 700u);
    ASSERT_EQ(timer.remaining_session_time(1000), 0u);
    TEST_PASS();
}

// =============================================================================
// Flow Control Sender Tests
// =============================================================================

TEST(flow_sender_initial_state) {
    FlowControlSender<4> sender;
    sender.init(DEFAULT_FLOW);

    ASSERT(sender.can_send());
    ASSERT_EQ(sender.pending_count(), 0);
    ASSERT(sender.all_acked());
    TEST_PASS();
}

TEST(flow_sender_enqueue) {
    FlowControlSender<4> sender;
    sender.init(DEFAULT_FLOW);

    uint8_t data[] = {1, 2, 3};
    int seq = sender.enqueue(data, 3);

    ASSERT_EQ(seq, 0);
    ASSERT_EQ(sender.pending_count(), 1);
    ASSERT(!sender.all_acked());
    TEST_PASS();
}

TEST(flow_sender_window_full) {
    FlowConfig config = {.window_size = 2, .max_payload_size = 48};
    FlowControlSender<4> sender;
    sender.init(config);

    uint8_t data[] = {1, 2, 3};
    sender.enqueue(data, 3);  // seq 0
    sender.enqueue(data, 3);  // seq 1

    ASSERT(!sender.can_send());
    ASSERT_EQ(sender.enqueue(data, 3), -1);  // Window full
    TEST_PASS();
}

TEST(flow_sender_get_next_to_send) {
    FlowControlSender<4> sender;
    sender.init(DEFAULT_FLOW);

    uint8_t data[] = {1, 2, 3};
    sender.enqueue(data, 3);

    uint8_t out[128];
    uint8_t seq;
    uint8_t len = sender.get_next_to_send(out, seq);

    ASSERT_EQ(len, 3);
    ASSERT_EQ(seq, 0);
    ASSERT_EQ(out[0], 1);
    ASSERT_EQ(out[1], 2);
    ASSERT_EQ(out[2], 3);

    // Second call returns nothing (already sent)
    len = sender.get_next_to_send(out, seq);
    ASSERT_EQ(len, 0);
    TEST_PASS();
}

TEST(flow_sender_process_ack) {
    FlowControlSender<4> sender;
    sender.init(DEFAULT_FLOW);

    uint8_t data[] = {1, 2, 3};
    sender.enqueue(data, 3);  // seq 0
    sender.enqueue(data, 3);  // seq 1

    sender.process_ack(0);  // ACK seq 0
    ASSERT_EQ(sender.pending_count(), 1);

    sender.process_ack(1);  // ACK seq 1
    ASSERT_EQ(sender.pending_count(), 0);
    ASSERT(sender.all_acked());
    TEST_PASS();
}

TEST(flow_sender_retransmit) {
    FlowControlSender<4> sender;
    sender.init(DEFAULT_FLOW);

    uint8_t data[] = {1, 2, 3};
    sender.enqueue(data, 3);

    uint8_t out[128];
    uint8_t seq;
    sender.get_next_to_send(out, seq);  // Mark as sent

    // Retransmit
    sender.mark_for_retransmit();

    // Should be able to get it again
    uint8_t len = sender.get_next_to_send(out, seq);
    ASSERT_EQ(len, 3);
    TEST_PASS();
}

// =============================================================================
// Flow Control Receiver Tests
// =============================================================================

TEST(flow_receiver_initial_state) {
    FlowControlReceiver receiver;
    receiver.reset();

    ASSERT_EQ(receiver.expected_sequence(), 0);
    TEST_PASS();
}

TEST(flow_receiver_in_order) {
    FlowControlReceiver receiver;
    receiver.reset();

    ASSERT(receiver.process_packet(0));
    ASSERT_EQ(receiver.expected_sequence(), 1);
    ASSERT_EQ(receiver.ack_sequence(), 0);

    ASSERT(receiver.process_packet(1));
    ASSERT_EQ(receiver.expected_sequence(), 2);
    ASSERT_EQ(receiver.ack_sequence(), 1);
    TEST_PASS();
}

TEST(flow_receiver_out_of_order) {
    FlowControlReceiver receiver;
    receiver.reset();

    ASSERT(receiver.process_packet(0));  // OK
    ASSERT(receiver.process_packet(2));  // Out of order, buffered
    ASSERT_EQ(receiver.expected_sequence(), 1);  // Still expecting 1

    ASSERT(receiver.process_packet(1));  // Fill the gap
    ASSERT_EQ(receiver.expected_sequence(), 3);  // Advanced past buffered
    TEST_PASS();
}

TEST(flow_receiver_duplicate) {
    FlowControlReceiver receiver;
    receiver.reset();

    ASSERT(receiver.process_packet(0));
    ASSERT(!receiver.process_packet(0));  // Duplicate
    TEST_PASS();
}

// =============================================================================
// Firmware Update Session Tests
// =============================================================================

TEST(session_initial_state) {
    FirmwareUpdateSession<4> session;
    session.init(DEFAULT_TIMEOUTS, DEFAULT_FLOW);

    ASSERT_EQ(session.state(), SessionState::IDLE);
    ASSERT_NEAR(session.progress(), 0.0f, 0.001f);
    TEST_PASS();
}

TEST(session_start) {
    FirmwareUpdateSession<4> session;
    session.init(DEFAULT_TIMEOUTS, DEFAULT_FLOW);
    session.start(0, 1000);

    ASSERT_EQ(session.state(), SessionState::UPDATING);
    ASSERT_EQ(session.total_bytes(), 1000u);
    ASSERT_EQ(session.received_bytes(), 0u);
    TEST_PASS();
}

TEST(session_receive_data) {
    FirmwareUpdateSession<4> session;
    session.init(DEFAULT_TIMEOUTS, DEFAULT_FLOW);
    session.start(0, 100);

    ASSERT(session.receive_data(0, 25, 10));
    ASSERT_EQ(session.received_bytes(), 25u);
    ASSERT_NEAR(session.progress(), 0.25f, 0.001f);
    TEST_PASS();
}

TEST(session_complete) {
    FirmwareUpdateSession<4> session;
    session.init(DEFAULT_TIMEOUTS, DEFAULT_FLOW);
    session.start(0, 100);

    session.receive_data(0, 50, 10);
    session.receive_data(1, 50, 20);

    ASSERT(session.is_complete());
    ASSERT_NEAR(session.progress(), 1.0f, 0.001f);
    TEST_PASS();
}

TEST(session_timeout_handling) {
    TimeoutConfig config = {
        .session_timeout_ms = 1000,
        .chunk_timeout_ms = 0,
        .ack_timeout_ms = 0,
        .idle_timeout_ms = 0,
        .max_retries = 3
    };

    FirmwareUpdateSession<4> session;
    session.init(config, DEFAULT_FLOW);
    session.start(0, 100);

    auto event = session.check_timeout(1000);
    ASSERT_EQ(event, TimeoutEvent::SESSION_EXPIRED);

    bool should_continue = session.handle_timeout(event);
    ASSERT(!should_continue);
    ASSERT_EQ(session.state(), SessionState::ERROR);
    TEST_PASS();
}

TEST(session_end) {
    FirmwareUpdateSession<4> session;
    session.init(DEFAULT_TIMEOUTS, DEFAULT_FLOW);
    session.start(0, 100);

    session.end();

    ASSERT_EQ(session.state(), SessionState::IDLE);
    TEST_PASS();
}

// =============================================================================
// Configuration Constants Tests
// =============================================================================

TEST(default_timeouts) {
    ASSERT_EQ(DEFAULT_TIMEOUTS.session_timeout_ms, 300000u);
    ASSERT_EQ(DEFAULT_TIMEOUTS.chunk_timeout_ms, 5000u);
    ASSERT_EQ(DEFAULT_TIMEOUTS.ack_timeout_ms, 1000u);
    ASSERT_EQ(DEFAULT_TIMEOUTS.idle_timeout_ms, 60000u);
    ASSERT_EQ(DEFAULT_TIMEOUTS.max_retries, 3);
    TEST_PASS();
}

TEST(default_flow) {
    ASSERT_EQ(DEFAULT_FLOW.window_size, 4);
    ASSERT_EQ(DEFAULT_FLOW.max_payload_size, 48);
    TEST_PASS();
}

// =============================================================================
// Main
// =============================================================================

int main() {
    printf("umi_boot Session Management Tests\n");
    printf("==================================\n");

    SECTION("Session Timer");
    RUN_TEST(timer_initial_state);
    RUN_TEST(timer_start_session);
    RUN_TEST(timer_session_timeout);
    RUN_TEST(timer_chunk_timeout);
    RUN_TEST(timer_ack_timeout);
    RUN_TEST(timer_idle_timeout);
    RUN_TEST(timer_activity_resets_idle);
    RUN_TEST(timer_retry_count);
    RUN_TEST(timer_ack_received_clears);
    RUN_TEST(timer_remaining_time);

    SECTION("Flow Control Sender");
    RUN_TEST(flow_sender_initial_state);
    RUN_TEST(flow_sender_enqueue);
    RUN_TEST(flow_sender_window_full);
    RUN_TEST(flow_sender_get_next_to_send);
    RUN_TEST(flow_sender_process_ack);
    RUN_TEST(flow_sender_retransmit);

    SECTION("Flow Control Receiver");
    RUN_TEST(flow_receiver_initial_state);
    RUN_TEST(flow_receiver_in_order);
    RUN_TEST(flow_receiver_out_of_order);
    RUN_TEST(flow_receiver_duplicate);

    SECTION("Firmware Update Session");
    RUN_TEST(session_initial_state);
    RUN_TEST(session_start);
    RUN_TEST(session_receive_data);
    RUN_TEST(session_complete);
    RUN_TEST(session_timeout_handling);
    RUN_TEST(session_end);

    SECTION("Configuration Constants");
    RUN_TEST(default_timeouts);
    RUN_TEST(default_flow);

    return summary();
}

// SPDX-License-Identifier: MIT
// UMI-Boot Session Management
// Timeout handling and sliding window flow control
#pragma once

#include <cstdint>
#include <cstring>

namespace umiboot {

// =============================================================================
// Session Timeout Management
// =============================================================================
//
// Timeout types:
// - Session timeout: Overall session duration limit
// - Chunk timeout: Time between receiving data chunks
// - ACK timeout: Time waiting for acknowledgment
// - Idle timeout: Time with no activity
//
// =============================================================================

/// Timeout configuration
struct TimeoutConfig {
    uint32_t session_timeout_ms;    // Overall session timeout (0 = no limit)
    uint32_t chunk_timeout_ms;      // Timeout between chunks (default 5000ms)
    uint32_t ack_timeout_ms;        // Timeout waiting for ACK (default 1000ms)
    uint32_t idle_timeout_ms;       // Idle timeout (default 60000ms)
    uint8_t  max_retries;           // Max retries before failure (default 3)
};

/// Default timeout configuration
inline constexpr TimeoutConfig DEFAULT_TIMEOUTS = {
    .session_timeout_ms = 300000,   // 5 minutes
    .chunk_timeout_ms = 5000,       // 5 seconds
    .ack_timeout_ms = 1000,         // 1 second
    .idle_timeout_ms = 60000,       // 1 minute
    .max_retries = 3,
};

/// Timeout configuration for slow connections (MIDI over USB)
inline constexpr TimeoutConfig SLOW_CONNECTION_TIMEOUTS = {
    .session_timeout_ms = 600000,   // 10 minutes
    .chunk_timeout_ms = 10000,      // 10 seconds
    .ack_timeout_ms = 3000,         // 3 seconds
    .idle_timeout_ms = 120000,      // 2 minutes
    .max_retries = 5,
};

/// Timeout event type
enum class TimeoutEvent : uint8_t {
    NONE,
    SESSION_EXPIRED,
    CHUNK_TIMEOUT,
    ACK_TIMEOUT,
    IDLE_TIMEOUT,
};

/// Session timer
/// Tracks multiple timeout conditions
class SessionTimer {
public:
    /// Initialize with timeout configuration
    void init(const TimeoutConfig& config) noexcept {
        config_ = config;
        reset();
    }

    /// Reset all timers (start new session)
    void reset() noexcept {
        session_start_ = 0;
        last_activity_ = 0;
        last_chunk_ = 0;
        ack_sent_ = 0;
        retry_count_ = 0;
        session_active_ = false;
    }

    /// Start session
    void start_session(uint32_t current_time_ms) noexcept {
        session_start_ = current_time_ms;
        last_activity_ = current_time_ms;
        last_chunk_ = current_time_ms;
        ack_sent_ = 0;
        retry_count_ = 0;
        session_active_ = true;
    }

    /// End session
    void end_session() noexcept {
        session_active_ = false;
    }

    /// Record activity (any message received/sent)
    void record_activity(uint32_t current_time_ms) noexcept {
        last_activity_ = current_time_ms;
    }

    /// Record chunk received
    void record_chunk(uint32_t current_time_ms) noexcept {
        last_chunk_ = current_time_ms;
        last_activity_ = current_time_ms;
    }

    /// Record ACK sent (start waiting for response)
    void record_ack_sent(uint32_t current_time_ms) noexcept {
        ack_sent_ = current_time_ms;
    }

    /// Record ACK received (clear ACK timer)
    void record_ack_received() noexcept {
        ack_sent_ = 0;
        retry_count_ = 0;
    }

    /// Increment retry count
    /// @return true if retries remaining
    bool increment_retry() noexcept {
        retry_count_++;
        return retry_count_ <= config_.max_retries;
    }

    /// Check for timeout conditions
    /// @param current_time_ms Current timestamp
    /// @return Timeout event if any occurred
    [[nodiscard]] TimeoutEvent check(uint32_t current_time_ms) const noexcept {
        if (!session_active_) return TimeoutEvent::NONE;

        // Session timeout
        if (config_.session_timeout_ms > 0 &&
            (current_time_ms - session_start_) >= config_.session_timeout_ms) {
            return TimeoutEvent::SESSION_EXPIRED;
        }

        // Chunk timeout (only during active transfer)
        if (config_.chunk_timeout_ms > 0 && last_chunk_ > 0 &&
            (current_time_ms - last_chunk_) >= config_.chunk_timeout_ms) {
            return TimeoutEvent::CHUNK_TIMEOUT;
        }

        // ACK timeout
        if (config_.ack_timeout_ms > 0 && ack_sent_ > 0 &&
            (current_time_ms - ack_sent_) >= config_.ack_timeout_ms) {
            return TimeoutEvent::ACK_TIMEOUT;
        }

        // Idle timeout
        if (config_.idle_timeout_ms > 0 &&
            (current_time_ms - last_activity_) >= config_.idle_timeout_ms) {
            return TimeoutEvent::IDLE_TIMEOUT;
        }

        return TimeoutEvent::NONE;
    }

    /// Check if session is active
    [[nodiscard]] bool is_active() const noexcept { return session_active_; }

    /// Get remaining session time (ms)
    [[nodiscard]] uint32_t remaining_session_time(uint32_t current_time_ms) const noexcept {
        if (!session_active_ || config_.session_timeout_ms == 0) {
            return UINT32_MAX;
        }
        uint32_t elapsed = current_time_ms - session_start_;
        return (elapsed < config_.session_timeout_ms)
            ? (config_.session_timeout_ms - elapsed)
            : 0;
    }

    /// Get retry count
    [[nodiscard]] uint8_t retry_count() const noexcept { return retry_count_; }

private:
    TimeoutConfig config_{};
    uint32_t session_start_ = 0;
    uint32_t last_activity_ = 0;
    uint32_t last_chunk_ = 0;
    uint32_t ack_sent_ = 0;
    uint8_t retry_count_ = 0;
    bool session_active_ = false;
};

// =============================================================================
// Sliding Window Flow Control
// =============================================================================
//
// Window-based flow control for reliable data transfer over unreliable links.
// Sender maintains a window of unacknowledged packets.
// Receiver sends cumulative ACKs.
//
// Window protocol:
// - Sender sends packets with sequence numbers (0-127, wraps)
// - Receiver ACKs with highest consecutive sequence received
// - Sender can have up to window_size unacknowledged packets
// - On ACK timeout, sender retransmits from first unacked packet
//
// =============================================================================

/// Flow control configuration
struct FlowConfig {
    uint8_t window_size;            // Max unacknowledged packets (default 4)
    uint8_t max_payload_size;       // Max payload per packet (default 48)
};

/// Default flow control configuration (conservative, works on slow links)
inline constexpr FlowConfig DEFAULT_FLOW = {
    .window_size = 4,
    .max_payload_size = 48,
};

/// High-throughput flow control (for fast USB MIDI)
inline constexpr FlowConfig HIGH_THROUGHPUT_FLOW = {
    .window_size = 8,
    .max_payload_size = 128,
};

/// Packet state for sender window
struct PacketState {
    uint8_t sequence;
    uint8_t data[128];
    uint8_t length;
    bool sent;
    bool acked;
};

/// Sender-side flow controller
/// @tparam WindowSize Maximum window size
template <uint8_t WindowSize = 8>
class FlowControlSender {
public:
    /// Initialize with configuration
    void init(const FlowConfig& config) noexcept {
        config_ = config;
        reset();
    }

    /// Reset state
    void reset() noexcept {
        next_seq_ = 0;
        base_seq_ = 0;
        window_count_ = 0;
        for (auto& p : window_) {
            p.sent = false;
            p.acked = false;
            p.length = 0;
        }
    }

    /// Check if window has space for another packet
    [[nodiscard]] bool can_send() const noexcept {
        return window_count_ < config_.window_size;
    }

    /// Queue packet for sending
    /// @param data Packet data
    /// @param len Data length
    /// @return Sequence number, or -1 if window full
    int enqueue(const uint8_t* data, uint8_t len) noexcept {
        if (!can_send() || len > sizeof(PacketState::data)) {
            return -1;
        }

        uint8_t idx = next_seq_ % WindowSize;
        window_[idx].sequence = next_seq_;
        std::memcpy(window_[idx].data, data, len);
        window_[idx].length = len;
        window_[idx].sent = false;
        window_[idx].acked = false;

        uint8_t seq = next_seq_;
        next_seq_ = (next_seq_ + 1) & 0x7F;
        window_count_++;

        return seq;
    }

    /// Get next packet to send
    /// @param out_data Output data buffer
    /// @param out_seq Output sequence number
    /// @return Data length, or 0 if nothing to send
    uint8_t get_next_to_send(uint8_t* out_data, uint8_t& out_seq) noexcept {
        // Find first unsent packet in window
        for (uint8_t i = 0; i < window_count_; ++i) {
            uint8_t idx = (base_seq_ + i) % WindowSize;
            if (!window_[idx].sent && !window_[idx].acked) {
                std::memcpy(out_data, window_[idx].data, window_[idx].length);
                out_seq = window_[idx].sequence;
                window_[idx].sent = true;
                return window_[idx].length;
            }
        }
        return 0;
    }

    /// Process ACK (cumulative acknowledgment)
    /// @param ack_seq Highest sequence number received by receiver
    void process_ack(uint8_t ack_seq) noexcept {
        // Acknowledge all packets up to ack_seq
        while (window_count_ > 0) {
            uint8_t idx = base_seq_ % WindowSize;

            // Check if this packet is acknowledged
            // Handle sequence wrap-around
            int8_t diff = static_cast<int8_t>(
                (ack_seq - window_[idx].sequence) & 0x7F
            );
            if (diff < 0 || diff > 64) {
                break;  // ack_seq is behind this packet
            }

            // Packet is acknowledged
            window_[idx].acked = true;
            base_seq_ = (base_seq_ + 1) & 0x7F;
            window_count_--;
        }
    }

    /// Mark all sent packets as unsent (for retransmission)
    void mark_for_retransmit() noexcept {
        for (uint8_t i = 0; i < window_count_; ++i) {
            uint8_t idx = (base_seq_ + i) % WindowSize;
            window_[idx].sent = false;
        }
    }

    /// Get number of unacknowledged packets
    [[nodiscard]] uint8_t pending_count() const noexcept { return window_count_; }

    /// Check if all packets acknowledged
    [[nodiscard]] bool all_acked() const noexcept { return window_count_ == 0; }

private:
    FlowConfig config_{};
    PacketState window_[WindowSize]{};
    uint8_t next_seq_ = 0;
    uint8_t base_seq_ = 0;
    uint8_t window_count_ = 0;
};

/// Receiver-side flow controller
class FlowControlReceiver {
public:
    /// Reset state
    void reset() noexcept {
        expected_seq_ = 0;
        received_mask_ = 0;
    }

    /// Process received packet
    /// @param seq Sequence number
    /// @return true if packet is new and in order
    bool process_packet(uint8_t seq) noexcept {
        // Check if this is the expected sequence
        if (seq == expected_seq_) {
            expected_seq_ = (expected_seq_ + 1) & 0x7F;

            // Check if we can advance further (out-of-order packets buffered)
            while (received_mask_ & (1u << (expected_seq_ & 31))) {
                received_mask_ &= ~(1u << (expected_seq_ & 31));
                expected_seq_ = (expected_seq_ + 1) & 0x7F;
            }
            return true;
        }

        // Check if packet is within acceptable window
        int8_t diff = static_cast<int8_t>((seq - expected_seq_) & 0x7F);
        if (diff > 0 && diff <= 32) {
            // Future packet - buffer it
            received_mask_ |= (1u << (seq & 31));
            return true;  // Accept but don't advance
        }

        // Duplicate or too old
        return false;
    }

    /// Get ACK sequence (highest consecutive received)
    [[nodiscard]] uint8_t ack_sequence() const noexcept {
        // ACK the last consecutive packet
        return (expected_seq_ - 1) & 0x7F;
    }

    /// Get expected sequence
    [[nodiscard]] uint8_t expected_sequence() const noexcept {
        return expected_seq_;
    }

private:
    uint8_t expected_seq_ = 0;
    uint32_t received_mask_ = 0;  // Bitmap for out-of-order packets
};

// =============================================================================
// Complete Session Controller
// =============================================================================

/// Combined session state
enum class SessionState : uint8_t {
    IDLE,
    AUTHENTICATING,     // Waiting for authentication
    ACTIVE,             // Normal operation
    UPDATING,           // Firmware update in progress
    ERROR,
};

/// Session events (for state machine)
enum class SessionEvent : uint8_t {
    NONE,
    START_REQUEST,
    AUTH_SUCCESS,
    AUTH_FAILURE,
    UPDATE_START,
    UPDATE_COMPLETE,
    TIMEOUT,
    ERROR,
    RESET,
};

/// Complete firmware update session controller
/// Combines timeout management and flow control
template <uint8_t WindowSize = 4>
class FirmwareUpdateSession {
public:
    /// Initialize with configurations
    void init(const TimeoutConfig& timeout_config,
              const FlowConfig& flow_config) noexcept {
        timer_.init(timeout_config);
        sender_.init(flow_config);
        receiver_.reset();
        state_ = SessionState::IDLE;
        total_bytes_ = 0;
        received_bytes_ = 0;
    }

    /// Start new session
    /// @param current_time Current timestamp (ms)
    /// @param total_size Expected total firmware size
    void start(uint32_t current_time, uint32_t total_size) noexcept {
        timer_.start_session(current_time);
        sender_.reset();
        receiver_.reset();
        state_ = SessionState::UPDATING;
        total_bytes_ = total_size;
        received_bytes_ = 0;
    }

    /// End session (success or failure)
    void end() noexcept {
        timer_.end_session();
        state_ = SessionState::IDLE;
    }

    /// Process incoming data packet
    /// @param seq Sequence number
    /// @param data_len Data length
    /// @param current_time Current timestamp
    /// @return true if packet accepted
    bool receive_data(uint8_t seq, size_t data_len, uint32_t current_time) noexcept {
        if (state_ != SessionState::UPDATING) return false;

        timer_.record_chunk(current_time);

        if (receiver_.process_packet(seq)) {
            received_bytes_ += data_len;
            return true;
        }
        return false;
    }

    /// Get ACK to send
    [[nodiscard]] uint8_t get_ack() const noexcept {
        return receiver_.ack_sequence();
    }

    /// Record that we sent an ACK
    void record_ack_sent(uint32_t current_time) noexcept {
        timer_.record_ack_sent(current_time);
    }

    /// Record that we received an ACK
    void record_ack_received() noexcept {
        timer_.record_ack_received();
    }

    /// Check for timeouts
    /// @param current_time Current timestamp
    /// @return Timeout event if any
    [[nodiscard]] TimeoutEvent check_timeout(uint32_t current_time) const noexcept {
        return timer_.check(current_time);
    }

    /// Handle timeout event
    /// @param event Timeout event
    /// @return true if session should continue, false if should abort
    bool handle_timeout(TimeoutEvent event) noexcept {
        switch (event) {
        case TimeoutEvent::SESSION_EXPIRED:
        case TimeoutEvent::IDLE_TIMEOUT:
            state_ = SessionState::ERROR;
            return false;

        case TimeoutEvent::CHUNK_TIMEOUT:
        case TimeoutEvent::ACK_TIMEOUT:
            if (timer_.increment_retry()) {
                // Retransmit
                sender_.mark_for_retransmit();
                return true;
            }
            state_ = SessionState::ERROR;
            return false;

        case TimeoutEvent::NONE:
        default:
            return true;
        }
    }

    /// Get session state
    [[nodiscard]] SessionState state() const noexcept { return state_; }

    /// Get progress (0.0 - 1.0)
    [[nodiscard]] float progress() const noexcept {
        if (total_bytes_ == 0) return 0.0f;
        return static_cast<float>(received_bytes_) / static_cast<float>(total_bytes_);
    }

    /// Get received bytes
    [[nodiscard]] uint32_t received_bytes() const noexcept { return received_bytes_; }

    /// Get total bytes expected
    [[nodiscard]] uint32_t total_bytes() const noexcept { return total_bytes_; }

    /// Check if transfer complete
    [[nodiscard]] bool is_complete() const noexcept {
        return received_bytes_ >= total_bytes_ && total_bytes_ > 0;
    }

    /// Access sender (for host-side use)
    FlowControlSender<WindowSize>& sender() noexcept { return sender_; }

    /// Access receiver (for device-side use)
    FlowControlReceiver& receiver() noexcept { return receiver_; }

    /// Access timer
    SessionTimer& timer() noexcept { return timer_; }

private:
    SessionTimer timer_;
    FlowControlSender<WindowSize> sender_;
    FlowControlReceiver receiver_;
    SessionState state_ = SessionState::IDLE;
    uint32_t total_bytes_ = 0;
    uint32_t received_bytes_ = 0;
};

// =============================================================================
// Progress Callback
// =============================================================================

/// Progress callback type
/// @param received Bytes received so far
/// @param total Total bytes expected
/// @param user_data User context
using ProgressCallback = void (*)(uint32_t received, uint32_t total, void* user_data);

/// Session with progress reporting
template <uint8_t WindowSize = 4>
class FirmwareUpdateSessionWithProgress : public FirmwareUpdateSession<WindowSize> {
public:
    using Base = FirmwareUpdateSession<WindowSize>;

    /// Set progress callback
    void set_progress_callback(ProgressCallback cb, void* user_data) noexcept {
        progress_cb_ = cb;
        progress_user_data_ = user_data;
        last_reported_progress_ = 0;
    }

    /// Process incoming data packet with progress reporting
    bool receive_data(uint8_t seq, size_t data_len, uint32_t current_time) noexcept {
        bool result = Base::receive_data(seq, data_len, current_time);
        if (result) {
            report_progress();
        }
        return result;
    }

private:
    void report_progress() noexcept {
        if (!progress_cb_) return;

        // Report every 1% or at completion
        uint32_t current = Base::received_bytes();
        uint32_t total = Base::total_bytes();

        if (total == 0) return;

        uint32_t percent = (current * 100) / total;
        if (percent > last_reported_progress_ || current >= total) {
            progress_cb_(current, total, progress_user_data_);
            last_reported_progress_ = percent;
        }
    }

    ProgressCallback progress_cb_ = nullptr;
    void* progress_user_data_ = nullptr;
    uint32_t last_reported_progress_ = 0;
};

} // namespace umiboot

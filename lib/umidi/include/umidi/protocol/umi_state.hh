// SPDX-License-Identifier: MIT
// UMI-OS MIDI Library - State Synchronization Protocol
// For WebMIDI/WebUSB anti-brick and resume support
#pragma once

#include "umi_sysex.hh"
#include "umi_bootloader.hh"
#include <cstdint>
#include <cstddef>

namespace umidi::protocol {

// =============================================================================
// Device State
// =============================================================================

enum class DeviceState : uint8_t {
    IDLE            = 0x00,  // Ready for operations
    UPDATE_STARTING = 0x01,  // Update initialization
    UPDATE_RECEIVING= 0x02,  // Receiving firmware data
    UPDATE_VERIFYING= 0x03,  // Verifying firmware
    UPDATE_READY    = 0x04,  // Verified, ready to commit
    UPDATE_COMMITTED= 0x05,  // Committed, ready to reboot
    OBJECT_TRANSFER = 0x06,  // Object transfer in progress
    ERROR           = 0x0F,  // Error state
};

// =============================================================================
// State Commands (0x28-0x2F)
// =============================================================================

enum class StateCommand : uint8_t {
    STATE_QUERY     = 0x28,  // Query current state
    STATE_REPORT    = 0x29,  // State report (response or unsolicited)
    STATE_SUBSCRIBE = 0x2A,  // Subscribe to state changes
    STATE_UNSUBSCRIBE= 0x2B, // Unsubscribe from state changes
    RESUME_QUERY    = 0x2C,  // Query resume info
    RESUME_INFO     = 0x2D,  // Resume info response
    FW_RESUME       = 0x2E,  // Resume firmware update
    BOOT_SUCCESS    = 0x2F,  // Mark boot as successful
};

// =============================================================================
// State Report Structure
// =============================================================================
// Sent in response to STATE_QUERY or when state changes (if subscribed)

struct StateReport {
    DeviceState state;           // Current device state       [0]
    uint8_t progress_percent;    // Progress (0-100)           [1]
    uint8_t last_error;          // Last error code            [2]
    uint8_t last_ack_seq;        // Last acknowledged seq      [3]
    uint32_t session_id;         // Current session ID         [4-7]
    uint32_t received_bytes;     // Bytes received so far      [8-11]
    uint8_t flags;               // State flags                [12]
    uint8_t reserved[3];         // Reserved for alignment     [13-15]

    // Flags
    static constexpr uint8_t FLAG_RESUMABLE      = 0x01;
    static constexpr uint8_t FLAG_AUTHENTICATED  = 0x02;
    static constexpr uint8_t FLAG_ROLLBACK_AVAIL = 0x04;
    static constexpr uint8_t FLAG_UPDATE_PENDING = 0x08;

    [[nodiscard]] constexpr bool is_resumable() const noexcept {
        return flags & FLAG_RESUMABLE;
    }

    [[nodiscard]] constexpr bool is_authenticated() const noexcept {
        return flags & FLAG_AUTHENTICATED;
    }

    [[nodiscard]] constexpr bool rollback_available() const noexcept {
        return flags & FLAG_ROLLBACK_AVAIL;
    }

    [[nodiscard]] constexpr bool update_pending() const noexcept {
        return flags & FLAG_UPDATE_PENDING;
    }
};

static_assert(sizeof(StateReport) == 16, "StateReport must be 16 bytes");

// =============================================================================
// Resume Info Structure
// =============================================================================
// Contains information needed to resume an interrupted transfer

struct ResumeInfo {
    uint32_t session_id;         // Session identifier
    uint32_t firmware_hash;      // First 4 bytes of firmware hash (for verification)
    uint32_t received_bytes;     // Bytes already received
    uint32_t total_bytes;        // Total expected bytes
    uint8_t last_ack_seq;        // Last acknowledged sequence
    uint8_t chunk_size;          // Chunk size used (for alignment)
    uint16_t reserved;

    [[nodiscard]] constexpr bool can_resume(uint32_t new_session_id, uint32_t new_hash) const noexcept {
        return session_id == new_session_id && firmware_hash == new_hash;
    }

    [[nodiscard]] constexpr uint32_t next_offset() const noexcept {
        return received_bytes;
    }
};

static_assert(sizeof(ResumeInfo) == 20, "ResumeInfo must be 20 bytes");

// =============================================================================
// Boot Verification
// =============================================================================
// Tracks boot success to enable automatic rollback on failure

struct BootVerification {
    static constexpr uint32_t MAGIC = 0x554D4256;  // "UMBV"

    uint32_t magic;
    uint8_t boot_count;          // Incremented each boot, reset on success
    uint8_t max_attempts;        // Maximum boot attempts before rollback
    uint8_t verified;            // 1 = boot verified successful
    uint8_t reserved;
    uint32_t last_success_time;  // Timestamp of last successful boot
    uint32_t checksum;

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return magic == MAGIC;
    }

    [[nodiscard]] constexpr bool should_rollback() const noexcept {
        return is_valid() && !verified && boot_count >= max_attempts;
    }

    constexpr void increment_boot() noexcept {
        if (boot_count < 255) ++boot_count;
        verified = 0;
    }

    constexpr void mark_success(uint32_t timestamp) noexcept {
        verified = 1;
        boot_count = 0;
        last_success_time = timestamp;
    }

    constexpr void init(uint8_t max_boot_attempts = 3) noexcept {
        magic = MAGIC;
        boot_count = 0;
        max_attempts = max_boot_attempts;
        verified = 1;
        last_success_time = 0;
    }

    [[nodiscard]] constexpr uint32_t compute_checksum() const noexcept {
        uint32_t sum = magic ^ boot_count ^ max_attempts ^ verified;
        sum ^= last_success_time;
        return sum;
    }

    constexpr void update_checksum() noexcept {
        checksum = compute_checksum();
    }

    [[nodiscard]] constexpr bool verify_checksum() const noexcept {
        return checksum == compute_checksum();
    }
};

static_assert(sizeof(BootVerification) == 16, "BootVerification must be 16 bytes");

// =============================================================================
// State Manager
// =============================================================================
// Manages device state and handles state sync commands

class StateManager {
public:
    /// Initialize state manager
    void init() noexcept {
        state_ = DeviceState::IDLE;
        progress_ = 0;
        session_id_ = 0;
        received_bytes_ = 0;
        total_bytes_ = 0;
        last_error_ = 0;
        last_ack_seq_ = 0;
        flags_ = 0;
        subscriber_count_ = 0;
    }

    /// Get current state
    [[nodiscard]] DeviceState state() const noexcept { return state_; }

    /// Set current state (triggers notification if subscribed)
    void set_state(DeviceState new_state) noexcept {
        if (state_ != new_state) {
            state_ = new_state;
            state_changed_ = true;
        }
    }

    /// Update progress
    void set_progress(uint8_t percent, uint32_t received, uint32_t total) noexcept {
        progress_ = percent;
        received_bytes_ = received;
        total_bytes_ = total;
    }

    /// Start a new session
    uint32_t start_session(uint32_t total_size) noexcept {
        // Generate session ID from timestamp-like counter
        session_id_ = ++session_counter_;
        total_bytes_ = total_size;
        received_bytes_ = 0;
        last_ack_seq_ = 0;
        flags_ |= StateReport::FLAG_RESUMABLE;
        set_state(DeviceState::UPDATE_STARTING);
        return session_id_;
    }

    /// Record received data
    void record_received(uint32_t bytes, uint8_t seq) noexcept {
        received_bytes_ += bytes;
        last_ack_seq_ = seq;
        progress_ = uint8_t((received_bytes_ * 100) / total_bytes_);
    }

    /// Set error state
    void set_error(uint8_t error_code) noexcept {
        last_error_ = error_code;
        set_state(DeviceState::ERROR);
    }

    /// Clear error and return to idle
    void clear_error() noexcept {
        last_error_ = 0;
        set_state(DeviceState::IDLE);
    }

    /// Build state report
    [[nodiscard]] StateReport build_report() const noexcept {
        return StateReport{
            .state = state_,
            .progress_percent = progress_,
            .last_error = last_error_,
            .last_ack_seq = last_ack_seq_,
            .session_id = session_id_,
            .received_bytes = received_bytes_,
            .flags = flags_,
            .reserved = {0, 0, 0},
        };
    }

    /// Build resume info
    [[nodiscard]] ResumeInfo build_resume_info(uint32_t firmware_hash, uint8_t chunk_size) const noexcept {
        return ResumeInfo{
            .session_id = session_id_,
            .firmware_hash = firmware_hash,
            .received_bytes = received_bytes_,
            .total_bytes = total_bytes_,
            .last_ack_seq = last_ack_seq_,
            .chunk_size = chunk_size,
            .reserved = 0,
        };
    }

    /// Check if state changed (and clear flag)
    [[nodiscard]] bool check_state_changed() noexcept {
        bool changed = state_changed_;
        state_changed_ = false;
        return changed;
    }

    /// Subscriber management
    void add_subscriber() noexcept {
        if (subscriber_count_ < 255) ++subscriber_count_;
    }

    void remove_subscriber() noexcept {
        if (subscriber_count_ > 0) --subscriber_count_;
    }

    [[nodiscard]] bool has_subscribers() const noexcept {
        return subscriber_count_ > 0;
    }

    /// Flag management
    void set_flag(uint8_t flag) noexcept { flags_ |= flag; }
    void clear_flag(uint8_t flag) noexcept { flags_ &= ~flag; }
    [[nodiscard]] bool has_flag(uint8_t flag) const noexcept { return flags_ & flag; }

    /// Session info
    [[nodiscard]] uint32_t session_id() const noexcept { return session_id_; }
    [[nodiscard]] uint32_t received_bytes() const noexcept { return received_bytes_; }
    [[nodiscard]] uint32_t total_bytes() const noexcept { return total_bytes_; }
    [[nodiscard]] uint8_t last_ack_seq() const noexcept { return last_ack_seq_; }

private:
    DeviceState state_ = DeviceState::IDLE;
    uint8_t progress_ = 0;
    uint8_t last_error_ = 0;
    uint8_t last_ack_seq_ = 0;
    uint8_t flags_ = 0;
    uint8_t subscriber_count_ = 0;
    bool state_changed_ = false;

    uint32_t session_id_ = 0;
    uint32_t received_bytes_ = 0;
    uint32_t total_bytes_ = 0;

    static inline uint32_t session_counter_ = 0;
};

// =============================================================================
// State Protocol Handler
// =============================================================================
// Processes state-related commands

class StateProtocolHandler {
public:
    StateProtocolHandler(StateManager& state_mgr) : state_(state_mgr) {}

    /// Process state command
    /// @param data Message data (after UMI SysEx parsing)
    /// @param len Data length
    /// @param send_fn Function to send response
    /// @return true if command was handled
    template <typename SendFn>
    bool process(const uint8_t* data, size_t len, SendFn send_fn) {
        if (len < 1) return false;

        auto cmd = static_cast<StateCommand>(data[0]);

        switch (cmd) {
        case StateCommand::STATE_QUERY:
            return handle_state_query(send_fn);

        case StateCommand::STATE_SUBSCRIBE:
            return handle_subscribe(true, send_fn);

        case StateCommand::STATE_UNSUBSCRIBE:
            return handle_subscribe(false, send_fn);

        case StateCommand::RESUME_QUERY:
            return handle_resume_query(send_fn);

        case StateCommand::BOOT_SUCCESS:
            return handle_boot_success(send_fn);

        default:
            return false;
        }
    }

    /// Check and send state updates to subscribers
    template <typename SendFn>
    void check_notifications(SendFn send_fn) {
        if (state_.has_subscribers() && state_.check_state_changed()) {
            send_state_report(send_fn);
        }
    }

    /// Set boot verification reference
    void set_boot_verification(BootVerification* boot_verif) noexcept {
        boot_verification_ = boot_verif;
    }

    /// Set resume info for current transfer
    void set_resume_context(uint32_t firmware_hash, uint8_t chunk_size) noexcept {
        resume_firmware_hash_ = firmware_hash;
        resume_chunk_size_ = chunk_size;
    }

private:
    template <typename SendFn>
    bool handle_state_query(SendFn send_fn) {
        send_state_report(send_fn);
        return true;
    }

    template <typename SendFn>
    bool handle_subscribe(bool subscribe, SendFn send_fn) {
        if (subscribe) {
            state_.add_subscriber();
        } else {
            state_.remove_subscriber();
        }

        // Send current state as confirmation
        send_state_report(send_fn);
        return true;
    }

    template <typename SendFn>
    bool handle_resume_query(SendFn send_fn) {
        auto resume_info = state_.build_resume_info(resume_firmware_hash_, resume_chunk_size_);

        MessageBuilder<64> builder;
        builder.begin(static_cast<Command>(StateCommand::RESUME_INFO), 0);

        // Encode resume info
        uint8_t info_bytes[sizeof(ResumeInfo)];
        std::memcpy(info_bytes, &resume_info, sizeof(ResumeInfo));
        builder.add_data(info_bytes, sizeof(ResumeInfo));

        send_fn(builder.data(), builder.finalize());
        return true;
    }

    template <typename SendFn>
    bool handle_boot_success(SendFn send_fn) {
        if (boot_verification_) {
            boot_verification_->mark_success(0);  // TODO: pass actual timestamp
            boot_verification_->update_checksum();
        }

        // Send ACK
        MessageBuilder<16> builder;
        builder.begin(Command::FW_ACK, 0);
        send_fn(builder.data(), builder.finalize());
        return true;
    }

    template <typename SendFn>
    void send_state_report(SendFn send_fn) {
        auto report = state_.build_report();

        MessageBuilder<64> builder;
        builder.begin(static_cast<Command>(StateCommand::STATE_REPORT), 0);

        // Encode state report
        uint8_t report_bytes[sizeof(StateReport)];
        std::memcpy(report_bytes, &report, sizeof(StateReport));
        builder.add_data(report_bytes, sizeof(StateReport));

        send_fn(builder.data(), builder.finalize());
    }

    StateManager& state_;
    BootVerification* boot_verification_ = nullptr;
    uint32_t resume_firmware_hash_ = 0;
    uint8_t resume_chunk_size_ = 0;
};

// =============================================================================
// Integrated State-Aware Firmware Update
// =============================================================================
// Extends SecureFirmwareUpdate with state management

template <const PlatformConfig* Config, size_t MaxChunkSize = 256>
class StatefulFirmwareUpdate {
public:
    void init(const FlashInterface& flash,
              const uint8_t* public_key = nullptr,
              const uint8_t* shared_secret = nullptr) noexcept {
        fw_update_.init(flash, public_key, shared_secret);
        state_mgr_.init();
        state_handler_.set_boot_verification(&boot_verif_);
        boot_verif_.init(3);  // 3 boot attempts before rollback
    }

    /// Process incoming message (state + firmware commands)
    template <typename SendFn>
    bool process(const uint8_t* data, size_t len, uint32_t current_time, SendFn send_fn) {
        auto msg = parse_message(data, len);
        if (!msg.valid) return false;

        // Check for state commands first
        uint8_t cmd_byte = static_cast<uint8_t>(msg.command);
        if (cmd_byte >= 0x28 && cmd_byte <= 0x2F) {
            // Decode payload for state handler
            uint8_t payload[64];
            payload[0] = cmd_byte;
            size_t payload_len = 1 + msg.decode_payload(&payload[1], sizeof(payload) - 1);
            return state_handler_.process(payload, payload_len, send_fn);
        }

        // Handle FW_RESUME specially
        if (msg.command == static_cast<Command>(StateCommand::FW_RESUME)) {
            return handle_resume(msg, current_time, send_fn);
        }

        // Delegate to firmware update handler
        bool result = fw_update_.process(data, len, current_time, send_fn);

        // Update state based on firmware update state
        sync_state();

        // Send state notifications if needed
        state_handler_.check_notifications(send_fn);

        return result;
    }

    /// Mark current boot as successful (call from application after successful startup)
    void mark_boot_success(uint32_t timestamp = 0) noexcept {
        boot_verif_.mark_success(timestamp);
        boot_verif_.update_checksum();
    }

    /// Check if rollback should be triggered (call early in boot)
    [[nodiscard]] bool should_rollback() const noexcept {
        return boot_verif_.should_rollback();
    }

    /// Increment boot counter (call at boot start)
    void increment_boot_count() noexcept {
        boot_verif_.increment_boot();
        boot_verif_.update_checksum();
    }

    /// Get state manager reference
    StateManager& state_manager() noexcept { return state_mgr_; }

    /// Get firmware update handler reference
    SecureFirmwareUpdate<Config, MaxChunkSize>& firmware_update() noexcept {
        return fw_update_;
    }

    /// Get boot verification reference
    BootVerification& boot_verification() noexcept { return boot_verif_; }

private:
    template <typename SendFn>
    bool handle_resume(const ParsedMessage& msg, uint32_t /*current_time*/, SendFn send_fn) {
        // Decode resume request
        uint8_t payload[32];
        size_t payload_len = msg.decode_payload(payload, sizeof(payload));

        if (payload_len < 8) {
            // Invalid resume request
            MessageBuilder<16> builder;
            builder.begin(Command::FW_NACK, msg.sequence);
            builder.add_byte(static_cast<uint8_t>(ErrorCode::INVALID_COMMAND));
            send_fn(builder.data(), builder.finalize());
            return true;
        }

        // Extract session_id and firmware_hash from request
        uint32_t req_session_id = (uint32_t(payload[0]) << 24) | (uint32_t(payload[1]) << 16) |
                                  (uint32_t(payload[2]) << 8) | payload[3];
        uint32_t req_fw_hash = (uint32_t(payload[4]) << 24) | (uint32_t(payload[5]) << 16) |
                               (uint32_t(payload[6]) << 8) | payload[7];

        // Check if we can resume
        auto resume_info = state_mgr_.build_resume_info(resume_fw_hash_, resume_chunk_size_);
        if (!resume_info.can_resume(req_session_id, req_fw_hash)) {
            // Cannot resume - session mismatch
            MessageBuilder<16> builder;
            builder.begin(Command::FW_NACK, msg.sequence);
            builder.add_byte(static_cast<uint8_t>(ErrorCode::INVALID_SEQUENCE));
            send_fn(builder.data(), builder.finalize());
            return true;
        }

        // Resume accepted - send resume info
        MessageBuilder<64> builder;
        builder.begin(Command::FW_ACK, msg.sequence);
        builder.add_u32(resume_info.received_bytes);  // Next expected offset
        builder.add_byte(resume_info.last_ack_seq);   // Next expected seq
        send_fn(builder.data(), builder.finalize());

        state_mgr_.set_state(DeviceState::UPDATE_RECEIVING);
        return true;
    }

    void sync_state() {
        auto fw_state = fw_update_.state();

        switch (fw_state) {
        case UpdateState::IDLE:
            state_mgr_.set_state(DeviceState::IDLE);
            break;
        case UpdateState::RECEIVING:
            state_mgr_.set_state(DeviceState::UPDATE_RECEIVING);
            state_mgr_.set_progress(
                uint8_t(fw_update_.progress() * 100),
                0, 0  // TODO: get actual bytes from fw_update
            );
            break;
        case UpdateState::VERIFYING:
            state_mgr_.set_state(DeviceState::UPDATE_VERIFYING);
            break;
        case UpdateState::READY_TO_COMMIT:
            state_mgr_.set_state(DeviceState::UPDATE_READY);
            break;
        case UpdateState::COMMITTED:
            state_mgr_.set_state(DeviceState::UPDATE_COMMITTED);
            break;
        case UpdateState::ERROR:
            state_mgr_.set_state(DeviceState::ERROR);
            break;
        }
    }

    SecureFirmwareUpdate<Config, MaxChunkSize> fw_update_;
    StateManager state_mgr_;
    StateProtocolHandler state_handler_{state_mgr_};
    BootVerification boot_verif_{};

    uint32_t resume_fw_hash_ = 0;
    uint8_t resume_chunk_size_ = 0;
};

// =============================================================================
// Convenience Type Aliases
// =============================================================================

using StatefulFirmwareUpdateSTM32F4 = StatefulFirmwareUpdate<&platforms::STM32F4_512K>;
using StatefulFirmwareUpdateSTM32H7 = StatefulFirmwareUpdate<&platforms::STM32H7_2M>;
using StatefulFirmwareUpdateESP32 = StatefulFirmwareUpdate<&platforms::ESP32_4M>;
using StatefulFirmwareUpdateRP2040 = StatefulFirmwareUpdate<&platforms::RP2040_2M>;

} // namespace umidi::protocol

# State Synchronization

State sync, resume support, and boot verification for WebMIDI/WebUSB.

## Overview

State synchronization enables:
- **Anti-brick protection**: Boot verification with automatic rollback
- **Resume support**: Continue interrupted firmware updates
- **Real-time status**: Subscribe to device state changes
- **Web compatibility**: Robust operation over WebMIDI/WebUSB

## Device States (protocol/umi_state.hh)

```cpp
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
```

## State Commands (0x28-0x2F)

| Command | Code | Direction | Description |
|---------|------|-----------|-------------|
| STATE_QUERY | 0x28 | Host→Device | Query current state |
| STATE_REPORT | 0x29 | Device→Host | State report |
| STATE_SUBSCRIBE | 0x2A | Host→Device | Subscribe to changes |
| STATE_UNSUBSCRIBE | 0x2B | Host→Device | Unsubscribe |
| RESUME_QUERY | 0x2C | Host→Device | Query resume info |
| RESUME_INFO | 0x2D | Device→Host | Resume info response |
| FW_RESUME | 0x2E | Host→Device | Resume firmware update |
| BOOT_SUCCESS | 0x2F | Host→Device | Mark boot successful |

## State Report

```cpp
struct StateReport {
    DeviceState state;           // Current device state
    uint8_t progress_percent;    // Progress (0-100)
    uint8_t last_error;          // Last error code
    uint8_t last_ack_seq;        // Last acknowledged sequence
    uint32_t session_id;         // Current session ID
    uint32_t received_bytes;     // Bytes received so far
    uint8_t flags;               // State flags

    // Flags
    static constexpr uint8_t FLAG_RESUMABLE      = 0x01;
    static constexpr uint8_t FLAG_AUTHENTICATED  = 0x02;
    static constexpr uint8_t FLAG_ROLLBACK_AVAIL = 0x04;
    static constexpr uint8_t FLAG_UPDATE_PENDING = 0x08;

    bool is_resumable() const noexcept;
    bool is_authenticated() const noexcept;
    bool rollback_available() const noexcept;
    bool update_pending() const noexcept;
};

static_assert(sizeof(StateReport) == 16);
```

## Resume Info

```cpp
struct ResumeInfo {
    uint32_t session_id;         // Session identifier
    uint32_t firmware_hash;      // First 4 bytes of firmware hash
    uint32_t received_bytes;     // Bytes already received
    uint32_t total_bytes;        // Total expected bytes
    uint8_t last_ack_seq;        // Last acknowledged sequence
    uint8_t chunk_size;          // Chunk size used

    bool can_resume(uint32_t new_session_id, uint32_t new_hash) const noexcept;
    uint32_t next_offset() const noexcept;
};

static_assert(sizeof(ResumeInfo) == 20);
```

## Boot Verification

Tracks boot success to enable automatic rollback on failure.

```cpp
struct BootVerification {
    static constexpr uint32_t MAGIC = 0x554D4256;  // "UMBV"

    uint32_t magic;
    uint8_t boot_count;          // Incremented each boot
    uint8_t max_attempts;        // Max attempts before rollback
    uint8_t verified;            // 1 = boot verified successful
    uint32_t last_success_time;  // Timestamp of last success
    uint32_t checksum;

    bool is_valid() const noexcept;
    bool should_rollback() const noexcept;

    void increment_boot() noexcept;
    void mark_success(uint32_t timestamp) noexcept;
    void init(uint8_t max_boot_attempts = 3) noexcept;

    uint32_t compute_checksum() const noexcept;
    void update_checksum() noexcept;
    bool verify_checksum() const noexcept;
};

static_assert(sizeof(BootVerification) == 16);
```

### Boot Flow

```
1. Bootloader starts
2. Load BootVerification from flash
3. Check boot_count < max_attempts
4. If exceeded, rollback to previous slot
5. Increment boot_count, save
6. Jump to application
7. Application calls mark_boot_success() after successful init
8. boot_count resets to 0
```

## State Manager

```cpp
class StateManager {
public:
    void init() noexcept;

    // State access
    DeviceState state() const noexcept;
    void set_state(DeviceState new_state) noexcept;

    // Progress tracking
    void set_progress(uint8_t percent, uint32_t received, uint32_t total) noexcept;

    // Session management
    uint32_t start_session(uint32_t total_size) noexcept;
    void record_received(uint32_t bytes, uint8_t seq) noexcept;

    // Error handling
    void set_error(uint8_t error_code) noexcept;
    void clear_error() noexcept;

    // Build reports
    StateReport build_report() const noexcept;
    ResumeInfo build_resume_info(uint32_t firmware_hash, uint8_t chunk_size) const noexcept;

    // State change notification
    bool check_state_changed() noexcept;

    // Subscriber management
    void add_subscriber() noexcept;
    void remove_subscriber() noexcept;
    bool has_subscribers() const noexcept;

    // Flag management
    void set_flag(uint8_t flag) noexcept;
    void clear_flag(uint8_t flag) noexcept;
    bool has_flag(uint8_t flag) const noexcept;

    // Session info
    uint32_t session_id() const noexcept;
    uint32_t received_bytes() const noexcept;
    uint32_t total_bytes() const noexcept;
    uint8_t last_ack_seq() const noexcept;
};
```

## State Protocol Handler

```cpp
class StateProtocolHandler {
public:
    StateProtocolHandler(StateManager& state_mgr);

    /// Process state command
    template <typename SendFn>
    bool process(const uint8_t* data, size_t len, SendFn send_fn);

    /// Check and send state updates to subscribers
    template <typename SendFn>
    void check_notifications(SendFn send_fn);

    /// Set boot verification reference
    void set_boot_verification(BootVerification* boot_verif) noexcept;

    /// Set resume context
    void set_resume_context(uint32_t firmware_hash, uint8_t chunk_size) noexcept;
};
```

## Stateful Firmware Update

Extends SecureFirmwareUpdate with state management.

```cpp
template <const PlatformConfig* Config, size_t MaxChunkSize = 256>
class StatefulFirmwareUpdate {
public:
    void init(const FlashInterface& flash,
              const uint8_t* public_key = nullptr,
              const uint8_t* shared_secret = nullptr) noexcept;

    /// Process incoming message (state + firmware commands)
    template <typename SendFn>
    bool process(const uint8_t* data, size_t len, uint32_t current_time, SendFn send_fn);

    /// Mark current boot as successful
    void mark_boot_success(uint32_t timestamp = 0) noexcept;

    /// Check if rollback should be triggered
    bool should_rollback() const noexcept;

    /// Increment boot counter (call at boot start)
    void increment_boot_count() noexcept;

    StateManager& state_manager() noexcept;
    SecureFirmwareUpdate<Config, MaxChunkSize>& firmware_update() noexcept;
    BootVerification& boot_verification() noexcept;
};

// Platform aliases
using StatefulFirmwareUpdateSTM32F4 = StatefulFirmwareUpdate<&platforms::STM32F4_512K>;
using StatefulFirmwareUpdateSTM32H7 = StatefulFirmwareUpdate<&platforms::STM32H7_2M>;
using StatefulFirmwareUpdateESP32 = StatefulFirmwareUpdate<&platforms::ESP32_4M>;
using StatefulFirmwareUpdateRP2040 = StatefulFirmwareUpdate<&platforms::RP2040_2M>;
```

## Usage Example

### Device-side (Bootloader)

```cpp
#include <umidi/umidi.hh>

umidi::protocol::StatefulFirmwareUpdateSTM32F4 updater;

void bootloader_main() {
    updater.init(flash_interface, public_key, shared_secret);

    // Check for boot failure
    updater.increment_boot_count();
    if (updater.should_rollback()) {
        // Rollback to previous firmware
        perform_rollback();
    }

    // Jump to application
    jump_to_app();
}
```

### Device-side (Application)

```cpp
void app_main() {
    // Mark boot as successful after initialization
    updater.mark_boot_success(get_timestamp());

    // Main loop
    while (true) {
        // Process incoming messages
        if (has_midi_data()) {
            uint8_t buf[256];
            size_t len = read_midi(buf, sizeof(buf));
            updater.process(buf, len, millis(), send_midi);

            if (updater.firmware_update().reboot_requested()) {
                system_reset();
            }
        }
    }
}
```

### Host-side (Web Client)

```javascript
class UMIUpdater {
    async update(firmware) {
        // 1. Query device state
        const state = await this.queryState();

        // 2. Check if we can resume
        if (state.is_resumable &&
            state.session_id === this.sessionId) {
            await this.resume(state.last_ack_seq);
        } else {
            await this.startFresh(firmware);
        }

        // 3. Send chunks with progress tracking
        for (const chunk of firmware.chunks()) {
            await this.sendChunk(chunk);

            // Periodic state sync for robustness
            if (chunk.index % 10 === 0) {
                await this.verifyState();
            }
        }

        // 4. Verify and commit
        await this.verify();
        await this.commit();

        // 5. Wait for reboot confirmation
        await this.waitForReboot();
    }
}
```

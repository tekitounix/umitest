# UMI SysEx Protocol

Standard IO and Firmware Update over MIDI SysEx.

## Protocol Overview

```
F0 <SYSEX_ID> <CMD> <SEQ> <DATA...> <CHECKSUM> F7

SYSEX_ID: 7E 7F 00 (Universal Non-Real-Time, Device ID 0x7F, UMI sub-ID)
CMD:      Command byte
SEQ:      Sequence number (0-127)
DATA:     7-bit encoded payload
CHECKSUM: XOR of CMD to last DATA byte (7-bit)
```

## 7-bit Encoding (protocol/umi_sysex.hh)

8-bit data to SysEx-safe 7-bit format conversion.

```cpp
/// Encode 8-bit data to 7-bit SysEx format
/// Every 7 bytes becomes 8 bytes (MSB collection method)
size_t encode_7bit(const uint8_t* in, size_t in_len, uint8_t* out);

/// Decode 7-bit SysEx data to 8-bit
size_t decode_7bit(const uint8_t* in, size_t in_len, uint8_t* out);

/// Calculate checksum
uint8_t calculate_checksum(const uint8_t* data, size_t len);

/// Calculate CRC32
uint32_t crc32(const uint8_t* data, size_t len);
```

## Command Categories

| Range | Category | Description |
|-------|----------|-------------|
| 0x01-0x0F | Standard IO | STDOUT, STDERR, STDIN, Flow Control |
| 0x10-0x1F | Firmware Update | Query, Begin, Data, Verify, Commit |
| 0x20-0x2F | System + State | Ping, Version, State Sync |
| 0x30-0x3F | Authentication | Challenge, Response, Session |
| 0x40-0x5F | Object Transfer | List, Read, Write, Delete |

## Standard IO (protocol/umi_sysex.hh)

### StandardIO Class

```cpp
template <size_t TxBufSize = 512, size_t RxBufSize = 512>
class StandardIO {
public:
    using StdinCallback = void (*)(const uint8_t* data, size_t len, void* ctx);

    /// Set callback for stdin data
    void set_stdin_callback(StdinCallback cb, void* ctx);

    /// Write to stdout
    template <typename SendFn>
    size_t write_stdout(const uint8_t* data, size_t len, SendFn send_fn);

    /// Write to stderr
    template <typename SendFn>
    size_t write_stderr(const uint8_t* data, size_t len, SendFn send_fn);

    /// Process incoming message
    bool process_message(const uint8_t* data, size_t len);

    /// Check flow control state
    bool is_paused() const;
    bool eof() const;
};
```

### Commands

| Command | Code | Direction | Description |
|---------|------|-----------|-------------|
| STDOUT_DATA | 0x01 | Device→Host | Standard output data |
| STDERR_DATA | 0x02 | Device→Host | Standard error data |
| STDIN_DATA | 0x03 | Host→Device | Standard input data |
| STDIN_EOF | 0x04 | Host→Device | End of input |
| FLOW_CTRL | 0x05 | Both | Pause/Resume transmission |

## Message Builder

```cpp
template <size_t MaxSize = 256>
class MessageBuilder {
public:
    /// Start new message
    void begin(Command cmd, uint8_t sequence);

    /// Add raw byte
    void add_byte(uint8_t b);

    /// Add 32-bit value (big endian)
    void add_u32(uint32_t value);

    /// Add 7-bit encoded data
    void add_data(const uint8_t* data, size_t len);

    /// Add raw data (no encoding)
    void add_raw(const uint8_t* data, size_t len);

    /// Finalize message (add checksum, F7)
    size_t finalize();

    /// Get message data
    const uint8_t* data() const;
};
```

## Message Parser

```cpp
struct ParsedMessage {
    bool valid;
    Command command;
    uint8_t sequence;
    const uint8_t* payload;
    size_t payload_len;

    /// Decode 7-bit payload to 8-bit
    size_t decode_payload(uint8_t* out, size_t max_len) const;
};

ParsedMessage parse_message(const uint8_t* data, size_t len);
```

## Firmware Update (protocol/umi_firmware.hh)

### FirmwareHeader

```cpp
struct FirmwareHeader {
    uint32_t magic;              // "UMIF" (0x554D4946)
    uint8_t  header_version;     // 1
    uint8_t  flags;              // FirmwareFlags
    uint32_t fw_version_major;
    uint32_t fw_version_minor;
    uint32_t fw_version_patch;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t load_address;
    uint32_t entry_point;
    uint8_t  target_board[16];
    uint8_t  fw_hash[32];        // SHA-256
    // ... (128 bytes total)
};

enum class FirmwareFlags : uint8_t {
    NONE       = 0x00,
    COMPRESSED = 0x01,
    ENCRYPTED  = 0x02,
    SIGNED     = 0x04,
    BOOTLOADER = 0x08,
};
```

### FirmwareHeaderBuilder

```cpp
class FirmwareHeaderBuilder {
public:
    FirmwareHeaderBuilder& version(uint32_t major, uint32_t minor, uint32_t patch);
    FirmwareHeaderBuilder& board(const char* board_id);
    FirmwareHeaderBuilder& image_size(uint32_t size);
    FirmwareHeaderBuilder& flags(FirmwareFlags f);
    FirmwareHeaderBuilder& load_address(uint32_t addr);
    FirmwareHeaderBuilder& entry_point(uint32_t addr);

    FirmwareHeader build() const;
};
```

### FirmwareValidator

```cpp
template <size_t MaxBoardIdLen = 16>
class FirmwareValidator {
public:
    void set_public_key(const uint8_t* key);
    void set_board_id(const char* id);
    void require_signature(bool required);

    enum class ValidationResult {
        OK,
        INVALID_MAGIC,
        INVALID_VERSION,
        BOARD_MISMATCH,
        SIZE_MISMATCH,
        CRC_MISMATCH,
        SIGNATURE_MISSING,
        SIGNATURE_INVALID,
    };

    ValidationResult validate_header(const FirmwareHeader& header);
    ValidationResult validate_signature(const FirmwareHeader& header,
                                        const uint8_t* data,
                                        const uint8_t* signature);
};
```

### Firmware Commands

| Command | Code | Description |
|---------|------|-------------|
| FW_QUERY | 0x10 | Query firmware info |
| FW_INFO | 0x11 | Firmware info response |
| FW_BEGIN | 0x12 | Begin firmware update |
| FW_DATA | 0x13 | Firmware data chunk |
| FW_VERIFY | 0x14 | Verify firmware |
| FW_COMMIT | 0x15 | Commit update |
| FW_ROLLBACK | 0x16 | Rollback to previous |
| FW_REBOOT | 0x17 | Reboot device |
| FW_ACK | 0x18 | Acknowledge |
| FW_NACK | 0x19 | Negative acknowledge |

## A/B Partition Bootloader (protocol/umi_bootloader.hh)

### BootConfig

```cpp
struct BootConfig {
    uint32_t magic;              // "UMIB" (0x554D4942)
    uint8_t  active_slot;        // 0=A, 1=B
    uint8_t  boot_count;
    uint8_t  max_boot_attempts;
    SlotState slot_a_state;
    SlotState slot_b_state;
    uint32_t slot_a_version;
    uint32_t slot_b_version;
    uint32_t checksum;
};

enum class SlotState : uint8_t {
    EMPTY = 0,
    VALID = 1,
    PENDING = 2,
    FAILED = 3,
};
```

### Platform Configurations

```cpp
namespace platforms {
    inline constexpr PlatformConfig STM32F4_512K = {
        .flash_base = 0x08000000,
        .flash_size = 512 * 1024,
        .slot_a_offset = 0x20000,
        .slot_b_offset = 0x60000,
        .slot_size = 256 * 1024,
        .config_offset = 0x10000,
        .sector_size = 16 * 1024,
    };

    inline constexpr PlatformConfig STM32H7_2M = { ... };
    inline constexpr PlatformConfig ESP32_4M = { ... };
    inline constexpr PlatformConfig RP2040_2M = { ... };
}
```

### BootloaderInterface

```cpp
template <const PlatformConfig* Config>
class BootloaderInterface {
public:
    enum class Slot { A = 0, B = 1 };

    Slot active_slot() const;
    Slot update_target() const;
    bool rollback_available() const;

    FlashResult prepare_slot(Slot slot);
    FlashResult write_firmware(Slot slot, uint32_t offset,
                               const uint8_t* data, size_t len);
    void mark_slot_valid(Slot slot, uint32_t version, uint32_t crc);
    bool commit_update();
    bool rollback();
};
```

## Authentication (protocol/umi_auth.hh)

### Authenticator

```cpp
template <size_t KeySize = 32, uint32_t SessionTimeoutMs = 300000>
class Authenticator {
public:
    using HmacSha256Fn = void (*)(const uint8_t* key, size_t key_len,
                                   const uint8_t* data, size_t data_len,
                                   uint8_t* out);
    using RandomFn = void (*)(uint8_t* out, size_t len);

    void init(const uint8_t* key, HmacSha256Fn hmac, RandomFn rng);

    /// Generate random challenge (32 bytes)
    void generate_challenge(uint8_t* out);

    /// Verify client response
    bool verify_response(const uint8_t* response, uint32_t current_time);

    /// Check if authenticated
    bool is_authenticated(uint32_t current_time) const;

    /// Logout
    void logout();
};
```

### AuthClient

```cpp
template <size_t KeySize = 32>
class AuthClient {
public:
    void init(const uint8_t* key, HmacSha256Fn hmac);

    /// Compute response for challenge
    void compute_response(const uint8_t* challenge, uint8_t* response);
};
```

### Auth Commands

| Command | Code | Direction | Description |
|---------|------|-----------|-------------|
| AUTH_CHALLENGE_REQ | 0x30 | Host→Device | Request challenge |
| AUTH_CHALLENGE | 0x31 | Device→Host | Challenge (32 bytes) |
| AUTH_RESPONSE | 0x32 | Host→Device | Response (32 bytes) |
| AUTH_OK | 0x33 | Device→Host | Authentication success |
| AUTH_FAIL | 0x34 | Device→Host | Authentication failed |
| AUTH_LOGOUT | 0x35 | Host→Device | Logout |
| AUTH_STATUS | 0x36 | Both | Query/report auth status |

## Session Management (protocol/umi_session.hh)

### TimeoutConfig

```cpp
struct TimeoutConfig {
    uint32_t session_timeout_ms;   // Default: 300000 (5 min)
    uint32_t chunk_timeout_ms;     // Default: 5000 (5 sec)
    uint32_t ack_timeout_ms;       // Default: 1000 (1 sec)
    uint32_t idle_timeout_ms;      // Default: 60000 (1 min)
};
```

### SessionTimer

```cpp
class SessionTimer {
public:
    void start_session(uint32_t current_time);
    void record_chunk(uint32_t current_time);
    void record_activity(uint32_t current_time);

    bool is_session_timeout(uint32_t current_time) const;
    bool is_chunk_timeout(uint32_t current_time) const;
    bool is_idle_timeout(uint32_t current_time) const;
};
```

### FlowControlSender

```cpp
template <uint8_t WindowSize = 4>
class FlowControlSender {
public:
    /// Enqueue data for sending
    int enqueue(const uint8_t* data, size_t len);

    /// Process ACK from receiver
    void process_ack(uint8_t ack_seq);

    /// Get next packet to send
    bool get_next(uint8_t* out, size_t& len, uint8_t& seq);

    /// Check if window is full
    bool is_window_full() const;
};
```

### FlowControlReceiver

```cpp
class FlowControlReceiver {
public:
    /// Process received packet
    bool process_packet(uint8_t seq, const uint8_t* data, size_t len);

    /// Get ACK sequence to send
    uint8_t get_ack_seq() const;

    /// Reset state
    void reset();
};
```

## SecureFirmwareUpdate (Complete Handler)

```cpp
template <const PlatformConfig* Config, size_t MaxChunkSize = 256>
class SecureFirmwareUpdate {
public:
    void init(const FlashInterface& flash,
              const uint8_t* public_key = nullptr,
              const uint8_t* shared_secret = nullptr);

    void set_crypto(HmacSha256Fn hmac, RandomFn rng,
                    Ed25519VerifyFn ed25519_verify, Sha256Fn sha256);

    template <typename SendFn>
    bool process(const uint8_t* data, size_t len,
                 uint32_t current_time, SendFn send_fn);

    UpdateState state() const;
    float progress() const;
    bool rollback_available() const;
    bool reboot_requested() const;
    void clear_reboot_request();
};

// Platform aliases
using SecureFirmwareUpdateSTM32F4 = SecureFirmwareUpdate<&platforms::STM32F4_512K>;
using SecureFirmwareUpdateSTM32H7 = SecureFirmwareUpdate<&platforms::STM32H7_2M>;
using SecureFirmwareUpdateESP32 = SecureFirmwareUpdate<&platforms::ESP32_4M>;
using SecureFirmwareUpdateRP2040 = SecureFirmwareUpdate<&platforms::RP2040_2M>;
```

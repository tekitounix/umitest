# API Reference

Complete API reference for umidi library.

## Namespaces

| Namespace | Description |
|-----------|-------------|
| `umidi` | Root namespace |
| `umidi::message` | Message types (NoteOn, CC, etc.) |
| `umidi::cc` | Control Change definitions |
| `umidi::protocol` | UMI Protocol (SysEx, FW Update, etc.) |
| `umidi::convert` | Value conversion utilities |

## Core Types

### UMP32

```cpp
namespace umidi {

struct UMP32 {
    uint32_t word;

    // Constructors
    constexpr UMP32() noexcept;
    constexpr explicit UMP32(uint32_t w) noexcept;
    constexpr UMP32(uint8_t mt, uint8_t group, uint8_t status,
                    uint8_t data1, uint8_t data2) noexcept;

    // Accessors
    uint8_t message_type() const noexcept;
    uint8_t group() const noexcept;
    uint8_t status() const noexcept;
    uint8_t channel() const noexcept;
    uint8_t data1() const noexcept;
    uint8_t data2() const noexcept;
    uint8_t note() const noexcept;
    uint8_t velocity() const noexcept;
    uint8_t cc_number() const noexcept;
    uint8_t cc_value() const noexcept;
    uint16_t pitch_bend_value() const noexcept;

    // Type checks
    bool is_note_on() const noexcept;
    bool is_note_off() const noexcept;
    bool is_cc() const noexcept;
    bool is_pitch_bend() const noexcept;
    bool is_program_change() const noexcept;
    bool is_channel_pressure() const noexcept;
    bool is_poly_pressure() const noexcept;
    bool is_realtime() const noexcept;
    bool is_system_common() const noexcept;

    // Factory methods (all constexpr, noexcept, nodiscard)
    static UMP32 note_on(uint8_t ch, uint8_t note, uint8_t vel, uint8_t group = 0);
    static UMP32 note_off(uint8_t ch, uint8_t note, uint8_t vel = 0, uint8_t group = 0);
    static UMP32 cc(uint8_t ch, uint8_t cc_num, uint8_t val, uint8_t group = 0);
    static UMP32 pitch_bend(uint8_t ch, uint16_t value, uint8_t group = 0);
    static UMP32 program_change(uint8_t ch, uint8_t program, uint8_t group = 0);
    static UMP32 channel_pressure(uint8_t ch, uint8_t pressure, uint8_t group = 0);
    static UMP32 poly_pressure(uint8_t ch, uint8_t note, uint8_t pressure, uint8_t group = 0);
    static UMP32 timing_clock(uint8_t group = 0);
    static UMP32 start(uint8_t group = 0);
    static UMP32 continue_msg(uint8_t group = 0);
    static UMP32 stop(uint8_t group = 0);
    static UMP32 active_sensing(uint8_t group = 0);
    static UMP32 system_reset(uint8_t group = 0);
    static UMP32 tune_request(uint8_t group = 0);
    static UMP32 mtc_quarter_frame(uint8_t data, uint8_t group = 0);
    static UMP32 song_position(uint16_t beats, uint8_t group = 0);
    static UMP32 song_select(uint8_t song, uint8_t group = 0);
};

} // namespace umidi
```

### UMP64

```cpp
namespace umidi {

struct UMP64 {
    uint32_t word0;
    uint32_t word1;

    constexpr UMP64() noexcept;
    constexpr UMP64(uint32_t w0, uint32_t w1) noexcept;

    uint8_t message_type() const noexcept;
    uint8_t group() const noexcept;
    uint8_t sysex_status() const noexcept;
    uint8_t sysex_num_bytes() const noexcept;

    static UMP64 sysex7_complete(uint8_t group, const uint8_t* data, uint8_t len);
    static UMP64 sysex7_start(uint8_t group, const uint8_t* data, uint8_t len);
    static UMP64 sysex7_continue(uint8_t group, const uint8_t* data, uint8_t len);
    static UMP64 sysex7_end(uint8_t group, const uint8_t* data, uint8_t len);
};

} // namespace umidi
```

### Parser

```cpp
namespace umidi {

class Parser {
public:
    Parser() noexcept;

    bool parse(uint8_t byte, UMP32& out) noexcept;
    bool parse_running(uint8_t byte, UMP32& out) noexcept;
    void reset() noexcept;
    void set_group(uint8_t group) noexcept;
};

class Serializer {
public:
    static size_t serialize(const UMP32& ump, uint8_t* out) noexcept;
    static size_t serialize_running(const UMP32& ump, uint8_t* out,
                                    uint8_t& running_status) noexcept;
};

} // namespace umidi
```

### Result

```cpp
namespace umidi {

enum class ErrorCode : uint8_t {
    Ok, IncompleteMessage, InvalidStatus, BufferOverflow,
    InvalidData, InvalidMessageType, ChannelFiltered,
    NotImplemented, NotSupported
};

struct Error {
    ErrorCode code;
    uint8_t context;

    static Error incomplete() noexcept;
    static Error invalid_status(uint8_t status) noexcept;
    static Error buffer_overflow() noexcept;
    static Error channel_filtered(uint8_t ch) noexcept;
};

template <typename T>
using Result = std::expected<T, Error>;

template <typename T>
Result<T> Ok(T value);

std::unexpected<Error> Err(ErrorCode code);
std::unexpected<Error> Err(Error error);

} // namespace umidi
```

### Event

```cpp
namespace umidi {

struct Event {
    uint32_t sample_pos;
    UMP32 ump;

    // Convenience accessors (delegate to ump)
    bool is_note_on() const noexcept;
    bool is_note_off() const noexcept;
    uint8_t note() const noexcept;
    uint8_t velocity() const noexcept;
    uint8_t channel() const noexcept;
};

static_assert(sizeof(Event) == 8);

template <size_t Capacity = 256>
class EventQueue {
public:
    EventQueue() noexcept;

    bool push(const Event& e) noexcept;
    bool pop(Event& out) noexcept;
    bool pop_until(uint32_t sample_pos, Event& out) noexcept;
    bool peek(Event& out) const noexcept;
    void clear() noexcept;
    bool is_empty() const noexcept;
    bool is_full() const noexcept;
    size_t size() const noexcept;
};

} // namespace umidi
```

## Message Types

### Channel Voice

```cpp
namespace umidi::message {

struct NoteOn {
    UMP32 ump;

    bool is_valid() const noexcept;
    uint8_t channel() const noexcept;
    uint8_t note() const noexcept;
    uint8_t velocity() const noexcept;
    uint8_t group() const noexcept;

    static NoteOn create(uint8_t ch, uint8_t note, uint8_t vel, uint8_t group = 0);
    static std::optional<NoteOn> from_ump(const UMP32& ump);
};

struct NoteOff { /* similar */ };
struct ControlChange { /* similar */ };
struct ProgramChange { /* similar */ };
struct PitchBend { /* similar */ };
struct ChannelPressure { /* similar */ };
struct PolyPressure { /* similar */ };

template <typename Handler>
void dispatch(const UMP32& ump, Handler&& handler);

} // namespace umidi::message
```

### Utility

```cpp
namespace umidi::message {

struct JRTimestamp {
    UMP32 ump;

    uint16_t timestamp() const noexcept;
    uint32_t to_microseconds() const noexcept;

    static JRTimestamp create(uint16_t ts, uint8_t group = 0);
    static JRTimestamp from_microseconds(uint32_t us, uint8_t group = 0);
};

class JRTimestampTracker {
public:
    void set_sample_rate(uint32_t rate) noexcept;
    void process(const JRTimestamp& ts) noexcept;
    bool has_timestamp() const noexcept;
    uint32_t get_sample_offset() const noexcept;
    void clear() noexcept;
};

} // namespace umidi::message
```

## Control Change

### RPN/NRPN Decoder

```cpp
namespace umidi::cc {

class ParameterNumberDecoder {
public:
    struct Result {
        uint16_t parameter_number;
        uint16_t value;
        bool is_nrpn;
        bool complete;
    };

    Result decode(uint8_t channel, uint8_t cc_num, uint8_t value) noexcept;

    template <typename T>
    bool is_selected(uint8_t channel) const noexcept;

    uint16_t get_selected(uint8_t channel) const noexcept;
    bool is_nrpn_selected(uint8_t channel) const noexcept;
    void reset() noexcept;
    void reset_channel(uint8_t channel) noexcept;
};

namespace pitch_bend_sensitivity {
    struct Value { uint8_t semitones; uint8_t cents; };
    Value parse(uint16_t rpn_value) noexcept;
    uint16_t format(uint8_t semitones, uint8_t cents = 0) noexcept;
}

} // namespace umidi::cc
```

## Protocol

### SysEx Encoding

```cpp
namespace umidi::protocol {

size_t encode_7bit(const uint8_t* in, size_t in_len, uint8_t* out) noexcept;
size_t decode_7bit(const uint8_t* in, size_t in_len, uint8_t* out) noexcept;
uint8_t calculate_checksum(const uint8_t* data, size_t len) noexcept;
uint32_t crc32(const uint8_t* data, size_t len) noexcept;

} // namespace umidi::protocol
```

### Message Builder/Parser

```cpp
namespace umidi::protocol {

template <size_t MaxSize = 256>
class MessageBuilder {
public:
    void begin(Command cmd, uint8_t sequence) noexcept;
    void add_byte(uint8_t b) noexcept;
    void add_u32(uint32_t value) noexcept;
    void add_data(const uint8_t* data, size_t len) noexcept;
    void add_raw(const uint8_t* data, size_t len) noexcept;
    size_t finalize() noexcept;
    const uint8_t* data() const noexcept;
};

struct ParsedMessage {
    bool valid;
    Command command;
    uint8_t sequence;
    const uint8_t* payload;
    size_t payload_len;
    size_t decode_payload(uint8_t* out, size_t max_len) const noexcept;
};

ParsedMessage parse_message(const uint8_t* data, size_t len) noexcept;

} // namespace umidi::protocol
```

### Firmware Update

```cpp
namespace umidi::protocol {

struct FirmwareHeader { /* 128 bytes */ };

class FirmwareHeaderBuilder {
public:
    FirmwareHeaderBuilder& version(uint32_t major, uint32_t minor, uint32_t patch);
    FirmwareHeaderBuilder& board(const char* board_id);
    FirmwareHeaderBuilder& image_size(uint32_t size);
    FirmwareHeaderBuilder& flags(FirmwareFlags f);
    FirmwareHeader build() const noexcept;
};

template <size_t MaxBoardIdLen = 16>
class FirmwareValidator {
public:
    void set_public_key(const uint8_t* key) noexcept;
    void set_board_id(const char* id) noexcept;
    void require_signature(bool required) noexcept;
    ValidationResult validate_header(const FirmwareHeader& header) noexcept;
};

template <const PlatformConfig* Config, size_t MaxChunkSize = 256>
class SecureFirmwareUpdate {
public:
    void init(const FlashInterface& flash,
              const uint8_t* public_key = nullptr,
              const uint8_t* shared_secret = nullptr) noexcept;

    template <typename SendFn>
    bool process(const uint8_t* data, size_t len,
                 uint32_t current_time, SendFn send_fn);

    UpdateState state() const noexcept;
    float progress() const noexcept;
    bool reboot_requested() const noexcept;
};

} // namespace umidi::protocol
```

### Transport

```cpp
namespace umidi::protocol {

class Transport {
public:
    virtual TransportType type() const noexcept = 0;
    virtual const TransportCapabilities& capabilities() const noexcept = 0;
    virtual bool is_connected() const noexcept = 0;
    virtual size_t send(const uint8_t* data, size_t len) = 0;
    virtual size_t receive(uint8_t* out, size_t max_len) = 0;
    virtual void flush() = 0;
    virtual void reset() = 0;
};

template <size_t TxBufSize = 512, size_t RxBufSize = 512>
class SysExTransport : public Transport { /* ... */ };

template <size_t TxBufSize = 1024, size_t RxBufSize = 1024>
class BulkTransport : public Transport { /* ... */ };

template <size_t MaxTransports = 2>
class TransportManager {
public:
    bool register_transport(Transport* transport) noexcept;
    Transport* get_best() noexcept;
    Transport* get(TransportType type) noexcept;
    bool any_connected() const noexcept;
};

} // namespace umidi::protocol
```

### State Management

```cpp
namespace umidi::protocol {

struct StateReport { /* 16 bytes */ };
struct ResumeInfo { /* 20 bytes */ };
struct BootVerification { /* 16 bytes */ };

class StateManager {
public:
    void init() noexcept;
    DeviceState state() const noexcept;
    void set_state(DeviceState new_state) noexcept;
    StateReport build_report() const noexcept;
    ResumeInfo build_resume_info(uint32_t firmware_hash, uint8_t chunk_size) const noexcept;
};

template <const PlatformConfig* Config, size_t MaxChunkSize = 256>
class StatefulFirmwareUpdate {
public:
    void init(const FlashInterface& flash,
              const uint8_t* public_key = nullptr,
              const uint8_t* shared_secret = nullptr) noexcept;

    template <typename SendFn>
    bool process(const uint8_t* data, size_t len,
                 uint32_t current_time, SendFn send_fn);

    void mark_boot_success(uint32_t timestamp = 0) noexcept;
    bool should_rollback() const noexcept;
};

} // namespace umidi::protocol
```

### Object Transfer

```cpp
namespace umidi::protocol {

struct ObjectHeader { /* 80 bytes */ };
struct SequenceMetadata { /* 16 bytes */ };
struct SampleMetadata { /* 16 bytes */ };
struct PresetMetadata { /* 16 bytes */ };
struct StorageInfo { /* 16 bytes */ };

class ObjectStorage {
public:
    virtual size_t list(ObjectType type, ObjectHeader* out, size_t max_count) = 0;
    virtual bool get_header(uint32_t id, ObjectHeader& out) = 0;
    virtual size_t read_data(uint32_t id, uint32_t offset, uint8_t* out, size_t len) = 0;
    virtual bool write_begin(const ObjectHeader& header) = 0;
    virtual bool write_data(uint32_t id, uint32_t offset, const uint8_t* data, size_t len) = 0;
    virtual bool write_commit(uint32_t id) = 0;
    virtual void write_abort(uint32_t id) = 0;
    virtual bool remove(uint32_t id) = 0;
    virtual StorageInfo get_storage_info() = 0;
    virtual uint32_t generate_id() = 0;
};

template <size_t MaxObjects = 16, size_t MaxDataSize = 65536>
class RAMObjectStorage : public ObjectStorage { /* ... */ };

template <size_t MaxChunkSize = 256>
class ObjectTransferHandler {
public:
    ObjectTransferHandler(ObjectStorage& storage, StateManager& state);

    template <typename SendFn>
    bool process(const uint8_t* data, size_t len, SendFn send_fn);
};

} // namespace umidi::protocol
```

## Value Conversions

```cpp
namespace umidi::convert {

// Velocity conversion
uint8_t velocity_16_to_7(uint16_t velocity_16) noexcept;
uint16_t velocity_7_to_16(uint8_t velocity_7) noexcept;

// Controller value conversion
uint16_t cc_7_to_14(uint8_t value_7) noexcept;
uint8_t cc_14_to_7(uint16_t value_14) noexcept;
uint32_t cc_7_to_32(uint8_t value_7) noexcept;
uint8_t cc_32_to_7(uint32_t value_32) noexcept;

// Pitch bend conversion
uint32_t pitch_bend_14_to_32(uint16_t bend_14) noexcept;
uint16_t pitch_bend_32_to_14(uint32_t bend_32) noexcept;
float pitch_bend_to_semitones(uint16_t value, float range = 2.0f) noexcept;

// Frequency conversion
float note_to_frequency(uint8_t note, float a4_freq = 440.0f) noexcept;
uint8_t frequency_to_note(float freq, float a4_freq = 440.0f) noexcept;

} // namespace umidi::convert
```

## Platform Aliases

```cpp
namespace umidi::protocol {

// SecureFirmwareUpdate
using SecureFirmwareUpdateSTM32F4 = SecureFirmwareUpdate<&platforms::STM32F4_512K>;
using SecureFirmwareUpdateSTM32H7 = SecureFirmwareUpdate<&platforms::STM32H7_2M>;
using SecureFirmwareUpdateESP32 = SecureFirmwareUpdate<&platforms::ESP32_4M>;
using SecureFirmwareUpdateRP2040 = SecureFirmwareUpdate<&platforms::RP2040_2M>;

// StatefulFirmwareUpdate
using StatefulFirmwareUpdateSTM32F4 = StatefulFirmwareUpdate<&platforms::STM32F4_512K>;
using StatefulFirmwareUpdateSTM32H7 = StatefulFirmwareUpdate<&platforms::STM32H7_2M>;
using StatefulFirmwareUpdateESP32 = StatefulFirmwareUpdate<&platforms::ESP32_4M>;
using StatefulFirmwareUpdateRP2040 = StatefulFirmwareUpdate<&platforms::RP2040_2M>;

} // namespace umidi::protocol
```

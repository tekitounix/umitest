# Core Types

umidi core types: UMP32, UMP64, Parser, Result, SysExBuffer.

## UMP32 (core/ump.hh)

32-bit Universal MIDI Packet for MIDI 1.0 Channel Voice messages.

### Memory Layout

```
Bit:  31-28  27-24  23-16     15-8    7-0
      MT     Group  Status    Data1   Data2
```

### Type Checking

Single-mask operations for efficient type detection:

```cpp
// Note On: MT=2, status=0x9X, velocity>0
bool is_note_on() const {
    return (word & 0xF0F00000u) == 0x20900000u && (word & 0x7Fu);
}
```

### API

```cpp
struct UMP32 {
    uint32_t word;

    // Type checks
    bool is_note_on() const;
    bool is_note_off() const;
    bool is_cc() const;
    bool is_pitch_bend() const;
    bool is_program_change() const;
    bool is_channel_pressure() const;
    bool is_poly_pressure() const;
    bool is_realtime() const;
    bool is_system_common() const;

    // Accessors
    uint8_t message_type() const;
    uint8_t group() const;
    uint8_t status() const;
    uint8_t channel() const;
    uint8_t data1() const;
    uint8_t data2() const;
    uint8_t note() const;
    uint8_t velocity() const;
    uint8_t cc_number() const;
    uint8_t cc_value() const;
    uint16_t pitch_bend_value() const;

    // Factory methods
    static UMP32 note_on(uint8_t ch, uint8_t note, uint8_t vel, uint8_t group = 0);
    static UMP32 note_off(uint8_t ch, uint8_t note, uint8_t vel = 0, uint8_t group = 0);
    static UMP32 cc(uint8_t ch, uint8_t cc_num, uint8_t val, uint8_t group = 0);
    static UMP32 pitch_bend(uint8_t ch, uint16_t value, uint8_t group = 0);
    static UMP32 program_change(uint8_t ch, uint8_t program, uint8_t group = 0);
    static UMP32 channel_pressure(uint8_t ch, uint8_t pressure, uint8_t group = 0);
    static UMP32 poly_pressure(uint8_t ch, uint8_t note, uint8_t pressure, uint8_t group = 0);

    // System messages
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
```

## UMP64 (core/ump.hh)

64-bit Universal MIDI Packet for SysEx and MIDI 2.0 messages.

```cpp
struct UMP64 {
    uint32_t word0;
    uint32_t word1;

    uint8_t sysex_status() const;    // 0=Complete, 1=Start, 2=Continue, 3=End
    uint8_t sysex_num_bytes() const; // 1-6 bytes per packet

    static UMP64 sysex7_complete(uint8_t group, const uint8_t* data, uint8_t len);
    static UMP64 sysex7_start(uint8_t group, const uint8_t* data, uint8_t len);
    static UMP64 sysex7_continue(uint8_t group, const uint8_t* data, uint8_t len);
    static UMP64 sysex7_end(uint8_t group, const uint8_t* data, uint8_t len);
};
```

## Parser (core/parser.hh)

MIDI 1.0 byte stream to UMP32 converter.

```cpp
class Parser {
public:
    /// Parse single byte, return true if complete message
    bool parse(uint8_t byte, UMP32& out);

    /// Parse with running status support
    bool parse_running(uint8_t byte, UMP32& out);

    /// Reset parser state
    void reset();

    /// Set UMP group for output messages
    void set_group(uint8_t group);
};
```

### Usage Example

```cpp
umidi::Parser parser;
umidi::UMP32 ump;

for (uint8_t byte : midi_stream) {
    if (parser.parse_running(byte, ump)) {
        if (ump.is_note_on()) {
            process_note_on(ump.note(), ump.velocity());
        }
    }
}
```

## Serializer (core/parser.hh)

UMP32 to MIDI 1.0 byte stream converter.

```cpp
class Serializer {
public:
    /// Serialize UMP32 to MIDI bytes
    /// @return Number of bytes written (1-3)
    static size_t serialize(const UMP32& ump, uint8_t* out);

    /// Serialize with running status optimization
    static size_t serialize_running(const UMP32& ump, uint8_t* out, uint8_t& running_status);
};
```

## Result (core/result.hh)

Error handling without exceptions using `std::expected`.

```cpp
enum class ErrorCode : uint8_t {
    Ok,
    IncompleteMessage,
    InvalidStatus,
    BufferOverflow,
    InvalidData,
    InvalidMessageType,
    ChannelFiltered,
    NotImplemented,
    NotSupported
};

struct Error {
    ErrorCode code;
    uint8_t context;

    static Error incomplete();
    static Error invalid_status(uint8_t status);
    static Error buffer_overflow();
    static Error channel_filtered(uint8_t ch);
};

template <typename T>
using Result = std::expected<T, Error>;

// Helper functions
template <typename T>
Result<T> Ok(T value);

std::unexpected<Error> Err(ErrorCode code);
std::unexpected<Error> Err(Error error);
```

## SysExBuffer (core/sysex_buffer.hh)

Ring buffer for accumulating SysEx data.

```cpp
template <size_t Capacity = 256>
class SysExBuffer {
public:
    /// Push byte to buffer
    bool push(uint8_t byte);

    /// Get buffer contents
    const uint8_t* data() const;
    size_t size() const;

    /// Clear buffer
    void clear();

    /// Check if buffer is full
    bool is_full() const;
};
```

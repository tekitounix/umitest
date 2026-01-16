# Message Types

Type-safe message wrappers with semantic APIs.

## Channel Voice Messages (messages/channel_voice.hh)

### NoteOn

```cpp
namespace umidi::message {

struct NoteOn {
    UMP32 ump;

    bool is_valid() const;
    uint8_t channel() const;
    uint8_t note() const;
    uint8_t velocity() const;
    uint8_t group() const;

    static NoteOn create(uint8_t ch, uint8_t note, uint8_t vel, uint8_t group = 0);
    static std::optional<NoteOn> from_ump(const UMP32& ump);
};
}
```

### NoteOff

```cpp
struct NoteOff {
    UMP32 ump;

    bool is_valid() const;
    uint8_t channel() const;
    uint8_t note() const;
    uint8_t velocity() const;

    static NoteOff create(uint8_t ch, uint8_t note, uint8_t vel = 0, uint8_t group = 0);
};
```

### ControlChange

```cpp
struct ControlChange {
    UMP32 ump;

    bool is_valid() const;
    uint8_t channel() const;
    uint8_t controller() const;
    uint8_t value() const;

    static ControlChange create(uint8_t ch, uint8_t cc, uint8_t val, uint8_t group = 0);
};
```

### ProgramChange

```cpp
struct ProgramChange {
    UMP32 ump;

    bool is_valid() const;
    uint8_t channel() const;
    uint8_t program() const;

    static ProgramChange create(uint8_t ch, uint8_t program, uint8_t group = 0);
};
```

### PitchBend

```cpp
struct PitchBend {
    UMP32 ump;

    bool is_valid() const;
    uint8_t channel() const;
    uint16_t value() const;         // 0-16383, center=8192
    int16_t signed_value() const;   // -8192 to +8191

    static PitchBend create(uint8_t ch, uint16_t value, uint8_t group = 0);
    static PitchBend from_signed(uint8_t ch, int16_t value, uint8_t group = 0);
};
```

### ChannelPressure

```cpp
struct ChannelPressure {
    UMP32 ump;

    bool is_valid() const;
    uint8_t channel() const;
    uint8_t pressure() const;

    static ChannelPressure create(uint8_t ch, uint8_t pressure, uint8_t group = 0);
};
```

### PolyPressure

```cpp
struct PolyPressure {
    UMP32 ump;

    bool is_valid() const;
    uint8_t channel() const;
    uint8_t note() const;
    uint8_t pressure() const;

    static PolyPressure create(uint8_t ch, uint8_t note, uint8_t pressure, uint8_t group = 0);
};
```

### Message Dispatch

```cpp
template <typename Handler>
void dispatch(const UMP32& ump, Handler&& handler);

// Usage:
dispatch(ump, [](auto&& msg) {
    using T = std::decay_t<decltype(msg)>;
    if constexpr (std::is_same_v<T, NoteOn>) {
        handle_note_on(msg.note(), msg.velocity());
    } else if constexpr (std::is_same_v<T, ControlChange>) {
        handle_cc(msg.controller(), msg.value());
    }
});
```

## System Messages (messages/system.hh)

### Real-time Messages

```cpp
struct TimingClock  { static constexpr uint8_t STATUS = 0xF8; };
struct Start        { static constexpr uint8_t STATUS = 0xFA; };
struct Continue     { static constexpr uint8_t STATUS = 0xFB; };
struct Stop         { static constexpr uint8_t STATUS = 0xFC; };
struct ActiveSensing { static constexpr uint8_t STATUS = 0xFE; };
struct SystemReset  { static constexpr uint8_t STATUS = 0xFF; };
```

### System Common Messages

```cpp
struct MidiTimeCode {
    uint8_t message_type;  // 0-7
    uint8_t value;         // 0-15

    static MidiTimeCode from_ump(const UMP32& ump);
};

struct SongPosition {
    uint16_t beats;  // 0-16383

    static SongPosition from_ump(const UMP32& ump);
    static UMP32 to_ump(uint16_t beats, uint8_t group = 0);
};

struct SongSelect {
    uint8_t song;  // 0-127

    static SongSelect from_ump(const UMP32& ump);
    static UMP32 to_ump(uint8_t song, uint8_t group = 0);
};

struct TuneRequest {
    static UMP32 to_ump(uint8_t group = 0);
};
```

## SysEx Messages (messages/sysex.hh)

### SysEx7

```cpp
struct SysEx7 {
    UMP64 ump;

    enum class Status : uint8_t {
        COMPLETE = 0x00,  // Single complete message
        START    = 0x10,  // First packet of multi-packet
        CONTINUE = 0x20,  // Middle packet
        END      = 0x30   // Last packet
    };

    Status sysex_status() const;
    uint8_t num_bytes() const;  // 1-6
    uint8_t group() const;
    uint8_t data_at(size_t idx) const;
    size_t copy_data(uint8_t* out, size_t max_len) const;

    static SysEx7 create(Status status, uint8_t group, const uint8_t* data, size_t len);
};
```

### SysExParser

```cpp
template <size_t MaxSysExSize = 256>
class SysExParser {
public:
    enum class Result {
        NeedMoreData,
        Complete,
        Error
    };

    /// Parse MIDI 1.0 SysEx byte
    Result parse(uint8_t byte, uint8_t group = 0);

    /// Get accumulated data
    const uint8_t* data() const;
    size_t size() const;

    /// Reset parser
    void reset();
};
```

### SysExSerializer

```cpp
class SysExSerializer {
public:
    /// Serialize UMP64 SysEx7 to MIDI 1.0 bytes
    static size_t serialize(const SysEx7& packet, uint8_t* out, bool include_markers = false);
};
```

## Utility Messages (messages/utility.hh)

### JR Timestamp (Jitter Reduction)

```cpp
struct JRTimestamp {
    UMP32 ump;

    uint16_t timestamp() const;       // 0-16383 (14-bit ticks)
    uint32_t to_microseconds() const; // timestamp * 32us
    uint8_t group() const;

    static JRTimestamp create(uint16_t ts, uint8_t group = 0);
    static JRTimestamp from_microseconds(uint32_t us, uint8_t group = 0);
};
```

### JR Timestamp Tracker

```cpp
class JRTimestampTracker {
public:
    void set_sample_rate(uint32_t rate);
    void process(const JRTimestamp& ts);
    bool has_timestamp() const;
    uint32_t get_sample_offset() const;
    void clear();
};
```

### JR Clock

```cpp
struct JRClock {
    UMP32 ump;

    uint16_t timestamp() const;

    static JRClock create(uint16_t ts, uint8_t group = 0);
};
```

### NOOP

```cpp
struct NOOP {
    static UMP32 create(uint8_t group = 0);
};
```

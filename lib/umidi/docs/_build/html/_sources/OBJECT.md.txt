# Object Transfer Protocol

Transfer sequences, samples, presets, and other data over MIDI.

## Overview

Object Transfer Protocol generalizes firmware update for arbitrary data:

| Object Type | Description |
|-------------|-------------|
| SEQUENCE | Step/grid sequence patterns |
| SAMPLE | Audio sample data |
| PRESET | Synthesizer/effect presets |
| CONFIG | Device configuration |
| WAVETABLE | Wavetable data |
| SYSEX_DUMP | Generic SysEx dump |
| MIDI_FILE | Standard MIDI file |

## Object Types (protocol/umi_object.hh)

```cpp
constexpr uint32_t OBJECT_MAGIC = 0x554D494F;  // "UMIO"

enum class ObjectType : uint8_t {
    FIRMWARE    = 0x00,  // Use dedicated FW protocol
    SEQUENCE    = 0x01,
    SAMPLE      = 0x02,
    PRESET      = 0x03,
    CONFIG      = 0x04,
    WAVETABLE   = 0x05,
    SYSEX_DUMP  = 0x06,
    MIDI_FILE   = 0x07,
    USER_START  = 0x80,  // User-defined types
};
```

## Object Flags

```cpp
enum class ObjectFlags : uint16_t {
    NONE        = 0x0000,
    COMPRESSED  = 0x0001,
    ENCRYPTED   = 0x0002,
    SIGNED      = 0x0004,
    READONLY    = 0x0008,
    SYSTEM      = 0x0010,
    TEMPORARY   = 0x0020,
};
```

## Object Header (80 bytes)

```cpp
struct ObjectHeader {
    uint32_t magic;              // "UMIO"
    uint8_t  header_version;     // 1
    uint8_t  object_type;        // ObjectType
    uint16_t flags;              // ObjectFlags
    uint32_t object_id;          // Unique ID
    uint32_t data_size;          // Size of data
    uint32_t data_crc32;         // CRC32 of data
    uint8_t  name[32];           // Human-readable name
    uint8_t  metadata[16];       // Type-specific metadata
    uint32_t created_time;       // Creation timestamp
    uint32_t modified_time;      // Modification timestamp
    uint32_t header_crc;         // CRC32 of header

    bool is_valid() const noexcept;
    ObjectType type() const noexcept;
    ObjectFlags get_flags() const noexcept;
    void set_flags(ObjectFlags f) noexcept;
    const char* get_name() const noexcept;
    void set_name(const char* n) noexcept;
    void init(ObjectType t, uint32_t id, const char* name, uint32_t size) noexcept;
};

static_assert(sizeof(ObjectHeader) == 80);
```

## Type-Specific Metadata

### Sequence Metadata

```cpp
struct SequenceMetadata {
    uint8_t  steps;          // Number of steps (16, 32, 64)
    uint8_t  tracks;         // Number of tracks
    uint16_t bpm_x10;        // BPM * 10 (1200 = 120.0)
    uint8_t  time_sig_num;   // Time signature numerator
    uint8_t  time_sig_den;   // Time signature denominator
    uint8_t  swing;          // Swing amount (0-127)
    uint8_t  quantize;       // Quantize setting
    uint32_t loop_start;     // Loop start step
    uint32_t loop_end;       // Loop end step

    void set_bpm(float bpm) noexcept;
    float get_bpm() const noexcept;
};

static_assert(sizeof(SequenceMetadata) == 16);
```

### Sample Metadata

```cpp
struct SampleMetadata {
    uint32_t sample_rate;    // 44100, 48000, etc.
    uint8_t  bit_depth;      // 8, 16, 24, 32
    uint8_t  channels;       // 1=mono, 2=stereo
    uint16_t root_note;      // MIDI note * 100 (for fine tuning)
    uint32_t loop_start;     // Loop start sample
    uint32_t loop_end;       // Loop end sample

    void set_root_note(uint8_t note, int8_t cents = 0) noexcept;
    uint8_t get_root_note() const noexcept;
    int8_t get_fine_tune() const noexcept;
};

static_assert(sizeof(SampleMetadata) == 16);
```

### Preset Metadata

```cpp
struct PresetMetadata {
    uint8_t  format_version;
    uint8_t  category;       // Category enum
    uint16_t bank;
    uint8_t  program;
    uint8_t  flags;
    uint8_t  author[10];

    enum class Category : uint8_t {
        NONE = 0, LEAD = 1, BASS = 2, PAD = 3,
        KEYS = 4, STRINGS = 5, BRASS = 6,
        DRUMS = 7, FX = 8, OTHER = 255,
    };
};

static_assert(sizeof(PresetMetadata) == 16);
```

### Config Metadata

```cpp
struct ConfigMetadata {
    uint8_t  config_version;
    uint8_t  config_type;    // ConfigType enum
    uint32_t target_device;  // Target device ID (0 = any)

    enum class ConfigType : uint8_t {
        GLOBAL = 0, MIDI = 1, AUDIO = 2,
        DISPLAY = 3, CONTROLS = 4, USER = 128,
    };
};

static_assert(sizeof(ConfigMetadata) == 16);
```

## Object Commands (0x40-0x5F)

### Management Commands (0x40-0x4F)

| Command | Code | Description |
|---------|------|-------------|
| OBJ_LIST | 0x40 | List objects of type |
| OBJ_LIST_RESP | 0x41 | List response |
| OBJ_INFO | 0x42 | Get object info |
| OBJ_INFO_RESP | 0x43 | Object info response |
| OBJ_READ_BEGIN | 0x44 | Begin reading object |
| OBJ_READ_DATA | 0x45 | Read data chunk |
| OBJ_WRITE_BEGIN | 0x46 | Begin writing object |
| OBJ_WRITE_DATA | 0x47 | Write data chunk |
| OBJ_DELETE | 0x48 | Delete object |
| OBJ_RENAME | 0x49 | Rename object |
| OBJ_VERIFY | 0x4A | Verify object integrity |
| OBJ_ACK | 0x4B | Operation ACK |
| OBJ_NACK | 0x4C | Operation NACK |
| OBJ_STORAGE_INFO | 0x4D | Query storage info |

### Action Commands (0x50-0x5F)

| Command | Code | Description |
|---------|------|-------------|
| SEQ_PLAY | 0x50 | Play sequence |
| SEQ_STOP | 0x51 | Stop sequence |
| SEQ_RECORD | 0x52 | Start recording |
| SAMPLE_PLAY | 0x53 | Play sample (preview) |
| SAMPLE_STOP | 0x54 | Stop sample |
| PRESET_LOAD | 0x55 | Load preset |
| PRESET_SAVE | 0x56 | Save current as preset |
| CONFIG_APPLY | 0x57 | Apply configuration |
| CONFIG_RESET | 0x58 | Reset to defaults |

## Object Error Codes

```cpp
enum class ObjectError : uint8_t {
    OK              = 0x00,
    NOT_FOUND       = 0x01,
    ALREADY_EXISTS  = 0x02,
    STORAGE_FULL    = 0x03,
    INVALID_TYPE    = 0x04,
    INVALID_HEADER  = 0x05,
    CRC_MISMATCH    = 0x06,
    PERMISSION_DENIED= 0x07,
    BUSY            = 0x08,
    IO_ERROR        = 0x09,
    INVALID_SEQUENCE= 0x0A,
};
```

## Storage Info

```cpp
struct StorageInfo {
    uint32_t total_space;    // Total storage in bytes
    uint32_t free_space;     // Free space in bytes
    uint32_t object_count;   // Number of stored objects
    uint32_t max_objects;    // Maximum number of objects
};
```

## Object Storage Interface

```cpp
class ObjectStorage {
public:
    virtual ~ObjectStorage() = default;

    /// List objects of a specific type
    virtual size_t list(ObjectType type, ObjectHeader* out, size_t max_count) = 0;

    /// Get object header by ID
    virtual bool get_header(uint32_t id, ObjectHeader& out) = 0;

    /// Read object data
    virtual size_t read_data(uint32_t id, uint32_t offset,
                             uint8_t* out, size_t len) = 0;

    /// Begin writing new object
    virtual bool write_begin(const ObjectHeader& header) = 0;

    /// Write object data chunk
    virtual bool write_data(uint32_t id, uint32_t offset,
                           const uint8_t* data, size_t len) = 0;

    /// Commit written object
    virtual bool write_commit(uint32_t id) = 0;

    /// Abort write operation
    virtual void write_abort(uint32_t id) = 0;

    /// Delete object
    virtual bool remove(uint32_t id) = 0;

    /// Rename object
    virtual bool rename(uint32_t id, const char* new_name) = 0;

    /// Get storage info
    virtual StorageInfo get_storage_info() = 0;

    /// Generate new unique ID
    virtual uint32_t generate_id() = 0;
};
```

## RAM Object Storage

For testing and staging.

```cpp
template <size_t MaxObjects = 16, size_t MaxDataSize = 65536>
class RAMObjectStorage : public ObjectStorage {
public:
    RAMObjectStorage();

    size_t list(ObjectType type, ObjectHeader* out, size_t max_count) override;
    bool get_header(uint32_t id, ObjectHeader& out) override;
    size_t read_data(uint32_t id, uint32_t offset, uint8_t* out, size_t len) override;
    bool write_begin(const ObjectHeader& header) override;
    bool write_data(uint32_t id, uint32_t offset, const uint8_t* data, size_t len) override;
    bool write_commit(uint32_t id) override;
    void write_abort(uint32_t id) override;
    bool remove(uint32_t id) override;
    bool rename(uint32_t id, const char* new_name) override;
    StorageInfo get_storage_info() override;
    uint32_t generate_id() override;
};
```

## Object Transfer Handler

```cpp
template <size_t MaxChunkSize = 256>
class ObjectTransferHandler {
public:
    ObjectTransferHandler(ObjectStorage& storage, StateManager& state);

    /// Process object command
    template <typename SendFn>
    bool process(const uint8_t* data, size_t len, SendFn send_fn);
};
```

## Usage Example

### Storing a Sequence

```cpp
#include <umidi/umidi.hh>

using namespace umidi::protocol;

RAMObjectStorage<16, 65536> storage;
StateManager state_mgr;
ObjectTransferHandler<256> handler(storage, state_mgr);

// Create sequence header
ObjectHeader header;
header.init(ObjectType::SEQUENCE, storage.generate_id(), "MySequence", sequence_size);

// Set metadata
SequenceMetadata* meta = reinterpret_cast<SequenceMetadata*>(header.metadata);
meta->steps = 16;
meta->tracks = 4;
meta->set_bpm(120.0f);
meta->time_sig_num = 4;
meta->time_sig_den = 4;

// Write to storage
storage.write_begin(header);
storage.write_data(header.object_id, 0, sequence_data, sequence_size);
storage.write_commit(header.object_id);
```

### Reading a Preset

```cpp
// List presets
ObjectHeader presets[8];
size_t count = storage.list(ObjectType::PRESET, presets, 8);

// Read first preset
if (count > 0) {
    uint8_t data[1024];
    size_t len = storage.read_data(presets[0].object_id, 0, data, sizeof(data));

    // Apply preset
    apply_preset(data, len);
}
```

### Host-side (Web Client)

```javascript
class ObjectManager {
    async listObjects(type) {
        await this.sendCommand('OBJ_LIST', [type]);
        return this.waitForResponse('OBJ_LIST_RESP');
    }

    async uploadSample(name, audioData, metadata) {
        // Create header
        const header = new ObjectHeader();
        header.init(ObjectType.SAMPLE, this.generateId(), name, audioData.length);
        header.setMetadata(metadata);

        // Begin write
        await this.sendCommand('OBJ_WRITE_BEGIN', header.toBytes());
        await this.waitForAck();

        // Send data chunks
        for (let offset = 0; offset < audioData.length; offset += 256) {
            const chunk = audioData.slice(offset, offset + 256);
            await this.sendCommand('OBJ_WRITE_DATA', [
                ...this.uint32ToBytes(offset),
                ...chunk
            ]);
            await this.waitForAck();
        }

        // Verify
        const crc = this.calculateCRC32(audioData);
        await this.sendCommand('OBJ_VERIFY', this.uint32ToBytes(crc));
        await this.waitForAck();
    }

    async playSample(objectId) {
        await this.sendCommand('SAMPLE_PLAY', this.uint32ToBytes(objectId));
    }

    async stopSample() {
        await this.sendCommand('SAMPLE_STOP', []);
    }
}
```

// SPDX-License-Identifier: MIT
// UMI-OS MIDI Library - Object Transfer Protocol
// For sequences, samples, presets, and other data transfers
#pragma once

#include "umi_sysex.hh"
#include "umi_state.hh"
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace umidi::protocol {

// =============================================================================
// Object Types
// =============================================================================

constexpr uint32_t OBJECT_MAGIC = 0x554D494F;  // "UMIO"

enum class ObjectType : uint8_t {
    FIRMWARE    = 0x00,  // Firmware image (use dedicated FW protocol)
    SEQUENCE    = 0x01,  // Step/grid sequence pattern
    SAMPLE      = 0x02,  // Audio sample data
    PRESET      = 0x03,  // Synthesizer/effect preset
    CONFIG      = 0x04,  // Device configuration
    WAVETABLE   = 0x05,  // Wavetable data
    SYSEX_DUMP  = 0x06,  // Generic SysEx dump
    MIDI_FILE   = 0x07,  // Standard MIDI file
    USER_START  = 0x80,  // User-defined types start here
};

// =============================================================================
// Object Flags
// =============================================================================

enum class ObjectFlags : uint16_t {
    NONE        = 0x0000,
    COMPRESSED  = 0x0001,  // Data is compressed
    ENCRYPTED   = 0x0002,  // Data is encrypted
    SIGNED      = 0x0004,  // Has signature
    READONLY    = 0x0008,  // Cannot be modified
    SYSTEM      = 0x0010,  // System object (protected)
    TEMPORARY   = 0x0020,  // Temporary (not persisted)
};

inline constexpr ObjectFlags operator|(ObjectFlags a, ObjectFlags b) {
    return static_cast<ObjectFlags>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}

inline constexpr bool operator&(ObjectFlags a, ObjectFlags b) {
    return (static_cast<uint16_t>(a) & static_cast<uint16_t>(b)) != 0;
}

// =============================================================================
// Object Header (80 bytes)
// =============================================================================

struct ObjectHeader {
    uint32_t magic;              // OBJECT_MAGIC ("UMIO")          [0-3]
    uint8_t  header_version;     // Header version (1)             [4]
    uint8_t  object_type;        // ObjectType                     [5]
    uint16_t flags;              // ObjectFlags                    [6-7]
    uint32_t object_id;          // Unique ID for this object      [8-11]
    uint32_t data_size;          // Size of data (excluding header)[12-15]
    uint32_t data_crc32;         // CRC32 of data                  [16-19]
    uint8_t  name[32];           // Human-readable name            [20-51]
    uint8_t  metadata[16];       // Type-specific metadata         [52-67]
    uint32_t created_time;       // Creation timestamp             [68-71]
    uint32_t modified_time;      // Last modification timestamp    [72-75]
    uint32_t header_crc;         // CRC32 of header                [76-79]

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return magic == OBJECT_MAGIC && header_version == 1;
    }

    [[nodiscard]] ObjectType type() const noexcept {
        return static_cast<ObjectType>(object_type);
    }

    [[nodiscard]] ObjectFlags get_flags() const noexcept {
        return static_cast<ObjectFlags>(flags);
    }

    void set_flags(ObjectFlags f) noexcept {
        flags = static_cast<uint16_t>(f);
    }

    [[nodiscard]] const char* get_name() const noexcept {
        return reinterpret_cast<const char*>(name);
    }

    void set_name(const char* n) noexcept {
        size_t len = 0;
        while (n[len] && len < 31) {
            name[len] = n[len];
            ++len;
        }
        name[len] = 0;
    }

    void init(ObjectType t, uint32_t id, const char* obj_name, uint32_t size) noexcept {
        magic = OBJECT_MAGIC;
        header_version = 1;
        object_type = static_cast<uint8_t>(t);
        flags = 0;
        object_id = id;
        data_size = size;
        data_crc32 = 0;
        set_name(obj_name);
        std::memset(metadata, 0, sizeof(metadata));
        created_time = 0;
        modified_time = 0;
        header_crc = 0;
    }
};

static_assert(sizeof(ObjectHeader) == 80, "ObjectHeader must be 80 bytes");

// =============================================================================
// Type-Specific Metadata Structures
// =============================================================================

// Sequence metadata (fits in 16 bytes)
struct SequenceMetadata {
    uint8_t  steps;          // Number of steps (e.g., 16, 32, 64)
    uint8_t  tracks;         // Number of tracks
    uint16_t bpm_x10;        // BPM * 10 (e.g., 1200 = 120.0 BPM)
    uint8_t  time_sig_num;   // Time signature numerator
    uint8_t  time_sig_den;   // Time signature denominator
    uint8_t  swing;          // Swing amount (0-127)
    uint8_t  quantize;       // Quantize setting
    uint32_t loop_start;     // Loop start step
    uint32_t loop_end;       // Loop end step

    void set_bpm(float bpm) noexcept {
        bpm_x10 = static_cast<uint16_t>(bpm * 10.0f);
    }

    [[nodiscard]] float get_bpm() const noexcept {
        return bpm_x10 / 10.0f;
    }
};

static_assert(sizeof(SequenceMetadata) == 16, "SequenceMetadata must be 16 bytes");

// Sample metadata (fits in 16 bytes)
struct SampleMetadata {
    uint32_t sample_rate;    // e.g., 44100, 48000
    uint8_t  bit_depth;      // 8, 16, 24, 32
    uint8_t  channels;       // 1=mono, 2=stereo
    uint16_t root_note;      // MIDI note number * 100 (for fine tuning)
    uint32_t loop_start;     // Loop start sample
    uint32_t loop_end;       // Loop end sample

    void set_root_note(uint8_t note, int8_t cents = 0) noexcept {
        root_note = static_cast<uint16_t>(note * 100 + cents);
    }

    [[nodiscard]] uint8_t get_root_note() const noexcept {
        return root_note / 100;
    }

    [[nodiscard]] int8_t get_fine_tune() const noexcept {
        return static_cast<int8_t>(root_note % 100);
    }
};

static_assert(sizeof(SampleMetadata) == 16, "SampleMetadata must be 16 bytes");

// Preset metadata (fits in 16 bytes)
struct PresetMetadata {
    uint8_t  format_version; // Preset format version
    uint8_t  category;       // Sound category (lead, bass, pad, etc.)
    uint16_t bank;           // Bank number
    uint8_t  program;        // Program number
    uint8_t  flags;          // Preset-specific flags
    uint8_t  author[10];     // Author name (truncated)

    enum class Category : uint8_t {
        NONE    = 0,
        LEAD    = 1,
        BASS    = 2,
        PAD     = 3,
        KEYS    = 4,
        STRINGS = 5,
        BRASS   = 6,
        DRUMS   = 7,
        FX      = 8,
        OTHER   = 255,
    };
};

static_assert(sizeof(PresetMetadata) == 16, "PresetMetadata must be 16 bytes");

// Config metadata (fits in 16 bytes)
struct ConfigMetadata {
    uint8_t  config_version; // Configuration version
    uint8_t  config_type;    // Type of configuration
    uint16_t reserved1;
    uint32_t target_device;  // Target device ID (0 = any)
    uint32_t reserved2[2];

    enum class ConfigType : uint8_t {
        GLOBAL      = 0,
        MIDI        = 1,
        AUDIO       = 2,
        DISPLAY     = 3,
        CONTROLS    = 4,
        USER        = 128,
    };
};

static_assert(sizeof(ConfigMetadata) == 16, "ConfigMetadata must be 16 bytes");

// =============================================================================
// Object Commands (0x40-0x5F)
// =============================================================================

enum class ObjectCommand : uint8_t {
    // Object management (0x40-0x4F)
    OBJ_LIST        = 0x40,  // List objects of type
    OBJ_LIST_RESP   = 0x41,  // List response
    OBJ_INFO        = 0x42,  // Get object info
    OBJ_INFO_RESP   = 0x43,  // Object info response
    OBJ_READ_BEGIN  = 0x44,  // Begin reading object
    OBJ_READ_DATA   = 0x45,  // Read data chunk
    OBJ_WRITE_BEGIN = 0x46,  // Begin writing object
    OBJ_WRITE_DATA  = 0x47,  // Write data chunk
    OBJ_DELETE      = 0x48,  // Delete object
    OBJ_RENAME      = 0x49,  // Rename object
    OBJ_VERIFY      = 0x4A,  // Verify object integrity
    OBJ_ACK         = 0x4B,  // Object operation ACK
    OBJ_NACK        = 0x4C,  // Object operation NACK
    OBJ_STORAGE_INFO= 0x4D,  // Query storage info

    // Object-specific actions (0x50-0x5F)
    SEQ_PLAY        = 0x50,  // Play sequence
    SEQ_STOP        = 0x51,  // Stop sequence
    SEQ_RECORD      = 0x52,  // Start recording
    SAMPLE_PLAY     = 0x53,  // Play sample (preview)
    SAMPLE_STOP     = 0x54,  // Stop sample
    PRESET_LOAD     = 0x55,  // Load preset
    PRESET_SAVE     = 0x56,  // Save current state as preset
    CONFIG_APPLY    = 0x57,  // Apply configuration
    CONFIG_RESET    = 0x58,  // Reset to defaults
};

// Object error codes
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

// =============================================================================
// Storage Info
// =============================================================================

struct StorageInfo {
    uint32_t total_space;        // Total storage in bytes
    uint32_t free_space;         // Free space in bytes
    uint32_t object_count;       // Number of stored objects
    uint32_t max_objects;        // Maximum number of objects
};

// =============================================================================
// Object Storage Interface (Abstract)
// =============================================================================

class ObjectStorage {
public:
    virtual ~ObjectStorage() = default;

    /// List objects of a specific type
    /// @param type Object type to list (0xFF for all)
    /// @param out Array to store headers
    /// @param max_count Maximum number of headers to return
    /// @return Number of objects found
    virtual size_t list(ObjectType type, ObjectHeader* out, size_t max_count) = 0;

    /// Get object header by ID
    virtual bool get_header(uint32_t id, ObjectHeader& out) = 0;

    /// Read object data
    virtual size_t read_data(uint32_t id, uint32_t offset, uint8_t* out, size_t len) = 0;

    /// Begin writing new object
    virtual bool write_begin(const ObjectHeader& header) = 0;

    /// Write object data chunk
    virtual bool write_data(uint32_t id, uint32_t offset, const uint8_t* data, size_t len) = 0;

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

// =============================================================================
// RAM Object Storage (for testing/staging)
// =============================================================================

template <size_t MaxObjects = 16, size_t MaxDataSize = 65536>
class RAMObjectStorage : public ObjectStorage {
public:
    RAMObjectStorage() {
        for (auto& slot : slots_) {
            slot.used = false;
        }
    }

    size_t list(ObjectType type, ObjectHeader* out, size_t max_count) override {
        size_t count = 0;
        for (const auto& slot : slots_) {
            if (count >= max_count) break;
            if (slot.used) {
                if (type == ObjectType::USER_START || slot.header.type() == type) {
                    out[count++] = slot.header;
                }
            }
        }
        return count;
    }

    bool get_header(uint32_t id, ObjectHeader& out) override {
        for (const auto& slot : slots_) {
            if (slot.used && slot.header.object_id == id) {
                out = slot.header;
                return true;
            }
        }
        return false;
    }

    size_t read_data(uint32_t id, uint32_t offset, uint8_t* out, size_t len) override {
        for (const auto& slot : slots_) {
            if (slot.used && slot.header.object_id == id) {
                if (offset >= slot.data_size) return 0;
                size_t available = slot.data_size - offset;
                size_t to_read = (len < available) ? len : available;
                std::memcpy(out, slot.data + offset, to_read);
                return to_read;
            }
        }
        return 0;
    }

    bool write_begin(const ObjectHeader& header) override {
        // Find free slot
        for (auto& slot : slots_) {
            if (!slot.used) {
                slot.header = header;
                slot.data_size = 0;
                slot.pending = true;
                return true;
            }
        }
        return false;  // No free slots
    }

    bool write_data(uint32_t id, uint32_t offset, const uint8_t* data, size_t len) override {
        for (auto& slot : slots_) {
            if (slot.pending && slot.header.object_id == id) {
                if (offset + len > MaxDataSize / MaxObjects) return false;
                std::memcpy(slot.data + offset, data, len);
                if (offset + len > slot.data_size) {
                    slot.data_size = offset + len;
                }
                return true;
            }
        }
        return false;
    }

    bool write_commit(uint32_t id) override {
        for (auto& slot : slots_) {
            if (slot.pending && slot.header.object_id == id) {
                slot.used = true;
                slot.pending = false;
                return true;
            }
        }
        return false;
    }

    void write_abort(uint32_t id) override {
        for (auto& slot : slots_) {
            if (slot.pending && slot.header.object_id == id) {
                slot.pending = false;
                break;
            }
        }
    }

    bool remove(uint32_t id) override {
        for (auto& slot : slots_) {
            if (slot.used && slot.header.object_id == id) {
                slot.used = false;
                return true;
            }
        }
        return false;
    }

    bool rename(uint32_t id, const char* new_name) override {
        for (auto& slot : slots_) {
            if (slot.used && slot.header.object_id == id) {
                slot.header.set_name(new_name);
                return true;
            }
        }
        return false;
    }

    StorageInfo get_storage_info() override {
        uint32_t count = 0;
        uint32_t used_space = 0;
        for (const auto& slot : slots_) {
            if (slot.used) {
                ++count;
                used_space += slot.data_size + static_cast<uint32_t>(sizeof(ObjectHeader));
            }
        }
        return StorageInfo{
            .total_space = static_cast<uint32_t>(MaxDataSize),
            .free_space = static_cast<uint32_t>(MaxDataSize) - used_space,
            .object_count = count,
            .max_objects = static_cast<uint32_t>(MaxObjects),
        };
    }

    uint32_t generate_id() override {
        return ++next_id_;
    }

private:
    struct Slot {
        ObjectHeader header;
        uint8_t data[MaxDataSize / MaxObjects];
        uint32_t data_size = 0;
        bool used = false;
        bool pending = false;
    };

    Slot slots_[MaxObjects];
    uint32_t next_id_ = 0;
};

// =============================================================================
// Object Transfer Handler
// =============================================================================

template <size_t MaxChunkSize = 256>
class ObjectTransferHandler {
public:
    ObjectTransferHandler(ObjectStorage& storage, StateManager& state)
        : storage_(storage), state_(state) {}

    /// Process object command
    template <typename SendFn>
    bool process(const uint8_t* data, size_t len, SendFn send_fn) {
        if (len < 1) return false;

        auto cmd = static_cast<ObjectCommand>(data[0]);

        switch (cmd) {
        case ObjectCommand::OBJ_LIST:
            return handle_list(&data[1], len - 1, send_fn);

        case ObjectCommand::OBJ_INFO:
            return handle_info(&data[1], len - 1, send_fn);

        case ObjectCommand::OBJ_READ_BEGIN:
            return handle_read_begin(&data[1], len - 1, send_fn);

        case ObjectCommand::OBJ_READ_DATA:
            return handle_read_data(&data[1], len - 1, send_fn);

        case ObjectCommand::OBJ_WRITE_BEGIN:
            return handle_write_begin(&data[1], len - 1, send_fn);

        case ObjectCommand::OBJ_WRITE_DATA:
            return handle_write_data(&data[1], len - 1, send_fn);

        case ObjectCommand::OBJ_VERIFY:
            return handle_verify(&data[1], len - 1, send_fn);

        case ObjectCommand::OBJ_DELETE:
            return handle_delete(&data[1], len - 1, send_fn);

        case ObjectCommand::OBJ_STORAGE_INFO:
            return handle_storage_info(send_fn);

        default:
            return false;
        }
    }

private:
    template <typename SendFn>
    bool handle_list(const uint8_t* data, size_t len, SendFn send_fn) {
        ObjectType type = (len > 0) ? static_cast<ObjectType>(data[0]) : ObjectType::USER_START;

        ObjectHeader headers[8];
        size_t count = storage_.list(type, headers, 8);

        MessageBuilder<512> builder;
        builder.begin(static_cast<Command>(ObjectCommand::OBJ_LIST_RESP), 0);
        builder.add_byte(static_cast<uint8_t>(count));

        for (size_t i = 0; i < count; ++i) {
            // Send abbreviated info: id, type, name
            builder.add_u32(headers[i].object_id);
            builder.add_byte(headers[i].object_type);
            builder.add_raw(headers[i].name, 16);  // First 16 chars of name
        }

        send_fn(builder.data(), builder.finalize());
        return true;
    }

    template <typename SendFn>
    bool handle_info(const uint8_t* data, size_t len, SendFn send_fn) {
        if (len < 4) return send_nack(ObjectError::INVALID_HEADER, send_fn);

        uint32_t id = (uint32_t(data[0]) << 24) | (uint32_t(data[1]) << 16) |
                      (uint32_t(data[2]) << 8) | data[3];

        ObjectHeader header;
        if (!storage_.get_header(id, header)) {
            return send_nack(ObjectError::NOT_FOUND, send_fn);
        }

        MessageBuilder<128> builder;
        builder.begin(static_cast<Command>(ObjectCommand::OBJ_INFO_RESP), 0);

        uint8_t header_bytes[sizeof(ObjectHeader)];
        std::memcpy(header_bytes, &header, sizeof(ObjectHeader));
        builder.add_data(header_bytes, sizeof(ObjectHeader));

        send_fn(builder.data(), builder.finalize());
        return true;
    }

    template <typename SendFn>
    bool handle_read_begin(const uint8_t* data, size_t len, SendFn send_fn) {
        if (len < 4) return send_nack(ObjectError::INVALID_HEADER, send_fn);

        uint32_t id = (uint32_t(data[0]) << 24) | (uint32_t(data[1]) << 16) |
                      (uint32_t(data[2]) << 8) | data[3];

        ObjectHeader header;
        if (!storage_.get_header(id, header)) {
            return send_nack(ObjectError::NOT_FOUND, send_fn);
        }

        read_id_ = id;
        read_offset_ = 0;
        read_total_ = header.data_size;

        state_.set_state(DeviceState::OBJECT_TRANSFER);
        return send_ack(send_fn);
    }

    template <typename SendFn>
    bool handle_read_data(const uint8_t* data, size_t len, SendFn send_fn) {
        if (read_id_ == 0) {
            return send_nack(ObjectError::INVALID_SEQUENCE, send_fn);
        }

        // Requested chunk size (default to MaxChunkSize)
        size_t chunk_size = MaxChunkSize;
        if (len >= 2) {
            chunk_size = (size_t(data[0]) << 8) | data[1];
            if (chunk_size > MaxChunkSize) chunk_size = MaxChunkSize;
        }

        uint8_t chunk[MaxChunkSize];
        size_t read_len = storage_.read_data(read_id_, read_offset_, chunk, chunk_size);

        MessageBuilder<MaxChunkSize + 32> builder;
        builder.begin(static_cast<Command>(ObjectCommand::OBJ_READ_DATA), read_seq_++);
        builder.add_u32(read_offset_);
        builder.add_data(chunk, read_len);

        send_fn(builder.data(), builder.finalize());

        read_offset_ += read_len;

        // Check if transfer complete
        if (read_offset_ >= read_total_) {
            read_id_ = 0;
            state_.set_state(DeviceState::IDLE);
        }

        return true;
    }

    template <typename SendFn>
    bool handle_write_begin(const uint8_t* data, size_t len, SendFn send_fn) {
        if (len < sizeof(ObjectHeader)) {
            return send_nack(ObjectError::INVALID_HEADER, send_fn);
        }

        ObjectHeader header;
        std::memcpy(&header, data, sizeof(ObjectHeader));

        if (!header.is_valid()) {
            return send_nack(ObjectError::INVALID_HEADER, send_fn);
        }

        if (!storage_.write_begin(header)) {
            return send_nack(ObjectError::STORAGE_FULL, send_fn);
        }

        write_id_ = header.object_id;
        write_offset_ = 0;
        write_total_ = header.data_size;
        write_crc_ = 0xFFFFFFFF;

        state_.set_state(DeviceState::OBJECT_TRANSFER);
        return send_ack(send_fn);
    }

    template <typename SendFn>
    bool handle_write_data(const uint8_t* data, size_t len, SendFn send_fn) {
        if (write_id_ == 0 || len < 5) {
            return send_nack(ObjectError::INVALID_SEQUENCE, send_fn);
        }

        uint32_t offset = (uint32_t(data[0]) << 24) | (uint32_t(data[1]) << 16) |
                          (uint32_t(data[2]) << 8) | data[3];

        if (offset != write_offset_) {
            return send_nack(ObjectError::INVALID_SEQUENCE, send_fn);
        }

        const uint8_t* chunk_data = &data[4];
        size_t chunk_len = len - 4;

        if (!storage_.write_data(write_id_, offset, chunk_data, chunk_len)) {
            storage_.write_abort(write_id_);
            write_id_ = 0;
            state_.set_state(DeviceState::ERROR);
            return send_nack(ObjectError::IO_ERROR, send_fn);
        }

        // Update CRC
        update_crc(chunk_data, chunk_len);
        write_offset_ += chunk_len;

        return send_ack(send_fn);
    }

    template <typename SendFn>
    bool handle_verify(const uint8_t* data, size_t len, SendFn send_fn) {
        if (write_id_ == 0) {
            return send_nack(ObjectError::INVALID_SEQUENCE, send_fn);
        }

        // Expected CRC from client
        if (len < 4) {
            return send_nack(ObjectError::INVALID_HEADER, send_fn);
        }

        uint32_t expected_crc = (uint32_t(data[0]) << 24) | (uint32_t(data[1]) << 16) |
                                (uint32_t(data[2]) << 8) | data[3];

        uint32_t actual_crc = write_crc_ ^ 0xFFFFFFFF;

        if (expected_crc != actual_crc) {
            storage_.write_abort(write_id_);
            write_id_ = 0;
            state_.set_state(DeviceState::ERROR);
            return send_nack(ObjectError::CRC_MISMATCH, send_fn);
        }

        if (!storage_.write_commit(write_id_)) {
            write_id_ = 0;
            state_.set_state(DeviceState::ERROR);
            return send_nack(ObjectError::IO_ERROR, send_fn);
        }

        write_id_ = 0;
        state_.set_state(DeviceState::IDLE);
        return send_ack(send_fn);
    }

    template <typename SendFn>
    bool handle_delete(const uint8_t* data, size_t len, SendFn send_fn) {
        if (len < 4) return send_nack(ObjectError::INVALID_HEADER, send_fn);

        uint32_t id = (uint32_t(data[0]) << 24) | (uint32_t(data[1]) << 16) |
                      (uint32_t(data[2]) << 8) | data[3];

        if (!storage_.remove(id)) {
            return send_nack(ObjectError::NOT_FOUND, send_fn);
        }

        return send_ack(send_fn);
    }

    template <typename SendFn>
    bool handle_storage_info(SendFn send_fn) {
        auto info = storage_.get_storage_info();

        MessageBuilder<32> builder;
        builder.begin(static_cast<Command>(ObjectCommand::OBJ_STORAGE_INFO), 0);
        builder.add_u32(info.total_space);
        builder.add_u32(info.free_space);
        builder.add_u32(info.object_count);
        builder.add_u32(info.max_objects);

        send_fn(builder.data(), builder.finalize());
        return true;
    }

    template <typename SendFn>
    bool send_ack(SendFn send_fn) {
        MessageBuilder<8> builder;
        builder.begin(static_cast<Command>(ObjectCommand::OBJ_ACK), 0);
        send_fn(builder.data(), builder.finalize());
        return true;
    }

    template <typename SendFn>
    bool send_nack(ObjectError err, SendFn send_fn) {
        MessageBuilder<8> builder;
        builder.begin(static_cast<Command>(ObjectCommand::OBJ_NACK), 0);
        builder.add_byte(static_cast<uint8_t>(err));
        send_fn(builder.data(), builder.finalize());
        return true;
    }

    void update_crc(const uint8_t* data, size_t len) noexcept {
        static constexpr uint32_t CRC32_POLY = 0xEDB88320;
        for (size_t i = 0; i < len; ++i) {
            write_crc_ ^= data[i];
            for (int j = 0; j < 8; ++j) {
                if (write_crc_ & 1) {
                    write_crc_ = (write_crc_ >> 1) ^ CRC32_POLY;
                } else {
                    write_crc_ >>= 1;
                }
            }
        }
    }

    ObjectStorage& storage_;
    StateManager& state_;

    // Read state
    uint32_t read_id_ = 0;
    uint32_t read_offset_ = 0;
    uint32_t read_total_ = 0;
    uint8_t read_seq_ = 0;

    // Write state
    uint32_t write_id_ = 0;
    uint32_t write_offset_ = 0;
    uint32_t write_total_ = 0;
    uint32_t write_crc_ = 0xFFFFFFFF;
};

} // namespace umidi::protocol

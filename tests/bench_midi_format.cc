// =============================================================================
// MIDI Format Benchmark: Current vs UMP
// =============================================================================
// Renode/Cortex-M4で実行し、MIDI 1.0メッセージの受信→変換→処理→送信を比較
// =============================================================================

#ifdef __clang__
#pragma GCC diagnostic ignored "-Wshorten-64-to-32"
#endif

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <array>

// =============================================================================
// Boot Vector (required for Renode startup)
// =============================================================================

extern "C" [[noreturn]] void _start();
extern std::uint32_t _estack;

__attribute__((section(".isr_vector"), used))
void* const boot_vectors[2] = {
    reinterpret_cast<void*>(&_estack),
    reinterpret_cast<void*>(_start),
};

// =============================================================================
// UART output
// =============================================================================

extern "C" int _write(int, const void*, int);

namespace {

void print(const char* s) {
    while (*s) { _write(1, s, 1); ++s; }
}

void println(const char* s) { print(s); print("\r\n"); }

void print_int(int val) {
    if (val < 0) { print("-"); val = -val; }
    if (val == 0) { print("0"); return; }
    char buf[12];
    int i = 0;
    while (val > 0) { buf[i++] = '0' + (val % 10); val /= 10; }
    while (i > 0) { char c[2] = {buf[--i], 0}; print(c); }
}

[[maybe_unused]]
void print_hex(uint32_t val) {
    print("0x");
    const char* hex = "0123456789ABCDEF";
    for (int i = 7; i >= 0; --i) {
        char c[2] = {hex[(val >> (i * 4)) & 0xF], 0};
        print(c);
    }
}

} // namespace

// =============================================================================
// DWT Cycle Counter
// =============================================================================

#define DWT_CTRL   (*(volatile uint32_t*)0xE0001000)
#define DWT_CYCCNT (*(volatile uint32_t*)0xE0001004)
#define SCB_DEMCR  (*(volatile uint32_t*)0xE000EDFC)

static void dwt_init() {
    SCB_DEMCR |= (1 << 24);
    DWT_CYCCNT = 0;
    DWT_CTRL |= 1;
}

// =============================================================================
// 現在の形式 (Current Event Format)
// =============================================================================

namespace current {

enum class EventType : uint8_t { Midi, Param, Raw };

struct MidiData {
    uint8_t bytes[3] = {0, 0, 0};
    uint8_t size = 0;

    [[nodiscard]] constexpr uint8_t status() const { return bytes[0]; }
    [[nodiscard]] constexpr uint8_t channel() const { return bytes[0] & 0x0F; }
    [[nodiscard]] constexpr uint8_t command() const { return bytes[0] & 0xF0; }
    [[nodiscard]] constexpr uint8_t note() const { return bytes[1]; }
    [[nodiscard]] constexpr uint8_t velocity() const { return bytes[2]; }
    [[nodiscard]] constexpr bool is_note_on() const {
        return command() == 0x90 && velocity() > 0;
    }
    [[nodiscard]] constexpr bool is_note_off() const {
        return command() == 0x80 || (command() == 0x90 && velocity() == 0);
    }
    [[nodiscard]] constexpr bool is_cc() const { return command() == 0xB0; }
};

struct ParamData {
    uint16_t id = 0;
    float value = 0.0f;
};

struct Event {
    uint8_t port_id = 0;
    uint32_t sample_pos = 0;
    EventType type = EventType::Midi;
    union {
        MidiData midi;
        ParamData param;
    };

    Event() : midi{} {}
};

// Parser: MIDI 1.0 bytes → Current Event
class Parser {
public:
    [[nodiscard]] bool parse(uint8_t byte, uint32_t pos, Event& out) {
        if (byte & 0x80) {
            running_status_ = byte;
            count_ = 0;
            return false;
        }
        data_[count_++] = byte;
        uint8_t needed = (running_status_ >= 0xC0 && running_status_ < 0xE0) ? 1 : 2;
        if (count_ < needed) return false;

        out.port_id = 0;
        out.sample_pos = pos;
        out.type = EventType::Midi;
        out.midi.bytes[0] = running_status_;
        out.midi.bytes[1] = data_[0];
        out.midi.bytes[2] = (needed > 1) ? data_[1] : 0;
        out.midi.size = needed + 1;
        count_ = 0;
        return true;
    }
private:
    uint8_t running_status_ = 0;
    uint8_t data_[2] = {};
    uint8_t count_ = 0;
};

// Serialize: Event → MIDI 1.0 bytes
inline size_t serialize(const Event& e, uint8_t* out) {
    if (e.type != EventType::Midi) return 0;
    out[0] = e.midi.bytes[0];
    out[1] = e.midi.bytes[1];
    if (e.midi.size > 2) {
        out[2] = e.midi.bytes[2];
        return 3;
    }
    return 2;
}

} // namespace current

// =============================================================================
// 従来形式最適化版 (Optimized Current Format)
// =============================================================================
// UMP-Optと同様の最適化を適用:
// 1. is_note_on() を単一マスク比較に
// 2. Parser でインクリメンタル構築
// 3. Serialize で分岐削減

namespace current_opt {

enum class EventType : uint8_t { Midi, Param, Raw };

struct MidiData {
    uint8_t bytes[3] = {0, 0, 0};
    uint8_t size = 0;

    [[nodiscard]] constexpr uint8_t status() const { return bytes[0]; }
    [[nodiscard]] constexpr uint8_t command() const { return bytes[0] & 0xF0; }
    [[nodiscard]] constexpr uint8_t note() const { return bytes[1]; }
    [[nodiscard]] constexpr uint8_t velocity() const { return bytes[2]; }

    // 最適化: 分岐を最小化
    [[nodiscard]] constexpr bool is_note_on() const {
        return (bytes[0] & 0xF0) == 0x90 && bytes[2] > 0;
    }
    [[nodiscard]] constexpr bool is_note_off() const {
        uint8_t cmd = bytes[0] & 0xF0;
        return cmd == 0x80 || (cmd == 0x90 && bytes[2] == 0);
    }
    [[nodiscard]] constexpr bool is_cc() const {
        return (bytes[0] & 0xF0) == 0xB0;
    }
};

struct ParamData {
    uint16_t id = 0;
    float value = 0.0f;
};

struct Event {
    uint8_t port_id = 0;
    uint32_t sample_pos = 0;
    EventType type = EventType::Midi;
    union {
        MidiData midi;
        ParamData param;
    };

    Event() : midi{} {}
};

// 最適化Parser: 2byte判定を先行
class Parser {
public:
    [[nodiscard]] bool parse(uint8_t byte, uint32_t pos, Event& out) {
        if (byte & 0x80) {
            running_status_ = byte;
            count_ = 0;
            is_2byte_ = ((byte & 0xE0) == 0xC0);
            return false;
        }

        if (count_ == 0) {
            data0_ = byte;
            count_ = 1;
            if (is_2byte_) {
                out.port_id = 0;
                out.sample_pos = pos;
                out.type = EventType::Midi;
                out.midi.bytes[0] = running_status_;
                out.midi.bytes[1] = data0_;
                out.midi.bytes[2] = 0;
                out.midi.size = 2;
                return true;
            }
            return false;
        }

        out.port_id = 0;
        out.sample_pos = pos;
        out.type = EventType::Midi;
        out.midi.bytes[0] = running_status_;
        out.midi.bytes[1] = data0_;
        out.midi.bytes[2] = byte;
        out.midi.size = 3;
        count_ = 0;
        return true;
    }
private:
    uint8_t running_status_ = 0;
    uint8_t data0_ = 0;
    uint8_t count_ = 0;
    bool is_2byte_ = false;
};

// 最適化Serialize
inline size_t serialize(const Event& e, uint8_t* out) {
    if (e.type != EventType::Midi) return 0;
    out[0] = e.midi.bytes[0];
    out[1] = e.midi.bytes[1];
    uint8_t cmd = out[0] & 0xF0;
    if ((cmd & 0xE0) == 0xC0) return 2;
    out[2] = e.midi.bytes[2];
    return 3;
}

} // namespace current_opt

// =============================================================================
// UMP準拠形式 (UMP-based Event Format)
// =============================================================================

namespace ump {

struct Event {
    uint32_t sample_pos;
    uint32_t ump[2];  // 64-bit UMP (MT 0-4対応)

    // UMP header access
    [[nodiscard]] constexpr uint8_t message_type() const { return (ump[0] >> 28) & 0x0F; }
    [[nodiscard]] constexpr uint8_t group() const { return (ump[0] >> 24) & 0x0F; }
    [[nodiscard]] constexpr uint8_t status() const { return (ump[0] >> 16) & 0xFF; }
    [[nodiscard]] constexpr uint8_t channel() const { return (ump[0] >> 16) & 0x0F; }

    // MIDI 1.0 (MT=2)
    [[nodiscard]] constexpr bool is_midi1() const { return message_type() == 0x2; }
    [[nodiscard]] constexpr uint8_t midi1_status() const { return (ump[0] >> 16) & 0xF0; }
    [[nodiscard]] constexpr uint8_t note() const { return (ump[0] >> 8) & 0x7F; }
    [[nodiscard]] constexpr uint8_t velocity() const { return ump[0] & 0x7F; }

    [[nodiscard]] constexpr bool is_note_on() const {
        return is_midi1() && midi1_status() == 0x90 && velocity() > 0;
    }
    [[nodiscard]] constexpr bool is_note_off() const {
        return is_midi1() && (midi1_status() == 0x80 ||
               (midi1_status() == 0x90 && velocity() == 0));
    }
    [[nodiscard]] constexpr bool is_cc() const {
        return is_midi1() && midi1_status() == 0xB0;
    }

    // Parameter (MT=0xF, custom extension)
    [[nodiscard]] constexpr bool is_param() const {
        return message_type() == 0xF && group() == 0x0;
    }
};

static_assert(sizeof(Event) == 12, "UMP Event should be 12 bytes");

// Parser: MIDI 1.0 bytes → UMP Event
class Parser {
public:
    [[nodiscard]] bool parse(uint8_t byte, uint32_t pos, Event& out) {
        if (byte & 0x80) {
            running_status_ = byte;
            count_ = 0;
            return false;
        }
        data_[count_++] = byte;
        uint8_t needed = (running_status_ >= 0xC0 && running_status_ < 0xE0) ? 1 : 2;
        if (count_ < needed) return false;

        // 単一の32bit演算で変換
        out.sample_pos = pos;
        out.ump[0] = 0x20000000u                       // MT=2 (MIDI 1.0 CV)
                   | (uint32_t(running_status_) << 16)
                   | (uint32_t(data_[0]) << 8)
                   | (needed > 1 ? data_[1] : 0);
        out.ump[1] = 0;
        count_ = 0;
        return true;
    }
private:
    uint8_t running_status_ = 0;
    uint8_t data_[2] = {};
    uint8_t count_ = 0;
};

// Serialize: UMP Event → MIDI 1.0 bytes
inline size_t serialize(const Event& e, uint8_t* out) {
    if (!e.is_midi1()) return 0;
    uint32_t w = e.ump[0];
    out[0] = (w >> 16) & 0xFF;  // status
    out[1] = (w >> 8) & 0x7F;   // data1
    uint8_t status = out[0] & 0xF0;
    if (status >= 0xC0 && status < 0xE0) return 2;
    out[2] = w & 0x7F;          // data2
    return 3;
}

} // namespace ump

// =============================================================================
// UMP最適化版 (Optimized UMP)
// =============================================================================
// 改善点:
// 1. is_note_on() を単一マスク比較に最適化
// 2. Parser で分岐を削減
// 3. Serialize でシフトをまとめる

namespace ump_opt {

struct Event {
    uint32_t sample_pos;
    uint32_t ump[2];

    // 最適化: 単一マスク比較で判定
    // MT=2, status=0x9X, velocity>0 を一度に検査
    [[nodiscard]] constexpr bool is_note_on() const {
        // Upper 4 bits = 0x2, bits 23-20 = 0x9, bits 6-0 > 0
        // Mask: 0xF0F00000 == 0x20900000 && (ump[0] & 0x7F) > 0
        return (ump[0] & 0xF0F00000u) == 0x20900000u && (ump[0] & 0x7Fu);
    }

    [[nodiscard]] constexpr bool is_note_off() const {
        // MT=2, status=0x8X or (status=0x9X && vel=0)
        uint32_t mt_status = ump[0] & 0xF0F00000u;
        return mt_status == 0x20800000u ||
               (mt_status == 0x20900000u && (ump[0] & 0x7Fu) == 0);
    }

    [[nodiscard]] constexpr bool is_cc() const {
        return (ump[0] & 0xF0F00000u) == 0x20B00000u;
    }

    [[nodiscard]] constexpr bool is_midi1() const {
        return (ump[0] >> 28) == 0x2u;
    }

    [[nodiscard]] constexpr uint8_t velocity() const { return ump[0] & 0x7F; }
    [[nodiscard]] constexpr uint8_t note() const { return (ump[0] >> 8) & 0x7F; }
};

static_assert(sizeof(Event) == 12, "UMP Event should be 12 bytes");

// 最適化Parser: UMPワードを直接構築
class Parser {
public:
    [[nodiscard]] bool parse(uint8_t byte, uint32_t pos, Event& out) {
        if (byte & 0x80) {
            // Status byte: 上位16bitに配置、MT=2を設定
            partial_ = 0x20000000u | (uint32_t(byte) << 16);
            count_ = 0;
            is_2byte_ = ((byte & 0xE0) == 0xC0);  // 0xC0 or 0xD0
            return false;
        }

        if (count_ == 0) {
            // First data byte: bit 15-8
            partial_ |= (uint32_t(byte) << 8);
            count_ = 1;
            if (is_2byte_) {
                out.sample_pos = pos;
                out.ump[0] = partial_;
                out.ump[1] = 0;
                return true;
            }
            return false;
        }

        // Second data byte: bit 7-0
        out.sample_pos = pos;
        out.ump[0] = partial_ | byte;
        out.ump[1] = 0;
        count_ = 0;
        return true;
    }
private:
    uint32_t partial_ = 0;  // 構築中のUMPワード
    uint8_t count_ = 0;
    bool is_2byte_ = false;
};

// 最適化Serialize: 分岐を削減
inline size_t serialize(const Event& e, uint8_t* out) {
    uint32_t w = e.ump[0];
    // MT check (upper 4 bits must be 0x2)
    if ((w >> 28) != 0x2u) return 0;

    out[0] = (w >> 16) & 0xFF;
    out[1] = (w >> 8) & 0x7F;

    // 2バイト判定: 0xC0-0xDF (Program Change, Channel Pressure)
    uint8_t cmd = out[0] & 0xF0;
    if ((cmd & 0xE0) == 0xC0) return 2;  // 0xC0 or 0xD0

    out[2] = w & 0x7F;
    return 3;
}

} // namespace ump_opt

// =============================================================================
// Benchmark
// =============================================================================

constexpr size_t NUM_MESSAGES = 100;
constexpr size_t ITERATIONS = 100;

// シミュレートされたMIDI入力データ (Note On/Off, CC)
static const uint8_t midi_input[] = {
    0x90, 60, 100,  // Note On C4
    0x90, 64, 80,   // Note On E4
    0x90, 67, 90,   // Note On G4
    0xB0, 1, 64,    // CC1 = 64
    0x80, 60, 0,    // Note Off C4
    0x90, 62, 100,  // Note On D4
    0xB0, 7, 100,   // CC7 = 100 (volume)
    0x80, 64, 0,    // Note Off E4
    0x80, 67, 0,    // Note Off G4
    0x80, 62, 0,    // Note Off D4
};
constexpr size_t MIDI_INPUT_SIZE = sizeof(midi_input);

// 処理結果格納用
static std::array<uint8_t, 512> output_buffer;

// --- Current Format Benchmark (Full) ---
__attribute__((noinline))
uint32_t bench_current() {
    current::Parser parser;
    current::Event events[16];
    size_t event_count = 0;
    size_t out_pos = 0;

    for (size_t i = 0; i < MIDI_INPUT_SIZE; ++i) {
        current::Event e;
        if (parser.parse(midi_input[i], i, e)) {
            events[event_count++] = e;
        }
    }

    for (size_t i = 0; i < event_count; ++i) {
        current::Event& e = events[i];
        if (e.type == current::EventType::Midi) {
            if (e.midi.is_note_on()) {
                e.midi.bytes[2] = (e.midi.bytes[2] * 3) / 4;
            }
            out_pos += current::serialize(e, output_buffer.data() + out_pos);
        }
    }

    return out_pos;
}

// --- Current Optimized: Full Pipeline ---
__attribute__((noinline))
uint32_t bench_current_opt() {
    current_opt::Parser parser;
    current_opt::Event events[16];
    size_t event_count = 0;
    size_t out_pos = 0;

    for (size_t i = 0; i < MIDI_INPUT_SIZE; ++i) {
        current_opt::Event e;
        if (parser.parse(midi_input[i], i, e)) {
            events[event_count++] = e;
        }
    }

    for (size_t i = 0; i < event_count; ++i) {
        current_opt::Event& e = events[i];
        if (e.type == current_opt::EventType::Midi) {
            if (e.midi.is_note_on()) {
                e.midi.bytes[2] = (e.midi.bytes[2] * 3) / 4;
            }
            out_pos += current_opt::serialize(e, output_buffer.data() + out_pos);
        }
    }

    return out_pos;
}

// --- Current Optimized: Parse only ---
__attribute__((noinline, used))
size_t bench_current_opt_parse(current_opt::Event* events) {
    current_opt::Parser parser;
    size_t event_count = 0;

    for (size_t i = 0; i < MIDI_INPUT_SIZE; ++i) {
        current_opt::Event e;
        if (parser.parse(midi_input[i], i, e)) {
            events[event_count++] = e;
        }
    }
    return event_count;
}

// --- Current Optimized: Dispatch only ---
__attribute__((noinline, used))
size_t bench_current_opt_dispatch(current_opt::Event* events, size_t count) {
    size_t note_on_count = 0;
    for (size_t i = 0; i < count; ++i) {
        if (events[i].type == current_opt::EventType::Midi) {
            if (events[i].midi.is_note_on()) {
                note_on_count = note_on_count + 1;
            }
        }
    }
    asm volatile("" : "+r"(note_on_count));
    return note_on_count;
}

// --- Current Optimized: Serialize only ---
__attribute__((noinline, used))
size_t bench_current_opt_serialize(current_opt::Event* events, size_t count) {
    size_t out_pos = 0;
    for (size_t i = 0; i < count; ++i) {
        out_pos += current_opt::serialize(events[i], output_buffer.data() + out_pos);
    }
    return out_pos;
}

// --- Current Format: Parse only ---
__attribute__((noinline, used))
size_t bench_current_parse(current::Event* events) {
    current::Parser parser;
    size_t event_count = 0;

    for (size_t i = 0; i < MIDI_INPUT_SIZE; ++i) {
        current::Event e;
        if (parser.parse(midi_input[i], i, e)) {
            events[event_count++] = e;
        }
    }
    return event_count;
}

// --- Current Format: Dispatch only (type check + is_note_on) ---
__attribute__((noinline, used))
size_t bench_current_dispatch(current::Event* events, size_t count) {
    size_t note_on_count = 0;
    for (size_t i = 0; i < count; ++i) {
        if (events[i].type == current::EventType::Midi) {
            if (events[i].midi.is_note_on()) {
                note_on_count = note_on_count + 1;
            }
        }
    }
    asm volatile("" : "+r"(note_on_count));  // Prevent optimization
    return note_on_count;
}

// --- Current Format: Serialize only ---
__attribute__((noinline, used))
size_t bench_current_serialize(current::Event* events, size_t count) {
    size_t out_pos = 0;
    for (size_t i = 0; i < count; ++i) {
        out_pos += current::serialize(events[i], output_buffer.data() + out_pos);
    }
    return out_pos;
}

// --- UMP Format Benchmark (Full) ---
__attribute__((noinline))
uint32_t bench_ump() {
    ump::Parser parser;
    ump::Event events[16];
    size_t event_count = 0;
    size_t out_pos = 0;

    // 受信 → 変換
    for (size_t i = 0; i < MIDI_INPUT_SIZE; ++i) {
        ump::Event e;
        if (parser.parse(midi_input[i], i, e)) {
            events[event_count++] = e;
        }
    }

    // 処理 (Note On → velocity変更して転送)
    for (size_t i = 0; i < event_count; ++i) {
        ump::Event& e = events[i];
        if (e.is_midi1()) {
            if (e.is_note_on()) {
                // velocity scaling (in-place bit manipulation)
                uint8_t vel = e.ump[0] & 0x7F;
                vel = (vel * 3) / 4;
                e.ump[0] = (e.ump[0] & ~0x7Fu) | vel;
            }
            // 送信
            out_pos += ump::serialize(e, output_buffer.data() + out_pos);
        }
    }

    return out_pos;
}

// --- UMP Format: Parse only ---
__attribute__((noinline, used))
size_t bench_ump_parse(ump::Event* events) {
    ump::Parser parser;
    size_t event_count = 0;

    for (size_t i = 0; i < MIDI_INPUT_SIZE; ++i) {
        ump::Event e;
        if (parser.parse(midi_input[i], i, e)) {
            events[event_count++] = e;
        }
    }
    return event_count;
}

// --- UMP Format: Dispatch only (is_midi1 + is_note_on) ---
__attribute__((noinline, used))
size_t bench_ump_dispatch(ump::Event* events, size_t count) {
    size_t note_on_count = 0;
    for (size_t i = 0; i < count; ++i) {
        if (events[i].is_midi1()) {
            if (events[i].is_note_on()) {
                note_on_count = note_on_count + 1;
            }
        }
    }
    asm volatile("" : "+r"(note_on_count));  // Prevent optimization
    return note_on_count;
}

// --- UMP Format: Serialize only ---
__attribute__((noinline, used))
size_t bench_ump_serialize(ump::Event* events, size_t count) {
    size_t out_pos = 0;
    for (size_t i = 0; i < count; ++i) {
        out_pos += ump::serialize(events[i], output_buffer.data() + out_pos);
    }
    return out_pos;
}

// --- UMP Optimized: Full Pipeline ---
__attribute__((noinline))
uint32_t bench_ump_opt() {
    ump_opt::Parser parser;
    ump_opt::Event events[16];
    size_t event_count = 0;
    size_t out_pos = 0;

    for (size_t i = 0; i < MIDI_INPUT_SIZE; ++i) {
        ump_opt::Event e;
        if (parser.parse(midi_input[i], i, e)) {
            events[event_count++] = e;
        }
    }

    for (size_t i = 0; i < event_count; ++i) {
        ump_opt::Event& e = events[i];
        if (e.is_midi1()) {
            if (e.is_note_on()) {
                uint8_t vel = e.ump[0] & 0x7F;
                vel = (vel * 3) / 4;
                e.ump[0] = (e.ump[0] & ~0x7Fu) | vel;
            }
            out_pos += ump_opt::serialize(e, output_buffer.data() + out_pos);
        }
    }
    return out_pos;
}

// --- UMP Optimized: Parse only ---
__attribute__((noinline, used))
size_t bench_ump_opt_parse(ump_opt::Event* events) {
    ump_opt::Parser parser;
    size_t event_count = 0;

    for (size_t i = 0; i < MIDI_INPUT_SIZE; ++i) {
        ump_opt::Event e;
        if (parser.parse(midi_input[i], i, e)) {
            events[event_count++] = e;
        }
    }
    return event_count;
}

// --- UMP Optimized: Dispatch only ---
__attribute__((noinline, used))
size_t bench_ump_opt_dispatch(ump_opt::Event* events, size_t count) {
    size_t note_on_count = 0;
    for (size_t i = 0; i < count; ++i) {
        if (events[i].is_midi1()) {
            if (events[i].is_note_on()) {
                note_on_count = note_on_count + 1;
            }
        }
    }
    asm volatile("" : "+r"(note_on_count));
    return note_on_count;
}

// --- UMP Optimized: Serialize only ---
__attribute__((noinline, used))
size_t bench_ump_opt_serialize(ump_opt::Event* events, size_t count) {
    size_t out_pos = 0;
    for (size_t i = 0; i < count; ++i) {
        out_pos += ump_opt::serialize(events[i], output_buffer.data() + out_pos);
    }
    return out_pos;
}

// =============================================================================
// Verification Functions
// =============================================================================

// Expected output after processing midi_input with velocity scaling (3/4)
// Note On: vel scaled, Note Off: unchanged, CC: unchanged
static const uint8_t expected_output[] = {
    0x90, 60, 75,   // Note On C4 (100 * 3/4 = 75)
    0x90, 64, 60,   // Note On E4 (80 * 3/4 = 60)
    0x90, 67, 67,   // Note On G4 (90 * 3/4 = 67)
    0xB0, 1, 64,    // CC1 = 64 (unchanged)
    0x80, 60, 0,    // Note Off C4 (unchanged)
    0x90, 62, 75,   // Note On D4 (100 * 3/4 = 75)
    0xB0, 7, 100,   // CC7 = 100 (unchanged)
    0x80, 64, 0,    // Note Off E4 (unchanged)
    0x80, 67, 0,    // Note Off G4 (unchanged)
    0x80, 62, 0,    // Note Off D4 (unchanged)
};
constexpr size_t EXPECTED_SIZE = sizeof(expected_output);

bool verify_output(const uint8_t* out, size_t size, const char* name) {
    if (size != EXPECTED_SIZE) {
        print("FAIL ");
        print(name);
        print(": size mismatch (");
        print_int(size);
        print(" vs ");
        print_int(EXPECTED_SIZE);
        println(")");
        return false;
    }
    for (size_t i = 0; i < size; ++i) {
        if (out[i] != expected_output[i]) {
            print("FAIL ");
            print(name);
            print(": byte ");
            print_int(i);
            print(" mismatch (");
            print_int(out[i]);
            print(" vs ");
            print_int(expected_output[i]);
            println(")");
            return false;
        }
    }
    print("PASS ");
    println(name);
    return true;
}

// =============================================================================
// Main Entry Point
// =============================================================================

extern "C" [[noreturn]] void _start() {
    println("");
    println("=====================================================");
    println("MIDI Format Benchmark: Cur vs Cur-Opt vs UMP vs UMP-Opt");
    println("=====================================================");
    println("");

    dwt_init();

    // === Verification Phase ===
    println("=== Verification ===");
    bool all_pass = true;

    // Current
    {
        std::array<uint8_t, 64> out{};
        current::Parser parser;
        current::Event events[16];
        size_t event_count = 0;
        for (size_t i = 0; i < MIDI_INPUT_SIZE; ++i) {
            current::Event e;
            if (parser.parse(midi_input[i], i, e)) events[event_count++] = e;
        }
        size_t out_pos = 0;
        for (size_t i = 0; i < event_count; ++i) {
            if (events[i].type == current::EventType::Midi) {
                if (events[i].midi.is_note_on()) {
                    events[i].midi.bytes[2] = (events[i].midi.bytes[2] * 3) / 4;
                }
                out_pos += current::serialize(events[i], out.data() + out_pos);
            }
        }
        all_pass &= verify_output(out.data(), out_pos, "Current");
    }

    // Current Optimized
    {
        std::array<uint8_t, 64> out{};
        current_opt::Parser parser;
        current_opt::Event events[16];
        size_t event_count = 0;
        for (size_t i = 0; i < MIDI_INPUT_SIZE; ++i) {
            current_opt::Event e;
            if (parser.parse(midi_input[i], i, e)) events[event_count++] = e;
        }
        size_t out_pos = 0;
        for (size_t i = 0; i < event_count; ++i) {
            if (events[i].type == current_opt::EventType::Midi) {
                if (events[i].midi.is_note_on()) {
                    events[i].midi.bytes[2] = (events[i].midi.bytes[2] * 3) / 4;
                }
                out_pos += current_opt::serialize(events[i], out.data() + out_pos);
            }
        }
        all_pass &= verify_output(out.data(), out_pos, "Current-Opt");
    }

    // UMP
    {
        std::array<uint8_t, 64> out{};
        ump::Parser parser;
        ump::Event events[16];
        size_t event_count = 0;
        for (size_t i = 0; i < MIDI_INPUT_SIZE; ++i) {
            ump::Event e;
            if (parser.parse(midi_input[i], i, e)) events[event_count++] = e;
        }
        size_t out_pos = 0;
        for (size_t i = 0; i < event_count; ++i) {
            if (events[i].is_midi1()) {
                if (events[i].is_note_on()) {
                    uint8_t vel = events[i].ump[0] & 0x7F;
                    vel = (vel * 3) / 4;
                    events[i].ump[0] = (events[i].ump[0] & ~0x7Fu) | vel;
                }
                out_pos += ump::serialize(events[i], out.data() + out_pos);
            }
        }
        all_pass &= verify_output(out.data(), out_pos, "UMP");
    }

    // UMP Optimized
    {
        std::array<uint8_t, 64> out{};
        ump_opt::Parser parser;
        ump_opt::Event events[16];
        size_t event_count = 0;
        for (size_t i = 0; i < MIDI_INPUT_SIZE; ++i) {
            ump_opt::Event e;
            if (parser.parse(midi_input[i], i, e)) events[event_count++] = e;
        }
        size_t out_pos = 0;
        for (size_t i = 0; i < event_count; ++i) {
            if (events[i].is_midi1()) {
                if (events[i].is_note_on()) {
                    uint8_t vel = events[i].ump[0] & 0x7F;
                    vel = (vel * 3) / 4;
                    events[i].ump[0] = (events[i].ump[0] & ~0x7Fu) | vel;
                }
                out_pos += ump_opt::serialize(events[i], out.data() + out_pos);
            }
        }
        all_pass &= verify_output(out.data(), out_pos, "UMP-Opt");
    }

    if (!all_pass) {
        println("");
        println("VERIFICATION FAILED - aborting benchmark");
        while (true) { asm volatile("wfi"); }
    }

    println("");
    println("=== Size Comparison ===");
    print("Current:     ");
    print_int(sizeof(current::Event));
    println(" bytes");
    print("Current-Opt: ");
    print_int(sizeof(current_opt::Event));
    println(" bytes");
    print("UMP-Opt:     ");
    print_int(sizeof(ump_opt::Event));
    println(" bytes");
    println("");

    // Prepare events for phase benchmarks
    current::Event cur_events[16];
    current_opt::Event cur_opt_events[16];
    ump_opt::Event ump_opt_events[16];
    {
        current::Parser p1;
        current_opt::Parser p2;
        ump_opt::Parser p3;
        size_t c1 = 0, c2 = 0, c3 = 0;
        for (size_t i = 0; i < MIDI_INPUT_SIZE; ++i) {
            current::Event e1;
            if (p1.parse(midi_input[i], i, e1)) cur_events[c1++] = e1;
            current_opt::Event e2;
            if (p2.parse(midi_input[i], i, e2)) cur_opt_events[c2++] = e2;
            ump_opt::Event e3;
            if (p3.parse(midi_input[i], i, e3)) ump_opt_events[c3++] = e3;
        }
    }
    constexpr size_t EVENT_COUNT = 10;

    // Warmup
    for (size_t i = 0; i < 10; ++i) {
        bench_current();
        bench_current_opt();
        bench_ump_opt();
    }

    // === Memory Access Pattern Benchmark ===
    // 多数のイベントをメモリに保持し、ランダムアクセス風に処理
    println("=== Memory Pattern Benchmark ===");

    constexpr size_t LARGE_COUNT = 128;
    static current_opt::Event cur_large[LARGE_COUNT];
    static ump_opt::Event ump_large[LARGE_COUNT];

    // Fill with Note On/Off pattern
    for (size_t i = 0; i < LARGE_COUNT; ++i) {
        cur_large[i].port_id = 0;
        cur_large[i].sample_pos = i;
        cur_large[i].type = current_opt::EventType::Midi;
        cur_large[i].midi.bytes[0] = (i % 2) ? 0x80 : 0x90;
        cur_large[i].midi.bytes[1] = 60 + (i % 12);
        cur_large[i].midi.bytes[2] = 100;
        cur_large[i].midi.size = 3;

        ump_large[i].sample_pos = i;
        ump_large[i].ump[0] = 0x20000000u
            | (((i % 2) ? 0x80u : 0x90u) << 16)
            | ((60 + (i % 12)) << 8)
            | 100;
        ump_large[i].ump[1] = 0;
    }

    // Benchmark: iterate and count note-ons with velocity sum
    volatile uint32_t sum = 0;

    DWT_CYCCNT = 0;
    for (size_t iter = 0; iter < 100; ++iter) {
        uint32_t s = 0;
        for (size_t i = 0; i < LARGE_COUNT; ++i) {
            if (cur_large[i].midi.is_note_on()) {
                s += cur_large[i].midi.velocity();
            }
        }
        sum = s;
    }
    uint32_t mem_cur = DWT_CYCCNT;

    DWT_CYCCNT = 0;
    for (size_t iter = 0; iter < 100; ++iter) {
        uint32_t s = 0;
        for (size_t i = 0; i < LARGE_COUNT; ++i) {
            if (ump_large[i].is_note_on()) {
                s += ump_large[i].velocity();
            }
        }
        sum = s;
    }
    uint32_t mem_ump = DWT_CYCCNT;
    (void)sum;

    print("128 events x100: Cur-O: ");
    print_int(mem_cur);
    print("  UMP-O: ");
    print_int(mem_ump);
    print(" (");
    print_int((mem_ump * 100) / mem_cur);
    println("%)");

    print("Memory footprint: Cur-O: ");
    print_int(LARGE_COUNT * sizeof(current_opt::Event));
    print(" bytes  UMP-O: ");
    print_int(LARGE_COUNT * sizeof(ump_opt::Event));
    println(" bytes");
    println("");

    // === Realistic Synth Workload ===
    // シンセの実際のワークフロー:
    // 1. MIDIクロック24ppq (120BPM = 48msg/sec) - Parse only, no storage
    // 2. Note On → ボイス検索 → 発音 → Note Off検索
    // 3. CC → パラメータ更新
    println("=== Realistic Synth Workload ===");

    // シミュレート: 8ボイスポリ、アクティブノート検索
    constexpr size_t NUM_VOICES = 8;

    struct VoiceCur {
        bool active = false;
        uint8_t note = 0;
        uint8_t velocity = 0;
        uint32_t start_sample = 0;
    };
    struct VoiceUmp {
        bool active = false;
        uint32_t ump = 0;  // note/vel packed
        uint32_t start_sample = 0;
    };

    static VoiceCur voices_cur[NUM_VOICES];
    static VoiceUmp voices_ump[NUM_VOICES];

    // Reset voices
    for (size_t i = 0; i < NUM_VOICES; ++i) {
        voices_cur[i] = {};
        voices_ump[i] = {};
    }

    // Workload: 1000 Note On/Off cycles with voice search
    constexpr size_t NOTE_CYCLES = 1000;
    volatile size_t found = 0;

    // Current format workload
    DWT_CYCCNT = 0;
    for (size_t cycle = 0; cycle < NOTE_CYCLES; ++cycle) {
        uint8_t note = 60 + (cycle % 12);
        uint8_t vel = 100;

        // Note On: find free voice
        for (size_t v = 0; v < NUM_VOICES; ++v) {
            if (!voices_cur[v].active) {
                voices_cur[v].active = true;
                voices_cur[v].note = note;
                voices_cur[v].velocity = vel;
                voices_cur[v].start_sample = cycle;
                found = v;
                break;
            }
        }

        // Note Off: find matching voice
        for (size_t v = 0; v < NUM_VOICES; ++v) {
            if (voices_cur[v].active && voices_cur[v].note == note) {
                voices_cur[v].active = false;
                found = v;
                break;
            }
        }
    }
    uint32_t synth_cur = DWT_CYCCNT;

    // UMP format workload
    DWT_CYCCNT = 0;
    for (size_t cycle = 0; cycle < NOTE_CYCLES; ++cycle) {
        uint8_t note = 60 + (cycle % 12);
        uint8_t vel = 100;
        uint32_t ump_note = (note << 8) | vel;

        // Note On: find free voice
        for (size_t v = 0; v < NUM_VOICES; ++v) {
            if (!voices_ump[v].active) {
                voices_ump[v].active = true;
                voices_ump[v].ump = ump_note;
                voices_ump[v].start_sample = cycle;
                found = v;
                break;
            }
        }

        // Note Off: find matching voice (compare note field)
        uint32_t note_mask = note << 8;
        for (size_t v = 0; v < NUM_VOICES; ++v) {
            if (voices_ump[v].active && (voices_ump[v].ump & 0xFF00) == note_mask) {
                voices_ump[v].active = false;
                found = v;
                break;
            }
        }
    }
    uint32_t synth_ump = DWT_CYCCNT;
    (void)found;

    print("Voice search (1000 cycles): Cur-O: ");
    print_int(synth_cur);
    print("  UMP-O: ");
    print_int(synth_ump);
    print(" (");
    print_int((synth_ump * 100) / synth_cur);
    println("%)");

    // === MIDI Clock Processing ===
    // リアルタイムメッセージは1バイト、Parseのみ
    println("");
    println("=== MIDI Clock (1-byte parse) ===");

    constexpr size_t CLOCK_COUNT = 1000;
    volatile uint32_t clock_count = 0;

    // Current: 1-byte message handling
    DWT_CYCCNT = 0;
    for (size_t i = 0; i < CLOCK_COUNT; ++i) {
        uint8_t byte = 0xF8;  // MIDI Clock
        if (byte >= 0xF8) {
            clock_count = clock_count + 1;  // System realtime - instant
        }
    }
    uint32_t clock_cur = DWT_CYCCNT;

    // UMP: 1-byte to UMP conversion
    DWT_CYCCNT = 0;
    for (size_t i = 0; i < CLOCK_COUNT; ++i) {
        uint8_t byte = 0xF8;
        if (byte >= 0xF8) {
            // UMP MT=0 (Utility) or MT=1 (System)
            uint32_t ump = 0x10000000u | (uint32_t(byte) << 16);
            if ((ump >> 28) == 0x1) {
                clock_count = clock_count + 1;
            }
        }
    }
    uint32_t clock_ump = DWT_CYCCNT;
    (void)clock_count;

    print("1000 clocks: Cur: ");
    print_int(clock_cur);
    print("  UMP: ");
    print_int(clock_ump);
    print(" (");
    print_int((clock_ump * 100) / clock_cur);
    println("%)");
    println("");

    println("=== Benchmark (cycles x100 iterations) ===");

    // === Phase 1: Parse ===
    println("--- Phase 1: Parse ---");

    volatile size_t parse_result = 0;

    DWT_CYCCNT = 0;
    for (size_t i = 0; i < ITERATIONS; ++i) {
        parse_result = bench_current_parse(cur_events);
    }
    uint32_t parse_current = DWT_CYCCNT;

    DWT_CYCCNT = 0;
    for (size_t i = 0; i < ITERATIONS; ++i) {
        parse_result = bench_current_opt_parse(cur_opt_events);
    }
    uint32_t parse_current_opt = DWT_CYCCNT;

    DWT_CYCCNT = 0;
    for (size_t i = 0; i < ITERATIONS; ++i) {
        parse_result = bench_ump_opt_parse(ump_opt_events);
    }
    uint32_t parse_ump_opt = DWT_CYCCNT;
    (void)parse_result;

    print("Cur: ");
    print_int(parse_current);
    print("  Cur-O: ");
    print_int(parse_current_opt);
    print("  UMP-O: ");
    print_int(parse_ump_opt);
    println("");

    // === Phase 2: Dispatch ===
    println("--- Phase 2: Dispatch ---");

    volatile size_t dispatch_result = 0;

    DWT_CYCCNT = 0;
    for (size_t i = 0; i < ITERATIONS; ++i) {
        dispatch_result = bench_current_dispatch(cur_events, EVENT_COUNT);
    }
    uint32_t dispatch_current = DWT_CYCCNT;

    DWT_CYCCNT = 0;
    for (size_t i = 0; i < ITERATIONS; ++i) {
        dispatch_result = bench_current_opt_dispatch(cur_opt_events, EVENT_COUNT);
    }
    uint32_t dispatch_current_opt = DWT_CYCCNT;

    DWT_CYCCNT = 0;
    for (size_t i = 0; i < ITERATIONS; ++i) {
        dispatch_result = bench_ump_opt_dispatch(ump_opt_events, EVENT_COUNT);
    }
    uint32_t dispatch_ump_opt = DWT_CYCCNT;
    (void)dispatch_result;

    print("Cur: ");
    print_int(dispatch_current);
    print("  Cur-O: ");
    print_int(dispatch_current_opt);
    print("  UMP-O: ");
    print_int(dispatch_ump_opt);
    println("");

    // === Phase 3: Serialize ===
    println("--- Phase 3: Serialize ---");

    volatile size_t serial_result = 0;

    DWT_CYCCNT = 0;
    for (size_t i = 0; i < ITERATIONS; ++i) {
        serial_result = bench_current_serialize(cur_events, EVENT_COUNT);
    }
    uint32_t serial_current = DWT_CYCCNT;

    DWT_CYCCNT = 0;
    for (size_t i = 0; i < ITERATIONS; ++i) {
        serial_result = bench_current_opt_serialize(cur_opt_events, EVENT_COUNT);
    }
    uint32_t serial_current_opt = DWT_CYCCNT;

    DWT_CYCCNT = 0;
    for (size_t i = 0; i < ITERATIONS; ++i) {
        serial_result = bench_ump_opt_serialize(ump_opt_events, EVENT_COUNT);
    }
    uint32_t serial_ump_opt = DWT_CYCCNT;
    (void)serial_result;

    print("Cur: ");
    print_int(serial_current);
    print("  Cur-O: ");
    print_int(serial_current_opt);
    print("  UMP-O: ");
    print_int(serial_ump_opt);
    println("");

    // === Full Pipeline ===
    println("--- Full Pipeline ---");

    DWT_CYCCNT = 0;
    for (size_t i = 0; i < ITERATIONS; ++i) {
        (void)bench_current();
    }
    uint32_t cycles_current = DWT_CYCCNT;

    DWT_CYCCNT = 0;
    for (size_t i = 0; i < ITERATIONS; ++i) {
        (void)bench_current_opt();
    }
    uint32_t cycles_current_opt = DWT_CYCCNT;

    DWT_CYCCNT = 0;
    for (size_t i = 0; i < ITERATIONS; ++i) {
        (void)bench_ump_opt();
    }
    uint32_t cycles_ump_opt = DWT_CYCCNT;

    print("Cur: ");
    print_int(cycles_current);
    print("  Cur-O: ");
    print_int(cycles_current_opt);
    print("  UMP-O: ");
    print_int(cycles_ump_opt);
    println("");

    println("");
    println("=== Comparison vs Current (%) ===");
    print("Current-Opt: ");
    print_int((cycles_current_opt * 100) / cycles_current);
    println("%");
    print("UMP-Opt:     ");
    print_int((cycles_ump_opt * 100) / cycles_current);
    println("%");

    // Memory comparison
    println("");
    println("=== Memory Usage (256 events) ===");
    print("Current:     ");
    print_int(256 * sizeof(current::Event));
    println(" bytes");
    print("Current-Opt: ");
    print_int(256 * sizeof(current_opt::Event));
    println(" bytes");
    print("UMP-Opt:     ");
    print_int(256 * sizeof(ump_opt::Event));
    print(" bytes (");
    print_int(100 - (256 * sizeof(ump_opt::Event) * 100) / (256 * sizeof(current::Event)));
    println("% savings)");

    println("");
    println("BENCHMARK_COMPLETE");

    while (true) {
        asm volatile("wfi");
    }
}

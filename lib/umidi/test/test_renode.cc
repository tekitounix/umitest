// =============================================================================
// SPDX-License-Identifier: MIT
// umidi Renode Test
// =============================================================================
// ARM Cortex-M test for umidi library with performance benchmarking
// =============================================================================

#ifdef __clang__
#pragma GCC diagnostic ignored "-Wshorten-64-to-32"
#pragma GCC diagnostic ignored "-Wreturn-type-c-linkage"
#pragma GCC diagnostic ignored "-Wsection"
#endif

#include "../../../port/arm/cortex-m/common/vector_table.hh"
#include <umidi/umidi.hh>

#include <cstdint>
#include <cstddef>

// =============================================================================
// Boot Vector Table (placed in .isr_vector section)
// =============================================================================

extern "C" [[noreturn]] void _start();
extern "C" char _estack;  // Linker symbol (address is what matters)

__attribute__((section(".isr_vector"), used))
void* const boot_vectors[] = {
    reinterpret_cast<void*>(&_estack),  // Initial SP
    reinterpret_cast<void*>(_start),    // Reset handler
};

// =============================================================================
// UART output (provided by syscalls.cc)
// =============================================================================

extern "C" int _write(int, const void*, int);

namespace {

void print(const char* s) {
    while (*s) {
        _write(1, s, 1);
        ++s;
    }
}

void println(const char* s) {
    print(s);
    print("\r\n");
}

void print_int(int val) {
    if (val < 0) {
        print("-");
        val = -val;
    }
    if (val == 0) {
        print("0");
        return;
    }
    char buf[12];
    int i = 0;
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (i > 0) {
        char c[2] = {buf[--i], 0};
        print(c);
    }
}

} // namespace

// =============================================================================
// DWT Cycle Counter (Cortex-M4)
// =============================================================================

namespace cortex_m {

// Data Watchpoint and Trace Unit addresses
constexpr uint32_t DWT_CTRL   = 0xE0001000;
constexpr uint32_t DWT_CYCCNT = 0xE0001004;
constexpr uint32_t DEMCR      = 0xE000EDFC;

inline void enable_cycle_counter() {
    // Enable debug trace
    *reinterpret_cast<volatile uint32_t*>(DEMCR) |= (1 << 24);
    // Reset cycle counter
    *reinterpret_cast<volatile uint32_t*>(DWT_CYCCNT) = 0;
    // Enable cycle counter
    *reinterpret_cast<volatile uint32_t*>(DWT_CTRL) |= 1;
}

inline uint32_t get_cycles() {
    return *reinterpret_cast<volatile uint32_t*>(DWT_CYCCNT);
}

} // namespace cortex_m

// =============================================================================
// Test Framework (minimal, embedded)
// =============================================================================

namespace test {

int tests_run = 0;
int tests_passed = 0;

void run(const char* name, bool result) {
    ++tests_run;
    if (result) {
        ++tests_passed;
        print("[PASS] ");
    } else {
        print("[FAIL] ");
    }
    println(name);
}

void summary() {
    println("");
    println("========================================");
    print("Tests: ");
    print_int(tests_passed);
    print("/");
    print_int(tests_run);
    println(" passed");

    if (tests_passed == tests_run) {
        println("ALL TESTS PASSED");
    } else {
        println("SOME TESTS FAILED");
    }
    println("========================================");
}

} // namespace test

// =============================================================================
// umidi Tests
// =============================================================================

namespace {

using namespace umidi;

void test_ump32() {
    println("\n--- UMP32 Tests ---");

    // Size check
    test::run("UMP32 size == 4", sizeof(UMP32) == 4);

    // Note On
    auto note_on = UMP32::note_on(0, 60, 100);
    test::run("Note On is_note_on", note_on.is_note_on());
    test::run("Note On channel", note_on.channel() == 0);
    test::run("Note On note", note_on.note() == 60);
    test::run("Note On velocity", note_on.velocity() == 100);

    // Note Off
    auto note_off = UMP32::note_off(1, 64, 0);
    test::run("Note Off is_note_off", note_off.is_note_off());

    // CC
    auto cc = UMP32::cc(2, 7, 100);
    test::run("CC is_cc", cc.is_cc());
    test::run("CC controller", cc.cc_number() == 7);

    // Pitch Bend
    auto pb = UMP32::pitch_bend(0, 8192);
    test::run("Pitch Bend is_pitch_bend", pb.is_pitch_bend());
    test::run("Pitch Bend value", pb.pitch_bend_value() == 8192);

    // System messages
    test::run("Timing Clock", UMP32::timing_clock().is_timing_clock());
    test::run("Start", UMP32::start().is_start());
    test::run("Stop", UMP32::stop().is_stop());
}

void test_parser() {
    println("\n--- Parser Tests ---");

    Parser parser;
    UMP32 ump;

    // Note On parse
    bool r1 = !parser.parse(0x90, ump);
    bool r2 = !parser.parse(60, ump);
    bool r3 = parser.parse(100, ump);
    test::run("Parse Note On", r1 && r2 && r3 && ump.is_note_on());

    // Reset and parse CC
    parser.reset();
    r1 = !parser.parse(0xB0, ump);
    r2 = !parser.parse(7, ump);
    r3 = parser.parse(100, ump);
    test::run("Parse CC", r1 && r2 && r3 && ump.is_cc());

    // Realtime message
    bool rt = parser.parse(0xF8, ump);
    test::run("Parse Realtime", rt && ump.is_timing_clock());
}

void test_message_types() {
    println("\n--- Message Types Tests ---");

    using namespace message;

    auto note_on = NoteOn::create(5, 60, 100);
    test::run("NoteOn create valid", note_on.is_valid());
    test::run("NoteOn channel", note_on.channel() == 5);

    auto cc = ControlChange::create(1, 7, 64);
    test::run("ControlChange valid", cc.is_valid());

    auto pb = PitchBend::create(0, 8192);
    test::run("PitchBend valid", pb.is_valid());
    test::run("PitchBend center", pb.signed_value() == 0);
}

void test_protocol() {
    println("\n--- Protocol Tests ---");

    using namespace protocol;

    // 7-bit encoding
    uint8_t input[] = {0xFF, 0x80, 0x00};
    uint8_t encoded[8];
    size_t enc_len = encode_7bit(input, 3, encoded);
    test::run("7-bit encode length", enc_len > 3);

    uint8_t decoded[8];
    size_t dec_len = decode_7bit(encoded, enc_len, decoded);
    test::run("7-bit decode length", dec_len == 3);
    test::run("7-bit roundtrip", decoded[0] == 0xFF && decoded[1] == 0x80);

    // Checksum
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t cs1 = calculate_checksum(data, 4);
    uint8_t cs2 = calculate_checksum(data, 4);
    test::run("Checksum consistent", cs1 == cs2);

    // CRC32
    uint8_t crc_data[] = {0x00, 0x01, 0x02, 0x03};
    uint32_t c1 = crc32(crc_data, 4);
    uint32_t c2 = crc32(crc_data, 4);
    test::run("CRC32 consistent", c1 == c2);
    test::run("CRC32 nonzero", c1 != 0);
}

// =============================================================================
// Performance Benchmarks
// =============================================================================

void benchmark_ump32_creation() {
    println("\n--- UMP32 Creation Benchmark ---");

    constexpr int ITERATIONS = 10000;

    cortex_m::enable_cycle_counter();

    // Note On creation
    uint32_t start = cortex_m::get_cycles();
    volatile uint32_t sum = 0;
    for (int i = 0; i < ITERATIONS; ++i) {
        auto ump = UMP32::note_on(i & 0x0F, i & 0x7F, (i >> 7) & 0x7F);
        sum += ump.raw();
    }
    uint32_t end = cortex_m::get_cycles();
    uint32_t cycles = end - start;

    print("  Note On x");
    print_int(ITERATIONS);
    print(": ");
    print_int(cycles);
    print(" cycles (");
    print_int(cycles / ITERATIONS);
    println(" cycles/op)");

    // Type check benchmark
    start = cortex_m::get_cycles();
    auto ump = UMP32::note_on(0, 60, 100);
    volatile bool b = false;
    for (int i = 0; i < ITERATIONS; ++i) {
        b = ump.is_note_on();
    }
    end = cortex_m::get_cycles();
    cycles = end - start;

    print("  is_note_on x");
    print_int(ITERATIONS);
    print(": ");
    print_int(cycles);
    print(" cycles (");
    print_int(cycles / ITERATIONS);
    println(" cycles/op)");

    (void)sum;
    (void)b;
}

void benchmark_parser() {
    println("\n--- Parser Benchmark ---");

    constexpr int ITERATIONS = 10000;

    cortex_m::enable_cycle_counter();

    // Parse complete Note On message
    uint32_t start = cortex_m::get_cycles();
    volatile uint32_t sum = 0;
    for (int i = 0; i < ITERATIONS; ++i) {
        Parser parser;
        UMP32 ump;
        (void)parser.parse(0x90, ump);
        (void)parser.parse(60, ump);
        (void)parser.parse(100, ump);
        sum += ump.raw();
    }
    uint32_t end = cortex_m::get_cycles();
    uint32_t cycles = end - start;

    print("  Parse Note On x");
    print_int(ITERATIONS);
    print(": ");
    print_int(cycles);
    print(" cycles (");
    print_int(cycles / ITERATIONS);
    println(" cycles/op)");

    (void)sum;
}

void benchmark_encoding() {
    println("\n--- 7-bit Encoding Benchmark ---");

    constexpr int ITERATIONS = 10000;

    cortex_m::enable_cycle_counter();

    uint8_t input[8] = {0xFF, 0x80, 0x7F, 0x40, 0x20, 0x10, 0x08, 0x04};
    uint8_t output[16];

    uint32_t start = cortex_m::get_cycles();
    volatile size_t total = 0;
    for (int i = 0; i < ITERATIONS; ++i) {
        total += protocol::encode_7bit(input, 8, output);
    }
    uint32_t end = cortex_m::get_cycles();
    uint32_t cycles = end - start;

    print("  encode_7bit(8 bytes) x");
    print_int(ITERATIONS);
    print(": ");
    print_int(cycles);
    print(" cycles (");
    print_int(cycles / ITERATIONS);
    println(" cycles/op)");

    (void)total;
}

} // namespace

// =============================================================================
// Entry Point (_start is required by linker script)
// =============================================================================

extern "C" [[noreturn]] void _start() {
    println("========================================");
    println("  umidi Renode Test Suite");
    println("========================================");

    // Run tests
    test_ump32();
    test_parser();
    test_message_types();
    test_protocol();

    // Print summary
    test::summary();

    // Run benchmarks
    println("\n========================================");
    println("  Performance Benchmarks");
    println("========================================");

    benchmark_ump32_creation();
    benchmark_parser();
    benchmark_encoding();

    println("\n========================================");
    println("  umidi Renode Test Complete");
    println("========================================");

    // Halt (infinite loop)
    while (1) {
        asm volatile ("wfi");
    }
}

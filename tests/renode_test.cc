// =============================================================================
// UMI-OS Renode Test (Minimal)
// =============================================================================
// Basic test to verify ARM build and Renode emulation works.
// Uses vector table from port layer.
// Includes span vs raw pointer benchmark.
// =============================================================================

#ifdef __clang__
#pragma GCC diagnostic ignored "-Wshorten-64-to-32"
#pragma GCC diagnostic ignored "-Wreturn-type-c-linkage"
#pragma GCC diagnostic ignored "-Wsection"
#endif

#include "../port/arm/cortex-m/common/vector_table.hh"
#include <umidi/umidi.hh>

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <span>
#include <array>

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
// Test Framework (minimal)
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
// Basic Tests
// =============================================================================

void test_basic_arithmetic() {
    test::run("1 + 1 == 2", 1 + 1 == 2);
    test::run("10 * 10 == 100", 10 * 10 == 100);
    test::run("100 / 4 == 25", 100 / 4 == 25);
}

void test_float_operations() {
    volatile float a = 3.14159f;
    volatile float b = 2.71828f;
    volatile float sum = a + b;
    volatile float prod = a * b;

    // Check FPU works (approximate checks)
    test::run("float addition works", sum > 5.8f && sum < 5.9f);
    test::run("float multiplication works", prod > 8.5f && prod < 8.6f);
}

void test_memory_operations() {
    volatile uint32_t arr[4] = {0};
    arr[0] = 0xDEADBEEF;
    arr[1] = 0xCAFEBABE;
    arr[2] = 0x12345678;
    arr[3] = 0x87654321;

    test::run("memory write/read [0]", arr[0] == 0xDEADBEEF);
    test::run("memory write/read [1]", arr[1] == 0xCAFEBABE);
    test::run("memory write/read [2]", arr[2] == 0x12345678);
    test::run("memory write/read [3]", arr[3] == 0x87654321);
}

void test_stack_operations() {
    volatile int local1 = 42;
    volatile int local2 = 100;
    volatile int local3 = local1 + local2;

    test::run("stack local variables", local3 == 142);
}

// =============================================================================
// Span vs Raw Pointer Benchmark
// =============================================================================

// DWT Cycle Counter (Cortex-M3/M4/M7)
#define DWT_CTRL   (*(volatile uint32_t*)0xE0001000)
#define DWT_CYCCNT (*(volatile uint32_t*)0xE0001004)
#define SCB_DEMCR  (*(volatile uint32_t*)0xE000EDFC)

constexpr size_t BENCH_BUFFER_SIZE = 64;  // Smaller for embedded
constexpr size_t BENCH_NUM_CHANNELS = 2;
constexpr size_t BENCH_ITERATIONS = 100;

// Raw pointer version
struct AudioContextRaw {
    const float* const* inputs;
    float* const* outputs;
    size_t num_inputs;
    size_t num_outputs;
    uint32_t buffer_size;

    const float* input(size_t ch) const {
        return ch < num_inputs ? inputs[ch] : nullptr;
    }
    float* output(size_t ch) const {
        return ch < num_outputs ? outputs[ch] : nullptr;
    }
};

// std::span version
struct AudioContextSpan {
    std::span<const float* const> inputs;
    std::span<float* const> outputs;
    uint32_t buffer_size;

    const float* input(size_t ch) const {
        return ch < inputs.size() ? inputs[ch] : nullptr;
    }
    float* output(size_t ch) const {
        return ch < outputs.size() ? outputs[ch] : nullptr;
    }
};

__attribute__((noinline))
void process_raw(AudioContextRaw& ctx, float gain) {
    for (size_t ch = 0; ch < ctx.num_outputs; ++ch) {
        const float* in = ctx.input(ch);
        float* out = ctx.output(ch);
        if (!in || !out) continue;
        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            out[i] = in[i] * gain;
        }
    }
}

__attribute__((noinline))
void process_span(AudioContextSpan& ctx, float gain) {
    for (size_t ch = 0; ch < ctx.outputs.size(); ++ch) {
        const float* in = ctx.input(ch);
        float* out = ctx.output(ch);
        if (!in || !out) continue;
        for (uint32_t i = 0; i < ctx.buffer_size; ++i) {
            out[i] = in[i] * gain;
        }
    }
}

static void print_hex(uint32_t val) {
    print("0x");
    const char* hex = "0123456789ABCDEF";
    for (int i = 7; i >= 0; --i) {
        char c[2] = {hex[(val >> (i * 4)) & 0xF], 0};
        print(c);
    }
}

void benchmark_span_vs_raw() {
    println("--- Span vs Raw Pointer Benchmark ---");

    // Enable DWT cycle counter
    SCB_DEMCR |= (1 << 24);
    DWT_CYCCNT = 0;
    DWT_CTRL |= 1;

    // Setup buffers
    static std::array<float, BENCH_BUFFER_SIZE> in_buf0, in_buf1;
    static std::array<float, BENCH_BUFFER_SIZE> out_buf0, out_buf1;

    for (size_t i = 0; i < BENCH_BUFFER_SIZE; ++i) {
        in_buf0[i] = static_cast<float>(i) / BENCH_BUFFER_SIZE;
        in_buf1[i] = static_cast<float>(i) / BENCH_BUFFER_SIZE;
    }

    const float* inputs_arr[] = {in_buf0.data(), in_buf1.data()};
    float* outputs_arr[] = {out_buf0.data(), out_buf1.data()};

    AudioContextRaw ctx_raw{
        .inputs = inputs_arr,
        .outputs = outputs_arr,
        .num_inputs = BENCH_NUM_CHANNELS,
        .num_outputs = BENCH_NUM_CHANNELS,
        .buffer_size = BENCH_BUFFER_SIZE,
    };

    AudioContextSpan ctx_span{
        .inputs = std::span<const float* const>(inputs_arr),
        .outputs = std::span<float* const>(outputs_arr),
        .buffer_size = BENCH_BUFFER_SIZE,
    };

    float gain = 0.5f;

    // Warmup
    for (size_t i = 0; i < 5; ++i) {
        process_raw(ctx_raw, gain);
        process_span(ctx_span, gain);
    }

    // Benchmark raw pointer
    DWT_CYCCNT = 0;
    for (size_t i = 0; i < BENCH_ITERATIONS; ++i) {
        process_raw(ctx_raw, gain);
    }
    uint32_t raw_cycles = DWT_CYCCNT;

    // Benchmark span
    DWT_CYCCNT = 0;
    for (size_t i = 0; i < BENCH_ITERATIONS; ++i) {
        process_span(ctx_span, gain);
    }
    uint32_t span_cycles = DWT_CYCCNT;

    // Results
    print("Raw pointer cycles: ");
    print_hex(raw_cycles);
    println("");

    print("std::span cycles:   ");
    print_hex(span_cycles);
    println("");

    print("Ratio (span/raw * 100): ");
    print_int((span_cycles * 100) / raw_cycles);
    println("%");

    // Pass/fail based on ratio
    bool acceptable = (span_cycles <= raw_cycles * 120 / 100);  // Allow 20% overhead
    test::run("span overhead < 20%", acceptable);
}

// =============================================================================
// UMIDI Tests
// =============================================================================

void test_umidi_parser() {
    println("--- UMIDI Parser ---");

    umidi::Parser parser;
    umidi::UMP32 ump;

    // Test Note On: 0x90 0x3C 0x7F (channel 0, middle C, velocity 127)
    bool result = parser.parse(0x90, ump);
    test::run("parser: status byte returns false", !result);

    result = parser.parse(0x3C, ump);
    test::run("parser: first data byte returns false", !result);

    result = parser.parse(0x7F, ump);
    test::run("parser: second data byte returns true", result);

    // Check UMP format: MT=2, status=0x90, data1=0x3C, data2=0x7F
    test::run("parser: UMP32 mt == 2", ump.mt() == 2);
    test::run("parser: UMP32 status == 0x90", ump.status() == 0x90);
    test::run("parser: UMP32 data1 == 0x3C", ump.data1() == 0x3C);
    test::run("parser: UMP32 data2 == 0x7F", ump.data2() == 0x7F);

    // Test real-time message (single byte)
    result = parser.parse(0xF8, ump);  // Timing Clock
    test::run("parser: real-time returns true", result);
    test::run("parser: real-time mt == 1", ump.mt() == 1);
}

void test_umidi_template_decoder() {
    println("--- UMIDI Template Decoder ---");

    // Test minimal synth decoder (NoteOn/NoteOff only)
    umidi::codec::SynthDecoder decoder;
    umidi::UMP32 ump;

    // Check compile-time support
    test::run("SynthDecoder: supports NoteOn",
              umidi::codec::SynthDecoder::is_supported<umidi::message::NoteOn>());
    test::run("SynthDecoder: supports NoteOff",
              umidi::codec::SynthDecoder::is_supported<umidi::message::NoteOff>());
    test::run("SynthDecoder: !supports ControlChange",
              !umidi::codec::SynthDecoder::is_supported<umidi::message::ControlChange>());

    // Decode Note On
    auto r1 = decoder.decode_byte(0x90, ump);
    test::run("decoder: status Ok(false)", r1.has_value() && !r1.value());

    auto r2 = decoder.decode_byte(0x3C, ump);
    test::run("decoder: data1 Ok(false)", r2.has_value() && !r2.value());

    auto r3 = decoder.decode_byte(0x7F, ump);
    test::run("decoder: data2 Ok(true)", r3.has_value() && r3.value());
    test::run("decoder: UMP32 correct", ump.status() == 0x90 && ump.data1() == 0x3C);
}

void test_umidi_sysex() {
    println("--- UMIDI SysEx ---");

    // Test SysEx7 creation
    uint8_t data[] = {0x7E, 0x00, 0x06, 0x01};  // Identity Request
    auto sysex = umidi::message::SysEx7::create_complete(0, data, 4);

    test::run("SysEx7: is_valid", sysex.is_valid());
    test::run("SysEx7: num_bytes == 4", sysex.num_bytes() == 4);
    test::run("SysEx7: data_at(0) == 0x7E", sysex.data_at(0) == 0x7E);
    test::run("SysEx7: data_at(3) == 0x01", sysex.data_at(3) == 0x01);
    test::run("SysEx7: status == COMPLETE",
              sysex.sysex_status() == umidi::message::SysEx7::Status::COMPLETE);

    // Test SysExParser
    umidi::message::SysExParser<64> parser;

    auto r = parser.parse(0xF0, 0);  // SysEx Start
    test::run("SysExParser: F0 not complete", !r.complete);
    test::run("SysExParser: in_sysex", parser.in_sysex());

    (void)parser.parse(0x7E, 0);
    (void)parser.parse(0x00, 0);
    (void)parser.parse(0x06, 0);
    (void)parser.parse(0x01, 0);
    r = parser.parse(0xF7, 0);  // SysEx End

    test::run("SysExParser: F7 complete", r.complete);
    test::run("SysExParser: packet has 4 bytes", r.packet.num_bytes() == 4);
}

void test_umidi_jr_timestamp() {
    println("--- UMIDI JR Timestamp ---");

    // Create JR Timestamp (1000 microseconds = 31.25 ticks, truncated to 31)
    auto ts = umidi::message::JRTimestamp::from_microseconds(1000, 0);

    test::run("JRTimestamp: is_valid", ts.is_valid());
    test::run("JRTimestamp: timestamp == 31", ts.timestamp() == 31);
    test::run("JRTimestamp: group == 0", ts.group() == 0);

    // Test tracker
    umidi::message::JRTimestampTracker tracker;
    tracker.set_sample_rate(48000);

    test::run("JRTracker: no timestamp initially", !tracker.has_timestamp());

    tracker.process(ts);
    test::run("JRTracker: has timestamp after process", tracker.has_timestamp());

    uint32_t offset = tracker.get_sample_offset();
    // 31 ticks * 32us = 992us, 992us * 48000 / 1000000 ≈ 47.6 samples
    test::run("JRTracker: sample offset reasonable", offset > 40 && offset < 60);
}

void test_umidi_rpn_nrpn() {
    println("--- UMIDI RPN/NRPN ---");

    umidi::cc::ParameterNumberDecoder decoder;

    // Send RPN for Pitch Bend Sensitivity (RPN 0x0000)
    // CC101 (RPN MSB) = 0
    auto r1 = decoder.decode(0, 101, 0);
    test::run("RPN: CC101 not complete", !r1.complete);

    // CC100 (RPN LSB) = 0
    auto r2 = decoder.decode(0, 100, 0);
    test::run("RPN: CC100 not complete", !r2.complete);

    // CC6 (Data Entry MSB) = 12 (12 semitones)
    auto r3 = decoder.decode(0, 6, 12);
    test::run("RPN: CC6 complete", r3.complete);
    test::run("RPN: parameter_number == 0", r3.parameter_number == 0);
    test::run("RPN: is_nrpn == false", !r3.is_nrpn);
    test::run("RPN: value MSB == 12", (r3.value >> 7) == 12);

    // Parse pitch bend sensitivity
    auto pbs = umidi::cc::pitch_bend_sensitivity::parse(r3.value);
    test::run("PBS: semitones == 12", pbs.semitones == 12);
    test::run("PBS: cents == 0", pbs.cents == 0);
}

// =============================================================================
// Main Entry Point
// =============================================================================

extern "C" [[noreturn]] void _start() {
    println("");
    println("========================================");
    println("UMI-OS Renode Test Suite");
    println("========================================");
    println("");

    // Run tests
    println("--- Basic Arithmetic ---");
    test_basic_arithmetic();

    println("");
    println("--- Float Operations ---");
    test_float_operations();

    println("");
    println("--- Memory Operations ---");
    test_memory_operations();

    println("");
    println("--- Stack Operations ---");
    test_stack_operations();

    // span vs raw benchmark - see ARM assembly comparison below
    // Note: Both produce identical assembly with -Os optimization
    println("");
    benchmark_span_vs_raw();

    // UMIDI tests
    println("");
    test_umidi_parser();

    println("");
    test_umidi_template_decoder();

    println("");
    test_umidi_sysex();

    println("");
    test_umidi_jr_timestamp();

    println("");
    test_umidi_rpn_nrpn();

    // Summary
    test::summary();

    // Halt
    while (true) {
        asm volatile("wfi");
    }
}

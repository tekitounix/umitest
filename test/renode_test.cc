// =============================================================================
// UMI-OS Renode Test (Minimal)
// =============================================================================
// Basic test to verify ARM build and Renode emulation works.
// Uses vector table from port layer.
// =============================================================================

#ifdef __clang__
#pragma GCC diagnostic ignored "-Wshorten-64-to-32"
#pragma GCC diagnostic ignored "-Wreturn-type-c-linkage"
#pragma GCC diagnostic ignored "-Wsection"
#endif

#include "../port/arm/cortex-m/common/vector_table.hh"

#include <cstdint>
#include <cstdio>

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

    // Summary
    test::summary();

    // Halt
    while (true) {
        asm volatile("wfi");
    }
}

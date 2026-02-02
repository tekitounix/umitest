#include <cassert>
#include <print>
#include <csignal>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <array>

#include <mmio/mmio.hh>

namespace {
// Mock DirectTransport for testing
std::array<std::uint32_t, 1024> mock_memory{};

template <typename CheckPolicy = std::true_type>
class TestDirectTransport : private mm::RegOps<TestDirectTransport<CheckPolicy>, CheckPolicy> {
    friend class mm::RegOps<TestDirectTransport<CheckPolicy>, CheckPolicy>;
public:
    using mm::RegOps<TestDirectTransport<CheckPolicy>, CheckPolicy>::write;
    using mm::RegOps<TestDirectTransport<CheckPolicy>, CheckPolicy>::read;
    using mm::RegOps<TestDirectTransport<CheckPolicy>, CheckPolicy>::modify;
    using mm::RegOps<TestDirectTransport<CheckPolicy>, CheckPolicy>::is;
    using mm::RegOps<TestDirectTransport<CheckPolicy>, CheckPolicy>::flip;
    using TransportTag = mm::DirectTransportTag;
    
    template <typename Reg>
    [[nodiscard]] auto reg_read(Reg /*reg*/) const noexcept -> typename Reg::RegValueType {
        using T = typename Reg::RegValueType;
        return *reinterpret_cast<const T*>(&mock_memory[(Reg::address - 0x1000) / 4]);
    }
    
    template <typename Reg>
    void reg_write(Reg /*reg*/, typename Reg::RegValueType value) const noexcept {
        using T = typename Reg::RegValueType;
        *reinterpret_cast<T*>(&mock_memory[(Reg::address - 0x1000) / 4]) = value;
    }
};

// Global transport instance for tests with checks enabled
TestDirectTransport<> transport;
// Transport without checks for no-assert tests
TestDirectTransport<std::false_type> transport_no_check;

/// @brief Test device with assert checking enabled (default)
struct AssertDev : mm::Device<> {
    struct Reg8 : mm::Register<AssertDev, 0x1000, 8> {
        struct F4 : mm::Field<Reg8, 0, 4> {};  ///< 4-bit field (max 15)
        struct F2 : mm::Field<Reg8, 4, 2> {};  ///< 2-bit field (max 3)
    };
    struct Reg16 : mm::Register<AssertDev, 0x1010, 16> {
        struct F8 : mm::Field<Reg16, 0, 8> {};  ///< 8-bit field (max 255)
        struct F5 : mm::Field<Reg16, 8, 5> {};  ///< 5-bit field (max 31)
    };
    struct Reg32 : mm::Register<AssertDev, 0x1020, 32> {
        struct F16 : mm::Field<Reg32, 0, 16> {}; ///< 16-bit field (max 65535)
    };
};

/// @brief Test device with assert checking disabled
struct NoAssertDev : mm::Device<mm::RW> {
    struct Reg8 : mm::Register<NoAssertDev, 0x2000, 8> {
        struct F4 : mm::Field<Reg8, 0, 4> {};
    };
};

namespace {
/// @brief Global flag to track if we're expecting an assert
bool expecting_assert = false;
bool assert_triggered = false;

/// @brief Custom SIGABRT handler for testing assertions
void test_sigabrt_handler(int sig) {
    (void)sig; ///< Unused parameter
    if (expecting_assert) {
        assert_triggered = true;
        /// Exit with success when assert is expected
        std::exit(0);
    } else {
        std::println(stderr, "Unexpected assertion failure!");
        std::exit(1);
    }
}

/// @brief Test that a function triggers an assertion
/// @tparam Func Function type
/// @param func Function to test
/// @param description Test description
template<typename Func>
void expect_assert(Func&& func, const char* description) {
    expecting_assert = true;
    assert_triggered = false;
    pid_t pid = fork();
    if (pid == 0) {
        /// Child process
        std::signal(SIGABRT, test_sigabrt_handler);
        func();
        /// If we reach here, assert didn't trigger
        std::exit(1);
    } else {
        /// Parent process
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            std::println("✓ Assert triggered as expected: {}", description);
        } else {
            std::println(stderr, "✗ Assert NOT triggered: {}", description);
            std::exit(1);
        }
    }
    expecting_assert = false;
}

/// @brief Test that a function does not trigger an assertion
/// @tparam Func Function type  
/// @param func Function to test
/// @param description Test description
template<typename Func>
void expect_no_assert(Func&& func, const char* description) {
    expecting_assert = false;
    pid_t pid = fork();
    if (pid == 0) {
        /// Child process
        std::signal(SIGABRT, test_sigabrt_handler);
        func();
        /// If we reach here, no assert triggered - good
        std::exit(0);
    } else {
        /// Parent process
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            std::println("✓ No assert (as expected): {}", description);
        } else {
            std::println(stderr, "✗ Unexpected assert: {}", description);
            std::exit(1);
        }
    }
}

/// @brief Test write() assertion checks
void test_write_asserts() {
    std::println();
    std::println("=== Testing write() asserts ===");
    
    /// Register write - out of range
    expect_assert([]{ transport.write(AssertDev::Reg8::value(256)); }, 
                  "transport.write(AssertDev::Reg8::value(256))");
    expect_no_assert([]{ transport.write(AssertDev::Reg8::value(255)); }, 
                     "transport.write(AssertDev::Reg8::value(255))");
    
    /// Field write with value() - out of range
    expect_assert([]{ transport.write(AssertDev::Reg8::F4::value(16)); }, 
                  "transport.write(AssertDev::Reg8::F4::value(16))");
    expect_no_assert([]{ transport.write(AssertDev::Reg8::F4::value(15)); }, 
                     "transport.write(AssertDev::Reg8::F4::value(15))");
    
    /// Test 8-bit field write
    expect_assert([]{ transport.write(AssertDev::Reg16::F8::value(256)); }, 
                  "transport.write(AssertDev::Reg16::F8::value(256))");
    expect_no_assert([]{ transport.write(AssertDev::Reg16::F8::value(255)); }, 
                     "transport.write(AssertDev::Reg16::F8::value(255))");
}

/// @brief Test modify() assertion checks
void test_modify_asserts() {
    std::println();
    std::println("=== Testing modify() asserts ===");
    
    /// Single field modify - out of range
    expect_assert([]{ transport.modify(AssertDev::Reg8::F4::value(16)); },
                  "transport.modify(AssertDev::Reg8::F4::value(16))");
    expect_no_assert([]{ transport.modify(AssertDev::Reg8::F4::value(15)); },
                     "transport.modify(AssertDev::Reg8::F4::value(15))");
    
    expect_assert([]{ transport.modify(AssertDev::Reg8::F2::value(4)); },
                  "transport.modify(AssertDev::Reg8::F2::value(4))");
    expect_no_assert([]{ transport.modify(AssertDev::Reg8::F2::value(3)); },
                     "transport.modify(AssertDev::Reg8::F2::value(3))");
    
    /// Multiple field modify with value() - out of range
    expect_assert([]{ transport.modify(AssertDev::Reg8::F4::value(16)); },
                  "transport.modify(AssertDev::Reg8::F4::value(16))");
    expect_no_assert([]{ transport.modify(AssertDev::Reg8::F4::value(15)); },
                     "transport.modify(AssertDev::Reg8::F4::value(15))");
    
    expect_assert([]{ transport.modify(AssertDev::Reg16::F5::value(32)); },
                  "transport.modify(AssertDev::Reg16::F5::value(32))");
    expect_no_assert([]{ transport.modify(AssertDev::Reg16::F5::value(31)); },
                     "transport.modify(AssertDev::Reg16::F5::value(31))");
}

/// @brief Test is() assertion checks
void test_is_asserts() {
    std::println();
    std::println("=== Testing is() asserts ===");
    
    /// Register comparison - out of range
    expect_assert([]{ (void)transport.is(AssertDev::Reg8::value(256)); },
                  "transport.is(AssertDev::Reg8::value(256))");
    expect_no_assert([]{ (void)transport.is(AssertDev::Reg8::value(255)); },
                     "transport.is(AssertDev::Reg8::value(255))");
    
    /// Field comparison - out of range
    expect_assert([]{ (void)transport.is(AssertDev::Reg8::F4::value(16)); },
                  "transport.is(AssertDev::Reg8::F4::value(16))");
    expect_no_assert([]{ (void)transport.is(AssertDev::Reg8::F4::value(15)); },
                     "transport.is(AssertDev::Reg8::F4::value(15))");
    
    /// Dynamic value comparison - out of range
    expect_assert([]{ (void)transport.is(AssertDev::Reg16::F8::value(256)); },
                  "transport.is(AssertDev::Reg16::F8::value(256))");
    expect_no_assert([]{ (void)transport.is(AssertDev::Reg16::F8::value(255)); },
                     "transport.is(AssertDev::Reg16::F8::value(255))");
}

/// @brief Test CheckPolicy=false_type disables assertions
void test_no_assert_policy() {
    std::println();
    std::println("=== Testing CheckPolicy=false_type (no asserts) ===");
    
    /// These should NOT trigger asserts because CheckPolicy is false_type
    expect_no_assert([]{ transport_no_check.write(NoAssertDev::Reg8::value(256)); },
                     "transport.write(NoAssertDev::Reg8::value(256)) with CheckPolicy=false");
    expect_no_assert([]{ transport_no_check.modify(NoAssertDev::Reg8::F4::value(16)); },
                     "transport.modify(NoAssertDev::Reg8::F4::value(16)) with CheckPolicy=false");
    expect_no_assert([]{ (void)transport_no_check.is(NoAssertDev::Reg8::F4::value(16)); },
                     "transport.is(NoAssertDev::Reg8::F4::value(16)) with CheckPolicy=false");
    
    /// Verify the values are actually masked
    mock_memory.fill(0);
    transport_no_check.write(NoAssertDev::Reg8::value(256));  ///< 256 & 0xFF = 0
    assert(transport_no_check.read(NoAssertDev::Reg8{}) == 0);
    
    transport_no_check.modify(NoAssertDev::Reg8::F4::value(16));  ///< 16 & 0xF = 0
    assert(transport_no_check.read(NoAssertDev::Reg8::F4{}) == 0);
}

/// @brief Test edge cases for assertion boundaries
void test_edge_cases() {
    std::println();
    std::println("=== Testing edge cases ===");
    
    /// Maximum values that should NOT assert
    expect_no_assert([]{ transport.write(AssertDev::Reg8::value(0xFF)); },
                     "transport.write(AssertDev::Reg8::value(0xFF))");
    expect_no_assert([]{ transport.write(AssertDev::Reg16::value(0xFFFF)); },
                     "transport.write(AssertDev::Reg16::value(0xFFFF))");
    expect_no_assert([]{ transport.write(AssertDev::Reg32::value(0xFFFFFFFF)); },
                     "transport.write(AssertDev::Reg32::value(0xFFFFFFFF))");
    
    /// Minimum overflow that should assert
    expect_assert([]{ transport.write(AssertDev::Reg8::value(0x100)); },
                  "transport.write(AssertDev::Reg8::value(0x100))");
    expect_assert([]{ transport.write(AssertDev::Reg16::value(0x10000)); },
                  "transport.write(AssertDev::Reg16::value(0x10000))");
    
    /// Field-specific edge cases
    expect_no_assert([]{ transport.modify(AssertDev::Reg16::F5::value(0b11111)); },
                     "transport.modify(AssertDev::Reg16::F5::value(0b11111))");
    expect_assert([]{ transport.modify(AssertDev::Reg16::F5::value(0b100000)); },
                  "transport.modify(AssertDev::Reg16::F5::value(0b100000))");
}

} // namespace

} // anonymous namespace

int main() {
    std::println("Running MMIO assert tests...");
    std::println("Note: This test uses fork() to safely test assertions.");
    
    test_write_asserts();
    test_modify_asserts();
    test_is_asserts();
    test_no_assert_policy();
    test_edge_cases();
    std::println();
    std::println("All assert tests passed!");
    return 0;
}
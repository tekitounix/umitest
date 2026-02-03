// Force assert() to be active even in release builds
#undef NDEBUG

#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <umi/test/test.hh>

#include "../mmio.hh"

namespace {
// Mock DirectTransport for testing
std::array<std::uint32_t, 2048> mock_memory{};

template <typename CheckPolicy = std::true_type>
class TestDirectTransport : private umi::mmio::RegOps<TestDirectTransport<CheckPolicy>, CheckPolicy> {
    friend class umi::mmio::RegOps<TestDirectTransport<CheckPolicy>, CheckPolicy>;

  public:
    using umi::mmio::RegOps<TestDirectTransport<CheckPolicy>, CheckPolicy>::write;
    using umi::mmio::RegOps<TestDirectTransport<CheckPolicy>, CheckPolicy>::read;
    using umi::mmio::RegOps<TestDirectTransport<CheckPolicy>, CheckPolicy>::modify;
    using umi::mmio::RegOps<TestDirectTransport<CheckPolicy>, CheckPolicy>::is;
    using umi::mmio::RegOps<TestDirectTransport<CheckPolicy>, CheckPolicy>::flip;
    using TransportTag = umi::mmio::DirectTransportTag;

    template <typename Reg>
    [[nodiscard]] auto reg_read(Reg /*reg*/) const noexcept -> typename Reg::RegValueType {
        using T = typename Reg::RegValueType;
        T value{};
        std::memcpy(&value, &mock_memory[(Reg::address - 0x1000) / 4], sizeof(T));
        return value;
    }

    template <typename Reg>
    void reg_write(Reg /*reg*/, typename Reg::RegValueType value) const noexcept {
        using T = typename Reg::RegValueType;
        std::memcpy(&mock_memory[(Reg::address - 0x1000) / 4], &value, sizeof(T));
    }
};

// Global transport instances
TestDirectTransport<> transport;
TestDirectTransport<std::false_type> transport_no_check;

/// Test device with assert checking enabled (default)
struct AssertDev : umi::mmio::Device<> {
    struct Reg8 : umi::mmio::Register<AssertDev, 0x1000, 8> {
        struct F4 : umi::mmio::Field<Reg8, 0, 4> {};
        struct F2 : umi::mmio::Field<Reg8, 4, 2> {};
    };
    struct Reg16 : umi::mmio::Register<AssertDev, 0x1010, 16> {
        struct F8 : umi::mmio::Field<Reg16, 0, 8> {};
        struct F5 : umi::mmio::Field<Reg16, 8, 5> {};
    };
    struct Reg32 : umi::mmio::Register<AssertDev, 0x1020, 32> {
        struct F16 : umi::mmio::Field<Reg32, 0, 16> {};
    };
};

/// Test device with assert checking disabled
struct NoAssertDev : umi::mmio::Device<umi::mmio::RW> {
    struct Reg8 : umi::mmio::Register<NoAssertDev, 0x2000, 8> {
        struct F4 : umi::mmio::Field<Reg8, 0, 4> {};
    };
};

/// Run func in a child process; returns true if child exited with code 0 (assert triggered and caught)
template <typename Func>
bool fork_expect_assert(Func&& func) {
    pid_t pid = fork();
    if (pid == 0) {
        std::signal(SIGABRT, [](int) { std::_Exit(0); });
        func();
        std::_Exit(1); // assert didn't trigger
    }
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/// Run func in a child process; returns true if child exited with code 0 (no assert)
template <typename Func>
bool fork_expect_no_assert(Func&& func) {
    pid_t pid = fork();
    if (pid == 0) {
        std::signal(SIGABRT, [](int) { std::_Exit(1); });
        func();
        std::_Exit(0);
    }
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

} // anonymous namespace

int main() {
    umi::test::Suite s("mmio_assert");

    s.section("write() asserts");
    s.check(fork_expect_assert([] { transport.write(AssertDev::Reg8::value(256)); }),
            "Reg8::value(256) triggers assert");
    s.check(fork_expect_no_assert([] { transport.write(AssertDev::Reg8::value(255)); }), "Reg8::value(255) no assert");
    s.check(fork_expect_assert([] { transport.write(AssertDev::Reg8::F4::value(16)); }),
            "F4::value(16) triggers assert");
    s.check(fork_expect_no_assert([] { transport.write(AssertDev::Reg8::F4::value(15)); }), "F4::value(15) no assert");
    s.check(fork_expect_assert([] { transport.write(AssertDev::Reg16::F8::value(256)); }),
            "F8::value(256) triggers assert");
    s.check(fork_expect_no_assert([] { transport.write(AssertDev::Reg16::F8::value(255)); }),
            "F8::value(255) no assert");

    s.section("modify() asserts");
    s.check(fork_expect_assert([] { transport.modify(AssertDev::Reg8::F4::value(16)); }),
            "modify F4::value(16) triggers assert");
    s.check(fork_expect_no_assert([] { transport.modify(AssertDev::Reg8::F4::value(15)); }),
            "modify F4::value(15) no assert");
    s.check(fork_expect_assert([] { transport.modify(AssertDev::Reg8::F2::value(4)); }),
            "modify F2::value(4) triggers assert");
    s.check(fork_expect_no_assert([] { transport.modify(AssertDev::Reg8::F2::value(3)); }),
            "modify F2::value(3) no assert");
    s.check(fork_expect_assert([] { transport.modify(AssertDev::Reg16::F5::value(32)); }),
            "modify F5::value(32) triggers assert");
    s.check(fork_expect_no_assert([] { transport.modify(AssertDev::Reg16::F5::value(31)); }),
            "modify F5::value(31) no assert");

    s.section("is() asserts");
    s.check(fork_expect_assert([] { (void)transport.is(AssertDev::Reg8::value(256)); }),
            "is Reg8::value(256) triggers assert");
    s.check(fork_expect_no_assert([] { (void)transport.is(AssertDev::Reg8::value(255)); }),
            "is Reg8::value(255) no assert");
    s.check(fork_expect_assert([] { (void)transport.is(AssertDev::Reg8::F4::value(16)); }),
            "is F4::value(16) triggers assert");
    s.check(fork_expect_no_assert([] { (void)transport.is(AssertDev::Reg8::F4::value(15)); }),
            "is F4::value(15) no assert");
    s.check(fork_expect_assert([] { (void)transport.is(AssertDev::Reg16::F8::value(256)); }),
            "is F8::value(256) triggers assert");
    s.check(fork_expect_no_assert([] { (void)transport.is(AssertDev::Reg16::F8::value(255)); }),
            "is F8::value(255) no assert");

    s.section("CheckPolicy=false_type (no asserts)");
    s.check(fork_expect_no_assert([] { transport_no_check.write(NoAssertDev::Reg8::value(256)); }),
            "write value(256) with CheckPolicy=false no assert");
    s.check(fork_expect_no_assert([] { transport_no_check.modify(NoAssertDev::Reg8::F4::value(16)); }),
            "modify F4::value(16) with CheckPolicy=false no assert");
    s.check(fork_expect_no_assert([] { (void)transport_no_check.is(NoAssertDev::Reg8::F4::value(16)); }),
            "is F4::value(16) with CheckPolicy=false no assert");

    // Verify masked values
    mock_memory.fill(0);
    transport_no_check.write(NoAssertDev::Reg8::value(256)); // 256 & 0xFF = 0
    s.check_eq(static_cast<int>(transport_no_check.read(NoAssertDev::Reg8{})), 0);
    transport_no_check.modify(NoAssertDev::Reg8::F4::value(16)); // 16 & 0xF = 0
    s.check_eq(static_cast<int>(transport_no_check.read(NoAssertDev::Reg8::F4{})), 0);

    s.section("edge cases");
    s.check(fork_expect_no_assert([] { transport.write(AssertDev::Reg8::value(0xFF)); }),
            "Reg8::value(0xFF) no assert");
    s.check(fork_expect_no_assert([] { transport.write(AssertDev::Reg16::value(0xFFFF)); }),
            "Reg16::value(0xFFFF) no assert");
    s.check(fork_expect_no_assert([] { transport.write(AssertDev::Reg32::value(0xFFFFFFFF)); }),
            "Reg32::value(0xFFFFFFFF) no assert");
    s.check(fork_expect_assert([] { transport.write(AssertDev::Reg8::value(0x100)); }),
            "Reg8::value(0x100) triggers assert");
    s.check(fork_expect_assert([] { transport.write(AssertDev::Reg16::value(0x10000)); }),
            "Reg16::value(0x10000) triggers assert");
    s.check(fork_expect_no_assert([] { transport.modify(AssertDev::Reg16::F5::value(0b11111)); }),
            "F5::value(0b11111) no assert");
    s.check(fork_expect_assert([] { transport.modify(AssertDev::Reg16::F5::value(0b100000)); }),
            "F5::value(0b100000) triggers assert");

    s.section("signed values");
    s.check(fork_expect_assert([] { transport.write(AssertDev::Reg8::F4::value(-1)); }),
            "F4::value(-1) triggers assert");

    return s.summary();
}

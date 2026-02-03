#include "../mmio.hh"

// This file documents expected compile-time errors.
// It is not compiled in normal test builds.

struct ReadOnlyDevice : umi::mmio::Device<> {
    struct STATUS : umi::mmio::Register<ReadOnlyDevice, 0x00, 8, umi::mmio::RO> {};
};

struct WriteOnlyDevice : umi::mmio::Device<> {
    struct COMMAND : umi::mmio::Register<WriteOnlyDevice, 0x00, 8, umi::mmio::WO> {};
};

struct DevA : umi::mmio::Device<> {
    struct REG0 : umi::mmio::Register<DevA, 0x00, 32> {
        struct FIELD0 : umi::mmio::Field<REG0, 0, 4> {};
    };
    struct REG1 : umi::mmio::Register<DevA, 0x04, 32> {
        struct FIELD1 : umi::mmio::Field<REG1, 0, 4> {};
    };
};

#if 0 // EXPECTED COMPILE ERROR: Cannot write to read-only register
void test_write_to_ro() {
    umi::mmio::DirectTransport<> t;
    t.write(ReadOnlyDevice::STATUS::value(0));
}
#endif

#if 0 // EXPECTED COMPILE ERROR: Cannot read from write-only register
void test_read_from_wo() {
    umi::mmio::DirectTransport<> t;
    auto v = t.read(WriteOnlyDevice::COMMAND{});
    (void)v;
}
#endif

#if 0 // EXPECTED COMPILE ERROR: All values must target the same register
void test_mixed_register_modify() {
    umi::mmio::DirectTransport<> t;
    t.modify(DevA::REG0::FIELD0::value(1), DevA::REG1::FIELD1::value(2));
}
#endif

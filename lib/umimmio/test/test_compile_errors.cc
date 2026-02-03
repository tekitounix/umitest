#include <umimmio.hh>

// This file documents expected compile-time errors.
// It is not compiled in normal test builds.

struct ReadOnlyDevice : mm::Device<> {
    struct STATUS : mm::Register<ReadOnlyDevice, 0x00, 8, mm::RO> {};
};

struct WriteOnlyDevice : mm::Device<> {
    struct COMMAND : mm::Register<WriteOnlyDevice, 0x00, 8, mm::WO> {};
};

struct DevA : mm::Device<> {
    struct REG0 : mm::Register<DevA, 0x00, 32> {
        struct FIELD0 : mm::Field<REG0, 0, 4> {};
    };
    struct REG1 : mm::Register<DevA, 0x04, 32> {
        struct FIELD1 : mm::Field<REG1, 0, 4> {};
    };
};

#if 0  // EXPECTED COMPILE ERROR: Cannot write to read-only register
void test_write_to_ro() {
    mm::DirectTransport<> t;
    t.write(ReadOnlyDevice::STATUS::value(0));
}
#endif

#if 0  // EXPECTED COMPILE ERROR: Cannot read from write-only register
void test_read_from_wo() {
    mm::DirectTransport<> t;
    auto v = t.read(WriteOnlyDevice::COMMAND{});
    (void)v;
}
#endif

#if 0  // EXPECTED COMPILE ERROR: All values must target the same register
void test_mixed_register_modify() {
    mm::DirectTransport<> t;
    t.modify(DevA::REG0::FIELD0::value(1), DevA::REG1::FIELD1::value(2));
}
#endif

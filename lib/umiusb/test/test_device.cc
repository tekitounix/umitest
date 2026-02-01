// SPDX-License-Identifier: MIT
// UMI-USB: Device Core Tests — BOS, vendor request, DeviceBuilder
#include "test_common.hh"
#include "core/device.hh"
#include "midi/usb_midi_class.hh"
#include "stub_hal.hh"

using namespace umiusb;

// A minimal class satisfying the Class concept for testing
struct TestClass {
    bool configured = false;
    bool vendor_handled = false;
    uint8_t bos_data[5] = {5, 0x0F, 5, 0, 1};  // Minimal BOS header
    bool has_bos = true;

    std::span<const uint8_t> config_descriptor() const {
        // Minimal config descriptor (9 bytes)
        static constexpr uint8_t desc[] = {9, 0x02, 9, 0, 0, 1, 0, 0xC0, 50};
        return {desc, sizeof(desc)};
    }

    std::span<const uint8_t> bos_descriptor() const {
        if (has_bos) return {bos_data, sizeof(bos_data)};
        return {};
    }

    bool handle_vendor_request(const SetupPacket& setup, std::span<uint8_t>& response) {
        if (setup.bRequest == 0x42) {
            vendor_handled = true;
            response = response.subspan(0, 0);  // ZLP
            return true;
        }
        return false;
    }

    void on_configured(bool c) { configured = c; }

    bool handle_request(const SetupPacket& /*setup*/, std::span<uint8_t>& /*response*/) {
        return false;
    }

    void on_rx(uint8_t /*ep*/, std::span<const uint8_t> /*data*/) {}

    template<typename HalT>
    void configure_endpoints(HalT& /*hal*/) {}

    template<typename HalT>
    void on_sof(HalT& /*hal*/) {}

    template<typename HalT>
    void on_tx_complete(HalT& /*hal*/, uint8_t /*ep*/) {}
};

// Verify TestClass satisfies Class concept
static_assert(Class<TestClass>, "TestClass must satisfy Class concept");

int main() {
    SECTION("Device handles GetDescriptor Device");
    {
        StubHal hal;
        hal.init();
        TestClass cls;
        DeviceInfo info{.vendor_id = 0x1234, .product_id = 0x5678};
        Device<StubHal, TestClass> dev(hal, cls, info);
        dev.init();

        // Simulate GET_DESCRIPTOR Device
        SetupPacket setup{};
        setup.bmRequestType = 0x80;  // IN, Standard, Device
        setup.bRequest = bRequest::GetDescriptor;
        setup.wValue = (bDescriptorType::Device << 8);
        setup.wLength = 18;
        hal.callbacks.on_setup(hal.callbacks.context, setup);

        CHECK(hal.ep_buf_len[0] > 0, "Device descriptor sent");
        CHECK_EQ(hal.ep_buf[0][0], uint8_t{18}, "bLength = 18");
        CHECK_EQ(hal.ep_buf[0][1], uint8_t{0x01}, "bDescriptorType = Device");
        // Check VID/PID
        uint16_t vid = hal.ep_buf[0][8] | (hal.ep_buf[0][9] << 8);
        uint16_t pid = hal.ep_buf[0][10] | (hal.ep_buf[0][11] << 8);
        CHECK_EQ(vid, uint16_t{0x1234}, "VID correct");
        CHECK_EQ(pid, uint16_t{0x5678}, "PID correct");
    }

    SECTION("Device handles BOS descriptor");
    {
        StubHal hal;
        hal.init();
        TestClass cls;
        DeviceInfo info{.vendor_id = 0x1234, .product_id = 0x5678};
        Device<StubHal, TestClass> dev(hal, cls, info);
        dev.init();

        SetupPacket setup{};
        setup.bmRequestType = 0x80;
        setup.bRequest = bRequest::GetDescriptor;
        setup.wValue = (bDescriptorType::Bos << 8);
        setup.wLength = 64;
        hal.callbacks.on_setup(hal.callbacks.context, setup);

        CHECK(hal.ep_buf_len[0] > 0, "BOS descriptor sent");
        CHECK_EQ(hal.ep_buf[0][0], uint8_t{5}, "BOS bLength");
        CHECK_EQ(hal.ep_buf[0][1], uint8_t{0x0F}, "BOS bDescriptorType");
    }

    SECTION("Device delegates vendor request to class");
    {
        StubHal hal;
        hal.init();
        TestClass cls;
        DeviceInfo info{.vendor_id = 0x1234, .product_id = 0x5678};
        Device<StubHal, TestClass> dev(hal, cls, info);
        dev.init();

        SetupPacket setup{};
        setup.bmRequestType = 0xC0;  // IN, Vendor, Device
        setup.bRequest = 0x42;
        setup.wValue = 0;
        setup.wIndex = 0;
        setup.wLength = 0;
        hal.callbacks.on_setup(hal.callbacks.context, setup);

        CHECK(cls.vendor_handled, "Vendor request delegated to class");
    }

    SECTION("Device STALLs DeviceQualifier for FS-only");
    {
        StubHal hal;
        hal.init();
        TestClass cls;
        DeviceInfo info{.vendor_id = 0x1234, .product_id = 0x5678};
        Device<StubHal, TestClass> dev(hal, cls, info);
        dev.init();

        SetupPacket setup{};
        setup.bmRequestType = 0x80;
        setup.bRequest = bRequest::GetDescriptor;
        setup.wValue = (bDescriptorType::DeviceQualifier << 8);
        setup.wLength = 10;
        hal.ep_stalled[0] = false;
        hal.callbacks.on_setup(hal.callbacks.context, setup);

        CHECK(hal.ep_stalled[0], "DeviceQualifier STALLed for FS-only device");
    }

    SECTION("DeviceBuilder initialization sequence");
    {
        StubHal hal;
        hal.init();
        hal.connect();
        TestClass cls;
        DeviceInfo info{.vendor_id = 0x1234, .product_id = 0x5678};

        // DeviceBuilder should disconnect, then reconnect
        auto dev = DeviceBuilder<StubHal, TestClass>(hal, cls, info).build();
        (void)dev;
        CHECK(hal.is_connected(), "HAL reconnected after build");
    }

    TEST_SUMMARY();
}

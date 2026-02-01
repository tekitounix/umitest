// SPDX-License-Identifier: MIT
// UMI-USB: Descriptor Builder Tests
#include "test_common.hh"
#include "core/descriptor.hh"
#include "stub_hal.hh"

using namespace umiusb;
using namespace umiusb::desc;

// ============================================================================
// Compile-time static_assert tests
// ============================================================================

// Device descriptor must be 18 bytes
static constexpr auto dev_desc = DeviceDesc{
    .usb_version = 0x0201,
    .device_class = 0xEF,
    .device_subclass = 0x02,
    .device_protocol = 0x01,
    .vendor_id = 0x1234,
    .product_id = 0x5678,
}.build();
static_assert(dev_desc.size == 18, "Device descriptor must be 18 bytes");
static_assert(dev_desc[0] == 18, "bLength");
static_assert(dev_desc[1] == dtype::Device, "bDescriptorType");
// bcdUSB = 0x0201 in LE
static_assert(dev_desc[2] == 0x01, "bcdUSB low");
static_assert(dev_desc[3] == 0x02, "bcdUSB high");
// idVendor = 0x1234 in LE
static_assert(dev_desc[8] == 0x34, "idVendor low");
static_assert(dev_desc[9] == 0x12, "idVendor high");
// idProduct = 0x5678 in LE
static_assert(dev_desc[10] == 0x78, "idProduct low");
static_assert(dev_desc[11] == 0x56, "idProduct high");

// Interface descriptor must be 9 bytes
static constexpr auto iface_desc = InterfaceDesc{
    .interface_number = 0,
    .num_endpoints = 2,
    .interface_class = 0x01,
    .interface_subclass = 0x01,
}.build();
static_assert(iface_desc.size == 9, "Interface descriptor must be 9 bytes");
static_assert(iface_desc[2] == 0, "bInterfaceNumber");
static_assert(iface_desc[4] == 2, "bNumEndpoints");

// Endpoint descriptor must be 7 bytes
static constexpr auto ep_desc = EndpointDesc{
    .address = ep::In | 1,
    .attributes = ep::Bulk,
    .max_packet_size = 512,
}.build();
static_assert(ep_desc.size == 7, "Endpoint descriptor must be 7 bytes");
static_assert(ep_desc[2] == 0x81, "bEndpointAddress IN|1");
static_assert(ep_desc[4] == 0x00, "wMaxPacketSize low (512)");
static_assert(ep_desc[5] == 0x02, "wMaxPacketSize high (512)");

// Audio endpoint descriptor must be 9 bytes
static constexpr auto audio_ep = AudioEndpointDesc{
    .address = ep::Out | 1,
    .attributes = ep::Isochronous | ep::Async,
    .max_packet_size = 200,
    .interval = 1,
}.build();
static_assert(audio_ep.size == 9, "Audio endpoint must be 9 bytes");

// IAD descriptor must be 8 bytes
static constexpr auto iad = InterfaceAssociationDesc{
    .first_interface = 0,
    .interface_count = 3,
    .function_class = 0x01,
    .function_subclass = 0x01,
}.build();
static_assert(iad.size == 8, "IAD must be 8 bytes");
static_assert(iad[2] == 0, "bFirstInterface");
static_assert(iad[3] == 3, "bInterfaceCount");

// String descriptor
static constexpr auto str = String("UMI");
static_assert(str.size == 2 + 3 * 2, "String descriptor: header + UTF-16LE");
static_assert(str[0] == 8, "bLength = 8");
static_assert(str[1] == dtype::String, "bDescriptorType");
static_assert(str[2] == 'U', "U");
static_assert(str[3] == 0, "null high byte");

// Bytes concatenation
static constexpr auto a = bytes(1, 2);
static constexpr auto b = bytes(3, 4, 5);
static constexpr auto ab = a + b;
static_assert(ab.size == 5);
static_assert(ab[0] == 1 && ab[4] == 5);

// le16 / le32
static_assert(le16(0x1234)[0] == 0x34 && le16(0x1234)[1] == 0x12);
static_assert(le32(0xDEADBEEF)[0] == 0xEF && le32(0xDEADBEEF)[3] == 0xDE);

// ============================================================================
// Hal concept static_assert
// ============================================================================

static_assert(Hal<StubHal>, "StubHal must satisfy Hal concept");

// ============================================================================
// Runtime tests
// ============================================================================

int main() {
    SECTION("Descriptor byte layout");
    {
        // Device descriptor runtime verification
        CHECK_EQ(dev_desc.data[0], uint8_t{18}, "Device desc bLength");
        CHECK_EQ(dev_desc.data[1], dtype::Device, "Device desc bDescriptorType");
        CHECK_EQ(dev_desc.data[4], uint8_t{0xEF}, "Device desc bDeviceClass");
    }

    SECTION("ConfigHeader");
    {
        constexpr auto cfg = ConfigHeader{.max_power = 250}.build<100, 3>();
        CHECK_EQ(cfg.data[0], uint8_t{9}, "ConfigHeader bLength");
        CHECK_EQ(cfg.data[1], dtype::Configuration, "ConfigHeader type");
        // wTotalLength = 100 in LE
        CHECK_EQ(cfg.data[2], uint8_t{100}, "wTotalLength low");
        CHECK_EQ(cfg.data[3], uint8_t{0}, "wTotalLength high");
        CHECK_EQ(cfg.data[4], uint8_t{3}, "bNumInterfaces");
        CHECK_EQ(cfg.data[8], uint8_t{250}, "bMaxPower");
    }

    SECTION("StubHal basic operations");
    {
        StubHal hal;
        hal.init();
        CHECK(hal.is_connected(), "Connected after init");
        CHECK_EQ(static_cast<uint8_t>(hal.get_speed()), static_cast<uint8_t>(Speed::FULL),
                 "Default speed is FULL");

        hal.set_address(7);
        hal.set_feedback_ep(2);
        CHECK_EQ(hal.fb_ep, uint8_t{2}, "Feedback EP set");
        CHECK(hal.is_feedback_tx_ready(), "Feedback tx ready by default");

        hal.set_feedback_tx_flag();
        CHECK(hal.fb_tx_flag, "Feedback tx flag set");

        hal.ep0_prepare_rx(64);
        CHECK_EQ(hal.ep0_rx_len, uint16_t{64}, "EP0 rx len");

        uint8_t data[] = {0xAA, 0xBB};
        hal.ep_write(1, data, 2);
        CHECK_EQ(hal.ep_buf_len[1], uint16_t{2}, "EP1 write len");
        CHECK_EQ(hal.ep_buf[1][0], uint8_t{0xAA}, "EP1 data[0]");
    }

    TEST_SUMMARY();
}

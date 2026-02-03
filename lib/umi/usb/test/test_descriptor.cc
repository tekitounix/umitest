// SPDX-License-Identifier: MIT
// UMI-USB: Descriptor Builder Tests
#include <umitest.hh>
using namespace umitest;
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

// BOS + WinUSB + WebUSB
static constexpr auto winusb_compat = winusb::CompatibleIdWinUsb();
static_assert(winusb_compat.size == 20, "WinUSB CompatibleId = 20 bytes");

static constexpr auto webusb_cap = webusb::PlatformCapability(0x02, 1);
static_assert(webusb_cap.size == 24, "WebUSB PlatformCapability = 24 bytes");
static_assert(webusb_cap[0] == 24, "bLength");
static_assert(webusb_cap[2] == 0x05, "bDevCapabilityType = Platform");

static constexpr auto webusb_url = webusb::UrlDescriptor(webusb::SCHEME_HTTPS, "example.com");
static_assert(webusb_url.size == 3 + 11, "URL descriptor size");
static_assert(webusb_url[2] == webusb::SCHEME_HTTPS, "scheme");
static_assert(webusb_url[3] == 'e', "url[0]");

// UAC2 Feature Unit descriptor
static constexpr auto uac2_fu = audio::Uac2FeatureUnit<2>(
    6,           // unit_id
    2,           // source_id (Input Terminal)
    0x0000000F,  // master: Mute + Volume (host r/w)
    0x00000000,  // ch1: none
    0x00000000,  // ch2: none
    0            // iFeature
);
// bLength = 6 + (2+1)*4 = 18
static_assert(uac2_fu.size == 18, "UAC2 FU stereo = 18 bytes");
static_assert(uac2_fu[0] == 18, "bLength");
static_assert(uac2_fu[1] == dtype::CsInterface, "bDescriptorType");
static_assert(uac2_fu[2] == audio::ac2::FeatureUnit, "bDescriptorSubtype");
static_assert(uac2_fu[3] == 6, "bUnitID");
static_assert(uac2_fu[4] == 2, "bSourceID");
// bmaControls(0) = 0x0000000F in LE
static_assert(uac2_fu[5] == 0x0F, "bmaControls[0] byte0");
static_assert(uac2_fu[6] == 0x00, "bmaControls[0] byte1");

// Mono variant
static constexpr auto uac2_fu_mono = audio::Uac2FeatureUnit<1>(7, 4, 0x0000000F, 0x00000000);
static_assert(uac2_fu_mono.size == 14, "UAC2 FU mono = 14 bytes"); // 6 + (1+1)*4

// Device Qualifier descriptor
static constexpr auto dq_desc = DeviceQualifierDesc{
    .usb_version = 0x0200,
    .device_class = 0xEF,
    .device_subclass = 0x02,
    .device_protocol = 0x01,
}.build();
static_assert(dq_desc.size == 10, "Device Qualifier must be 10 bytes");
static_assert(dq_desc[0] == 10, "bLength");
static_assert(dq_desc[1] == dtype::DeviceQualifier, "bDescriptorType");
static_assert(dq_desc[9] == 0, "bReserved");

// SpeedTraits
static_assert(SpeedTraits<Speed::FULL>::FRAME_DIVISOR == 1000, "FS frame divisor");
static_assert(SpeedTraits<Speed::FULL>::FB_BYTES == 3, "FS feedback 3 bytes");
static_assert(SpeedTraits<Speed::FULL>::FB_SHIFT == 14, "FS 10.14 format");
static_assert(SpeedTraits<Speed::HIGH>::FRAME_DIVISOR == 8000, "HS frame divisor");
static_assert(SpeedTraits<Speed::HIGH>::FB_BYTES == 4, "HS feedback 4 bytes");
static_assert(SpeedTraits<Speed::HIGH>::FB_SHIFT == 16, "HS 16.16 format");
static_assert(SpeedTraits<Speed::HIGH>::FB_BINTERVAL == 4, "HS fb interval 2^3*125us=1ms");

// ============================================================================
// Hal concept static_assert
// ============================================================================

static_assert(Hal<StubHal>, "StubHal must satisfy Hal concept");

// ============================================================================
// Runtime tests
// ============================================================================

int main() {
    Suite s("usb_descriptor");

    s.section("Descriptor byte layout");
    {
        // Device descriptor runtime verification
        s.check_eq(dev_desc.data[0], uint8_t{18});
        s.check_eq(dev_desc.data[1], dtype::Device);
        s.check_eq(dev_desc.data[4], uint8_t{0xEF});
    }

    s.section("ConfigHeader");
    {
        constexpr auto cfg = ConfigHeader{.max_power = 250}.build<100, 3>();
        s.check_eq(cfg.data[0], uint8_t{9});
        s.check_eq(cfg.data[1], dtype::Configuration);
        // wTotalLength = 100 in LE
        s.check_eq(cfg.data[2], uint8_t{100});
        s.check_eq(cfg.data[3], uint8_t{0});
        s.check_eq(cfg.data[4], uint8_t{3});
        s.check_eq(cfg.data[8], uint8_t{250});
    }

    s.section("StubHal basic operations");
    {
        StubHal hal;
        hal.init();
        s.check(hal.is_connected(), "Connected after init");
        s.check_eq(static_cast<uint8_t>(hal.get_speed()), static_cast<uint8_t>(Speed::FULL));

        hal.set_address(7);
        hal.set_feedback_ep(2);
        s.check_eq(hal.fb_ep, uint8_t{2});
        s.check(hal.is_feedback_tx_ready(), "Feedback tx ready by default");

        hal.set_feedback_tx_flag();
        s.check(hal.fb_tx_flag, "Feedback tx flag set");

        hal.ep0_prepare_rx(64);
        s.check_eq(hal.ep0_rx_len, uint16_t{64});

        uint8_t data[] = {0xAA, 0xBB};
        hal.ep_write(1, data, 2);
        s.check_eq(hal.ep_buf_len[1], uint16_t{2});
        s.check_eq(hal.ep_buf[1][0], uint8_t{0xAA});
    }

    return s.summary();
}

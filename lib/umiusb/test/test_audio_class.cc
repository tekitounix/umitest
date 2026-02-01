// SPDX-License-Identifier: MIT
// UMI-USB: AudioClass Tests — UAC2 Feature Unit GET/SET CUR (Mute, Volume)
#include "test_common.hh"
#include "audio/audio_interface.hh"
#include "stub_hal.hh"

using namespace umiusb;

// Use UAC2 AudioClass with stereo 48kHz OUT
using TestAudioClass = AudioClass<UacVersion::UAC2, MaxSpeed::FULL, AudioStereo48k>;

int main() {
    SECTION("UAC2 Feature Unit GET CUR Mute (entity 6)");
    {
        TestAudioClass audio;
        audio.set_feature_defaults(true, -256, false, 0);

        // GET CUR Mute: bmRequestType=0xA1 (class, interface, IN), bRequest=0x01 (CUR)
        // wValue=0x0100 (CS=Mute, CN=0), wIndex=0x0600 (entity=6, iface=0)
        SetupPacket setup{};
        setup.bmRequestType = 0xA1; // IN | Class | Interface
        setup.bRequest = 0x01;      // CUR
        setup.wValue = 0x0100;      // CS=1 (Mute), CN=0
        setup.wIndex = 0x0600;      // entity=6, interface=0
        setup.wLength = 1;

        uint8_t buf[64]{};
        std::span<uint8_t> response(buf, sizeof(buf));
        bool handled = audio.handle_request(setup, response);
        CHECK(handled, "Feature Unit GET CUR Mute handled");
        CHECK_EQ(response.size(), size_t{1}, "Response is 1 byte");
        CHECK_EQ(response[0], uint8_t{1}, "Mute is ON");
    }

    SECTION("UAC2 Feature Unit GET CUR Volume (entity 6)");
    {
        TestAudioClass audio;
        audio.set_feature_defaults(false, -512, false, 0); // -2dB in 1/256 dB

        SetupPacket setup{};
        setup.bmRequestType = 0xA1;
        setup.bRequest = 0x01;      // CUR
        setup.wValue = 0x0200;      // CS=2 (Volume), CN=0
        setup.wIndex = 0x0600;      // entity=6
        setup.wLength = 2;

        uint8_t buf[64]{};
        std::span<uint8_t> response(buf, sizeof(buf));
        bool handled = audio.handle_request(setup, response);
        CHECK(handled, "Feature Unit GET CUR Volume handled");
        CHECK_EQ(response.size(), size_t{2}, "Response is 2 bytes");
        int16_t vol = static_cast<int16_t>(response[0] | (response[1] << 8));
        CHECK_EQ(vol, int16_t{-512}, "Volume is -512 (1/256 dB)");
    }

    SECTION("UAC2 Feature Unit GET RANGE Volume (entity 6)");
    {
        TestAudioClass audio;

        SetupPacket setup{};
        setup.bmRequestType = 0xA1;
        setup.bRequest = 0x02;      // RANGE
        setup.wValue = 0x0200;      // CS=2 (Volume)
        setup.wIndex = 0x0600;      // entity=6
        setup.wLength = 8;

        uint8_t buf[64]{};
        std::span<uint8_t> response(buf, sizeof(buf));
        bool handled = audio.handle_request(setup, response);
        CHECK(handled, "Feature Unit GET RANGE Volume handled");
        CHECK_EQ(response.size(), size_t{8}, "Response is 8 bytes");
        // wNumSubRanges = 1
        CHECK_EQ(response[0], uint8_t{1}, "wNumSubRanges = 1");
    }

    SECTION("UAC2 Clock Source GET CUR Frequency");
    {
        TestAudioClass audio;

        SetupPacket setup{};
        setup.bmRequestType = 0xA1;
        setup.bRequest = 0x01;      // CUR
        setup.wValue = 0x0100;      // CS=1 (SAM_FREQ)
        setup.wIndex = 0x0100;      // entity=1 (Clock Source)
        setup.wLength = 4;

        uint8_t buf[64]{};
        std::span<uint8_t> response(buf, sizeof(buf));
        bool handled = audio.handle_request(setup, response);
        CHECK(handled, "Clock Source GET CUR handled");
        CHECK_EQ(response.size(), size_t{4}, "Response is 4 bytes");
        uint32_t rate = response[0] | (response[1] << 8) | (response[2] << 16) | (response[3] << 24);
        CHECK_EQ(rate, uint32_t{48000}, "Sample rate is 48000");
    }

    SECTION("UAC2 Clock Source GET RANGE");
    {
        TestAudioClass audio;

        SetupPacket setup{};
        setup.bmRequestType = 0xA1;
        setup.bRequest = 0x02;      // RANGE
        setup.wValue = 0x0100;      // CS=1 (SAM_FREQ)
        setup.wIndex = 0x0100;      // entity=1 (Clock Source)
        setup.wLength = 64;

        uint8_t buf[64]{};
        std::span<uint8_t> response(buf, sizeof(buf));
        bool handled = audio.handle_request(setup, response);
        CHECK(handled, "Clock Source GET RANGE handled");
        CHECK(response.size() >= 14, "Response has at least 1 range entry");
        // wNumSubRanges >= 1
        uint16_t count = response[0] | (response[1] << 8);
        CHECK(count >= 1, "At least 1 rate range");
    }

    SECTION("UAC2 Selector Unit GET CUR (entity 8)");
    {
        TestAudioClass audio;

        SetupPacket setup{};
        setup.bmRequestType = 0xA1;
        setup.bRequest = 0x01;      // CUR
        setup.wValue = 0x0000;      // CS=0
        setup.wIndex = 0x0800;      // entity=8
        setup.wLength = 1;

        uint8_t buf[64]{};
        std::span<uint8_t> response(buf, sizeof(buf));
        bool handled = audio.handle_request(setup, response);
        CHECK(handled, "Selector Unit GET CUR handled");
        CHECK_EQ(response.size(), size_t{1}, "Response is 1 byte");
        CHECK_EQ(response[0], uint8_t{1}, "Default selection is 1");
    }

    SECTION("UAC2 Clock Selector GET CUR (entity 9)");
    {
        TestAudioClass audio;

        SetupPacket setup{};
        setup.bmRequestType = 0xA1;
        setup.bRequest = 0x01;      // CUR
        setup.wValue = 0x0000;
        setup.wIndex = 0x0900;      // entity=9
        setup.wLength = 1;

        uint8_t buf[64]{};
        std::span<uint8_t> response(buf, sizeof(buf));
        bool handled = audio.handle_request(setup, response);
        CHECK(handled, "Clock Selector GET CUR handled");
        CHECK_EQ(response.size(), size_t{1}, "Response is 1 byte");
        CHECK_EQ(response[0], uint8_t{1}, "Default clock selection is 1");
    }

    SECTION("UAC2 Mixer Unit GET CUR (entity 10, crosspoint 0)");
    {
        TestAudioClass audio;

        SetupPacket setup{};
        setup.bmRequestType = 0xA1;
        setup.bRequest = 0x01;      // CUR
        setup.wValue = 0x0000;      // CS=0, CN=0 (crosspoint 0)
        setup.wIndex = 0x0A00;      // entity=10
        setup.wLength = 2;

        uint8_t buf[64]{};
        std::span<uint8_t> response(buf, sizeof(buf));
        bool handled = audio.handle_request(setup, response);
        CHECK(handled, "Mixer Unit GET CUR handled");
        CHECK_EQ(response.size(), size_t{2}, "Response is 2 bytes");
        int16_t val = static_cast<int16_t>(response[0] | (response[1] << 8));
        CHECK_EQ(val, int16_t{0}, "Default mixer gain is 0");
    }

    SECTION("UAC2 Mixer Unit SET CUR (entity 10, crosspoint 2)");
    {
        TestAudioClass audio;

        // SET CUR: setup phase
        SetupPacket setup{};
        setup.bmRequestType = 0x21;  // OUT | Class | Interface
        setup.bRequest = 0x01;       // CUR
        setup.wValue = 0x0002;       // CS=0, CN=2 (crosspoint 2)
        setup.wIndex = 0x0A00;       // entity=10
        setup.wLength = 2;

        uint8_t buf[64]{};
        std::span<uint8_t> response(buf, sizeof(buf));
        bool handled = audio.handle_request(setup, response);
        CHECK(handled, "Mixer Unit SET CUR setup handled");

        // Data phase: set gain to -128 (1/256 dB)
        uint8_t data[2] = {0x80, 0xFF};  // -128 as int16_t
        audio.on_ep0_rx(std::span<const uint8_t>(data, 2));

        // Verify by GET CUR
        SetupPacket get_setup{};
        get_setup.bmRequestType = 0xA1;
        get_setup.bRequest = 0x01;
        get_setup.wValue = 0x0002;   // CN=2
        get_setup.wIndex = 0x0A00;
        get_setup.wLength = 2;

        std::span<uint8_t> get_response(buf, sizeof(buf));
        handled = audio.handle_request(get_setup, get_response);
        CHECK(handled, "Mixer Unit GET CUR after SET handled");
        int16_t val = static_cast<int16_t>(get_response[0] | (get_response[1] << 8));
        CHECK_EQ(val, int16_t{-128}, "Mixer gain is -128 after SET");
    }

    SECTION("BOS descriptor contains WinUSB and WebUSB capabilities");
    {
        TestAudioClass audio;
        auto bos = audio.bos_descriptor();
        CHECK(bos.size() == 57, "BOS descriptor is 57 bytes");
        CHECK_EQ(bos[0], uint8_t{5}, "BOS header bLength = 5");
        CHECK_EQ(bos[1], uint8_t{0x0F}, "BOS bDescriptorType = 0x0F");
        CHECK_EQ(bos[4], uint8_t{2}, "bNumDeviceCaps = 2");
        // WinUSB Platform Capability at offset 5
        CHECK_EQ(bos[5], uint8_t{28}, "WinUSB cap bLength = 28");
        CHECK_EQ(bos[6], uint8_t{0x10}, "WinUSB cap type = DeviceCapability");
        CHECK_EQ(bos[7], uint8_t{0x05}, "WinUSB cap subtype = Platform");
        // WebUSB Platform Capability at offset 33
        CHECK_EQ(bos[33], uint8_t{24}, "WebUSB cap bLength = 24");
        CHECK_EQ(bos[34], uint8_t{0x10}, "WebUSB cap type = DeviceCapability");
    }

    SECTION("WinUSB vendor request returns MS OS 2.0 descriptor set");
    {
        TestAudioClass audio;

        SetupPacket setup{};
        setup.bmRequestType = 0xC0;  // IN | Vendor | Device
        setup.bRequest = 0x01;       // WINUSB_VENDOR_CODE
        setup.wValue = 0x0000;
        setup.wIndex = 0x0007;       // MS_OS_20_DESCRIPTOR_INDEX
        setup.wLength = 64;

        uint8_t buf[192]{};
        std::span<uint8_t> response(buf, sizeof(buf));
        bool handled = audio.handle_vendor_request(setup, response);
        CHECK(handled, "WinUSB vendor request handled");
        CHECK(response.size() > 10, "MS OS 2.0 desc set has content");
        // Descriptor Set Header check
        uint16_t hdr_len = response[0] | (response[1] << 8);
        CHECK_EQ(hdr_len, uint16_t{10}, "Descriptor set header length = 10");
    }

    SECTION("WebUSB vendor request returns URL descriptor");
    {
        TestAudioClass audio;

        SetupPacket setup{};
        setup.bmRequestType = 0xC0;
        setup.bRequest = 0x02;       // WEBUSB_VENDOR_CODE
        setup.wValue = 0x0000;
        setup.wIndex = 0x0002;       // GET_URL
        setup.wLength = 64;

        uint8_t buf[64]{};
        std::span<uint8_t> response(buf, sizeof(buf));
        bool handled = audio.handle_vendor_request(setup, response);
        CHECK(handled, "WebUSB vendor request handled");
        CHECK(response.size() > 3, "URL descriptor has content");
        CHECK_EQ(response[1], uint8_t{0x03}, "bDescriptorType = WEBUSB_URL");
        CHECK_EQ(response[2], uint8_t{0x01}, "bScheme = HTTPS");
    }

    TEST_SUMMARY();
}

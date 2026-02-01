// SPDX-License-Identifier: MIT
// UMI-USB: UsbMidiClass Tests
#include "test_common.hh"
#include "midi/usb_midi_class.hh"
#include "stub_hal.hh"

using namespace umiusb;

// Verify UsbMidiClass satisfies Class concept
using TestMidiClass = UsbMidiClass<MidiPort<1, 3>, MidiPort<1, 3>>;

static_assert(requires(TestMidiClass& c, const SetupPacket& s) {
    { c.config_descriptor() } -> std::convertible_to<std::span<const uint8_t>>;
    { c.on_configured(bool{}) } -> std::same_as<void>;
    { c.handle_request(s, std::declval<std::span<uint8_t>&>()) } -> std::convertible_to<bool>;
    { c.on_rx(uint8_t{}, std::declval<std::span<const uint8_t>>()) } -> std::same_as<void>;
    { c.bos_descriptor() } -> std::convertible_to<std::span<const uint8_t>>;
    { c.handle_vendor_request(s, std::declval<std::span<uint8_t>&>()) } -> std::convertible_to<bool>;
}, "UsbMidiClass must satisfy Class concept");

int main() {
    SECTION("UsbMidiClass descriptor build");
    {
        TestMidiClass midi;
        midi.build_descriptor(0, 1);

        auto desc = midi.config_descriptor();
        CHECK(desc.size() > 9, "Descriptor has content");

        // First 9 bytes = Configuration descriptor header
        CHECK_EQ(desc[0], uint8_t{9}, "Config bLength");
        CHECK_EQ(desc[1], uint8_t{0x02}, "Config bDescriptorType");
        // wTotalLength should match span size
        uint16_t total = static_cast<uint16_t>(desc[2]) | (static_cast<uint16_t>(desc[3]) << 8);
        CHECK_EQ(total, static_cast<uint16_t>(desc.size()), "wTotalLength matches");
        CHECK_EQ(desc[4], uint8_t{2}, "bNumInterfaces = 2");
    }

    SECTION("UsbMidiClass MIDI rx callback");
    {
        TestMidiClass midi;
        midi.build_descriptor(0, 1);
        midi.on_configured(true);

        // Track MIDI callback
        static uint8_t last_cable = 0xFF;
        static uint8_t last_len = 0;
        static uint8_t last_data[3] = {};
        midi.set_midi_callback([](uint8_t cable, const uint8_t* data, uint8_t len) {
            last_cable = cable;
            last_len = len;
            for (uint8_t i = 0; i < len && i < 3; ++i) last_data[i] = data[i];
        });

        // Simulate receiving a Note On message (CIN=0x09, cable 0)
        uint8_t packet[] = {0x09, 0x90, 0x3C, 0x7F};  // Note On, C4, velocity 127
        midi.on_rx(3, std::span<const uint8_t>(packet, 4));

        CHECK_EQ(last_cable, uint8_t{0}, "MIDI callback cable = 0");
        CHECK_EQ(last_len, uint8_t{3}, "MIDI callback len = 3");
        CHECK_EQ(last_data[0], uint8_t{0x90}, "Note On status");
        CHECK_EQ(last_data[1], uint8_t{0x3C}, "Note number C4");
        CHECK_EQ(last_data[2], uint8_t{0x7F}, "Velocity 127");
    }

    SECTION("UsbMidiClass configure_endpoints");
    {
        TestMidiClass midi;
        StubHal hal;
        hal.init();
        midi.configure_endpoints(hal);

        CHECK_EQ(hal.num_configured_eps, uint8_t{2}, "2 EPs configured (OUT + IN)");
    }

    SECTION("UsbMidiClass send_midi");
    {
        TestMidiClass midi;
        midi.build_descriptor(0, 1);
        midi.on_configured(true);

        StubHal hal;
        hal.init();

        uint8_t msg[] = {0x90, 0x3C, 0x7F};  // Note On
        bool sent = midi.send_midi(hal, 0, msg, 3);
        CHECK(sent, "send_midi returns true");
        CHECK_EQ(hal.ep_buf_len[3], uint16_t{4}, "4 bytes written to EP");
        CHECK_EQ(hal.ep_buf[3][0], uint8_t{0x09}, "Header: cable 0, CIN 0x09");
        CHECK_EQ(hal.ep_buf[3][1], uint8_t{0x90}, "Status");
    }

    SECTION("ump_word_count returns correct sizes");
    {
        CHECK_EQ(ump_word_count(0), uint8_t{1}, "MT=0 Utility: 1 word");
        CHECK_EQ(ump_word_count(1), uint8_t{1}, "MT=1 System: 1 word");
        CHECK_EQ(ump_word_count(2), uint8_t{1}, "MT=2 MIDI1.0 CV: 1 word");
        CHECK_EQ(ump_word_count(3), uint8_t{2}, "MT=3 SysEx7: 2 words");
        CHECK_EQ(ump_word_count(4), uint8_t{2}, "MT=4 MIDI2.0 CV: 2 words");
        CHECK_EQ(ump_word_count(5), uint8_t{4}, "MT=5 SysEx8: 4 words");
    }

    SECTION("MIDI 2.0 Alt Setting switch");
    {
        TestMidiClass midi;
        midi.build_descriptor(0, 1);
        StubHal hal;
        hal.init();

        CHECK_EQ(static_cast<uint8_t>(midi.active_version()), uint8_t{0}, "Default MIDI 1.0");

        midi.set_interface(hal, 1, 1);  // Alt Setting 1 = MIDI 2.0
        CHECK_EQ(static_cast<uint8_t>(midi.active_version()), uint8_t{1}, "Switched to MIDI 2.0");

        midi.set_interface(hal, 1, 0);  // Back to Alt Setting 0
        CHECK_EQ(static_cast<uint8_t>(midi.active_version()), uint8_t{0}, "Back to MIDI 1.0");
    }

    SECTION("MIDI 2.0 UMP native receive");
    {
        TestMidiClass midi;
        midi.build_descriptor(0, 1);
        StubHal hal;
        hal.init();

        // Switch to MIDI 2.0 mode
        midi.set_interface(hal, 1, 1);

        static uint16_t rx_len = 0;
        static uint8_t rx_data[16]{};
        midi.set_ump_rx_callback([](const uint8_t* data, uint16_t len) {
            rx_len = len;
            for (uint16_t i = 0; i < len && i < 16; ++i) rx_data[i] = data[i];
        });

        // MT=2 (MIDI 1.0 CV) = 1 word = 4 bytes, little-endian
        // UMP32: 0x20903C7F (MT=2, group=0, status=0x90, note=0x3C, vel=0x7F)
        uint8_t ump_data[] = {0x7F, 0x3C, 0x90, 0x20};
        midi.on_rx(3, std::span<const uint8_t>(ump_data, 4));  // EP_MIDI_OUT = 3

        CHECK_EQ(rx_len, uint16_t{4}, "UMP callback received 4 bytes");
        CHECK_EQ(rx_data[3], uint8_t{0x20}, "MT=2 header byte");
    }

    TEST_SUMMARY();
}
